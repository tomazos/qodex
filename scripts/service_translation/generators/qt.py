from __future__ import annotations

import keyword
import re
from dataclasses import dataclass
from functools import cached_property
from pathlib import Path

from jinja2 import Environment, FileSystemLoader

from ..inline_object_normalizer import rewrite_inline_object_exprs
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
    "any": "QJsonValue",
    "boolean": "bool",
    "integer": "qint64",
    "number": "double",
    "string": "QString",
}


@dataclass(frozen=True)
class _QtEnumerationContext:
    name: str
    comment_lines: tuple[str, ...]
    enumerators: tuple[dict[str, object], ...]


@dataclass(frozen=True)
class _QtStructContext:
    name: str
    comment_lines: tuple[str, ...]
    members: tuple[dict[str, object], ...]
    allows_anything: bool
    known_keys_condition: str


@dataclass(frozen=True)
class _QtExtendedEnumerationContext:
    name: str
    comment_lines: tuple[str, ...]
    payload_variant_cpp: str
    enumerators: tuple[dict[str, object], ...]


@dataclass(frozen=True)
class _QtUnionContext:
    name: str
    comment_lines: tuple[str, ...]
    discriminator: str
    payload_variant_cpp: str
    alternatives: tuple[dict[str, object], ...]


class QtGenerator:
    name = "qt"

    def generate(self, service: ServiceDescription, output_path: Path) -> None:
        output_path.mkdir(parents=True, exist_ok=True)
        context = _QtTemplateContextBuilder(service).build()
        for filename in (
            "CodexProtocol.h",
            "CodexProtocol.cpp",
            "CodexClient.h",
            "CodexClient.cpp",
        ):
            (output_path / filename).write_text(
                self._render_template(f"{filename}.j2", **context),
                encoding="utf-8",
            )

    @cached_property
    def _template_environment(self) -> Environment:
        return Environment(
            loader=FileSystemLoader(Path(__file__).with_name("templates").joinpath("qt")),
            autoescape=False,
            keep_trailing_newline=True,
            trim_blocks=True,
            lstrip_blocks=True,
        )

    def _render_template(self, template_name: str, **context: object) -> str:
        return self._template_environment.get_template(template_name).render(**context)


