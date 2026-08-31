# GNOME Legacy 3.12 provenance

This Nightlock pack is a curated, unmodified subset of the upstream
`gnome-icon-theme` 3.12.0 release. It contains 64 generic desktop icons and
intentionally excludes `start-here` and product/application logos.

## Pinned upstream source

- Project: GNOME Icon Theme (GNOME Project)
- Release: `3.12.0`
- Archive: `gnome-icon-theme-3.12.0.tar.xz`
- Canonical URL: <https://download.gnome.org/sources/gnome-icon-theme/3.12/gnome-icon-theme-3.12.0.tar.xz>
- Archive size: `17,742,624` bytes
- Archive SHA-256: `359e720b9202d3aba8d477752c4cd11eced368182281d51ffd64c8572b4e503a`
- Retrieved: `2026-08-31`

The archive checksum, rather than a mutable branch, is the reproducibility
pin for this historical release.

## Selection and transformation

Nightlock copied the upstream 48×48 PNG renditions without resampling or
editing. Runtime files are PNG only. Their original basenames are preserved,
so each source path can be reconstructed from this table:

| Nightlock category | Upstream directory |
| --- | --- |
| `applications` | `gnome/48x48/apps/` |
| `actions` | `gnome/48x48/actions/` |
| `folders-places` | `gnome/48x48/places/` |
| `devices-volumes` | `gnome/48x48/devices/` |
| `documents-mimetypes` | `gnome/48x48/mimetypes/` |
| `alerts-badges` | `gnome/48x48/status/` |
| `emblems` | `gnome/48x48/emblems/` |

The manifest records the exact SHA-256 digest and byte size of every selected
PNG. The total runtime PNG payload is `176,997` bytes.

## License and attribution

Upstream states that GNOME Icon Theme may be redistributed under either the
GNU Lesser General Public License version 3 or Creative Commons Attribution-
ShareAlike 3.0. The exact upstream notice is retained in `LICENSE`; the
upstream LGPL text and CC BY-SA attribution notice are retained in
`LICENSE-LGPL-3.0` and `LICENSE-CC-BY-SA-3.0`. Upstream says attribution as
“GNOME Project” is sufficient. The upstream contributor list is retained in
`AUTHORS`.
