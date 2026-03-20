from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import warnings
from xml.etree import ElementTree as ET

import xmlschema

from .protocol import ParamsShape, ProtocolAnalysis, ProtocolMessageVariant


@dataclass(frozen=True)
class ServiceIrTypeExpr:
    text: str | None = None
    array_item: ServiceIrTypeExpr | None = None
    map_value: ServiceIrTypeExpr | None = None
    variant_alternatives: tuple["ServiceIrTypeExpr", ...] | None = None
    object_members: tuple["ServiceIrMember", ...] | None = None
    object_allows_anything: bool = False
    comments: tuple[str, ...] = ()


@dataclass(frozen=True)
class ServiceIrMember:
    id: str
    use: str
    type_expr: ServiceIrTypeExpr
    comments: tuple[str, ...] = ()


@dataclass(frozen=True)
class ServiceIrParamField:
    id: str
    use: str
    type_expr: ServiceIrTypeExpr
    comments: tuple[str, ...] = ()


@dataclass(frozen=True)
class ServiceIrEntry:
    kind: str
    origin: str
    method: str
    title: str
    has_params_element: bool
    params_is_null: bool = False
    comments: tuple[str, ...] = ()
    params_comments: tuple[str, ...] = ()
    response_comments: tuple[str, ...] = ()
    params_expr: ServiceIrTypeExpr | None = None
    param_fields: tuple[ServiceIrParamField, ...] = ()
    response_expr: ServiceIrTypeExpr | None = None


@dataclass(frozen=True)
class ServiceIrEnumerator:
    id: str
    type_expr: ServiceIrTypeExpr | None = None
    comments: tuple[str, ...] = ()


@dataclass(frozen=True)
class ServiceIrEnumerationEntry:
    id: str
    enumerators: tuple[ServiceIrEnumerator, ...]
    extended: bool = False
    comments: tuple[str, ...] = ()


@dataclass(frozen=True)
class ServiceIrStructEntry:
    id: str
    expr: ServiceIrTypeExpr
    comments: tuple[str, ...] = ()


@dataclass(frozen=True)
class ServiceIrUnionAlternative:
    id: str
    type_expr: ServiceIrTypeExpr | None = None
    comments: tuple[str, ...] = ()


@dataclass(frozen=True)
class ServiceIrUnionEntry:
    id: str
    discriminator: str | None
    alternatives: tuple[ServiceIrUnionAlternative, ...]
    comments: tuple[str, ...] = ()


