# Nightlock icon-pack catalog

Nightlock ships only the built-in `nightlock-default` pack. Optional packs are
published by adding an entry to `catalog.json` and a versioned manifest below
this directory. Every published collection is provenance-reviewed, PNG-only,
and excluded from the application installers. Desktop clients read the catalog
from:

`https://raw.githubusercontent.com/res138/nightlock/main/icon-packs/catalog.json`

The legacy `P1`–`P7` artwork is not a downloadable pack until every image has
documented provenance and redistribution terms in `THIRD_PARTY_NOTICES.md`.

## Catalog entry

```json
{
  "id": "example-pack",
  "title": "Example Pack",
  "description": "A concise user-facing description.",
  "version": "1.0.0",
  "author": "Example Author",
  "license": "MIT",
  "platforms": ["linux", "macos"],
  "iconCount": 42,
  "payloadBytes": 123456,
  "manifest": "packs/example-pack/manifest.json"
}
```

`manifest` is a safe relative `.json` path contained by `icon-packs/`. Valid
platform values are `linux`, `macos`, `windows`, or the single value
`cross-platform`. `iconCount` and `payloadBytes` let the library show useful
size information before downloading the manifest; run
`python3 tools/icon_packs/sync_catalog.py` after changing a pack.

## Pack manifest

```json
{
  "schemaVersion": 1,
  "id": "example-pack",
  "title": "Example Pack",
  "description": "A concise user-facing description.",
  "version": "1.0.0",
  "author": "Example Author",
  "license": "SPDX-License-Identifier",
  "platforms": ["linux", "macos"],
  "categories": [
    {
      "id": "applications",
      "icons": [
        {
          "id": "example-app",
          "title": "Example App",
          "file": "applications/example-app.png",
          "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
          "size": 12345
        }
      ]
    }
  ]
}
```

Pack identity, version, author, license, and platform metadata must match the
catalog entry. `file` is both the safe relative installation path and, by
default, the URL relative to the manifest. An icon may instead provide one of
`source` or `url`; production clients accept only HTTPS content from the
official `res138/nightlock` repository.

Use an SPDX identifier when the upstream license has one. For material such as
Tango that upstream explicitly releases into the public domain without an SPDX
license, use a stable reviewed label and retain the exact upstream statement in
the pack's provenance files.

Every icon must:

- be a real, decodable PNG with the `.png` suffix;
- declare its exact byte size and SHA-256 digest;
- use unique lowercase, dash-separated IDs;
- use lowercase ASCII file paths that are portable to Windows, macOS, and
  Linux (reserved Windows device names and case-folding collisions are
  rejected);
- stay within the documented file, image-dimension, and pack-size limits;
- have author, license, source, and trademark provenance reviewed before the
  catalog entry is merged.

Non-icon files may be used for a manifest and provenance record, but every file
listed as an icon in a manifest must be a PNG. ICO, SVG, ICNS, and other image
formats are not accepted by the client.

Pack IDs and icon IDs are persistence keys. Once published, they must remain
stable across pack releases. Nightlock stores a selection in a vault as
`nightlock-icon://<pack-id>/<category-id>/<icon-id>` rather than as a local
application-data path. Moving an icon between normalized categories remains
resolvable, but changing its pack or icon ID breaks that contract.

## Normalized categories

Linux, macOS, Windows, and cross-platform packs share the same category IDs:

- `applications`
- `system-applications`
- `actions`
- `folders-places`
- `devices-volumes`
- `documents-mimetypes`
- `network-sharing`
- `users-accounts`
- `settings-categories`
- `status-menu`
- `sidebar-toolbar`
- `alerts-badges`
- `emblems`
- `animations`
- `legacy-special`
- `ui-symbols`

Clients reject unknown categories instead of silently creating incompatible
navigation. Display names are localized by Nightlock; an optional manifest
category title is metadata only.

## Client limits

- catalog: 1 MiB and at most 256 packs;
- manifest: 2 MiB, at most 16 categories, and at most 4,096 icons;
- one PNG: 16 MiB, at most 4,096 × 4,096 pixels and 16 megapixels;
- one installed pack: 512 MiB of declared PNG payload.

The desktop downloads a bounded number of icons concurrently, verifies every
payload before writing it into a private staging directory, and activates the
pack only after the complete manifest succeeds. Downloaded packs are stored in
local application data so large collections do not enter Windows roaming
profiles.

Developer builds started with `NIGHTLOCK_DEMO=1` expose the current source-tree
packs as read-only previews. This makes every curated pack immediately visible
in the picker without embedding it into `Nightlock.app`; normal builds retain
the Download/Remove flow backed by the official GitHub catalog.
