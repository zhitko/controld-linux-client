#!/bin/bash
# Build controld-tray-x86_64.AppImage. QT_ROOT is the Qt gcc_64 prefix
# (the directory that contains bin/qmake). Run this on an old glibc, such as
# Ubuntu 20.04, so the AppImage runs on newer distributions.

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
QT_ROOT=${QT_ROOT:?Set QT_ROOT to the Qt gcc_64 directory}
BUILD_DIR=${BUILD_DIR:-"$ROOT/build-appimage"}
APPDIR=${APPDIR:-"$ROOT/AppDir"}
TOOLS=${TOOLS:-"$ROOT/.appimage-tools"}
OUTPUT=${OUTPUT:-"$ROOT/controld-tray-x86_64.AppImage"}

LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
PLUGIN_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"

mkdir -p "$TOOLS"
download() {
    local url=$1
    local destination=$2
    if [[ ! -x "$destination" ]]; then
        curl -fL --retry 3 --retry-delay 2 -o "$destination" "$url"
        chmod +x "$destination"
    fi
}
download "$LINUXDEPLOY_URL" "$TOOLS/linuxdeploy-x86_64.AppImage"
download "$PLUGIN_URL" "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage"
download "$APPIMAGETOOL_URL" "$TOOLS/appimagetool-x86_64.AppImage"

export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64
export PATH="$QT_ROOT/bin:$TOOLS:$PATH"
export QMAKE="$QT_ROOT/bin/qmake"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
export LD_LIBRARY_PATH="$QT_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_ROOT" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCONTROLD_HELPER_PRIVATE_RPATH=ON
cmake --build "$BUILD_DIR"
(
    cd "$BUILD_DIR"
    ctest --output-on-failure
)

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

linuxdeploy-x86_64.AppImage \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/controld-tray" \
    --executable "$APPDIR/usr/libexec/controld-tray-chrome-policy" \
    --desktop-file "$APPDIR/usr/share/applications/controld-tray.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/controld-tray.png" \
    --plugin qt

HELPER="$APPDIR/usr/libexec/controld-tray-chrome-policy"
patchelf --set-rpath '$ORIGIN/../lib/controld-tray' "$HELPER"
test "$(patchelf --print-rpath "$HELPER")" = '$ORIGIN/../lib/controld-tray'
"$BUILD_DIR/controld-check-helper" "$HELPER" "$APPDIR"

rm -f "$OUTPUT"
appimagetool-x86_64.AppImage "$APPDIR" "$OUTPUT"
chmod +x "$OUTPUT"
echo "Wrote $OUTPUT"
