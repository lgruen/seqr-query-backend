#include <absl/base/thread_annotations.h>
#include <absl/container/flat_hash_set.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/strip.h>
#include <absl/synchronization/blocking_counter.h>
#include <absl/synchronization/mutex.h>
#include <absl/time/time.h>
#include <arrow/compute/exec.h>
#include <arrow/dataset/dataset.h>
#include <arrow/dataset/scanner.h>
#include <arrow/io/memory.h>
#include <arrow/ipc/options.h>
#include <arrow/ipc/reader.h>
#include <google/cloud/storage/client.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <cassert>
#include <cstddef>
#include <functional>
#include <iostream>
#include <optional>
#include <queue>
#include <string_view>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "seqr_query_service.grpc.pb.h"

namespace {

constexpr size_t kNumThreadPoolWorkers = 32;

// Adapted from the Abseil thread pool.
class ThreadPool {
 public:
  explicit ThreadPool(const int num_threads) {
    for (int i = 0; i < num_threads; ++i) {
      threads_.push_back(std::thread(&ThreadPool::WorkLoop, this));
    }
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  ~ThreadPool() {
    {
      absl::MutexLock l(&mu_);
      for (size_t i = 0; i < threads_.size(); i++) {
        queue_.push(nullptr);  // Shutdown signal.
      }
    }
    for (auto& t : threads_) {
      t.join();
    }
  }

  // Schedule a function to be run on a ThreadPool thread immediately.
  void Schedule(std::function<void()> func) {
    assert(func != nullptr);
    absl::MutexLock l(&mu_);
    queue_.push(std::move(func));
  }

 private:
  bool WorkAvailable() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return !queue_.empty();
  }

  void WorkLoop() {
    while (true) {
      std::function<void()> func;
      {
        absl::MutexLock l(&mu_);
        mu_.Await(absl::Condition(this, &ThreadPool::WorkAvailable));
        func = std::move(queue_.front());
        queue_.pop();
      }
      if (func == nullptr) {  // Shutdown signal.
        break;
      }
      func();
    }
  }

  absl::Mutex mu_;
  std::queue<std::function<void()>> queue_ ABSL_GUARDED_BY(mu_);
  std::vector<std::thread> threads_;
};

class UrlReader {
 public:
  virtual ~UrlReader() = default;

  virtual absl::StatusOr<std::string> Read(absl::string_view url) const = 0;
};

// Returns the bucket and file name from a complete gs:// path.
absl::StatusOr<std::pair<std::string_view, std::string_view>> SplitBlobPath(
    std::string_view blob_path) {
  if (!absl::ConsumePrefix(&blob_path, "gs://")) {
    return absl::InvalidArgumentError("Missing gs:// prefix");
  }
  const size_t slash_pos = blob_path.find_first_of('/');
  if (slash_pos == std::string_view::npos) {
    return absl::InvalidArgumentError("Incomplete blob path");
  }
  return std::make_pair(blob_path.substr(0, slash_pos),
                        blob_path.substr(slash_pos + 1));
}

namespace gcs = google::cloud::storage;

class GcsReader : public UrlReader {
 public:
  absl::StatusOr<std::string> Read(const absl::string_view url) const override {
    if (!url.starts_with("gs://")) {
      return absl::InvalidArgumentError(absl::StrCat("Unsupported URL: ", url));
    }

    // Make a copy of the GCS client for thread-safety.
    gcs::Client gcs_client = shared_gcs_client_;
    const auto& split_path = SplitBlobPath(url);
    if (!split_path.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid path", split_path.status().message()));
    }

    try {
      const absl::Time start_time = absl::Now();
      auto reader = gcs_client.ReadObject(std::string(split_path->first),
                                          std::string(split_path->second));
      if (reader.bad()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Failed to read blob: ", reader.status().message()));
      }

      std::optional<int> content_length;
      for (const auto& header : reader.headers()) {
        if (header.first == "content-length") {
          int value = 0;
          if (!absl::SimpleAtoi(header.second, &value)) {
            return absl::NotFoundError(
                "Couldn't parse content-length header value");
          }
          content_length = value;
        }
      }
      if (!content_length) {
        return absl::NotFoundError("Couldn't find content-length header");
      }

      std::string result(*content_length, 0);
      reader.read(result.data(), *content_length);
      if (reader.bad()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Failed to read blob: ", reader.status().message()));
      }
      const absl::Time end_time = absl::Now();
      std::cout << "Read " << url << " (" << *content_length << "B) in "
                << (end_time - start_time) << std::endl;
      return result;
    } catch (const std::exception& e) {
      // Unfortunately the googe-cloud-storage library throws exceptions.
      return absl::InternalError(
          absl::StrCat("Exception during reading of ", url, ": ", e.what()));
    }
  }

