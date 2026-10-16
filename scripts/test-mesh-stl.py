#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""External STL -> typed bundle workflow; not public Surface acceptance."""
import hashlib
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.environ.get("VSR_MESH_HELPER_PATH", str(pathlib.Path(__file__).resolve().parents[1] / "python")))
from vsr_mesh import Bundle


class StlBundleTests(unittest.TestCase):
    def test_invalid_empty_and_multi_solid_inputs_publish_no_bundle(self):
        script = pathlib.Path(os.environ.get("VSR_MESH_EXAMPLE_PATH", pathlib.Path(__file__).parent)) / "mesh-stl-to-bundle.py"
        solid = b"""solid part
facet normal 0 0 1
outer loop
vertex 0 0 0
vertex 1 0 0
vertex 0 1 0
endloop
endfacet
endsolid part
"""
        for data in (b"not STL", b"solid empty\nendsolid empty\n", solid + solid,
                     solid.replace(b"vertex 1", b"vertex nan")):
            with self.subTest(data=data), tempfile.TemporaryDirectory() as directory:
                source = pathlib.Path(directory) / "rejected.stl"
                output = pathlib.Path(directory) / "rejected.vmesh"
                source.write_bytes(data)
                completed = subprocess.run([sys.executable, str(script), str(source), str(output)], capture_output=True, text=True)
                self.assertNotEqual(completed.returncode, 0)
                if data == solid + solid:
                    self.assertIn("one nonempty STL solid", completed.stderr)
                self.assertFalse(output.exists())
                self.assertEqual(source.read_bytes(), data)
                self.assertEqual(sorted(p.name for p in pathlib.Path(directory).iterdir()), ["rejected.stl"])

    def test_original_path_and_output_links_are_never_written(self):
        script = pathlib.Path(os.environ.get("VSR_MESH_EXAMPLE_PATH", pathlib.Path(__file__).parent)) / "mesh-stl-to-bundle.py"
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "original.stl"
            source.write_bytes(b"original asset is not a mesh bundle")
            link = pathlib.Path(directory) / "working.vmesh"
            link.symlink_to(source)
            dangling = pathlib.Path(directory) / "dangling.vmesh"
            dangling.symlink_to(pathlib.Path(directory) / "missing")
            for output in (source, link, dangling):
                completed = subprocess.run([sys.executable, str(script), str(source), str(output)], capture_output=True, text=True)
                self.assertNotEqual(completed.returncode, 0)
                self.assertEqual(source.read_bytes(), b"original asset is not a mesh bundle")
            self.assertTrue(link.is_symlink())
            self.assertTrue(dangling.is_symlink())
            self.assertFalse((pathlib.Path(directory) / "missing").exists())

    def test_ascii_stl_precision_loss_is_an_explicit_decision(self):
        script = pathlib.Path(os.environ.get("VSR_MESH_EXAMPLE_PATH", pathlib.Path(__file__).parent)) / "mesh-stl-to-bundle.py"
        source_bytes = b"""solid authored
facet normal 0 0 1
outer loop
vertex 0.1 0 0
vertex 1 0 0
vertex 0 1 0
endloop
endfacet
endsolid authored
"""
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "decimal.stl"
            output = pathlib.Path(directory) / "decimal.vmesh"
            source.write_bytes(source_bytes)
            command = [sys.executable, str(script), str(source), str(output)]
            refused = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn("precision loss", refused.stderr)
            self.assertFalse(output.exists())
            accepted = subprocess.run(command + ["--allow-precision-loss"], capture_output=True, text=True)
            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            self.assertTrue(json.loads(accepted.stdout)["replacement"]["precision_loss"])
            self.assertEqual(Bundle.load(output).read("primitive.index").tolist(), [[0, 1, 2]])
            self.assertEqual(source.read_bytes(), source_bytes)

    def test_binary_stl_retains_units_facet_order_and_reports_normal_loss(self):
        script = pathlib.Path(os.environ.get("VSR_MESH_EXAMPLE_PATH", pathlib.Path(__file__).parent)) / "mesh-stl-to-bundle.py"
        # Hand-authored binary STL, not produced by the converter under test.
        # Duplicate/degenerate triangles must not be welded or repaired.
        facet = struct.pack("<12fH", 0, 0, -1, 10, 20, 30, 12, 20, 30, 10, 23, 30, 0x801F)
        degenerate = struct.pack("<12fH", 0, 0, 0, *([10, 20, 30] * 3), 0)
        source_bytes = b"hand authored facet control".ljust(80, b"\0") + struct.pack("<I", 3) + facet + facet + degenerate
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "source.stl"
            output = pathlib.Path(directory) / "working.vmesh"
            source.write_bytes(source_bytes)
            command = [sys.executable, str(script), str(source), str(output)]
            completed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            report = json.loads(completed.stdout)
            bundle = Bundle.load(output)
            self.assertNotIn("source", bundle.inspect())
            self.assertEqual(bundle.read("primitive.index").tolist(), [[0, 1, 2], [3, 4, 5], [6, 7, 8]])
            self.assertEqual(bundle.read("vertex.position").tolist(), [[10, 20, 30], [12, 20, 30], [10, 23, 30]] * 2 + [[10, 20, 30]] * 3)
            self.assertEqual(bundle.read("vertex.position").dtype, np.dtype("<f4"))
            self.assertEqual(set(bundle.inspect()["fields"]), {"vertex.position", "primitive.index"})
            self.assertIn("STL facet normals", report["omitted_source_semantics"])
            self.assertIn("face_attributes.stl", report["replacement"]["discarded_tool_attributes"])
            self.assertIn("metadata", report["replacement"]["discarded_tool_attributes"])
            self.assertFalse(report["replacement"]["precision_loss"])
            self.assertEqual(report["source_sha256"], hashlib.sha256(source_bytes).hexdigest())
            self.assertEqual(report["triangles"], 3)
            self.assertEqual(source.read_bytes(), source_bytes)
            output_bytes = output.read_bytes()
            refused = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn("new working file", refused.stderr)
            self.assertEqual(output.read_bytes(), output_bytes)
            self.assertEqual(source.read_bytes(), source_bytes)


if __name__ == "__main__":
    unittest.main()
