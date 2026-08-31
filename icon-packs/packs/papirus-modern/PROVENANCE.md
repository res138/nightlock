# Papirus Modern provenance

This Nightlock pack is a curated subset of Papirus Icon Theme `20250501`. It
contains 96 generic Linux desktop icons and intentionally excludes product,
vendor, distribution, service, application, and brand logos. The few icons in
the normalized `applications` category represent generic tasks such as a
calculator, notes, text editing, web browsing, and a terminal; they do not
represent named applications.

## Pinned upstream source

- Project: Papirus Icon Theme (Papirus Development Team)
- Repository: <https://github.com/PapirusDevelopmentTeam/papirus-icon-theme>
- Release: <https://github.com/PapirusDevelopmentTeam/papirus-icon-theme/releases/tag/20250501>
- Release tag: `20250501`
- Annotated tag object: `1920d4a1b0d7ffcd8b7ffb2f2cf584d877383c5b`
- Tag commit: `863243822aa6693d4eb3758fd6444690d365c695`
- Archive: `papirus-icon-theme-20250501.tar.gz`
- Canonical URL: <https://github.com/PapirusDevelopmentTeam/papirus-icon-theme/archive/refs/tags/20250501.tar.gz>
- Archive size: `32,512,075` bytes
- Archive SHA-256: `3831a487f813479ad3224fdbfb0c7023f23056899bc78c93737f341aa655558e`
- Retrieved: `2026-08-31`

The importer's selected-source fingerprint is
`71bcbe4d5f40f6f4928fe395192e61ffc4820afc783ffa285ea0ccf814059daf`.
`SOURCE_MAPPING.tsv` records, for every PNG, its requested upstream path, the
ultimate SVG path after resolving upstream aliases, and the SVG SHA-256.

## Selection and transformation

Nightlock rendered the selected upstream SVG files with librsvg
`rsvg-convert` 2.62.3 (Cairo 1.18.4) directly to transparent 128×128 PNG files
using `--width 128 --height 128 --keep-aspect-ratio`. No upstream build,
installer, conversion, or maintenance script was executed. No visual editing,
recoloring, or compositing was applied. Every runtime image is an 8-bit PNG
with an alpha channel; no SVG, ICO, or ICNS file is used at runtime.

The selected upstream directories map to Nightlock's normalized categories as
follows:

| Nightlock category | Upstream source |
| --- | --- |
| `applications` | `Papirus/64x64/apps/` (generic-purpose icons only) |
| `actions` | `Papirus/24x24/actions/` |
| `folders-places` | `Papirus/64x64/places/` |
| `devices-volumes` | `Papirus/64x64/devices/` |
| `documents-mimetypes` | `Papirus/64x64/mimetypes/` |
| `network-sharing` | `Papirus/64x64/devices/` |
| `users-accounts` | `Papirus/48x48/status/` and `Papirus/64x64/apps/` |
| `settings-categories` | `Papirus/64x64/apps/` |
| `status-menu` | `Papirus/48x48/status/` |
| `alerts-badges` | `Papirus/48x48/status/` |
| `emblems` | `Papirus/48x48/emblems/` |

The manifest records the exact SHA-256 digest and byte size of every rendered
PNG. `tools/icon_packs/import_papirus_modern.py` reproduces the transformation
from an extracted copy of the verified pinned archive and refuses modified
metadata or selected SVG source content.

## License and attribution

The upstream README grants the Papirus icon theme under the GNU General Public
License, version 3, without an "or any later version" grant. This pack
therefore uses the precise SPDX expression `GPL-3.0-only`. The exact upstream
GPLv3 text is retained as `LICENSE`, and the exact upstream `AUTHORS` file is
retained, including maintainers, contributors, and credits for base artwork.
The corresponding SVG source is available in the pinned archive above, with a
per-file lookup in `SOURCE_MAPPING.tsv`.

Papirus' upstream repository contains many third-party trademarks. None of
those logo files is selected for this curated pack.
