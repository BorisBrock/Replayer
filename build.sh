#!/usr/bin/env bash
# build.sh — Configure, compile, and package Replayer as an AppImage.
#
# Requirements (Linux/GNOME):
#   Qt6, FFmpeg, pkg-config, cmake, ninja-build (or make)
#
# Usage:
#   ./build.sh [--clean]
#
# Output:
#   dist/replayer-<version>-x86_64.AppImage

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-release"
APPDIR="$BUILD_DIR/AppDir"
DIST_DIR="$SCRIPT_DIR/dist"
TOOLS_DIR="$SCRIPT_DIR/tools"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/VERSION")"
APPIMAGE="$DIST_DIR/replayer-$VERSION-x86_64.AppImage"

# ── helpers ──────────────────────────────────────────────────────────────────

log() { echo "==> $*"; }

download_if_missing() {
    local url="$1" dest="$2"
    if [[ ! -f "$dest" ]]; then
        log "Downloading $(basename "$dest")..."
        curl -fsSL "$url" -o "$dest"
        chmod +x "$dest"
    fi
}

# ── optional clean ────────────────────────────────────────────────────────────

if [[ "${1:-}" == "--clean" ]]; then
    log "Cleaning build directories..."
    rm -rf "$BUILD_DIR" "$DIST_DIR"
fi

# ── tools (linuxdeploy + Qt plugin) ──────────────────────────────────────────

mkdir -p "$TOOLS_DIR"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

download_if_missing \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
    "$LINUXDEPLOY"

download_if_missing \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
    "$LINUXDEPLOY_QT"

# ── configure ────────────────────────────────────────────────────────────────

log "Configuring (Release)..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

# ── build ─────────────────────────────────────────────────────────────────────

log "Building..."
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

# ── install into AppDir ───────────────────────────────────────────────────────

log "Installing into AppDir..."
rm -rf "$APPDIR"
# Also installs the .desktop file and icon (see CMakeLists.txt). --strip uses
# the system strip, which understands current ELF sections.
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --strip

# linuxdeploy-plugin-qt creates no AppRun hook for Qt 6 and relies on qt.conf
# alone, which some Qt builds ignore; Qt then can't find its platform plugins.
# linuxdeploy sources this hook from AppRun before starting the app.
mkdir -p "$APPDIR/apprun-hooks"
cat > "$APPDIR/apprun-hooks/replayer-qt.sh" <<'EOF'
export QT_PLUGIN_PATH="${APPDIR:-$this_dir}/usr/plugins"
EOF

# ── package ───────────────────────────────────────────────────────────────────

log "Bundling libraries..."

# NO_STRIP: linuxdeploy (and the copy embedded in its Qt plugin, which ignores
# $STRIP) bundles an old strip that fails on the .relr.dyn sections of
# libraries from current distros. Distro libraries are already stripped.
export NO_STRIP=1
QMAKE="$(which qmake6 2>/dev/null || which qmake)" \
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --plugin qt

# The bundled distro libraries (FFmpeg and its codecs) come with license terms
# of their own, see THIRD-PARTY-NOTICES.md. Ship their copyright files.
log "Collecting copyright files of bundled libraries..."
for lib in "$APPDIR"/usr/lib/*.so*; do
    pkg="$(dpkg -S "*/$(basename "$lib")" 2>/dev/null | head -n1 | cut -d: -f1)" || true
    copyright="/usr/share/doc/$pkg/copyright"
    if [[ -n "$pkg" && -f "$copyright" ]]; then
        install -Dm644 "$copyright" "$APPDIR/usr/share/doc/$pkg/copyright"
    fi
done

log "Packaging AppImage..."
mkdir -p "$DIST_DIR"
OUTPUT="$APPIMAGE" \
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --output appimage

log "Done: $APPIMAGE"
