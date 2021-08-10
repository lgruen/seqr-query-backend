# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: scan_service.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from google.protobuf import reflection as _reflection
from google.protobuf import symbol_database as _symbol_database
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()




DESCRIPTOR = _descriptor.FileDescriptor(
  name='scan_service.proto',
  package='cpg',
  syntax='proto3',
  serialized_options=None,
  create_key=_descriptor._internal_create_key,
  serialized_pb=b'\n\x12scan_service.proto\x12\x03\x63pg\" \n\x0bLoadRequest\x12\x11\n\tblob_path\x18\x01 \x03(\t\"\x0b\n\tLoadReply29\n\x0bScanService\x12*\n\x04Load\x12\x10.cpg.LoadRequest\x1a\x0e.cpg.LoadReply\"\x00\x62\x06proto3'
)




_LOADREQUEST = _descriptor.Descriptor(
  name='LoadRequest',
  full_name='cpg.LoadRequest',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  create_key=_descriptor._internal_create_key,
  fields=[
    _descriptor.FieldDescriptor(
      name='blob_path', full_name='cpg.LoadRequest.blob_path', index=0,
      number=1, type=9, cpp_type=9, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=27,
  serialized_end=59,
)


_LOADREPLY = _descriptor.Descriptor(
  name='LoadReply',
  full_name='cpg.LoadReply',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  create_key=_descriptor._internal_create_key,
  fields=[
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=61,
  serialized_end=72,
)

DESCRIPTOR.message_types_by_name['LoadRequest'] = _LOADREQUEST
DESCRIPTOR.message_types_by_name['LoadReply'] = _LOADREPLY
_sym_db.RegisterFileDescriptor(DESCRIPTOR)

LoadRequest = _reflection.GeneratedProtocolMessageType('LoadRequest', (_message.Message,), {
  'DESCRIPTOR' : _LOADREQUEST,
  '__module__' : 'scan_service_pb2'
  # @@protoc_insertion_point(class_scope:cpg.LoadRequest)
  })
_sym_db.RegisterMessage(LoadRequest)

LoadReply = _reflection.GeneratedProtocolMessageType('LoadReply', (_message.Message,), {
  'DESCRIPTOR' : _LOADREPLY,
  '__module__' : 'scan_service_pb2'
  # @@protoc_insertion_point(class_scope:cpg.LoadReply)
  })
_sym_db.RegisterMessage(LoadReply)



_SCANSERVICE = _descriptor.ServiceDescriptor(
  name='ScanService',
  full_name='cpg.ScanService',
  file=DESCRIPTOR,
  index=0,
  serialized_options=None,
  create_key=_descriptor._internal_create_key,
  serialized_start=74,
  serialized_end=131,
  methods=[
  _descriptor.MethodDescriptor(
    name='Load',
    full_name='cpg.ScanService.Load',
    index=0,
    containing_service=None,
    input_type=_LOADREQUEST,
    output_type=_LOADREPLY,
    serialized_options=None,
    create_key=_descriptor._internal_create_key,
  ),
])
_sym_db.RegisterServiceDescriptor(_SCANSERVICE)

DESCRIPTOR.services_by_name['ScanService'] = _SCANSERVICE

# @@protoc_insertion_point(module_scope)
