#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Reproducible external-tool example; run outside the privileged host."""
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
    parser.add_argument("input", help="exported working bundle (unused in independent mode)")
    parser.add_argument("output", help="new confined working bundle, never a source asset")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--independent", action="store_true")
    modes.add_argument("--maintained", action="store_true", help="translate without topology/count changes; deliberately retain attributes")
    args = parser.parse_args()
    if pathlib.Path(args.input).resolve() == pathlib.Path(args.output).resolve():
        parser.error("use a distinct output working file")
    if os.path.lexists(args.output):
        parser.error("output must be a new working file, not an existing asset")

    if args.independent:
        # Independently authored, not derived from the input file or its source.
        source = Bundle.create()
        tool = trimesh.Trimesh(vertices=[[-.4, -.4, 0], [.4, -.4, 0], [.4, .4, 0], [-.4, .4, 0]], faces=[[0, 1, 2], [0, 2, 3]], process=False)
        transfer = {"unsupported_fields": [], "representation_changes": []}
        resolutions = {}
    else:
        source = Bundle.load(args.input)
        tool, transfer = to_trimesh(source)
        # The LLM chooses the operation and correspondence policy. Subdivision
        # invalidates ordinary vertex/primitive attribute correspondence.
        if args.maintained:
            tool.apply_translation([10, 0, 0])
            resolutions = {}
        else:
            tool = tool.subdivide()
            tool.apply_scale(.4)
            resolutions = {name: "remove" for name in transfer["unsupported_fields"]}
    if not args.maintained:
        # Explicitly authored per-primitive colour and alias, not inferred mapping.
        resolutions["primitive.color"] = Replacement(np.tile(np.array([[1, 0, 0]], dtype="<f4"), (len(tool.faces), 1)), "ANARI_FLOAT32_VEC3")
        resolutions["primitive.attribute1"] = Alias("primitive.color", False)
    replacement, losses = replace_topology(source, tool, correspondence="maintained" if args.maintained else "lost", resolutions=resolutions, allow_precision_loss=not args.maintained)
    if args.independent:
        # Explicit target binding action, not omission-as-deletion. The agent
        # knows this field from target inspection/export, not foreign metadata.
        replacement.remove("vertex.attribute0")
        losses["discarded_fields"].append("vertex.attribute0")
        assert "source" not in replacement.inspect()
    replacement.save(args.output)
    print(json.dumps({"tool": "trimesh", "version": trimesh.__version__, "transfer": transfer, "replacement": losses, "actions": replacement.inspect()["fields"]}, indent=2))


if __name__ == "__main__":
    main()
