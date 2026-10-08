#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""External NumPy/Trimesh edit for the private mesh-device qualification square.

This is a fixture-specific correspondence policy, not a general UV/remesh mapper.
Never executed inside the privileged Mesh Import service.
"""
import argparse
import json
import os
import pathlib
import sys

import numpy as np
import trimesh

sys.path.insert(0, os.environ.get("VSR_MESH_HELPER_PATH", str(pathlib.Path(__file__).resolve().parents[1] / "python")))
from vsr_mesh import Bundle
from vsr_mesh_trimesh import Alias, Replacement, replace_topology, to_trimesh


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("round", type=int, choices=range(3))
    args = parser.parse_args()
    if pathlib.Path(args.input).resolve() == pathlib.Path(args.output).resolve() or os.path.lexists(args.output):
        parser.error("output must be a distinct new working artifact")
    source = Bundle.load(args.input)
    # Fail rather than apply this known correspondence to an arbitrary asset.
    np.testing.assert_array_equal(source.read("vertex.position"), [[-1, -1, 0], [1, -1, 0], [1, 1, 0], [-1, 1, 0]])
    np.testing.assert_array_equal(source.read("primitive.index"), [[0, 1, 2], [0, 2, 3]])
    np.testing.assert_array_equal(source.read("primitive.color"), [[1, 0, 0], [0, 1, 0]])
    tool, transfer = to_trimesh(source)
    if args.round == 1:
        # Existing vertices and faces keep their correspondence. An unused
        # vertex changes the upload cardinality while primitive bindings remain
        # EXACTLY preserved. The silhouette still changes through tool scaling.
        tool = trimesh.Trimesh(vertices=np.vstack([tool.vertices, [0, 0, 0]]), faces=tool.faces, process=False, validate=False)
        correspondence = "maintained"
    else:
        tool = tool.subdivide()
        correspondence = "lost"
    unscaled = np.asarray(tool.vertices).copy()
    resolutions = {
        "vertex.attribute0": Replacement(((unscaled[:, :2] + 1) / 2).astype("<f4"), "ANARI_FLOAT32_VEC2"),
        "vertex.normal": Replacement(np.tile(np.array([[0, 0, 1]], dtype="<f4"), (len(unscaled), 1)), "ANARI_FLOAT32_VEC3"),
    }
    if correspondence == "lost":
        # Explicit known planar-square correspondence, not generic inference.
        # Subdivision children stay on their original side of x=y; each child
        # receives the exact original parent's colour value.
        centers = unscaled[np.asarray(tool.faces)].mean(axis=1)
        parent = (centers[:, 1] > centers[:, 0]).astype(int)
        resolutions["primitive.color"] = Replacement(source.read("primitive.color")[parent].copy(), "ANARI_FLOAT32_VEC3")
        resolutions["primitive.attribute1"] = Alias("primitive.color", False)
    tool.apply_scale((0.375, 0.5, 0.625)[args.round])
    replacement, report = replace_topology(source, tool, correspondence=correspondence, resolutions=resolutions)
    replacement.save(args.output)
    print(json.dumps({"tool": "trimesh", "version": trimesh.__version__, "numpy": np.__version__, "round": args.round, "transfer": transfer, "replacement": report, "actions": replacement.inspect()["fields"]}, indent=2))


if __name__ == "__main__":
    main()
