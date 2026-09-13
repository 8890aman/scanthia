# Design — Flight-Deck Cockpit

<!-- Visual world committed via impeccable direction roll 44f38b76;
     user-chosen direction: flight-deck cockpit (pick card). -->

## World

A medical imaging workstation that reads like a glass-cockpit MFD: each
pane is an instrument, every value is a readout, and the interface is a
dark instrument panel that never competes with the image. Density and
precision over decoration — a 737 multifunction display, not a dashboard
marketing page.

## Scene

A dim reading room, one or two large monitors, eyes on grayscale anatomy
for hours. Everything around the image must be dark enough to disappear
at arm's length and legible enough to trust at a glance.

## Palette

| Token | Value | Use |
|---|---|---|
| `panel` | `#131519` | window/app ground — soft charcoal |
| `instrument` | `#1A1E24` | pane interiors, dock, menu ground |
| `bezel` | `#2E3540` | 1px pane borders, splitters, grooves |
| `bezel-lit` | `#3D4750` | hover/raised bezels |
| `label` | `#8E99A6` | secondary text, instrument legends |
| `data` | `#DCE2E9` | primary text and values |
| `nav` | `#4DA3E8` | professional blue — selection, active pane edge, focus |
| `amber` | `#E8A33D` | armed/alert states — active tool, warnings, ROI |
| `ok` | `#2ECC71` | progress, success, SCP listener |
| `danger` | `#E74C3C` | errors only |

Rules: blue marks what is selected or live; amber marks what is armed or
demands attention; they never swap. Image panes stay pure black — the
image is the only light source.

## Type

- Labels, menus, buttons: system sans (Segoe UI on Windows), 12–13px,
  instrument legends in 10px caps with +0.06em tracking.
- All numeric readouts — HUD corners, measurements, slice counters,
  coordinates: monospace (Cascadia Mono/Consolas), tabular figures.
- No all-caps body text; caps only for instrument legends.

## Structure & Motifs

- Panes carry a 1px `bezel` hairline; the focused pane's bezel takes a
  `nav` edge. No rounded corners anywhere — instruments are square.
- Splitters are 2px grooves in `bezel`; hover lights them.
- The status bar is an annunciator strip: left = mode/state, right =
  data readouts; progress shows as a green tape.
- Menus and docks are flat instruments — `instrument` ground, hairline
  separators, `nav` selection edge, no gradients.
- Scrollbars are thin grooves, 8px, `bezel` thumb on `panel`.
- Toolbar tools read as an annunciator row: active tool glows `amber`,
  idle tools are `label` on `instrument`.

## States

Every control ships: default / hover (bezel-lit) / armed-active (amber
or nav per role) / disabled (35% label) / loading (annunciator text +
tape). Focus is a 1px nav outline, never a dotted box.

## Motion

State changes only — no entrance animation, no decoration. Pane-edge
selection fades over ~120ms.

## Non-negotiables

- Nothing is brighter than the medical image.
- No rounded corners, no shadows, no gradients, no icons-in-lieu-of-text.
- HUD corner text is the only text inside a pane, in `data`/`label`
  monospace at ~11px.
