#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Behavioral tests for the topology-only external-tool adapter."""
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

import numpy as np
import trimesh

sys.path.insert(0, os.environ.get("VSR_MESH_HELPER_PATH", str(pathlib.Path(__file__).resolve().parents[1] / "python")))
from vsr_mesh import Bundle
from vsr_mesh_trimesh import to_trimesh, replace_topology, Replacement, Alias


class TrimeshExchangeTests(unittest.TestCase):
    def triangle(self):
        bundle = Bundle.create()
        bundle.replace("vertex.position", np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype="<f4"), "ANARI_FLOAT32_VEC3")
        bundle.replace("primitive.index", np.array([[0, 1, 2]], dtype="<u4"), "ANARI_UINT32_VEC3")
        bundle.replace("vertex.color", np.array([[0, 1, 254, 255]] * 3, dtype="u1"), "ANARI_UFIXED8_VEC4", enabled=False)
        bundle.alias("vertex.attribute0", "vertex.color", enabled=False)
        return bundle

    def test_real_subdivision_requires_explicit_attribute_loss_resolution(self):
        source = self.triangle()
        before = source.inspect()
        tool, report = to_trimesh(source)
        self.assertEqual(tool.faces.tolist(), [[0, 1, 2]])
        self.assertEqual(report["unsupported_fields"], ["vertex.color", "vertex.attribute0"])
        self.assertIn("float32 -> float64", report["representation_changes"][0])
        subdivided = tool.subdivide()
        self.assertEqual(len(subdivided.vertices), 6)
        self.assertEqual(len(subdivided.faces), 4)
        with self.assertRaisesRegex(ValueError, "unresolved.*vertex.color"):
            replace_topology(source, subdivided, correspondence="lost")
        result, losses = replace_topology(source, subdivided, correspondence="lost", resolutions={"vertex.color": "remove", "vertex.attribute0": "remove"})
        self.assertEqual(result.read("vertex.position").dtype, np.dtype("<f4"))
        self.assertEqual(result.read("primitive.index").shape, (4, 3))
        self.assertEqual(losses["discarded_fields"], ["vertex.color", "vertex.attribute0"])
        self.assertEqual(source.inspect(), before)
        self.assertEqual(source.read("vertex.position").tolist(), [[0, 0, 0], [1, 0, 0], [0, 1, 0]])

    def test_deliberately_maintained_correspondence_keeps_exact_semantics_and_aliases(self):
        source = self.triangle()
        tool, _ = to_trimesh(source)
        tool.vertices[:, 0] += 2
        result, report = replace_topology(source, tool, correspondence="maintained")
        self.assertEqual(report["discarded_fields"], [])
        self.assertEqual(result.read("vertex.color").tobytes(), source.read("vertex.color").tobytes())
        fields = result.inspect()["fields"]
        self.assertEqual(fields["vertex.color"]["payload"], fields["vertex.attribute0"]["payload"])
        self.assertFalse(fields["vertex.color"]["enabled"])
        self.assertEqual(result.inspect()["arrays"][fields["vertex.color"]["payload"]]["element_type"], "ANARI_UFIXED8_VEC4")
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "result.vmesh"
            result.save(path)
            self.assertEqual(Bundle.load(path).read("vertex.color").tobytes(), source.read("vertex.color").tobytes())

    def test_remapped_replacements_and_aliases_are_explicit(self):
        source = self.triangle()
        tool, _ = to_trimesh(source)
        values = np.array([[255, 0, 0, 255]] * 6, dtype="u1")
        result, report = replace_topology(source, tool.subdivide(), correspondence="lost", resolutions={"vertex.color": Replacement(values, "ANARI_UFIXED8_VEC4", False), "vertex.attribute0": Alias("vertex.color", False)})
        self.assertEqual(result.read("vertex.attribute0").tobytes(), values.tobytes())
        self.assertEqual(report["replaced_fields"], ["vertex.color", "vertex.attribute0"])
        self.assertEqual(report["discarded_fields"], [])
        with self.assertRaisesRegex(ValueError, "lost correspondence"):
            replace_topology(source, tool, correspondence="lost", resolutions={"vertex.color": "preserve", "vertex.attribute0": "remove"})

    def test_nonindexed_triangles_and_precision_changes_are_not_silent(self):
        source = self.triangle()
        source.remove("primitive.index")
        tool, report = to_trimesh(source)
        self.assertEqual(tool.faces.tolist(), [[0, 1, 2]])
        self.assertIn("implicit triangles -> explicit indices", report["representation_changes"])
        tool.vertices[0, 0] = 1.0 / 3.0
        with self.assertRaisesRegex(ValueError, "precision"):
            replace_topology(source, tool, correspondence="maintained")
        result, report = replace_topology(source, tool, correspondence="maintained", allow_precision_loss=True)
        self.assertTrue(report["precision_loss"])
        self.assertEqual(result.inspect()["fields"]["primitive.index"]["action"], "replace")

    def test_topology_alias_splits_require_deliberate_resolution(self):
        source = self.triangle()
        source.alias("vertex.normal", "vertex.position", enabled=False)
        tool, _ = to_trimesh(source)
        with self.assertRaisesRegex(ValueError, "topology aliases"):
            replace_topology(source, tool, correspondence="maintained")
        result, report = replace_topology(source, tool, correspondence="maintained", resolutions={"vertex.normal": "preserve"})
        self.assertEqual(report["split_topology_aliases"], ["vertex.normal"])
        self.assertEqual(result.read("vertex.normal").tobytes(), source.read("vertex.position").tobytes())
        self.assertNotEqual(result.inspect()["fields"]["vertex.normal"]["payload"], result.inspect()["fields"]["vertex.position"]["payload"])

    def test_invalid_tool_results_and_false_correspondence_do_not_publish(self):
        source = self.triangle()
        tool, _ = to_trimesh(source)
        before = source.inspect()
        with self.assertRaisesRegex(ValueError, "attribute count"):
            replace_topology(source, tool.subdivide(), correspondence="maintained")
        tool.faces[0, 2] = 3
        with self.assertRaisesRegex(ValueError, "range"):
            replace_topology(source, tool, correspondence="maintained")
        self.assertEqual(source.inspect(), before)
        with self.assertRaisesRegex(ValueError, "chains/cycles"):
            replace_topology(source, tool, correspondence="lost", resolutions={"vertex.color": Alias("vertex.attribute0"), "vertex.attribute0": Alias("vertex.color")})

    def test_independent_example_never_reads_or_overwrites_an_original_asset(self):
        script = pathlib.Path(os.environ.get("VSR_MESH_EXAMPLE_PATH", pathlib.Path(__file__).parent)) / "mesh-tool-replacement.py"
        with tempfile.TemporaryDirectory() as directory:
            original = pathlib.Path(directory) / "original.obj"
            original.write_bytes(b"not a mesh bundle: original source asset")
            output = pathlib.Path(directory) / "replacement.vmesh"
            command = [sys.executable, str(script), str(original), str(output), "--independent"]
            completed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            authored = Bundle.load(output)
            self.assertNotIn("source", authored.inspect())
            self.assertEqual(authored.read("vertex.position").shape, (4, 3))
            self.assertEqual(authored.read("primitive.index").tolist(), [[0, 1, 2], [0, 2, 3]])
            self.assertEqual(original.read_bytes(), b"not a mesh bundle: original source asset")
            refused = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn("new working file", refused.stderr)
            self.assertEqual(original.read_bytes(), b"not a mesh bundle: original source asset")

    def test_numpy_example_keeps_exact_types_and_declares_alias_splits(self):
        script = pathlib.Path(os.environ.get("VSR_MESH_EXAMPLE_PATH", pathlib.Path(__file__).parent)) / "mesh-numpy-edit.py"
        source = self.triangle()
        source.alias("vertex.normal", "vertex.position", enabled=False)
        with tempfile.TemporaryDirectory() as directory:
            original = pathlib.Path(directory) / "export.vmesh"
            output = pathlib.Path(directory) / "edit.vmesh"
            source.save(original)
            original_bytes = original.read_bytes()
            completed = subprocess.run([sys.executable, str(script), str(original), str(output), "--offset-x", "2"], capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            edited = Bundle.load(output)
            self.assertEqual(edited.read("vertex.position").tolist(), [[2, 0, 0], [3, 0, 0], [2, 1, 0]])
            self.assertEqual(edited.read("vertex.position").dtype, np.dtype("<f4"))
            self.assertEqual(edited.read("vertex.normal").tobytes(), source.read("vertex.normal").tobytes())
            self.assertEqual(edited.read("vertex.color").tobytes(), source.read("vertex.color").tobytes())
            self.assertEqual(original.read_bytes(), original_bytes)
            self.assertIn("split position binding only", completed.stdout)
            shared = pathlib.Path(directory) / "shared.vmesh"
            completed = subprocess.run([sys.executable, str(script), str(original), str(shared), "--offset-x", "2", "--shared-payload"], capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            aliased = Bundle.load(shared)
            self.assertEqual(aliased.read("vertex.normal").tolist(), [[2, 0, 0], [3, 0, 0], [2, 1, 0]])
            self.assertEqual(aliased.inspect()["fields"]["vertex.normal"]["payload"], aliased.inspect()["fields"]["vertex.position"]["payload"])
            self.assertEqual(original.read_bytes(), original_bytes)

    def test_loss_acknowledgements_must_be_boolean_and_normals_are_reported(self):
        source = self.triangle()
        tool, _ = to_trimesh(source)
        tool.vertex_normals = np.array([[0, 0, 1]] * 3, dtype="<f8")
        for flag in ("allow_precision_loss", "discard_tool_attributes"):
            with self.subTest(flag=flag), self.assertRaisesRegex(ValueError, "boolean"):
                replace_topology(source, tool, correspondence="maintained", **{flag: "false"})
        _, report = replace_topology(source, tool, correspondence="maintained")
        self.assertIn("vertex_normals", report["ignored_tool_semantics"])
        self.assertEqual(report["field_changes"]["vertex.color"]["after_type"], "ANARI_UFIXED8_VEC4")
        self.assertEqual(report["field_changes"]["vertex.color"]["action"], "retain exact")

    def test_unprocessed_tool_mesh_keeps_degenerate_and_duplicate_connectivity(self):
        source = self.triangle()
        source.replace("primitive.index", np.array([[0, 1, 2], [0, 1, 2], [0, 0, 0]], dtype="<u4"), "ANARI_UINT32_VEC3")
        tool, _ = to_trimesh(source)
        self.assertEqual(tool.faces.tolist(), [[0, 1, 2], [0, 1, 2], [0, 0, 0]])
        result, _ = replace_topology(source, tool, correspondence="maintained")
        self.assertEqual(result.read("primitive.index").tolist(), [[0, 1, 2], [0, 1, 2], [0, 0, 0]])
        self.assertEqual(result.read("vertex.position").tolist(), [[0, 0, 0], [1, 0, 0], [0, 1, 0]])

    def test_tool_attributes_never_silently_replace_bundle_attributes(self):
        source = self.triangle()
        tool, _ = to_trimesh(source)
        tool.visual.vertex_colors = [10, 20, 30, 255]
        tool.vertex_attributes["foreign_uv"] = np.zeros((3, 2))
        with self.assertRaisesRegex(ValueError, "tool-only"):
            replace_topology(source, tool, correspondence="maintained")
        _, report = replace_topology(source, tool, correspondence="maintained", discard_tool_attributes=True)
        self.assertIn("visual", report["discarded_tool_attributes"])
        self.assertIn("vertex_attributes.foreign_uv", report["discarded_tool_attributes"])


if __name__ == "__main__":
    unittest.main()
