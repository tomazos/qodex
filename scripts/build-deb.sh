#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${BUILD_DIR:-${repo_root}/build-deb}"
dist_dir="${DIST_DIR:-${repo_root}/dist}"
build_type="${BUILD_TYPE:-Release}"
generator="${CMAKE_GENERATOR:-Ninja}"
version="$(tr -d '[:space:]' < "${repo_root}/VERSION")"
arch="${DEB_ARCH:-$(dpkg --print-architecture)}"
package_name="qodex"
package_path="${dist_dir}/${package_name}_${version}_${arch}.deb"
stage_dir=""
helper_dir=""

cleanup() {
    rm -rf "${stage_dir}"
    rm -rf "${helper_dir}"
}

trap cleanup EXIT

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Required command not found on PATH: %s\n' "$1" >&2
        exit 1
    fi
}

require_command cmake
require_command dpkg
require_command dpkg-deb
require_command dpkg-shlibdeps
require_command find
require_command sort

cmake_args=(
    -S "${repo_root}"
    -B "${build_dir}"
    -G "${generator}"
    -DCMAKE_BUILD_TYPE="${build_type}"
    -DCMAKE_INSTALL_MESSAGE=LAZY
    -DCMAKE_INSTALL_PREFIX=/usr
)

cmake "${cmake_args[@]}"
cmake --build "${build_dir}" -j"$(nproc)"

mkdir -p "${dist_dir}"
stage_dir="$(mktemp -d "${build_dir}/package.XXXXXX")"
helper_dir="$(mktemp -d "${build_dir}/deps.XXXXXX")"
chmod 0755 "${stage_dir}"
mkdir -p "${stage_dir}/DEBIAN" "${helper_dir}/debian"
DESTDIR="${stage_dir}" cmake --install "${build_dir}" --config "${build_type}"

stage_qodex="${stage_dir}/usr/bin/qodex"
stage_thread_ui_root="${stage_dir}/usr/lib/qodex/thread-ui"
stage_thread_ui_app_dir="${stage_thread_ui_root}/app"
stage_thread_ui_native_dir="${stage_thread_ui_root}/native"
stage_electron_dist_dir="${stage_thread_ui_app_dir}/node_modules/electron/dist"

if [[ ! -x "${stage_qodex}" ]]; then
    printf 'Expected installed qodex executable at %s\n' "${stage_qodex}" >&2
    exit 1
fi

if [[ ! -d "${stage_thread_ui_app_dir}" ]]; then
    printf 'Expected installed thread-ui app directory at %s\n' "${stage_thread_ui_app_dir}" >&2
    exit 1
fi

if [[ ! -f "${stage_thread_ui_native_dir}/qodex_thread_ui.node" ]]; then
    printf 'Expected installed native module at %s\n' "${stage_thread_ui_native_dir}/qodex_thread_ui.node" >&2
    exit 1
fi

find "${stage_dir}" -type d -exec chmod 0755 {} +
find "${stage_dir}" -type f -exec chmod 0644 {} +
chmod 0755 \
    "${stage_qodex}" \
    "${stage_electron_dist_dir}/electron" \
    "${stage_electron_dist_dir}/chrome_crashpad_handler"

if [[ -e "${stage_electron_dist_dir}/chrome-sandbox" ]]; then
    chmod 0755 "${stage_electron_dist_dir}/chrome-sandbox"
fi

cat > "${helper_dir}/debian/control" <<EOF
Source: ${package_name}
Section: utils
Priority: optional
Maintainer: Andrew Tomazos <andrewtomazos@gmail.com>
Standards-Version: 4.6.0

Package: ${package_name}
Architecture: ${arch}
Description: Qt desktop shell for Codex sessions with an Electron thread UI
EOF

declare -a shlibdep_inputs
declare -a shlibdep_args

shlibdep_inputs+=("${stage_qodex}")
while IFS= read -r -d '' path; do
    shlibdep_inputs+=("${path}")
done < <(
    find "${stage_thread_ui_root}" -type f \( -name '*.node' -o -name '*.so' -o -name '*.so.*' -o -perm /111 \) -print0 | sort -z
)

for input_path in "${shlibdep_inputs[@]}"; do
    shlibdep_args+=(-e "${input_path}")
done

deps_output="$(
    cd "${helper_dir}" && dpkg-shlibdeps -O \
        -l"${stage_electron_dist_dir}" \
        "${shlibdep_args[@]}" 2>&1 || true
)"
depends="$(printf '%s\n' "${deps_output}" | sed -n 's/^shlibs:Depends=//p')"

if [[ -z "${depends}" ]]; then
    printf '%s\n' "${deps_output}" >&2
    printf 'Failed to compute shared-library dependencies for %s\n' "${stage_qodex}" >&2
    exit 1
fi

manual_depends="$(
    printf '%s\n' "${depends}" "libqt6sql6-sqlite" "nodejs" |
        tr ',' '\n' |
        sed 's/^ *//; s/ *$//' |
        sed '/^$/d' |
        sort -u |
        awk 'BEGIN { first = 1 } { if (!first) printf ", "; printf "%s", $0; first = 0 } END { printf "\n" }'
)"

cat > "${stage_dir}/DEBIAN/control" <<EOF
Package: ${package_name}
Version: ${version}
Section: utils
Priority: optional
Architecture: ${arch}
Maintainer: Andrew Tomazos <andrewtomazos@gmail.com>
Depends: ${manual_depends}
Description: Qt desktop shell for Codex sessions with an Electron thread UI
EOF

rm -f "${package_path}"
dpkg-deb --build --root-owner-group "${stage_dir}" "${package_path}" >/dev/null

printf 'Created %s\n' "${package_path}"
