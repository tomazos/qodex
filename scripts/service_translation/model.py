from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class ServiceTypeExpr:
    comments: tuple[str, ...] = ()


@dataclass(frozen=True)
class ServiceTextTypeExpr(ServiceTypeExpr):
    text: str = ""


@dataclass(frozen=True)
class ServiceArrayTypeExpr(ServiceTypeExpr):
    item_type: ServiceTypeExpr | None = None


@dataclass(frozen=True)
class ServiceMapTypeExpr(ServiceTypeExpr):
    value_type: ServiceTypeExpr | None = None


@dataclass(frozen=True)
class ServiceVariantAlternative:
    comments: tuple[str, ...] = ()
    type_expr: ServiceTypeExpr | None = None


@dataclass(frozen=True)
class ServiceVariantTypeExpr(ServiceTypeExpr):
    alternatives: tuple[ServiceVariantAlternative, ...] = ()


@dataclass(frozen=True)
class ServiceMember:
    id: str
    use: str
    type_expr: ServiceTypeExpr
    comments: tuple[str, ...] = ()


@dataclass(frozen=True)
class ServiceObjectTypeExpr(ServiceTypeExpr):
    members: tuple[ServiceMember, ...] = ()
    allows_anything: bool = False


@dataclass(frozen=True)
class ServiceTypeDefinition:
    id: str
    comments: tuple[str, ...] = ()


@dataclass(frozen=True)
class ServiceEnumerator:
    id: str
    comments: tuple[str, ...] = ()
    type_expr: ServiceTypeExpr | None = None


@dataclass(frozen=True)
class ServiceEnumeration:
    id: str
    comments: tuple[str, ...] = ()
    enumerators: tuple[ServiceEnumerator, ...] = ()
    extended: bool = False


@dataclass(frozen=True)
class ServiceStruct:
    id: str
    comments: tuple[str, ...] = ()
    members: tuple[ServiceMember, ...] = ()
    allows_anything: bool = False


@dataclass(frozen=True)
class ServiceUnionAlternative:
    id: str
    comments: tuple[str, ...] = ()
    type_expr: ServiceTypeExpr | None = None


@dataclass(frozen=True)
class ServiceUnion:
    id: str
    discriminator: str
    comments: tuple[str, ...] = ()
    alternatives: tuple[ServiceUnionAlternative, ...] = ()


@dataclass(frozen=True)
class ServiceMessage:
    kind: str
    origin: str
    method: str
    title: str
    comments: tuple[str, ...] = ()
    has_params_element: bool = False
    params_is_null: bool = False
    params_comments: tuple[str, ...] = ()
    params_fields: tuple[ServiceMember, ...] = ()
    params_expr: ServiceTypeExpr | None = None
    response_comments: tuple[str, ...] = ()
    response_expr: ServiceTypeExpr | None = None


@dataclass(frozen=True)
class ServiceDescription:
    comments: tuple[str, ...] = ()
    types: tuple[ServiceTypeDefinition, ...] = ()
    enumerations: tuple[ServiceEnumeration, ...] = ()
    structs: tuple[ServiceStruct, ...] = ()
    unions: tuple[ServiceUnion, ...] = ()
    messages: tuple[ServiceMessage, ...] = ()
