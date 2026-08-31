# Adwaita Modern provenance

This Nightlock pack is a curated subset of GNOME Adwaita Icon Theme 50.0. It
contains 70 generic system, file, and UI icons and intentionally excludes
application, vendor, service, and product logos.

## Pinned upstream source

- Project: Adwaita Icon Theme (GNOME Project)
- Repository: <https://gitlab.gnome.org/GNOME/adwaita-icon-theme>
- Release tag: `50.0`
- Annotated tag object: `9c528e249ec07ba5f1566ecd467ecb581ef74281`
- Peeled tag commit: `551245ae75fdc42cde42a8cf24ca2ccab9d3a815`
- Archive: `adwaita-icon-theme-50.0.tar.xz`
- Canonical URL: <https://download.gnome.org/sources/adwaita-icon-theme/50/adwaita-icon-theme-50.0.tar.xz>
- Archive size: `4,517,092` bytes
- Archive SHA-256: `fac6e0401fca714780561a081b8f7e27c3bc1db34ebda4da175081f26b24d460`
- Retrieved: `2026-08-31`

## Selection and transformation

Nightlock rendered the selected upstream SVG files with librsvg
`rsvg-convert` 2.62.3 (Cairo 1.18.4) directly to transparent 128×128 PNG files
using `--width 128 --height 128 --keep-aspect-ratio`. No upstream build or
conversion script was executed. No visual editing, recoloring, or compositing
was applied. Runtime files are PNG only.

Full-color scalable icons retain their upstream basenames. Symbolic output
IDs omit the upstream `-symbolic` suffix. Source directories map to Nightlock
categories as follows:

| Nightlock category | Upstream directory | Source naming |
| --- | --- | --- |
| `actions` | `Adwaita/symbolic/actions/` | `<id>-symbolic.svg` |
| `folders-places` | `Adwaita/scalable/places/` | `<id>.svg` |
| `devices-volumes` | `Adwaita/scalable/devices/` | `<id>.svg` |
| `documents-mimetypes` | `Adwaita/scalable/mimetypes/` | `<id>.svg` |
| `alerts-badges` | `Adwaita/symbolic/status/` | `<id>-symbolic.svg` |
| `settings-categories` | `Adwaita/symbolic/categories/` | `<id>-symbolic.svg` |
| `ui-symbols` | `Adwaita/symbolic/ui/` | `<id>-symbolic.svg` |

The manifest records the exact SHA-256 digest and byte size of every rendered
PNG. The total runtime PNG payload is `241,717` bytes.

## License and attribution

Upstream states that Adwaita Icon Theme may be redistributed under either the
GNU Lesser General Public License version 3 or Creative Commons Attribution-
ShareAlike 3.0. The exact upstream notice is retained in `LICENSE`; the
upstream LGPL text and CC BY-SA attribution notice are retained in
`LICENSE-LGPL-3.0` and `LICENSE-CC-BY-SA-3.0`. Upstream says attribution as
“GNOME Project” is sufficient. The upstream contributor list is retained in
`AUTHORS`. The pinned source archive contains the corresponding SVG sources
for every rendered PNG.
