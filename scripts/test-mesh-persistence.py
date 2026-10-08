#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Author the Archive regression's real replacement with the installed helper."""
import os
import sys

import numpy as np

sys.path.insert(0, os.environ["VSR_MESH_HELPER_PATH"])
from vsr_mesh import Bundle

mesh = Bundle.load(sys.argv[1])
assert mesh.read("vertex.position").shape == (3, 3)
assert mesh.inspect()["fields"]["vertex.attribute1"]["association"] == "vertex"
nonindexed = sys.argv[3] == "nonindexed"
positions = np.array([[-0.4, -0.4, 0], [0.4, -0.4, 0],
                      [0.4, 0.4, 0], [-0.4, 0.4, 0]], dtype=np.float32)
uv = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype=np.float32)
triangles = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.uint32)
if nonindexed:
    positions = positions[triangles.reshape(-1)]
    uv = uv[triangles.reshape(-1)]
    mesh.remove("primitive.index")
else:
    mesh.replace("primitive.index", triangles, "ANARI_UINT32_VEC3")
mesh.replace("vertex.position", positions, "ANARI_FLOAT32_VEC3")
mesh.replace("vertex.attribute0", uv, "ANARI_FLOAT32_VEC2")
mesh.alias("vertex.attribute1", "vertex.attribute0", enabled=False)
# Equal bytes are intentionally NOT an alias.
mesh.replace("vertex.attribute2", uv.copy(), "ANARI_FLOAT32_VEC2", enabled=False)
mesh.replace("vertex.normal", np.tile(np.array([[0, 0, 1]], dtype=np.float32),
                                    (len(positions), 1)), "ANARI_FLOAT32_VEC3")
mesh.replace("primitive.color", np.array([[1, 0, 0], [0, 1, 0]],
                                        dtype=np.float32), "ANARI_FLOAT32_VEC3")
mesh.alias("primitive.attribute0", "primitive.color", enabled=False)
mesh.replace("vertex.color", np.tile(np.array([[1, 17, 254, 255]], dtype=np.uint8),
                                    (len(positions), 1)),
             "ANARI_UFIXED8_VEC4", enabled=False)
mesh.replace("vertex.tangent", np.tile(np.array([[1, 0, 0, -1]], dtype=np.float64),
                                      (len(positions), 1)),
             "ANARI_FLOAT64_VEC4", enabled=False)
mesh.replace("primitive.attribute2", np.array([2**63 + 7, 2**64 - 1],
                                              dtype=np.uint64),
             "ANARI_UINT64", enabled=False)
mesh.replace("faceVarying.attribute3", np.arange(-6, 6, dtype=np.int16)
             .reshape(6, 2), "ANARI_INT16_VEC2", enabled=False)
mesh.remove("vertex.attribute3")
mesh.save(sys.argv[2])
# Read back through the documented helper, including semantic types and aliases.
result = Bundle.load(sys.argv[2])
fields = result.inspect()["fields"]
assert fields["vertex.attribute0"]["payload"] == fields["vertex.attribute1"]["payload"]
assert fields["vertex.attribute0"]["payload"] != fields["vertex.attribute2"]["payload"]
assert result.read("primitive.attribute2").tolist() == [2**63 + 7, 2**64 - 1]
np.testing.assert_array_equal(result.read("vertex.position"), positions)
