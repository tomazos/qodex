from __future__ import annotations

from pathlib import Path

from ..model import ServiceDescription


class NullGenerator:
    name = "null"

    def generate(self, service: ServiceDescription, output_path: Path) -> None:
        _ = service
        _ = output_path
