#!/usr/bin/env bash
set -euo pipefail

IMAGE="${QODEX_DOCKER_IMAGE:-ubuntu:26.04}"
CONTAINER_NAME="${QODEX_DOCKER_CONTAINER_NAME:-qodex-build-test-$$}"
GIT_URL="${QODEX_GIT_URL:-https://github.com/tomazos/qodex.git}"
GIT_REF="${QODEX_GIT_REF:-main}"
CONTAINER_WORKDIR="/work/qodex"

cleanup() {
  "${DOCKER_CMD[@]}" rm -f "${CONTAINER_NAME}" >/dev/null 2>&1 || true
}

trap cleanup EXIT INT TERM

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required but was not found on PATH" >&2
  exit 1
fi

if docker info >/dev/null 2>&1; then
  DOCKER_CMD=(docker)
elif sudo -n docker info >/dev/null 2>&1; then
  DOCKER_CMD=(sudo docker)
else
  echo "docker is installed but is not accessible; try running with sudo privileges" >&2
  exit 1
fi

echo "Starting fresh Docker build test in ${IMAGE}"

"${DOCKER_CMD[@]}" run \
  --name "${CONTAINER_NAME}" \
  --rm \
  "${IMAGE}" \
  bash -lc "
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y ca-certificates git

git clone '${GIT_URL}' '${CONTAINER_WORKDIR}'
cd '${CONTAINER_WORKDIR}'
git checkout '${GIT_REF}'

xargs -a dependencies.txt apt-get install -y
npm i -g @openai/codex
./setup_python.sh
cmake -S . -B build -G Ninja
cmake --build build

if ctest --test-dir build -N 2>/dev/null | grep -q 'Total Tests: 0'; then
  echo 'No CTest tests registered; skipping test execution.'
else
  ctest --test-dir build --output-on-failure
fi
"

echo "Docker build test completed successfully."
