"""Render a 128x64 canvas as an OLED module: bezel, dot matrix and glow (the look of the manual's screenshots)."""

from PIL import Image, ImageChops, ImageDraw, ImageFilter

from .canvas import OLEDCanvas

BEZEL = 14                       # bezel width in px at scale 4
BEZEL_COLOR = (58, 60, 64)
BEZEL_EDGE = (86, 88, 93)        # the lighter outer rim
PANEL = (0, 26, 28)              # the gaps between the dots
CELL_OFF = (0, 21, 21)
CELL_ON = (36, 165, 235)
GLOW = (30, 150, 170)            # added around lit dots, scaled by the blurred neighbourhood


def device_image(pixels: list, scale: int = 4) -> Image.Image:
    """RGBA image of the module: a 128x64 bool array, `scale` px per dot (a dot is scale-1 px with a 1 px gap)."""
    rows, cols = len(pixels), len(pixels[0])
    dot = scale - 1
    panel = Image.new("RGB", (cols * scale, rows * scale), PANEL)
    draw = ImageDraw.Draw(panel)
    mask = Image.new("L", panel.size, 0)
    mask_draw = ImageDraw.Draw(mask)
    for y, row in enumerate(pixels):
        for x, on in enumerate(row):
            box = (x * scale + 1, y * scale + 1, x * scale + dot, y * scale + dot)
            if on:
                draw.rectangle(box, fill=CELL_ON)
                mask_draw.rectangle(box, fill=255)
            else:
                draw.rectangle(box, fill=CELL_OFF)
    glow = mask.filter(ImageFilter.GaussianBlur(scale))
    glow_layer = Image.merge("RGB", [glow.point(lambda v, c=c: v * c // 255) for c in GLOW])
    panel = ImageChops.add(panel, glow_layer)

    margin = round(BEZEL * scale / 4)
    w, h = panel.width + 2 * margin, panel.height + 2 * margin
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(out)
    radius = round(10 * scale / 4)
    rim = max(1, round(2 * scale / 4))
    d.rounded_rectangle((0, 0, w - 1, h - 1), radius=radius, fill=BEZEL_EDGE)
    d.rounded_rectangle((rim, rim, w - 1 - rim, h - 1 - rim), radius=max(1, radius - rim), fill=BEZEL_COLOR)
    out.paste(panel, (margin, margin))
    return out


def canvas_image(canvas: OLEDCanvas, scale: int = 4) -> Image.Image:
    return device_image(canvas.pixels, scale)
