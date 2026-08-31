#!/usr/bin/env python3
"""Render Nightlock's Platinum Community pack from a pinned upstream checkout.

This importer reads the upstream TSX artwork as data. It does not install
dependencies or execute any script from the upstream repository.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


PINNED_COMMIT = "69ede5d5a739428a078f47ffcd0a32067121847a"
CANVAS_SIZE = 256
SOURCE_HASHES = {
    "package.json": "eb7b2b7c828715eafe91682dfde53b07833d8d58969af78fa3ca8e1bc7f57686",
    "LICENSE": "1ee00e29c67f9cae4cd94e37d00eae341cc70df05d02c783d450f058229448f9",
    "ATTRIBUTION.md": "f69a84b1723cb9c5d4b67ff5f0e87a8c063bb05ee3be41fb8d722af813c58ab0",
    "src/components/Icon/pixel.tsx":
        "71193f941cfd3f1e7d516f4c2a95b5743fa9559238ed4bd44e9ef2b46ea04e46",
    "src/components/Icon/registry.ts":
        "1c67700294019f924af3fa304aacf8dfe29e3b8a1664bd51647cbbb39b4aeaaf",
    "src/components/Icon/categories/actions.tsx":
        "e505d63b5ffd2d901d5fb15c797c1b5930452614db8b9f64705eae54961f4578",
    "src/components/Icon/categories/files.tsx":
        "1b940549c7fc3ead2ae362a5ebc091d2ba4b6d03ff5d0fed2e1328769d458523",
    "src/components/Icon/categories/navigation.tsx":
        "3a7ddaa5003cc8f473be29e33b2f61adceab37922ca1e1ab7217b298521210ee",
    "src/components/Icon/categories/media.tsx":
        "94391f3408bfc1841a4ce2dc8aaea597b11d52fa333dbeefc989f872bd829454",
    "src/components/Icon/categories/status.tsx":
        "12337d6ab3f84e29fb29fdb6ecd58643acdf7be25d5b83a7643295d28382786c",
    "src/components/Icon/categories/ui.tsx":
        "5593fdbf4504ea868f4c095398bac5eba89ed58bb044af6d7fa360ca09a2de24",
    "src/styles/tokens.css":
        "b2bdc3da9f0cb7a3968f9058dd69c1fe10ca16685700a100f2785755d2991aaa",
}


@dataclass(frozen=True)
class Selection:
    export_name: str
    category: str
    icon_id: str


SELECTIONS = (
    Selection("CalendarIcon", "applications", "calendar"),
    Selection("CloseIcon", "actions", "close"),
    Selection("TrashIcon", "actions", "trash"),
    Selection("SearchIcon", "actions", "search"),
    Selection("CopyIcon", "actions", "copy"),
    Selection("PrintIcon", "actions", "print"),
    Selection("DownloadIcon", "actions", "download"),
    Selection("LinkIcon", "actions", "link"),
    Selection("MailIcon", "actions", "mail"),
    Selection("PlayIcon", "actions", "play"),
    Selection("PauseIcon", "actions", "pause"),
    Selection("StopIcon", "actions", "stop"),
    Selection("FolderIcon", "folders-places", "folder"),
    Selection("FolderOpenIcon", "folders-places", "folder-open"),
    Selection("HomeIcon", "folders-places", "home"),
    Selection("DiskIcon", "devices-volumes", "floppy-disk"),
    Selection("HardDriveIcon", "devices-volumes", "hard-drive"),
    Selection("DocumentIcon", "documents-mimetypes", "document"),
    Selection("ApplicationIcon", "documents-mimetypes", "application"),
    Selection("ImageIcon", "documents-mimetypes", "image"),
    Selection("MusicIcon", "documents-mimetypes", "music"),
    Selection("UserIcon", "users-accounts", "user"),
    Selection("VolumeIcon", "status-menu", "volume"),
    Selection("VolumeMuteIcon", "status-menu", "volume-muted"),
    Selection("AlertIcon", "alerts-badges", "warning"),
    Selection("InfoIcon", "alerts-badges", "information"),
    Selection("ErrorIcon", "alerts-badges", "error"),
    Selection("QuestionIcon", "alerts-badges", "question"),
    Selection("CheckIcon", "emblems", "checked"),
    Selection("LockIcon", "emblems", "locked"),
    Selection("ArrowUpIcon", "ui-symbols", "arrow-up"),
    Selection("ArrowDownIcon", "ui-symbols", "arrow-down"),
    Selection("ArrowLeftIcon", "ui-symbols", "arrow-left"),
    Selection("ArrowRightIcon", "ui-symbols", "arrow-right"),
    Selection("ChevronRightIcon", "ui-symbols", "chevron-right"),
    Selection("ChevronDownIcon", "ui-symbols", "chevron-down"),
    Selection("DividerIcon", "ui-symbols", "divider"),
    Selection("ResizeHandleIcon", "ui-symbols", "resize-handle"),
    Selection("GrabberIcon", "ui-symbols", "grabber"),
)

PIXEL_ICON_RE = re.compile(
    r"export const (?P<export>[A-Za-z0-9_]+) = createPixelIcon\("
    r"\s*'[^']*',\s*'[^']*',\s*\[(?P<rows>.*?)\]"
    r"\s*(?:,\s*(?P<grid>[0-9]+))?\s*\);",
    re.DOTALL,
)
ROW_RE = re.compile(r"'([^']*)'")
SVG_PATH_RE = re.compile(r'<path d="([^"]+)" fill="([^"]+)"\s*/>')
RECT_PATH_RE = re.compile(
    r"M(?P<x1>[0-9]+) (?P<y1>[0-9]+)H(?P<x2>[0-9]+)"
    r"V(?P<y2>[0-9]+)H(?P<x3>[0-9]+)V(?P<y3>[0-9]+)Z"
)
PIXEL_COLORS = {
    "#": (0x26, 0x26, 0x26, 0xFF),
    "o": (0xFF, 0xFF, 0xFF, 0xFF),
    "x": (0x99, 0x99, 0x99, 0xFF),
}
SVG_COLORS = {
    "white": (0xFF, 0xFF, 0xFF, 0xFF),
    "#999999": (0x99, 0x99, 0x99, 0xFF),
    "#BBBBBB": (0xBB, 0xBB, 0xBB, 0xFF),
}


def fail(message: str) -> "NoReturn":
    raise SystemExit(message)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_sources(upstream: Path) -> None:
    for relative, expected in SOURCE_HASHES.items():
        path = upstream / relative
        if not path.is_file() or path.is_symlink():
            fail(f"missing ordinary pinned source: {path}")
        actual = digest(path)
        if actual != expected:
            fail(f"source checksum mismatch for {relative}: {actual}")

    git_dir = upstream / ".git"
    if git_dir.exists():
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


def parse_pixel_maps(upstream: Path) -> dict[str, tuple[list[str], int]]:
    found: dict[str, tuple[list[str], int]] = {}
    categories = upstream / "src/components/Icon/categories"
    for path in sorted(categories.glob("*.tsx")):
        text = path.read_text(encoding="utf-8")
        for match in PIXEL_ICON_RE.finditer(text):
            rows = ROW_RE.findall(match.group("rows"))
            grid = int(match.group("grid") or 16)
            if len(rows) != grid or any(len(row) != grid for row in rows):
                fail(f"non-square pixel map in {path}: {match.group('export')}")
            unknown = set("".join(rows)) - {".", " ", *PIXEL_COLORS}
            if unknown:
                fail(f"unknown pixel-map characters in {match.group('export')}: {unknown}")
            found[match.group("export")] = (rows, grid)
    return found


def transparent_canvas() -> bytearray:
    return bytearray(CANVAS_SIZE * CANVAS_SIZE * 4)


def fill_rect(
    pixels: bytearray,
    left: int,
    top: int,
    right: int,
    bottom: int,
    color: tuple[int, int, int, int],
) -> None:
    if not (0 <= left <= right <= CANVAS_SIZE and
            0 <= top <= bottom <= CANVAS_SIZE):
        fail(f"rectangle is outside the {CANVAS_SIZE}px canvas")
    row = bytes(color) * (right - left)
    for y in range(top, bottom):
        offset = (y * CANVAS_SIZE + left) * 4
        pixels[offset:offset + len(row)] = row


def render_pixel_map(rows: list[str], grid: int) -> bytearray:
    if CANVAS_SIZE % grid != 0:
        fail(f"pixel grid {grid} does not divide canvas {CANVAS_SIZE}")
    scale = CANVAS_SIZE // grid
    pixels = transparent_canvas()
    for y, row in enumerate(rows):
        for x, symbol in enumerate(row):
            color = PIXEL_COLORS.get(symbol)
            if color:
                fill_rect(
                    pixels,
                    x * scale,
                    y * scale,
                    (x + 1) * scale,
                    (y + 1) * scale,
                    color,
                )
    return pixels


def render_divider(upstream: Path) -> bytearray:
    text = (upstream / "src/components/Icon/categories/ui.tsx").read_text(
        encoding="utf-8"
    )
    paths = SVG_PATH_RE.findall(text)
    if len(paths) != 24:
        fail(f"expected 24 DividerIcon SVG paths, found {len(paths)}")

    # DividerIcon has a 10x32 viewBox. Eight device pixels per source unit
    # preserves its aspect ratio on a centered 256x256 transparent canvas.
    source_width = 10
    source_height = 32
    scale = CANVAS_SIZE // source_height
    x_offset = (CANVAS_SIZE - source_width * scale) // 2
    pixels = transparent_canvas()
    for path_data, fill in paths:
        match = RECT_PATH_RE.fullmatch(path_data)
        color = SVG_COLORS.get(fill)
        if not match or not color:
            fail(f"unsupported DividerIcon path: {path_data} ({fill})")
        xs = [int(match.group(name)) for name in ("x1", "x2", "x3")]
        ys = [int(match.group(name)) for name in ("y1", "y2", "y3")]
        fill_rect(
            pixels,
            x_offset + min(xs) * scale,
            min(ys) * scale,
            x_offset + max(xs) * scale,
            max(ys) * scale,
            color,
        )
    return pixels


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_png(path: Path, pixels: bytearray) -> None:
    stride = CANVAS_SIZE * 4
    scanlines = b"".join(
        b"\x00" + pixels[offset:offset + stride]
        for offset in range(0, len(pixels), stride)
    )
    ihdr = struct.pack(">IIBBBBB", CANVAS_SIZE, CANVAS_SIZE, 8, 6, 0, 0, 0)
    payload = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(scanlines, level=9))
        + png_chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def import_pack(upstream: Path, pack: Path) -> None:
    upstream = upstream.resolve()
    pack = pack.resolve()
    verify_sources(upstream)
    pixel_maps = parse_pixel_maps(upstream)

    for selection in SELECTIONS:
        destination = pack / selection.category / f"{selection.icon_id}.png"
        if selection.export_name == "DividerIcon":
            pixels = render_divider(upstream)
        else:
            source = pixel_maps.get(selection.export_name)
            if not source:
                fail(f"missing pixel icon export: {selection.export_name}")
            pixels = render_pixel_map(*source)
        write_png(destination, pixels)

    shutil.copyfile(upstream / "LICENSE", pack / "LICENSE")
    shutil.copyfile(upstream / "ATTRIBUTION.md", pack / "ATTRIBUTION.md")
    print(
        f"rendered {len(SELECTIONS)} pinned upstream icons into {pack}",
        file=sys.stdout,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("upstream", type=Path)
    parser.add_argument("pack_dir", type=Path)
    args = parser.parse_args()
    import_pack(args.upstream, args.pack_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
