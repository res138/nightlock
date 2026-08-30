#!/usr/bin/env python3
"""Regenerate Nightlock's multi-resolution Windows ICO assets.

ImageMagick does the high-quality vector/raster resampling; this script writes
the ICO directory itself so every frame remains a modern 32-bit PNG and the
set of Windows 11 fractional-DPI sizes is deterministic.
"""

from __future__ import annotations

import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


ICON_SIZES = (16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 128, 256)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def render_frames(source: Path, temporary: Path) -> list[bytes]:
    frames: list[bytes] = []
    for size in ICON_SIZES:
        destination = temporary / f"{size}.png"
        subprocess.run(
            [
                "magick",
                "-background",
                "none",
                str(source),
                "-filter",
                "Lanczos",
                "-resize",
                f"{size}x{size}!",
                "-depth",
                "8",
                # Drop source timestamps/profiles so two runs produce the
                # exact same committed ICO bytes.
                "-strip",
                f"PNG32:{destination}",
            ],
            check=True,
        )
        payload = destination.read_bytes()
        if not payload.startswith(PNG_SIGNATURE):
            raise RuntimeError(f"ImageMagick did not create a PNG: {destination}")
        width, height = struct.unpack(">II", payload[16:24])
        if (width, height) != (size, size):
            raise RuntimeError(
                f"Expected {size}x{size}, got {width}x{height}: {destination}"
            )
        frames.append(payload)
    return frames


def write_ico(destination: Path, frames: list[bytes]) -> None:
    header_size = 6 + 16 * len(frames)
    offset = header_size
    directory = bytearray(struct.pack("<HHH", 0, 1, len(frames)))
    for size, payload in zip(ICON_SIZES, frames, strict=True):
        encoded_size = 0 if size == 256 else size
        directory.extend(
            struct.pack(
                "<BBBBHHII",
                encoded_size,
                encoded_size,
                0,
                0,
                1,
                32,
                len(payload),
                offset,
            )
        )
        offset += len(payload)

    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_bytes(bytes(directory) + b"".join(frames))
    temporary.replace(destination)


def main() -> None:
    if shutil.which("magick") is None:
        raise SystemExit("ImageMagick 7 ('magick') is required")

    repository = Path(__file__).resolve().parents[2]
    resources = repository / "apps" / "desktop" / "resources"
    icons = resources / "icons"
    assets = (
        # ImageMagick does not faithfully implement every SVG mask/filter used
        # by the petal artwork. Its approved 1024px RGBA render is therefore
        # the canonical Windows master as well as a useful visual oracle.
        (icons / "appicon-petal-keyhole.png", resources / "nightlock.ico"),
        (icons / "appicon-petal-keyhole.png", icons / "appicon-petal-keyhole.ico"),
        (icons / "appicon-flower.png", icons / "appicon-flower.ico"),
    )

    with tempfile.TemporaryDirectory(prefix="nightlock-windows-icons-") as raw:
        temporary_root = Path(raw)
        rendered: dict[Path, list[bytes]] = {}
        for source, destination in assets:
            frames = rendered.get(source)
            if frames is None:
                source_temporary = temporary_root / source.stem
                source_temporary.mkdir()
                frames = render_frames(source, source_temporary)
                rendered[source] = frames
            write_ico(destination, frames)
            print(f"wrote {destination.relative_to(repository)}")


if __name__ == "__main__":
    main()
