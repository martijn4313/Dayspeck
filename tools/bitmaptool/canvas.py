"""OLEDCanvas: an in-memory 128x64 pixel buffer that mirrors the SSD1306 framebuffer."""

from PIL import Image

from .constants import *  # noqa: F403
from .font5x7 import FONT_5X7


# ──────────────────────────────────────────────────────────────────────────────
# OLEDCanvas — In-memory 128x64 pixel buffer
# ──────────────────────────────────────────────────────────────────────────────

class OLEDCanvas:
    """Mirrors the SSD1306 framebuffer as a 128x64 bool array."""

    def __init__(self):
        self.width = CANVAS_W
        self.height = CANVAS_H
        self.pixels = [[False] * self.width for _ in range(self.height)]

    def clear(self):
        self.pixels = [[False] * self.width for _ in range(self.height)]

    def set_pixel(self, x: int, y: int, on: bool = True):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y][x] = on

    def get_pixel(self, x: int, y: int) -> bool:
        if 0 <= x < self.width and 0 <= y < self.height:
            return self.pixels[y][x]
        return False

    def draw_line(self, x0: int, y0: int, x1: int, y1: int, on: bool = True):
        """Bresenham's line algorithm."""
        dx = abs(x1 - x0)
        dy = abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx - dy
        while True:
            self.set_pixel(x0, y0, on)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                x0 += sx
            if e2 < dx:
                err += dx
                y0 += sy

    def draw_rect(self, x: int, y: int, w: int, h: int, fill: bool = False, on: bool = True):
        if fill:
            for dy in range(h):
                for dx in range(w):
                    self.set_pixel(x + dx, y + dy, on)
        else:
            for dx in range(w):
                self.set_pixel(x + dx, y, on)
                self.set_pixel(x + dx, y + h - 1, on)
            for dy in range(h):
                self.set_pixel(x, y + dy, on)
                self.set_pixel(x + w - 1, y + dy, on)

    def draw_circle(self, cx: int, cy: int, r: int, fill: bool = False, on: bool = True):
        """Midpoint circle algorithm."""
        x = r
        y = 0
        err = 1 - r
        while x >= y:
            if fill:
                for i in range(-x, x + 1):
                    self.set_pixel(cx + i, cy + y, on)
                    self.set_pixel(cx + i, cy - y, on)
                    self.set_pixel(cx + y, cy + i, on)
                    self.set_pixel(cx - y, cy + i, on)
            else:
                self.set_pixel(cx + x, cy + y, on)
                self.set_pixel(cx - x, cy + y, on)
                self.set_pixel(cx + x, cy - y, on)
                self.set_pixel(cx - x, cy - y, on)
                self.set_pixel(cx + y, cy + x, on)
                self.set_pixel(cx - y, cy + x, on)
                self.set_pixel(cx + y, cy - x, on)
                self.set_pixel(cx - y, cy - x, on)
            y += 1
            if err < 0:
                err += 2 * y + 1
            else:
                x -= 1
                err += 2 * (y - x) + 1

    def fill_rect(self, x: int, y: int, w: int, h: int, on: bool = True):
        for dy in range(h):
            for dx in range(w):
                self.set_pixel(x + dx, y + dy, on)

    def draw_hline(self, x: int, y: int, w: int, on: bool = True):
        for dx in range(w):
            self.set_pixel(x + dx, y, on)

    def fill_disc(self, cx: int, cy: int, r: int, on: bool = True):
        """Filled circle (all pixels with x^2 + y^2 <= r^2), like the firmware's drawFilledCircle."""
        for y in range(-r, r + 1):
            for x in range(-r, r + 1):
                if x * x + y * y <= r * r:
                    self.set_pixel(cx + x, cy + y, on)

    def draw_circle_helper(self, x0: int, y0: int, r: int, corner: int, on: bool = True):
        """Port of Adafruit GFX drawCircleHelper: quarter circles. corner bits: 1 TL, 2 TR, 4 BR, 8 BL."""
        f = 1 - r
        ddf_x = 1
        ddf_y = -2 * r
        x = 0
        y = r
        while x < y:
            if f >= 0:
                y -= 1
                ddf_y += 2
                f += ddf_y
            x += 1
            ddf_x += 2
            f += ddf_x
            if corner & 0x4:
                self.set_pixel(x0 + x, y0 + y, on)
                self.set_pixel(x0 + y, y0 + x, on)
            if corner & 0x2:
                self.set_pixel(x0 + x, y0 - y, on)
                self.set_pixel(x0 + y, y0 - x, on)
            if corner & 0x8:
                self.set_pixel(x0 - y, y0 + x, on)
                self.set_pixel(x0 - x, y0 + y, on)
            if corner & 0x1:
                self.set_pixel(x0 - y, y0 - x, on)
                self.set_pixel(x0 - x, y0 - y, on)

    def draw_text(self, x: int, y: int, text: str, on: bool = True, size: int = 1):
        """Draw text with the classic 5x7 GFX font (6 px per character at size 1), like display.print()
        after setTextSize(size): every font pixel becomes a size x size block."""
        for ch in text:
            glyph = FONT_5X7.get(ch, FONT_5X7["?"])
            for col, bits in enumerate(glyph):
                for row in range(8):
                    if bits & (1 << row):
                        self.fill_rect(x + col * size, y + row * size, size, size, on)
            x += 6 * size

    def blit(self, x: int, y: int, bitmap: list[list[bool]]):
        """Paste a 2D bool array at offset (x, y)."""
        for row_idx, row in enumerate(bitmap):
            for col_idx, val in enumerate(row):
                if val:
                    self.set_pixel(x + col_idx, y + row_idx, True)

    def to_image(self, scale: int = DEFAULT_SCALE, on: str = None, off: str = None) -> Image.Image:
        """Render to a Pillow RGB image (no Tk needed). Colours default to the current theme."""
        from . import theme
        on_rgb = self._hex_to_rgb(on or theme.on)
        off_rgb = self._hex_to_rgb(off or theme.off)
        img = Image.new("RGB", (self.width, self.height))
        img.putdata([on_rgb if px else off_rgb for row in self.pixels for px in row])
        if scale > 1:
            img = img.resize((self.width * scale, self.height * scale), Image.Resampling.NEAREST)
        return img

    def to_photoimage(self, scale: int = DEFAULT_SCALE):
        """Convert to a PhotoImage for tk.Canvas display."""
        from PIL import ImageTk
        return ImageTk.PhotoImage(self.to_image(scale))

    @staticmethod
    def _hex_to_rgb(hex_color: str) -> tuple:
        """Convert hex color string to RGB tuple."""
        hex_color = hex_color.lstrip('#')
        return tuple(int(hex_color[i:i+2], 16) for i in (0, 2, 4))


