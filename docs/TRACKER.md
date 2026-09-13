# Scanthia — RadiAnt-Parity Tracker

Goal: close the gaps vs. RadiAnt (the best native Windows DICOM viewer)
while keeping the differentiators RadiAnt lacks (open source, AI/DirectML,
plugins, scheduled auto-pull, SEG editing).

Legend: ☐ todo · ◐ in progress · ☑ done

---

## Phase 1 — Quick wins

- [x] Negative / invert toggle — "Invert Grayscale" (`I` key) toggles
      Gray ↔ Inverted Gray LUT; also exposed in View → Color Map
- [x] `scanthia://` URL protocol — installer registers `HKCR\scanthia`;
      `scanthia://open?path=<dir>` indexes + loads,
      `scanthia://study/<uid>` loads from the library
- [x] JPEG export — Save Screenshot dialog now offers PNG/JPEG,
      quality 95, picked by file extension
- [x] Image sorting — View → Slice Order: Position / Instance Number /
      Acquisition Time / Filename, applied at load
- [x] Multi-frame cine — single-file multiframe (US/XA) reads frames as
      a 3D volume via GDCM, so the existing cine tool plays them

## Phase 2 — Async / incremental loading (the big feel fix)

- [x] Stream slices into a partially-allocated `vtkImageData` in Z order;
      `Modified()` per chunk so loaded slices are scrollable immediately
      (`DicomLoader::loadSeriesStreaming` — identity-direction series;
      others fall back to the reorienting path)
- [x] Progress during stream: status bar "Loading — N / total slices"
      + progress bar
- [x] Memory-safe mode: studies larger than 1 GiB (ITK+VTK buffers)
      are downsampled in-plane first (strideXY), then in-z (strideZ),
      to fit the budget. Spacing scales to match. Amber "DOWNSAMPLED"
      badge on every pane + status bar shows "Memory-safe mode:
      WxHxD (full: WxHxD)".
- [x] Cancel-in-flight when switching series mid-load (generation
      counter — stale ticks and results are dropped)

Files touched: `DicomLoader`, `Volume`, `SliceViewer`, `MprWidget`.

## Phase 3 — Fusion (PET-CT)

- [x] Right-click series → "Fuse This Series Onto Current View"
- [x] Resample fused series onto the base grid via ITK
      (`resampleOntoGrid` — linear interp, identity transform, output
      params from the base image)
- [x] Dedicated fusion channel in `SliceViewer` (second overlay actor —
      independent of the seg labelmap), hot-iron LUT with alpha ramp
      (low values transparent), View → Fusion Opacity presets + Clear
      Fusion
- [ ] Persist fusion pairing per study

## Phase 4 — Anonymizer + export breadth

- [x] Anonymizer dialog — tag checklist (patient/dates/institution/device/
      comments), GDCM `Anonymizer`, write copies to folder; library
      context-menu entry "Anonymize Study/Series Export..."
- [x] Video export — cine loop frames → ffmpeg → MP4
      (ffmpeg confirmed on system); File → Export Video (MP4)...
- [x] PDF / print — `QPrinter` → render pane → system print dialog
      (Ctrl+P); "Print to PDF" via the system dialog's printer list

## Phase 5 — Oblique MPR + polish

- [x] Oblique MPR — `SliceViewer::setObliqueAngles(pitch, yaw)` rotates
      the reslice plane normal; Alt+arrows rotate the active MPR pane
      in 5° steps, `O` resets. View menu entries too.
- [x] Remappable keyboard shortcuts — `QSettings("shortcuts")` maps
      action objectName → QKeySequence; applied at startup via
      `applyCustomShortcuts()`. Actions without overrides keep defaults.
- [x] i18n scaffolding — `QTranslator` loads `scanthia_<locale>.qm`
      from app dir or `translations/`; English `.ts` seed in
      `translations/scanthia_en.ts` (run `lupdate`/`lrelease` to build
      new locales)
- [x] Touch gestures — pinch to zoom, pan to scroll slices
      (Qt gesture events on `SliceViewer`)
- [ ] ARM64 build — MSYS2 `clangarm64` toolchain experiment

## Phase 6 — DSA (niche, only if requested)

- [ ] Pixel-shift mask subtraction mode for angio series

---

## Done this cycle (context)

- ☑ W/L drag fixed (custom handler, live HUD readout)
- ☑ Cine FPS + Seg Brush slider split-button flyouts
- ☑ PACS menu, DICOM Nodes store + manager, library source selector
- ☑ Send-to-node picker, auto-pull rules + Task Scheduler, burn CD
