#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Selective Geometry-local NumPy edit; no modeling inside the host."""
import argparse
import json
import os
import pathlib
import sys

sys.path.insert(0, os.environ.get("VSR_MESH_HELPER_PATH", str(pathlib.Path(__file__).resolve().parents[1] / "python")))
from vsr_mesh import Bundle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--offset-x", type=float, default=0.1)
    parser.add_argument("--shared-payload", action="store_true", help="deliberately edit all position payload aliases instead of splitting positions")
    args = parser.parse_args()
    if pathlib.Path(args.input).resolve() == pathlib.Path(args.output).resolve() or os.path.lexists(args.output):
        parser.error("output must be a new, distinct working file")
    bundle = Bundle.load(args.input)
    metadata = bundle.inspect()
    field = metadata["fields"]["vertex.position"]
    aliases = [name for name, value in metadata["fields"].items() if name != "vertex.position" and value.get("payload") == field["payload"]]
    positions = bundle.read("vertex.position")
    positions[:, 0] += args.offset_x
    if args.shared_payload:
        bundle.write("vertex.position", positions)
    else:
        bundle.replace("vertex.position", positions, metadata["arrays"][field["payload"]]["element_type"])
    bundle.save(args.output)
    print(json.dumps({"correspondence": "maintained by deliberate translation", "position_aliases": aliases, "alias_action": "replace shared payload" if args.shared_payload else "split position binding only", "discarded_fields": [], "actions": bundle.inspect()["fields"]}, indent=2))


if __name__ == "__main__":
    main()
