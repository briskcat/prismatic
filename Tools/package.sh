#!/bin/bash
# Builds Prism FX for Apple Silicon and Intel and packages the AU and VST3 into a zip for sharing.
#   ./Tools/package.sh            -> dist/Prism-FX-<version>-macOS.zip
# The plug-ins are signed ad hoc (no Apple Developer ID), so people opening them for the first
# time need the step in INSTALL.txt.
set -euo pipefail
cd "$(dirname "$0")/.."

version=$(sed -n 's/^project(PrismFX VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
build=build-release
art="$build/PrismFX_artefacts/Release"

cmake -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DPRISM_LTO=ON -DPRISM_INSTALL_PLUGINS=OFF
cmake --build "$build" -j6 --target PrismFX_AU PrismFX_VST3

stage="dist/Prism FX $version"
rm -rf "$stage"
mkdir -p "$stage"
cp -R "$art/AU/Prism FX.component" "$stage/"
cp -R "$art/VST3/Prism FX.vst3" "$stage/"
for p in "$stage/Prism FX.component" "$stage/Prism FX.vst3"; do
    codesign --force --deep --sign - "$p"
done
# the install notes point at this repo's GitHub page, if it has one
repo=$(git remote get-url origin 2>/dev/null | sed -e 's/\.git$//' -e 's|^git@github.com:|https://github.com/|' || true)
if [ -n "$repo" ]; then
    sed "s|REPO_URL|$repo|" Tools/INSTALL.txt > "$stage/INSTALL.txt"
else
    grep -v REPO_URL Tools/INSTALL.txt > "$stage/INSTALL.txt"
fi
mkdir -p "$stage/Licenses"
cp LICENSE "$stage/Licenses/LICENSE (AGPLv3).txt"
cp NOTICE.md TRADEMARKS.md "$stage/Licenses/"
cp Assets/fonts/OFL-*.txt "$stage/Licenses/"

zip_name="Prism-FX-$version-macOS.zip"
(cd dist && rm -f "$zip_name" && ditto -c -k --sequesterRsrc --keepParent "Prism FX $version" "$zip_name")
echo "dist/$zip_name"
