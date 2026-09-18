# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

This repo is **multi-context**: a root `CONTEXT-MAP.md` points at one
`CONTEXT.md` per area. Unusually for a multi-context layout, **all ADRs are
centralised** in the root `docs/adr/`; there are no per-context `docs/adr/`
directories, and its `README.md` groups the ADRs by topic.

## Before exploring, read these

- **`CONTEXT-MAP.md`** at the repo root: it points at one `CONTEXT.md` per
  context, and describes how the contexts relate. Read each `CONTEXT.md`
  relevant to the topic.
- **`docs/adr/`**: read ADRs that touch the area you're about to work in. Start
  from `docs/adr/README.md`, which groups them by topic and records
  supersedes/revises links. Do not look for `src/<context>/docs/adr/` — this
  repo does not use them.
- **`STYLEGUIDE.md`** at the repo root for C++/CUDA conventions.

If any of these files don't exist, **proceed silently**. Don't flag their
absence; don't suggest creating them upfront. The `/domain-modeling` skill
(reached via `/grill-with-docs` and `/improve-codebase-architecture`) creates
them lazily when terms or decisions actually get resolved.

## File structure

```
/
├── CONTEXT-MAP.md
├── STYLEGUIDE.md
├── docs/adr/                                   ← all decisions, all contexts
│   ├── README.md                               ← grouped index
│   └── 00NN-<kebab-case-title>.md
└── src/
    ├── vsr/core/CONTEXT.md
    ├── vsr/io/CONTEXT.md
    ├── vsr/app/CONTEXT.md
    ├── vsr/rendering/CONTEXT.md
    └── apps/interactive/scivisStudio/CONTEXT.md
```

New ADRs take the next unused number regardless of topic; numbers are
identifiers, not an ordering. ADRs are immutable once written — a decision that
changes gets a new ADR that supersedes it, and `README.md` records the link.

## Use the glossary's vocabulary

When your output names a domain concept (in an issue title, a refactor proposal, a hypothesis, a test name), use the term as defined in the relevant `CONTEXT.md`. Don't drift to synonyms the glossary explicitly avoids — each entry lists them under `_Avoid_`.

If the concept you need isn't in the glossary yet, that's a signal: either you're inventing language the project doesn't use (reconsider) or there's a real gap (note it for `/domain-modeling`).

## Flag ADR conflicts

If your output contradicts an existing ADR, surface it explicitly rather than silently overriding:

> _Contradicts ADR-0007 (event-sourced orders), but worth reopening because…_
