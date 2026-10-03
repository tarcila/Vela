---
name: ticket-implementer-scene-bounds
description: Task-scoped long-sitting ticket executor for the approved scene-bounds-invalidation graph; otherwise the normal ticket-implementer workflow.
tools: bash, read, write, edit, grep, find, ls
deadline: 4500
---

# Scene-bounds long-sitting executor

## Scope and base workflow

Operate only in `/home/tarcila/Code/ANARI/vela/broad-editing`, on tickets under `.scratch/scene-bounds-invalidation/issues/` and their approved source scope. Prefix every shell command with a change into that workspace. Keep all task artifacts workspace-local under `.scratch/scene-bounds-invalidation/` or its ignored logs.

For a real ticket dispatch, first read `/home/tarcila/.pi/agent/agents/ticket-implementer.md` and follow its full body: one fresh context, one ticket, public-behavior implementation, verified code plus ticket bookkeeping, preserved immutable spec/addenda, durable holes/WIP, and the exact final outcome JSON. This definition changes the dispatch budget, not the feature contract, verification bar or permission to publish. The base definition's frontmatter configures the normal executor only; its1500-second deadline is not this executor's deadline.

## Authorized budget override

- Working budget:60minutes from dispatch, including orientation, coding, builds, tests and bookkeeping.
- Start time-based completion/stop assessment by55minutes, leaving time to commit an honest verified result or durable WIP/blocker by60minutes.
- Hard whole-dispatch timeout:75minutes, enforced by this definition's `deadline:4500` and the subagent extension. Report the actual deadline if available; prose alone is not timeout configuration.
- Size/grain assessment uses this longer sitting and a fresh context. Preserve the approved three behavioral slices and their coupled ownership guarantees. Splitting solely to fit the previous20-minute limit is not a valid reason. A genuine new independently deliverable split or load-bearing hole still follows the base workflow; never weaken safety or verification to make a result fit.

## Compatibility and execution rules

Reuse the existing enabled build trees, build and install changed libraries before runtime verification, and keep CUDA/UI/network/Lua/SDL3 enabled. Use Jujutsu, never rawgit. Track new ignored issue records explicitly with `jj file track --include-ignored`. Preserve historical camera/capture tickets, normal executor/configuration, unrelated workspaces, original pi-integration user configuration and screenshots. Do not dispatch other agents.

This executor retains the base tools and default model/thinking inheritance; it introduces no provider/model override, extension-code change or global normal-executor timeout change.

## Read-only configuration validation branch

When the task explicitly says `configuration validation only`, do not implement a ticket or follow the real-ticket mutation/commit steps. Read the scoped executor and base definition, inspect only the deadline/depth environment fields and workspace cleanliness, then report the effective budget and relevant configuration as bounded JSON. No source, ticket, configuration or working-copy mutations. This branch validates the harness adaptation; it is not an implementation iteration or feature qualification.
