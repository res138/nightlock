# Oxygen Classic provenance

This Nightlock pack is a curated subset of KDE Oxygen Icons 6.29.0. It
contains 70 generic desktop icons and intentionally excludes KDE, Plasma,
application, vendor, service, and product logos.

## Pinned upstream source

- Project: Oxygen Icons (KDE Community)
- Repository: <https://invent.kde.org/frameworks/oxygen-icons>
- Release tag: `v6.29.0`
- Annotated tag object: `828bb191f7bde7b98dfb88df7db82aaff6891931`
- Peeled tag commit: `0574833aed5d7f2946d0905168a214a7b604a60e`
- Official release archive: <https://download.kde.org/stable/frameworks/6.29/oxygen-icons-6.29.0.tar.xz>
- Retrieved: `2026-08-31`

The official archive is approximately 253 MiB. To avoid downloading or
vendoring the complete historical theme, Nightlock used a shallow partial
clone (`--depth 1 --filter=blob:none`) at the exact peeled tag commit and a
sparse checkout containing only the selected paths, license metadata, and
symlink targets. The immutable commit is the reproducibility pin; the full
archive was not used, so no archive checksum is asserted here.

## Selection and transformation

Nightlock copied the upstream 48×48 PNG renditions without resampling,
recoloring, or visual editing. Upstream symlink aliases were dereferenced into
ordinary standalone PNG files. No upstream build or conversion script was
executed. Runtime files are PNG only.

The selected basenames are unchanged. Their source directories map to
Nightlock categories as follows:

| Nightlock category | Upstream directory |
| --- | --- |
| `applications` | `48x48/apps/` |
| `actions` | `48x48/actions/` |
| `folders-places` | `48x48/places/` |
| `devices-volumes` | `48x48/devices/` |
| `documents-mimetypes` | `48x48/mimetypes/` |
| `alerts-badges` | `48x48/status/` |
| `settings-categories` | `48x48/categories/` |
| `emblems` | `48x48/emblems/` |

The manifest records the exact SHA-256 digest and byte size of every selected
PNG. The total runtime PNG payload is `215,210` bytes.

## License and source obligations

The pinned repository's `REUSE.toml` assigns the icon directories
`LGPL-3.0-or-later`. The exact upstream `COPYING` notice and full license are
retained as `LICENSE`, and the upstream contributor list is retained as
`AUTHORS`. The notice expressly defines SVG as source where available and PNG
otherwise. The repository and immutable commit above identify the complete
corresponding source for every selected rendition; release publication must
keep that source available alongside this notice.
