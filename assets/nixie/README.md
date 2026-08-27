---
title: Nixie Sprite Source Artwork
description: Source artwork and generation workflow for Nixie clock digit sprites
ms.date: 2026-08-18
ms.topic: reference
---

## Source artwork

`nix2.jpg` is the source atlas for the Nixie digit sprites. It contains the characters in this order:

```text
0 1 2 3
4 5 6 7
8 9 . [empty]
```

The `.` cell is used as the clock separator. Generated images are ignored because they can always be reproduced from the source artwork.

## Generate sprites

Run the generator from the repository root:

```bash
python3 tools/generate-nixie-sprites.py
```

The default configuration produces RGBA PNGs in `assets/nixie/generated/` and a checkerboard preview at `assets/nixie/generated/preview.png`. Defaults implement the selected wide-balanced glow:

* 18 pixel glow radius
* 5 alpha cutoff
* 7 alpha gain
* 16 pixel transparent edge fade

The cathode seed detector ignores the outer 18 pixels of each cell, and the fade reaches transparent at every sprite edge. These constraints prevent glow from an adjacent character from appearing in the generated sprite.

## Tune the result

Use command-line options to adjust crop bounds or glow behaviour:

```bash
python3 tools/generate-nixie-sprites.py \
  --output-dir build/nixie-sprites \
  --preview build/nixie-sprites-preview.png \
  --glow-radius 20 \
  --glow-cutoff 4 \
  --glow-gain 5 \
  --edge-fade-width 16
```

Run `python3 tools/generate-nixie-sprites.py --help` for all options. The generator requires Python 3 and Pillow.

## Generate Extension assets

The native Nixie Clock extension uses opaque RGB565 artwork on a black canvas because the Extension canvas ABI does not support alpha blending. Generate its tracked palette-RLE include with:

```bash
python3 tools/generate-nixie-extension-assets.py
```

The default output, `extensions/nixie-clock/nixie_assets.inc`, contains 96 x 147 pixel canonical sprites. The extension decodes one sprite into PSRAM at a time, applies uniform aspect-fit scaling without upscaling, and leaves unused button space black. The default output is designed for the 120 KiB native Extension slot.

Use `--width`, `--height`, and `--colors` to experiment with the digit package-size and image-quality tradeoff. The separator defaults to its own 64-colour palette through `--separator-colors` and a short separator-specific falloff, which avoids a visible halo around its small glowing dot. Rebuild the Nixie Clock extension after regenerating the include.