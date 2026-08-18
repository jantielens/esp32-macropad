#!/usr/bin/env python3
"""Generate palette-RLE RGB565 Nixie sprites for the native extension."""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path

from PIL import Image, ImageFilter


CHARACTERS = ("0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "separator")
SEPARATOR_INDEX = len(CHARACTERS) - 1
X_EDGES = (88, 270, 447, 627, 813)
Y_EDGES = (31, 309, 578, 840)


def load_sprite_generator() -> object:
    script_path = Path(__file__).with_name("generate-nixie-sprites.py")
    spec = importlib.util.spec_from_file_location("nixie_sprites", script_path)
    if not spec or not spec.loader:
        raise RuntimeError(f"Unable to load {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def rgb565(rgb: tuple[int, int, int]) -> int:
    red, green, blue = rgb
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def prepare_sprite(module: object, source: Image.Image, index: int, width: int, height: int, colors: int) -> list[int]:
    row, column = divmod(index, 4)
    crop = source.crop((X_EDGES[column], Y_EDGES[row], X_EDGES[column + 1], Y_EDGES[row + 1]))
    alpha = module.make_glow_alpha(crop, index, 18, 20, 18, 5, 7, 16)
    rgba = module.remove_black_matte(crop, alpha)
    opaque = Image.new("RGB", rgba.size, "black")
    opaque.paste(rgba, mask=rgba.getchannel("A"))
    resized = opaque.resize((width, height), Image.Resampling.LANCZOS)
    reduced = resized.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert("RGB")
    return [rgb565(pixel) for pixel in reduced.getdata()]


def prepare_separator(module: object, source: Image.Image, width: int, height: int, colors: int) -> list[int]:
    """Prepare the solid separator dot with a short, non-ring-like falloff."""
    row, column = divmod(SEPARATOR_INDEX, 4)
    crop = source.crop((X_EDGES[column], Y_EDGES[row], X_EDGES[column + 1], Y_EDGES[row + 1]))
    seed = module.make_separator_seed(crop, 18, 20)
    blurred = seed.filter(ImageFilter.GaussianBlur(11))
    alpha = Image.new("L", crop.size, 0)
    seed_pixels = seed.load()
    blurred_pixels = blurred.load()
    alpha_pixels = alpha.load()
    for y in range(crop.height):
        for x in range(crop.width):
            glow = blurred_pixels[x, y]
            alpha_pixels[x, y] = 255 if seed_pixels[x, y] else (0 if glow < 12 else min(255, (glow - 12) * 8))
    opaque = Image.new("RGB", crop.size, "black")
    opaque.paste(crop, mask=alpha)
    resized = opaque.resize((width, height), Image.Resampling.LANCZOS)
    reduced = resized.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert("RGB")
    return [rgb565(pixel) for pixel in reduced.getdata()]


def rle_encode(pixels: list[int]) -> tuple[list[int], list[int]]:
    palette: list[int] = []
    indexes: list[int] = []
    for pixel in pixels:
        try:
            index = palette.index(pixel)
        except ValueError:
            palette.append(pixel)
            index = len(palette) - 1
        indexes.append(index)

    encoded: list[int] = []
    offset = 0
    while offset < len(indexes):
        end = offset + 1
        while end < len(indexes) and indexes[end] == indexes[offset] and end - offset < 255:
            end += 1
        encoded.extend((end - offset, indexes[offset]))
        offset = end
    return palette, encoded


def write_values(handle: object, values: list[int], width: int, suffix: str, digits: int) -> None:
    for offset in range(0, len(values), width):
        line = ", ".join(f"0x{value:0{digits}X}{suffix}" for value in values[offset:offset + width])
        handle.write(f"    {line},\n")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate compressed RGB565 Nixie assets for the native extension.")
    parser.add_argument("--source", type=Path, default=Path("assets/nixie/nix2.jpg"))
    parser.add_argument("--output", type=Path, default=Path("extensions/nixie-clock/nixie_assets.inc"))
    parser.add_argument("--width", type=int, default=96, help="Canonical digit width in pixels")
    parser.add_argument("--height", type=int, default=147, help="Canonical digit height in pixels")
    parser.add_argument("--colors", type=int, default=16, help="Maximum colours per digit sprite, including black")
    parser.add_argument(
        "--separator-colors",
        type=int,
        default=64,
        help="Maximum colours for the small separator sprite, including black",
    )
    arguments = parser.parse_args()
    if not arguments.source.is_file():
        parser.error(f"Source image does not exist: {arguments.source}")
    if not 8 <= arguments.width <= 255 or not 8 <= arguments.height <= 255:
        parser.error("Width and height must be in the range 8 through 255.")
    if not 2 <= arguments.colors <= 255 or not 2 <= arguments.separator_colors <= 255:
        parser.error("Digit and separator colour counts must be in the range 2 through 255.")

    module = load_sprite_generator()
    source = Image.open(arguments.source).convert("RGB")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    total_bytes = 0
    with arguments.output.open("w", encoding="utf-8") as output:
        output.write("// Generated by tools/generate-nixie-extension-assets.py. Do not edit.\n\n")
        output.write(f"constexpr uint8_t NIXIE_SPRITE_WIDTH = {arguments.width};\n")
        output.write(f"constexpr uint8_t NIXIE_SPRITE_HEIGHT = {arguments.height};\n")
        output.write(f"constexpr uint8_t NIXIE_SPRITE_COUNT = {len(CHARACTERS)};\n\n")
        for index, name in enumerate(CHARACTERS):
            color_count = arguments.separator_colors if index == SEPARATOR_INDEX else arguments.colors
            pixels = (
                prepare_separator(module, source, arguments.width, arguments.height, color_count)
                if index == SEPARATOR_INDEX
                else prepare_sprite(module, source, index, arguments.width, arguments.height, color_count)
            )
            palette, encoded = rle_encode(pixels)
            total_bytes += len(palette) * 2 + len(encoded)
            output.write(f"constexpr uint8_t NIXIE_PALETTE_{index}_SIZE = {len(palette)};\n")
            output.write(f"static constexpr uint16_t NIXIE_PALETTE_{index}[] = {{\n")
            write_values(output, palette, 8, "", 4)
            output.write("};\n")
            output.write(f"static constexpr uint8_t NIXIE_RLE_{index}[] = {{\n")
            write_values(output, encoded, 16, "", 2)
            output.write("};\n\n")
    print(f"Generated {arguments.output} ({total_bytes} bytes of palette-RLE data)")


if __name__ == "__main__":
    main()