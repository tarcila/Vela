# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Public MCP fixture artifact → documented NumPy helper boundary."""
import os
import pathlib
import sys

sys.path.insert(0, os.environ.get('VSR_MESH_HELPER_PATH', str(pathlib.Path(__file__).resolve().parents[1] / 'python')))
from vsr_mesh import Bundle

bundle = Bundle.load(sys.argv[1])
metadata = bundle.inspect()
fields = metadata['fields']
if '--indexed' in sys.argv:
    assert bundle.read('vertex.position').dtype.str == '<f8'
    assert bundle.read('vertex.position').tolist() == [[0,0,0], [1,0,0], [0,1,0], [3,4,5]]
    assert bundle.read('primitive.index').dtype.str == '<u8'
    assert bundle.read('primitive.index').tolist() == [[0,1,2]]
    assert bundle.read('primitive.attribute2').tolist() == [-123]
    assert bundle.read('vertex.color').tolist() == [[0,1,2,3], [4,5,6,7], [8,9,10,11], [252,253,254,255]]
    assert bundle.read('vertex.tangent').tolist() == [[1,0,0,-1], [1,0,0,1], [1,0,0,-1], [1,0,0,1]]
    assert fields['faceVarying.attribute0']['association'] == 'corner'
    assert fields['primitive.attribute2']['association'] == 'primitive'
    assert fields['faceVarying.attribute0']['payload'] == fields['faceVarying.attribute1']['payload']
    assert fields['faceVarying.attribute0']['payload'] != fields['faceVarying.attribute3']['payload']
    assert bundle.read('faceVarying.attribute0').tolist() == [[0,0], [1,0], [0,1]]
    sys.exit(0)
assert fields['vertex.position']['payload'] == fields['vertex.normal']['payload']
assert fields['vertex.position']['enabled'] is True
assert fields['vertex.normal']['enabled'] is False
assert fields['vertex.position']['association'] == 'vertex'
assert bundle.read('vertex.position').dtype.str == '<f4'
if '--edited' in sys.argv:
    assert bundle.read('vertex.position').tolist() == [[2, 0, 0], [3, 0, 0], [2, 1, 0]]
    assert fields['vertex.position']['action'] == 'replace'
else:
    assert bundle.read('vertex.position').tolist() == [[0, 0, 0], [1, 0, 0], [0, 1, 0]]
    assert fields['vertex.position']['action'] == 'preserve'
    points = bundle.read('vertex.position')
    points[:, 0] += 2
    bundle.write('vertex.position', points)
    bundle.save(sys.argv[1])
