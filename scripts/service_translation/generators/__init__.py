from __future__ import annotations

from enum import Enum

from .html import HtmlGenerator
from .null import NullGenerator


class GeneratorName(str, Enum):
    HTML = "html"
    NULL = "null"


GENERATOR_REGISTRY = {
    GeneratorName.HTML.value: HtmlGenerator(),
    GeneratorName.NULL.value: NullGenerator(),
}


def get_generator(name: str):
    try:
        return GENERATOR_REGISTRY[name]
    except KeyError as exc:
        raise ValueError(f"Unknown generator: {name}") from exc


__all__ = [
    "GENERATOR_REGISTRY",
    "GeneratorName",
    "get_generator",
]
