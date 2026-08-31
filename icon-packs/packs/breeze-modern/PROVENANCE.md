# Breeze Modern provenance

This Nightlock pack is a curated subset of KDE Breeze Icons 6.29.0. It
contains 64 generic desktop icons and intentionally excludes KDE, Plasma,
application, vendor, service, and product logos.

## Pinned upstream source

- Project: Breeze Icons (KDE Community)
- Repository: <https://invent.kde.org/frameworks/breeze-icons>
- Release tag: `v6.29.0`
- Tag commit: `a806a2dbc75b9d7d089c2d0053bc5db46a31340a`
- Archive: `breeze-icons-6.29.0.tar.xz`
- Canonical URL: <https://download.kde.org/stable/frameworks/6.29/breeze-icons-6.29.0.tar.xz>
- Archive size: `2,106,836` bytes
- Archive SHA-256: `2948a313ffe35895b846dde5a93302a1fcd587ad8dfb860b8757629209586fe4`
- Retrieved: `2026-08-31`

## Selection and transformation

Nightlock rendered the selected upstream SVG source files with librsvg
`rsvg-convert` 2.62.3 (Cairo 1.18.4) directly to transparent 128×128 PNG files
using `--width 128 --height 128 --keep-aspect-ratio`. No upstream build or
conversion script was executed. No visual editing, recoloring, or compositing
was applied. Runtime files are PNG only.

The selected basenames are unchanged. Their original source directories map
to Nightlock categories as follows:

| Nightlock category | Upstream directory |
| --- | --- |
| `applications` | `icons/apps/48/` |
| `actions` | `icons/actions/32/` |
| `folders-places` | `icons/places/64/` |
| `devices-volumes` | `icons/devices/64/` |
| `documents-mimetypes` | `icons/mimetypes/64/` |
| `alerts-badges` | `icons/status/64/` |
| `emblems` | `icons/emblems/22/` |

The manifest records the exact SHA-256 digest and byte size of every rendered
PNG. The total runtime PNG payload is `185,290` bytes.

## License and attribution

The icons are distributed under `LGPL-3.0-or-later`. The exact upstream
`COPYING-ICONS` file is retained as `LICENSE`; it explicitly clarifies that
the LGPL applies to the artwork library and identifies SVG as corresponding
source where available. `AUTHORS` retains the upstream copyright notice and
links to the pinned repository history for complete per-file authorship.
