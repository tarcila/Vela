# VSR / Pi integration — design interview

Status: design interview complete and synthesized into [spec.md](spec.md), the local `ready-for-agent` specification. No implementation has been performed.

## Consolidated direction

Provide a headless, explicitly enabled VSR agent-integration capability with an optional embedded loopback MCP server. Applications register scenes, views, permissions, and safe execution points; Pi attaches as an external client rather than being embedded in the application. A dedicated vsrViewer activity/results window consumes the same integration state and remains optional.

The first complete workflow inspects an existing scene and image, changes supported value parameters on existing materials/lights through validated all-or-nothing Agent Edit Batches, then obtains a fresh state-identified render. Stale edits and conflicting restorations fail without overwriting newer work. One expiring logical controller session owns agent control at a time; completed edits and bounded operation/restoration history belong to the application, not the client connection.

Product-level interview complete. The chronology below records accepted decisions; later rounds resolve or narrow earlier provisional statements. The user subsequently requested specification and confirmed the public headless integration API as the primary testing boundary, with thin MCP/Pi, rendering, and viewer checks. That specification is now recorded in [spec.md](spec.md); technical validation remains future work.

## Working decisions

The user accepted the recommended direction in round 1 ("Looks good"):

- Integration is a headless VSR framework capability. vsrViewer adds a dedicated human-facing window; it does not own the integration.
- Pi is the first supported client, not a required framework dependency. Other harnesses and ordinary automation should not be excluded by the framework boundary.
- The conversation initially lives in Pi. The viewer window shows activity and results rather than acting as a second chat client. Exact contents remain undecided.
- Live scene editing can be explicitly enabled without approving every parameter change. File writes, external commands, and destructive operations require distinct permission decisions.
- The initial workflow exercises inspection, modification, rendering, and inspection of the result.

The user accepted the recommended direction in round 2 ("Looks good"):

- First acceptance scenario: inspect the current scene and rendered image, adjust lighting and materials to make the main object clearer, then show the result. Importing datasets, modifying application source code, and diagnosing renderer internals are outside this first scenario.
- Attach to a running application instance, discover its exposed scenes and views, and select a working target. Simple applications can expose a single default target.
- Provide structured operations for routine inspection and edits. Lua is optional and separately enabled; it is not required for application integration.
- Initial connections are same-machine only, without assuming every local process is trusted. Preserve the possibility of remote use later, but remote deployment/authentication is outside the first milestone.

The user accepted the recommended direction in round 3 ("Looks good"):

- Viewer window: connection and target information; requested/running/completed/failed operations; concise results and references to affected objects; controls to disable edits or disconnect. Image comparisons may come later.
- VSR-observed operation history is authoritative. Optional client-supplied task summaries are distinguished from it; the full Pi conversation is not mirrored.
- Keep the application interactive. Reject conflicting stale agent edits and require reinspection instead of overwriting manual changes or locking the application for an entire agent task.
- First-version material/lighting edit batches must support restoring their prior affected state without discarding unrelated manual edits. This is not a requirement for general application-wide undo. Batch boundaries and restoration conflicts remain to be defined.
- On disconnect, retain completed edits, cancel queued work from that connection, and let an already-executing atomic edit finish safely. Reconnection requires fresh inspection. Exact render/cancellation behavior remains open.

The user explicitly agreed with round 4:

- MCP is the first external interface, backed by a transport-independent VSR integration library. The installed Pi MCP gateway supplies the initial client path. A bespoke Pi extension is not required for basic inspection/editing/rendering; add one only for a concrete unmet need. Server hosting is undecided.
- Applications explicitly register exposed scenes, views, and operations and explicitly activate the integration. Linking alone does not open an endpoint. Provide a convenience adapter for standard `vsr::app::Context` and support custom/headless applications.
- Application launch configuration sets the capability ceiling. Attachment requires a per-launch credential. Viewer controls may revoke or narrow access, never exceed that ceiling.
- Initially allow one controlling connection per application instance. Reject a second controller explicitly; no silent takeover. Multiple observers and finer-grained ownership are deferred.

The user explicitly agreed with round 5:

- Optional embedded MCP server on loopback, separate from the transport-independent integration library. Accept optional HTTP/JSON dependencies rather than require a companion process. Applications not enabling the component are unaffected.
- An Agent Edit Batch is one explicitly submitted group of edits. Validate targets, permissions, and expected versions together; apply all edits or leave the affected scene state unchanged. Successful batches are individually restorable. A multi-turn task may contain several batches; never hold a transaction while Pi thinks or renders.
- Restoration is conditional: reject the whole restoration without changing the scene when affected state or required object references conflict with the recorded batch. Do not overwrite newer work or partially restore. Unrelated manual changes remain untouched.
- Rendering returns a new completed render from the selected view, plus identifying information about the scene/view state represented. Use a bounded rendering budget rather than promise convergence. If changes prevent capturing a matching result, report superseded/timeout rather than mislabel an old frame.

