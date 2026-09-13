# Scanthia — agent notes

## Build (Windows, MSYS2 UCRT64)

```sh
export PATH=/usr/bin:/ucrt64/bin:$PATH
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

All deps come from MSYS2 ucrt64 packages (Qt6, VTK, ITK, DCMTK, GDCM,
onnxruntime, eigen3, nlohmann-json, …). The CMake is toolchain-agnostic —
MSVC + vcpkg should also work (untested).

## Verify

- `build/tests/meda_tests.exe` — unit tests
- `python tests/gen_test_data.py` then
  `build/tests/meda_probe.exe tests/data/ct_series out.png` — end-to-end
  scan → load → render → PNG on a synthetic 128×128×48 CT
- `build/src/app/Scanthia.exe` — GUI smoke test

## Gotchas

- **VTK module init**: any binary that renders must `VTK_MODULE_INIT` the
  needed modules (`vtkRenderingOpenGL2`, `vtkRenderingFreeType`,
  `vtkRenderingVolumeOpenGL2`, `vtkInteractionStyle`) — see
  `src/app/main.cpp`. Missing inits = segfault in Render().
- **onnxruntime.dll shadowing**: a stale `onnxruntime.dll` may live in
  `C:\Windows\System32` and win DLL search over PATH. The build copies the
  correct DLL next to the exe (app dir wins). Keep that POST_BUILD step.
- **Canonical orientation**: `DicomLoader` reorients every volume to
  identity direction at load; index space == display space. Segmentation
  and AI code must produce labelmaps on `Volume::itkImage()`'s grid.
- Link VTK with explicit `VTK::` module targets — `${VTK_LIBRARIES}` does
  not reliably include all requested components from MSYS2's VTK.
- When PowerShell runs MSYS2 bash, `$VAR` and inner quotes get mangled —
  put non-trivial shell in a `.sh` file and execute that.
