from __future__ import annotations

from dataclasses import dataclass
from functools import cached_property
from html import escape
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


@dataclass(frozen=True)
class HtmlEntityRef:
    kind: str
    id: str
    anchor: str


class HtmlGenerator:
    name = "html"

    def generate(self, service: ServiceDescription, output_path: Path) -> None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(self._render_document(service), encoding="utf-8")

    @cached_property
    def _template_environment(self) -> Environment:
        return Environment(
            loader=FileSystemLoader(Path(__file__).with_name("templates").joinpath("html")),
            autoescape=False,
            keep_trailing_newline=True,
        )

    def _render_template(self, template_name: str, **context: object) -> str:
        return self._template_environment.get_template(template_name).render(**context)

    def _render_document(self, service: ServiceDescription) -> str:
        entity_refs = self._build_entity_refs(service)
        struct_lookup = {entry.id: entry for entry in service.structs}
        sections = [
            self._render_header(service),
            self._render_catalog(service, entity_refs),
            self._render_messages(service, entity_refs, struct_lookup),
            self._render_enumerations(service, entity_refs),
            self._render_structs(service, entity_refs),
            self._render_unions(service, entity_refs),
            self._render_types(service, entity_refs),
        ]
        body = "\n".join(section for section in sections if section)
        return self._render_template("document.j2", body=body)

    def _build_entity_refs(self, service: ServiceDescription) -> dict[str, HtmlEntityRef]:
        refs: dict[str, HtmlEntityRef] = {}
        for type_def in service.types:
            refs[type_def.id] = HtmlEntityRef(
                kind="type",
                id=type_def.id,
                anchor=self._anchor("type", type_def.id),
            )
        for enumeration in service.enumerations:
            kind = "extendedEnumeration" if enumeration.extended else "enumeration"
            refs[enumeration.id] = HtmlEntityRef(
                kind=kind,
                id=enumeration.id,
                anchor=self._anchor(kind, enumeration.id),
            )
        for struct in service.structs:
            refs[struct.id] = HtmlEntityRef(
                kind="struct",
                id=struct.id,
                anchor=self._anchor("struct", struct.id),
            )
        for union in service.unions:
            refs[union.id] = HtmlEntityRef(
                kind="union",
                id=union.id,
                anchor=self._anchor("union", union.id),
            )
        return refs

    def _render_header(self, service: ServiceDescription) -> str:
        comments = "".join(
            f'<div class="summary comment">// {escape(comment)}</div>'
            for comment in service.comments
        )
        return self._render_template("header.j2", comments=comments).strip()

    def _render_catalog(
        self,
        service: ServiceDescription,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        lines: list[str] = []
        if service.messages:
            lines.extend(self._render_message_catalog_lines(service.messages))
        plain_enums = sorted(
            (entry for entry in service.enumerations if not entry.extended),
            key=lambda entry: entry.id.casefold(),
        )
        if plain_enums:
            lines.append(self._render_catalog_line("Enumerations", plain_enums, entity_refs))
        extended_enums = sorted(
            (entry for entry in service.enumerations if entry.extended),
            key=lambda entry: entry.id.casefold(),
        )
        if extended_enums:
            lines.append(
                self._render_catalog_line("Extended Enumerations", extended_enums, entity_refs)
            )
        if service.structs:
            lines.append(self._render_catalog_line("Structs", service.structs, entity_refs))
        if service.unions:
            lines.append(self._render_catalog_line("Unions", service.unions, entity_refs))
        if service.types:
            lines.append(self._render_catalog_line("Types", service.types, entity_refs))
        if not lines:
            return ""
        return self._render_template("catalog.j2", lines_html="".join(lines)).strip()

    def _render_catalog_line(
        self,
        title: str,
        entries,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        ordered = sorted(entries, key=lambda entry: entry.id.casefold())
        links = " ".join(
            self._render_entity_link(entry.id, entity_refs)
            for entry in ordered
        )
        return self._render_template(
            "catalog_line.j2",
            title=escape(title),
            links_html=links,
        ).strip()

    def _render_message_catalog_lines(self, messages: tuple[ServiceMessage, ...]) -> list[str]:
        lines: list[str] = []
        for group_name, group_messages in self._group_messages(messages):
            links = " ".join(
                f'<a href="#{escape(self._message_anchor(message))}">{escape(self._message_catalog_label(message))}</a>'
                for message in group_messages
            )
            lines.append(
                '<div class="catalog-line">'
                f'<span class="catalog-label">Messages {escape(group_name)}:</span> '
                f"{links}"
                "</div>"
            )
        return lines

    def _render_types(
        self,
        service: ServiceDescription,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        if not service.types:
            return ""
        cards = "\n".join(
            self._render_card(
                anchor=self._anchor("type", entry.id),
                header=f"type {entry.id}",
                rows=self._comment_rows(entry.comments),
            )
            for entry in sorted(service.types, key=lambda entry: entry.id.casefold())
        )
        return self._wrap_section("Types", cards)

    def _render_enumerations(
        self,
        service: ServiceDescription,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        sections: list[str] = []
        plain = sorted(
            (entry for entry in service.enumerations if not entry.extended),
            key=lambda entry: entry.id.casefold(),
        )
        if plain:
            cards = "\n".join(
                self._render_enumeration_card(entry, entity_refs)
                for entry in plain
            )
            sections.append(self._wrap_section("Enumerations", cards))
        extended = sorted(
            (entry for entry in service.enumerations if entry.extended),
            key=lambda entry: entry.id.casefold(),
        )
        if extended:
            cards = "\n".join(
                self._render_enumeration_card(entry, entity_refs)
                for entry in extended
            )
            sections.append(self._wrap_section("Extended Enumerations", cards))
        return "\n".join(sections)

    def _render_enumeration_card(
        self,
        enumeration: ServiceEnumeration,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        kind = "extendedEnumeration" if enumeration.extended else "enumeration"
        rows = self._comment_rows(enumeration.comments)
        for enumerator in enumeration.enumerators:
            if enumeration.extended and enumerator.type_expr is not None:
                line = (
                    f"{escape(enumerator.id)} "
                    f"{self._render_type_expr(enumerator.type_expr, entity_refs)}"
                )
                comments = self._merge_comment_tuples(
                    enumerator.comments,
                    enumerator.type_expr.comments,
                )
            else:
                line = escape(enumerator.id)
                comments = enumerator.comments
            rows.append(self._row(line, comments))
        if not rows:
            rows.append(self._row("&nbsp;"))
        return self._render_card(
            anchor=self._anchor(kind, enumeration.id),
            header=f"{kind} {enumeration.id}",
            rows=rows,
        )

    def _render_structs(
        self,
        service: ServiceDescription,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        if not service.structs:
            return ""
        cards = "\n".join(
            self._render_struct_card(entry, entity_refs)
            for entry in sorted(service.structs, key=lambda entry: entry.id.casefold())
        )
        return self._wrap_section("Structs", cards)

    def _render_struct_card(
        self,
        struct: ServiceStruct,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        rows = self._comment_rows(struct.comments)
        for member in struct.members:
            rows.append(self._render_struct_member_row(member, entity_refs))
        if struct.allows_anything:
            rows.append(self._row("anything"))
        if not rows:
            rows.append(self._row("&nbsp;"))
        return self._render_card(
            anchor=self._anchor("struct", struct.id),
            header=f"struct {struct.id}",
            rows=rows,
        )

    def _render_struct_member_row(
        self,
        member: ServiceMember,
        entity_refs: dict[str, HtmlEntityRef],
        *,
        nested: bool = False,
    ) -> str:
        line = (
            f"{escape(member.use)} "
            f"{self._render_type_expr(member.type_expr, entity_refs)} "
            f"{escape(member.id)}"
        )
        comments = self._merge_comment_tuples(member.comments, member.type_expr.comments)
        return self._row(line, comments, nested=nested)

    def _render_unions(
        self,
        service: ServiceDescription,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        if not service.unions:
            return ""
        cards = "\n".join(
            self._render_union_card(entry, entity_refs)
            for entry in sorted(service.unions, key=lambda entry: entry.id.casefold())
        )
        return self._wrap_section("Unions", cards)

    def _render_union_card(
        self,
        union: ServiceUnion,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        rows = self._comment_rows(union.comments)
        rows.append(self._row(f"discriminator {escape(union.discriminator)}"))
        for alternative in union.alternatives:
            type_html = (
                self._render_type_expr(alternative.type_expr, entity_refs)
                if alternative.type_expr is not None
                else "&nbsp;"
            )
            comments = alternative.comments
            if alternative.type_expr is not None:
                comments = self._merge_comment_tuples(
                    comments,
                    alternative.type_expr.comments,
                )
            rows.append(
                self._row(
                    f"{type_html} {escape(alternative.id)}",
                    comments,
                )
            )
        return self._render_card(
            anchor=self._anchor("union", union.id),
            header=f"union {union.id}",
            rows=rows,
        )

    def _render_messages(
        self,
        service: ServiceDescription,
        entity_refs: dict[str, HtmlEntityRef],
        struct_lookup: dict[str, ServiceStruct],
    ) -> str:
        if not service.messages:
            return ""
        groups: list[str] = []
        for group_name, group_messages in self._group_messages(service.messages):
            cards = "\n".join(
                self._render_message_card(message, entity_refs, struct_lookup)
                for message in group_messages
            )
            groups.append(
                self._render_template(
                    "message_group.j2",
                    group_name=escape(group_name),
                    cards_html=cards,
                ).strip()
            )
        return self._wrap_section("Messages", "\n".join(groups), wrap_cards=False)

    def _render_message_card(
        self,
        message: ServiceMessage,
        entity_refs: dict[str, HtmlEntityRef],
        struct_lookup: dict[str, ServiceStruct],
    ) -> str:
        rows = self._comment_rows(message.comments)
        if message.has_params_element:
            if message.params_fields:
                if message.params_comments:
                    rows.append(self._row("params", message.params_comments))
                rows.extend(
                    self._render_inline_params_fields(message.params_fields, entity_refs)
                )
            else:
                params_line = (
                    f"{self._render_message_payload_expr(message.params_expr, entity_refs, is_null=message.params_is_null)} "
                    "params"
                )
                params_comments = self._merge_comment_tuples(
                    message.params_comments,
                    message.params_expr.comments if message.params_expr is not None else (),
                )
                rows.append(self._row(params_line, params_comments))
                rows.extend(
                    self._render_expanded_message_params(
                        message,
                        entity_refs,
                        struct_lookup,
                    )
                )
        if message.kind == "request":
            response_line = (
                f"{self._render_message_payload_expr(message.response_expr, entity_refs, is_null=False)} "
                "response"
            )
            response_comments = self._merge_comment_tuples(
                message.response_comments,
                message.response_expr.comments if message.response_expr is not None else (),
            )
            rows.append(self._row(response_line, response_comments))
        if not rows:
            rows.append(self._row("&nbsp;"))
        return self._render_card(
            anchor=self._message_anchor(message),
            header=self._message_label(message),
            rows=rows,
        )

    def _render_expanded_message_params(
        self,
        message: ServiceMessage,
        entity_refs: dict[str, HtmlEntityRef],
        struct_lookup: dict[str, ServiceStruct],
    ) -> list[str]:
        if message.params_is_null or message.params_expr is None:
            return []
        if not isinstance(message.params_expr, ServiceTextTypeExpr):
            return []
        params_struct = struct_lookup.get(message.params_expr.text)
        if params_struct is None:
            return []
        return [
            self._render_struct_member_row(member, entity_refs, nested=True)
            for member in params_struct.members
        ]

    def _render_inline_params_fields(
        self,
        params_fields: tuple[ServiceMember, ...],
        entity_refs: dict[str, HtmlEntityRef],
    ) -> list[str]:
        return [
            self._render_struct_member_row(member, entity_refs, nested=True)
            for member in params_fields
        ]

    def _render_message_payload_expr(
        self,
        expr: ServiceTypeExpr | None,
        entity_refs: dict[str, HtmlEntityRef],
        *,
        is_null: bool,
    ) -> str:
        if expr is None:
            return "null" if is_null else "{}"
        return self._render_type_expr(expr, entity_refs)

    def _render_type_expr(
        self,
        expr: ServiceTypeExpr,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        if isinstance(expr, ServiceTextTypeExpr):
            return self._render_named_text(expr.text, entity_refs)
        if isinstance(expr, ServiceArrayTypeExpr):
            return f"{self._render_type_expr(expr.item_type, entity_refs)}[]"
        if isinstance(expr, ServiceMapTypeExpr):
            return f"map&lt;string, {self._render_type_expr(expr.value_type, entity_refs)}&gt;"
        if isinstance(expr, ServiceVariantTypeExpr):
            inner = ", ".join(
                self._render_type_expr(alternative.type_expr, entity_refs)
                for alternative in expr.alternatives
                if alternative.type_expr is not None
            )
            return f"variant&lt;{inner}&gt;"
        if isinstance(expr, ServiceObjectTypeExpr):
            pieces = [
                f"{escape(member.use)} {self._render_type_expr(member.type_expr, entity_refs)} {escape(member.id)}"
                for member in expr.members
            ]
            if expr.allows_anything:
                pieces.append("anything")
            return "object{" + ", ".join(pieces) + "}"
        return escape(str(expr))

    def _render_named_text(
        self,
        text: str,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        ref = entity_refs.get(text)
        escaped = escape(text)
        if ref is None:
            return escaped
        return f'<a href="#{escape(ref.anchor)}">{escaped}</a>'

    def _render_entity_link(
        self,
        entity_id: str,
        entity_refs: dict[str, HtmlEntityRef],
    ) -> str:
        ref = entity_refs[entity_id]
        return f'<a href="#{escape(ref.anchor)}">{escape(entity_id)}</a>'

    def _row(
        self,
        line_html: str,
        comments: tuple[str, ...] = (),
        *,
        nested: bool = False,
    ) -> str:
        classes = " nested" if nested else ""
        return f'<tr><td class="{classes.strip()}">{self._with_inline_comments(line_html, comments)}</td></tr>'

    def _with_inline_comments(self, line_html: str, comments: tuple[str, ...]) -> str:
        if not comments:
            return line_html
        suffix = "".join(
            f' <span class="comment">// {escape(comment)}</span>'
            for comment in comments
        )
        return line_html + suffix

    def _comment_rows(self, comments: tuple[str, ...]) -> list[str]:
        return [self._row("", (comment,)) for comment in comments]

    def _merge_comment_tuples(
        self,
        *tuples_to_merge: tuple[str, ...],
    ) -> tuple[str, ...]:
        merged: list[str] = []
        seen: set[str] = set()
        for comments in tuples_to_merge:
            for comment in comments:
                if comment in seen:
                    continue
                seen.add(comment)
                merged.append(comment)
        return tuple(merged)

    def _render_card(
        self,
        *,
        anchor: str,
        header: str,
        rows: list[str],
    ) -> str:
        body_rows = "".join(rows) if rows else self._row("&nbsp;")
        return self._render_template(
            "card.j2",
            anchor=escape(anchor),
            header=escape(header),
            body_rows=body_rows,
        ).strip()

    def _wrap_section(self, title: str, cards: str, *, wrap_cards: bool = True) -> str:
        body = f'<div class="section-grid">{cards}</div>' if wrap_cards else cards
        return self._render_template(
            "section.j2",
            anchor=escape(self._section_anchor(title)),
            title=escape(title),
            body=body,
        ).strip()

    def _message_anchor(self, message: ServiceMessage) -> str:
        return self._anchor("message", f"{message.origin}-{message.kind}-{message.method}")

    def _message_label(self, message: ServiceMessage) -> str:
        return f"{message.origin} {message.kind} {message.method}"

    def _message_catalog_label(self, message: ServiceMessage) -> str:
        return message.method

    def _group_messages(
        self,
        messages: tuple[ServiceMessage, ...],
    ) -> list[tuple[str, list[ServiceMessage]]]:
        groups: dict[str, list[ServiceMessage]] = {}
        for message in messages:
            groups.setdefault(self._message_group_name(message), []).append(message)
        misc_messages: list[ServiceMessage] = []
        grouped: list[tuple[str, list[ServiceMessage]]] = []
        for group_name, group_messages in groups.items():
            if group_name != "misc" and len(group_messages) == 1:
                misc_messages.extend(group_messages)
                continue
            grouped.append((group_name, group_messages))
        if misc_messages:
            misc_group = next(
                (group_messages for group_name, group_messages in grouped if group_name == "misc"),
                None,
            )
            if misc_group is None:
                grouped.append(("misc", misc_messages))
            else:
                misc_group.extend(misc_messages)
        return [
            (
                group_name,
                sorted(
                    group_messages,
                    key=lambda message: self._message_catalog_label(message).casefold(),
                ),
            )
            for group_name, group_messages in sorted(
                grouped,
                key=lambda item: item[0].casefold(),
            )
        ]

    def _message_group_name(self, message: ServiceMessage) -> str:
        return message.method.split("/", 1)[0]

    def _anchor(self, kind: str, value: str) -> str:
        return f"{kind}-{self._slug(value)}"

    def _section_anchor(self, title: str) -> str:
        return f"section-{self._slug(title)}"

    def _slug(self, value: str) -> str:
        chars: list[str] = []
        for char in value:
            chars.append(char.lower() if char.isalnum() else "-")
        slug = "".join(chars).strip("-")
        while "--" in slug:
            slug = slug.replace("--", "-")
        return slug or "item"
