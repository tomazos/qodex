#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

from protocol_schema import ProtocolAnalyzer, SchemaParser


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Parse a generated Codex app-server JSON Schema tree and run basic sanity checks."
        )
    )
    parser.add_argument(
        "schema_dir",
        type=Path,
        help="Base schema directory, for example protocol/schema/0.115.0/experimental",
    )
    parser.add_argument(
        "--show-warnings",
        action="store_true",
        help="Print warnings as well as hard errors.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    parser = SchemaParser()
    bundle = parser.parse_directory(args.schema_dir)
    protocol = ProtocolAnalyzer().analyze_bundle(bundle)

    print(f"Schema root: {bundle.base_dir}")
    print(f"Documents:   {bundle.document_count}")
    print(f"Nodes:       {bundle.node_count}")
    print(f"Namespaces:  {bundle.namespace_count}")
    print(f"References:  {len(bundle.references)}")
    print(f"Client reqs: {len(protocol.client_requests.variants)}")
    print(f"Server reqs: {len(protocol.server_requests.variants)}")
    print(f"Client ntfs: {len(protocol.client_notifications.variants)}")
    print(f"Server ntfs: {len(protocol.server_notifications.variants)}")
    print(f"Req->resp:   {len(protocol.request_response_mappings)}")
    print(f"Warnings:    {len(bundle.warnings)}")
    print(f"Errors:      {len(bundle.errors) + len(protocol.errors)}")

    if args.show_warnings and bundle.warnings:
        print("\nWarnings:")
        for warning in bundle.warnings:
            print(f"  - {warning}")
    if args.show_warnings and protocol.warnings:
        if not bundle.warnings:
            print("\nWarnings:")
        for warning in protocol.warnings:
            print(f"  - {warning}")

    all_errors = [*bundle.errors, *protocol.errors]
    if all_errors:
        print("\nErrors:")
        for error in all_errors:
            print(f"  - {error}")
        return 1

    print("\nSchema sanity check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
