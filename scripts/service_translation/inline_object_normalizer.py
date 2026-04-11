from __future__ import annotations

import re
from dataclasses import replace

from .model import (
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
    ServiceVariantAlternative,
    ServiceVariantTypeExpr,
)


def rewrite_inline_object_exprs(service: ServiceDescription) -> ServiceDescription:
    return _InlineObjectNormalizer(service).normalize()


class _InlineObjectNormalizer:
    def __init__(self, service: ServiceDescription) -> None:
        self._service = service
        self._used_ids = {
            *[entry.id for entry in service.types],
            *[entry.id for entry in service.enumerations],
            *[entry.id for entry in service.structs],
            *[entry.id for entry in service.unions],
        }
        self._generated_structs: list[ServiceStruct] = []

    def normalize(self) -> ServiceDescription:
        return replace(
            self._service,
            enumerations=tuple(
                replace(
                    enumeration,
                    enumerators=tuple(
                        replace(
                            enumerator,
                            type_expr=self._normalize_type_expr(
                                enumerator.type_expr,
                                [enumeration.id, enumerator.id],
                            )
                            if enumerator.type_expr is not None
                            else None,
                        )
                        for enumerator in enumeration.enumerators
                    ),
                )
                for enumeration in self._service.enumerations
            ),
            structs=tuple(
                replace(
                    struct,
                    members=tuple(
                        self._normalize_member(member, [struct.id, member.id])
                        for member in struct.members
                    ),
                )
                for struct in self._service.structs
            )
            + tuple(self._generated_structs),
            unions=tuple(
                replace(
                    union_entry,
                    alternatives=tuple(
                        replace(
                            alternative,
                            type_expr=self._normalize_type_expr(
                                alternative.type_expr,
                                [union_entry.id, alternative.id],
                            ),
                        )
                        for alternative in union_entry.alternatives
                    ),
                )
                for union_entry in self._service.unions
            ),
            messages=tuple(self._normalize_message(message) for message in self._service.messages),
        )

    def _normalize_message(self, message: ServiceMessage) -> ServiceMessage:
        path_base = [message.title]
        return replace(
            message,
            params_fields=tuple(
                self._normalize_member(member, [*path_base, "params", member.id])
                for member in message.params_fields
            ),
            params_expr=self._normalize_type_expr(message.params_expr, [*path_base, "params"])
            if message.params_expr is not None
            else None,
            response_expr=self._normalize_type_expr(
                message.response_expr, [*path_base, "response"]
            )
            if message.response_expr is not None
            else None,
        )

    def _normalize_member(self, member: ServiceMember, path: list[str]) -> ServiceMember:
        return replace(member, type_expr=self._normalize_type_expr(member.type_expr, path))

    def _normalize_type_expr(self, expr: ServiceTypeExpr, path: list[str]) -> ServiceTypeExpr:
        if isinstance(expr, ServiceTextTypeExpr):
            return expr

        if isinstance(expr, ServiceArrayTypeExpr):
            return replace(
                expr,
                item_type=self._normalize_type_expr(expr.item_type, [*path, "item"]),
            )

        if isinstance(expr, ServiceMapTypeExpr):
            return replace(
                expr,
                value_type=self._normalize_type_expr(expr.value_type, [*path, "value"]),
            )

        if isinstance(expr, ServiceVariantTypeExpr):
            return replace(
                expr,
                alternatives=tuple(
                    replace(
                        alternative,
                        type_expr=self._normalize_type_expr(
                            alternative.type_expr,
                            [*path, f"alternative{index + 1}"],
                        )
                        if alternative.type_expr is not None
                        else None,
                    )
                    for index, alternative in enumerate(expr.alternatives)
                ),
            )

        if isinstance(expr, ServiceObjectTypeExpr):
            struct_id = self._allocate_inline_struct_id(path)
            normalized_members = tuple(
                self._normalize_member(member, [*path, member.id]) for member in expr.members
            )
            self._generated_structs.append(
                ServiceStruct(
                    id=struct_id,
                    comments=expr.comments,
                    members=normalized_members,
                    allows_anything=expr.allows_anything,
                )
            )
            return ServiceTextTypeExpr(text=struct_id, comments=expr.comments)

        raise TypeError(f"Unsupported type expression: {type(expr)!r}")

    def _allocate_inline_struct_id(self, path: list[str]) -> str:
        base = "InlineObject_" + "_".join(self._sanitize_segment(part) for part in path)
        candidate = base
        suffix = 2
        while candidate in self._used_ids:
            candidate = f"{base}_{suffix}"
            suffix += 1
        self._used_ids.add(candidate)
        return candidate

    def _sanitize_segment(self, value: str) -> str:
        cleaned = re.sub(r"[^0-9A-Za-z]+", "_", value).strip("_")
        return cleaned or "Anonymous"