 private:
  // Share connection pool, but need to make copies for thread-safety.
  gcs::Client shared_gcs_client_{
      google::cloud::Options{}.set<gcs::ConnectionPoolSizeOption>(
          kNumThreadPoolWorkers)};
};

// Returns an Arrow compute expression from the protobuf specification.
absl::StatusOr<arrow::compute::Expression> BuildFilterExpression(
    const seqr::Expression& filter_expression) {
  namespace cp = arrow::compute;
  switch (filter_expression.type_case()) {
    case seqr::Expression::TYPE_NOT_SET:
      return absl::InvalidArgumentError("Expression type not set");
    case seqr::Expression::kColumn:
      return cp::field_ref(filter_expression.column());
    case seqr::Expression::kLiteral: {
      const auto& literal = filter_expression.literal();
      switch (literal.type_case()) {
        case seqr::Expression::Literal::TYPE_NOT_SET:
          return absl::InvalidArgumentError("Literal type not set");
        case seqr::Expression::Literal::kBoolVal:
          return cp::literal(literal.bool_val());
        case seqr::Expression::Literal::kInt32Val:
          return cp::literal(literal.int32_val());
        case seqr::Expression::Literal::kInt64Val:
          return cp::literal(literal.int64_val());
        case seqr::Expression::Literal::kFloatVal:
          return cp::literal(literal.float_val());
        case seqr::Expression::Literal::kDoubleVal:
          return cp::literal(literal.double_val());
        case seqr::Expression::Literal::kStringVal:
          return cp::literal(literal.string_val());
      }
    }
    case seqr::Expression::kCall: {
      const auto& call = filter_expression.call();
      std::vector<cp::Expression> arguments;
      arguments.reserve(call.arguments_size());
      for (const auto& argument : call.arguments()) {
        auto expression = BuildFilterExpression(argument);
        if (!expression.ok()) {
          return expression.status();
        }
        arguments.push_back(*std::move(expression));
      }
      return cp::call(call.function_name(), std::move(arguments));
    }
  }
}

struct ScannerOptions {
  std::vector<std::string> projection_columns;
  arrow::compute::Expression filter_expression;
};

absl::StatusOr<ScannerOptions> BuildScannerOptions(
    const seqr::QueryRequest& request) {
  auto filter_expression = BuildFilterExpression(request.filter_expression());
  if (!filter_expression.ok()) {
    return filter_expression.status();
  }

  absl::flat_hash_set<std::string> projection_columns;
  for (const auto& projection_column : request.projection_columns()) {
    projection_columns.insert(projection_column);
  }

  for (const auto& sort_column : request.sort_columns()) {
    projection_columns.insert(sort_column);
  }

  return ScannerOptions{{projection_columns.begin(), projection_columns.end()},
                        *std::move(filter_expression)};
}

absl::StatusOr<size_t> ProcessArrowUrl(const UrlReader& url_reader,
                                       const std::string_view url,
                                       const ScannerOptions& scanner_options) {
  const auto data = url_reader.Read(url);
  if (!data.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to read ", url, ": ", data.status().message()));
  }

  arrow::ipc::IpcReadOptions ipc_read_options;
  // We parallelize over URLs already, no need for nested parallelism.
  ipc_read_options.use_threads = false;

  arrow::io::BufferReader buffer_reader{*data};
  auto record_batch_file_reader =
      arrow::ipc::RecordBatchFileReader::Open(&buffer_reader, ipc_read_options);
  if (!record_batch_file_reader.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to open record batch reader for ", url, ": ",
                     record_batch_file_reader.status().ToString()));
  }

  const auto schema = (*record_batch_file_reader)->schema();

  const int num_record_batches =
      (*record_batch_file_reader)->num_record_batches();
  arrow::RecordBatchVector record_batch_vector;
  record_batch_vector.reserve(num_record_batches);
  for (int i = 0; i < num_record_batches; ++i) {
    auto record_batch = (*record_batch_file_reader)->ReadRecordBatch(i);
    if (!record_batch.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to read record batch ", i, " for ", url, ": ",
                       record_batch.status().ToString()));
    }
    record_batch_vector.push_back(std::move(*record_batch));
  }

  auto in_memory_dataset = std::make_shared<arrow::dataset::InMemoryDataset>(
      schema, std::move(record_batch_vector));
  auto scanner_builder = in_memory_dataset->NewScan();
  if (!scanner_builder.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to create scanner builder for ", url, ": ",
                     scanner_builder.status().ToString()));
  }

  if (const auto status =
          (*scanner_builder)->Project(scanner_options.projection_columns);
      !status.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to set projection columns for ", url, ": ", status.ToString()));
  }

  if (const auto status =
          (*scanner_builder)->Filter(scanner_options.filter_expression);
      !status.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to set scanner filter for ", url, ": ", status.ToString()));
  }

  // We parallelize over URLs already, no need for nested parallelism.
  if (const auto status = (*scanner_builder)->UseThreads(false); !status.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to disable scanner threads for ", url, ": ",
                     status.ToString()));
  }

  const auto scanner = (*scanner_builder)->Finish();
  if (!scanner.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to create scanner for ", url, ": ",
                     scanner.status().ToString()));
  }

  size_t num_rows = 0;  // Count after filtering.
  if (const auto status = (*scanner)->Scan(
          [&num_rows](
              const arrow::dataset::TaggedRecordBatch tagged_record_batch) {
            const auto record_batch = tagged_record_batch.record_batch;
            num_rows += record_batch->num_rows();
            return arrow::Status::OK();
          });
      !status.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to run scanner on ", url, ": ", status.ToString()));
  }

  return num_rows;
}

