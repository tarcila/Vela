#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Count-changing public Mesh Exchange tracer using the installed helper."""
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.environ["VSR_MESH_HELPER_PATH"])
from vsr_mesh import Bundle

mesh = Bundle.load(sys.argv[1])
mode = sys.argv[3] if len(sys.argv) > 3 else "tracer"
if mode == "normal-on":
    mesh.alias("vertex.normal", "vertex.position", enabled=True)
    mesh.save(sys.argv[2])
    sys.exit(0)
if mode == "equivalent":
    mesh.write("vertex.position", mesh.read("vertex.position"))
    mesh.save(sys.argv[2])
    sys.exit(0)
if mode == "noop":
    mesh.save(sys.argv[2])
    sys.exit(0)
if mode in ("alias-write", "split-equal", "split", "nonindexed"):
    positions = mesh.read("vertex.position")
    if mode != "split-equal":
        positions[:, 0] += 10
    if mode == "alias-write":
        mesh.write("vertex.position", positions)
    elif mode == "nonindexed":
        mesh.replace("vertex.position", positions.repeat(2, axis=0),
                     "ANARI_FLOAT32_VEC3")
        mesh.remove("primitive.index")
        for name in list(mesh.inspect()["fields"]):
            if name.startswith("vertex.") and name != "vertex.position":
                mesh.remove(name)
    else:
        mesh.replace("vertex.position", positions, "ANARI_FLOAT32_VEC3")
    mesh.save(sys.argv[2])
    sys.exit(0)
# Two open triangles with a new silhouette, not a modeling-tool workflow.
mesh.replace("vertex.position", np.array([
    [-0.4, -0.4, 0], [0.4, -0.4, 0], [0.4, 0.4, 0], [-0.4, 0.4, 0]
], dtype=np.float32), "ANARI_FLOAT32_VEC3")
mesh.replace("primitive.index", np.array([[0, 1, 2], [0, 2, 3]],
                                       dtype=np.uint32), "ANARI_UINT32_VEC3")
if mode != "retain":
    mesh.remove("vertex.attribute0")
    if "vertex.attribute1" in mesh.inspect()["fields"]:
        mesh.remove("vertex.attribute1")
if mode == "retain-primitive":
    mesh.replace("primitive.index", np.array([[0, 1, 2]], dtype=np.uint32),
                 "ANARI_UINT32_VEC3")
if mode == "nonmanifold":
    mesh.replace("primitive.index", np.array([[0, 1, 2], [0, 1, 3], [0, 1, 2],
                                            [0, 0, 0]], dtype=np.uint32),
                 "ANARI_UINT32_VEC3")
if mode == "range":
    mesh.replace("primitive.index", np.array([[0, 1, 4], [0, 2, 3]],
                                           dtype=np.uint32), "ANARI_UINT32_VEC3")
if mode == "typed64":
    mesh.replace("vertex.position", np.array([
        [-0.4, -0.4, 0], [0.4, -0.4, 0], [0.4, 0.4, 0], [-0.4, 0.4, 0]
    ], dtype=np.float64), "ANARI_FLOAT64_VEC3")
    mesh.replace("primitive.index", np.array([[0, 1, 2], [0, 2, 3]],
                                           dtype=np.uint64), "ANARI_UINT64_VEC3")
    mesh.replace("vertex.color", np.tile(np.array([[0, 1, 254, 255]],
                                                dtype=np.uint8), (4, 1)),
                 "ANARI_UFIXED8_VEC4", enabled=False)
triangles = len(mesh.read("primitive.index"))
if mode != "retain-primitive":
    mesh.replace("primitive.color", np.tile(np.array([[1, 0, 0]], dtype=np.float32),
                                          (triangles, 1)), "ANARI_FLOAT32_VEC3")
mesh.alias("primitive.attribute1", "primitive.color", enabled=False)
mesh.replace("faceVarying.attribute2", np.arange(triangles * 6, dtype=np.int16)
             .reshape(triangles * 3, 2), "ANARI_INT16_VEC2", enabled=False)
mesh.replace("vertex.tangent", np.tile(np.array([[1, 0, 0, -1]], dtype=np.float64),
                                     (4, 1)), "ANARI_FLOAT64_VEC4", enabled=False)
if mode == "corner-count":
    mesh.replace("faceVarying.attribute2", np.zeros((triangles, 2), dtype=np.int16),
                 "ANARI_INT16_VEC2", enabled=False)
if mode == "index-off":
    mesh.replace("primitive.index", mesh.read("primitive.index"),
                 "ANARI_UINT32_VEC3", enabled=False)
if mode == "enabled":
    # Changing enabled state is explicit replacement, not preservation.
    mesh.replace("primitive.attribute1", mesh.read("primitive.color"),
                 "ANARI_FLOAT32_VEC3", enabled=True)
mesh.save(sys.argv[2])

# Documented VMESH01 envelope corruption for public refusal tests. This is not
# a convenience conversion or a native Archive/editor seam.
if mode.startswith("malformed-"):
    with open(sys.argv[2], "rb") as stream:
        carrier = stream.read()
    header_size = struct.unpack_from("<I", carrier, 8)[0]
    header = json.loads(carrier[12:12 + header_size])
    data = carrier[12 + header_size:]
    if mode == "malformed-preserve":
        header["fields"]["primitive.color"]["action"] = "preserve"
    elif mode == "malformed-field":
        header["fields"]["surface.geometry"] = header["fields"].pop("primitive.color")
    elif mode == "malformed-type":
        identifier = header["fields"]["vertex.position"]["payload"]
        header["arrays"][identifier]["element_type"] = "ANARI_INT32_VEC3"
    elif mode == "malformed-revision":
        header["revision"] = 2
    elif mode == "malformed-overflow":
        next(iter(header["arrays"].values()))["count"] = 2 ** 64
    elif mode == "malformed-length":
        next(iter(header["arrays"].values()))["byte_length"] += 1
    elif mode == "malformed-nonfinite":
        data = struct.pack("<f", float("nan")) + data[4:]
    encoded = json.dumps(header, separators=(",", ":")).encode()
    if mode == "malformed-duplicate":
        encoded = encoded.replace(b'"action":"replace"',
                                  b'"action":"replace","action":"remove"', 1)
    with open(sys.argv[2], "wb") as stream:
        stream.write(carrier[:8] + struct.pack("<I", len(encoded)) + encoded + data)
