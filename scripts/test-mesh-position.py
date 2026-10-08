#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Public NumPy helper edit used by the MCP position-import tracer."""
import os
import sys

sys.path.insert(0, os.environ["VSR_MESH_HELPER_PATH"])
from vsr_mesh import Bundle

mesh = Bundle.load(sys.argv[1])
positions = mesh.read("vertex.position")
positions[:, 0] += 10
if len(sys.argv) > 3 and sys.argv[3] == "independent":
    mesh = Bundle.create()
    mesh.replace("vertex.position", positions, "ANARI_FLOAT32_VEC3")
elif len(sys.argv) > 3 and sys.argv[3] == "split":
    mesh.replace("vertex.position", positions, "ANARI_FLOAT32_VEC3")
elif len(sys.argv) > 3 and sys.argv[3] == "count":
    mesh.replace("vertex.position", positions.repeat(2, axis=0), "ANARI_FLOAT32_VEC3")
elif len(sys.argv) > 3 and sys.argv[3] == "state":
    mesh.replace("vertex.position", positions, "ANARI_FLOAT32_VEC3", enabled=False)
elif len(sys.argv) > 3 and sys.argv[3] == "optional":
    mesh.replace("vertex.normal", positions, "ANARI_FLOAT32_VEC3")
elif len(sys.argv) > 3 and sys.argv[3] == "primitive":
    mesh.replace("primitive.color", positions, "ANARI_FLOAT32_VEC3")
else:
    mesh.write("vertex.position", positions)
mesh.save(sys.argv[2])
