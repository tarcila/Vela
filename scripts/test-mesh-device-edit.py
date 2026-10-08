#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Real helper/CLI controls for the fixture-specific qualification edit."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, os.environ.get("VSR_MESH_HELPER_PATH", str(ROOT / "python")))
from vsr_mesh import Bundle


class QualificationEditTests(unittest.TestCase):
    def square(self):
        bundle = Bundle.create()
        bundle.replace("vertex.position", np.array([[-1, -1, 0], [1, -1, 0], [1, 1, 0], [-1, 1, 0]], dtype="<f4"), "ANARI_FLOAT32_VEC3")
        bundle.replace("primitive.index", np.array([[0, 1, 2], [0, 2, 3]], dtype="<u4"), "ANARI_UINT32_VEC3")
        bundle.replace("vertex.attribute0", np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype="<f4"), "ANARI_FLOAT32_VEC2")
        bundle.replace("vertex.normal", np.tile(np.array([[0, 0, 1]], dtype="<f4"), (4, 1)), "ANARI_FLOAT32_VEC3")
        bundle.replace("primitive.color", np.array([[1, 0, 0], [0, 1, 0]], dtype="<f4"), "ANARI_FLOAT32_VEC3")
        bundle.alias("primitive.attribute1", "primitive.color", enabled=False)
        return bundle

    def run_edit(self, source, output, round):
        examples = pathlib.Path(os.environ.get("VSR_MESH_EXAMPLE_PATH", str(ROOT / "scripts")))
        return subprocess.run([sys.executable, str(examples / "mesh-device-edit.py"), str(source), str(output), str(round)], capture_output=True, text=True)

    def test_all_rounds_preserve_known_appearance_and_report_actions(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "original.vmesh"
            self.square().save(source)
            original = source.read_bytes()
            for round, vertices, faces in ((0, 9, 8), (1, 5, 2), (2, 9, 8)):
                with self.subTest(round=round):
                    output = pathlib.Path(directory) / f"edit-{round}.vmesh"
                    result = self.run_edit(source, output, round)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    bundle = Bundle.load(output)
                    self.assertEqual(bundle.read("vertex.position").shape, (vertices, 3))
                    self.assertEqual(bundle.read("primitive.index").shape, (faces, 3))
                    self.assertEqual(bundle.read("vertex.attribute0").shape, (vertices, 2))
                    np.testing.assert_array_equal(bundle.read("vertex.normal"), np.tile([[0, 0, 1]], (vertices, 1)))
                    np.testing.assert_array_equal(np.unique(bundle.read("primitive.color"), axis=0), [[0, 1, 0], [1, 0, 0]])
                    fields = bundle.inspect()["fields"]
                    self.assertEqual(fields["primitive.color"]["payload"], fields["primitive.attribute1"]["payload"])
                    self.assertFalse(fields["primitive.attribute1"]["enabled"])
                    if round == 1:
                        # Export's explicit replacement actions remain intact;
                        # equivalent payloads retain live bindings at Import.
                        report = json.loads(result.stdout)["replacement"]
                        self.assertIn("primitive.color", report["retained_fields"])
                        self.assertEqual(report["field_changes"]["primitive.color"]["action"], "retain exact")
                        np.testing.assert_array_equal(bundle.read("primitive.color"), [[1, 0, 0], [0, 1, 0]])
                    self.assertEqual(source.read_bytes(), original)
                    self.assertNotEqual(self.run_edit(source, output, round).returncode, 0)
                    self.assertEqual(source.read_bytes(), original)

    def test_refuses_a_different_asset_without_publishing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "different.vmesh"
            output = pathlib.Path(directory) / "must-not-exist.vmesh"
            bundle = self.square()
            bundle.replace("vertex.position", bundle.read("vertex.position") * 2, "ANARI_FLOAT32_VEC3")
            bundle.save(source)
            original = source.read_bytes()
            self.assertNotEqual(self.run_edit(source, output, 0).returncode, 0)
            self.assertFalse(output.exists())
            self.assertEqual(source.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
