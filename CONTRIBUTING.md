# Contributing to Scanthia

Thanks for your interest. Scanthia is early — the codebase is small enough to
read end-to-end, so start there.

## Development setup

See the build instructions in [README.md](README.md) and the pitfalls in
[AGENTS.md](AGENTS.md) (works for humans too).

## Guidelines

- **Correctness over cleverness.** This is medical imaging software —
  orientations, spacing, and Hounsfield math must be right. Every volume is
  reoriented to canonical axial (identity direction) at load; keep that
  invariant.
- Keep `src/core` free of Qt and VTK *widget* dependencies; rendering code
  lives in `src/render`.
- AI plugins implement `meda::IAiPlugin` — see `plugins/ThresholdSegPlugin.cpp`.
- Run `meda_tests` and `meda_probe` before submitting.

## Reporting issues

Please include the modality, transfer syntax (uncompressed/JPEG2000/etc.),
and whether the data is anonymized when reporting reader bugs.
