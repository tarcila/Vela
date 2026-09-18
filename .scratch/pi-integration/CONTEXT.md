# VSR Agent Integration

VSR Agent Integration allows an external automation client to inspect and control explicitly exposed state in a running VSR application. This design glossary records agreed terminology pending placement in the framework's domain documentation.

## Language

**Operation Reference**:
The session-qualified identity of an externally submitted scene operation, combining its originating session identity and client request number. It identifies work across transport reconnects without granting permission to act on that work.
_Avoid_: MCP request ID, unqualified request number

**Agent Edit Batch**:
An explicitly submitted group of scene edits that either takes effect in full or leaves the affected scene state unchanged; downstream synchronization failure does not undo a committed batch. A committed batch is eligible for one conditional Batch Restoration while its record is retained; a newer parameter change on any affected object blocks restoration.
_Avoid_: Agent task, update batch, undo transaction

**Batch Reference**:
The public, launch-qualified identity of a committed Agent Edit Batch's retained restoration record. It remains stable across client disconnects while retained, independently of the originating operation's result storage, and grants no authority. Local and external restoration select the same record through this reference, distinct from the restoration operation's own correlation.
_Avoid_: Restore request ID, most recent batch, storage index

**Batch Restoration**:
A one-way operation selecting an eligible Agent Edit Batch by its Batch Reference and returning its parameters to their recorded prior values without changing unrelated state. Its commit consumes the batch's restoration eligibility, even if downstream synchronization fails, and creates no new restorable batch.
_Avoid_: Batch rollback
