# WhiteSur Modern provenance

- Upstream project: WhiteSur icon theme
- Upstream repository:
  <https://github.com/vinceliuice/WhiteSur-icon-theme>
- Pinned release and tag: `2026-08-11`
- Release publication timestamp: `2026-08-10T17:31:07Z`
- Resolved commit:
  `555a4505920475482f62afd02366441a53669c30`
- Official source archive:
  <https://github.com/vinceliuice/WhiteSur-icon-theme/archive/refs/tags/2026-08-11.tar.gz>
- Downloaded archive SHA-256:
  `70c1b1b89854655ba054793c1a1e7359842e6f1c3f5320920f715af63bcd75b1`
- Imported: 2026-08-31
- Artwork license: `GPL-3.0-only`

Upstream places the GNU GPL version 3 text in `COPYING` and identifies Vince
Liuice in `AUTHORS`. Both files are preserved byte-for-byte. Because upstream
does not provide separate `LICENSE` or `CREDITS.md` files, Nightlock's
`LICENSE` is a byte-for-byte copy of upstream `COPYING`, and
`CREDITS.md` is a byte-for-byte copy of upstream `AUTHORS`. The pinned
source archive contains the preferred-form SVG sources for every rendered PNG
listed below.

## Selection policy

This Nightlock edition contains 78 generic desktop icons. The complete upstream
`apps` directory was excluded, as were Apple, Finder, distribution-logo,
branded application, and branded service variants.

Visual review also rejected apparently generic upstream files that contain
recognizable marks: `src/devices/scalable/computer.svg` and
`src/devices/scalable/input-mouse.svg` contain Apple logos,
`src/mimes/scalable/video-x-generic.svg` depicts the QuickTime mark, and
`src/devices/scalable/cpu.svg` contains the Intel mark. None of those files is
present in this pack.

Only regular SVG files from the pinned archive were accepted; upstream symlinks
were not imported. Selected source files were additionally scanned for common
brand names before rendering.

## Rendering

The upstream SVGs were rendered on 2026-08-31 with ImageMagick `7.1.2-28`
and its librsvg `2.62.3` delegate. The delegate rasterized each source on a
transparent 1024 × 1024 canvas with aspect ratio preserved. ImageMagick then
resized it to a centered 256 × 256 transparent canvas, removed ancillary
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
| `actions` | `src/actions/32` | 12 |
| `folders-places` | `src/places/scalable` | 16 |
| `devices-volumes` | `src/devices/scalable` | 16 |
| `documents-mimetypes` | `src/mimes/scalable` | 14 |
| `settings-categories` | `src/preferences/32` | 8 |
| `alerts-badges` | `src/status/32` | 5 |
| `emblems` | `src/emblems/24` | 7 |

### `src/actions/32`

`document-new.svg`, `document-open.svg`, `document-save.svg`,
`document-print.svg`, `document-export.svg`, `edit-delete.svg`,
`edit-redo.svg`, `edit-undo.svg`, `folder-new.svg`,
`system-lock-screen.svg`, `view-refresh.svg`, and `zoom-in.svg`.

### `src/places/scalable`

`folder.svg`, `folder-bookmark.svg`, `folder-cloud.svg`,
`folder-code.svg`, `folder-design.svg`, `folder-development.svg`,
`folder-documents.svg`, `folder-download.svg`, `folder-games.svg`,
`folder-images.svg`, `folder-locked.svg`, `folder-mail.svg`,
`folder-music.svg`, `folder-public.svg`, `folder-templates.svg`, and
`folder-videos.svg`.

### `src/devices/scalable`

`audio-headphones.svg`, `audio-input-microphone.svg`,
`audio-speakers.svg`, `camera-photo.svg`, `camera-video.svg`,
`camera-web.svg`, `computer-laptop.svg`, `drive-harddisk.svg`,
`drive-removable-media.svg`, `input-gaming.svg`, `input-keyboard.svg`,
`input-touchpad.svg`, `media-optical.svg`, `memory.svg`,
`printer.svg`, and `scanner.svg`.

### `src/mimes/scalable`

`application-audio.svg`, `application-blank.svg`,
`application-certificate.svg`, `application-document.svg`,
`application-json.svg`, `application-software.svg`,
`application-sql.svg`, `application-toml.svg`,
`application-vector.svg`, `application-x-archive.svg`,
`image-x-generic.svg`, `text-html.svg`, `text-markdown.svg`, and
`text-x-generic.svg`.

### `src/preferences/32`

`preferences-desktop-accessibility.svg`,
`preferences-desktop-color.svg`, `preferences-desktop-display.svg`,
`preferences-desktop-keyboard.svg`, `preferences-desktop-sound.svg`,
`preferences-desktop-wallpaper.svg`, `preferences-system-network.svg`,
and `preferences-system-power.svg`.

### `src/status/32`

`dialog-error.svg`, `dialog-information.svg`, `dialog-question.svg`,
`dialog-warning.svg`, and `security-high.svg`.

### `src/emblems/24`

`emblem-added.svg`, `emblem-checked.svg`, `emblem-information.svg`,
`emblem-mounted.svg`, `emblem-pause.svg`, `emblem-unlocked.svg`, and
`emblem-warning.svg`.