class QueryServiceImpl final : public seqr::QueryService::Service {
 public:
  QueryServiceImpl(const UrlReader& url_reader) : url_reader_(url_reader) {}

 private:
  grpc::Status Query(grpc::ServerContext* const context,
                     const seqr::QueryRequest* const request,
                     seqr::QueryResponse* const response) override {
    const auto scanner_options = BuildScannerOptions(*request);
    if (!scanner_options.ok()) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          std::string(scanner_options.status().message()));
    }

    const size_t num_arrow_urls = request->arrow_urls_size();
    std::vector<absl::StatusOr<size_t>> results(num_arrow_urls);
    absl::BlockingCounter blocking_counter(num_arrow_urls);
    for (size_t i = 0; i < num_arrow_urls; ++i) {
      thread_pool_.Schedule(
          [&url_reader = url_reader_, &url = request->arrow_urls(i),
           &result = results[i], &scanner_options, &blocking_counter] {
            result = ProcessArrowUrl(url_reader, url, *scanner_options);
            blocking_counter.DecrementCount();
          });
    }

    blocking_counter.Wait();

    size_t num_results = 0;
    for (const auto& result : results) {
      if (!result.ok()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            std::string(result.status().message()));
      }
      num_results += *result;
    }

    response->set_num_results(num_results);

    return grpc::Status::OK;
  }

  ThreadPool thread_pool_{kNumThreadPoolWorkers};
  const UrlReader& url_reader_;
};

void RunServer(const int port) {
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  const std::string server_address = absl::StrCat("[::]:", port);
  std::cout << "Starting server on " << server_address << std::endl;
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

  GcsReader gcs_reader;
  QueryServiceImpl query_service(gcs_reader);
  builder.RegisterService(&query_service);

  builder.BuildAndStart()->Wait();
}

}  // namespace

int main(int argc, char** argv) {
  // Get the port number from the environment.
  const char* const port_env = std::getenv("PORT");
  if (port_env == nullptr) {
    std::cerr << "PORT environment variable not set" << std::endl;
    return 1;
  }

  int port = 0;
  if (!absl::SimpleAtoi(port_env, &port)) {
    std::cerr << "Failed to parse PORT environment variable" << std::endl;
    return 1;
  }

  RunServer(port);

  return 0;
}
