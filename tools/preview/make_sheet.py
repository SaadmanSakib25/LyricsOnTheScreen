"""Turn raw RGB565 frames from preview.exe into a labelled PNG contact sheet.

usage: python make_sheet.py frames.bin sheet.png <label> [<label> ...]
Labels are usually the same song times (ms) you passed to preview.exe.
Needs Pillow (pip install pillow).
"""
import struct
import sys

from PIL import Image, ImageDraw

W, H, SCALE, COLS = 128, 160, 2, 6


def main() -> None:
    raw = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    labels = sys.argv[3:]
    n = len(raw) // (W * H * 2)
    cols = min(COLS, n)
    rows = (n + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (W * SCALE + 8) + 8, rows * (H * SCALE + 22) + 8), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)
    for k in range(n):
        frame = Image.new("RGB", (W, H))
        px = frame.load()
        for i in range(W * H):
            v = struct.unpack_from("<H", raw, (k * W * H + i) * 2)[0]
            px[i % W, i // W] = ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
        x = 8 + (k % cols) * (W * SCALE + 8)
        y = 8 + (k // cols) * (H * SCALE + 22)
        sheet.paste(frame.resize((W * SCALE, H * SCALE), Image.NEAREST), (x, y + 16))
        draw.text((x, y + 2), labels[k] if k < len(labels) else "", fill=(220, 220, 220))
    sheet.save(out)
    print(f"wrote {out} ({n} frames)")


if __name__ == "__main__":
    main()
