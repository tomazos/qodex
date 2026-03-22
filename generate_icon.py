#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


STANDARD_SIZES = (16, 20, 22, 24, 32, 48, 64, 128, 256, 512)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate a set of standard square PNG icon sizes from a square PNG source. "
            "Each generated file is written as STEM-XxX.png."
        )
    )
    parser.add_argument("input_png", type=Path, help="Path to the source PNG image")
    parser.add_argument(
        "output_stem",
        type=Path,
        help="Output file stem, for example images/brain-icon-",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    input_path = args.input_png
    output_stem = args.output_stem

    if not input_path.is_file():
        raise SystemExit(f"Input file does not exist: {input_path}")

    with Image.open(input_path) as image:
        if image.format != "PNG":
            raise SystemExit(f"Input file is not a PNG: {input_path}")

        width, height = image.size
        if width != height:
            raise SystemExit(
                f"Input image must be square, got {width}x{height}: {input_path}"
            )

        rgba_image = image.convert("RGBA")

    output_parent = output_stem.parent
    if output_parent != Path():
        output_parent.mkdir(parents=True, exist_ok=True)

    generated_paths: list[Path] = []
    for size in STANDARD_SIZES:
        if size >= width:
            continue

        output_path = output_parent / f"{output_stem.name}{size}x{size}.png"
        resized = rgba_image.resize((size, size), Image.Resampling.BICUBIC)
        resized.save(output_path, format="PNG")
        generated_paths.append(output_path)

    if not generated_paths:
        print(f"No output sizes are smaller than source size {width}x{height}.")
        return 0

    print(f"Generated {len(generated_paths)} icon(s) from {input_path}:")
    for path in generated_paths:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
