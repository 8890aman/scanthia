#!/usr/bin/env bash
# Build a standalone Scanthia package + NSIS installer.
export PATH=/usr/bin:/bin:/ucrt64/bin:$PATH
set -e
cd /c/Users/Administrator/Desktop/medaview

BUILD=build
PKG=dist/Scanthia

echo "=== Staging $PKG ==="
rm -rf dist
mkdir -p "$PKG/plugins"

cp "$BUILD/src/app/Scanthia.exe" "$PKG/"
cp DISCLAIMER.txt README.md "$PKG/"
cp /ucrt64/bin/storescu.exe "$PKG/" 2>/dev/null || true

# Qt runtime + plugins (platforms, styles, imageformats, svg, tls)
/ucrt64/bin/windeployqt6.exe --release --no-translations --no-system-d3d-compiler \
    "$PKG/Scanthia.exe"

# ONNX Runtime: prefer the vendored GPU (DirectML) build so inference
# runs on any DX12 GPU; fall back to the MSYS2 CPU-only dll.
if [ -d thirdparty/onnxruntime-gpu ]; then
    cp -f thirdparty/onnxruntime-gpu/*.dll "$PKG/" 2>/dev/null || true
else
    for dll in onnxruntime.dll onnxruntime_providers_shared.dll; do
        cp -n "/ucrt64/bin/$dll" "$PKG/" 2>/dev/null || true
    done
fi

# Non-Qt runtime DLLs (VTK, ITK, DCMTK, GDCM, ONNX Runtime, GCC...) —
# transitive closure over every binary we stage.
for pass in 1 2 3; do
    for f in "$PKG"/*.dll "$PKG"/*/*.dll "$PKG"/*.exe; do
        [ -e "$f" ] && ldd "$f" 2>/dev/null | awk '/ucrt64/ {print $3}'
    done
done | sort -u | while read -r d; do
    cp -n "$d" "$PKG/" 2>/dev/null || true
done

# AI plugins, if built
cp "$BUILD"/plugins/*.dll "$PKG/plugins/" 2>/dev/null || true

echo "=== Staged $(ls "$PKG" | wc -l) files ==="
du -sh "$PKG"

echo "=== Building installer ==="
/ucrt64/bin/makensis.exe installer.nsi
ls -lh Scanthia-Setup.exe
