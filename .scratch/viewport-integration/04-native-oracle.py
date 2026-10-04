"""Independent Pillow/libpng decoder vs pre-Present actual SDL framebuffer.
Diagnostic files stay private; no desktop/user screenshot or encoder oracle.
"""
import json
import sys
from PIL import Image

native = Image.open(sys.argv[1]).convert('RGBA')
display = Image.open(sys.argv[2]).convert('RGBA')
assert native.width > 0 and native.height > 0
error = flipped = black = alpha_error = pixels = 0
# Interior comparison avoids ImGui window borders; the private fixture disables
# chrome overlays, HUD and gizmos. This does not claim physical monitor parity.
for y in range(native.height // 5, 4 * native.height // 5, 3):
    for x in range(native.width // 5, 4 * native.width // 5, 3):
        dx = min(display.width - 1, int((x + .5) * display.width / native.width))
        dy = min(display.height - 1, int((y + .5) * display.height / native.height))
        actual = display.getpixel((dx, dy))
        expected = native.getpixel((x, y))
        inverse = native.getpixel((x, native.height - 1 - y))
        error += sum(abs(actual[c] - expected[c]) for c in range(3))
        flipped += sum(abs(actual[c] - inverse[c]) for c in range(3))
        black += sum(actual[:3])
        alpha_error += abs(expected[3] - 255)
        pixels += 1
assert pixels > 500
mean = error / (3 * pixels)
assert mean < 8, (mean, native.size, display.size)
assert alpha_error == 0, alpha_error
print(json.dumps(dict(native=native.size, display=display.size, sampledPixels=pixels,
                      meanRgbError=mean, flippedRgbError=flipped / (3 * pixels),
                      blackRgbError=black / (3 * pixels), opaqueAlpha=True)))