The user accepted the recommended direction in round 6 ("Looks good"):

- First editing surface: supported value parameters on existing materials and lights. Scene inspection and rendering are included. Defer object creation/deletion, material-type replacement, object-reference edits, array edits, and file operations. The acceptance scene already contains suitable materials and lights. Lua is a future, separately enabled capability, not a first-version escape hatch.
- Application code owns safe execution points for all scene access. Network handling queues requests; application code explicitly services them. Provide a viewer hook and a blocking service-loop helper for headless applications. Network threads do not directly read or write scene state; integration does not silently move application state onto another thread.
- Initial attachment is explicit through a private local connection descriptor containing endpoint and per-launch credential. Viewer connection information and headless startup expose the descriptor path, not the credential. Defer automatic discovery. Verify exact Pi configuration support before promising one-command setup.
- Bounded in-memory history lives for the application lifetime and survives client disconnects, but not application restarts. Clearly indicate when retained restoration state has expired. Durable audit logs, persistent agent sessions, and cross-restart undo are deferred.

The user accepted the recommended direction in the final round ("Looks good"):

- Control belongs to an expiring logical session, not a TCP connection. Explicit detach releases control immediately; abandoned sessions expire after a bounded interval. Expiry cancels queued work belonging to that session and releases ownership. After expiry the client must attach and inspect again. Already-executing atomic edits retain the previously agreed safe-completion rule.
- Verify whether the installed Pi MCP gateway can maintain that lifecycle unaided. If not, a small Pi adapter is justified; do not weaken VSR ownership rules to avoid it.
- Expose structured batch restoration for headless clients under edit permission, and a Restore action for eligible viewer batches usable after Pi disconnects. Both paths use the same conflict checks. Explain expired/conflicting restoration state and do not silently force overwrite.

## Established constraints

- Start a VSR-enabled application, independently start Pi, then attach to the running application.
- Existing VSR seams include Lua scripting, scene-oriented networking, and ImGui window registration. They do not by themselves establish an agent-control protocol or safe execution boundary.
- `vsr::app::Context` does not own render pipelines; views need separate explicit registration.
- VSR has object-change versions and notification batching, but not transactional edits or undo. The agreed all-or-nothing and restoration behavior is new work, not an existing guarantee.
- This installed Pi provides an MCP gateway through an extension. MCP exposure and a Pi-specific adapter are not mutually exclusive.

## Technical validation and specification still required

These are not existing guarantees or authorization to implement:

- Verify end-to-end installed Pi gateway support for authenticated loopback MCP attachment, private connection-descriptor consumption, image results, and logical controller-session maintenance. Decide whether a minimal Pi adapter is necessary from that evidence.
- Select MCP transport/protocol version and maintained optional HTTP/JSON dependencies. Specify endpoint authentication, credential/descriptor protection and cleanup, request limits, and session expiry timing.
- Define external target/object identity that cannot silently address a different object after removal, index reuse, or storage defragmentation. Define scene/view state identifiers for observations and render results.
- Specify the supported parameter types and validation rules. Demonstrate all-or-nothing application and restoration, including failures during mutation, changed/deleted targets, and dependent state. Existing notification batching is insufficient proof.
- Define host execution and renderer coordination so scene reads/writes are safe without blocking the viewer for the duration of agent thought or long renders. Specify bounded render budgets, cancellation, and superseded results during concurrent edits or animation.
- Define capability enforcement for every operation, especially restoration, and distinguish client-supplied summaries from VSR-observed activity.
- Specify bounded history/restoration accounting, eviction visibility, queue limits, and behavior under resource pressure.
- Verify both a minimal headless host and vsrViewer complete the same acceptance workflow, including stale edits, restoration conflicts, detach/expiry, second-controller rejection, and reconnect-with-fresh-inspection.
- Settle public API/package names, tool schemas, domain-document placement, and any ADRs during specification; the conceptual architecture does not prescribe a source layout.

## Documentation

The agreed Agent Edit Batch term is recorded in the adjacent `CONTEXT.md` design glossary; final domain-doc placement remains undecided. “Feedback” is represented by the agreed activity/results window but has not been adopted as a canonical term. No ADRs yet. Keep exploratory material here; promote settled documentation when the design is confirmed.
