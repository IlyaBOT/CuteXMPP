#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP_NAME="${APP_NAME:-CuteXMPP}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/linux-release}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist/linux}"
APPDIR="${APPDIR:-${DIST_DIR}/${APP_NAME}.AppDir}"
ICON_FILE="${ICON_FILE:-${ROOT_DIR}/assets/cutexmpp-logo.png}"
DESKTOP_FILE_SOURCE="${DESKTOP_FILE_SOURCE:-${ROOT_DIR}/packaging/CuteXMPP.desktop}"
LINUXDEPLOY_URL="${LINUXDEPLOY_URL:-https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage}"
LINUXDEPLOY_PLUGIN_QT_URL="${LINUXDEPLOY_PLUGIN_QT_URL:-https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage}"
LINUXDEPLOY_PLUGIN_APPIMAGE_URL="${LINUXDEPLOY_PLUGIN_APPIMAGE_URL:-https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-x86_64.AppImage}"
PROJECT_VERSION="${PROJECT_VERSION:-$(sed -n 's/^project(CuteXMPP VERSION \([0-9.]*\).*/\1/p' "${ROOT_DIR}/CMakeLists.txt")}"
QMAKE_BIN="${QMAKE_BIN:-${QMAKE:-}}"
QT_ROOT_DIR="${QT_ROOT_DIR:-}"
QXMPP_INSTALL_PREFIX="${QXMPP_INSTALL_PREFIX:-}"
TOOLS_DIR="${DIST_DIR}/tools"
LINUXDEPLOY_APPIMAGE="${TOOLS_DIR}/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_PLUGIN_QT_APPIMAGE="${TOOLS_DIR}/linuxdeploy-plugin-qt-x86_64.AppImage"
LINUXDEPLOY_PLUGIN_APPIMAGE_APPIMAGE="${TOOLS_DIR}/linuxdeploy-plugin-appimage-x86_64.AppImage"
LINUXDEPLOY_BIN="${TOOLS_DIR}/linuxdeploy"
LINUXDEPLOY_PLUGIN_QT_BIN="${TOOLS_DIR}/linuxdeploy-plugin-qt"
LINUXDEPLOY_PLUGIN_APPIMAGE_BIN="${TOOLS_DIR}/linuxdeploy-plugin-appimage"
TARGET_BINARY="${BUILD_DIR}/${APP_NAME}"
APPIMAGE_OUTPUT="${DIST_DIR}/${APP_NAME}-linux-x64.AppImage"
TARBALL_OUTPUT="${DIST_DIR}/${APP_NAME}-linux-x64.tar.gz"

if [[ ! -f "${TARGET_BINARY}" ]]; then
    echo "[ERROR] Build output is missing: ${TARGET_BINARY}" >&2
    exit 1
fi

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

rm -rf "${DIST_DIR}"
mkdir -p \
    "${APPDIR}/usr/bin" \
    "${APPDIR}/usr/lib" \
    "${APPDIR}/usr/share/applications" \
    "${APPDIR}/usr/share/icons/hicolor/256x256/apps" \
    "${TOOLS_DIR}"

cp "${TARGET_BINARY}" "${APPDIR}/usr/bin/${APP_NAME}"
chmod +x "${APPDIR}/usr/bin/${APP_NAME}"
cp "${DESKTOP_FILE_SOURCE}" "${APPDIR}/usr/share/applications/${APP_NAME}.desktop"
cp "${ICON_FILE}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/${APP_NAME}.png"
cp "${DESKTOP_FILE_SOURCE}" "${APPDIR}/${APP_NAME}.desktop"
cp "${ICON_FILE}" "${APPDIR}/.DirIcon"

cat > "${APPDIR}/AppRun" <<EOF
#!/usr/bin/env bash
set -e
HERE="\$(cd "\$(dirname "\$0")" && pwd)"
export LD_LIBRARY_PATH="\${HERE}/usr/lib:\${LD_LIBRARY_PATH:-}"
exec "\${HERE}/usr/bin/${APP_NAME}" "\$@"
EOF
chmod +x "${APPDIR}/AppRun"

