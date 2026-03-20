from __future__ import annotations

import json
import posixpath
from pathlib import Path, PurePosixPath
from typing import Any

from .model import (
    DefinitionEntry,
    SchemaBundle,
    SchemaDocument,
    SchemaNamespace,
    SchemaNode,
    SchemaReference,
    SourceLocation,
)


SCHEMA_KEYWORDS = {
    "$schema",
    "$ref",
    "additionalProperties",
    "allOf",
    "anyOf",
    "const",
    "default",
    "definitions",
    "description",
    "enum",
    "format",
    "items",
    "maxItems",
    "maxLength",
    "maximum",
    "minItems",
    "minLength",
    "minimum",
    "not",
    "oneOf",
    "pattern",
    "properties",
    "required",
    "title",
    "type",
}

NODE_KEYWORDS = {
    "$schema",
    "$ref",
    "type",
    "enum",
    "const",
    "required",
    "properties",
    "definitions",
    "items",
    "additionalProperties",
    "anyOf",
    "oneOf",
    "allOf",
    "not",
    "title",
    "description",
}


class SchemaParser:
    def parse_directory(self, base_dir: str | Path) -> SchemaBundle:
        base_path = Path(base_dir).resolve()
        if not base_path.is_dir():
            raise ValueError(f"Schema directory does not exist: {base_path}")

        documents: dict[str, SchemaDocument] = {}
        errors: list[str] = []
        warnings: list[str] = []

        for json_path in sorted(base_path.rglob("*.json")):
            document = self._parse_document(json_path, base_path, errors, warnings)
            if document is not None:
                documents[document.relative_path] = document

        bundle = SchemaBundle(
            base_dir=base_path,
            documents_by_path=documents,
            references=[],
            errors=errors,
            warnings=warnings,
        )
        self._validate_bundle(bundle)
        return bundle

    def _parse_document(
        self,
        json_path: Path,
        base_path: Path,
        errors: list[str],
        warnings: list[str],
    ) -> SchemaDocument | None:
        relative_path = json_path.relative_to(base_path).as_posix()

        try:
            raw_document = json.loads(json_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            errors.append(f"{relative_path}: invalid JSON at line {exc.lineno}: {exc.msg}")
            return None

        if not isinstance(raw_document, dict):
            errors.append(f"{relative_path}: expected top-level object, got {type(raw_document).__name__}")
            return None

        pointer_index: dict[str, DefinitionEntry] = {}
        references: list[SchemaReference] = []

        root = self._parse_node(
            raw=raw_document,
            document_path=relative_path,
            pointer="",
            pointer_index=pointer_index,
            references=references,
            errors=errors,
        )

        document = SchemaDocument(
            relative_path=relative_path,
            absolute_path=json_path,
            root=root,
            pointer_index=pointer_index,
            references=references,
        )

        if document.title is None:
            warnings.append(f"{relative_path}: top-level schema has no title")

        return document

    def _parse_node(
        self,
        raw: Any,
        document_path: str,
        pointer: str,
        pointer_index: dict[str, DefinitionEntry],
        references: list[SchemaReference],
        errors: list[str],
    ) -> SchemaNode:
        location = SourceLocation(document_path=document_path, pointer=pointer)

        if isinstance(raw, bool):
            node = SchemaNode(location=location, raw=raw)
            pointer_index[pointer] = node
            return node

        if not isinstance(raw, dict):
            raise ValueError(
                f"{document_path}:{pointer or '/'} expected schema object/bool, got {type(raw).__name__}"
            )

        node = SchemaNode(
            location=location,
            raw=raw,
            schema_uri=self._optional_string(raw.get("$schema")),
            title=self._optional_string(raw.get("title")),
            description=self._optional_string(raw.get("description")),
            ref=self._optional_string(raw.get("$ref")),
            types=self._parse_types(raw.get("type")),
            enum_values=tuple(raw.get("enum", ())),
            const_value=raw.get("const"),
            required=tuple(raw.get("required", ())),
        )
        pointer_index[pointer] = node

        if node.ref is not None:
            references.append(SchemaReference(source=location, ref=node.ref))

        properties = raw.get("properties")
        if properties is not None:
            if not isinstance(properties, dict):
                errors.append(f"{document_path}:{pointer or '/'} properties must be an object")
            else:
                for name, child_raw in properties.items():
                    child_pointer = self._join_pointer(pointer, "properties", name)
                    node.properties[name] = self._parse_node(
                        raw=child_raw,
                        document_path=document_path,
                        pointer=child_pointer,
                        pointer_index=pointer_index,
                        references=references,
                        errors=errors,
                    )

        definitions = raw.get("definitions")
        if definitions is not None:
            if not isinstance(definitions, dict):
                errors.append(f"{document_path}:{pointer or '/'} definitions must be an object")
            else:
                node.definitions = self._parse_definition_entries(
                    entries=definitions,
                    document_path=document_path,
                    pointer=self._join_pointer(pointer, "definitions"),
                    pointer_index=pointer_index,
                    references=references,
                    errors=errors,
                )

        items = raw.get("items")
        if items is not None:
            if isinstance(items, list):
                parsed_items: list[SchemaNode] = []
                for index, item_raw in enumerate(items):
                    item_pointer = self._join_pointer(pointer, "items", str(index))
                    parsed_items.append(
                        self._parse_node(
                            raw=item_raw,
                            document_path=document_path,
                            pointer=item_pointer,
                            pointer_index=pointer_index,
                            references=references,
                            errors=errors,
                        )
                    )
                node.items = tuple(parsed_items)
            else:
                node.items = self._parse_node(
                    raw=items,
                    document_path=document_path,
                    pointer=self._join_pointer(pointer, "items"),
                    pointer_index=pointer_index,
                    references=references,
                    errors=errors,
                )

        additional_properties = raw.get("additionalProperties")
        if additional_properties is not None:
            if isinstance(additional_properties, bool):
                node.additional_properties = additional_properties
            else:
                node.additional_properties = self._parse_node(
                    raw=additional_properties,
                    document_path=document_path,
                    pointer=self._join_pointer(pointer, "additionalProperties"),
                    pointer_index=pointer_index,
                    references=references,
                    errors=errors,
                )

        node.any_of = self._parse_node_list(
            raw.get("anyOf"),
            document_path,
            self._join_pointer(pointer, "anyOf"),
            pointer_index,
            references,
            errors,
        )
        node.one_of = self._parse_node_list(
            raw.get("oneOf"),
            document_path,
            self._join_pointer(pointer, "oneOf"),
            pointer_index,
            references,
            errors,
        )
        node.all_of = self._parse_node_list(
            raw.get("allOf"),
            document_path,
            self._join_pointer(pointer, "allOf"),
            pointer_index,
            references,
            errors,
        )

        not_schema = raw.get("not")
        if not_schema is not None:
            node.not_schema = self._parse_node(
                raw=not_schema,
                document_path=document_path,
                pointer=self._join_pointer(pointer, "not"),
                pointer_index=pointer_index,
                references=references,
                errors=errors,
            )

        node.passthrough_keywords = {
            key: value for key, value in raw.items() if key not in NODE_KEYWORDS
        }
        return node

    def _parse_definition_entries(
        self,
        entries: dict[str, Any],
        document_path: str,
        pointer: str,
        pointer_index: dict[str, DefinitionEntry],
        references: list[SchemaReference],
        errors: list[str],
    ) -> dict[str, DefinitionEntry]:
        parsed_entries: dict[str, DefinitionEntry] = {}
        for name, entry_raw in entries.items():
            entry_pointer = self._join_pointer(pointer, name)
            if self._looks_like_schema(entry_raw):
                parsed_entries[name] = self._parse_node(
                    raw=entry_raw,
                    document_path=document_path,
                    pointer=entry_pointer,
                    pointer_index=pointer_index,
                    references=references,
                    errors=errors,
                )
                continue

            if isinstance(entry_raw, dict):
                namespace = SchemaNamespace(
                    name=name,
                    location=SourceLocation(document_path=document_path, pointer=entry_pointer),
                )
                pointer_index[entry_pointer] = namespace
                namespace.entries = self._parse_definition_entries(
                    entries=entry_raw,
                    document_path=document_path,
                    pointer=entry_pointer,
                    pointer_index=pointer_index,
                    references=references,
                    errors=errors,
                )
                parsed_entries[name] = namespace
                continue

            errors.append(
                f"{document_path}:{entry_pointer} definition entry must be a schema or namespace object"
            )
        return parsed_entries

    def _parse_node_list(
        self,
        raw: Any,
        document_path: str,
        pointer: str,
        pointer_index: dict[str, DefinitionEntry],
        references: list[SchemaReference],
        errors: list[str],
    ) -> tuple[SchemaNode, ...]:
        if raw is None:
            return ()
        if not isinstance(raw, list):
            errors.append(f"{document_path}:{pointer} must be an array")
            return ()
        parsed_nodes: list[SchemaNode] = []
        for index, child_raw in enumerate(raw):
            parsed_nodes.append(
                self._parse_node(
                    raw=child_raw,
                    document_path=document_path,
                    pointer=self._join_pointer(pointer, str(index)),
                    pointer_index=pointer_index,
                    references=references,
                    errors=errors,
                )
            )
        return tuple(parsed_nodes)

    def _validate_bundle(self, bundle: SchemaBundle) -> None:
        bundle.references.extend(
            ref for document in bundle.documents_by_path.values() for ref in document.references
        )

        titles: dict[str, str] = {}
        for document in bundle.iter_documents():
            if document.title is None:
                continue
            previous = titles.get(document.title)
            if previous is not None and previous != document.relative_path:
                bundle.warnings.append(
                    f"duplicate top-level title {document.title!r} in {previous} and {document.relative_path}"
                )
            else:
                titles[document.title] = document.relative_path

        for reference in bundle.references:
            self._resolve_reference(reference, bundle)
            if not reference.is_resolved:
                bundle.errors.append(
                    f"{reference.source.document_path}:{reference.source.pointer or '/'} "
                    f"unresolved ref {reference.ref!r}"
                )

    def _resolve_reference(self, reference: SchemaReference, bundle: SchemaBundle) -> None:
        source_path = reference.source.document_path
        raw_ref = reference.ref
        target_document_path: str
        fragment: str

        if raw_ref.startswith("#"):
            target_document_path = source_path
            fragment = raw_ref[1:]
        else:
            ref_document, separator, tail = raw_ref.partition("#")
            if "://" in ref_document:
                return
            target_document_path = self._resolve_relative_document_path(source_path, ref_document)
            fragment = tail if separator else ""

        target_document = bundle.documents_by_path.get(target_document_path)
        if target_document is None:
            return

        target_pointer = self._normalize_pointer_fragment(fragment)
        resolved_target = target_document.pointer_index.get(target_pointer)
        if resolved_target is None:
            return

        reference.target_document_path = target_document_path
        reference.target_pointer = target_pointer
        reference.resolved_target = resolved_target

    def _resolve_relative_document_path(self, source_path: str, target_path: str) -> str:
        base = PurePosixPath(source_path).parent.as_posix()
        return posixpath.normpath(posixpath.join(base, target_path))

    def _normalize_pointer_fragment(self, fragment: str) -> str:
        if not fragment:
            return ""
        if not fragment.startswith("/"):
            return ""
        return fragment

    def _looks_like_schema(self, value: Any) -> bool:
        if isinstance(value, bool):
            return True
        if not isinstance(value, dict):
            return False
        return any(keyword in value for keyword in SCHEMA_KEYWORDS)

    def _parse_types(self, raw_type: Any) -> tuple[str, ...]:
        if raw_type is None:
            return ()
        if isinstance(raw_type, str):
            return (raw_type,)
        if isinstance(raw_type, list):
            return tuple(value for value in raw_type if isinstance(value, str))
        return ()

    def _optional_string(self, value: Any) -> str | None:
        return value if isinstance(value, str) else None

    def _join_pointer(self, base_pointer: str, *segments: str) -> str:
        existing_parts = [] if not base_pointer else base_pointer.lstrip("/").split("/")
        existing_parts.extend(
            self._escape_pointer_part(segment) for segment in segments if segment != ""
        )
        if not existing_parts:
            return ""
        return "/" + "/".join(existing_parts)

    def _escape_pointer_part(self, part: str) -> str:
        return part.replace("~", "~0").replace("/", "~1")
