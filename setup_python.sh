#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="${ROOT_DIR}/.venv"
REQUIREMENTS_FILE="${ROOT_DIR}/requirements.txt"
SYSTEM_PYTHON="${PYTHON:-python3}"

if [[ ! -x "${VENV_DIR}/bin/python" ]]; then
    "${SYSTEM_PYTHON}" -m venv "${VENV_DIR}"
fi

VENV_PYTHON="${VENV_DIR}/bin/python"

if ! "${VENV_PYTHON}" - "${REQUIREMENTS_FILE}" <<'PY'
from __future__ import annotations

import re
import sys
from importlib import metadata
from pathlib import Path

requirements_path = Path(sys.argv[1])
pattern = re.compile(r"^\s*([A-Za-z0-9_.-]+)\s*==\s*([^\s;]+)\s*$")

for raw_line in requirements_path.read_text(encoding="utf-8").splitlines():
    line = raw_line.split("#", 1)[0].strip()
    if not line:
        continue
    match = pattern.match(line)
    if match is None:
        raise SystemExit(1)
    package_name, expected_version = match.groups()
    try:
        installed_version = metadata.version(package_name)
    except metadata.PackageNotFoundError:
        raise SystemExit(1)
    if installed_version != expected_version:
        raise SystemExit(1)
PY
then
    "${VENV_PYTHON}" -m pip install --disable-pip-version-check --requirement "${REQUIREMENTS_FILE}"
fi
