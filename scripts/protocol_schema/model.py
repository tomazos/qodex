from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterator, TypeAlias


SchemaScalar: TypeAlias = str | int | float | bool | None
SchemaMapping: TypeAlias = dict[str, Any]


@dataclass(frozen=True)
class SourceLocation:
    document_path: str
    pointer: str


@dataclass
class SchemaNamespace:
    name: str
    location: SourceLocation
    entries: dict[str, "DefinitionEntry"] = field(default_factory=dict)

    def iter_nodes(self) -> Iterator["SchemaNode"]:
        for entry in self.entries.values():
            if isinstance(entry, SchemaNode):
                yield from entry.iter_nodes()
            else:
                yield from entry.iter_nodes()

    def iter_namespaces(self) -> Iterator["SchemaNamespace"]:
        yield self
        for entry in self.entries.values():
            if isinstance(entry, SchemaNamespace):
                yield from entry.iter_namespaces()


@dataclass
class SchemaNode:
    location: SourceLocation
    raw: bool | SchemaMapping
    schema_uri: str | None = None
    title: str | None = None
    description: str | None = None
    ref: str | None = None
    types: tuple[str, ...] = ()
    enum_values: tuple[Any, ...] = ()
    const_value: Any | None = None
    required: tuple[str, ...] = ()
    properties: dict[str, "SchemaNode"] = field(default_factory=dict)
    definitions: dict[str, "DefinitionEntry"] = field(default_factory=dict)
    items: "SchemaNode | tuple[SchemaNode, ...] | None" = None
    additional_properties: bool | "SchemaNode" | None = None
    any_of: tuple["SchemaNode", ...] = ()
    one_of: tuple["SchemaNode", ...] = ()
    all_of: tuple["SchemaNode", ...] = ()
    not_schema: "SchemaNode | None" = None
    passthrough_keywords: dict[str, Any] = field(default_factory=dict)

    @property
    def is_boolean_schema(self) -> bool:
        return isinstance(self.raw, bool)

    def iter_nodes(self) -> Iterator["SchemaNode"]:
        yield self
        for child in self.properties.values():
            yield from child.iter_nodes()
        for entry in self.definitions.values():
            if isinstance(entry, SchemaNode):
                yield from entry.iter_nodes()
            else:
                yield from entry.iter_nodes()
        if isinstance(self.items, SchemaNode):
            yield from self.items.iter_nodes()
        elif isinstance(self.items, tuple):
            for item in self.items:
                yield from item.iter_nodes()
        if isinstance(self.additional_properties, SchemaNode):
            yield from self.additional_properties.iter_nodes()
        for branch in self.any_of:
            yield from branch.iter_nodes()
        for branch in self.one_of:
            yield from branch.iter_nodes()
        for branch in self.all_of:
            yield from branch.iter_nodes()
        if self.not_schema is not None:
            yield from self.not_schema.iter_nodes()


DefinitionEntry: TypeAlias = SchemaNode | SchemaNamespace


@dataclass
class SchemaReference:
    source: SourceLocation
    ref: str
    target_document_path: str | None = None
    target_pointer: str | None = None
    resolved_target: DefinitionEntry | None = None

    @property
    def is_resolved(self) -> bool:
        return self.resolved_target is not None


@dataclass
class SchemaDocument:
    relative_path: str
    absolute_path: Path
    root: SchemaNode
    pointer_index: dict[str, DefinitionEntry] = field(default_factory=dict)
    references: list[SchemaReference] = field(default_factory=list)

    @property
    def title(self) -> str | None:
        return self.root.title

    def iter_nodes(self) -> Iterator[SchemaNode]:
        yield from self.root.iter_nodes()

    def iter_namespaces(self) -> Iterator[SchemaNamespace]:
        for entry in self.pointer_index.values():
            if isinstance(entry, SchemaNamespace):
                yield entry


@dataclass
class SchemaBundle:
    base_dir: Path
    documents_by_path: dict[str, SchemaDocument]
    references: list[SchemaReference]
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    def iter_documents(self) -> Iterator[SchemaDocument]:
        for relative_path in sorted(self.documents_by_path):
            yield self.documents_by_path[relative_path]

    def iter_nodes(self) -> Iterator[SchemaNode]:
        for document in self.iter_documents():
            yield from document.iter_nodes()

    def iter_namespaces(self) -> Iterator[SchemaNamespace]:
        seen: set[tuple[str, str]] = set()
        for document in self.iter_documents():
            for namespace in document.iter_namespaces():
                key = (namespace.location.document_path, namespace.location.pointer)
                if key in seen:
                    continue
                seen.add(key)
                yield namespace

    @property
    def document_count(self) -> int:
        return len(self.documents_by_path)

    @property
    def node_count(self) -> int:
        return sum(1 for _ in self.iter_nodes())

    @property
    def namespace_count(self) -> int:
        return sum(1 for _ in self.iter_namespaces())
