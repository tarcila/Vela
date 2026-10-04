# Final Standards-Axis Review — viewport-integration (888e173a..70a79567)

## Coverage
Read in full: `.scratch/viewport-integration/logs/final-review-source.diff` (13086
lines, 49 files), commit list (32 commits), `CODING_STANDARDS.md` (all 23
sections), `CONTEXT-MAP.md`, `src/vsr/rendering/CONTEXT.md`,
`src/vsr/integration/frame/README.md`, `src/vsr/integration/mcp/README.md`.
Spot-verified against current tree: `Translation.hpp`, `FrameObservation.hpp`
(integration + rendering view), `ViewerFrameSource.hpp`, `CapturePolicy.hpp`,
`RequestDiagnostics.hpp`, `Integration.{h,cpp}pp`, `Activity.hpp`,
`ImagePipeline.{h,cpp}`, `AnariSceneRenderPass.{h,cpp}`, `Viewport.{h,cpp}`,
`Window.{h,cpp}`, `Application.{h,cpp}`, `McpServer.hpp`, `EditTools.hpp`,
`FrameTools.hpp`. Not independently re-run: build/CTest (data per task scope).

## Hard violations
None found against documented `CODING_STANDARDS.md` rules. Header-only
layering (`vsr_integration/CMakeLists.txt` note), `#pragma once`, include
grouping/labels, class-layout order (public types → ctor/dtor → lifetime
macros → methods → private data), the "Inlined definitions" section split,
§16 pointer-not-reference non-owning members (`SceneIdentity *identity`,
`Integration *m_integration`, etc.), and the §13 polymorphic-hook shape
(`Window::completePresentation`, `Application::uiRenderBeforePresent`,
`RenderBackend::renderedBinding`) are all followed correctly across the new
and modified files. `std::map` use in `FrameService` (`m_views`, `m_jobs`) is
the explicitly-deferred broad `FlatMap` refactor and is out of scope per the
task brief.

## Judgement calls (baseline smells, not hard violations)

- **Duplicated Code** — the six-line `EditEntry`/`EditBatch` construction
  (`targetKind = NODE; parameter = "position"; space = "world";
  hasAncestryVersion = true; …`) is repeated verbatim in
  `translation_detail::canonicalBatch`, `TranslationService::commit`, and
  `TranslationService::prepare` (`src/vsr/integration/edit/Translation.hpp`,
  e.g. lines ~298-308, ~835-841, ~987-993). A small `makeWorldPositionEdit(...)`
  helper would remove the triplication.
- **Primitive Obsession / Data Clumps** — the MCP response bound `12582912`
  is repeated as a bare literal in `FrameTools.hpp` (twice) and
  `ToolSupport.hpp`, uncoupled from `CapturePolicy::maxResponseBytes`
  (same value, `CapturePolicy.hpp:29`). A shared named constant would make the
  coupling explicit instead of three independently-maintained copies.
- **Large Class / Divergent Change (borderline)** — `TranslationService`
  (`Translation.hpp:83-174`) now owns envelope validation, authorization,
  budget bookkeeping, request/executor construction, and the full commit/prepare
  algorithms. It mirrors `EditService`'s existing shape, so this is consistent
  with established precedent rather than a new pattern, but it is the single
  largest unit touched in this range.

## Not flagged
Long string literals (tool descriptions) exceeding 80 columns are explicitly
permitted by §1. Localized `try { … } catch (const std::bad_alloc &)`
boundaries pre-exist at the base revision in `AgentEdit.hpp` and are applied
consistently to new code under the same documented rationale (translate
allocation exhaustion to `busy`), not a new §11 deviation.

## Summary
0 required findings, 3 optional/judgement-call suggestions (all Duplicated
Code / Primitive Obsession flavors), 0 nits worth separating out.

Reviewer: standards-axis (ad hoc, n/a)
