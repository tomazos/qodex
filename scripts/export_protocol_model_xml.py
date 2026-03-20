#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

from protocol_schema import ProtocolAnalyzer, SchemaParser
from protocol_schema.service_ir import ServiceIrExporter


def parse_args() -> argparse.Namespace:
    script_root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description=(
            "Export the analyzed Codex protocol service IR as validated XML."
        )
    )
    parser.add_argument(
        "schema_dir",
        type=Path,
        help="Base schema directory, for example protocol/schema/0.115.0/nonexperimental",
    )
    parser.add_argument(
        "--out",
        type=Path,
        help=(
            "Output XML path. Defaults to protocol/model/<version>/<variant>/service.xml "
            "when the schema directory matches protocol/schema/<version>/<variant>."
        ),
    )
    parser.add_argument(
        "--xsd",
        type=Path,
        default=script_root / "protocol_schema" / "service_ir.xsd",
        help="XSD used to validate the emitted XML. Default: %(default)s",
    )
    return parser.parse_args()


def default_output_path(schema_dir: Path) -> Path:
    resolved = schema_dir.resolve()
    parts = list(resolved.parts)
    try:
        schema_index = parts.index("schema")
        version = parts[schema_index + 1]
        variant = parts[schema_index + 2]
    except (ValueError, IndexError):
        return resolved / "service.xml"
    protocol_root = Path(*parts[:schema_index])
    return protocol_root / "model" / version / variant / "service.xml"


def main() -> int:
    args = parse_args()
    parser = SchemaParser()
    bundle = parser.parse_directory(args.schema_dir)
    analysis = ProtocolAnalyzer().analyze_bundle(bundle)

    errors = [*bundle.errors, *analysis.errors]
    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1

    output_path = args.out.resolve() if args.out else default_output_path(args.schema_dir)
    xsd_path = args.xsd.resolve()

    exporter = ServiceIrExporter()
    exporter.write_and_validate(
        analysis=analysis,
        output_path=output_path,
        xsd_path=xsd_path,
    )

    print(f"XML service IR written to: {output_path}")
    print(f"Validated against:    {xsd_path}")
    print(
        f"Entries:              "
        f"{len(analysis.client_requests.variants) + len(analysis.server_requests.variants) + len(analysis.client_notifications.variants) + len(analysis.server_notifications.variants)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
