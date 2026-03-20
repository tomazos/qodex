from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Iterable

from .model import DefinitionEntry, SchemaBundle, SchemaDocument, SchemaNamespace, SchemaNode


class MessageDirection(str, Enum):
    CLIENT = "client"
    SERVER = "server"


class MessageCategory(str, Enum):
    REQUEST = "request"
    NOTIFICATION = "notification"


class ParamsShape(str, Enum):
    REF = "ref"
    NULL = "null"
    MISSING = "missing"
    INLINE = "inline"


@dataclass(frozen=True)
class NamedSchema:
    name: str
    qualified_name: str
    node: SchemaNode


@dataclass
class ProtocolMessageVariant:
    union_name: str
    category: MessageCategory
    direction: MessageDirection
    title: str
    method: str
    node: SchemaNode
    params_shape: ParamsShape
    params_schema: SchemaNode | None = None
    params_type_name: str | None = None


@dataclass
class RequestResponseMapping:
    request: ProtocolMessageVariant
    response_type_name: str
    response_schema: NamedSchema


@dataclass
class ProtocolUnion:
    name: str
    category: MessageCategory
    direction: MessageDirection
    node: SchemaNode
    variants: list[ProtocolMessageVariant] = field(default_factory=list)


@dataclass
class ProtocolAnalysis:
    bundle_document: SchemaDocument
    named_schemas: dict[str, list[NamedSchema]]
    unions: dict[str, ProtocolUnion]
    request_response_mappings: list[RequestResponseMapping]
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    @property
    def client_requests(self) -> ProtocolUnion:
        return self.unions["ClientRequest"]

    @property
    def server_requests(self) -> ProtocolUnion:
        return self.unions["ServerRequest"]

    @property
    def client_notifications(self) -> ProtocolUnion:
        return self.unions["ClientNotification"]

    @property
    def server_notifications(self) -> ProtocolUnion:
        return self.unions["ServerNotification"]


NULL_PARAMS_REQUEST_RESPONSE_BY_METHOD = {
    "config/mcpServer/reload": "McpServerRefreshResponse",
    "account/logout": "LogoutAccountResponse",
    "account/rateLimits/read": "GetAccountRateLimitsResponse",
    "configRequirements/read": "ConfigRequirementsReadResponse",
}

PARAMS_TYPE_RESPONSE_OVERRIDES = {
    "ConfigValueWriteParams": "ConfigWriteResponse",
    "ConfigBatchWriteParams": "ConfigWriteResponse",
}

ALLOWED_SHARED_RESPONSE_TYPES = {
    "ConfigWriteResponse",
}


