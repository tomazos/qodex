from __future__ import annotations

import keyword
import re
from dataclasses import dataclass
from functools import cached_property
from pathlib import Path

from jinja2 import Environment, FileSystemLoader

from ..model import (
    ServiceArrayTypeExpr,
    ServiceDescription,
    ServiceEnumeration,
    ServiceMapTypeExpr,
    ServiceMember,
    ServiceMessage,
    ServiceObjectTypeExpr,
    ServiceStruct,
    ServiceTextTypeExpr,
    ServiceTypeDefinition,
    ServiceTypeExpr,
    ServiceUnion,
    ServiceVariantTypeExpr,
)


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

_PRIMITIVE_TYPE_MAP = {
    "any": "JsonValue",
    "boolean": "bool",
    "integer": "std::int64_t",
    "number": "double",
    "string": "std::string",
}


@dataclass(frozen=True)
class _CppParameter:
    cpp_type: str
    cpp_name: str


class CppGenerator:
    name = "cpp"

    def generate(self, service: ServiceDescription, output_path: Path) -> None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        context = _CppTemplateContextBuilder(service).build()
        output_path.write_text(
            self._render_template("header.j2", **context),
            encoding="utf-8",
        )

    @cached_property
    def _template_environment(self) -> Environment:
        return Environment(
            loader=FileSystemLoader(Path(__file__).with_name("templates").joinpath("cpp")),
            autoescape=False,
            keep_trailing_newline=True,
            trim_blocks=True,
            lstrip_blocks=True,
        )

    def _render_template(self, template_name: str, **context: object) -> str:
        return self._template_environment.get_template(template_name).render(**context)


