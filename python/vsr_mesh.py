# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Lossless Vela mesh bundle revision 1. See docs/mesh-exchange.md."""
import copy
import json
import os
import re
import struct
import tempfile

import numpy as np

MAGIC = b'VMESH01\n'
MAX_BYTES = 64 << 20
MAX_HEADER = 65536
MAX_FIELDS = 64


def representation(element_type):
    match = re.fullmatch(r'ANARI_(FLOAT|U?INT|U?FIXED)(8|16|32|64)(?:_VEC([234]))?', element_type)
    if not match:
        raise ValueError('unsupported element type')
    kind, width, components = match.groups()
    width = int(width)
    if kind == 'FLOAT' and width not in (32, 64):
        raise ValueError('unsupported floating representation')
    if 'FIXED' in kind and width not in (8, 16, 32):
        raise ValueError('unsupported fixed representation')
    code = 'f' if kind == 'FLOAT' else ('u' if kind.startswith('U') else 'i')
    return np.dtype('<' + code + str(width // 8)), int(components or 1)


def association(name):
    if name == 'primitive.index':
        return 'index'
    match = re.fullmatch(r'(vertex|primitive|faceVarying)\.(position|normal|tangent|color|attribute[0-3])', name)
    if not match or (match[2] == 'position' and match[1] != 'vertex'):
        raise ValueError('unsupported mesh field')
    return {'faceVarying': 'corner'}.get(match[1], match[1])


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate declaration')
        result[key] = value
    return result


class Bundle:
    def __init__(self, metadata, payloads):
        self._metadata = metadata
        self._payloads = payloads

    @classmethod
    def create(cls):
        return cls({'revision': 1, 'byte_order': 'little', 'fields': {}, 'arrays': {}}, {})

    @classmethod
    def load(cls, path):
        with open(path, 'rb') as stream:
            if stream.read(8) != MAGIC:
                raise ValueError('unsupported bundle')
            length_bytes = stream.read(4)
            if len(length_bytes) != 4:
                raise ValueError('truncated header')
            length, = struct.unpack('<I', length_bytes)
            if length > MAX_HEADER:
                raise ValueError('header limit')
            header = stream.read(length)
            metadata = json.loads(header, object_pairs_hook=unique_object)
            if metadata.get('revision') != 1 or metadata.get('byte_order') != 'little':
                raise ValueError('incompatible revision or byte order')
            fields, arrays = metadata['fields'], metadata['arrays']
            if len(fields) > MAX_FIELDS or len(arrays) > MAX_FIELDS:
                raise ValueError('field limit')
            payloads = {}
            total = 0
            for identifier, array in arrays.items():
                dtype, components = representation(array['element_type'])
                count = array['count']
                if type(count) is not int or count <= 0:
                    raise ValueError('invalid count')
                length = count * components * dtype.itemsize
                total += length
                if total > MAX_BYTES or array['byte_length'] != length:
                    raise ValueError('payload limit or length mismatch')
                payload = stream.read(length)
                if len(payload) != length:
                    raise ValueError('truncated payload')
                payloads[identifier] = payload
            if stream.read(1):
                raise ValueError('trailing bytes')
            for name, field in fields.items():
                if field['association'] != association(name) or type(field['enabled']) is not bool:
                    raise ValueError('invalid field semantics')
                if field['action'] not in ('preserve', 'replace', 'remove'):
                    raise ValueError('invalid action')
                if field['action'] != 'remove' and field['payload'] not in arrays:
                    raise ValueError('missing payload')
            return cls(metadata, payloads)

    def inspect(self):
        return copy.deepcopy(self._metadata)

    def read(self, name):
        identifier = self._metadata['fields'][name]['payload']
        array = self._metadata['arrays'][identifier]
        dtype, components = representation(array['element_type'])
        result = np.frombuffer(self._payloads[identifier], dtype=dtype).copy()
        return result.reshape((array['count'], components)) if components > 1 else result

    def write(self, name, values):
        """Write one payload, updating all aliases explicitly to replace intent."""
        identifier = self._metadata['fields'][name]['payload']
        array = self._metadata['arrays'][identifier]
        dtype, components = representation(array['element_type'])
        expected_shape = (array['count'], components) if components > 1 else (array['count'],)
        if values.dtype != dtype or values.shape != expected_shape:
            raise ValueError('write requires exact dtype and shape; use replace for count changes')
        self._payloads[identifier] = values.tobytes(order='C')
        for field in self._metadata['fields'].values():
            if field.get('payload') == identifier:
                field['action'] = 'replace'

    def replace(self, name, values, element_type, enabled=True):
        associated = association(name)
        if type(enabled) is not bool:
            raise ValueError('enabled must be boolean')
        if len(self._metadata['arrays']) >= MAX_FIELDS or (name not in self._metadata['fields'] and len(self._metadata['fields']) >= MAX_FIELDS):
            raise ValueError('field/payload limit')
        dtype, components = representation(element_type)
        if values.dtype != dtype or values.ndim != (2 if components > 1 else 1) or (components > 1 and values.shape[1] != components):
            raise ValueError('exact type/component representation required')
        if not values.shape[0] or values.nbytes > MAX_BYTES - sum(map(len, self._payloads.values())):
            raise ValueError('payload limit')
        identifier = 'a' + str(len(self._metadata['arrays']))
        self._metadata['arrays'][identifier] = {'element_type': element_type, 'count': values.shape[0], 'byte_length': values.nbytes}
        self._payloads[identifier] = values.tobytes(order='C')
        self._metadata['fields'][name] = {'action': 'replace', 'payload': identifier, 'enabled': enabled, 'association': associated}

    def alias(self, name, source, enabled=True):
        associated = association(name)
        if type(enabled) is not bool or (name not in self._metadata['fields'] and len(self._metadata['fields']) >= MAX_FIELDS):
            raise ValueError('field limit or invalid enabled state')
        field = self._metadata['fields'][source]
        if field['action'] == 'remove':
            raise ValueError('cannot alias a removed field')
        self._metadata['fields'][name] = dict(field, action='replace', enabled=enabled, association=associated)

    def remove(self, name):
        associated = association(name)
        if name not in self._metadata['fields'] and len(self._metadata['fields']) >= MAX_FIELDS:
            raise ValueError('field limit')
        if name == 'vertex.position':
            raise ValueError('positions required')
        self._metadata['fields'][name] = {'action': 'remove', 'enabled': False, 'association': associated}

    def save(self, path):
        """Atomic local working-file replacement; never follows a destination link."""
        header = json.dumps(self._metadata, separators=(',', ':'), allow_nan=False).encode()
        if len(header) > MAX_HEADER or len(self._metadata['fields']) > MAX_FIELDS or sum(map(len, self._payloads.values())) > MAX_BYTES:
            raise ValueError('bundle limit')
        path = os.fspath(path)
        if os.path.islink(path):
            raise ValueError('destination link')
        fd, temporary = tempfile.mkstemp(prefix='.vmesh-', dir=os.path.dirname(os.path.abspath(path)))
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(MAGIC + struct.pack('<I', len(header)) + header)
                for identifier in self._metadata['arrays']:
                    stream.write(self._payloads[identifier])
            os.replace(temporary, path)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
