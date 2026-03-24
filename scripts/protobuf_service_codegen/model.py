from __future__ import annotations

import keyword
import re
from dataclasses import dataclass
from pathlib import PurePosixPath


_CPP_KEYWORDS = {
    "alignas",
    "alignof",
    "and",
    "and_eq",
    "asm",
    "auto",
    "bitand",
    "bitor",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "char8_t",
    "char16_t",
    "char32_t",
    "class",
    "compl",
    "concept",
    "const",
    "consteval",
    "constexpr",
    "constinit",
    "const_cast",
    "continue",
    "co_await",
    "co_return",
    "co_yield",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "nullptr",
    "operator",
    "or",
    "or_eq",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "unsigned",
    "using",
    "virtual",
    "void",
    "volatile",
    "wchar_t",
    "while",
    "xor",
    "xor_eq",
}


def sanitize_identifier(name: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if not sanitized:
        sanitized = "_"
    if sanitized[0].isdigit():
        sanitized = f"_{sanitized}"
    if sanitized in _CPP_KEYWORDS or keyword.iskeyword(sanitized):
        sanitized = f"{sanitized}_"
    return sanitized


@dataclass(frozen=True)
class MethodModel:
    name: str
    trait_name: str
    request_cpp_type: str
    response_cpp_type: str
    request_handler_name: str
    response_handler_name: str


@dataclass(frozen=True)
class ServiceModel:
    name: str
    full_name: str
    generated_namespace: str
    methods: tuple[MethodModel, ...]


@dataclass(frozen=True)
class GeneratedFileModel:
    proto_name: str
    output_name: str
    include_paths: tuple[str, ...]
    services: tuple[ServiceModel, ...]


def proto_to_generated_header_name(proto_name: str) -> str:
    return str(PurePosixPath(proto_name).with_suffix(".qodex_rpc.h"))
