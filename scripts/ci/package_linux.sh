#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP_NAME="${APP_NAME:-CuteXMPP}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/linux-release}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist/linux}"
APPDIR="${APPDIR:-${DIST_DIR}/${APP_NAME}.AppDir}"
ICON_FILE="${ICON_FILE:-${ROOT_DIR}/assets/cutexmpp-logo.png}"
DESKTOP_FILE_SOURCE="${DESKTOP_FILE_SOURCE:-${ROOT_DIR}/packaging/CuteXMPP.desktop}"
LINUXDEPLOYQT_URL="${LINUXDEPLOYQT_URL:-https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage}"
PROJECT_VERSION="${PROJECT_VERSION:-$(sed -n 's/^project(CuteXMPP VERSION \([0-9.]*\).*/\1/p' "${ROOT_DIR}/CMakeLists.txt")}"
QMAKE_BIN="${QMAKE_BIN:-${QMAKE:-}}"
LINUXDEPLOYQT_BIN="${DIST_DIR}/tools/linuxdeployqt.AppImage"
TARGET_BINARY="${BUILD_DIR}/${APP_NAME}"
APPIMAGE_OUTPUT="${DIST_DIR}/${APP_NAME}-linux-x64.AppImage"
TARBALL_OUTPUT="${DIST_DIR}/${APP_NAME}-linux-x64.tar.gz"

if [[ ! -f "${TARGET_BINARY}" ]]; then
    echo "[ERROR] Build output is missing: ${TARGET_BINARY}" >&2
    exit 1
fi

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

rm -rf "${DIST_DIR}"
mkdir -p \
    "${APPDIR}/usr/bin" \
    "${APPDIR}/usr/share/applications" \
    "${APPDIR}/usr/share/icons/hicolor/256x256/apps" \
    "${DIST_DIR}/tools"

cp "${TARGET_BINARY}" "${APPDIR}/usr/bin/${APP_NAME}"
chmod +x "${APPDIR}/usr/bin/${APP_NAME}"
cp "${DESKTOP_FILE_SOURCE}" "${APPDIR}/usr/share/applications/${APP_NAME}.desktop"
cp "${ICON_FILE}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/${APP_NAME}.png"

curl --fail --location --retry 5 --retry-delay 5 \
    --output "${LINUXDEPLOYQT_BIN}" \
    "${LINUXDEPLOYQT_URL}"
chmod +x "${LINUXDEPLOYQT_BIN}"

export QMAKE="${QMAKE_BIN}"
export VERSION="${PROJECT_VERSION:-0.1.0}"
export APPIMAGE_EXTRACT_AND_RUN=1

pushd "${DIST_DIR}" >/dev/null
"${LINUXDEPLOYQT_BIN}" "${APPDIR}/usr/share/applications/${APP_NAME}.desktop" \
    -bundle-non-qt-libs \
    -unsupported-allow-new-glibc \
    -appimage \
    -verbose=2

generated_appimage="$(find "${DIST_DIR}" -maxdepth 1 -type f -name '*.AppImage' | head -n 1)"
if [[ -z "${generated_appimage}" ]]; then
    echo "[ERROR] linuxdeployqt did not produce an AppImage." >&2
    exit 1
fi

mv "${generated_appimage}" "${APPIMAGE_OUTPUT}"
tar -C "${DIST_DIR}" -czf "${TARBALL_OUTPUT}" "$(basename "${APPDIR}")"
popd >/dev/null

echo "[INFO] Created ${APPIMAGE_OUTPUT}"
echo "[INFO] Created ${TARBALL_OUTPUT}"
