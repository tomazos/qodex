#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

from service_translation import ServiceDescriptionParser
from service_translation.generators import GENERATOR_REGISTRY, GeneratorName, get_generator


def parse_args() -> argparse.Namespace:
    script_root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description=(
            "Validate and parse a service.xml IR file, then pass it to a named generator."
        )
    )
    parser.add_argument(
        "service_xml",
        type=Path,
        help="Path to the service.xml file to validate and translate.",
    )
    parser.add_argument(
        "generator",
        choices=sorted(name for name in GENERATOR_REGISTRY),
        help="Generator plugin to run.",
    )
    parser.add_argument(
        "output_path",
        type=Path,
        help="Generator output path. May be a file or directory depending on the generator.",
    )
    parser.add_argument(
        "--xsd",
        type=Path,
        default=script_root / "protocol_schema" / "service_ir.xsd",
        help="XSD used to validate the input XML. Default: %(default)s",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    parser = ServiceDescriptionParser()
    service = parser.parse_file(
        xml_path=args.service_xml.resolve(),
        xsd_path=args.xsd.resolve(),
    )
    generator = get_generator(args.generator)
    generator.generate(service, args.output_path.resolve())

    print(f"Validated service XML: {args.service_xml.resolve()}")
    print(f"Generator:             {args.generator}")
    print(f"Output path:           {args.output_path.resolve()}")
    print(
        "Parsed entries:        "
        f"{len(service.enumerations)} enumerations, "
        f"{len(service.structs)} structs, "
        f"{len(service.unions)} unions, "
        f"{len(service.messages)} messages"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
