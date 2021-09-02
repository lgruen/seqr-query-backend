# Base image with libraries that are rarely changing, but slow to compile.

FROM debian:bullseye-slim AS base

RUN apt update && \
    apt install --no-install-recommends -y \
    apt-transport-https \
    apt-utils \
    automake \
    ca-certificates \
    ccache \
    cmake \
    clang \
    curl \
    git \
    libc-ares-dev \
    libc-ares2 \
    libcurl4-openssl-dev \
    libgoogle-perftools-dev \
    libgtest-dev \
    libre2-dev \
    libssl-dev \
    lldb \
    m4 \
    make \
    pkg-config \
    tar \
    wget \
    zlib1g-dev

ARG CMAKE_BUILD_TYPE=Release

RUN mkdir -p /build/abseil-cpp && cd /build/abseil-cpp && \
    curl -sSL https://github.com/abseil/abseil-cpp/archive/refs/tags/20210324.2.tar.gz | tar -xzf - --strip=1 && \
    mkdir build && cd build && \
    cmake .. \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_CXX_STANDARD=20 \
    -DBUILD_TESTING=OFF \
    -DBUILD_SHARED_LIBS=yes && \
    make -j8 install

RUN mkdir -p /build/protobuf && cd /build/protobuf && \
    curl -sSL https://github.com/google/protobuf/archive/v3.17.3.tar.gz | tar -xzf - --strip=1 && \
    mkdir build && cd build && \
    cmake ../cmake \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_CXX_STANDARD=20 \
    -DBUILD_SHARED_LIBS=yes \
    -Dprotobuf_BUILD_TESTS=OFF && \
    make -j8 install && \
    ldconfig

RUN mkdir -p /build/grpc && cd /build/grpc && \
    curl -sSL https://github.com/grpc/grpc/archive/refs/tags/v1.39.0-pre1.tar.gz| tar -xzf - --strip=1 && \
    mkdir build && cd build && \
    cmake .. \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_CXX_STANDARD=20 \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DgRPC_ABSL_PROVIDER=package \
    -DgRPC_CARES_PROVIDER=package \
    -DgRPC_PROTOBUF_PROVIDER=package \
    # https://github.com/faaxm/exmpl-cmake-grpc/issues/1#issuecomment-873905356
    -DgRPC_PROTOBUF_PACKAGE_TYPE=CONFIG \
    -DgRPC_RE2_PROVIDER=package \
    -DgRPC_SSL_PROVIDER=package \
    -DgRPC_ZLIB_PROVIDER=package && \
    make -j8 install && \
    ldconfig

RUN mkdir -p /build/crc32c && cd /build/crc32c && \
    curl -sSL https://github.com/google/crc32c/archive/refs/tags/1.1.1.tar.gz | tar -xzf - --strip=1 && \
    mkdir build && cd build && \
    cmake .. \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_CXX_STANDARD=20 \
    -DBUILD_SHARED_LIBS=yes \
    -DCRC32C_BUILD_TESTS=OFF \
    -DCRC32C_BUILD_BENCHMARKS=OFF \
    -DCRC32C_USE_GLOG=OFF && \
    make -j8 install && \
    ldconfig

RUN mkdir -p /build/json && cd /build/json && \
    curl -sSL https://github.com/nlohmann/json/archive/refs/tags/v3.9.1.tar.gz | tar -xzf - --strip=1 && \
    mkdir build && cd build && \
    cmake .. \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_CXX_STANDARD=20 \
    -DBUILD_SHARED_LIBS=yes \
    -DBUILD_TESTING=OFF && \
    make -j8 install && \
    ldconfig

RUN mkdir -p /build/google-cloud-cpp && cd /build/google-cloud-cpp && \
    curl -sSL https://github.com/googleapis/google-cloud-cpp/archive/refs/tags/v1.29.0.tar.gz | tar -xzf - --strip=1 && \
    mkdir build && cd build && \
    cmake .. \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_CXX_STANDARD=20 \
    -DBUILD_TESTING=OFF \
    -DGOOGLE_CLOUD_CPP_ENABLE_EXAMPLES=OFF && \
    make -j8 install && \
    ldconfig

RUN mkdir -p /build/arrow && cd /build/arrow && \
    curl -sSL https://github.com/apache/arrow/archive/refs/tags/apache-arrow-5.0.0.tar.gz | tar -xzf - --strip=1 && \
    mkdir build && cd build && \
    cmake ../cpp \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    # https://issues.apache.org/jira/browse/ARROW-13256.
    -DCMAKE_CXX_STANDARD=11 \
    -DARROW_BUILD_STATIC=ON \
    -DARROW_COMPUTE=ON \
    -DARROW_DATASET=ON \
    -DARROW_PARQUET=ON \
    -DARROW_WITH_BZ2=ON \
    -DARROW_WITH_ZLIB=ON \
    -DARROW_WITH_LZ4=ON \
    -DARROW_WITH_SNAPPY=ON \
    -DARROW_WITH_ZSTD=ON \
    -DARROW_WITH_BROTLI=ON && \
    make -j8 install && \
    ldconfig

# extract-elf-so tars .so files to create small Docker images.
RUN curl -sSL -o /build/extract-elf-so https://github.com/William-Yeh/extract-elf-so/releases/download/v0.6/extract-elf-so_static_linux-amd64 && \
    chmod +x /build/extract-elf-so

FROM base AS server

COPY src /src

RUN mkdir -p /src/build && cd /src/build && \
    cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=20 && \
    make -j8 && \
    make test CTEST_OUTPUT_ON_FAILURE=1

FROM server AS extract

RUN /build/extract-elf-so --cert /src/build/server/server && \
    mkdir /rootfs && cd /rootfs && \
    tar xf /rootfs.tar

FROM debian:bullseye-slim AS slim

COPY --from=extract /rootfs /

RUN ldconfig