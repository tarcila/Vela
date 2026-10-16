#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Convert an STL with existing Trimesh into a new topology-only working bundle.

External-process example, NOT a privileged importer or Surface creation command.
Reads only the selected input; never writes it. No repair, welding or unit guess.
"""
import argparse
import hashlib
import io
import json
import os
import pathlib
import sys
import tempfile

import trimesh
from trimesh.exchange.stl import load_stl

sys.path.insert(0, os.environ.get("VSR_MESH_HELPER_PATH", str(pathlib.Path(__file__).resolve().parents[1] / "python")))
from vsr_mesh import Bundle, MAX_BYTES
from vsr_mesh_trimesh import replace_topology


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="actual ASCII or binary STL; opened read-only by Trimesh")
    parser.add_argument("output", help="new .vmesh working file, never an original asset")
    parser.add_argument("--allow-precision-loss", action="store_true",
                        help="explicitly accept reported float64 -> float32 rounding")
    args = parser.parse_args()
    if pathlib.Path(args.input).resolve() == pathlib.Path(args.output).resolve():
        parser.error("use a distinct output working file")
    if os.path.lexists(args.output):
        parser.error("output must be a new working file, not an existing asset")
    if pathlib.Path(args.output).suffix != ".vmesh":
        parser.error("output must have .vmesh suffix")
    with open(args.input, "rb") as stream:
        source = stream.read(MAX_BYTES + 1)
    if len(source) > MAX_BYTES:
        parser.error("STL input exceeds 64 MiB example limit")
    # Explicit STL-only reader: no generic format dispatch, dependency resolver,
    # scene concatenation, automatic repair or foreign material conversion.
    loaded = load_stl(io.BytesIO(source))
    if "geometry" in loaded:
        raise ValueError("one nonempty STL solid required; no scene concatenation")
    tool = trimesh.Trimesh(**loaded, process=False, validate=False)
    bundle, replacement = replace_topology(
        Bundle.create(), tool, correspondence="lost",
        position_type="ANARI_FLOAT32_VEC3", index_type="ANARI_UINT32_VEC3",
        discard_tool_attributes=True,
        allow_precision_loss=args.allow_precision_loss)
    report = {
        "tool": "trimesh.exchange.stl.load_stl",
        "version": trimesh.__version__,
        "source_sha256": hashlib.sha256(source).hexdigest(),
        "triangles": len(tool.faces),
        "semantics": "unprocessed STL facet order and coordinates; authored units unchanged",
        "omitted_source_semantics": [
            "STL facet normals", "STL header/solid labels",
            "STL attribute-byte/color extensions (not interpreted)"],
        "replacement": replacement,
        "actions": bundle.inspect()["fields"],
    }
    # Complete candidate/report first, then publish exclusively. A competing
    # creator must never turn this example into an overwrite of another asset.
    # This local-file policy is not privileged host authority/confinement.
    text = json.dumps(report, indent=2, allow_nan=False)
    parent = pathlib.Path(args.output).absolute().parent
    with tempfile.TemporaryDirectory(prefix=".stl-bundle-", dir=parent) as staging:
        candidate = pathlib.Path(staging) / "candidate.vmesh"
        bundle.save(candidate)
        os.link(candidate, args.output)
    print(text)


if __name__ == "__main__":
    main()
