# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit topology-only Trimesh adapter. See mesh-tool-cookbook.md.

No visual/normal/UV conversion, repair, welding or correspondence inference.
Reports are returned separately, never confused with bundle source authority.
"""
import copy
from dataclasses import dataclass

import numpy as np

from vsr_mesh import MAX_BYTES, association, representation


@dataclass(frozen=True)
class Replacement:
    values: np.ndarray
    element_type: str
    enabled: bool = True


@dataclass(frozen=True)
class Alias:
    source: str
    enabled: bool = True


def _topology(vertices, faces):
    vertices, faces = np.asarray(vertices), np.asarray(faces)
    if (vertices.ndim != 2 or vertices.shape[1] != 3 or not len(vertices)
            or vertices.dtype.kind != "f" or not np.isfinite(vertices).all()):
        raise ValueError("finite nonempty floating Nx3 positions required")
    if (faces.ndim != 2 or faces.shape[1] != 3 or not len(faces)
            or faces.dtype.kind not in "iu"):
        raise ValueError("nonempty integer Mx3 triangle indices required")
    if vertices.nbytes + faces.nbytes > MAX_BYTES:
        raise ValueError("topology exceeds helper payload limit")
    if np.any(faces < 0) or np.any(faces >= len(vertices)):
        raise ValueError("index out of range")
    return vertices, faces


def to_trimesh(bundle):
    """Return (detached Trimesh, report), transferring positions/indices only.

    Other fields stay exact in the original bundle, NOT in the tool. Generated
    tool normals/default colours are never substitutes for those fields.
    """
    import trimesh

    metadata = bundle.inspect()
    fields, arrays = metadata["fields"], metadata["arrays"]
    position = fields.get("vertex.position", {})
    if position.get("action") == "remove" or not position.get("enabled"):
        raise ValueError("enabled positions required")
    position_type = arrays[position["payload"]]["element_type"]
    if position_type not in ("ANARI_FLOAT32_VEC3", "ANARI_FLOAT64_VEC3"):
        raise ValueError("unsupported position representation")
    vertices = bundle.read("vertex.position")
    changes = []
    if vertices.dtype == np.dtype("<f4"):
        changes.append("positions: float32 -> float64 (exact widening in tool)")
    index = fields.get("primitive.index", {})
    if index.get("action") != "remove" and index.get("enabled"):
        index_type = arrays[index["payload"]]["element_type"]
        if index_type not in ("ANARI_UINT32_VEC3", "ANARI_UINT64_VEC3"):
            raise ValueError("unsupported index representation")
        faces = bundle.read("primitive.index")
        changes.append("indices: unsigned -> int64 (range checked in tool)")
    else:
        if index.get("payload"):
            changes.append("disabled stored indices are not transferred to tool")
        if len(vertices) % 3:
            raise ValueError("non-indexed vertex count must be divisible by three")
        faces = np.arange(len(vertices), dtype="<i8").reshape(-1, 3)
        changes.append("implicit triangles -> explicit indices")
    _topology(vertices, faces)
    unsupported = [name for name, field in fields.items()
                   if name not in ("vertex.position", "primitive.index")
                   and field["action"] != "remove"]
    tool = trimesh.Trimesh(vertices=vertices, faces=faces, process=False,
                           validate=False)
    return tool, {"unsupported_fields": unsupported,
                  "representation_changes": changes,
                  "discarded_fields": [],
                  "semantics": "Geometry-local topology only; attributes remain in bundle",
                  "ignored_tool_semantics": ["generated normals", "default visual colours"]}


def _cast(values, element_type, allowed, allow_precision_loss):
    if element_type not in allowed:
        raise ValueError("unsupported output topology representation")
    dtype, _ = representation(element_type)
    if dtype.kind == "u" and (np.any(values < 0) or np.any(values > np.iinfo(dtype).max)):
        raise ValueError("index representation overflow")
    with np.errstate(over="ignore", invalid="ignore"):
        converted = values.astype(dtype)
    if not np.isfinite(converted).all():
        raise ValueError("nonfinite output representation")
    loss = not np.array_equal(converted.astype(values.dtype), values)
    if loss and not allow_precision_loss:
        raise ValueError("position precision loss requires explicit acknowledgement")
    return converted, loss


def replace_topology(bundle, tool, *, correspondence, resolutions=None,
                     position_type=None, index_type=None,
                     allow_precision_loss=False, discard_tool_attributes=False):
    """Return (new Bundle, loss/action report); never mutate the input.

    correspondence is the caller's assertion, not a cardinality heuristic.
    With 'lost', every old optional field needs remove, Replacement or Alias.
    With 'maintained', omitted optional fields stay exact; lengths still check.
    Tool-only visuals/custom attributes/metadata require explicit discard.
    Output always uses enabled explicit triangle indices.
    """
    if correspondence not in ("maintained", "lost"):
        raise ValueError("explicit maintained/lost correspondence decision required")
    if type(allow_precision_loss) is not bool or type(discard_tool_attributes) is not bool:
        raise ValueError("loss acknowledgements must be boolean")
    resolutions = dict(resolutions or {})
    metadata = bundle.inspect()
    fields, arrays = metadata["fields"], metadata["arrays"]
    optional = [name for name, field in fields.items()
                if name not in ("vertex.position", "primitive.index")
                and field["action"] != "remove"]
    unresolved = [name for name in optional if name not in resolutions]
    if correspondence == "lost" and unresolved:
        raise ValueError("unresolved attributes after lost correspondence: " + ", ".join(unresolved))
    topology_payloads = {fields[name].get("payload")
                         for name in ("vertex.position", "primitive.index")
                         if name in fields}
    undeclared_splits = [name for name in unresolved
                         if fields[name]["payload"] in topology_payloads]
    if undeclared_splits:
        raise ValueError("topology aliases require explicit resolution: " + ", ".join(undeclared_splits))
    alias_names = {name for name, value in resolutions.items()
                   if isinstance(value, Alias)}
    for name, resolution in resolutions.items():
        association(name)
        if name in ("vertex.position", "primitive.index"):
            raise ValueError("topology actions are supplied by tool, not resolutions")
        if isinstance(resolution, str) and resolution not in ("preserve", "remove"):
            raise ValueError("unknown attribute resolution")
        if resolution == "preserve" and correspondence == "lost":
            raise ValueError("cannot preserve attributes after lost correspondence")
        if resolution == "preserve" and name not in optional:
            raise ValueError("cannot preserve absent attribute")
        if not isinstance(resolution, (str, Replacement, Alias)):
            raise ValueError("typed Replacement or Alias required")
        if isinstance(resolution, Alias) and resolution.source in alias_names:
            raise ValueError("alias chains/cycles require a direct non-alias source")
    discarded_tool = []
    if tool.visual.defined:
        discarded_tool.append("visual")
    for kind in ("vertex_attributes", "face_attributes"):
        discarded_tool.extend(kind + "." + name for name in getattr(tool, kind))
    if tool.metadata:
        discarded_tool.append("metadata")
    if discarded_tool and not discard_tool_attributes:
        raise ValueError("tool-only attributes require explicit discard: " + ", ".join(discarded_tool))
    vertices, faces = _topology(tool.vertices, tool.faces)
    old_position = fields.get("vertex.position", {})
    old_index = fields.get("primitive.index", {})
    position_type = position_type or (arrays[old_position["payload"]]["element_type"]
                                     if old_position.get("payload") else "ANARI_FLOAT32_VEC3")
    index_type = index_type or (arrays[old_index["payload"]]["element_type"]
                               if old_index.get("payload") else "ANARI_UINT32_VEC3")
    positions, precision_loss = _cast(vertices, position_type,
                                     ("ANARI_FLOAT32_VEC3", "ANARI_FLOAT64_VEC3"),
                                     allow_precision_loss)
    indices, _ = _cast(faces, index_type,
                       ("ANARI_UINT32_VEC3", "ANARI_UINT64_VEC3"), False)
    result = copy.deepcopy(bundle)
    result.replace("vertex.position", positions, position_type)
    result.replace("primitive.index", indices, index_type)
    discarded, replaced = [], []
    for name, resolution in resolutions.items():
        if resolution == "remove":
            result.remove(name)
            discarded.append(name)
        elif isinstance(resolution, Replacement):
            result.replace(name, resolution.values, resolution.element_type,
                           resolution.enabled)
            replaced.append(name)
    # Alias targets must exist first; cycles/forward alias chains are not inferred.
    for name, resolution in resolutions.items():
        if isinstance(resolution, Alias):
            result.alias(name, resolution.source, resolution.enabled)
            replaced.append(name)
    final = result.inspect()
    for name, field in final["fields"].items():
        if field["action"] == "remove" or name in ("vertex.position", "primitive.index"):
            continue
        count = final["arrays"][field["payload"]]["count"]
        expected = {"vertex": len(vertices), "primitive": len(faces),
                    "corner": 3 * len(faces)}[field["association"]]
        if count != expected:
            raise ValueError("incompatible retained/replaced attribute count: " + name)
    split_aliases = [name for name in optional
                     if fields[name]["payload"] in (old_position.get("payload"), old_index.get("payload"))
                     and name not in discarded and name not in replaced]
    field_changes = {}
    for name in dict.fromkeys(optional + list(resolutions)):
        before, after = fields.get(name, {}), final["fields"][name]
        before_array = arrays.get(before.get("payload"), {})
        after_array = final["arrays"].get(after.get("payload"), {})
        field_changes[name] = {
            "action": "remove" if name in discarded else "replace" if name in replaced else "retain exact",
            "association": after["association"],
            "before_type": before_array.get("element_type"),
            "after_type": after_array.get("element_type"),
            "before_count": before_array.get("count"),
            "after_count": after_array.get("count"),
            "before_enabled": before.get("enabled"),
            "after_enabled": after["enabled"],
        }
    return result, {"correspondence": correspondence,
                    "discarded_fields": discarded, "replaced_fields": replaced,
                    "retained_fields": [name for name in optional if name not in discarded and name not in replaced],
                    "split_topology_aliases": split_aliases,
                    "field_changes": field_changes,
                    "discarded_tool_attributes": discarded_tool,
                    "ignored_tool_semantics": ["vertex_normals", "face_normals", "default visual colours"],
                    "representation_changes": [str(vertices.dtype) + " -> " + position_type,
                                               str(faces.dtype) + " -> " + index_type,
                                               "output uses enabled explicit triangle indices"],
                    "precision_loss": precision_loss}
