# Product

<!-- impeccable:product-schema 1 -->

## Platform

desktop — native Windows application (Qt 6 / C++). No web view, no Electron.
Future ports to Linux/macOS are intended but not committed.

## Users

Primary: radiologists and clinicians reading diagnostic studies at a
workstation — speed, correct geometry, and trustworthy measurements are
the job. Secondary: researchers and AI engineers who need segmentation
overlays, pluggable models, and exportable annotations on the same data.
Clinical use leads; research features must never degrade the read.

## Product Purpose

Scanthia is an open-source, medical-grade DICOM viewer meant to be a
credible successor to Horos and OsiriX: fast local image review, MPR and
3D reconstruction, measurements, PACS interoperability, segmentation
overlays, and in-app AI inference. Success means a radiologist can pick
it over a commercial viewer for a real read and a researcher can run a
model without leaving the app.

## Positioning

The only viewer that is simultaneously: (1) free and open source, so
hospitals and researchers can audit and extend it; (2) natively
cross-platform instead of macOS-only like Horos/OsiriX; and (3)
AI-native — ONNX models and plugins run inside the viewer against the
live volume, not in a separate tool. No incumbent can claim all three.

## Operating Context

Reading-room workstations with one or more monitors; studies arrive via
PACS query/retrieve or local folders; the library dock lists indexed
studies and a split/compare pane holds priors. Sessions are interactive
and keyboard/mouse driven: window-level drags, slice wheel, Z+wheel zoom,
cine playback. Typical data: CT/MR series of hundreds of slices, plus
scouts, dose reports, and non-image objects that must not break the app.

## Capabilities and Constraints

Confirmed functionality today: DICOM load/scan/index with thumbnails and
a local study database; single-view and MPR+3D layouts; window/level with
DICOM auto-WL and presets; thick-slab MPR (AVG/MIP/MinIP); color maps;
orientation labels; measurements (distance, ROI stats, 3-point angle)
with per-series JSON persistence; cine with speed control; linked
MPR zoom/pan; detachable panes for multi-monitor; side-by-side compare
with drag-and-drop from the library; segmentation overlay display and
brush/eraser editing; ONNX inference (CPU; GPU EP when a GPU runtime is
present) with a model library; plugin system; screenshot export;
PACS query/retrieve and a local C-STORE SCP; unsharp-mask sharpening.

Hard constraint confirmed by the owner: performance — native C++/GPU,
no Electron, no web view. The project is already MIT-licensed open
source and local-first (no cloud dependency); those are existing
product facts to preserve.

Known gaps vs. incumbents: multiframe/enhanced DICOM and RT objects,
curved/oblique MPR, hanging protocols beyond basic auto-WL, DICOM PR/SR
export, structured reporting, and clinical validation.

## Brand Commitments

Name: Scanthia. MIT-licensed open source. No other binding brand assets
exist yet — no logo, palette, or type system has been committed.

## Evidence on Hand

Real CT test data in `tests/data` (synthetic phantom series plus
downloaded sample studies used during development). ONNX segmentation
models (lungs, airways, lymph nodes, mediastinum) under `tests/data` and
the model library. No testimonials, benchmarks, or clinical evidence —
do not fabricate any.

## Product Principles

1. Never block the read — UI, AI, and indexing stay off the critical
   path of viewing a study.
2. Correctness over cosmetics — geometry, measurements, and display
   fidelity must be right before they are pretty.
3. Speed is a feature — native code and GPU rendering; interactions
   must feel instant.
4. Everything local — data, annotations, and models live on the
   workstation; no silent cloud calls.
5. Extensible by default — AI models and plugins are first-class,
   loadable without a rebuild.

## Accessibility & Inclusion

High-contrast rendering for image content; dark theme required for
reading-room use. Keyboard-driven interaction is expected (tool
shortcuts, cine, zoom modifier).
