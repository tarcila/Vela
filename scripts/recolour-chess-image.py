#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Swap R/G only in green-dominant chess accents, preserving raw other bytes.

Usage: recolour-chess-image.py input.png output.png
No resizing, channel conversion, gamma transform or asset overwriting.
"""

import hashlib
import json
import sys
from pathlib import Path
from PIL import Image


def main():
    source, target = map(Path, sys.argv[1:3])
    if source.resolve() == target.resolve():
        raise ValueError('use a distinct working-copy basename')
    with Image.open(source) as image:
        if image.mode not in ('RGB', 'RGBA'):
            raise ValueError('RGB/RGBA raw chess Image required')
        original = image.tobytes()
        changed = bytearray(original)
        channels = len(image.getbands())
        accents = neutral = 0
        neutral_example = accent_example = None
        for i in range(0, len(changed), channels):
            r, g, b = original[i:i+3]
            if g >= 40 and g > r + 12 and g > b + 6:
                changed[i], changed[i+1] = g, r
                accents += 1
                if accent_example is None:
                    accent_example = list(original[i:i+channels])
            elif max(r, g, b) - min(r, g, b) <= 3 and max(r, g, b) < 64:
                neutral += 1
                assert changed[i:i+channels] == original[i:i+channels]
                if r == g == b and r > 0 and neutral_example is None:
                    neutral_example = list(original[i:i+channels])
        assert accents and neutral and neutral_example, 'actual green accents and neutral dark texels'
        if channels == 4:
            assert changed[3::4] == original[3::4], 'exact alpha bytes'
        output = Image.frombytes(image.mode, image.size, bytes(changed))
        output.save(target, format='PNG', compress_level=6)
        print(json.dumps({'dimensions': list(image.size), 'mode': image.mode,
                          'channels': channels, 'green_accent_pixels': accents,
                          'neutral_dark_pixels_preserved': neutral,
                          'exact_neutral_dark_sample': neutral_example,
                          'green_accent_sample': accent_example,
                          'source_texels_sha256': hashlib.sha256(original).hexdigest(),
                          'processed_texels_sha256': hashlib.sha256(changed).hexdigest()}))


if __name__ == '__main__':
    main()
