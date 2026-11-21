"""Render the app SVG as a multi-resolution Windows icon using Pillow."""
import io
import os
import re
import struct
import xml.etree.ElementTree as ET
from PIL import Image, ImageChops, ImageColor, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
SUPER = 8


def subpaths(d):
    """Yields each subpath of a path as a list of points (curves flattened)."""
    tokens = re.findall(r"[MmLlHhVvCcSsZz]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?", d)
    i, cmd = 0, None
    x = y = sx = sy = 0.0
    last_ctrl = None
    pts = []

    def num():
        nonlocal i
        v = float(tokens[i])
        i += 1
        return v

    def bezier(p0, p1, p2, p3):
        for k in range(1, 17):
            t = k / 16
            a, b, c, e = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t, t ** 3
            pts.append((a * p0[0] + b * p1[0] + c * p2[0] + e * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + e * p3[1]))

    while i < len(tokens):
        if re.match(r"[A-Za-z]", tokens[i]):
            cmd = tokens[i]
            i += 1
        rel = cmd.islower()
        c = cmd.upper()
        ctrl = None
        if c == "M":
            if len(pts) > 1:
                yield pts
            x, y = (x + num(), y + num()) if rel else (num(), num())
            sx, sy = x, y
            pts = [(x, y)]
            cmd = "l" if rel else "L"  # extra pairs after M are lines
        elif c == "L":
            x, y = (x + num(), y + num()) if rel else (num(), num())
            pts.append((x, y))
        elif c == "H":
            x = x + num() if rel else num()
            pts.append((x, y))
        elif c == "V":
            y = y + num() if rel else num()
            pts.append((x, y))
        elif c in "CS":
            if c == "C":
                x1, y1 = num(), num()
                if rel:
                    x1, y1 = x + x1, y + y1
            else:
                x1, y1 = (2 * x - last_ctrl[0], 2 * y - last_ctrl[1]) if last_ctrl else (x, y)
            x2, y2, ex, ey = num(), num(), num(), num()
            if rel:
                x2, y2, ex, ey = x + x2, y + y2, x + ex, y + ey
            bezier((x, y), (x1, y1), (x2, y2), (ex, ey))
            x, y = ex, ey
            ctrl = (x2, y2)
        elif c == "Z":
            x, y = sx, sy
            if len(pts) > 1:
                yield pts
            pts = [(x, y)]
        last_ctrl = ctrl
    if len(pts) > 1:
        yield pts


def render(svg, size):
    big = size * SUPER
    scale = big / float(svg.attrib["viewBox"].split()[2])
    icon = Image.new("RGBA", (big, big))
    for shape in svg:
        attrs = shape.attrib
        color = attrs.get("fill", "none")
        if color == "none":
            continue
        mask = Image.new("1", (big, big))
        draw = ImageDraw.Draw(mask)
        kind = shape.tag.rsplit("}", 1)[-1]
        if kind == "rect":
            w, h, radius = (float(attrs[key]) * scale for key in ("width", "height", "rx"))
            draw.rounded_rectangle((0, 0, w - 1, h - 1), radius=radius, fill=1)
        elif kind == "path":
            for subpath in subpaths(attrs["d"]):
                layer = Image.new("1", (big, big))
                ImageDraw.Draw(layer).polygon([(x * scale, y * scale) for x, y in subpath], fill=1)
                mask = ImageChops.logical_xor(mask, layer)
        elif kind == "circle":
            x, y, radius = (float(attrs[key]) for key in ("cx", "cy", "r"))
            draw.ellipse(tuple(v * scale for v in (x - radius, y - radius, x + radius, y + radius)), fill=1)
        else:
            raise ValueError(f"Unsupported icon shape: {kind}")
        layer = Image.new("RGBA", (big, big), ImageColor.getrgb(color))
        layer.putalpha(mask.convert("L"))
        icon.alpha_composite(layer)
    return icon.resize((size, size), Image.LANCZOS)


def icon_frame(image):
    size = image.width
    if size == 256:
        png = io.BytesIO()
        image.save(png, "PNG")
        return png.getvalue()

    # Small Windows icon frames use a bottom-up 32-bit DIB and a 1-bit transparency mask.
    flipped = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    alpha = flipped.getchannel("A")
    mask_rows = (size + 31) // 32 * 4
    mask = bytearray(mask_rows * size)
    for y in range(size):
        for x in range(size):
            if alpha.getpixel((x, y)) == 0:
                mask[y * mask_rows + x // 8] |= 0x80 >> (x % 8)
    pixels = flipped.tobytes("raw", "BGRA")
    info = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(pixels), 0, 0, 0, 0)
    return info + pixels + mask


def main():
    svg = ET.parse(os.path.join(HERE, "app.svg")).getroot()
    frames = [icon_frame(render(svg, size)) for size in SIZES]
    with open(os.path.join(HERE, "app.ico"), "wb") as out:
        out.write(struct.pack("<HHH", 0, 1, len(SIZES)))
        offset = 6 + 16 * len(SIZES)
        for size, frame in zip(SIZES, frames):
            d = size % 256
            out.write(struct.pack("<BBBBHHII", d, d, 0, 0, 1, 32, len(frame), offset))
            offset += len(frame)
        for frame in frames:
            out.write(frame)
    print("wrote app.ico:", ", ".join(map(str, SIZES)))


if __name__ == "__main__":
    main()
