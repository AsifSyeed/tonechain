#!/bin/bash
# Builds a universal (Apple Silicon + Intel) Release and packages it as
# dist/Tonechain-<version>-macOS.zip, ready to attach to a GitHub release.
set -euo pipefail

cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(Tonechain VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
NAME="Tonechain-${VERSION}-macOS"
OUT="dist/${NAME}"

cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --target Tonechain_AU Tonechain_Standalone

ARTEFACTS=build/Tonechain_artefacts/Release
rm -rf "$OUT" "dist/${NAME}.zip"
mkdir -p "$OUT"
ditto "$ARTEFACTS/AU/Tonechain.component" "$OUT/Tonechain.component"
ditto "$ARTEFACTS/Standalone/Tonechain.app" "$OUT/Tonechain.app"
cp LICENSE "$OUT/LICENSE.txt"

cat > "$OUT/INSTALL.txt" <<TXT
Tonechain ${VERSION}

1. Copy Tonechain.component to:
     ~/Library/Audio/Plug-Ins/Components/
   (In Finder: Go > Go to Folder..., paste the path above.)

2. This build isn't signed by Apple, so macOS blocks it until you run this
   once in Terminal:
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Tonechain.component

3. Restart Logic Pro. Tonechain is under Audio FX > Audio Units > Asif Syeed.
   If it doesn't show up: Logic Pro > Settings > Plug-in Manager, select
   Tonechain and click "Reset & Rescan Selection".

Standalone app: move Tonechain.app to /Applications, then run
     xattr -dr com.apple.quarantine /Applications/Tonechain.app

Full guide: https://github.com/AsifSyeed/tonechain
TXT

(cd dist && ditto -c -k --norsrc --noextattr --keepParent "${NAME}" "${NAME}.zip")
echo "Packaged dist/${NAME}.zip"
