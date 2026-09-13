# Scanthia — Competitive Analysis & Roadmap

Goal: an open-source DICOM viewer that beats **Horos / OsiriX** (the macOS
reference viewers) and competes with **Weasis / 3D Slicer / OHIF** on the
desktop. This document is the honest gap list — what they have, what we
have, and what it takes to win.

Legend: ✅ done · 🟡 partial · ❌ missing

---

## 1. Core viewing

| Feature | Horos/OsiriX | Scanthia today | Priority |
|---|---|---|---|
| Series load + thumbnails | ✅ fast, DICOMDIR-aware | ✅ thumbnails via CPU WL map | done |
| Window/level + presets | ✅ | ✅ | done |
| Zoom / pan / scroll / cine | ✅ | ✅ | done |
| Measurements (distance) | ✅ + angle, ROI stats, Hounsfield probe | 🟡 distance only | **P1** |
| Magnifying glass | ✅ | ❌ | P2 |
| Annotations / key images | ✅ | ❌ | P2 |
| Hanging protocols (auto-layout by study) | ✅ | ❌ | **P1** |
| Multi-series compare / link scroll | ✅ | ❌ | **P1** |
| 4D / temporal series | ✅ | ❌ | P2 |
| Key Object Selection, PR (presentation state) | ✅ | ❌ | P2 |
| SR (structured reports) viewing | ✅ | ❌ | P2 |
| Encrypted / transfer-syntax coverage (JPEG-LS, JPEG2000, RLE) | ✅ via DCMTK | ✅ GDCM codecs + negotiated | done |

## 2. MPR & 3D

| Feature | Horos/OsiriX | Scanthia | Priority |
|---|---|---|---|
| Orthogonal MPR + crosshairs | ✅ | ✅ | done |
| Thick MPR (avg/MIP/MinIP slabs) | ✅ | ❌ | **P1** |
| Oblique / curved MPR (vessel tracing) | ✅ (curved CPR is a flagship) | ❌ | **P2** |
| GPU volume rendering | ✅ | ✅ presets | done |
| Volume clipping / cropping planes | ✅ | ❌ | P2 |
| Fly-through / endoscopy | ✅ | ❌ | P3 |
| Surface rendering (mesh from seg) | ✅ | ❌ | P2 |
| MIP slab mode | ✅ | 🟡 MIP blend only | P1 |

## 3. PACS & data

| Feature | Horos/OsiriX | Scanthia | Priority |
|---|---|---|---|
| C-ECHO / C-FIND / C-MOVE | ✅ | ✅ | done |
| C-GET | ✅ | ✅ | done |
| C-STORE SCP (receive pushes) | ✅ | ✅ port 11112 | done |
| Local database / study browser (DICOMDIR index) | ✅ core feature | ✅ SQLite index + tree browser | done |
| DICOM send (C-STORE SCU) | ✅ | ❌ | P1 |
| DICOMweb (WADO-RS / QIDO-RS / STOW-RS) | 🟡 via plugins | ❌ | **P1** |
| Anonymization / de-identification export | ✅ | ❌ | P2 |
| CD/DVD import, DICOMDIR media | ✅ | ❌ | P2 |
| Zip/import/export non-DICOM (JPEG/PNG/TIFF) | ✅ | ❌ | P2 |

## 4. Segmentation & AI

| Feature | Horos/OsiriX | Scanthia | Priority |
|---|---|---|---|
| DICOM SEG display | ✅ | ✅ | done |
| RTSTRUCT display | ✅ | ❌ | P1 |
| Manual segmentation / brush tools | ✅ | ❌ | **P1** |
| ROI statistics (mean HU, volume, histogram) | ✅ | ❌ | P1 |
| AI segmentation plugins | ❌ (nothing built-in) | ✅ ONNX + plugin API | **edge** |
| MONAI / TotalSegmentator integration | ❌ | 🟡 engine ready | **edge** |
| Grow-cut / region growing | ✅ | ❌ | P2 |
| Quantitative PET (SUV bw/lbm/bsa) | ✅ | ❌ | P1 |
| PET-CT fusion | ✅ | ❌ | P1 |

