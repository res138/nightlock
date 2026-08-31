#!/usr/bin/env python3
"""Rebuild a Nightlock PNG-only icon-pack manifest deterministically."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path


CATEGORY_ORDER = (
    "applications",
    "system-applications",
    "actions",
    "folders-places",
    "devices-volumes",
    "documents-mimetypes",
    "network-sharing",
    "users-accounts",
    "settings-categories",
    "status-menu",
    "sidebar-toolbar",
    "alerts-badges",
    "emblems",
    "animations",
    "legacy-special",
    "ui-symbols",
)
PORTABLE_ID = re.compile(r"^[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?$")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def display_title(icon_id: str) -> str:
    words = icon_id.replace("-", " ").split()
    acronyms = {"cd", "dvd", "html", "ip", "pdf", "rss", "usb"}
    return " ".join(word.upper() if word in acronyms else word.capitalize()
                    for word in words)


def fail(message: str) -> "NoReturn":
    raise SystemExit(message)


def rebuild(pack_dir: Path) -> int:
    pack_dir = pack_dir.resolve()
    manifest_path = pack_dir / "manifest.json"
    if not manifest_path.is_file() or manifest_path.is_symlink():
        fail(f"missing ordinary manifest: {manifest_path}")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    required_metadata = (
        "schemaVersion", "id", "title", "description", "version",
        "author", "license", "platforms",
    )
    for field in required_metadata:
        if field not in manifest:
            fail(f"manifest is missing {field}")

    known_categories = set(CATEGORY_ORDER)
    present_directories = {
        child.name for child in pack_dir.iterdir()
        if child.is_dir() and not child.is_symlink()
    }
    unknown = sorted(present_directories - known_categories)
    if unknown:
        fail(f"unknown category directories: {', '.join(unknown)}")

    categories: list[dict[str, object]] = []
    seen_ids: set[str] = set()
    icon_count = 0
    for category_id in CATEGORY_ORDER:
        category_dir = pack_dir / category_id
        if not category_dir.is_dir():
            continue
        icons: list[dict[str, object]] = []
        for path in sorted(category_dir.iterdir(), key=lambda item: item.name):
            if path.is_symlink() or not path.is_file():
                fail(f"category contains a non-ordinary file: {path}")
            if path.suffix != ".png":
                fail(f"runtime icon is not PNG: {path}")
            icon_id = path.stem
            if not PORTABLE_ID.fullmatch(icon_id):
                fail(f"invalid portable icon id: {icon_id}")
            if icon_id in seen_ids:
                fail(f"duplicate pack-wide icon id: {icon_id}")
            seen_ids.add(icon_id)
            payload = path.read_bytes()
            if not payload.startswith(PNG_SIGNATURE):
                fail(f"invalid PNG signature: {path}")
            relative = path.relative_to(pack_dir).as_posix()
            icons.append({
                "id": icon_id,
                "title": display_title(icon_id),
                "file": relative,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload),
            })
            icon_count += 1
        if icons:
            categories.append({"id": category_id, "icons": icons})

    if not categories:
        fail("pack has no PNG categories")
    manifest["categories"] = categories
    temporary = manifest_path.with_suffix(".json.tmp")
    temporary.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, manifest_path)
    return icon_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pack_dir", type=Path)
    args = parser.parse_args()
    count = rebuild(args.pack_dir)
    print(f"rebuilt {args.pack_dir}/manifest.json with {count} PNG icons")
    return 0


if __name__ == "__main__":
    sys.exit(main())