class _QtTemplateContextBuilder:
    def __init__(self, service: ServiceDescription) -> None:
        self._service = rewrite_inline_object_exprs(service)
        self._plain_enums = tuple(
            entry for entry in self._service.enumerations if not entry.extended
        )
        self._extended_enums = tuple(
            entry for entry in self._service.enumerations if entry.extended
        )
        self._message_param_structs = self._build_message_param_structs()
        self._structs = tuple(self._service.structs) + self._message_param_structs
        self._plain_enum_ids = {entry.id for entry in self._plain_enums}
        self._extended_enum_ids = {entry.id for entry in self._extended_enums}
        self._struct_ids = {entry.id for entry in self._structs}
        self._union_ids = {entry.id for entry in self._service.unions}
        self._composite_ids = self._struct_ids | self._extended_enum_ids | self._union_ids
        self._type_name_map = self._build_type_name_map()
        self._plain_enum_cpp_names = {
            self._cpp_type_name(entry.id) for entry in self._plain_enums
        }
        self._message_param_type_ids = {
            message.method: self._message_param_struct_id(message)
            for message in self._service.messages
            if message.params_fields
        }

    def build(self) -> dict[str, object]:
        plain_enumerations = tuple(
            self._plain_enum_context(entry) for entry in self._plain_enums
        )
        extended_enumerations = tuple(
            self._extended_enum_context(entry) for entry in self._extended_enums
        )
        structs = tuple(self._struct_context(entry) for entry in self._structs)
        unions = tuple(self._union_context(entry) for entry in self._service.unions)
        client_requests = tuple(
            self._client_request_context(message)
            for message in self._service.messages
            if message.origin == "client" and message.kind == "request"
        )
        client_notifications = tuple(
            self._client_notification_context(message)
            for message in self._service.messages
            if message.origin == "client" and message.kind == "notification"
        )
        server_notifications = tuple(
            self._server_notification_context(message)
            for message in self._service.messages
            if message.origin == "server" and message.kind == "notification"
        )
        server_requests = tuple(
            self._server_request_context(message)
            for message in self._service.messages
            if message.origin == "server" and message.kind == "request"
        )

        composite_type_names = tuple(
            self._cpp_type_name(entry.id)
            for entry in (*self._structs, *self._extended_enums, *self._service.unions)
        )

        return {
            "plain_enumerations": plain_enumerations,
            "extended_enumerations": extended_enumerations,
            "structs": structs,
            "unions": unions,
            "forward_composite_names": composite_type_names,
            "declared_type_names": (
                ("EmptyObject",)
                + tuple(entry.name for entry in plain_enumerations)
                + composite_type_names
            ),
            "client_requests": client_requests,
            "client_notifications": client_notifications,
            "server_notifications": server_notifications,
            "server_requests": server_requests,
        }

    def _build_message_param_structs(self) -> tuple[ServiceStruct, ...]:
        entries: list[ServiceStruct] = []
        for message in self._service.messages:
            if not message.params_fields:
                continue
            if self._message_uses_flattened_params(message):
                continue
            entries.append(
                ServiceStruct(
                    id=self._message_param_struct_id(message),
                    comments=message.params_comments,
                    members=message.params_fields,
                    allows_anything=False,
                )
            )
        return tuple(entries)

    def _build_type_name_map(self) -> dict[str, str]:
        raw_names = [
            entry.id
            for entry in (
                list(self._plain_enums)
                + list(self._extended_enums)
                + list(self._structs)
                + list(self._service.unions)
            )
        ]
        reserved = {
            "CodexClient",
            "CodexProtocol",
            "EmptyObject",
            "Nullable",
            "Ref",
            "toJson",
            "fromJson",
        }
        mapping: dict[str, str] = {}
        seen: set[str] = set()
        for raw_name in raw_names:
            candidate = self._sanitize_type_name(raw_name)
            if candidate in reserved:
                candidate = f"{candidate}Type"
            base = candidate
            suffix = 2
            while candidate in seen:
                candidate = f"{base}{suffix}"
                suffix += 1
            mapping[raw_name] = candidate
            seen.add(candidate)
        return mapping

    def _plain_enum_context(self, enumeration: ServiceEnumeration) -> _QtEnumerationContext:
        return _QtEnumerationContext(
            name=self._cpp_type_name(enumeration.id),
            comment_lines=self._comment_lines(enumeration.comments),
            enumerators=tuple(
                {
                    "cpp_name": self._sanitize_enum_enumerator(enumerator.id),
                    "json_value": enumerator.id,
                    "comment_lines": self._comment_lines(enumerator.comments, indent="    "),
                }
                for enumerator in enumeration.enumerators
            ),
        )

    def _struct_context(self, struct: ServiceStruct) -> _QtStructContext:
        members = tuple(
            {
                "cpp_type": self._render_type_expr(member.type_expr, nested=True),
                "field_name": self._sanitize_member_name(member.id),
                "json_key": member.id,
                "use": member.use,
                "comment_lines": self._comment_lines(
                    (*member.comments, *member.type_expr.comments),
                    indent="    ",
                ),
            }
            for member in struct.members
        )
        key_conditions = [
            f'key == QStringLiteral("{member.id}")' for member in struct.members
        ]
        return _QtStructContext(
            name=self._cpp_type_name(struct.id),
            comment_lines=self._comment_lines(struct.comments),
            members=members,
            allows_anything=struct.allows_anything,
            known_keys_condition=" || ".join(key_conditions),
        )

    def _extended_enum_context(
        self,
        enumeration: ServiceEnumeration,
    ) -> _QtExtendedEnumerationContext:
        payload_types = self._unique_in_order(
            tuple(
                self._render_type_expr(enumerator.type_expr, nested=True)
                for enumerator in enumeration.enumerators
                if enumerator.type_expr is not None
            )
        )
        variant_types = ("std::monostate", *payload_types)
        return _QtExtendedEnumerationContext(
            name=self._cpp_type_name(enumeration.id),
            comment_lines=self._comment_lines(enumeration.comments),
            payload_variant_cpp=self._variant_cpp_type(variant_types),
            enumerators=tuple(
                {
                    "kind_name": self._sanitize_enum_enumerator(enumerator.id),
                    "json_value": enumerator.id,
                    "has_payload": enumerator.type_expr is not None,
                    "payload_cpp_type": self._render_type_expr(enumerator.type_expr, nested=True)
                    if enumerator.type_expr is not None
                    else "",
                    "comment_lines": self._comment_lines(
                        enumerator.comments,
                        indent="        ",
                    ),
                }
                for enumerator in enumeration.enumerators
            ),
        )

    def _union_context(self, union: ServiceUnion) -> _QtUnionContext:
        payload_types = tuple(
            self._render_type_expr(alternative.type_expr, nested=True)
            for alternative in union.alternatives
        )
        return _QtUnionContext(
            name=self._cpp_type_name(union.id),
            comment_lines=self._comment_lines(union.comments),
            discriminator=union.discriminator,
            payload_variant_cpp=self._variant_cpp_type(payload_types),
            alternatives=tuple(
                {
                    "kind_name": self._sanitize_enum_enumerator(alternative.id),
                    "json_value": alternative.id,
                    "payload_cpp_type": self._render_type_expr(
                        alternative.type_expr,
                        nested=True,
                    ),
                    "comment_lines": self._comment_lines(
                        alternative.comments,
                        indent="        ",
                    ),
                }
                for alternative in union.alternatives
            ),
        )

    def _client_request_context(self, message: ServiceMessage) -> dict[str, object]:
        response_cpp_type = self._message_response_cpp_type(message)
        return {
            "method": message.method,
            "method_name": f"send{self._method_pascal_name(message.method)}Request",
            "success_signal_name": f"{self._method_camel_name(message.method)}Succeeded",
            "failure_signal_name": f"{self._method_camel_name(message.method)}Failed",
            "comment_lines": self._comment_lines((*message.comments,)),
            "parameters": self._message_parameters_context(message),
            "has_parameters": bool(self._message_parameters_context(message)),
            "send_setup_lines": self._message_send_setup_lines(message),
            "send_argument": self._message_send_argument(message),
            "response_cpp_type": response_cpp_type,
            "response_signal_cpp_type": self._signal_cpp_type(response_cpp_type),
        }

    def _client_notification_context(self, message: ServiceMessage) -> dict[str, object]:
        return {
            "method": message.method,
            "method_name": f"send{self._method_pascal_name(message.method)}Notification",
            "comment_lines": self._comment_lines((*message.comments,)),
            "parameters": self._message_parameters_context(message),
            "has_parameters": bool(self._message_parameters_context(message)),
            "send_setup_lines": self._message_send_setup_lines(message),
            "send_argument": self._message_send_argument(message),
        }

    def _server_notification_context(self, message: ServiceMessage) -> dict[str, object]:
        payload_cpp_type = self._server_message_payload_cpp_type(message)
        return {
            "method": message.method,
            "signal_name": f"{self._method_camel_name(message.method)}NotificationReceived",
            "comment_lines": self._comment_lines((*message.comments,), indent="    "),
            "has_payload": payload_cpp_type is not None,
            "payload_cpp_type": payload_cpp_type,
            "payload_signal_cpp_type": self._signal_cpp_type(payload_cpp_type)
            if payload_cpp_type is not None
            else "",
        }

    def _server_request_context(self, message: ServiceMessage) -> dict[str, object]:
        payload_cpp_type = self._server_message_payload_cpp_type(message)
        return {
            "method": message.method,
            "signal_name": f"{self._method_camel_name(message.method)}RequestReceived",
            "comment_lines": self._comment_lines((*message.comments,), indent="    "),
            "has_payload": payload_cpp_type is not None,
            "payload_cpp_type": payload_cpp_type,
            "payload_signal_cpp_type": self._signal_cpp_type(payload_cpp_type)
            if payload_cpp_type is not None
            else "",
        }

    def _message_param_mode(self, message: ServiceMessage) -> str:
        if not message.has_params_element:
            return "absent"
        if message.params_is_null:
            return "null"
        if message.params_fields or message.params_expr is not None:
            return "typed"
        return "emptyobject"

    def _message_param_declaration(self, message: ServiceMessage) -> str:
        param_cpp_type = self._message_param_cpp_type(message)
        if param_cpp_type is None:
            return ""
        return f"{self._parameter_cpp_type(param_cpp_type)} params"

    def _message_param_cpp_type(self, message: ServiceMessage) -> str | None:
        mode = self._message_param_mode(message)
        if mode != "typed":
            return None
        if message.params_fields:
            return self._cpp_type_name(self._message_param_type_ids[message.method])
        if message.params_expr is not None:
            return self._render_type_expr(message.params_expr, nested=False)
        raise ValueError(f"Unhandled typed params for {message.method}")

    def _server_message_payload_cpp_type(self, message: ServiceMessage) -> str | None:
        mode = self._message_param_mode(message)
        if mode == "absent":
            return None
        if mode == "null":
            return None
        if mode == "typed":
            param_cpp_type = self._message_param_cpp_type(message)
            if param_cpp_type is None:
                raise ValueError(f"Expected typed payload type for {message.method}")
            return param_cpp_type
        if mode == "emptyobject":
            return "EmptyObject"
        raise ValueError(f"Unhandled message payload mode {mode!r}")

    def _message_response_cpp_type(self, message: ServiceMessage) -> str:
        if message.response_expr is None:
            return "EmptyObject"
        return self._render_type_expr(message.response_expr, nested=False)

    def _message_send_argument(self, message: ServiceMessage) -> str:
        mode = self._message_param_mode(message)
        if mode == "absent":
            return "QJsonValue(QJsonValue::Undefined)"
        if mode == "null":
            return "QJsonValue(QJsonValue::Null)"
        if mode == "typed":
            if self._message_uses_flattened_params(message):
                return "paramsObject"
            return "toJson(params)"
        if mode == "emptyobject":
            return "QJsonObject{}"
        raise ValueError(f"Unhandled message param mode {mode!r}")

    def _message_parameters_context(self, message: ServiceMessage) -> tuple[dict[str, str], ...]:
        if self._message_uses_flattened_params(message):
            return tuple(self._member_parameter_context(member) for member in message.params_fields)

        param_cpp_type = self._message_param_cpp_type(message)
        if param_cpp_type is None:
            return ()
        return (
            {
                "declaration": f"{self._parameter_cpp_type(param_cpp_type)} params",
            },
        )

    def _message_send_setup_lines(self, message: ServiceMessage) -> tuple[str, ...]:
        if not self._message_uses_flattened_params(message):
            return ()

        lines = ["QJsonObject paramsObject;"]
        for member in message.params_fields:
            field_name = self._sanitize_member_name(member.id)
            if member.use == "required":
                lines.append(
                    f'paramsObject.insert(QStringLiteral("{member.id}"), toJson({field_name}));'
                )
            elif member.use == "optional":
                lines.append(
                    f'writeOptionalMember(paramsObject, QStringLiteral("{member.id}"), {field_name});'
                )
            elif member.use == "nullable":
                lines.append(
                    f'writeNullableMember(paramsObject, QStringLiteral("{member.id}"), {field_name});'
                )
            else:
                raise ValueError(
                    f"Unsupported member use {member.use!r} for flattened params on {message.method}"
                )
        return tuple(lines)

    def _message_uses_flattened_params(self, message: ServiceMessage) -> bool:
        return (
            message.origin == "client"
            and bool(message.params_fields)
        )

    def _member_parameter_context(self, member: ServiceMember) -> dict[str, str]:
        cpp_type = self._render_member_cpp_type(member)
        return {
            "declaration": f"{self._parameter_cpp_type(cpp_type)} {self._sanitize_member_name(member.id)}",
        }

    def _render_member_cpp_type(self, member: ServiceMember) -> str:
        base_cpp_type = self._render_type_expr(member.type_expr, nested=True)
        if member.use == "optional":
            return f"std::optional<{base_cpp_type}>"
        if member.use == "nullable":
            return f"Nullable<{base_cpp_type}>"
        return base_cpp_type

    def _parameter_cpp_type(self, cpp_type: str) -> str:
        if cpp_type in {"bool", "double", "qint64"}:
            return cpp_type
        if cpp_type in self._plain_enum_cpp_names or cpp_type == "EmptyObject":
            return cpp_type
        return f"const {cpp_type} &"

    def _signal_cpp_type(self, cpp_type: str) -> str:
        return self._parameter_cpp_type(cpp_type)

    def _variant_cpp_type(self, variant_types: tuple[str, ...]) -> str:
        return f"std::variant<{', '.join(variant_types)}>"

    def _render_type_expr(self, expr: ServiceTypeExpr | None, *, nested: bool) -> str:
        if expr is None:
            raise ValueError("Qt generator does not support missing type expressions")

        if isinstance(expr, ServiceTextTypeExpr):
            text = expr.text.strip()
            if "|" in text:
                parts = tuple(part.strip() for part in text.split("|") if part.strip())
                return self._variant_cpp_type(
                    tuple(
                        "std::monostate"
                        if part == "null"
                        else self._render_type_expr(ServiceTextTypeExpr(text=part), nested=True)
                        for part in parts
                    )
                )
            if text in _PRIMITIVE_TYPE_MAP:
                return _PRIMITIVE_TYPE_MAP[text]
            if text in self._plain_enum_ids:
                return self._cpp_type_name(text)
            if text in self._composite_ids:
                type_name = self._cpp_type_name(text)
                return f"Ref<{type_name}>" if nested else type_name
            raise ValueError(f"Unsupported type reference in qt generator: {text!r}")

        if isinstance(expr, ServiceArrayTypeExpr):
            return f"QList<{self._render_type_expr(expr.item_type, nested=True)}>"

        if isinstance(expr, ServiceMapTypeExpr):
            return f"QMap<QString, {self._render_type_expr(expr.value_type, nested=True)}>"

        if isinstance(expr, ServiceVariantTypeExpr):
            return self._variant_cpp_type(
                tuple(
                    self._render_type_expr(alternative.type_expr, nested=True)
                    for alternative in expr.alternatives
                )
            )

        if isinstance(expr, ServiceObjectTypeExpr):
            raise ValueError(
                "Inline object expressions should be normalized before qt generation"
            )

        raise ValueError(
            f"Qt generator encountered unsupported inline type expression: {type(expr).__name__}"
        )

    def _cpp_type_name(self, raw_id: str) -> str:
        return self._type_name_map[raw_id]

    def _message_param_struct_id(self, message: ServiceMessage) -> str:
        return f"{self._method_pascal_name(message.method)}{self._capitalize_word(message.kind)}Params"

    def _sanitize_type_name(self, raw_name: str) -> str:
        words = self._split_words(raw_name)
        candidate = "Type" if not words else "".join(
            self._capitalize_word(word) for word in words
        )
        if candidate[0].isdigit():
            candidate = f"X{candidate}"
        if candidate in _CPP_KEYWORDS or keyword.iskeyword(candidate):
            candidate = f"{candidate}Type"
        return candidate

    def _sanitize_member_name(self, raw_name: str) -> str:
        words = self._split_words(raw_name)
        if not words:
            candidate = "value"
        else:
            first, *rest = words
            candidate = first.lower() + "".join(self._capitalize_word(word) for word in rest)
        if candidate[0].isdigit():
            candidate = f"field{candidate}"
        if candidate in _CPP_KEYWORDS or keyword.iskeyword(candidate):
            candidate = f"{candidate}Value"
        return candidate

    def _sanitize_enum_enumerator(self, raw_name: str) -> str:
        words = self._split_words(raw_name)
        candidate = "Value" if not words else "".join(
            self._capitalize_word(word) for word in words
        )
        if candidate[0].isdigit():
            candidate = f"X{candidate}"
        if candidate in _CPP_KEYWORDS or keyword.iskeyword(candidate):
            candidate = f"{candidate}Value"
        return candidate

    def _method_pascal_name(self, method: str) -> str:
        words = self._split_words(method)
        identifier = "Message" if not words else "".join(
            self._capitalize_word(word) for word in words
        )
        if identifier[0].isdigit():
            identifier = f"X{identifier}"
        if identifier in _CPP_KEYWORDS or keyword.iskeyword(identifier):
            identifier = f"{identifier}Value"
        return identifier

    def _method_camel_name(self, method: str) -> str:
        pascal = self._method_pascal_name(method)
        return pascal[:1].lower() + pascal[1:] if pascal else "message"

    def _split_words(self, raw_name: str) -> list[str]:
        chunks = re.split(r"[^A-Za-z0-9]+", raw_name)
        words: list[str] = []
        for chunk in chunks:
            if not chunk:
                continue
            if chunk.islower():
                words.append(chunk)
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
    ) -> tuple[str, ...]:
        lines: list[str] = []
        for comment in comments:
            for line in comment.splitlines():
                stripped = line.strip()
                if stripped:
                    lines.append(f"{indent}// {stripped}")
        return tuple(lines)

    def _unique_in_order(self, values: tuple[str, ...]) -> tuple[str, ...]:
        result: list[str] = []
        seen: set[str] = set()
        for value in values:
            if value in seen:
                continue
            seen.add(value)
            result.append(value)
        return tuple(result)
