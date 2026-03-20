#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


VERSION_RE = re.compile(r"(\d+\.\d+\.\d+(?:[-+._A-Za-z0-9]+)?)")


def parse_args() -> argparse.Namespace:
    script_root = Path(__file__).resolve().parents[1]
    default_schema_root = script_root / "protocol" / "schema"

    parser = argparse.ArgumentParser(
        description=(
            "Generate versioned Codex app-server JSON Schema output under "
            "protocol/schema/<version>/{nonexperimental,experimental}."
        )
    )
    parser.add_argument(
        "--codex",
        default="codex",
        help="Codex executable to invoke. Default: %(default)s",
    )
    parser.add_argument(
        "--schema-root",
        type=Path,
        default=default_schema_root,
        help="Destination schema root. Default: %(default)s",
    )
    parser.add_argument(
        "--version",
        help=(
            "Override the detected Codex version used in the output path. "
            "By default, the script parses `codex --version`."
        ),
    )
    return parser.parse_args()


def detect_version(codex_bin: str) -> tuple[str, str]:
    completed = subprocess.run(
        [codex_bin, "--version"],
        check=True,
        capture_output=True,
        text=True,
    )
    version_output = completed.stdout.strip() or completed.stderr.strip()
    match = VERSION_RE.search(version_output)
    if not match:
        raise RuntimeError(
            f"Could not parse Codex version from `{codex_bin} --version`: {version_output!r}"
        )
    return match.group(1), version_output


def generate_schema(codex_bin: str, output_dir: Path, experimental: bool) -> None:
    command = [codex_bin, "app-server", "generate-json-schema"]
    if experimental:
        command.append("--experimental")
    command.extend(["--out", str(output_dir)])
    subprocess.run(command, check=True)


def replace_tree(source_dir: Path, dest_dir: Path) -> None:
    if dest_dir.exists():
        shutil.rmtree(dest_dir)
    dest_dir.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source_dir, dest_dir)


def main() -> int:
    args = parse_args()
    version, version_output = detect_version(args.codex)
    if args.version:
        version = args.version

    schema_root = args.schema_root.resolve()
    version_root = schema_root / version
    nonexperimental_dir = version_root / "nonexperimental"
    experimental_dir = version_root / "experimental"

    with tempfile.TemporaryDirectory(prefix="qodex-schema-") as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        nonexperimental_temp = temp_dir / "nonexperimental"
        experimental_temp = temp_dir / "experimental"

        generate_schema(args.codex, nonexperimental_temp, experimental=False)
        generate_schema(args.codex, experimental_temp, experimental=True)

        replace_tree(nonexperimental_temp, nonexperimental_dir)
        replace_tree(experimental_temp, experimental_dir)

    print(f"Codex version: {version_output}")
    print(f"Wrote nonexperimental schema to: {nonexperimental_dir}")
    print(f"Wrote experimental schema to: {experimental_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
