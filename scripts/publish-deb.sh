#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
build_script="${repo_root}/scripts/build-deb.sh"
build_dir="${BUILD_DIR:-${repo_root}/build-deb}"
dist_dir="${DIST_DIR:-${repo_root}/dist}"
version="$(tr -d '[:space:]' < "${repo_root}/VERSION")"
arch="${DEB_ARCH:-$(dpkg --print-architecture)}"
package_name="qodex"
package_filename="${package_name}_${version}_${arch}.deb"
package_path="${dist_dir}/${package_filename}"
apt_repo_root="${APT_REPO_ROOT:-/srv/apt/tomazos}"
apt_repo_suite="${APT_REPO_SUITE:-stable}"
apt_repo_component="${APT_REPO_COMPONENT:-main}"
apt_repo_origin="${APT_REPO_ORIGIN:-tomazos}"
apt_repo_label="${APT_REPO_LABEL:-tomazos}"
apt_signing_key="${APT_GPG_KEY_ID:-685C521F252074CF}"
apt_repo_use_sudo="${APT_REPO_USE_SUDO:-auto}"
package_pool_dir="${apt_repo_root}/pool/${apt_repo_component}/${package_name:0:1}/${package_name}"
package_archive_dir="${apt_repo_root}/archive"
packages_dir="${apt_repo_root}/dists/${apt_repo_suite}/${apt_repo_component}/binary-${arch}"
release_dir="${apt_repo_root}/dists/${apt_repo_suite}"
temp_dir=""

cleanup() {
    rm -rf "${temp_dir}"
}

trap cleanup EXIT

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Required command not found on PATH: %s\n' "$1" >&2
        exit 1
    fi
}

run_repo_command() {
    case "${apt_repo_use_sudo}" in
        0|false|FALSE|no|NO)
            "$@"
            ;;
        1|true|TRUE|yes|YES)
            sudo "$@"
            ;;
        auto|AUTO)
            if [[ -w "${apt_repo_root}" ]]; then
                "$@"
            else
                sudo "$@"
            fi
            ;;
        *)
            printf 'Invalid APT_REPO_USE_SUDO value: %s\n' "${apt_repo_use_sudo}" >&2
            exit 1
            ;;
    esac
}

require_command apt-ftparchive
require_command dpkg-scanpackages
require_command gpg
require_command gzip
require_command install

"${build_script}"

if [[ ! -f "${package_path}" ]]; then
    printf 'Expected package at %s after build\n' "${package_path}" >&2
    exit 1
fi

temp_dir="$(mktemp -d "${build_dir}/publish.XXXXXX")"
temp_metadata_root="${temp_dir}/metadata-root"
temp_packages_path="${temp_dir}/Packages"
temp_packages_gz_path="${temp_dir}/Packages.gz"
temp_release_path="${temp_dir}/Release"
temp_inrelease_path="${temp_dir}/InRelease"
temp_release_gpg_path="${temp_dir}/Release.gpg"

run_repo_command install -d "${package_pool_dir}" "${package_archive_dir}" "${packages_dir}"
run_repo_command install -m 0644 "${package_path}" "${package_pool_dir}/${package_filename}"
run_repo_command install -m 0644 "${package_path}" "${package_archive_dir}/${package_filename}"

mkdir -p "${temp_metadata_root}/dists/${apt_repo_suite}/${apt_repo_component}/binary-${arch}"

(
    cd "${apt_repo_root}"
    dpkg-scanpackages --multiversion pool /dev/null > "${temp_packages_path}"
)
gzip -9c "${temp_packages_path}" > "${temp_packages_gz_path}"

install -m 0644 "${temp_packages_path}" "${temp_metadata_root}/dists/${apt_repo_suite}/${apt_repo_component}/binary-${arch}/Packages"
install -m 0644 "${temp_packages_gz_path}" "${temp_metadata_root}/dists/${apt_repo_suite}/${apt_repo_component}/binary-${arch}/Packages.gz"

(
    cd "${temp_metadata_root}"
    apt-ftparchive \
        -o "APT::FTPArchive::Release::Origin=${apt_repo_origin}" \
        -o "APT::FTPArchive::Release::Label=${apt_repo_label}" \
        -o "APT::FTPArchive::Release::Suite=${apt_repo_suite}" \
        -o "APT::FTPArchive::Release::Codename=${apt_repo_suite}" \
        -o "APT::FTPArchive::Release::Components=${apt_repo_component}" \
        -o "APT::FTPArchive::Release::Architectures=${arch}" \
        release "dists/${apt_repo_suite}" > "${temp_release_path}"
)

gpg --batch --yes --local-user "${apt_signing_key}" --output "${temp_release_gpg_path}" --detach-sign "${temp_release_path}"
gpg --batch --yes --local-user "${apt_signing_key}" --output "${temp_inrelease_path}" --clearsign "${temp_release_path}"

run_repo_command install -m 0644 "${temp_packages_path}" "${packages_dir}/Packages"
run_repo_command install -m 0644 "${temp_packages_gz_path}" "${packages_dir}/Packages.gz"
run_repo_command install -m 0644 "${temp_release_path}" "${release_dir}/Release"
run_repo_command install -m 0644 "${temp_release_gpg_path}" "${release_dir}/Release.gpg"
run_repo_command install -m 0644 "${temp_inrelease_path}" "${release_dir}/InRelease"

printf 'Published %s to %s\n' "${package_filename}" "${apt_repo_root}"
