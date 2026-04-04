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
QT_ROOT_DIR="${QT_ROOT_DIR:-}"
QT_PREFIX_PATH="${QT_PREFIX_PATH:-}"
QXMPP_BUILD_SHARED="${QXMPP_BUILD_SHARED:-OFF}"

if [[ -z "${QMAKE_BIN}" && -n "${QT_ROOT_DIR}" ]]; then
    for candidate in "${QT_ROOT_DIR}/bin/qmake6" "${QT_ROOT_DIR}/bin/qmake"; do
        if [[ -x "${candidate}" ]]; then
            QMAKE_BIN="${candidate}"
            break
        fi
    done
fi

if [[ -z "${QMAKE_BIN}" ]]; then
    if command -v qmake6 >/dev/null 2>&1; then
        QMAKE_BIN="$(command -v qmake6)"
    elif command -v qmake >/dev/null 2>&1; then
        QMAKE_BIN="$(command -v qmake)"
    elif [[ -z "${QT_PREFIX_PATH}" && -z "${QT_ROOT_DIR}" ]]; then
        echo "[ERROR] qmake was not found in PATH." >&2
        exit 1
    fi
fi

if [[ -z "${QT_PREFIX_PATH}" && -n "${QMAKE_BIN}" ]]; then
    QT_PREFIX_PATH="$("${QMAKE_BIN}" -query QT_INSTALL_PREFIX)"
fi

if [[ -z "${QT_PREFIX_PATH}" && -n "${QT_ROOT_DIR}" ]]; then
    QT_PREFIX_PATH="${QT_ROOT_DIR}"
fi

if [[ -z "${QT_PREFIX_PATH}" ]]; then
    echo "[ERROR] Failed to resolve QT_PREFIX_PATH." >&2
    exit 1
fi

mkdir -p "${WORK_DIR}" "${SOURCE_ROOT}"

if [[ ! -f "${ARCHIVE_PATH}" ]]; then
    curl --fail --location --retry 5 --retry-delay 5 \
        --output "${ARCHIVE_PATH}" \
        "https://invent.kde.org/libraries/qxmpp/-/archive/v${QXMPP_VERSION}/qxmpp-v${QXMPP_VERSION}.tar.gz"
fi

rm -rf "${SOURCE_DIR}" "${BUILD_DIR}" "${INSTALL_PREFIX}"
tar -xzf "${ARCHIVE_PATH}" -C "${SOURCE_ROOT}"

# QXmpp 1.14.4 uses Q_DECL_DEPRECATED_X after QXMPP_EXPORT on a few deprecated
# classes. That ordering breaks on the GitHub Linux toolchain when those headers
# are included from server sources. Reorder the macros before building.
python3 - "${SOURCE_DIR}" <<'PY'
from pathlib import Path
import sys

source_dir = Path(sys.argv[1])
replacements = {
    source_dir / "src" / "base" / "QXmppBindIq.h":
        ('class QXMPP_EXPORT Q_DECL_DEPRECATED_X("Removed from public API") QXmppBindIq',
         'class Q_DECL_DEPRECATED_X("Removed from public API") QXMPP_EXPORT QXmppBindIq'),
    source_dir / "src" / "client" / "QXmppRemoteMethod.h":
        ('class QXMPP_EXPORT Q_DECL_DEPRECATED_X("Removed from public API (unmaintained)") QXmppRemoteMethod',
         'class Q_DECL_DEPRECATED_X("Removed from public API (unmaintained)") QXMPP_EXPORT QXmppRemoteMethod'),
    source_dir / "src" / "client" / "QXmppRpcManager.h":
        ('class QXMPP_EXPORT Q_DECL_DEPRECATED_X("Removed from public API (unmaintained)") QXmppRpcManager',
         'class Q_DECL_DEPRECATED_X("Removed from public API (unmaintained)") QXMPP_EXPORT QXmppRpcManager'),
}

for path, (old, new) in replacements.items():
    text = path.read_text(encoding="utf-8")
    if old in text:
        text = text.replace(old, new)
        path.write_text(text, encoding="utf-8")
PY

cmake -S "${SOURCE_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_PREFIX_PATH="${QT_PREFIX_PATH}" \
    -DBUILD_WITH_QT6=ON \
    -DBUILD_SHARED="${QXMPP_BUILD_SHARED}" \
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
