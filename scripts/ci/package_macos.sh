#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP_NAME="${APP_NAME:-CuteXMPP}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/macos-release}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist/macos}"
SOURCE_APP="${SOURCE_APP:-${BUILD_DIR}/${APP_NAME}.app}"
TARGET_APP="${DIST_DIR}/${APP_NAME}.app"
DMG_OUTPUT="${DIST_DIR}/${APP_NAME}-macos-x64.dmg"

if [[ ! -d "${SOURCE_APP}" ]]; then
    echo "[ERROR] macOS bundle is missing: ${SOURCE_APP}" >&2
    exit 1
fi

if command -v macdeployqt >/dev/null 2>&1; then
    MACDEPLOYQT_BIN="$(command -v macdeployqt)"
else
    if command -v qmake6 >/dev/null 2>&1; then
        QMAKE_BIN="$(command -v qmake6)"
    elif command -v qmake >/dev/null 2>&1; then
        QMAKE_BIN="$(command -v qmake)"
    else
        echo "[ERROR] qmake was not found in PATH." >&2
        exit 1
    fi
    QT_BINS_DIR="$("${QMAKE_BIN}" -query QT_INSTALL_BINS)"
    MACDEPLOYQT_BIN="${QT_BINS_DIR}/macdeployqt"
fi

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"
cp -R "${SOURCE_APP}" "${TARGET_APP}"

"${MACDEPLOYQT_BIN}" "${TARGET_APP}" -always-overwrite -verbose=2
hdiutil create -volname "${APP_NAME}" -srcfolder "${TARGET_APP}" -ov -format UDZO "${DMG_OUTPUT}"

echo "[INFO] Created ${TARGET_APP}"
echo "[INFO] Created ${DMG_OUTPUT}"
