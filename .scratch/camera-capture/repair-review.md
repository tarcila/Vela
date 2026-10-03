# Blocker repair verification/disposition

## Encoder33 — implemented, qualified and independently reviewed

Commit uwnryxop/4ed3aece modifies only PngEncode.cpp, ImageNormalization.hpp and test_IntegrationImage.cpp plus its ticket. Public pngRgba8OutputBound checked before input access/allocation; stored-deflate fallback keeps output at or below the proved framing-inclusive bound while retaining compression for compressible inputs. No vendor/CLI/default cap changes.

Entire permitted default envelope maximum:8,297,136 bytes at1024x2025, leaving91,472 bytes under8,388,608. FullHD bound8,296,191. Independent semantic decoder and separate framing/CRC/Adler/block-boundary oracles used. All2048 permitted heights tested at maximum allowed width; monotonicity covers smaller widths.

Worker positive verification after existing CUDA/UI/network/Lua/SDL3-enabled full build/fresh install:5 new bound cases71,989 assertions;13 image cases72,349 assertions;276 prior regression cases;281 total integration cases108,533 assertions; matchingCTest1/1. Valgrind image cases0errors, zero definite/indirect/possible leaks;2,318bytes reachable. ASan and GPU ANARI capture not run/claimed.

Fresh code-reviewer APPROVE, no required changes. Independently re-derived entire envelope and framing/overflow limits, traced stb growth assumptions, checked formatting. Did not rerun binary/build/Valgrind; worker verification is distinct. Optional portability and future vendor-cutoff maintenance observations are not established current bugs and no default-limit change follows from them.

## Scene dependency repair — NOT implemented

34→35→38→40 were split before source changes. Current replacement chain42→43→41→39→36→37 preserves the original requirements;28 waits for37.42 stopped ready-for-human at its grain decision, with no source/tests/acceptance boxes complete. Authorization29 is resolved as a human decision only. No node-invalidation repair is claimed.

Coupled foundation needs Scene and Layer lifetime-qualified resolution: a retained shared Layer can survive Scene teardown, and a detached object payload can survive Layer destruction while its Scene remains live. Merely nulling resolution when the Layer dies would lose the detached payload's required live-Scene use-count decrement. State must initialize before root construction and preserve copy/move/slot/relocation semantics without extra callbacks or precision-history changes. These are source implementation obligations already covered by user authorization, not another product decision.

Six standalone implementation dispatches used:33 resolved;34/35/38/40 split;42 budget/grain escalation. The20minute single-ticket workflow repeatedly decomposed but did not eliminate the ownership foundation. Stop rather than manufacture more slices or weaken safety. A longer execution approach is needed. No WIP source, no cap increase, no whole camera graph resumption.

## Tracker disposition

24/29 resolved decisions;20 reblocked33 and28 reblocked37.30/31 deferred nonblocking triage.32 closed superseded and consistency folded into20. All graph state local; no remote publication and original pi-integration user artifacts untouched.

The requested8MiB impact/increase discussion follows completed repairs. Relevant later inputs: actual encoded-size distribution versus worst bound, base64 plusJSON and response/proxy ceilings, retained-image and encode scratch budgets, concurrent requests and latency. Current measurements qualify encoder behavior, not real camera/capture/CPU/CUDA backend responsiveness or quality.
