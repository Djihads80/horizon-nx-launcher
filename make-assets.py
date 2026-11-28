#!/usr/bin/env python3
"""Draws the launcher's own pictures, so nothing of Mojang's art is in the app or the repository:

  romfs/img/launcher.png   the icon beside the title
  romfs/img/icons/*.png    the pictures an instance can pick
  icon.jpg                 the HOME menu icon of the .nro (256x256, required by libnx)

Run by packaging/switch-launcher/build.sh; needs Pillow.
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROMFS = os.path.join(HERE, "romfs", "img")
# The launcher's own mark, drawn by its author: scaled to the size the app and the HOME menu need.
ART = os.path.join(HERE, "art", "icon.png")

SIZE = 256


def rounded(color, radius=48):
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([0, 0, SIZE - 1, SIZE - 1], radius=radius, fill=color)
    return image


def save(image, *parts):
    path = os.path.join(*parts)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    image.save(path)
    print("  wrote", os.path.relpath(path, HERE))


def launcher():
    """The launcher's mark: art/icon.png when it is there, else the horizon drawn below."""
    if os.path.isfile(ART):
        source = Image.open(ART).convert("RGBA")
        # Square by construction, but a design that is not is cropped to its middle rather than
        # squashed.
        side = min(source.size)
        left = (source.width - side) // 2
        top = (source.height - side) // 2
        return source.crop((left, top, left + side, top + side)).resize((SIZE, SIZE), Image.LANCZOS)

    """A dark tile with a horizon line and a rising sun."""
    image = rounded((32, 40, 52))
    draw = ImageDraw.Draw(image)
    for y in range(120, 200):                       # sky, top to bottom
        t = (y - 120) / 80.0
        colour = (int(58 + 40 * (1 - t)), int(84 + 60 * (1 - t)), int(96 + 30 * t))
        draw.line([(28, y), (SIZE - 28, y)], fill=colour + (255,))
    draw.ellipse([92, 84, 164, 156], fill=(255, 206, 120))   # the sun
    draw.rectangle([28, 200, SIZE - 28, 228], fill=(70, 150, 96))   # the ground
    draw.rectangle([28, 200, SIZE - 28, 206], fill=(104, 190, 122))
    return image


ICONS = {
    "grass": lambda d: (
        d.rectangle([48, 48, 208, 208], fill=(122, 92, 66)),
        d.rectangle([48, 48, 208, 120], fill=(104, 178, 88)),
        d.rectangle([48, 112, 208, 132], fill=(84, 154, 72)),
    ),
    "stone": lambda d: (
        d.rectangle([48, 48, 208, 208], fill=(140, 140, 148)),
        d.ellipse([80, 88, 116, 124], fill=(116, 116, 126)),
        d.ellipse([136, 128, 184, 176], fill=(160, 160, 168)),
        d.ellipse([96, 160, 120, 184], fill=(116, 116, 126)),
    ),
    "diamond": lambda d: (
        d.polygon([(128, 52), (204, 128), (128, 204), (52, 128)], fill=(86, 210, 214)),
        d.polygon([(128, 82), (174, 136), (128, 174), (82, 136)], fill=(190, 244, 246)),
    ),
    "heart": lambda d: (
        d.ellipse([64, 68, 148, 152], fill=(226, 84, 88)),
        d.ellipse([108, 68, 192, 152], fill=(226, 84, 88)),
        d.polygon([(64, 120), (192, 120), (128, 204)], fill=(226, 84, 88)),
    ),
    "pickaxe": lambda d: (
        d.line([(70, 190), (172, 74)], fill=(150, 108, 70), width=22),
        d.arc([60, 40, 214, 152], start=200, end=340, fill=(206, 208, 214), width=24),
    ),
    "star": lambda d: (
        d.polygon(
            [(128, 46), (152, 108), (218, 108), (166, 148), (186, 210),
             (128, 172), (70, 210), (90, 148), (38, 108), (104, 108)],
            fill=(250, 206, 90)),
    ),
}


def icon(name, painter):
    """A square tile: no rounded corners, no transparency, so it sits in its row evenly."""
    image = Image.new("RGBA", (SIZE, SIZE), (245, 245, 248, 255))
    draw = ImageDraw.Draw(image)
    painter(draw)
    return image


def main():
    save(launcher(), ROMFS, "launcher.png")

    # The six drawn pictures are a starting set, not an icon pack: a folder that already holds PNGs
    # (the player's own, or a texture pack's) is left exactly as it is.
    folder = os.path.join(ROMFS, "icons")
    existing = [f for f in os.listdir(folder) if f.lower().endswith(".png")] if os.path.isdir(folder) else []
    if existing:
        print(f"  kept {len(existing)} icons already in romfs/img/icons")
    else:
        for name, painter in ICONS.items():
            save(icon(name, painter), ROMFS, "icons", name + ".png")

    # The .nro's HOME menu icon has to be a JPEG of 256x256.
    launcher().convert("RGB").save(os.path.join(HERE, "icon.jpg"), quality=92)
    print("  wrote icon.jpg")


if __name__ == "__main__":
    main()
