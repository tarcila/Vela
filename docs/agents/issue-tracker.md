# Issue tracker: Local Markdown

Issues and specs for this repo live as markdown files in `.scratch/`.

**Never** create GitHub issues or PRs for this material, and never push it,
unless the user explicitly instructs it. `origin` is a personal fork of an
upstream NVIDIA repository; `.scratch/` is working notes. See the root
`AGENTS.md`.

Note: `.scratch/` is ignored globally on this machine. A pipeline directory
that needs tracked state tracks its files explicitly (e.g. `jj file track`)
or un-ignores itself with a negation in this repo's `.gitignore`.

## Version control

This repo uses **Jujutsu (`jj`)**, not raw git. Ticket and pipeline state is
committed with `jj commit`; history is read with `jj log`. See the root
`AGENTS.md`.

## Conventions

- One feature per directory: `.scratch/<feature-slug>/`
- The spec is `.scratch/<feature-slug>/spec.md`
- Implementation issues are one file per ticket at
  `.scratch/<feature-slug>/issues/<NN>-<slug>.md`, numbered from `01`, never a
  single combined tickets file
- Next free `NN` is the highest existing `NN` + 1, **never a gap**, and numbers
  are never reused or renumbered
- Triage state is a `Status:` line near the top of each issue file (see
  `triage-labels.md` for the role strings)
- Comments and conversation history append to the bottom under a `## Comments`
  heading
- Addenda to a frozen spec live at `.scratch/<feature-slug>/addenda/NN-<slug>.md`
- Pipeline orchestration state, when present, is
  `.scratch/<feature-slug>/pipeline.md`

## Ticket file

The full contract. A ticket is read by an implementer that has **the ticket and
nothing else**: no conversation, no spec unless the ticket points at it, no
memory of how it was cut.

```markdown
# NN — <imperative title>

**Status:** ready-for-agent
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/rendering/pipeline/ImagePipeline.h, src/vsr/rendering/pipeline/ImagePipeline.cpp

## Current behavior

What the code does today, stated so the implementer can verify it before
changing anything.

## Desired behavior

What must be observably true when the ticket is done. Observable: a test that
passes, a command that prints something different, a build that stops warning.
Not a description of the edit to make.

## Out of scope

What this ticket may not touch, named explicitly. Adjacent files, neighbouring
refactors, and tempting cleanups belong here.

## Comments
```

Field rules:

- **`Status:`** — one of the five role strings in `triage-labels.md`.
- **`Milestone:`** — integer, matching a `## Milestone N` heading in the spec,
  or `1` for a single-milestone spec.
- **`Blocked by:`** — comma-separated `NN` values, or `none`. A ticket is
  unblocked when every ticket it names has `Status: resolved`.
- **`Files:`** — repo-relative paths this ticket is expected to touch, best
  effort, used to run disjoint tickets concurrently. **Omit the field entirely
  rather than guess**; a ticket without it runs alone.
- **Cite no line numbers.** Files and symbols only — line numbers rot between
  the cut and the implementation.

## When a skill says "publish to the issue tracker"

Create a new file under `.scratch/<feature-slug>/` (creating the directory if
needed). Do not open a GitHub issue.

## When a skill says "fetch the relevant ticket"

Read the file at the referenced path. The user will normally pass the path or
the issue number directly.

## Wayfinding operations

Used by `/wayfinder`. The **map** is a file with one **child** file per ticket.

- **Map**: `.scratch/<effort>/map.md` (the Notes / Decisions-so-far / Fog body).
- **Child ticket**: `.scratch/<effort>/issues/NN-<slug>.md`, numbered from `01`,
  with the question in the body. A `Type:` line records the ticket type
  (`research`/`prototype`/`grilling`/`task`); a `Status:` line records
  `claimed`/`resolved`.
- **Blocking**: a `Blocked by: NN, NN` line near the top. A ticket is unblocked
  when every file it lists is `resolved`.
- **Frontier**: scan `.scratch/<effort>/issues/` for files that are open,
  unblocked, and unclaimed; first by number wins.
- **Claim**: set `Status: claimed` and save before any work.
- **Resolve**: append the answer under an `## Answer` heading, set
  `Status: resolved`, then append a context pointer (gist + link) to the map's
  Decisions-so-far in `map.md`.
