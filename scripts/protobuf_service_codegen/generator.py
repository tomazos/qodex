from __future__ import annotations

from dataclasses import dataclass
from functools import cached_property
from pathlib import Path, PurePosixPath

from google.protobuf.compiler import plugin_pb2
from google.protobuf.descriptor_pb2 import DescriptorProto, FileDescriptorProto
from jinja2 import Environment, FileSystemLoader

from .model import GeneratedFileModel, MethodModel, ServiceModel, proto_to_generated_header_name, sanitize_identifier


@dataclass(frozen=True)
class _MessageTypeInfo:
    cpp_type: str
    include_path: str


class ProtobufRpcCppGenerator:
    def generate(self, request: plugin_pb2.CodeGeneratorRequest) -> plugin_pb2.CodeGeneratorResponse:
        files_by_name = {proto.name: proto for proto in request.proto_file}
        type_registry = _build_message_type_registry(request.proto_file)

        response = plugin_pb2.CodeGeneratorResponse()
        for proto_name in request.file_to_generate:
            file_descriptor = files_by_name.get(proto_name)
            if file_descriptor is None:
                response.error = f"Missing FileDescriptorProto for {proto_name}"
                return response

            file_model = self._build_file_model(file_descriptor, type_registry)
            if not file_model.services:
                continue

            output_file = response.file.add()
            output_file.name = file_model.output_name
            output_file.content = self._render_template("cpp_service_header.j2", file=file_model)

        return response

    def _build_file_model(
        self,
        file_descriptor: FileDescriptorProto,
        type_registry: dict[str, _MessageTypeInfo],
    ) -> GeneratedFileModel:
        services = []
        include_paths = set()
        for service_descriptor in file_descriptor.service:
            methods = []
            for method_descriptor in service_descriptor.method:
                request_info = type_registry[method_descriptor.input_type]
                response_info = type_registry[method_descriptor.output_type]
                include_paths.add(request_info.include_path)
                include_paths.add(response_info.include_path)
                trait_name = sanitize_identifier(method_descriptor.name)
                methods.append(
                    MethodModel(
                        name=method_descriptor.name,
                        trait_name=trait_name,
                        request_cpp_type=request_info.cpp_type,
                        response_cpp_type=response_info.cpp_type,
                        request_handler_name=f"on{trait_name}Request",
                        response_handler_name=f"on{trait_name}Response",
                    )
                )

            cpp_package_namespace = "::".join(part for part in file_descriptor.package.split(".") if part)
            if cpp_package_namespace:
                generated_namespace = f"{cpp_package_namespace}::rpc::{sanitize_identifier(service_descriptor.name)}"
            else:
                generated_namespace = f"rpc::{sanitize_identifier(service_descriptor.name)}"

            full_name = service_descriptor.name
            if file_descriptor.package:
                full_name = f"{file_descriptor.package}.{full_name}"

            services.append(
                ServiceModel(
                    name=service_descriptor.name,
                    full_name=full_name,
                    generated_namespace=generated_namespace,
                    methods=tuple(methods),
                )
            )

        return GeneratedFileModel(
            proto_name=file_descriptor.name,
            output_name=proto_to_generated_header_name(file_descriptor.name),
            include_paths=tuple(sorted(include_paths)),
            services=tuple(services),
        )

    @cached_property
    def _template_environment(self) -> Environment:
        return Environment(
            loader=FileSystemLoader(Path(__file__).with_name("templates")),
            autoescape=False,
            keep_trailing_newline=True,
            trim_blocks=True,
            lstrip_blocks=True,
        )

    def _render_template(self, template_name: str, **context: object) -> str:
        return self._template_environment.get_template(template_name).render(**context)


def _build_message_type_registry(
    file_descriptors: list[FileDescriptorProto] | tuple[FileDescriptorProto, ...],
) -> dict[str, _MessageTypeInfo]:
    registry: dict[str, _MessageTypeInfo] = {}
    for file_descriptor in file_descriptors:
        package_parts = [part for part in file_descriptor.package.split(".") if part]
        include_path = str(PurePosixPath(file_descriptor.name).with_suffix(".pb.h"))
        _register_message_types(
            registry=registry,
            package_parts=package_parts,
            include_path=include_path,
            parent_proto_names=(),
            messages=file_descriptor.message_type,
        )
    return registry


def _register_message_types(
    registry: dict[str, _MessageTypeInfo],
    package_parts: list[str],
    include_path: str,
    parent_proto_names: tuple[str, ...],
    messages: list[DescriptorProto] | tuple[DescriptorProto, ...],
) -> None:
    for message in messages:
        proto_names = (*parent_proto_names, message.name)
        full_proto_name = "." + ".".join((*package_parts, *proto_names))
        local_cpp_type = "_".join(proto_names)
        namespace_prefix = "::".join(package_parts)
        cpp_type = f"::{namespace_prefix}::{local_cpp_type}" if namespace_prefix else f"::{local_cpp_type}"
        registry[full_proto_name] = _MessageTypeInfo(cpp_type=cpp_type, include_path=include_path)
        _register_message_types(
            registry=registry,
            package_parts=package_parts,
            include_path=include_path,
            parent_proto_names=proto_names,
            messages=message.nested_type,
        )