class ProtocolAnalyzer:
    def analyze_bundle(self, bundle: SchemaBundle) -> ProtocolAnalysis:
        bundle_document = self._find_bundle_document(bundle)
        named_schemas = self._collect_named_schemas(bundle_document)
        errors: list[str] = []
        warnings: list[str] = []

        unions = {
            "ClientRequest": self._analyze_union(
                bundle_document,
                named_schemas,
                root_name="ClientRequest",
                category=MessageCategory.REQUEST,
                direction=MessageDirection.CLIENT,
                errors=errors,
                warnings=warnings,
            ),
            "ServerRequest": self._analyze_union(
                bundle_document,
                named_schemas,
                root_name="ServerRequest",
                category=MessageCategory.REQUEST,
                direction=MessageDirection.SERVER,
                errors=errors,
                warnings=warnings,
            ),
            "ClientNotification": self._analyze_union(
                bundle_document,
                named_schemas,
                root_name="ClientNotification",
                category=MessageCategory.NOTIFICATION,
                direction=MessageDirection.CLIENT,
                errors=errors,
                warnings=warnings,
            ),
            "ServerNotification": self._analyze_union(
                bundle_document,
                named_schemas,
                root_name="ServerNotification",
                category=MessageCategory.NOTIFICATION,
                direction=MessageDirection.SERVER,
                errors=errors,
                warnings=warnings,
            ),
        }

        mappings = self._analyze_request_response_mappings(
            named_schemas=named_schemas,
            request_variants=[
                *unions["ClientRequest"].variants,
                *unions["ServerRequest"].variants,
            ],
            errors=errors,
            warnings=warnings,
        )
        self._validate_response_bijection(
            named_schemas=named_schemas,
            mappings=mappings,
            errors=errors,
        )

        return ProtocolAnalysis(
            bundle_document=bundle_document,
            named_schemas=named_schemas,
            unions=unions,
            request_response_mappings=mappings,
            errors=errors,
            warnings=warnings,
        )

    def _find_bundle_document(self, bundle: SchemaBundle) -> SchemaDocument:
        preferred_names = (
            "codex_app_server_protocol.schemas.json",
            "codex_app_server_protocol.v2.schemas.json",
        )
        for preferred_name in preferred_names:
            for relative_path, document in bundle.documents_by_path.items():
                if relative_path.endswith(preferred_name):
                    return document
        raise ValueError("Could not find a protocol bundle document in schema directory")

    def _collect_named_schemas(self, document: SchemaDocument) -> dict[str, list[NamedSchema]]:
        root_definitions = document.root.definitions
        named_schemas: dict[str, list[NamedSchema]] = {}

        def add_named(name: str, qualified_name: str, node: SchemaNode) -> None:
            named_schemas.setdefault(name, []).append(
                NamedSchema(name=name, qualified_name=qualified_name, node=node)
            )

        for name, entry in root_definitions.items():
            if isinstance(entry, SchemaNode):
                add_named(name, name, entry)
            else:
                self._collect_namespace_named_schemas(entry, prefix=name, sink=add_named)

        return named_schemas

    def _collect_namespace_named_schemas(
        self,
        namespace: SchemaNamespace,
        prefix: str,
        sink,
    ) -> None:
        for name, entry in namespace.entries.items():
            qualified_name = f"{prefix}.{name}"
            if isinstance(entry, SchemaNode):
                sink(name, qualified_name, entry)
            else:
                self._collect_namespace_named_schemas(entry, qualified_name, sink)

    def _analyze_union(
        self,
        bundle_document: SchemaDocument,
        named_schemas: dict[str, list[NamedSchema]],
        root_name: str,
        category: MessageCategory,
        direction: MessageDirection,
        errors: list[str],
        warnings: list[str],
    ) -> ProtocolUnion:
        union_entry = bundle_document.root.definitions.get(root_name)
        if not isinstance(union_entry, SchemaNode):
            raise ValueError(f"Root definition {root_name!r} is missing or not a schema node")

        union = ProtocolUnion(
            name=root_name,
            category=category,
            direction=direction,
            node=union_entry,
        )

        if not union_entry.one_of:
            errors.append(f"{root_name} does not define a oneOf union")
            return union

        for branch in union_entry.one_of:
            variant = self._analyze_variant(
                named_schemas=named_schemas,
                union_name=root_name,
                category=category,
                direction=direction,
                branch=branch,
                errors=errors,
                warnings=warnings,
            )
            if variant is not None:
                union.variants.append(variant)

        return union

    def _analyze_variant(
        self,
        named_schemas: dict[str, list[NamedSchema]],
        union_name: str,
        category: MessageCategory,
        direction: MessageDirection,
        branch: SchemaNode,
        errors: list[str],
        warnings: list[str],
    ) -> ProtocolMessageVariant | None:
        title = branch.title or f"{union_name}Variant"
        location = f"{branch.location.document_path}:{branch.location.pointer or '/'}"

        if "object" not in branch.types:
            errors.append(f"{location} {title} is not an object schema")
            return None

        method_node = branch.properties.get("method")
        if method_node is None:
            errors.append(f"{location} {title} has no method property")
            return None
        if len(method_node.enum_values) != 1 or not isinstance(method_node.enum_values[0], str):
            errors.append(f"{location} {title} method is not a single-string enum")
            return None

        method = method_node.enum_values[0]
        params_node = branch.properties.get("params")
        params_shape = self._classify_params_shape(params_node)
        params_type_name: str | None = None

        if category == MessageCategory.REQUEST:
            self._validate_request_variant_shape(
                title=title,
                union_name=union_name,
                branch=branch,
                params_shape=params_shape,
                errors=errors,
            )
        else:
            self._validate_notification_variant_shape(
                title=title,
                union_name=union_name,
                branch=branch,
                params_shape=params_shape,
                errors=errors,
            )

        if params_shape == ParamsShape.REF and params_node is not None and params_node.ref:
            params_type_name = params_node.ref.split("/")[-1]
            if self._lookup_named_schema(named_schemas, params_type_name) is None:
                errors.append(
                    f"{location} {title} params ref points to unknown named schema {params_type_name!r}"
                )

        return ProtocolMessageVariant(
            union_name=union_name,
            category=category,
            direction=direction,
            title=title,
            method=method,
            node=branch,
            params_shape=params_shape,
            params_schema=params_node,
            params_type_name=params_type_name,
        )

    def _validate_request_variant_shape(
        self,
        title: str,
        union_name: str,
        branch: SchemaNode,
        params_shape: ParamsShape,
        errors: list[str],
    ) -> None:
        location = f"{branch.location.document_path}:{branch.location.pointer or '/'}"
        required = set(branch.required)
        property_names = set(branch.properties)

        if property_names != {"id", "method", "params"}:
            errors.append(
                f"{location} {union_name} branch {title} must expose exactly id/method/params properties"
            )

        if not {"id", "method"}.issubset(required):
            errors.append(f"{location} {union_name} branch {title} must require id and method")

        id_node = branch.properties.get("id")
        if id_node is None or id_node.ref is None or not id_node.ref.endswith("/RequestId"):
            errors.append(f"{location} {union_name} branch {title} id is not a RequestId ref")

        if params_shape not in {ParamsShape.REF, ParamsShape.NULL}:
            errors.append(
                f"{location} {union_name} branch {title} params must be either a named ref or null"
            )

    def _validate_notification_variant_shape(
        self,
        title: str,
        union_name: str,
        branch: SchemaNode,
        params_shape: ParamsShape,
        errors: list[str],
    ) -> None:
        location = f"{branch.location.document_path}:{branch.location.pointer or '/'}"
        required = set(branch.required)
        property_names = set(branch.properties)

        if "method" not in required:
            errors.append(f"{location} {union_name} branch {title} must require method")

        if union_name == "ClientNotification":
            allowed_shapes = (
                property_names == {"method"} and params_shape == ParamsShape.MISSING
            )
            if not allowed_shapes:
                errors.append(
                    f"{location} {union_name} branch {title} must expose only a method property"
                )
            return

        if property_names != {"method", "params"}:
            errors.append(
                f"{location} {union_name} branch {title} must expose exactly method/params properties"
            )
        if params_shape != ParamsShape.REF:
            errors.append(
                f"{location} {union_name} branch {title} params must be a named ref"
            )

    def _analyze_request_response_mappings(
        self,
        named_schemas: dict[str, list[NamedSchema]],
        request_variants: Iterable[ProtocolMessageVariant],
        errors: list[str],
        warnings: list[str],
    ) -> list[RequestResponseMapping]:
        mappings: list[RequestResponseMapping] = []
        response_to_requests: dict[str, list[ProtocolMessageVariant]] = {}

        for request_variant in request_variants:
            response_type_name = self._derive_response_type_name(request_variant, errors)
            if response_type_name is None:
                continue

            response_schema = self._lookup_named_schema(named_schemas, response_type_name)
            if response_schema is None:
                errors.append(
                    f"{request_variant.title} ({request_variant.method}) maps to missing response type "
                    f"{response_type_name!r}"
                )
                continue

            response_to_requests.setdefault(response_type_name, []).append(request_variant)
            mappings.append(
                RequestResponseMapping(
                    request=request_variant,
                    response_type_name=response_type_name,
                    response_schema=response_schema,
                )
            )

        return mappings

    def _derive_response_type_name(
        self,
        request_variant: ProtocolMessageVariant,
        errors: list[str],
    ) -> str | None:
        if request_variant.params_shape == ParamsShape.REF and request_variant.params_type_name:
            override = PARAMS_TYPE_RESPONSE_OVERRIDES.get(request_variant.params_type_name)
            if override is not None:
                return override
            if not request_variant.params_type_name.endswith("Params"):
                errors.append(
                    f"{request_variant.title} params type {request_variant.params_type_name!r} "
                    "does not end with 'Params'"
                )
                return None
            return request_variant.params_type_name[: -len("Params")] + "Response"

        if request_variant.params_shape == ParamsShape.NULL:
            response_type_name = NULL_PARAMS_REQUEST_RESPONSE_BY_METHOD.get(request_variant.method)
            if response_type_name is None:
                errors.append(
                    f"{request_variant.title} ({request_variant.method}) has null params but no "
                    "known response mapping"
                )
            return response_type_name

        errors.append(
            f"{request_variant.title} ({request_variant.method}) has unsupported params shape "
            f"{request_variant.params_shape.value!r} for response derivation"
        )
        return None

    def _validate_response_bijection(
        self,
        named_schemas: dict[str, list[NamedSchema]],
        mappings: list[RequestResponseMapping],
        errors: list[str],
    ) -> None:
        mapped_response_names = {mapping.response_type_name for mapping in mappings}
        all_response_names = {
            name for name in named_schemas if name.endswith("Response") and name != "JSONRPCResponse"
        }

        response_to_requests: dict[str, list[ProtocolMessageVariant]] = {}
        for mapping in mappings:
            response_to_requests.setdefault(mapping.response_type_name, []).append(mapping.request)

        for response_name, requests in sorted(response_to_requests.items()):
            if len(requests) <= 1:
                continue
            if response_name in ALLOWED_SHARED_RESPONSE_TYPES:
                continue
            errors.append(
                f"response type {response_name!r} is claimed by "
                + ", ".join(request.title for request in requests)
            )

        unmapped_response_names = sorted(all_response_names - mapped_response_names)
        for response_name in unmapped_response_names:
            errors.append(f"response type {response_name!r} is not mapped from any request")

    def _lookup_named_schema(
        self,
        named_schemas: dict[str, list[NamedSchema]],
        name: str,
    ) -> NamedSchema | None:
        candidates = named_schemas.get(name, [])
        if not candidates:
            return None
        if len(candidates) == 1:
            return candidates[0]

        v2_candidates = [candidate for candidate in candidates if candidate.qualified_name.startswith("v2.")]
        if len(v2_candidates) == 1:
            return v2_candidates[0]

        return None

    def _classify_params_shape(self, params_node: SchemaNode | None) -> ParamsShape:
        if params_node is None:
            return ParamsShape.MISSING
        if params_node.ref is not None:
            return ParamsShape.REF
        if params_node.types == ("null",):
            return ParamsShape.NULL
        return ParamsShape.INLINE
