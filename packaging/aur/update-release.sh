#!/usr/bin/env bash
# update-release.sh — Update a clone of the AUR "replayer" repository to a
# release and commit it. Pushing is left to the caller.
#
# Usage:
#   packaging/aur/update-release.sh <version> <aur-clone-dir>
#
# Takes replayer/PKGBUILD from this repository, sets pkgver to <version> and
# pkgrel to 1, fills in the checksum of the GitHub source tarball, test-builds
# the package and regenerates .SRCINFO. Needs makepkg and the build
# dependencies of the package installed (not run as root).

set -euo pipefail

VERSION="$1"
AUR_DIR="$(realpath "$2")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Version '$VERSION' isn't a stable version like 2.0.0" >&2
    exit 1
fi

log() { echo "==> $*"; }

# Keep downloads and build output out of the AUR clone
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
export SRCDEST="$WORK_DIR" BUILDDIR="$WORK_DIR" PKGDEST="$WORK_DIR"

cp "$SCRIPT_DIR/replayer/PKGBUILD" "$AUR_DIR/PKGBUILD"
cd "$AUR_DIR"
sed -i "s/^pkgver=.*/pkgver=$VERSION/; s/^pkgrel=.*/pkgrel=1/" PKGBUILD

log "Updating checksum..."
sums="$(makepkg -g)"
# Replace the sha256sums=(...) line with the new one
awk -v sums="$sums" '/^sha256sums=/ { print sums; next } { print }' PKGBUILD > PKGBUILD.new
mv PKGBUILD.new PKGBUILD

log "Test-building..."
makepkg -f --noconfirm

makepkg --printsrcinfo > .SRCINFO

git add PKGBUILD .SRCINFO
if git diff --cached --quiet; then
    log "AUR package is already at $VERSION"
else
    git commit -q -m "Update to $VERSION"
    log "Committed update to $VERSION"
fi
