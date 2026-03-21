from __future__ import annotations

from enum import Enum

from .cpp import CppGenerator
from .html import HtmlGenerator
from .null import NullGenerator
from .qt import QtGenerator


class GeneratorName(str, Enum):
    CPP = "cpp"
    HTML = "html"
    NULL = "null"
    QT = "qt"


GENERATOR_REGISTRY = {
    GeneratorName.CPP.value: CppGenerator(),
    GeneratorName.HTML.value: HtmlGenerator(),
    GeneratorName.NULL.value: NullGenerator(),
    GeneratorName.QT.value: QtGenerator(),
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
