#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
build_script="${repo_root}/scripts/build-deb.sh"
dist_dir="${DIST_DIR:-${repo_root}/dist}"
version="$(tr -d '[:space:]' < "${repo_root}/VERSION")"
arch="${DEB_ARCH:-$(dpkg --print-architecture)}"
package_path="${dist_dir}/qodex_${version}_${arch}.deb"
image="${QODEX_DOCKER_IMAGE:-ubuntu:26.04}"
container_name="${QODEX_DOCKER_CONTAINER_NAME:-qodex-deb-test-$$}"

cleanup() {
  "${DOCKER_CMD[@]}" rm -f "${container_name}" >/dev/null 2>&1 || true
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

"${build_script}"

if [[ ! -f "${package_path}" ]]; then
  echo "Expected package at ${package_path} after build" >&2
  exit 1
fi

echo "Starting fresh Docker .deb install test in ${image}"

"${DOCKER_CMD[@]}" run \
  --name "${container_name}" \
  --rm \
  -v "${package_path}:/tmp/qodex.deb:ro" \
  "${image}" \
  bash -lc "
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y ca-certificates dbus-daemon xvfb
apt-get install -y /tmp/qodex.deb

node_program=\"\$(command -v node || command -v nodejs)\"
if [[ -z \"\${node_program}\" ]]; then
  echo 'node executable was not installed with qodex' >&2
  exit 1
fi

timeout 30s env ELECTRON_OZONE_PLATFORM_HINT=x11 dbus-run-session -- \
  xvfb-run -a \"\${node_program}\" /usr/lib/qodex/thread-ui/app/scripts/start-electron.js --smoke-test
"

echo "Docker .deb install test completed successfully."
