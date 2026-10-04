"""Independent production PNG vs diagnostic SDL viewport framebuffer.
Production retains its normal HUD/axes/animation slider. Compare only the
central scene area, excluding those known UI overlays (outside the contract).
Full overlay-free composition remains covered by the real private SDL fixture.
"""
import json
import sys
from PIL import Image
native = Image.open(sys.argv[1]).convert('RGBA')
display = Image.open(sys.argv[2]).convert('RGBA')
error = flipped = black = pixels = 0
assert native.width > 0 and native.height > 0
assert native.getextrema()[3] == (255, 255)
for y in range(native.height // 5, 4 * native.height // 5, 3):
    for x in range(7 * native.width // 20, 4 * native.width // 5, 3):
        dx = min(display.width - 1, int((x + .5) * display.width / native.width))
        dy = min(display.height - 1, int((y + .5) * display.height / native.height))
        actual = display.getpixel((dx, dy))
        expected = native.getpixel((x, y))
        inverse = native.getpixel((x, native.height - 1 - y))
        error += sum(abs(actual[c] - expected[c]) for c in range(3))
        flipped += sum(abs(actual[c] - inverse[c]) for c in range(3))
        black += sum(actual[:3])
        pixels += 1
assert pixels > 500
mean = error / (3 * pixels)
assert mean < 8, (mean, native.size, display.size)
print(json.dumps(dict(native=native.size, display=display.size,
    sampledPixels=pixels, region='central scene; normal HUD/axes/slider excluded',
    meanRgbError=mean, flippedRgbError=flipped / (3 * pixels),
    blackRgbError=black / (3 * pixels), opaqueAlpha=True)))
