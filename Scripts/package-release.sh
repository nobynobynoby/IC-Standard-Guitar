#!/bin/sh
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${1:-$PROJECT_DIR/build}"
VERSION="$(sed -n 's/^project(ICStandardGuitar VERSION \([0-9.]*\).*/\1/p' "$PROJECT_DIR/CMakeLists.txt")"
: "${VERSION:=1.1.0}"
PACKAGE_NAME="IC-Standard-Guitar-v${VERSION}-macOS-Universal"
ARTEFACTS_DIR="$BUILD_DIR/ICStandardGuitar_artefacts/Release"
DIST_DIR="$PROJECT_DIR/dist"
STAGE_DIR="$DIST_DIR/$PACKAGE_NAME"

AU_SRC="$ARTEFACTS_DIR/AU/IC Standard Guitar.component"
APP_SRC="$ARTEFACTS_DIR/Standalone/IC Standard Guitar.app"

if [ ! -d "$AU_SRC" ] || [ ! -d "$APP_SRC" ]; then
    echo "Error: Release artefacts not found in $ARTEFACTS_DIR" >&2
    echo "Run: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release -j8" >&2
    exit 1
fi

rm -rf "$STAGE_DIR" "$DIST_DIR/${PACKAGE_NAME}.zip"
mkdir -p "$STAGE_DIR"

cp -R "$AU_SRC" "$STAGE_DIR/"
cp -R "$APP_SRC" "$STAGE_DIR/"
cp "$PROJECT_DIR/README.md" "$STAGE_DIR/"
cp "$PROJECT_DIR/LICENSE" "$STAGE_DIR/"

# Ensure clean ad-hoc signatures and strip any extended attributes before archiving
for BUNDLE in "$STAGE_DIR/IC Standard Guitar.component" "$STAGE_DIR/IC Standard Guitar.app"; do
    /usr/bin/xattr -cr "$BUNDLE"
    /usr/bin/codesign --force --sign - --deep "$BUNDLE"
    /usr/bin/codesign --verify --deep --strict "$BUNDLE"
done

/usr/bin/ditto -c -k --norsrc --keepParent "$STAGE_DIR" "$DIST_DIR/${PACKAGE_NAME}.zip"
rm -rf "$STAGE_DIR"

echo "Created release package: $DIST_DIR/${PACKAGE_NAME}.zip"
