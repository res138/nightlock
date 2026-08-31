# Platinum Community provenance

Platinum Community is a Nightlock PNG rendering of 39 generic icons from the
openly licensed `@liiift-studio/mac-os9-ui` icon library. The selected artwork
was recreated by that community project as source-level pixel maps and SVG
geometry; it was not extracted from Apple software, resource forks, system
files, screenshots, or install media.

## Pinned upstream source

- Project: `@liiift-studio/mac-os9-ui`
- Repository: <https://github.com/Liiift-Studio/Mac-OS-9-React>
- Pinned tag: `v2.6.0`
- Resolved commit: `69ede5d5a739428a078f47ffcd0a32067121847a`
- Package version: `2.6.0`
- Imported: `2026-08-31`

The import tool rejects a different Git commit when given a checkout and also
verifies every input listed here before rendering:

| Upstream file | SHA-256 |
|---|---|
| `package.json` | `eb7b2b7c828715eafe91682dfde53b07833d8d58969af78fa3ca8e1bc7f57686` |
| `LICENSE` | `1ee00e29c67f9cae4cd94e37d00eae341cc70df05d02c783d450f058229448f9` |
| `ATTRIBUTION.md` | `f69a84b1723cb9c5d4b67ff5f0e87a8c063bb05ee3be41fb8d722af813c58ab0` |
| `src/components/Icon/pixel.tsx` | `71193f941cfd3f1e7d516f4c2a95b5743fa9559238ed4bd44e9ef2b46ea04e46` |
| `src/components/Icon/registry.ts` | `1c67700294019f924af3fa304aacf8dfe29e3b8a1664bd51647cbbb39b4aeaaf` |
| `src/components/Icon/categories/actions.tsx` | `e505d63b5ffd2d901d5fb15c797c1b5930452614db8b9f64705eae54961f4578` |
| `src/components/Icon/categories/files.tsx` | `1b940549c7fc3ead2ae362a5ebc091d2ba4b6d03ff5d0fed2e1328769d458523` |
| `src/components/Icon/categories/navigation.tsx` | `3a7ddaa5003cc8f473be29e33b2f61adceab37922ca1e1ab7217b298521210ee` |
| `src/components/Icon/categories/media.tsx` | `94391f3408bfc1841a4ce2dc8aaea597b11d52fa333dbeefc989f872bd829454` |
| `src/components/Icon/categories/status.tsx` | `12337d6ab3f84e29fb29fdb6ecd58643acdf7be25d5b83a7643295d28382786c` |
| `src/components/Icon/categories/ui.tsx` | `5593fdbf4504ea868f4c095398bac5eba89ed58bb044af6d7fa360ca09a2de24` |
| `src/styles/tokens.css` | `b2bdc3da9f0cb7a3968f9058dd69c1fe10ca16685700a100f2785755d2991aaa` |

## Selection and trademark review

The upstream registry contains 39 icons and this pack uses all 39. They depict
only generic actions, files, folders, media controls, status symbols, a user,
a lock, a calendar, and UI geometry. The selection contains no Apple logo,
Finder face, product logo, company logo, branded application icon, or copied
Apple system resource. “Platinum” describes the visual era and does not imply
affiliation with or endorsement by Apple.

Nightlock normalized the upstream exports as follows:

| Nightlock category | Upstream exports |
|---|---|
| `applications` | `CalendarIcon` |
| `actions` | `CloseIcon`, `TrashIcon`, `SearchIcon`, `CopyIcon`, `PrintIcon`, `DownloadIcon`, `LinkIcon`, `MailIcon`, `PlayIcon`, `PauseIcon`, `StopIcon` |
| `folders-places` | `FolderIcon`, `FolderOpenIcon`, `HomeIcon` |
| `devices-volumes` | `DiskIcon`, `HardDriveIcon` |
| `documents-mimetypes` | `DocumentIcon`, `ApplicationIcon`, `ImageIcon`, `MusicIcon` |
| `users-accounts` | `UserIcon` |
| `status-menu` | `VolumeIcon`, `VolumeMuteIcon` |
| `alerts-badges` | `AlertIcon`, `InfoIcon`, `ErrorIcon`, `QuestionIcon` |
| `emblems` | `CheckIcon`, `LockIcon` |
| `ui-symbols` | `ArrowUpIcon`, `ArrowDownIcon`, `ArrowLeftIcon`, `ArrowRightIcon`, `ChevronRightIcon`, `ChevronDownIcon`, `DividerIcon`, `ResizeHandleIcon`, `GrabberIcon` |

## Rendering and changes

`tools/icon_packs/import_platinum_community.py` performs the complete import
without installing upstream dependencies or executing any upstream script.

- Thirty-eight `createPixelIcon` character maps are rendered onto transparent
  256 × 256 canvases. Each source pixel becomes an exact 16 × 16 block, so no
  interpolation or anti-aliasing is introduced.
- The upstream `#` (`currentColor`) pixels are resolved to its default
  `--color-text` value, `#262626`; `o` pixels are resolved to the upstream
  highlight `#ffffff`; and `x` pixels to the upstream shade `#999999`.
- `DividerIcon` is the one JSX/SVG icon. Its 10 × 32 rectangular path geometry
  and `#ffffff`, `#bbbbbb`, and `#999999` fills are rasterized at eight device
  pixels per source unit and centered horizontally on a transparent 256 × 256
  canvas.
- Output is deterministic, metadata-free, 8-bit RGBA PNG. The Nightlock IDs
  and category placement are adaptations. Every runtime PNG byte size and
  SHA-256 digest is recorded in `manifest.json`.

## License and required attribution

The upstream implementation is distributed under the MIT License, and its
root `LICENSE` is retained verbatim beside this file. Upstream states that the
visual designs are based on Michael Feeney's “Mac OS 9 UI Kit (Macostalgia),”
licensed under Creative Commons Attribution 4.0. The exact upstream
`ATTRIBUTION.md` is also retained beside this file.

Attribution: icon implementation by Liiift Studio under MIT; original UI Kit
design by Michael Feeney, <https://swallowmygraphicdesign.com/project/macostalgia>,
under CC BY 4.0, <https://creativecommons.org/licenses/by/4.0/>. Nightlock
converted the selected source artwork to PNG, enlarged it with nearest-neighbor
pixel replication, renamed it, and reorganized it into normalized categories.
Neither Liiift Studio, Michael Feeney, nor Apple endorses Nightlock.
