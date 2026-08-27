#!/usr/bin/env python3
"""Slice a Nixie digit atlas and generate isolated RGBA sprite PNGs."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter


CHARACTERS = ("0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "separator", "empty")
DEFAULT_X_EDGES = (88, 270, 447, 627, 813)
DEFAULT_Y_EDGES = (31, 309, 578, 840)


def parse_edges(value: str, expected_count: int) -> tuple[int, ...]:
    """Parse a comma-separated sequence of strictly increasing pixel edges."""
    try:
        edges = tuple(int(item.strip()) for item in value.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("Edges must be comma-separated integers.") from error

    if len(edges) != expected_count or any(right <= left for left, right in zip(edges, edges[1:])):
        raise argparse.ArgumentTypeError(
            f"Expected {expected_count} strictly increasing edges, got {value!r}."
        )
    return edges


def make_digit_seed(crop: Image.Image, edge_exclusion: int) -> Image.Image:
    """Return bright amber cathode pixels that are safely inside the cell border."""
    width, height = crop.size
    seed = Image.new("L", crop.size, 0)
    source_pixels = crop.load()
    seed_pixels = seed.load()

    for y in range(edge_exclusion, height - edge_exclusion):
        for x in range(edge_exclusion, width - edge_exclusion):
            red, green, blue = source_pixels[x, y]
            if (
                red > 125
                and red * 100 > green * 130
                and green * 100 > blue * 120
                and red + green + blue > 235
            ):
                seed_pixels[x, y] = 255
    return seed


def make_separator_seed(crop: Image.Image, edge_exclusion: int, radius: int) -> Image.Image:
    """Return the compact, solid circle seed for the clock separator."""
    width, height = crop.size
    source_pixels = crop.load()
    candidates: list[tuple[int, int]] = []

    for y in range(edge_exclusion, height - edge_exclusion):
        for x in range(edge_exclusion, width - edge_exclusion):
            red, green, blue = source_pixels[x, y]
            if (
                red > 100
                and red * 100 > green * 122
                and green * 100 > blue * 108
                and red + green + blue > 180
            ):
                candidates.append((x, y))

    seed = Image.new("L", crop.size, 0)
    if not candidates:
        return seed

    centre_x, centre_y = width // 2, height // 2
    seed_x, seed_y = min(
        candidates,
        key=lambda point: (point[0] - centre_x) ** 2 + (point[1] - centre_y) ** 2,
    )
    source_pixels = crop.load()
    seed_pixels = seed.load()
    radius_squared = radius * radius
    for y in range(max(0, seed_y - radius), min(height, seed_y + radius + 1)):
        for x in range(max(0, seed_x - radius), min(width, seed_x + radius + 1)):
            if (x - seed_x) ** 2 + (y - seed_y) ** 2 > radius_squared:
                continue
            red, green, blue = source_pixels[x, y]
            if red > 70 and red * 100 > green * 115 and green * 100 > blue * 103:
                seed_pixels[x, y] = 255
    return seed.filter(ImageFilter.MaxFilter(3))


def make_edge_fade(size: tuple[int, int], fade_width: int) -> Image.Image:
    """Return alpha that reaches zero at the sprite edge to prevent cross-cell glow."""
    width, height = size
    fade = Image.new("L", size, 0)
    pixels = fade.load()
    for y in range(height):
        for x in range(width):
            distance = min(x, y, width - 1 - x, height - 1 - y)
            pixels[x, y] = min(255, round(distance * 255 / fade_width))
    return fade


def make_glow_alpha(
    crop: Image.Image,
    index: int,
    edge_exclusion: int,
    separator_radius: int,
    glow_radius: float,
    glow_cutoff: int,
    glow_gain: int,
    edge_fade_width: int,
) -> Image.Image:
    """Build alpha from a protected cathode core and its local glow field."""
    seed = (
        make_separator_seed(crop, edge_exclusion, separator_radius)
        if index == 10
        else make_digit_seed(crop, edge_exclusion)
    )
    blurred_seed = seed.filter(ImageFilter.GaussianBlur(glow_radius))
    alpha = Image.new("L", crop.size, 0)
    seed_pixels = seed.load()
    blurred_pixels = blurred_seed.load()
    alpha_pixels = alpha.load()

    for y in range(crop.height):
        for x in range(crop.width):
            value = blurred_pixels[x, y]
            glow_alpha = 0 if value <= glow_cutoff else min(238, (value - glow_cutoff) * glow_gain)
            alpha_pixels[x, y] = 255 if seed_pixels[x, y] else glow_alpha

    return ImageChops.multiply(alpha, make_edge_fade(crop.size, edge_fade_width))


def remove_black_matte(crop: Image.Image, alpha: Image.Image) -> Image.Image:
    """Straighten colours that were photographed over a black background.

    The source RGB values are black-matted: a semi-transparent glow pixel holds
    its colour already darkened by the black background. Dividing colour by the
    generated alpha restores straight alpha RGB, so compositing over a light
    background does not leave a dark halo.
    """
    result = Image.new("RGBA", crop.size)
    source_pixels = crop.load()
    alpha_pixels = alpha.load()
    result_pixels = result.load()

    for y in range(crop.height):
        for x in range(crop.width):
            opacity = alpha_pixels[x, y]
            if opacity == 0:
                result_pixels[x, y] = (0, 0, 0, 0)
                continue
            red, green, blue = source_pixels[x, y]
            brightness = red + green + blue
            if brightness < 50:
                # The original background is near-black. It has no useful colour
                # information for a glow pixel, so render emitted amber light
                # rather than preserving a black fringe within the alpha field.
                result_pixels[x, y] = (255, 142, 24, opacity)
            else:
                result_pixels[x, y] = (
                    min(255, red * 255 // opacity),
                    min(255, green * 255 // opacity),
                    min(255, blue * 255 // opacity),
                    opacity,
                )
    return result


def save_preview(
    sprites: list[Image.Image],
    output_path: Path,
    checkerboard: bool,
) -> None:
    """Write a labelled 4 x 3 preview of generated sprites."""
    label_height = 24
    cell_width = max(sprite.width for sprite in sprites)
    cell_height = max(sprite.height for sprite in sprites)
    preview = Image.new("RGB", (cell_width * 4, (cell_height + label_height) * 3), "black")
    draw = ImageDraw.Draw(preview)

    for row in range(3):
        for column in range(4):
            index = row * 4 + column
            x = column * cell_width
            y = row * (cell_height + label_height)
            if checkerboard:
                for checker_y in range(y + label_height, y + label_height + cell_height, 12):
                    for checker_x in range(x, x + cell_width, 12):
                        shade = 62 if ((checker_x // 12) + (checker_y // 12)) % 2 else 42
                        draw.rectangle(
                            (
                                checker_x,
                                checker_y,
                                min(checker_x + 11, x + cell_width - 1),
                                min(checker_y + 11, y + label_height + cell_height - 1),
                            ),
                            fill=(shade, shade, shade),
                        )
            preview.paste(sprites[index], (x, y + label_height), sprites[index])
            draw.text((x + 5, y + 5), CHARACTERS[index], fill=(255, 190, 90))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    preview.save(output_path, optimize=True)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate transparent Nixie digit sprites from a 4 x 3 atlas."
    )
    parser.add_argument(
        "source",
        nargs="?",
        type=Path,
        default=Path("assets/nixie/nix2.jpg"),
        help="Source Nixie atlas image (default: assets/nixie/nix2.jpg)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("assets/nixie/generated"),
        help="Directory for RGBA sprite PNGs",
    )
    parser.add_argument(
        "--preview",
        type=Path,
        default=Path("assets/nixie/generated/preview.png"),
        help="Checkerboard preview PNG path",
    )
    parser.add_argument(
        "--x-edges",
        type=lambda value: parse_edges(value, 5),
        default=DEFAULT_X_EDGES,
        help="Five comma-separated column crop edges",
    )
    parser.add_argument(
        "--y-edges",
        type=lambda value: parse_edges(value, 4),
        default=DEFAULT_Y_EDGES,
        help="Four comma-separated row crop edges",
    )
    parser.add_argument("--edge-exclusion", type=int, default=18, help="Pixels excluded from seed detection at every edge")
    parser.add_argument("--edge-fade-width", type=int, default=16, help="Pixels used to fade each sprite to transparent at every edge")
    parser.add_argument("--glow-radius", type=float, default=18, help="Gaussian blur radius for the local glow")
    parser.add_argument("--glow-cutoff", type=int, default=5, help="Blur alpha below this value is transparent")
    parser.add_argument("--glow-gain", type=int, default=7, help="Multiplier for the post-cutoff glow alpha")
    parser.add_argument("--separator-radius", type=int, default=20, help="Radius of the separator's solid-circle seed")
    arguments = parser.parse_args()

    if not arguments.source.is_file():
        parser.error(f"Source image does not exist: {arguments.source}")
    if min(arguments.edge_exclusion, arguments.edge_fade_width, arguments.separator_radius) < 1:
        parser.error("Edge exclusion, edge fade width, and separator radius must be positive.")
    if arguments.glow_radius <= 0 or arguments.glow_gain < 1 or arguments.glow_cutoff < 0:
        parser.error("Glow radius and gain must be positive; cutoff cannot be negative.")

    source = Image.open(arguments.source).convert("RGB")
    if arguments.x_edges[-1] > source.width or arguments.y_edges[-1] > source.height:
        parser.error("Crop edges exceed the source image dimensions.")

    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    sprites: list[Image.Image] = []
    for row in range(3):
        for column in range(4):
            index = row * 4 + column
            crop = source.crop(
                (
                    arguments.x_edges[column],
                    arguments.y_edges[row],
                    arguments.x_edges[column + 1],
                    arguments.y_edges[row + 1],
                )
            )
            alpha = make_glow_alpha(
                crop,
                index,
                arguments.edge_exclusion,
                arguments.separator_radius,
                arguments.glow_radius,
                arguments.glow_cutoff,
                arguments.glow_gain,
                arguments.edge_fade_width,
            )
            sprite = remove_black_matte(crop, alpha)
            sprite.save(arguments.output_dir / f"{index:02d}-{CHARACTERS[index]}.png", optimize=True)
            sprites.append(sprite)

    save_preview(sprites, arguments.preview, checkerboard=True)
    print(f"Generated {len(sprites)} transparent sprites in {arguments.output_dir}")
    print(f"Preview: {arguments.preview}")


if __name__ == "__main__":
    main()