if [[ -n "${QXMPP_INSTALL_PREFIX}" && -d "${QXMPP_INSTALL_PREFIX}/lib" ]]; then
    while IFS= read -r shared_lib; do
        cp -a "${shared_lib}" "${APPDIR}/usr/lib/"
    done < <(find "${QXMPP_INSTALL_PREFIX}/lib" -maxdepth 1 \( -type f -o -type l \) \( -name 'libQXmppQt6*.so*' -o -name 'libQXmpp*.so*' \) | sort -u)
fi

curl --fail --location --retry 5 --retry-delay 5 \
    --output "${LINUXDEPLOY_APPIMAGE}" \
    "${LINUXDEPLOY_URL}"
curl --fail --location --retry 5 --retry-delay 5 \
    --output "${LINUXDEPLOY_PLUGIN_QT_APPIMAGE}" \
    "${LINUXDEPLOY_PLUGIN_QT_URL}"
curl --fail --location --retry 5 --retry-delay 5 \
    --output "${LINUXDEPLOY_PLUGIN_APPIMAGE_APPIMAGE}" \
    "${LINUXDEPLOY_PLUGIN_APPIMAGE_URL}"
chmod +x "${LINUXDEPLOY_APPIMAGE}" "${LINUXDEPLOY_PLUGIN_QT_APPIMAGE}" "${LINUXDEPLOY_PLUGIN_APPIMAGE_APPIMAGE}"
ln -sf "$(basename "${LINUXDEPLOY_APPIMAGE}")" "${LINUXDEPLOY_BIN}"
ln -sf "$(basename "${LINUXDEPLOY_PLUGIN_QT_APPIMAGE}")" "${LINUXDEPLOY_PLUGIN_QT_BIN}"
ln -sf "$(basename "${LINUXDEPLOY_PLUGIN_APPIMAGE_APPIMAGE}")" "${LINUXDEPLOY_PLUGIN_APPIMAGE_BIN}"

export QMAKE="${QMAKE_BIN}"
export VERSION="${PROJECT_VERSION:-0.1.0}"
export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH="${ARCH:-x86_64}"
export PATH="${TOOLS_DIR}:${PATH}"
if [[ -n "${QXMPP_INSTALL_PREFIX}" && -d "${QXMPP_INSTALL_PREFIX}/lib" ]]; then
    export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${QXMPP_INSTALL_PREFIX}/lib:${LD_LIBRARY_PATH:-}"
else
    export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${LD_LIBRARY_PATH:-}"
fi

pushd "${DIST_DIR}" >/dev/null
rm -f "${DIST_DIR}"/*.AppImage
"${LINUXDEPLOY_BIN}" \
    --appdir "${APPDIR}" \
    -e "${APPDIR}/usr/bin/${APP_NAME}" \
    -d "${APPDIR}/${APP_NAME}.desktop" \
    -i "${APPDIR}/.DirIcon" \
    --plugin qt \
    --output appimage

GENERATED_APPIMAGE="$(find "${DIST_DIR}" -maxdepth 1 -type f -name '*.AppImage' | head -n 1)"
if [[ -z "${GENERATED_APPIMAGE}" ]]; then
    echo "[ERROR] linuxdeploy did not produce an AppImage." >&2
    exit 1
fi

mv -f "${GENERATED_APPIMAGE}" "${APPIMAGE_OUTPUT}"

if [[ ! -f "${APPIMAGE_OUTPUT}" ]]; then
    echo "[ERROR] linuxdeploy did not produce an AppImage." >&2
    exit 1
fi

tar -C "${DIST_DIR}" -czf "${TARBALL_OUTPUT}" "$(basename "${APPDIR}")"
popd >/dev/null

echo "[INFO] Created ${APPIMAGE_OUTPUT}"
echo "[INFO] Created ${TARBALL_OUTPUT}"
