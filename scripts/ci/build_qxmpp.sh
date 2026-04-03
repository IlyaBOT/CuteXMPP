#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
QXMPP_VERSION="${QXMPP_VERSION:-1.14.4}"
WORK_DIR="${WORK_DIR:-${ROOT_DIR}/.deps/qxmpp}"
ARCHIVE_PATH="${ARCHIVE_PATH:-${WORK_DIR}/qxmpp-v${QXMPP_VERSION}.tar.gz}"
SOURCE_ROOT="${SOURCE_ROOT:-${WORK_DIR}/src}"
SOURCE_DIR="${SOURCE_ROOT}/qxmpp-v${QXMPP_VERSION}"
BUILD_DIR="${BUILD_DIR:-${WORK_DIR}/build}"
INSTALL_PREFIX="${INSTALL_PREFIX:-${WORK_DIR}/install}"
QMAKE_BIN="${QMAKE_BIN:-${QMAKE:-}}"

if [[ -z "${QMAKE_BIN}" ]]; then
    if command -v qmake6 >/dev/null 2>&1; then
        QMAKE_BIN="$(command -v qmake6)"
    elif command -v qmake >/dev/null 2>&1; then
        QMAKE_BIN="$(command -v qmake)"
    else
        echo "[ERROR] qmake was not found in PATH." >&2
        exit 1
    fi
fi

QT_PREFIX_PATH="${QT_PREFIX_PATH:-$("${QMAKE_BIN}" -query QT_INSTALL_PREFIX)}"

mkdir -p "${WORK_DIR}" "${SOURCE_ROOT}"

if [[ ! -f "${ARCHIVE_PATH}" ]]; then
    curl --fail --location --retry 5 --retry-delay 5 \
        --output "${ARCHIVE_PATH}" \
        "https://invent.kde.org/libraries/qxmpp/-/archive/v${QXMPP_VERSION}/qxmpp-v${QXMPP_VERSION}.tar.gz"
fi

rm -rf "${SOURCE_DIR}" "${BUILD_DIR}" "${INSTALL_PREFIX}"
tar -xzf "${ARCHIVE_PATH}" -C "${SOURCE_ROOT}"

cmake -S "${SOURCE_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_PREFIX_PATH="${QT_PREFIX_PATH}" \
    -DBUILD_WITH_QT6=ON \
    -DBUILD_SHARED=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_DOCUMENTATION=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_OMEMO=OFF \
    -DWITH_GSTREAMER=OFF \
    -DWITH_QCA=OFF

cmake --build "${BUILD_DIR}" --parallel
cmake --install "${BUILD_DIR}"

echo "[INFO] QXmpp ${QXMPP_VERSION} installed to ${INSTALL_PREFIX}"

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    {
        echo "install_prefix=${INSTALL_PREFIX}"
        echo "qt_prefix_path=${QT_PREFIX_PATH}"
        echo "qmake_bin=${QMAKE_BIN}"
    } >> "${GITHUB_OUTPUT}"
fi
