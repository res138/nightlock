#!/usr/bin/env python3
"""Validate pack metadata and sync catalog icon counts and payload sizes."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path


IDENTITY_FIELDS = (
    "id",
    "title",
    "description",
    "version",
    "author",
    "license",
    "platforms",
)
PACK_ID = re.compile(r"^[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?$")
SEMVER = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][A-Za-z0-9.-]+)?$")


def load_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise SystemExit(f"expected a JSON object: {path}")
    return value


def synchronized_catalog(root: Path) -> tuple[dict[str, object], int]:
    root = root.resolve()
    catalog_path = root / "catalog.json"
    catalog = load_json(catalog_path)
    entries = catalog.get("packs")
    if catalog.get("schemaVersion") != 1 or not isinstance(entries, list):
        raise SystemExit("catalog must use schemaVersion 1 and contain packs")

    seen_ids: set[str] = set()
    total_icons = 0
    for entry in entries:
        if not isinstance(entry, dict):
            raise SystemExit("catalog pack entry is not an object")
        manifest_reference = entry.get("manifest")
        if not isinstance(manifest_reference, str) or not manifest_reference:
            raise SystemExit("catalog pack is missing manifest")
        manifest_path = (root / manifest_reference).resolve()
        try:
            manifest_path.relative_to(root)
        except ValueError as error:
            raise SystemExit(f"manifest leaves icon-packs/: {manifest_reference}") from error
        manifest = load_json(manifest_path)
        for field in IDENTITY_FIELDS:
            if entry.get(field) != manifest.get(field):
                raise SystemExit(
                    f"{entry.get('id', '<unknown>')}: catalog/manifest mismatch in {field}"
                )

        pack_id = entry["id"]
        if (not isinstance(pack_id, str) or not PACK_ID.fullmatch(pack_id)
                or pack_id in seen_ids):
            raise SystemExit(f"duplicate or invalid pack id: {pack_id}")
        seen_ids.add(pack_id)
        version = entry.get("version")
        if not isinstance(version, str) or not SEMVER.fullmatch(version):
            raise SystemExit(f"{pack_id}: version is not semver-compatible: {version}")

        categories = manifest.get("categories")
        if not isinstance(categories, list) or not categories:
            raise SystemExit(f"{pack_id}: manifest has no categories")
        icon_count = 0
        payload_bytes = 0
        for category in categories:
            if not isinstance(category, dict) or not isinstance(category.get("icons"), list):
                raise SystemExit(f"{pack_id}: invalid category")
            for icon in category["icons"]:
                if not isinstance(icon, dict):
                    raise SystemExit(f"{pack_id}: invalid icon entry")
                size = icon.get("size")
                if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
                    raise SystemExit(f"{pack_id}: invalid icon size")
                icon_count += 1
                payload_bytes += size
        entry["iconCount"] = icon_count
        entry["payloadBytes"] = payload_bytes
        total_icons += icon_count
    return catalog, total_icons


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", type=Path, default=Path("icon-packs"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    catalog_path = args.root / "catalog.json"
    catalog, total_icons = synchronized_catalog(args.root)
    rendered = json.dumps(catalog, ensure_ascii=False, indent=2) + "\n"
    current = catalog_path.read_text(encoding="utf-8")
    if args.check:
        if rendered != current:
            raise SystemExit("catalog counts are stale; run sync_catalog.py")
    elif rendered != current:
        temporary = catalog_path.with_suffix(".json.tmp")
        temporary.write_text(rendered, encoding="utf-8")
        os.replace(temporary, catalog_path)
    print(f"validated {len(catalog['packs'])} packs / {total_icons} icons")
    return 0


if __name__ == "__main__":
    sys.exit(main())
