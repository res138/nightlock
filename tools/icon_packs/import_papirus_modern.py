#!/usr/bin/env python3
"""Render the curated Papirus Modern pack from a pinned upstream checkout.

The importer reads upstream SVG files as data and invokes only librsvg's
``rsvg-convert``. It never executes Papirus install, build, or maintenance
scripts.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


PINNED_TAG = "20250501"
PINNED_TAG_OBJECT = "1920d4a1b0d7ffcd8b7ffb2f2cf584d877383c5b"
PINNED_COMMIT = "863243822aa6693d4eb3758fd6444690d365c695"
ARCHIVE_SHA256 = "3831a487f813479ad3224fdbfb0c7023f23056899bc78c93737f341aa655558e"
ARCHIVE_SIZE = 32_512_075
METADATA_HASHES = {
    "AUTHORS": "94bd147aa8c7f1dafd4ee060aa4de899bfe3fa422535b0ee2f009a8ea4b4c188",
    "LICENSE": "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986",
    "README.md": "97125b1dac71358931e49629b24280090c8ec70eb2caf23abde59666f3f2e2ab",
}
SOURCE_SET_SHA256 = "71bcbe4d5f40f6f4928fe395192e61ffc4820afc783ffa285ea0ccf814059daf"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


@dataclass(frozen=True)
class Selection:
    category: str
    icon_id: str
    source: str


def selections(category: str, source_dir: str, *icon_ids: str) -> tuple[Selection, ...]:
    return tuple(
        Selection(category, icon_id, f"Papirus/{source_dir}/{icon_id}.svg")
        for icon_id in icon_ids
    )


SELECTIONS = (
    *selections(
        "applications",
        "64x64/apps",
        "accessories-calculator",
        "accessories-character-map",
        "accessories-notes",
        "accessories-text-editor",
        "internet-web-browser",
        "utilities-terminal",
    ),
    *selections(
        "actions",
        "24x24/actions",
        "bookmark-new",
        "document-new",
        "document-open",
        "document-print",
        "document-save",
        "edit-copy",
        "edit-cut",
        "edit-delete",
        "edit-find",
        "edit-paste",
        "edit-redo",
        "edit-undo",
        "list-add",
        "list-remove",
        "media-playback-pause",
        "media-playback-start",
        "media-playback-stop",
        "view-refresh",
    ),
    *selections(
        "folders-places",
        "64x64/places",
        "folder",
        "folder-bookmarks",
        "folder-documents",
        "folder-download",
        "folder-locked",
        "folder-music",
        "folder-open",
        "folder-pictures",
        "folder-publicshare",
        "folder-remote",
        "folder-templates",
        "folder-videos",
        "user-home",
        "user-trash",
    ),
    *selections(
        "devices-volumes",
        "64x64/devices",
        "audio-headphones",
        "audio-input-microphone",
        "audio-speakers",
        "battery",
        "camera-photo",
        "camera-video",
        "camera-web",
        "computer-laptop",
        "computer",
        "drive-harddisk",
        "drive-optical",
        "drive-removable-media",
        "input-keyboard",
        "input-mouse",
        "printer",
    ),
    *selections(
        "documents-mimetypes",
        "64x64/mimetypes",
        "application-json",
        "application-octet-stream",
        "application-pgp-encrypted",
        "application-pgp-keys",
        "application-pkix-cert",
        "application-x-compress",
        "application-x-font-ttf",
        "application-xml",
        "application-yaml",
        "audio-x-generic",
        "image-x-generic",
        "text-x-generic",
        "video-x-generic",
    ),
    *selections(
        "network-sharing",
        "64x64/devices",
        "network-card",
        "network-modem",
        "network-server",
        "network-vpn",
        "network-wired",
        "network-wireless",
    ),
    Selection("users-accounts", "avatar-default", "Papirus/48x48/status/avatar-default.svg"),
    Selection("users-accounts", "system-users", "Papirus/64x64/apps/system-users.svg"),
    *selections(
        "settings-categories",
        "64x64/apps",
        "preferences-desktop-display",
        "preferences-desktop-keyboard",
        "preferences-system-network",
        "preferences-system-power",
        "preferences-system-privacy",
    ),
    *selections(
        "status-menu",
        "48x48/status",
        "battery-full",
        "battery-low",
        "notification-disabled",
    ),
    *selections(
        "alerts-badges",
        "48x48/status",
        "computer-fail",
        "dialog-error",
        "dialog-information",
        "dialog-question",
        "dialog-warning",
        "image-missing",
    ),
    *selections(
        "emblems",
        "48x48/emblems",
        "emblem-default",
        "emblem-encrypted-locked",
        "emblem-favorite",
        "emblem-important",
        "emblem-information",
        "emblem-new",
        "emblem-shared",
        "emblem-urgent",
    ),
)


def fail(message: str) -> "NoReturn":
    raise SystemExit(message)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative_to_root(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError as error:
        fail(f"source leaves the pinned upstream checkout: {path}")
        raise AssertionError from error


def verify_metadata(upstream: Path) -> None:
    for relative, expected in METADATA_HASHES.items():
        path = upstream / relative
        if not path.is_file() or path.is_symlink():
            fail(f"missing ordinary pinned metadata file: {path}")
        actual = digest(path)
        if actual != expected:
            fail(f"metadata checksum mismatch for {relative}: {actual}")

    if (upstream / ".git").exists():
        completed = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=upstream,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        commit = completed.stdout.strip()
        if commit != PINNED_COMMIT:
            fail(f"upstream checkout is {commit}, expected {PINNED_COMMIT}")


def source_records(upstream: Path) -> list[tuple[Selection, Path, str, str]]:
    root = upstream.resolve()
    records: list[tuple[Selection, Path, str, str]] = []
    seen_ids: set[str] = set()
    fingerprint = hashlib.sha256()
    for selection in SELECTIONS:
        if selection.icon_id in seen_ids:
            fail(f"duplicate pack-wide icon id: {selection.icon_id}")
        seen_ids.add(selection.icon_id)
        requested = root / selection.source
        if not requested.exists():
            fail(f"missing pinned SVG source: {requested}")
        resolved = requested.resolve(strict=True)
        resolved_relative = relative_to_root(resolved, root)
        if not resolved.is_file() or resolved.suffix != ".svg":
            fail(f"source is not an SVG file: {resolved}")
        source_sha = digest(resolved)
        line = (
            f"{selection.category}\t{selection.icon_id}\t{selection.source}\t"
            f"{resolved_relative}\t{source_sha}\n"
        )
        fingerprint.update(line.encode("utf-8"))
        records.append((selection, resolved, resolved_relative, source_sha))

    actual_fingerprint = fingerprint.hexdigest()
    if actual_fingerprint != SOURCE_SET_SHA256:
        fail(
            "selected-source fingerprint mismatch: "
            f"{actual_fingerprint} (expected {SOURCE_SET_SHA256})"
        )
    return records


def verify_png(path: Path) -> None:
    payload = path.read_bytes()
    if len(payload) < 33 or not payload.startswith(PNG_SIGNATURE):
        fail(f"librsvg did not produce a valid PNG: {path}")
    width, height, bit_depth, color_type = struct.unpack(">IIBB", payload[16:26])
    if (width, height) != (128, 128):
        fail(f"unexpected PNG dimensions for {path}: {width}x{height}")
    if bit_depth != 8 or color_type not in (4, 6):
        fail(f"PNG is not 8-bit with an alpha channel: {path}")


def render(upstream: Path, output: Path, rsvg_convert: str) -> int:
    verify_metadata(upstream)
    records = source_records(upstream)
    if output.exists():
        fail(f"output already exists; refusing to overwrite it: {output}")
    output.mkdir(parents=True)

    mapping = [
        "category\ticon_id\trequested_source\tresolved_source\tsource_sha256"
    ]
    for selection, source, resolved_relative, source_sha in records:
        destination = output / selection.category / f"{selection.icon_id}.png"
        destination.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                rsvg_convert,
                "--width",
                "128",
                "--height",
                "128",
                "--keep-aspect-ratio",
                "--output",
                str(destination),
                str(source),
            ],
            check=True,
        )
        verify_png(destination)
        mapping.append(
            f"{selection.category}\t{selection.icon_id}\t{selection.source}\t"
            f"{resolved_relative}\t{source_sha}"
        )

    shutil.copyfile(upstream / "LICENSE", output / "LICENSE")
    shutil.copyfile(upstream / "AUTHORS", output / "AUTHORS")
    (output / "SOURCE_MAPPING.tsv").write_text(
        "\n".join(mapping) + "\n", encoding="utf-8"
    )
    return len(records)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("upstream", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--rsvg-convert",
        default=shutil.which("rsvg-convert"),
        help="path to the librsvg renderer",
    )
    args = parser.parse_args()
    if not args.rsvg_convert:
        fail("rsvg-convert was not found")
    count = render(args.upstream.resolve(), args.output.resolve(), args.rsvg_convert)
    print(f"rendered {count} Papirus Modern icons into {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
