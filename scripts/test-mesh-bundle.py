# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Behavioral tests for the documented typed bundle helper."""
import os
import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.environ.get('VSR_MESH_HELPER_PATH', str(pathlib.Path(__file__).resolve().parents[1] / "python")))
from vsr_mesh import Bundle
import numpy as np


class MeshBundleTests(unittest.TestCase):
    def test_numpy_edit_preserves_alias_and_disabled_field(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "working.vmesh"
            bundle = Bundle.create()
            points = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype='<f4')
            bundle.replace("vertex.position", points, "ANARI_FLOAT32_VEC3")
            bundle.alias("vertex.normal", "vertex.position", enabled=False)
            bundle.save(path)
            loaded = Bundle.load(path)
            self.assertEqual(loaded.read("vertex.normal").tobytes(), points.tobytes())
            self.assertFalse(loaded.inspect()["fields"]["vertex.normal"]["enabled"])
            changed = loaded.read("vertex.position")
            changed[:, 0] += 2
            loaded.write("vertex.position", changed)
            loaded.save(path)
            again = Bundle.load(path)
            self.assertEqual(again.read("vertex.normal").tolist(), [[2, 0, 0], [3, 0, 0], [2, 1, 0]])
            self.assertEqual(again.inspect()["fields"]["vertex.normal"]["payload"], again.inspect()["fields"]["vertex.position"]["payload"])
            with self.assertRaises(ValueError):
                again.write("vertex.position", changed.astype('<f8'))

    def test_explicit_alias_is_replacement_even_when_source_is_preserved(self):
        points = np.zeros((3,3), dtype='<f4')
        metadata = {'revision': 1, 'byte_order': 'little', 'arrays': {'a0': {'element_type': 'ANARI_FLOAT32_VEC3', 'count': 3, 'byte_length': 36}}, 'fields': {'vertex.position': {'action': 'preserve', 'enabled': True, 'association': 'vertex', 'payload': 'a0'}}}
        bundle = Bundle(metadata, {'a0': points.tobytes()})
        bundle.alias('vertex.normal', 'vertex.position')
        self.assertEqual(bundle.inspect()['fields']['vertex.normal']['action'], 'replace')
        self.assertEqual(bundle.inspect()['fields']['vertex.position']['action'], 'preserve')

    def test_refused_edit_leaves_helper_state_unchanged(self):
        bundle = Bundle.create()
        before = bundle.inspect()
        with self.assertRaises(ValueError):
            bundle.replace('vertex.unsupported', np.zeros(3, dtype='<f4'), 'ANARI_FLOAT32')
        self.assertEqual(bundle.inspect(), before)

    def test_all_numeric_representations_round_trip_untouched(self):
        bases = [('FLOAT', (32,64)), ('INT', (8,16,32,64)), ('UINT', (8,16,32,64)), ('FIXED', (8,16,32)), ('UFIXED', (8,16,32))]
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'types.vmesh'
            for kind, widths in bases:
                for width in widths:
                    for components in range(1,5):
                        element_type = f'ANARI_{kind}{width}' + (f'_VEC{components}' if components > 1 else '')
                        code = 'f' if kind == 'FLOAT' else ('u' if kind.startswith('U') else 'i')
                        values = np.arange(3 * components, dtype=f'<{code}{width//8}')
                        if components > 1:
                            values = values.reshape(3, components)
                        with self.subTest(element_type=element_type):
                            bundle = Bundle.create()
                            bundle.replace('vertex.attribute0', values, element_type)
                            bundle.alias('faceVarying.attribute1', 'vertex.attribute0', enabled=False)
                            bundle.save(path)
                            original = path.read_bytes()
                            loaded = Bundle.load(path)
                            self.assertEqual(loaded.read('vertex.attribute0').tobytes(), values.tobytes())
                            self.assertEqual(loaded.inspect()['arrays']['a0']['element_type'], element_type)
                            loaded.save(path)
                            self.assertEqual(path.read_bytes(), original)

    def test_payload_and_field_counts_are_bounded_before_edit(self):
        bundle = Bundle.create()
        values = np.zeros(1, dtype='<f4')
        for _ in range(64):
            bundle.replace('vertex.attribute0', values, 'ANARI_FLOAT32')
        before = bundle.inspect()
        with self.assertRaises(ValueError):
            bundle.replace('vertex.attribute0', values, 'ANARI_FLOAT32')
        self.assertEqual(bundle.inspect(), before)

    def test_decoder_refuses_incompatible_duplicate_and_truncated_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'bad.vmesh'
            headers = [b'{"revision":2,"byte_order":"little"}', b'{"revision":1,"revision":1}', b'{"revision":1,"byte_order":"big"}']
            for header in headers:
                path.write_bytes(b'VMESH01\n' + struct.pack('<I', len(header)) + header)
                with self.assertRaises(ValueError):
                    Bundle.load(path)
            path.write_bytes(b'VMESH01\n' + struct.pack('<I', 65537))
            with self.assertRaises(ValueError):
                Bundle.load(path)
            bundle = Bundle.create()
            bundle.replace('vertex.position', np.zeros((3,3), dtype='<f4'), 'ANARI_FLOAT32_VEC3')
            bundle.save(path)
            raw = path.read_bytes()
            for corrupted in (raw[:-1], raw + b'x'):
                path.write_bytes(corrupted)
                with self.assertRaises(ValueError):
                    Bundle.load(path)

    def test_optional_removal_is_explicit_and_links_are_not_overwritten(self):
        bundle = Bundle.create()
        bundle.remove('vertex.normal')
        self.assertEqual(bundle.inspect()['fields']['vertex.normal']['action'], 'remove')
        with self.assertRaises(ValueError):
            bundle.remove('vertex.position')
        with tempfile.TemporaryDirectory() as directory:
            original = pathlib.Path(directory) / 'asset'
            original.write_bytes(b'original')
            link = pathlib.Path(directory) / 'link.vmesh'
            link.symlink_to(original)
            with self.assertRaises(ValueError):
                bundle.save(link)
            self.assertEqual(original.read_bytes(), b'original')


if __name__ == '__main__':
    unittest.main()
