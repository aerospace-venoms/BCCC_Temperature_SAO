#!/usr/bin/env bash
#
# bump-firmware.sh — rebuild the firmware and refresh the prebuilt release UF2
# so its filename always matches the embedded FW_VERSION (no drift).
#
# Usage:
#   scripts/bump-firmware.sh              # rebuild at the current FW_VERSION
#   scripts/bump-firmware.sh 1.1.0        # set FW_VERSION=1.1.0, then rebuild
#
# FW_VERSION in CMakeLists.txt is the single source of truth for the version.
set -euo pipefail

# Resolve repo root from this script's location (works from any CWD).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

CMAKELISTS="CMakeLists.txt"
BUILD_DIR="build"
FW_DIR="firmware"

read_version() {
    sed -n 's/^set(FW_VERSION "\(.*\)").*/\1/p' "$CMAKELISTS" | head -n1
}

# Optional: bump the version in CMakeLists.txt first.
if [[ $# -ge 1 ]]; then
    NEW_VERSION="$1"
    if ! [[ "$NEW_VERSION" =~ ^[0-9A-Za-z.+-]+$ ]]; then
        echo "error: '$NEW_VERSION' doesn't look like a version string" >&2
        exit 1
    fi
    if ! grep -q '^set(FW_VERSION "' "$CMAKELISTS"; then
        echo "error: no FW_VERSION line found in $CMAKELISTS" >&2
        exit 1
    fi
    sed -i "s/^set(FW_VERSION \"[^\"]*\")/set(FW_VERSION \"$NEW_VERSION\")/" "$CMAKELISTS"
    echo "==> FW_VERSION set to $NEW_VERSION in $CMAKELISTS"
fi

VERSION="$(read_version)"
if [[ -z "$VERSION" ]]; then
    echo "error: could not read FW_VERSION from $CMAKELISTS" >&2
    exit 1
fi
echo "==> Building firmware v$VERSION"

# Configure (if needed) and build.
cmake -B "$BUILD_DIR" -S . >/dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)" >/dev/null

UF2_SRC="$BUILD_DIR/thermometer.uf2"
if [[ ! -f "$UF2_SRC" ]]; then
    echo "error: build did not produce $UF2_SRC" >&2
    exit 1
fi

# Refresh the prebuilt release image: drop any stale versioned UF2s, then copy.
mkdir -p "$FW_DIR"
rm -f "$FW_DIR"/thermometer-v*.uf2
DEST="$FW_DIR/thermometer-v$VERSION.uf2"
cp "$UF2_SRC" "$DEST"
echo "==> Wrote $DEST ($(du -h "$DEST" | cut -f1))"

# Sanity-check that the version literal made it into the binary.
# (No `grep -q`: it exits early, and under `pipefail` the resulting SIGPIPE on
# `strings` would falsely report the pipeline as failed.)
if ! strings "$BUILD_DIR/thermometer.elf" | grep -xF "$VERSION" >/dev/null; then
    echo "warning: version literal '$VERSION' not found in the built binary" >&2
fi

echo ""
echo "Done. Remember to update version references in the docs if you bumped:"
echo "  - firmware/README.md   (version table + example filename)"
echo "  - README.md            (Flashing section example filename)"
echo "Then: git add -A && git commit"
