#!/usr/bin/env python3
"""Conservative sampled heap bound, not an exact ownership profiler.

Remove only named existing ImagePipeline buffers and SDL display allocations.
Keep all other process heap (including Scene, client copies, unknown branches
and allocator overhead), so the residual overcounts observation heap. Explicit
encoder stack/guard mappings are added separately; generic server thread stacks
are pre-existing transport infrastructure, not spawned by frame observation.
Massif sampling supplements the source allocation bounds; it cannot prove them.
"""
import json
import re
from pathlib import Path

root = Path(__file__).parent
text = (root / "logs/03-cont-massif.out").read_text()
samples = []
for block in text.split("snapshot=")[1:]:
    heap = int(re.search(r"mem_heap_B=(\d+)", block)[1])
    extra = int(re.search(r"mem_heap_extra_B=(\d+)", block)[1])
    excluded = 0
    excluded_depth = None
    for line in block.splitlines():
        match = re.match(r"( *)n\d+: (\d+) (.*)", line)
        if not match:
            continue
        depth, size, symbol = len(match[1]), int(match[2]), match[3]
        if excluded_depth is not None and depth > excluded_depth:
            continue
        excluded_depth = None
        if "ImagePipeline::setDimensions" in symbol or "SDL_malloc_REAL" in symbol:
            excluded += size
            excluded_depth = depth
    if "heap_tree=empty" not in block:
        samples.append({"snapshot": int(block.splitlines()[0]),
                        "heap_plus_extra": heap + extra,
                        "named_non_observation_heap": excluded,
                        "remaining_process_heap": heap + extra - excluded})

result = {"detailed_samples": len(samples),
          "whole_process_sampled_heap_peak": max(s["heap_plus_extra"] for s in samples),
          "conservative_remaining_heap_peak": max(s["remaining_process_heap"] for s in samples),
          "explicit_encoder_stack_and_guard": 2162688,
          "sampled_remaining_heap_plus_encoder_mapping": max(s["remaining_process_heap"] for s in samples) + 2162688,
          "reservation_ceiling": 268435456,
          "limits": "Sampled residual overcounts owned heap, not total-process memory or an all-time peak proof; static bounds and live lease accounting remain necessary."}
print(json.dumps(result, indent=2))
(root / "03-memory-audit.json").write_text(json.dumps(result, indent=2) + "\n")
