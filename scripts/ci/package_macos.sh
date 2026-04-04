#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP_NAME="${APP_NAME:-CuteXMPP}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/macos-release}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist/macos}"
SOURCE_APP="${SOURCE_APP:-${BUILD_DIR}/${APP_NAME}.app}"
TARGET_APP="${DIST_DIR}/${APP_NAME}.app"
DMG_OUTPUT="${DIST_DIR}/${APP_NAME}-macos-x64.dmg"
QMAKE_BIN="${QMAKE_BIN:-${QMAKE:-}}"
QT_ROOT_DIR="${QT_ROOT_DIR:-}"
QXMPP_INSTALL_PREFIX="${QXMPP_INSTALL_PREFIX:-}"

if [[ ! -d "${SOURCE_APP}" ]]; then
    echo "[ERROR] macOS bundle is missing: ${SOURCE_APP}" >&2
    exit 1
fi

if command -v macdeployqt >/dev/null 2>&1; then
    MACDEPLOYQT_BIN="$(command -v macdeployqt)"
else
    if [[ -z "${QMAKE_BIN}" ]]; then
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
        else
            echo "[ERROR] qmake was not found in PATH." >&2
            exit 1
        fi
    fi
    QT_BINS_DIR="$("${QMAKE_BIN}" -query QT_INSTALL_BINS)"
    MACDEPLOYQT_BIN="${QT_BINS_DIR}/macdeployqt"
fi

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"
cp -R "${SOURCE_APP}" "${TARGET_APP}"

if [[ -n "${QXMPP_INSTALL_PREFIX}" && -d "${QXMPP_INSTALL_PREFIX}/lib" ]]; then
    APP_BINARY="${TARGET_APP}/Contents/MacOS/${APP_NAME}"
    FRAMEWORKS_DIR="${TARGET_APP}/Contents/Frameworks"
    mkdir -p "${FRAMEWORKS_DIR}"

    while IFS= read -r dylib_path; do
        dylib_name="$(basename "${dylib_path}")"
        target_dylib="${FRAMEWORKS_DIR}/${dylib_name}"
        cp -f "${dylib_path}" "${target_dylib}"
        chmod u+w "${target_dylib}"
        install_name_tool -id "@rpath/${dylib_name}" "${target_dylib}"
    done < <(find "${QXMPP_INSTALL_PREFIX}/lib" -maxdepth 1 \( -type f -o -type l \) -name 'libQXmppQt6*.dylib' | sort -u)

    while IFS= read -r linked_lib; do
        if [[ "${linked_lib}" == *libQXmppQt6*.dylib ]]; then
            install_name_tool -change "${linked_lib}" "@executable_path/../Frameworks/$(basename "${linked_lib}")" "${APP_BINARY}"
        fi
    done < <(otool -L "${APP_BINARY}" | tail -n +2 | awk '{print $1}')
fi

"${MACDEPLOYQT_BIN}" "${TARGET_APP}" -always-overwrite -verbose=2
hdiutil create -volname "${APP_NAME}" -srcfolder "${TARGET_APP}" -ov -format UDZO "${DMG_OUTPUT}"

echo "[INFO] Created ${TARGET_APP}"
echo "[INFO] Created ${DMG_OUTPUT}"