class ServiceIrExporter:
    _TYPE_TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_.]*")
    _NON_NAMED_TYPE_TOKENS = {
        "any",
        "array",
        "boolean",
        "enum",
        "integer",
        "map",
        "never",
        "null",
        "number",
        "object",
        "string",
        "tuple",
    }

    def build_entries(self, analysis: ProtocolAnalysis) -> list[ServiceIrEntry]:
        entries: list[ServiceIrEntry] = []

        for variant in analysis.client_requests.variants:
            entries.append(self._build_entry(analysis, variant, origin="client", kind="request"))
        for variant in analysis.server_requests.variants:
            entries.append(self._build_entry(analysis, variant, origin="server", kind="request"))
        for variant in analysis.client_notifications.variants:
            entries.append(
                self._build_entry(analysis, variant, origin="client", kind="notification")
            )
        for variant in analysis.server_notifications.variants:
            entries.append(
                self._build_entry(analysis, variant, origin="server", kind="notification")
            )

        return sorted(entries, key=lambda entry: (entry.kind, entry.origin, entry.method))

    def _directly_mentioned_named_types_in_messages(
        self,
        analysis: ProtocolAnalysis,
    ) -> set[str]:
        known_type_names = set(analysis.named_schemas)
        mentioned_type_names: set[str] = set()

        for entry in self.build_entries(analysis):
            if entry.params_expr is not None:
                mentioned_type_names.update(
                    self._collect_named_type_mentions_from_expr(
                        entry.params_expr,
                        known_type_names,
                    )
                )
            for param_field in entry.param_fields:
                mentioned_type_names.update(
                    self._collect_named_type_mentions_from_expr(
                        param_field.type_expr,
                        known_type_names,
                    )
                )
            if entry.response_expr is not None:
                mentioned_type_names.update(
                    self._collect_named_type_mentions_from_expr(
                        entry.response_expr,
                        known_type_names,
                    )
                )

        return mentioned_type_names

    def _reachable_named_types(self, analysis: ProtocolAnalysis) -> set[str]:
        known_type_names = set(analysis.named_schemas)
        pending = list(self._directly_mentioned_named_types_in_messages(analysis))
        reachable: set[str] = set()

        while pending:
            type_name = pending.pop()
            if type_name in reachable:
                continue
            reachable.add(type_name)

            named_schema = self._lookup_named_schema(analysis, type_name)
            if named_schema is None:
                continue

            nested = self._collect_named_type_mentions_from_schema_node(
                named_schema.node,
                known_type_names,
            )
            for nested_type_name in nested:
                if nested_type_name not in reachable:
                    pending.append(nested_type_name)

        return reachable

    def build_type_names(self, analysis: ProtocolAnalysis) -> list[str]:
        mentioned_type_names = self._reachable_named_types(analysis)
        return sorted(
            type_name
            for type_name in mentioned_type_names
            if not self._is_plain_string_type_name(analysis, type_name)
            and not self._is_inline_variant_type_name(analysis, type_name)
            and not self._is_enumeration_type_name(analysis, type_name)
            and not self._is_union_type_name(analysis, type_name)
            and not self._is_struct_type_name(analysis, type_name)
        )

    def build_enumeration_entries(
        self,
        analysis: ProtocolAnalysis,
    ) -> list[ServiceIrEnumerationEntry]:
        mentioned_type_names = self._reachable_named_types(analysis)
        entries: list[ServiceIrEnumerationEntry] = []

        for type_name in sorted(mentioned_type_names):
            enumorstruct_entry = self._enumorstruct_enumeration_entry_for_type_name(
                analysis,
                type_name,
            )
            if enumorstruct_entry is not None:
                entries.append(enumorstruct_entry)
                continue
            enumext_entry = self._enumext_enumeration_entry_for_type_name(
                analysis,
                type_name,
            )
            if enumext_entry is not None:
                entries.append(enumext_entry)
                continue
            enumerators = self._enumerators_for_type_name(analysis, type_name)
            if enumerators is None:
                continue
            entries.append(
                ServiceIrEnumerationEntry(
                    id=type_name,
                    enumerators=enumerators,
                    comments=self._node_comments(self._lookup_named_schema(analysis, type_name).node),
                )
            )

        return entries

    def build_union_entries(
        self,
        analysis: ProtocolAnalysis,
    ) -> list[ServiceIrUnionEntry]:
        mentioned_type_names = self._reachable_named_types(analysis)
        entries: list[ServiceIrUnionEntry] = []

        for type_name in sorted(mentioned_type_names):
            union_info = self._discriminated_object_union_info(analysis, type_name)
            if union_info is None:
                continue
            named_schema = self._lookup_named_schema(analysis, type_name)
            if named_schema is None:
                continue
            discriminator, alternative_ids = union_info
            entries.append(
                ServiceIrUnionEntry(
                    id=type_name,
                    discriminator=discriminator,
                    alternatives=tuple(
                        ServiceIrUnionAlternative(
                            id=alternative_id,
                            type_expr=ServiceIrTypeExpr(
                                text=self._struct_name_for_union_alternative(
                                    type_name,
                                    alternative_id,
                                )
                            ),
                            comments=self._comments_from_nodes(
                                branch_node,
                                self._resolve_object_union_branch(analysis, branch_node),
                            ),
                        )
                        for branch_node, alternative_id in zip(
                            named_schema.node.one_of,
                            alternative_ids,
                            strict=True,
                        )
                    ),
                    comments=self._node_comments(named_schema.node),
                )
            )

        return entries

    def build_struct_entries(
        self,
        analysis: ProtocolAnalysis,
    ) -> list[ServiceIrStructEntry]:
        mentioned_type_names = self._reachable_named_types(analysis)
        inline_params_struct_names = self._inline_message_params_struct_type_names(analysis)
        entries: list[ServiceIrStructEntry] = []

        for type_name in sorted(mentioned_type_names):
            if type_name in inline_params_struct_names:
                continue
            enumorstruct_struct_entry = self._enumorstruct_struct_entry_for_type_name(
                analysis,
                type_name,
            )
            if enumorstruct_struct_entry is not None:
                entries.append(enumorstruct_struct_entry)
                continue
            enumext_struct_entries = self._enumext_struct_entries_for_type_name(
                analysis,
                type_name,
            )
            if enumext_struct_entries is not None:
                entries.extend(enumext_struct_entries)
                continue
            if self._is_union_type_name(analysis, type_name):
                discriminator_info = self._discriminated_object_union_info(analysis, type_name)
                if discriminator_info is None:
                    continue
                discriminator, alternative_ids = discriminator_info
                named_schema = self._lookup_named_schema(analysis, type_name)
                if named_schema is None:
                    continue
                for branch_node, alternative_id in zip(
                    named_schema.node.one_of,
                    alternative_ids,
                    strict=True,
                ):
                    resolved_branch = self._resolve_object_union_branch(
                        analysis,
                        branch_node,
                    )
                    entries.append(
                        ServiceIrStructEntry(
                            id=self._struct_name_for_union_alternative(
                                type_name,
                                alternative_id,
                            ),
                            expr=self._object_expr_from_node(
                                analysis,
                                resolved_branch,
                                stack={type_name},
                                exclude_properties={discriminator},
                            ),
                            comments=self._comments_from_nodes(branch_node, resolved_branch),
                        )
                    )
                continue
            if not self._is_struct_type_name(analysis, type_name):
                continue
            named_schema = self._lookup_named_schema(analysis, type_name)
            if named_schema is None:
                continue
            entries.append(
                ServiceIrStructEntry(
                    id=type_name,
                    expr=self._object_expr_from_node(
                        analysis,
                        named_schema.node,
                        stack={type_name},
                    ),
                    comments=self._node_comments(named_schema.node),
                )
            )

        return entries

    def build_tree(self, analysis: ProtocolAnalysis) -> ET.ElementTree:
        self._emitted_comments: set[str] = set()
        type_names = self.build_type_names(analysis)
        if type_names:
            warnings.warn(
                "service IR still contains named <types> entries: "
                + ", ".join(type_names[:10])
                + (" ..." if len(type_names) > 10 else ""),
                stacklevel=2,
            )
        enumeration_entries = self.build_enumeration_entries(analysis)
        struct_entries = self.build_struct_entries(analysis)
        union_entries = self.build_union_entries(analysis)
        self._assert_catalog_names_disjoint(
            type_names=type_names,
            enumeration_entries=enumeration_entries,
            struct_entries=struct_entries,
            union_entries=union_entries,
        )

        root = ET.Element("service")

        if type_names:
            types_el = ET.SubElement(root, "types")
            for type_name in type_names:
                ET.SubElement(types_el, "type", {"id": type_name})

        enumerations_el = ET.SubElement(root, "enumerations")
        for enumeration_entry in enumeration_entries:
            tag_name = "extendedEnumeration" if enumeration_entry.extended else "enumeration"
            enumeration_el = ET.SubElement(
                enumerations_el,
                tag_name,
                {"id": enumeration_entry.id},
            )
            self._append_comments(enumeration_el, enumeration_entry.comments)
            for enumerator in enumeration_entry.enumerators:
                enumerator_el = ET.SubElement(
                    enumeration_el,
                    "enumerator",
                    {"id": enumerator.id},
                )
                self._append_comments(enumerator_el, enumerator.comments)
                if enumerator.type_expr is not None:
                    self._append_type_expr(enumerator_el, enumerator.type_expr)

        structs_el = ET.SubElement(root, "structs")
        for struct_entry in struct_entries:
            struct_el = ET.SubElement(
                structs_el,
                "struct",
                {"id": struct_entry.id},
            )
            self._append_comments(struct_el, struct_entry.comments)
            self._append_struct_body(struct_el, struct_entry.expr)

        unions_el = ET.SubElement(root, "unions")
        for union_entry in union_entries:
            union_el = ET.SubElement(
                unions_el,
                "union",
                self._union_attributes(union_entry),
            )
            self._append_comments(union_el, union_entry.comments)
            for alternative in union_entry.alternatives:
                alternative_el = ET.SubElement(
                    union_el,
                    "alternative",
                    {"id": alternative.id},
                )
                self._append_comments(alternative_el, alternative.comments)
                if alternative.type_expr is not None:
                    self._append_type_expr(alternative_el, alternative.type_expr)

        messages_el = ET.SubElement(root, "messages")
        for entry in self.build_entries(analysis):
            self._append_message_entry(messages_el, entry)

        tree = ET.ElementTree(root)
        ET.indent(tree, space="  ")
        return tree

    def write_and_validate(
        self,
        analysis: ProtocolAnalysis,
        output_path: Path,
        xsd_path: Path,
    ) -> None:
        tree = self.build_tree(analysis)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        xml_bytes = ET.tostring(tree.getroot(), encoding="utf-8")
        with output_path.open("wb") as handle:
            handle.write(b"<?xml version='1.0' encoding='utf-8'?>\n")
            handle.write(b"<!-- AUTOMATICALLY GENERATED - DO NOT EDIT -->\n")
            handle.write(xml_bytes)
        self.validate(output_path, xsd_path)

    def validate(self, xml_path: Path, xsd_path: Path) -> None:
        schema = xmlschema.XMLSchema(xsd_path)
        schema.validate(xml_path)
        self._validate_type_coverage(xml_path)

    def _build_entry(
        self,
        analysis: ProtocolAnalysis,
        variant: ProtocolMessageVariant,
        *,
        origin: str,
        kind: str,
    ) -> ServiceIrEntry:
        has_params_element = variant.params_shape != ParamsShape.MISSING
        params_expr: ServiceIrTypeExpr | None = None
        param_fields: tuple[ServiceIrParamField, ...] = ()
        params_comments: tuple[str, ...] = ()

        if variant.params_shape == ParamsShape.REF and variant.params_type_name is not None:
            named_schema = self._lookup_named_schema(analysis, variant.params_type_name)
            params_comments = self._comments_from_nodes(
                variant.params_schema,
                named_schema.node if named_schema is not None else None,
            )
            if (
                named_schema is not None
                and variant.params_type_name in self._inline_message_params_struct_type_names(analysis)
            ):
                param_fields = self._param_fields_from_object_node(
                    analysis,
                    named_schema.node,
                    stack={variant.params_type_name},
                )
            else:
                params_expr = self._type_expr_for_named_type_name(
                    analysis,
                    variant.params_type_name,
                    stack=set(),
                )
        elif variant.params_schema is not None:
            params_comments = self._node_comments(variant.params_schema)

        response_expr: ServiceIrTypeExpr | None = None
        response_comments: tuple[str, ...] = ()
        if kind == "request":
            response_mapping = self._response_mapping_for_request(analysis, variant)
            if response_mapping is not None:
                response_expr = self._type_expr_for_named_type_name(
                    analysis,
                    response_mapping.response_type_name,
                    stack=set(),
                )
                response_comments = self._node_comments(response_mapping.response_schema.node)

        return ServiceIrEntry(
            kind=kind,
            origin=origin,
            method=variant.method,
            title=variant.title,
            has_params_element=has_params_element,
            params_is_null=variant.params_shape == ParamsShape.NULL,
            comments=self._node_comments(variant.node),
            params_comments=params_comments,
            param_fields=param_fields,
            response_comments=response_comments,
            params_expr=params_expr,
            response_expr=response_expr,
        )

    def _append_message_entry(self, parent: ET.Element, entry: ServiceIrEntry) -> None:
        element = ET.SubElement(
            parent,
            entry.kind,
            {
                "origin": entry.origin,
                "method": entry.method,
                "title": entry.title,
            },
        )
        self._append_comments(element, entry.comments)

        if entry.has_params_element:
            params_attrs: dict[str, str] = {}
            if entry.params_is_null:
                params_attrs["null"] = "true"
            params_el = ET.SubElement(element, "params", params_attrs)
            self._append_comments(
                params_el,
                self._merge_comments(
                    entry.params_comments,
                    entry.params_expr.comments
                    if self._is_empty_object_expr(entry.params_expr)
                    else (),
                ),
            )
            for param_field in entry.param_fields:
                param_el = ET.SubElement(
                    params_el,
                    "param",
                    {
                        "id": param_field.id,
                        "use": param_field.use,
                    },
                )
                self._append_comments(param_el, param_field.comments)
                self._append_type_expr(param_el, param_field.type_expr)
            if (
                not entry.param_fields
                and entry.params_expr is not None
                and not self._is_empty_object_expr(entry.params_expr)
            ):
                self._append_type_expr(params_el, entry.params_expr)

        if entry.kind == "request" and entry.response_expr is not None:
            response_el = ET.SubElement(element, "response")
            self._append_comments(
                response_el,
                self._merge_comments(
                    entry.response_comments,
                    entry.response_expr.comments
                    if self._is_empty_object_expr(entry.response_expr)
                    else (),
                ),
            )
            if not self._is_empty_object_expr(entry.response_expr):
                self._append_type_expr(response_el, entry.response_expr)

    def _append_type_expr(self, parent: ET.Element, expr: ServiceIrTypeExpr) -> None:
        last_comment = self._append_comments(parent, expr.comments)
        if last_comment is None:
            last_comment = self._last_leading_comment(parent)
        if expr.array_item is not None:
            array_el = ET.SubElement(parent, "array")
            self._append_type_expr(array_el, expr.array_item)
            return

        if expr.map_value is not None:
            map_el = ET.SubElement(parent, "map")
            self._append_type_expr(map_el, expr.map_value)
            return

        if expr.variant_alternatives is not None:
            variant_el = ET.SubElement(parent, "variant")
            for alternative_expr in expr.variant_alternatives:
                alternative_el = ET.SubElement(variant_el, "alternative")
                self._append_type_expr(alternative_el, alternative_expr)
            return

        if expr.object_members is not None:
            object_el = ET.SubElement(parent, "object")
            for member in expr.object_members:
                member_el = ET.SubElement(
                    object_el,
                    "member",
                    {
                        "id": member.id,
                        "use": member.use,
                    },
                )
                self._append_type_expr(member_el, member.type_expr)
            if expr.object_allows_anything:
                ET.SubElement(object_el, "anything")
            return

        self._set_text_after_comments(parent, expr.text, last_comment)

    def _is_empty_object_expr(self, expr: ServiceIrTypeExpr | None) -> bool:
        return (
            expr is not None
            and expr.object_members is not None
            and not expr.object_members
            and not expr.object_allows_anything
            and expr.text is None
            and expr.array_item is None
            and expr.map_value is None
            and expr.variant_alternatives is None
        )

    def _append_struct_body(self, parent: ET.Element, expr: ServiceIrTypeExpr) -> None:
        if expr.object_members is None:
            raise ValueError("struct body must be an object expression")
        for member in expr.object_members:
            member_el = ET.SubElement(
                parent,
                "member",
                {
                    "id": member.id,
                    "use": member.use,
                },
            )
            self._append_comments(member_el, member.comments)
            self._append_type_expr(member_el, member.type_expr)
        if expr.object_allows_anything:
            ET.SubElement(parent, "anything")

    def _append_comments(self, parent: ET.Element, comments: tuple[str, ...]) -> ET.Element | None:
        last_comment: ET.Element | None = None
        for comment in comments:
            normalized = comment.strip()
            if not normalized or normalized in self._emitted_comments:
                continue
            self._emitted_comments.add(normalized)
            last_comment = ET.SubElement(parent, "comment")
            last_comment.text = normalized
        return last_comment

    def _set_text_after_comments(
        self,
        parent: ET.Element,
        text: str | None,
        last_comment: ET.Element | None,
    ) -> None:
        if text is None:
            return
        if last_comment is None:
            parent.text = text
            return
        last_comment.tail = text

    def _last_leading_comment(self, parent: ET.Element) -> ET.Element | None:
        last_comment: ET.Element | None = None
        for child in parent:
            if child.tag != "comment":
                break
            last_comment = child
        return last_comment

    def _response_for_request(
        self,
        analysis: ProtocolAnalysis,
        variant: ProtocolMessageVariant,
    ) -> str | None:
        mapping = self._response_mapping_for_request(analysis, variant)
        if mapping is None:
            return None
        return mapping.response_type_name

    def _response_mapping_for_request(
        self,
        analysis: ProtocolAnalysis,
        variant: ProtocolMessageVariant,
    ):
        for mapping in analysis.request_response_mappings:
            if mapping.request is variant:
                return mapping
        return None

    def _param_fields_from_object_node(
        self,
        analysis: ProtocolAnalysis,
        node,
        *,
        stack: set[str],
    ) -> tuple[ServiceIrParamField, ...]:
        required_names = set(node.required)
        fields: list[ServiceIrParamField] = []

        for property_name, property_node in node.properties.items():
            fields.append(
                ServiceIrParamField(
                    id=property_name,
                    use=self._field_use(
                        property_node,
                        required=property_name in required_names,
                    ),
                    type_expr=self._type_expr_for_field_node(
                        analysis,
                        property_node,
                        required=property_name in required_names,
                        stack=stack,
                    ),
                    comments=self._comments_for_property_node(
                        analysis,
                        property_node,
                    ),
                )
            )

        return tuple(fields)

    def _inline_message_params_struct_type_names(
        self,
        analysis: ProtocolAnalysis,
    ) -> set[str]:
        known_type_names = set(analysis.named_schemas)
        protocol_wrapper_schema_names = {
            "ClientRequest",
            "ServerRequest",
            "ClientNotification",
            "ServerNotification",
        }
        params_type_names: set[str] = set()
        non_param_references: set[str] = set()

        for family in (
            analysis.client_requests,
            analysis.server_requests,
            analysis.client_notifications,
            analysis.server_notifications,
        ):
            for variant in family.variants:
                if variant.params_shape == ParamsShape.REF and variant.params_type_name is not None:
                    params_type_names.add(variant.params_type_name)

        for mapping in analysis.request_response_mappings:
            non_param_references.add(mapping.response_type_name)

        for schema_name, candidates in analysis.named_schemas.items():
            if schema_name in protocol_wrapper_schema_names:
                continue
            for candidate in candidates:
                for referenced_name in self._collect_named_type_mentions_from_schema_node(
                    candidate.node,
                    known_type_names,
                ):
                    if referenced_name != schema_name:
                        non_param_references.add(referenced_name)

        return {
            type_name
            for type_name in params_type_names
            if self._is_struct_type_name(analysis, type_name)
            and type_name not in non_param_references
        }

    def _type_expr_for_named_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
        *,
        stack: set[str],
    ) -> ServiceIrTypeExpr:
        named_schema = self._lookup_named_schema(analysis, type_name)
        if named_schema is None:
            return ServiceIrTypeExpr(text=type_name)

        if type_name in stack:
            return ServiceIrTypeExpr(text=type_name)

        if self._is_plain_string_type_name(analysis, type_name):
            return ServiceIrTypeExpr(
                text="string",
                comments=self._node_comments(named_schema.node),
            )

        enumorstruct_variant_expr = self._enumorstruct_variant_expr_for_type_name(
            analysis,
            type_name,
        )
        if enumorstruct_variant_expr is not None:
            return self._expr_with_comments(enumorstruct_variant_expr, self._node_comments(named_schema.node))

        if self._is_inline_variant_type_name(analysis, type_name):
            return self._expr_with_comments(
                self._variant_expr_from_named_type_name(
                    analysis,
                    type_name,
                    stack={*stack, type_name},
                ),
                self._node_comments(named_schema.node),
            )

        if self._is_empty_object_schema(named_schema.node):
            return ServiceIrTypeExpr(
                object_members=(),
                comments=self._node_comments(named_schema.node),
            )

        if self._is_struct_type_name(analysis, type_name):
            return ServiceIrTypeExpr(text=type_name)

        if self._is_inlineable_object_schema(named_schema.node):
            return self._expr_with_comments(
                self._object_expr_from_node(
                    analysis,
                    named_schema.node,
                    stack={*stack, type_name},
                ),
                self._node_comments(named_schema.node),
            )

        return ServiceIrTypeExpr(text=type_name)

    def _type_expr_for_node(
        self,
        analysis: ProtocolAnalysis,
        node,
        *,
        stack: set[str],
    ) -> ServiceIrTypeExpr:
        if node.ref is not None:
            return self._type_expr_for_named_type_name(
                analysis,
                node.ref.split("/")[-1],
                stack=stack,
            )

        if node.one_of:
            if len(node.one_of) == 1:
                return self._type_expr_for_node(analysis, node.one_of[0], stack=stack)
            return ServiceIrTypeExpr(
                text=self._join_type_parts(
                    self._render_type_text(analysis, branch) for branch in node.one_of
                )
            )
        if node.any_of:
            if len(node.any_of) == 1:
                return self._type_expr_for_node(analysis, node.any_of[0], stack=stack)
            return ServiceIrTypeExpr(
                text=self._join_type_parts(
                    self._render_type_text(analysis, branch) for branch in node.any_of
                )
            )
        if node.all_of:
            if len(node.all_of) == 1:
                return self._type_expr_for_node(analysis, node.all_of[0], stack=stack)
            return ServiceIrTypeExpr(
                text=self._join_type_parts(
                    self._render_type_text(analysis, branch) for branch in node.all_of
                )
            )

        if isinstance(node.items, tuple):
            rendered_items = [self._render_type_text(analysis, item) for item in node.items]
            return ServiceIrTypeExpr(text=f"tuple<{', '.join(rendered_items)}>")
        if node.items is not None:
            return ServiceIrTypeExpr(
                array_item=self._type_expr_for_node(analysis, node.items, stack=stack)
            )

        if (
            node.types == ("object",)
            and not node.properties
            and node.additional_properties not in (None, False)
        ):
            if isinstance(node.additional_properties, bool):
                return ServiceIrTypeExpr(map_value=ServiceIrTypeExpr(text="any"))
            return ServiceIrTypeExpr(
                map_value=self._type_expr_for_node(
                    analysis,
                    node.additional_properties,
                    stack=stack,
                )
            )

        if self._is_inlineable_object_schema(node):
            return self._object_expr_from_node(analysis, node, stack=stack)

        return ServiceIrTypeExpr(text=self._render_type_text(analysis, node))

    def _variant_expr_from_named_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
        *,
        stack: set[str],
    ) -> ServiceIrTypeExpr:
        named_schema = self._lookup_named_schema(analysis, type_name)
        if named_schema is None:
            return ServiceIrTypeExpr(text=type_name)
        variant_expr = self._variant_expr_from_schema_node(
            analysis,
            named_schema.node,
            stack=stack,
        )
        if variant_expr is None:
            return ServiceIrTypeExpr(text=type_name)
        return variant_expr

    def _variant_expr_from_schema_node(
        self,
        analysis: ProtocolAnalysis,
        node,
        *,
        stack: set[str],
    ) -> ServiceIrTypeExpr | None:
        branches = node.one_of or node.any_of or node.all_of
        if not branches:
            return None

        alternatives: list[ServiceIrTypeExpr] = []
        seen_serializations: set[str] = set()

        for branch in branches:
            alternative_expr = self._type_expr_for_node(analysis, branch, stack=stack)
            serialized = ET.tostring(
                self._type_expr_to_element(alternative_expr),
                encoding="unicode",
            )
            if serialized in seen_serializations:
                continue
            seen_serializations.add(serialized)
            alternatives.append(alternative_expr)

        if not alternatives:
            return None

        return ServiceIrTypeExpr(variant_alternatives=tuple(alternatives))

    def _type_expr_to_element(self, expr: ServiceIrTypeExpr) -> ET.Element:
        root = ET.Element("typeexpr")
        self._append_type_expr(root, expr)
        return root

    def _type_expr_for_field_node(
        self,
        analysis: ProtocolAnalysis,
        node,
        *,
        required: bool,
        stack: set[str],
    ) -> ServiceIrTypeExpr:
        if not required and self._is_top_level_nullable(node):
            return self._type_expr_for_node_without_top_level_null(
                analysis,
                node,
                stack=stack,
            )
        return self._type_expr_for_node(analysis, node, stack=stack)

    def _type_expr_for_node_without_top_level_null(
        self,
        analysis: ProtocolAnalysis,
        node,
        *,
        stack: set[str],
    ) -> ServiceIrTypeExpr:
        non_null_one_of = tuple(
            branch for branch in node.one_of if not self._is_null_only_schema(branch)
        )
        if node.one_of and non_null_one_of:
            if len(non_null_one_of) == 1:
                return self._type_expr_for_node_without_top_level_null(
                    analysis,
                    non_null_one_of[0],
                    stack=stack,
                )
            return ServiceIrTypeExpr(
                text=self._join_type_parts(
                    self._render_type_text(analysis, branch) for branch in non_null_one_of
                )
            )

        non_null_any_of = tuple(
            branch for branch in node.any_of if not self._is_null_only_schema(branch)
        )
        if node.any_of and non_null_any_of:
            if len(non_null_any_of) == 1:
                return self._type_expr_for_node_without_top_level_null(
                    analysis,
                    non_null_any_of[0],
                    stack=stack,
                )
            return ServiceIrTypeExpr(
                text=self._join_type_parts(
                    self._render_type_text(analysis, branch) for branch in non_null_any_of
                )
            )

        non_null_all_of = tuple(
            branch for branch in node.all_of if not self._is_null_only_schema(branch)
        )
        if node.all_of and non_null_all_of:
            if len(non_null_all_of) == 1:
                return self._type_expr_for_node_without_top_level_null(
                    analysis,
                    non_null_all_of[0],
                    stack=stack,
                )
            return ServiceIrTypeExpr(
                text=self._join_type_parts(
                    self._render_type_text(analysis, branch) for branch in non_null_all_of
                )
            )

        if node.types and "null" in node.types:
            non_null_types = tuple(type_name for type_name in node.types if type_name != "null")
            if node.items is not None and non_null_types == ("array",):
                return ServiceIrTypeExpr(
                    array_item=self._type_expr_for_node(analysis, node.items, stack=stack)
                )
            if (
                non_null_types == ("object",)
                and not node.properties
                and node.additional_properties not in (None, False)
            ):
                if isinstance(node.additional_properties, bool):
                    return ServiceIrTypeExpr(map_value=ServiceIrTypeExpr(text="any"))
                return ServiceIrTypeExpr(
                    map_value=self._type_expr_for_node(
                        analysis,
                        node.additional_properties,
                        stack=stack,
                    )
                )
            if non_null_types == ("object",) and self._is_inlineable_object_schema(node):
                return self._object_expr_from_node(analysis, node, stack=stack)
            if non_null_types:
                return ServiceIrTypeExpr(text=self._join_type_parts(non_null_types))

        return self._type_expr_for_node(analysis, node, stack=stack)

    def _object_expr_from_node(
        self,
        analysis: ProtocolAnalysis,
        node,
        *,
        stack: set[str],
        exclude_properties: set[str] | frozenset[str] = frozenset(),
    ) -> ServiceIrTypeExpr:
        required_names = set(node.required)
        members: list[ServiceIrMember] = []

        for property_name, property_node in node.properties.items():
            if property_name in exclude_properties:
                continue
            members.append(
                ServiceIrMember(
                    id=property_name,
                    use=self._field_use(
                        property_node,
                        required=property_name in required_names,
                    ),
                    type_expr=self._type_expr_for_field_node(
                        analysis,
                        property_node,
                        required=property_name in required_names,
                        stack=stack,
                    ),
                    comments=self._comments_for_property_node(
                        analysis,
                        property_node,
                    ),
                )
            )

        return ServiceIrTypeExpr(
            object_members=tuple(members),
            object_allows_anything=node.additional_properties is True,
        )

    def _expr_with_comments(
        self,
        expr: ServiceIrTypeExpr,
        comments: tuple[str, ...],
    ) -> ServiceIrTypeExpr:
        merged_comments = self._merge_comments(expr.comments, comments)
        if merged_comments == expr.comments:
            return expr
        return ServiceIrTypeExpr(
            text=expr.text,
            array_item=expr.array_item,
            map_value=expr.map_value,
            variant_alternatives=expr.variant_alternatives,
            object_members=expr.object_members,
            object_allows_anything=expr.object_allows_anything,
            comments=merged_comments,
        )

    def _comments_for_property_node(
        self,
        analysis: ProtocolAnalysis,
        property_node,
    ) -> tuple[str, ...]:
        comments = self._node_comments(property_node)
        if comments:
            return comments
        if property_node.ref is None:
            return ()
        type_name = property_node.ref.split("/")[-1]
        named_schema = self._lookup_named_schema(analysis, type_name)
        if named_schema is None:
            return ()
        if (
            self._is_plain_string_type_name(analysis, type_name)
            or self._is_inline_variant_type_name(analysis, type_name)
            or self._is_empty_object_schema(named_schema.node)
            or self._is_inlineable_object_schema(named_schema.node)
            or self._enumorstruct_info(analysis, type_name) is not None
        ):
            return self._node_comments(named_schema.node)
        return ()

    def _node_comments(self, node) -> tuple[str, ...]:
        if node is None:
            return ()
        format_value = None
        raw = getattr(node, "raw", None)
        if isinstance(raw, dict):
            format_value = raw.get("format")
        format_comment = (
            f"format: {format_value}" if isinstance(format_value, str) and format_value else None
        )
        return self._comments_from_descriptions(
            getattr(node, "description", None),
            format_comment,
        )

    def _comments_from_nodes(self, *nodes) -> tuple[str, ...]:
        return self._comments_from_descriptions(
            *(getattr(node, "description", None) for node in nodes if node is not None)
        )

    def _comments_from_descriptions(self, *descriptions: str | None) -> tuple[str, ...]:
        comments: list[str] = []
        seen: set[str] = set()
        for description in descriptions:
            if description is None:
                continue
            normalized = description.strip()
            if not normalized or normalized in seen:
                continue
            seen.add(normalized)
            comments.append(normalized)
        return tuple(comments)

    def _merge_comments(
        self,
        left: tuple[str, ...],
        right: tuple[str, ...],
    ) -> tuple[str, ...]:
        return self._comments_from_descriptions(*left, *right)

    def _lookup_named_schema(self, analysis: ProtocolAnalysis, name: str):
        candidates = analysis.named_schemas.get(name, [])
        if not candidates:
            return None
        if len(candidates) == 1:
            return candidates[0]

        v2_candidates = [
            candidate for candidate in candidates if candidate.qualified_name.startswith("v2.")
        ]
        if len(v2_candidates) == 1:
            return v2_candidates[0]

        return None

    def _is_union_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> bool:
        return self._discriminated_object_union_info(analysis, type_name) is not None

    def _is_plain_string_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> bool:
        named_schema = self._lookup_named_schema(analysis, type_name)
        if named_schema is None:
            return False
        node = named_schema.node
        return (
            node.types == ("string",)
            and not node.enum_values
            and not node.one_of
            and not node.any_of
            and not node.all_of
            and node.ref is None
        )

    def _is_inline_variant_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> bool:
        named_schema = self._lookup_named_schema(analysis, type_name)
        if named_schema is None:
            return False
        if self._is_enumeration_type_name(analysis, type_name):
            return False
        if self._is_union_type_name(analysis, type_name):
            return False
        if self._is_struct_type_name(analysis, type_name):
            return False
        if self._is_plain_string_type_name(analysis, type_name):
            return False
        if self._enumorstruct_info(analysis, type_name) is not None:
            return True
        return self._variant_expr_from_schema_node(
            analysis,
            named_schema.node,
            stack={type_name},
        ) is not None

    def _discriminated_object_union_info(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> tuple[str, tuple[str, ...]] | None:
        named_schema = self._lookup_named_schema(analysis, type_name)
        if named_schema is None or not named_schema.node.one_of:
            return None

        branches = [
            self._resolve_object_union_branch(analysis, branch)
            for branch in named_schema.node.one_of
        ]
        if not branches or any(branch.types != ("object",) for branch in branches):
            return None

        common_properties = set(branches[0].properties)
        for branch in branches[1:]:
            common_properties &= set(branch.properties)

        discriminators: list[tuple[str, tuple[str, ...]]] = []
        for property_name in sorted(common_properties):
            discriminator_values: list[str] = []
            for branch in branches:
                property_node = branch.properties[property_name]
                literal_value = self._single_string_literal_value(property_node)
                if literal_value is None:
                    break
                discriminator_values.append(literal_value)
            else:
                discriminators.append((property_name, tuple(discriminator_values)))

        if len(discriminators) != 1:
            return None

        discriminator_name, discriminator_values = discriminators[0]
        return discriminator_name, discriminator_values

    def _resolve_object_union_branch(self, analysis: ProtocolAnalysis, branch):
        if branch.ref is None:
            return branch
        branch_name = branch.ref.split("/")[-1]
        named_schema = self._lookup_named_schema(analysis, branch_name)
        if named_schema is None:
            return branch
        return named_schema.node

    def _single_string_literal_value(self, node) -> str | None:
        if node.const_value is not None and isinstance(node.const_value, str):
            return node.const_value
        if node.types == ("string",) and len(node.enum_values) == 1:
            enum_value = node.enum_values[0]
            if isinstance(enum_value, str):
                return enum_value
        return None

    def _enumorstruct_info(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> tuple[tuple[str, ...], str, object] | None:
        named_schema = self._lookup_named_schema(analysis, type_name)
        if named_schema is None or not named_schema.node.one_of:
            return None

        enumerators: list[str] = []
        seen_enumerators: set[str] = set()
        struct_alternative: tuple[str, object] | None = None

        for branch in named_schema.node.one_of:
            string_values = self._string_enumerators_from_node(branch)
            if string_values is not None:
                for value in string_values:
                    if value in seen_enumerators:
                        continue
                    seen_enumerators.add(value)
                    enumerators.append(value)
                continue

            resolved_branch = self._resolve_object_union_branch(analysis, branch)
            if resolved_branch.types != ("object",):
                return None
            if len(resolved_branch.properties) != 1:
                return None
            if set(resolved_branch.required) != set(resolved_branch.properties):
                return None
            if resolved_branch.additional_properties not in (None, False):
                return None

            property_name, property_node = next(iter(resolved_branch.properties.items()))
            resolved_property_node = self._resolve_referenced_node(analysis, property_node)
            if (
                not self._is_inlineable_object_schema(resolved_property_node)
                or self._is_empty_object_schema(resolved_property_node)
            ):
                return None
            if struct_alternative is not None:
                return None
            struct_alternative = (property_name, resolved_property_node)

        if not enumerators or struct_alternative is None:
            return None

        return tuple(enumerators), struct_alternative[0], struct_alternative[1]

    def _enumext_union_info(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> tuple[tuple[str, object | None], ...] | None:
        if self._enumorstruct_info(analysis, type_name) is not None:
            return None
        named_schema = self._lookup_named_schema(analysis, type_name)
        if named_schema is None or not named_schema.node.one_of:
            return None

        alternatives: list[tuple[str, object | None]] = []
        saw_object_wrapper = False

        for branch in named_schema.node.one_of:
            string_values = self._string_enumerators_from_node(branch)
            if string_values is not None:
                for value in string_values:
                    alternatives.append((value, None))
                continue

            resolved_branch = self._resolve_object_union_branch(analysis, branch)
            if resolved_branch.types != ("object",):
                return None
            if len(resolved_branch.properties) != 1:
                return None
            if set(resolved_branch.required) != set(resolved_branch.properties):
                return None
            if resolved_branch.additional_properties not in (None, False):
                return None

            property_name, property_node = next(iter(resolved_branch.properties.items()))
            alternatives.append((property_name, property_node))
            saw_object_wrapper = True

        if not alternatives or not saw_object_wrapper:
            return None

        return tuple(alternatives)

    def _enumext_enumeration_entry_for_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> ServiceIrEnumerationEntry | None:
        union_info = self._enumext_union_info(analysis, type_name)
        if union_info is None:
            return None

        return ServiceIrEnumerationEntry(
            id=type_name,
            extended=True,
            enumerators=tuple(
                ServiceIrEnumerator(
                    id=alternative_id,
                    type_expr=self._enumext_alternative_type_expr(
                        analysis,
                        type_name,
                        alternative_id,
                        value_node,
                    ),
                    comments=self._node_comments(value_node) if value_node is not None else (),
                )
                for alternative_id, value_node in union_info
            ),
            comments=self._node_comments(self._lookup_named_schema(analysis, type_name).node),
        )

    def _enumext_struct_entries_for_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> list[ServiceIrStructEntry] | None:
        union_info = self._enumext_union_info(analysis, type_name)
        if union_info is None:
            return None

        entries: list[ServiceIrStructEntry] = []
        for alternative_id, value_node in union_info:
            if value_node is None:
                continue
            if not self._enumext_alternative_uses_synthetic_struct(analysis, value_node):
                continue
            entries.append(
                ServiceIrStructEntry(
                    id=self._struct_name_for_union_alternative(type_name, alternative_id),
                    expr=self._object_expr_from_node(
                        analysis,
                        value_node,
                        stack={type_name},
                    ),
                    comments=self._node_comments(value_node),
                )
            )
        return entries

    def _enumext_alternative_uses_synthetic_struct(self, analysis: ProtocolAnalysis, node) -> bool:
        if node.ref is not None:
            return False
        return self._is_inlineable_object_schema(node)

    def _resolve_referenced_node(self, analysis: ProtocolAnalysis, node):
        if node.ref is None:
            return node
        type_name = node.ref.split("/")[-1]
        named_schema = self._lookup_named_schema(analysis, type_name)
        if named_schema is None:
            return node
        return named_schema.node

    def _enumext_alternative_type_expr(
        self,
        analysis: ProtocolAnalysis,
        union_name: str,
        alternative_id: str,
        value_node,
    ) -> ServiceIrTypeExpr | None:
        if value_node is None:
            return None
        if self._enumext_alternative_uses_synthetic_struct(analysis, value_node):
            return ServiceIrTypeExpr(
                text=self._struct_name_for_union_alternative(union_name, alternative_id)
            )
        return self._type_expr_for_node(analysis, value_node, stack={union_name})

    def _enumorstruct_enumeration_entry_for_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> ServiceIrEnumerationEntry | None:
        enumorstruct_info = self._enumorstruct_info(analysis, type_name)
        if enumorstruct_info is None:
            return None
        enumerators, _, _ = enumorstruct_info
        return ServiceIrEnumerationEntry(
            id=self._enumorstruct_enum_name(type_name),
            enumerators=tuple(ServiceIrEnumerator(id=enumerator) for enumerator in enumerators),
            comments=self._node_comments(self._lookup_named_schema(analysis, type_name).node),
        )

    def _enumorstruct_struct_entry_for_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> ServiceIrStructEntry | None:
        enumorstruct_info = self._enumorstruct_info(analysis, type_name)
        if enumorstruct_info is None:
            return None
        _, alternative_id, object_node = enumorstruct_info
        return ServiceIrStructEntry(
            id=self._struct_name_for_union_alternative(type_name, alternative_id),
            expr=self._object_expr_from_node(
                analysis,
                object_node,
                stack={type_name},
            ),
            comments=self._node_comments(object_node),
        )

    def _enumorstruct_variant_expr_for_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> ServiceIrTypeExpr | None:
        enumorstruct_info = self._enumorstruct_info(analysis, type_name)
        if enumorstruct_info is None:
            return None
        _, alternative_id, _ = enumorstruct_info
        return ServiceIrTypeExpr(
            variant_alternatives=(
                ServiceIrTypeExpr(text=self._enumorstruct_enum_name(type_name)),
                ServiceIrTypeExpr(
                    text=self._struct_name_for_union_alternative(type_name, alternative_id)
                ),
            )
        )

    def _union_attributes(self, union_entry: ServiceIrUnionEntry) -> dict[str, str]:
        if union_entry.discriminator is None:
            raise AssertionError(
                "union must have discriminator: "
                f"{union_entry.id}"
            )
        return {
            "id": union_entry.id,
            "discriminator": union_entry.discriminator,
        }

    def _enumorstruct_enum_name(self, type_name: str) -> str:
        return f"{type_name}Enum"

    def _struct_name_for_union_alternative(
        self,
        union_name: str,
        alternative_id: str,
    ) -> str:
        return f"{union_name}{self._pascalize_identifier(alternative_id)}"

    def _pascalize_identifier(self, value: str) -> str:
        pieces = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", value)
        pieces = re.split(r"[^A-Za-z0-9]+", pieces)
        return "".join(piece[:1].upper() + piece[1:] for piece in pieces if piece)

    def _assert_catalog_names_disjoint(
        self,
        *,
        type_names: list[str],
        enumeration_entries: list[ServiceIrEnumerationEntry],
        struct_entries: list[ServiceIrStructEntry],
        union_entries: list[ServiceIrUnionEntry],
    ) -> None:
        catalogs = {
            "types": set(type_names),
            "enumerations": {entry.id for entry in enumeration_entries},
            "structs": {entry.id for entry in struct_entries},
            "unions": {entry.id for entry in union_entries},
        }

        problems: list[str] = []
        catalog_items = list(catalogs.items())
        for index, (left_name, left_values) in enumerate(catalog_items):
            for right_name, right_values in catalog_items[index + 1 :]:
                overlap = sorted(left_values & right_values)
                if overlap:
                    problems.append(
                        f"{left_name}/{right_name} overlap: "
                        + ", ".join(overlap[:20])
                        + (" ..." if len(overlap) > 20 else "")
                    )

        if problems:
            raise AssertionError(
                "catalog name collision while building service IR: " + "; ".join(problems)
            )

    def _is_struct_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> bool:
        named_schema = self._lookup_named_schema(analysis, type_name)
        if named_schema is None:
            return False
        return (
            self._is_inlineable_object_schema(named_schema.node)
            and not self._is_empty_object_schema(named_schema.node)
        )

    def _is_enumeration_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> bool:
        return (
            self._enumerators_for_type_name(analysis, type_name) is not None
            or self._enumext_enumeration_entry_for_type_name(analysis, type_name) is not None
        )

    def _enumerators_for_type_name(
        self,
        analysis: ProtocolAnalysis,
        type_name: str,
    ) -> tuple[ServiceIrEnumerator, ...] | None:
        named_schema = self._lookup_named_schema(analysis, type_name)
        if named_schema is None:
            return None
        node = named_schema.node

        if node.types == ("string",) and node.enum_values:
            return tuple(
                ServiceIrEnumerator(id=str(value))
                for value in node.enum_values
            )

        enum_values = self._string_enumerators_from_union_branches(node.one_of)
        if enum_values is not None:
            return enum_values

        enum_values = self._string_enumerators_from_union_branches(node.any_of)
        if enum_values is not None:
            return enum_values

        enum_values = self._string_enumerators_from_union_branches(node.all_of)
        if enum_values is not None:
            return enum_values

        return None

    def _string_enumerators_from_union_branches(
        self,
        branches,
    ) -> tuple[ServiceIrEnumerator, ...] | None:
        if not branches:
            return None

        enumerators: list[ServiceIrEnumerator] = []
        seen: set[str] = set()
        for branch in branches:
            branch_values = self._string_enumerators_from_node(branch)
            if branch_values is None:
                return None
            for value in branch_values:
                if value in seen:
                    continue
                seen.add(value)
                enumerators.append(
                    ServiceIrEnumerator(
                        id=value,
                        comments=self._node_comments(branch),
                    )
                )

        return tuple(enumerators) if enumerators else None

    def _string_enumerators_from_node(self, node) -> tuple[str, ...] | None:
        if node.const_value is not None:
            if isinstance(node.const_value, str):
                return (node.const_value,)
            return None

        if node.types == ("string",) and node.enum_values:
            values: list[str] = []
            for value in node.enum_values:
                if not isinstance(value, str):
                    return None
                values.append(value)
            return tuple(values)

        return None

    def _is_inlineable_object_schema(self, node) -> bool:
        return (
            node.types == ("object",)
            and not node.one_of
            and not node.any_of
            and not node.all_of
            and (node.additional_properties in (None, False, True))
            and not (not node.properties and node.additional_properties is True)
        )

    def _is_empty_object_schema(self, node) -> bool:
        return (
            node.types == ("object",)
            and not node.one_of
            and not node.any_of
            and not node.all_of
            and not node.properties
            and node.additional_properties in (None, False)
        )

    def _is_null_only_schema(self, node) -> bool:
        return node.types == ("null",)

    def _is_top_level_nullable(self, node) -> bool:
        if "null" in node.types:
            return True
        if node.one_of and any(self._is_null_only_schema(branch) for branch in node.one_of):
            return True
        if node.any_of and any(self._is_null_only_schema(branch) for branch in node.any_of):
            return True
        if node.all_of and any(self._is_null_only_schema(branch) for branch in node.all_of):
            return True
        return False

    def _field_use(self, node, *, required: bool) -> str:
        if not required and self._is_top_level_nullable(node):
            return "nullable"
        return "required" if required else "optional"

    def _render_type_text(self, analysis: ProtocolAnalysis, node) -> str:
        if node.ref is not None:
            named_schema = self._lookup_named_schema(analysis, node.ref.split("/")[-1])
            if named_schema is not None:
                return named_schema.name
            return node.ref.split("/")[-1]

        if node.one_of:
            return self._join_type_parts(
                self._render_type_text(analysis, branch) for branch in node.one_of
            )
        if node.any_of:
            return self._join_type_parts(
                self._render_type_text(analysis, branch) for branch in node.any_of
            )
        if node.all_of:
            return self._join_type_parts(
                self._render_type_text(analysis, branch) for branch in node.all_of
            )

        if isinstance(node.items, tuple):
            rendered_items = [self._render_type_text(analysis, item) for item in node.items]
            return f"tuple<{', '.join(rendered_items)}>"
        if node.items is not None:
            return f"array<{self._render_type_text(analysis, node.items)}>"

        if node.types:
            if node.types == ("object",):
                if not node.properties and node.additional_properties is not None:
                    if isinstance(node.additional_properties, bool):
                        return "map<string, any>" if node.additional_properties else "object"
                    return (
                        "map<string, "
                        f"{self._render_type_text(analysis, node.additional_properties)}>"
                    )
                return "object"
            return "|".join(node.types)

        if node.enum_values:
            return "enum"
        if isinstance(node.raw, bool):
            return "any" if node.raw else "never"
        return "any"

    def _join_type_parts(self, parts) -> str:
        ordered_parts: list[str] = []
        seen: set[str] = set()
        for part in parts:
            if not part or part in seen:
                continue
            seen.add(part)
            ordered_parts.append(part)
        return "|".join(ordered_parts) if ordered_parts else "any"

    def _collect_named_type_mentions_from_expr(
        self,
        expr: ServiceIrTypeExpr,
        known_type_names: set[str],
    ) -> set[str]:
        mentioned: set[str] = set()

        if expr.text is not None:
            mentioned.update(self._extract_named_type_tokens(expr.text, known_type_names))
        if expr.array_item is not None:
            mentioned.update(
                self._collect_named_type_mentions_from_expr(expr.array_item, known_type_names)
            )
        if expr.map_value is not None:
            mentioned.update(
                self._collect_named_type_mentions_from_expr(expr.map_value, known_type_names)
            )
        if expr.variant_alternatives is not None:
            for alternative_expr in expr.variant_alternatives:
                mentioned.update(
                    self._collect_named_type_mentions_from_expr(
                        alternative_expr,
                        known_type_names,
                    )
                )
        if expr.object_members is not None:
            for member in expr.object_members:
                mentioned.update(
                    self._collect_named_type_mentions_from_expr(
                        member.type_expr,
                        known_type_names,
                    )
                )

        return mentioned

    def _collect_named_type_mentions_from_schema_node(
        self,
        node,
        known_type_names: set[str],
    ) -> set[str]:
        mentioned: set[str] = set()

        if node.ref is not None:
            ref_name = node.ref.split("/")[-1]
            if ref_name in known_type_names:
                mentioned.add(ref_name)

        for branch in node.one_of:
            mentioned.update(
                self._collect_named_type_mentions_from_schema_node(branch, known_type_names)
            )
        for branch in node.any_of:
            mentioned.update(
                self._collect_named_type_mentions_from_schema_node(branch, known_type_names)
            )
        for branch in node.all_of:
            mentioned.update(
                self._collect_named_type_mentions_from_schema_node(branch, known_type_names)
            )

        if isinstance(node.items, tuple):
            for item in node.items:
                mentioned.update(
                    self._collect_named_type_mentions_from_schema_node(item, known_type_names)
                )
        elif node.items is not None:
            mentioned.update(
                self._collect_named_type_mentions_from_schema_node(node.items, known_type_names)
            )

        for property_node in node.properties.values():
            mentioned.update(
                self._collect_named_type_mentions_from_schema_node(
                    property_node,
                    known_type_names,
                )
            )

        if not isinstance(node.additional_properties, bool) and node.additional_properties is not None:
            mentioned.update(
                self._collect_named_type_mentions_from_schema_node(
                    node.additional_properties,
                    known_type_names,
                )
            )

        return mentioned

    def _validate_type_coverage(self, xml_path: Path) -> None:
        root = ET.parse(xml_path).getroot()
        messages_el = root.find("messages")
        types_el = root.find("types")
        enumerations_el = root.find("enumerations")
        structs_el = root.find("structs")
        unions_el = root.find("unions")
        if (
            messages_el is None
            or enumerations_el is None
            or structs_el is None
            or unions_el is None
        ):
            raise ValueError(
                "service XML must contain <enumerations>, <structs>, <unions>, and <messages>"
            )

        listed_types = (
            {
                type_el.attrib.get("id", "").strip()
                for type_el in types_el.findall("type")
                if type_el.attrib.get("id", "").strip()
            }
            if types_el is not None
            else set()
        )
        listed_enumerations = {
            enumeration_el.attrib.get("id", "").strip()
            for enumeration_el in enumerations_el.findall("enumeration")
            if enumeration_el.attrib.get("id", "").strip()
        } | {
            enumeration_el.attrib.get("id", "").strip()
            for enumeration_el in enumerations_el.findall("extendedEnumeration")
            if enumeration_el.attrib.get("id", "").strip()
        }
        listed_structs = {
            struct_el.attrib.get("id", "").strip()
            for struct_el in structs_el.findall("struct")
            if struct_el.attrib.get("id", "").strip()
        }
        listed_unions = {
            union_el.attrib.get("id", "").strip()
            for union_el in unions_el.findall("union")
            if union_el.attrib.get("id", "").strip()
        }
        mentioned_types = (
            self._collect_type_mentions_from_messages(messages_el)
            | self._collect_type_mentions_from_enumerations(enumerations_el)
            | self._collect_type_mentions_from_structs(structs_el)
            | self._collect_type_mentions_from_unions(unions_el)
        )

        overlapping_names = sorted(
            (listed_types & listed_enumerations)
            | (listed_types & listed_structs)
            | (listed_enumerations & listed_structs)
            | (listed_types & listed_unions)
            | (listed_enumerations & listed_unions)
            | (listed_structs & listed_unions)
        )
        missing_listed_types = sorted(
            mentioned_types
            - (listed_types | listed_enumerations | listed_structs | listed_unions)
        )
        invalid_union_targets = sorted(
            self._invalid_union_alternative_targets(unions_el, listed_structs)
        )
        invalid_union_attrs = sorted(self._invalid_union_attributes(unions_el))
        invalid_params_null = sorted(self._invalid_message_params_null(messages_el))

        problems: list[str] = []
        if overlapping_names:
            problems.append(
                "names listed in multiple catalogs (<types>/<enumerations>/<structs>/<unions>): "
                + ", ".join(overlapping_names[:20])
                + (" ..." if len(overlapping_names) > 20 else "")
            )
        if missing_listed_types:
            problems.append(
                "names mentioned in messages/enumerations/structs/unions but missing from <types>/<enumerations>/<structs>/<unions>: "
                + ", ".join(missing_listed_types[:20])
                + (" ..." if len(missing_listed_types) > 20 else "")
            )
        if invalid_union_targets:
            problems.append(
                "union alternatives targeting non-struct names: "
                + ", ".join(invalid_union_targets[:20])
                + (" ..." if len(invalid_union_targets) > 20 else "")
            )
        if invalid_union_attrs:
            problems.append(
                "unions must have discriminator: "
                + ", ".join(invalid_union_attrs[:20])
                + (" ..." if len(invalid_union_attrs) > 20 else "")
            )
        if invalid_params_null:
            problems.append(
                "params with null=\"true\" must not contain a nested type expression: "
                + ", ".join(invalid_params_null[:20])
                + (" ..." if len(invalid_params_null) > 20 else "")
            )
        if problems:
            raise ValueError("; ".join(problems))

    def _collect_type_mentions_from_messages(self, messages_el: ET.Element) -> set[str]:
        mentioned: set[str] = set()

        for message_el in messages_el:
            params_el = message_el.find("params")
            if params_el is not None:
                mentioned.update(self._collect_type_mentions_from_expr_element(params_el))

            response_el = message_el.find("response")
            if response_el is not None:
                mentioned.update(self._collect_type_mentions_from_expr_element(response_el))

        return mentioned

    def _collect_type_mentions_from_structs(self, structs_el: ET.Element) -> set[str]:
        mentioned: set[str] = set()
        for struct_el in structs_el.findall("struct"):
            mentioned.update(self._collect_type_mentions_from_expr_element(struct_el))
        return mentioned

    def _collect_type_mentions_from_enumerations(self, enumerations_el: ET.Element) -> set[str]:
        mentioned: set[str] = set()
        for enumeration_el in enumerations_el.findall("extendedEnumeration"):
            for enumerator_el in enumeration_el.findall("enumerator"):
                mentioned.update(self._collect_type_mentions_from_expr_element(enumerator_el))
        return mentioned

    def _collect_type_mentions_from_unions(self, unions_el: ET.Element) -> set[str]:
        mentioned: set[str] = set()
        for union_el in unions_el.findall("union"):
            for alternative_el in union_el.findall("alternative"):
                mentioned.update(self._collect_type_mentions_from_expr_element(alternative_el))
        return mentioned

    def _invalid_union_alternative_targets(
        self,
        unions_el: ET.Element,
        listed_structs: set[str],
    ) -> set[str]:
        invalid: set[str] = set()
        for union_el in unions_el.findall("union"):
            union_id = union_el.attrib.get("id", "").strip()
            for alternative_el in union_el.findall("alternative"):
                text = self._direct_text_content(alternative_el)
                has_child = any(child.tag != "comment" for child in alternative_el)
                if has_child or text not in listed_structs:
                    invalid.add(f"{union_id}:{alternative_el.attrib.get('id', '').strip()}")
        return invalid

    def _invalid_union_attributes(self, unions_el: ET.Element) -> set[str]:
        invalid: set[str] = set()
        for union_el in unions_el.findall("union"):
            union_id = union_el.attrib.get("id", "").strip()
            has_discriminator = bool(union_el.attrib.get("discriminator", "").strip())
            if not has_discriminator:
                invalid.add(union_id)
        return invalid

    def _invalid_message_params_null(self, messages_el: ET.Element) -> set[str]:
        invalid: set[str] = set()
        for message_el in messages_el:
            if message_el.tag not in {"request", "notification"}:
                continue
            params_el = message_el.find("params")
            if params_el is None:
                continue
            if params_el.attrib.get("null") not in {"true", "1"}:
                continue
            if self._collect_type_mentions_from_expr_element(params_el) or any(
                child.tag != "comment" for child in params_el
            ):
                invalid.add(f"{message_el.tag}:{message_el.attrib.get('method', '').strip()}")
        return invalid

    def _collect_type_mentions_from_expr_element(self, element: ET.Element) -> set[str]:
        mentioned = self._extract_named_type_tokens(self._direct_text_content(element))

        array_el = element.find("array")
        if array_el is not None:
            mentioned.update(self._collect_type_mentions_from_expr_element(array_el))

        map_el = element.find("map")
        if map_el is not None:
            mentioned.update(self._collect_type_mentions_from_expr_element(map_el))

        variant_el = element.find("variant")
        if variant_el is not None:
            for alternative_el in variant_el.findall("alternative"):
                mentioned.update(self._collect_type_mentions_from_expr_element(alternative_el))

        object_el = element.find("object")
        if object_el is not None:
            for member_el in object_el.findall("member"):
                mentioned.update(self._collect_type_mentions_from_expr_element(member_el))

        for param_el in element.findall("param"):
            mentioned.update(self._collect_type_mentions_from_expr_element(param_el))

        return mentioned

    def _direct_text_content(self, element: ET.Element) -> str:
        parts: list[str] = []
        if element.text:
            parts.append(element.text)
        for child in element:
            if child.tag == "comment" and child.tail:
                parts.append(child.tail)
        return "".join(parts).strip()

    def _extract_named_type_tokens(
        self,
        text: str,
        known_type_names: set[str] | None = None,
    ) -> set[str]:
        if not text:
            return set()

        tokens = set(self._TYPE_TOKEN_RE.findall(text))
        tokens -= self._NON_NAMED_TYPE_TOKENS
        if known_type_names is not None:
            return {token for token in tokens if token in known_type_names}
        return tokens
