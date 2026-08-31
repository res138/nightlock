# La Capitaine provenance

- Upstream project: La Capitaine icon theme
- Upstream repository:
  <https://github.com/keeferrourke/la-capitaine-icon-theme>
- Pinned upstream tag: `v0.6.2`
- Resolved commit:
  `9c514f1281d895b44a5ac92320dfc36f21aa5984`
- Official source archive:
  <https://github.com/keeferrourke/la-capitaine-icon-theme/archive/refs/tags/v0.6.2.tar.gz>
- Downloaded archive SHA-256:
  `e63e4bc97a7bbf4db71f17fa3b1c71086e3ed8b1dc44b4d10f95d97aa7db2c9d`
- Imported: 2026-08-31
- Artwork license: `GPL-3.0-or-later`

The upstream `COPYING`, `LICENSE`, and `Credits.md` files are preserved
beside this record as `COPYING`, `LICENSE`, and `CREDITS.md`. The pinned
source archive contains the preferred-form SVG sources for every rendered PNG
listed below.

## Selection policy

This Nightlock edition contains 72 generic desktop icons. The complete
upstream `apps` directory was excluded. The selection also excludes Apple and
Finder artwork, distribution logos, branded application icons, and branded
cloud/service folder variants. Only regular SVG files from the pinned archive
were accepted; upstream symlinks were not imported.

## Rendering

The upstream SVGs were rendered on 2026-08-31 with ImageMagick `7.1.2-28`
and its librsvg `2.62.3` delegate. The delegate rasterized each source on a
transparent 1024 x 1024 canvas with aspect ratio preserved. ImageMagick then
resized it to a centered 256 x 256 transparent canvas, removed ancillary
metadata, and wrote an 8-bit RGBA PNG:

```sh
MAGICK_CONFIGURE_PATH=<rsvg-delegate-config> magick \
  -background none source.svg \
  -resize 256x256 -gravity center -extent 256x256 \
  -strip PNG32:destination.png
```

The ImageMagick SVG delegate command used for this import was:

```text
rsvg-convert --width 1024 --height 1024 --keep-aspect-ratio SOURCE -o OUTPUT
```

No script from the upstream archive was executed.

## Curated source mapping

| Nightlock category | Upstream directory | Files |
|---|---|---:|
| `actions` | `actions/22x22-light` | 10 |
| `folders-places` | `places/scalable` | 16 |
| `devices-volumes` | `devices/scalable-light` | 18 |
| `documents-mimetypes` | `mimetypes/scalable` | 16 |
| `alerts-badges` | `status/scalable-light` | 5 |
| `emblems` | `emblems/scalable` | 7 |

### `actions/22x22-light`

`document-new.svg`, `document-open.svg`, `document-save.svg`,
`document-print.svg`, `document-export.svg`, `edit-cut.svg`,
`edit-delete.svg`, `edit-paste.svg`, `edit-redo.svg`, and
`edit-undo.svg`.

### `places/scalable`

`folder.svg`, `folder-bookmarks.svg`, `folder-camera.svg`,
`folder-design.svg`, `folder-development.svg`, `folder-documents.svg`,
`folder-download.svg`, `folder-favorites.svg`, `folder-games.svg`,
`folder-mail.svg`, `folder-music.svg`, `folder-pictures.svg`,
`folder-private.svg`, `folder-public.svg`, `folder-templates.svg`, and
`folder-videos.svg`.

### `devices/scalable-light`

`audio-headset.svg`, `audio-input-microphone.svg`, `audio-speakers.svg`,
`camera-photo.svg`, `camera-web.svg`, `computer-laptop.svg`,
`computer.svg`, `drive-harddisk.svg`, `drive-removable-media.svg`,
`input-gaming.svg`, `input-keyboard.svg`, `input-mouse.svg`,
`media-flash-memory-stick.svg`, `media-optical.svg`, `printer.svg`,
`scanner.svg`, `smartphone.svg`, and `video-display.svg`.

### `mimetypes/scalable`

`application-archive.svg`, `application-audio.svg`,
`application-blank.svg`, `application-certificate.svg`,
`application-database.svg`, `application-document.svg`,
`application-drawing.svg`, `application-executable.svg`,
`application-font.svg`, `application-images.svg`,
`application-presentation.svg`, `application-table.svg`,
`application-text.svg`, `application-json.svg`,
`application-video.svg`, and `text-html.svg`.

### `status/scalable-light`

`dialog-error.svg`, `dialog-information.svg`, `dialog-password.svg`,
`dialog-question.svg`, and `dialog-warning.svg`.

### `emblems/scalable`

`emblem-checked.svg`, `emblem-error.svg`, `emblem-important.svg`,
`emblem-information.svg`, `emblem-mounted.svg`, `emblem-unlocked.svg`,
and `emblem-warning.svg`.
