from __future__ import annotations

from pathlib import Path
from xml.etree import ElementTree as ET

from protocol_schema.service_ir import ServiceIrExporter

from .model import (
    ServiceArrayTypeExpr,
    ServiceDescription,
    ServiceEnumeration,
    ServiceEnumerator,
    ServiceMapTypeExpr,
    ServiceMember,
    ServiceMessage,
    ServiceObjectTypeExpr,
    ServiceStruct,
    ServiceTextTypeExpr,
    ServiceTypeDefinition,
    ServiceTypeExpr,
    ServiceUnion,
    ServiceUnionAlternative,
    ServiceVariantAlternative,
    ServiceVariantTypeExpr,
)


class ServiceDescriptionParser:
    def parse_file(self, xml_path: Path, xsd_path: Path) -> ServiceDescription:
        ServiceIrExporter().validate(xml_path, xsd_path)
        root = ET.parse(xml_path).getroot()
        if root.tag != "service":
            raise ValueError(f"Expected <service> root, got <{root.tag}>")

        types_el = root.find("types")
        enumerations_el = root.find("enumerations")
        structs_el = root.find("structs")
        unions_el = root.find("unions")
        messages_el = root.find("messages")
        if (
            enumerations_el is None
            or structs_el is None
            or unions_el is None
            or messages_el is None
        ):
            raise ValueError(
                "service XML must contain <enumerations>, <structs>, <unions>, and <messages>"
            )

        return ServiceDescription(
            comments=self._leading_comments(root),
            types=self._parse_types(types_el),
            enumerations=self._parse_enumerations(enumerations_el),
            structs=self._parse_structs(structs_el),
            unions=self._parse_unions(unions_el),
            messages=self._parse_messages(messages_el),
        )

    def _parse_types(self, types_el: ET.Element | None) -> tuple[ServiceTypeDefinition, ...]:
        if types_el is None:
            return ()
        return tuple(
            ServiceTypeDefinition(
                id=type_el.attrib["id"],
                comments=self._leading_comments(type_el),
            )
            for type_el in types_el.findall("type")
        )

    def _parse_enumerations(self, enumerations_el: ET.Element) -> tuple[ServiceEnumeration, ...]:
        entries: list[ServiceEnumeration] = []
        for enumeration_el in enumerations_el:
            if enumeration_el.tag not in {"enumeration", "extendedEnumeration"}:
                continue
            extended = enumeration_el.tag == "extendedEnumeration"
            entries.append(
                ServiceEnumeration(
                    id=enumeration_el.attrib["id"],
                    comments=self._leading_comments(enumeration_el),
                    enumerators=tuple(
                        self._parse_enumerator(enumerator_el, extended=extended)
                        for enumerator_el in enumeration_el.findall("enumerator")
                    ),
                    extended=extended,
                )
            )
        return tuple(entries)

    def _parse_enumerator(
        self,
        enumerator_el: ET.Element,
        *,
        extended: bool,
    ) -> ServiceEnumerator:
        type_expr = self._parse_inline_expr_from_container(enumerator_el)
        if not extended and type_expr is not None:
            raise ValueError(
                f"Plain enumeration enumerator {enumerator_el.attrib.get('id', '')!r} "
                "must not carry a payload type"
            )
        return ServiceEnumerator(
            id=enumerator_el.attrib["id"],
            comments=self._leading_comments(enumerator_el),
            type_expr=type_expr,
        )

    def _parse_structs(self, structs_el: ET.Element) -> tuple[ServiceStruct, ...]:
        return tuple(
            self._parse_struct(struct_el)
            for struct_el in structs_el.findall("struct")
        )

    def _parse_struct(self, struct_el: ET.Element) -> ServiceStruct:
        object_expr = self._parse_object_body(struct_el)
        return ServiceStruct(
            id=struct_el.attrib["id"],
            comments=self._leading_comments(struct_el),
            members=object_expr.members,
            allows_anything=object_expr.allows_anything,
        )

    def _parse_unions(self, unions_el: ET.Element) -> tuple[ServiceUnion, ...]:
        return tuple(
            ServiceUnion(
                id=union_el.attrib["id"],
                discriminator=union_el.attrib["discriminator"],
                comments=self._leading_comments(union_el),
                alternatives=tuple(
                    self._parse_union_alternative(alternative_el)
                    for alternative_el in union_el.findall("alternative")
                ),
            )
            for union_el in unions_el.findall("union")
        )

    def _parse_union_alternative(self, alternative_el: ET.Element) -> ServiceUnionAlternative:
        type_expr = self._parse_inline_expr_from_container(alternative_el)
        if type_expr is None:
            raise ValueError(
                f"Union alternative {alternative_el.attrib.get('id', '')!r} is missing a type"
            )
        return ServiceUnionAlternative(
            id=alternative_el.attrib["id"],
            comments=self._leading_comments(alternative_el),
            type_expr=type_expr,
        )

    def _parse_messages(self, messages_el: ET.Element) -> tuple[ServiceMessage, ...]:
        messages: list[ServiceMessage] = []
        for message_el in messages_el:
            if message_el.tag not in {"request", "notification"}:
                continue
            params_el = message_el.find("params")
            response_el = message_el.find("response")
            messages.append(
                ServiceMessage(
                    kind=message_el.tag,
                    origin=message_el.attrib["origin"],
                    method=message_el.attrib["method"],
                    title=message_el.attrib["title"],
                    comments=self._leading_comments(message_el),
                    has_params_element=params_el is not None,
                    params_is_null=self._is_true_xml_boolean(
                        params_el.attrib.get("null", "")
                    )
                    if params_el is not None
                    else False,
                    params_comments=self._leading_comments(params_el) if params_el is not None else (),
                    params_fields=self._parse_params_fields(params_el)
                    if params_el is not None
                    else (),
                    params_expr=self._parse_inline_expr_from_container(params_el)
                    if params_el is not None and not params_el.findall("param")
                    else None,
                    response_comments=self._leading_comments(response_el)
                    if response_el is not None
                    else (),
                    response_expr=self._parse_inline_expr_from_container(response_el)
                    if response_el is not None
                    else None,
                )
            )
        return tuple(messages)

    def _parse_params_fields(self, params_el: ET.Element) -> tuple[ServiceMember, ...]:
        param_children = params_el.findall("param")
        if not param_children:
            return ()
        non_param_children = [
            child
            for child in params_el
            if child.tag not in {"comment", "param"}
        ]
        if non_param_children:
            raise ValueError("<params> cannot mix <param> children with other type expressions")
        if self._direct_text_content(params_el):
            raise ValueError("<params> with <param> children must not contain direct text")
        return tuple(self._parse_param_field(param_el) for param_el in param_children)

    def _parse_param_field(self, param_el: ET.Element) -> ServiceMember:
        type_expr = self._parse_inline_expr_from_container(param_el)
        if type_expr is None:
            raise ValueError(f"Param {param_el.attrib.get('id', '')!r} is missing its type")
        return ServiceMember(
            id=param_el.attrib["id"],
            use=param_el.attrib["use"],
            comments=self._leading_comments(param_el),
            type_expr=type_expr,
        )

    def _is_true_xml_boolean(self, value: str) -> bool:
        return value in {"true", "1"}

    def _parse_inline_expr_from_container(self, element: ET.Element | None) -> ServiceTypeExpr | None:
        if element is None:
            return None
        expr_children = [child for child in element if child.tag != "comment"]
        direct_text = self._direct_text_content(element)

        if direct_text and expr_children:
            raise ValueError(
                f"Element <{element.tag}> contains both direct text and nested type expression"
            )
        if len(expr_children) > 1:
            raise ValueError(f"Element <{element.tag}> contains multiple nested type expressions")
        if direct_text:
            return ServiceTextTypeExpr(text=direct_text)
        if not expr_children:
            return None
        return self._parse_type_expr_element(expr_children[0])

    def _parse_type_expr_element(self, element: ET.Element) -> ServiceTypeExpr:
        comments = self._leading_comments(element)
        if element.tag == "array":
            item_type = self._parse_inline_expr_from_container(element)
            if item_type is None:
                raise ValueError("<array> is missing its item type")
            return ServiceArrayTypeExpr(comments=comments, item_type=item_type)

        if element.tag == "map":
            value_type = self._parse_inline_expr_from_container(element)
            if value_type is None:
                raise ValueError("<map> is missing its value type")
            return ServiceMapTypeExpr(comments=comments, value_type=value_type)

        if element.tag == "variant":
            alternatives = tuple(
                self._parse_variant_alternative(alternative_el)
                for alternative_el in element.findall("alternative")
            )
            if not alternatives:
                raise ValueError("<variant> must contain at least one <alternative>")
            return ServiceVariantTypeExpr(comments=comments, alternatives=alternatives)

        if element.tag == "object":
            object_expr = self._parse_object_body(element)
            return ServiceObjectTypeExpr(
                comments=comments,
                members=object_expr.members,
                allows_anything=object_expr.allows_anything,
            )

        raise ValueError(f"Unexpected type expression element <{element.tag}>")

    def _parse_variant_alternative(self, alternative_el: ET.Element) -> ServiceVariantAlternative:
        type_expr = self._parse_inline_expr_from_container(alternative_el)
        if type_expr is None:
            raise ValueError("<variant>/<alternative> is missing its type")
        return ServiceVariantAlternative(
            comments=self._leading_comments(alternative_el),
            type_expr=type_expr,
        )

    def _parse_object_body(self, element: ET.Element) -> ServiceObjectTypeExpr:
        members = tuple(
            self._parse_member(member_el)
            for member_el in element.findall("member")
        )
        allows_anything = element.find("anything") is not None
        return ServiceObjectTypeExpr(members=members, allows_anything=allows_anything)

    def _parse_member(self, member_el: ET.Element) -> ServiceMember:
        type_expr = self._parse_inline_expr_from_container(member_el)
        if type_expr is None:
            raise ValueError(f"Member {member_el.attrib.get('id', '')!r} is missing its type")
        return ServiceMember(
            id=member_el.attrib["id"],
            use=member_el.attrib["use"],
            comments=self._leading_comments(member_el),
            type_expr=type_expr,
        )

    def _leading_comments(self, element: ET.Element | None) -> tuple[str, ...]:
        if element is None:
            return ()
        comments: list[str] = []
        for child in element:
            if child.tag != "comment":
                break
            text = (child.text or "").strip()
            if text:
                comments.append(text)
        return tuple(comments)

    def _direct_text_content(self, element: ET.Element) -> str:
        parts: list[str] = []
        if element.text:
            parts.append(element.text)
        for child in element:
            if child.tag == "comment" and child.tail:
                parts.append(child.tail)
        return "".join(parts).strip()