## 5. Platform & workflow

| Feature | Horos/OsiriX | Scanthia | Priority |
|---|---|---|---|
| Cross-platform | ❌ macOS only | ✅ Win/Linux/macOS | **edge** |
| Reporting (export PDF) | ✅ | ❌ | P2 |
| Keyboard-first workflow | ✅ | ❌ | P2 |
| Plugin architecture | 🟡 Obj-C plugins, stale | ✅ modern Qt/ONNX | **edge** |
| Web / zero-install | ❌ | ❌ (possible later via wasm) | P3 |
| Dark modern UI | 🟡 dated | 🟡 default Qt | P2 |
| i18n | ✅ | ❌ | P3 |
| DICOM conformance statement | ✅ | ❌ | P2 |

## 6. The hard truth

- **"Medical grade" = regulatory clearance.** OsiriX MD is FDA/CE cleared;
  Horos is not cleared but is used clinically. Certification is a
  *process*, not code — budget for it separately (or stay
  research/not-for-diagnosis, like 3D Slicer).
- **Breadth is the moat.** Horos wins on ~15 years of edge-case handling:
  weird transfer syntaxes, broken vendor files, monochrome calibration,
  huge studies on old hardware. We close that gap through the plugin API
  and an aggressive test corpus, not one big feature.
- **Where we can genuinely win:**
  1. *AI-native* — no other mainstream viewer ships an inference engine +
     plugin ABI. Ship TotalSegmentator/MONAI plugins and it's instantly
     differentiated.
  2. *Cross-platform* — Horos is macOS-only; Windows + Linux is a huge
     underserved market.
  3. *Modern UX* — GPU-first rendering, instant startup, sane defaults.
  4. *DICOMweb-native* — modern cloud PACS interop is bolted onto Horos.

---

## 7. Execution plan

**Phase 1 — "usable daily" (the P0s):**
- [x] Study database: SQLite index of local studies/series,
      thumbnails (CPU-rendered middle slice per series)
- [x] Transfer syntaxes: JPEG-LS, JPEG2000, RLE, JPEG decode —
      GDCM handles decode; negotiation now offers all compressed syntaxes
      (verified: J2K lossless series loads with correct HU values)
- [x] Series thumbnails in the left dock (two-level study browser)
- [ ] Study search/filter UI
- [ ] ROI stats overlay + angle measurement

**Phase 2 — "respectable radiologist tool" (P1s):**
- [ ] Hanging protocols (modality/body-part → layout + presets)
- [ ] Thick-slab MPR (MIP/MinIP/average)
- [ ] Multi-series compare + linked scrolling
- [ ] DICOMweb client (WADO/QIDO/STOW)
- [ ] C-STORE SCU (send to PACS)
- [ ] RTSTRUCT display; brush-based manual segmentation editing
- [ ] PET SUV + PET-CT fusion
- [ ] DICOM send/export dialog

**Phase 3 — "beat them" (differentiators):**
- [ ] Plugin marketplace: curated ONNX/MONAI models (TotalSegmentator,
      lung nodules, etc.) installable in-app
- [ ] Curved planar reformation
- [ ] GPU pipeline polish: async volume load, mipmapped streaming for
      10k-slice studies
- [ ] Optional cloud-sharing / anonymized export
- [ ] First-class test corpus: public datasets (TCGA, IDC) in CI

**Phase 4 — "medical grade" (if pursued):**
- [ ] DICOM conformance statement
- [ ] Formal QMS (ISO 13485-style docs), IEC 62304 software lifecycle
- [ ] FDA 510(k) / CE MDR submission — multi-year, needs legal/clinical
      partners

## 8. What to deliberately NOT copy

- Don't copy Horos's UI — it's a decade behind. Aim at 3D Slicer /
  modern-OHIF interaction quality with a native feel.
- Don't build a monolith — the plugin ABI is the long-term moat; keep the
  core viewer thin and fast.
- Don't chase film printing (DICOM print) unless a user demands it.