class _CppTemplateContextBuilder:
    def __init__(self, service: ServiceDescription) -> None:
        self._service = service
        self._plain_enumerations = tuple(
            entry for entry in service.enumerations if not entry.extended
        )
        self._extended_enumerations = tuple(
            entry for entry in service.enumerations if entry.extended
        )
        self._type_names = self._build_type_name_map()
        self._message_names = self._build_message_name_map()
        self._composite_entity_ids = {
            *[entry.id for entry in service.types],
            *[entry.id for entry in service.structs],
            *[entry.id for entry in service.unions],
            *[entry.id for entry in self._extended_enumerations],
        }
        self._all_entity_ids = {
            *self._composite_entity_ids,
            *[entry.id for entry in self._plain_enumerations],
        }

    def build(self) -> dict[str, object]:
        return {
            "service_comment_lines": self._comment_lines(self._service.comments),
            "forward_declarations": self._forward_declarations(),
            "plain_enumerations": [
                self._plain_enumeration_context(entry) for entry in self._plain_enumerations
            ],
            "opaque_types": [self._opaque_type_context(entry) for entry in self._service.types],
            "structs": [self._struct_context(entry) for entry in self._service.structs],
            "extended_enumerations": [
                self._extended_enumeration_context(entry)
                for entry in self._extended_enumerations
            ],
            "unions": [self._union_context(entry) for entry in self._service.unions],
            "messages": [self._message_context(entry) for entry in self._service.messages],
        }

    def _forward_declarations(self) -> list[str]:
        composite_ids = (
            [entry.id for entry in self._service.types]
            + [entry.id for entry in self._service.structs]
            + [entry.id for entry in self._extended_enumerations]
            + [entry.id for entry in self._service.unions]
        )
        return [self._cpp_type_name(entity_id) for entity_id in composite_ids]

    def _plain_enumeration_context(self, enumeration: ServiceEnumeration) -> dict[str, object]:
        return {
            "name": self._cpp_type_name(enumeration.id),
            "comment_lines": self._comment_lines(enumeration.comments),
            "enumerators": [
                {
                    "name": self._sanitize_enum_enumerator(enumerator.id),
                    "comment_lines": self._comment_lines(enumerator.comments, indent="    "),
                }
                for enumerator in enumeration.enumerators
            ],
        }

    def _opaque_type_context(self, type_def: ServiceTypeDefinition) -> dict[str, object]:
        return {
            "name": self._cpp_type_name(type_def.id),
            "comment_lines": self._comment_lines(type_def.comments),
        }

    def _struct_context(self, struct: ServiceStruct) -> dict[str, object]:
        members = [self._struct_member_context(member) for member in struct.members]
        return {
            "name": self._cpp_type_name(struct.id),
            "comment_lines": self._comment_lines(struct.comments),
            "members": members,
            "allows_anything": struct.allows_anything,
            "has_body": bool(members or struct.allows_anything),
        }

    def _struct_member_context(self, member: ServiceMember) -> dict[str, object]:
        comments = (*member.comments, *member.type_expr.comments)
        return {
            "comment_lines": self._comment_lines(comments, indent="    "),
            "cpp_type": self._wrap_member_use(
                self._render_type_expr(member.type_expr, nested=True),
                member.use,
            ),
            "cpp_name": self._sanitize_member_name(member.id),
        }

    def _extended_enumeration_context(
        self,
        enumeration: ServiceEnumeration,
    ) -> dict[str, object]:
        payload_types = [
            self._render_type_expr(enumerator.type_expr, nested=True)
            for enumerator in enumeration.enumerators
            if enumerator.type_expr is not None
        ]
        return {
            "name": self._cpp_type_name(enumeration.id),
            "comment_lines": self._comment_lines(enumeration.comments),
            "enumerators": [
                {
                    "name": self._sanitize_enum_enumerator(enumerator.id),
                    "comment_lines": self._comment_lines(
                        enumerator.comments,
                        indent="        ",
                    ),
                }
                for enumerator in enumeration.enumerators
            ],
            "payload_alias": self._variant_alias(payload_types),
        }

    def _union_context(self, union: ServiceUnion) -> dict[str, object]:
        payload_types = [
            self._render_type_expr(alternative.type_expr, nested=True)
            for alternative in union.alternatives
        ]
        return {
            "name": self._cpp_type_name(union.id),
            "comment_lines": self._comment_lines(union.comments),
            "alternatives": [
                {
                    "name": self._sanitize_enum_enumerator(alternative.id),
                    "comment_lines": self._comment_lines(
                        alternative.comments,
                        indent="        ",
                    ),
                }
                for alternative in union.alternatives
            ],
            "payload_alias": self._variant_alias(payload_types),
        }

    def _message_context(self, message: ServiceMessage) -> dict[str, object]:
        message_header = f"{message.origin} {message.kind} {message.method}"
        comments = [message_header, *message.comments]
        if message.params_comments:
            comments.append("params: " + " | ".join(message.params_comments))
        if message.response_comments:
            comments.append("response: " + " | ".join(message.response_comments))
        return {
            "comment_lines": self._comment_lines(comments, indent="    "),
            "return_type": self._message_return_type(message),
            "name": self._message_names[message.method],
            "parameters": [
                f"{parameter.cpp_type} {parameter.cpp_name}"
                for parameter in self._message_parameters(message)
            ],
        }

    def _message_return_type(self, message: ServiceMessage) -> str:
        if message.kind == "notification":
            return "void"
        if message.response_expr is None:
            return "EmptyObject"
        return self._render_type_expr(message.response_expr, nested=False)

    def _message_parameters(self, message: ServiceMessage) -> tuple[_CppParameter, ...]:
        if not message.has_params_element or message.params_is_null:
            return ()
        if message.params_fields:
            return tuple(
                _CppParameter(
                    cpp_type=self._wrap_member_use(
                        self._render_type_expr(field.type_expr, nested=False),
                        field.use,
                    ),
                    cpp_name=self._sanitize_member_name(field.id),
                )
                for field in message.params_fields
            )
        if message.params_expr is not None:
            return (
                _CppParameter(
                    cpp_type=self._render_type_expr(message.params_expr, nested=False),
                    cpp_name="params",
                ),
            )
        return (_CppParameter(cpp_type="EmptyObject", cpp_name="params"),)

    def _build_type_name_map(self) -> dict[str, str]:
        raw_names = [
            entry.id
            for entry in (
                list(self._service.types)
                + list(self._service.enumerations)
                + list(self._service.structs)
                + list(self._service.unions)
            )
        ]
        reserved = {"Service", "EmptyObject", "JsonValue", "Ref"}
        mapping: dict[str, str] = {}
        seen: set[str] = set()
        for raw_name in raw_names:
            candidate = self._sanitize_type_name(raw_name)
            if candidate in reserved:
                candidate = f"{candidate}Type"
            original_candidate = candidate
            suffix = 2
            while candidate in seen:
                candidate = f"{original_candidate}{suffix}"
                suffix += 1
            mapping[raw_name] = candidate
            seen.add(candidate)
        return mapping

    def _build_message_name_map(self) -> dict[str, str]:
        mapping: dict[str, str] = {}
        seen: set[str] = {"service"}
        for message in self._service.messages:
            base_name = self._sanitize_member_name(message.method)
            candidate = base_name
            suffix = 2
            while candidate in seen:
                candidate = f"{base_name}_{suffix}"
                suffix += 1
            mapping[message.method] = candidate
            seen.add(candidate)
        return mapping

    def _render_type_expr(self, expr: ServiceTypeExpr, *, nested: bool) -> str:
        if isinstance(expr, ServiceTextTypeExpr):
            if "|" in expr.text:
                parts = [part.strip() for part in expr.text.split("|") if part.strip()]
                non_null_parts = [part for part in parts if part != "null"]
                rendered_non_null = [
                    self._render_type_expr(ServiceTextTypeExpr(text=part), nested=nested)
                    for part in non_null_parts
                ]
                has_null = len(non_null_parts) != len(parts)
                if has_null and len(rendered_non_null) == 1:
                    return f"std::optional<{rendered_non_null[0]}>"
                variant_alternatives: list[str] = []
                if has_null:
                    variant_alternatives.append("std::monostate")
                variant_alternatives.extend(rendered_non_null)
                return self._variant_alias(variant_alternatives)
            primitive_type = _PRIMITIVE_TYPE_MAP.get(expr.text)
            if primitive_type is not None:
                return primitive_type
            if expr.text not in self._all_entity_ids:
                raise ValueError(f"Unknown service type reference: {expr.text}")
            cpp_name = self._cpp_type_name(expr.text)
            if expr.text in self._composite_entity_ids and nested:
                return f"Ref<{cpp_name}>"
            return cpp_name

        if isinstance(expr, ServiceArrayTypeExpr):
            return f"std::vector<{self._render_type_expr(expr.item_type, nested=nested)}>"

        if isinstance(expr, ServiceMapTypeExpr):
            return (
                "std::map<std::string, "
                f"{self._render_type_expr(expr.value_type, nested=nested)}>"
            )

        if isinstance(expr, ServiceVariantTypeExpr):
            alternatives = [
                self._render_type_expr(alternative.type_expr, nested=nested)
                for alternative in expr.alternatives
            ]
            return self._variant_alias(alternatives)

        if isinstance(expr, ServiceObjectTypeExpr):
            raise ValueError("Inline object expressions are not supported by the cpp generator")

        raise TypeError(f"Unsupported type expression: {type(expr)!r}")

    def _variant_alias(self, alternatives: list[str]) -> str:
        if not alternatives:
            return "std::monostate"
        unique_alternatives: list[str] = []
        for alternative in alternatives:
            if alternative not in unique_alternatives:
                unique_alternatives.append(alternative)
        if len(unique_alternatives) == 1:
            return unique_alternatives[0]
        return f"std::variant<{', '.join(unique_alternatives)}>"

    def _wrap_member_use(self, cpp_type: str, use: str) -> str:
        if use == "required":
            return cpp_type
        if use in {"optional", "nullable"}:
            return f"std::optional<{cpp_type}>"
        raise ValueError(f"Unknown member use: {use}")

    def _cpp_type_name(self, entity_id: str) -> str:
        return self._type_names[entity_id]

    def _sanitize_type_name(self, raw_name: str) -> str:
        identifier = self._sanitize_identifier(raw_name)
        if identifier and identifier[0].isdigit():
            identifier = f"T{identifier}"
        if identifier in _CPP_KEYWORDS or keyword.iskeyword(identifier):
            identifier = f"{identifier}Type"
        return identifier

    def _sanitize_member_name(self, raw_name: str) -> str:
        words = self._split_words(raw_name)
        candidate = "value" if not words else "_".join(word.lower() for word in words)
        if candidate[0].isdigit():
            candidate = f"field_{candidate}"
        if candidate in _CPP_KEYWORDS or keyword.iskeyword(candidate):
            candidate = f"{candidate}_"
        return candidate

    def _sanitize_enum_enumerator(self, raw_name: str) -> str:
        words = self._split_words(raw_name)
        candidate = "Value" if not words else "".join(
            self._capitalize_word(word) for word in words
        )
        if candidate[0].isdigit():
            candidate = f"Value{candidate}"
        if candidate in _CPP_KEYWORDS or keyword.iskeyword(candidate):
            candidate = f"{candidate}Value"
        return candidate

    def _sanitize_identifier(self, raw_name: str) -> str:
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", raw_name):
            return raw_name
        words = self._split_words(raw_name)
        if not words:
            return "Value"
        return "".join(self._capitalize_word(word) for word in words)

    def _split_words(self, raw_name: str) -> list[str]:
        chunks = re.split(r"[^A-Za-z0-9]+", raw_name)
        words: list[str] = []
        for chunk in chunks:
            if not chunk:
                continue
            words.extend(
                part
                for part in re.findall(
                    r"[A-Z]+(?=[A-Z][a-z]|[0-9]|$)|[A-Z]?[a-z]+|[0-9]+",
                    chunk,
                )
                if part
            )
        return words

    def _capitalize_word(self, word: str) -> str:
        if word.isupper() and len(word) > 1:
            return word[0] + word[1:].lower()
        return word[:1].upper() + word[1:]

    def _comment_lines(
        self,
        comments: tuple[str, ...] | list[str],
        *,
        indent: str = "",
    ) -> list[str]:
        lines: list[str] = []
        for comment in comments:
            for line in comment.splitlines():
                stripped = line.strip()
                if stripped:
                    lines.append(f"{indent}// {stripped}")
        return lines
