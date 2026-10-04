// Read-only, isolated qualification of the installed Pi MCP client path.
// No Pi session/configuration, model request, or user's controller is touched.
import assert from 'node:assert/strict';
import { writeFile } from 'node:fs/promises';
const root = '/nix/store/bkc4gfqj0dbfnavrz22ip7yx369f4sg7-pi-coding-agent-1.0.0/lib/pi-coding-agent';
const { McpServerConnection, createDefaultTransport } = await import(`${root}/dist/extensions/mcp/runtime.js`);
const { createMcpToolDefinition } = await import(`${root}/dist/extensions/mcp/tools.js`);
const workspace = process.cwd();
const [descriptor, output] = process.argv.slice(2);
const transported = [];
const connection = new McpServerConnection({
  entry: {name: 'isolated_viewport_01', config: {
    command: `${workspace}/_out/_cmake/RelWithDebInfo/vsrMcpProxy`,
    args: [descriptor], timeout: 20, exposure: 'direct'
  }}, cwd: workspace,
  createTransport: (...args) => {
    const transport = createDefaultTransport(...args);
    const send = transport.send.bind(transport);
    transport.send = async (message, ...rest) => {
      if (message.method === 'tools/call') {
        // Capture actual stdio messages at the transport boundary, not a
        // reconstructed request. No auth credentials travel on this side.
        transported.push(structuredClone(message.params));
      }
      return send(message, ...rest);
    };
    return transport;
  },
  onTools: () => {}, onChange: () => {}
});
try {
  const client = await connection.getClient();
  const tools = await client.listTools();
  const native = tools.find(t => t.name === 'vsr_inspect');
  assert(native);
  assert.deepEqual(native.inputSchema.required, ['request_id', 'scene_id']);
  const definitions = new Map(tools.map(tool => [tool.name, createMcpToolDefinition({
    name: `mcp__isolated_viewport_01__${tool.name}`, server: 'isolated_viewport_01',
    tool, exposure: 'direct', timeoutMs: 20000, getClient: () => connection.getClient()
  })]));
  const exposed = definitions.get('vsr_inspect');
  assert.deepEqual(exposed.parameters.required, ['request_id', 'scene_id']);
  assert(exposed.description.includes('Omit unused filters'));
  assert(exposed.description.includes('RootDelta'));
  let next = 1;
  const call = async (name, args) => {
    const reply = await definitions.get(name).execute(`fixture-${next}`, args);
    return reply.structuredContent.structuredContent;
  };
  const admitted = async (name, args) => {
    const ack = await call(name, {request_id: next++, ...args});
    const ref = ack.operation_ref;
    for (let i = 0; i < 200; ++i) {
      const status = await call('vsr_operation_status', {operation_ref: ref});
      if (status.state === 'terminal') {
        assert.equal(status.outcome, 'ok');
        return status.result;
      }
      await new Promise(r => setTimeout(r, 25));
    }
    throw new Error('isolated viewer did not service query');
  };
  const discovery = await admitted('vsr_discover', {});
  const scene = discovery.items.find(x => x.kind === 'scene');
  assert(scene);
  const sceneId = scene.scene_id ?? scene.id;
  assert.equal(typeof sceneId, 'number');
  const queen = await admitted('vsr_inspect', {scene_id: sceneId, kind: 'nodes', name: 'Queen_B'});
  assert.equal(queen.items.length, 1);
  assert.equal(queen.items[0].name.text, 'Queen_B');
  assert.equal(queen.items[0].node_kind, 'transform');
  assert.match(queen.items[0].handle, /^[0-9a-f]{32}$/);
  const pawns = await admitted('vsr_inspect', {scene_id: sceneId, kind: 'nodes', name_contains: 'Pawn_', node_kind: 'transform'});
  assert(pawns.items.length > 0);
  assert(pawns.items.every(x => x.name.text.includes('Pawn_') && x.node_kind === 'transform'));
  const fresh = queen.items[0];
  const direct = await admitted('vsr_inspect', {scene_id: sceneId, kind: 'nodes', handle: fresh.handle});
  assert.equal(direct.items.length, 1);
  assert.equal(direct.items[0].node_edit_version, fresh.node_edit_version);
  const queryMessages = transported.filter(x => x.name === 'vsr_inspect');
  assert.deepEqual(Object.keys(queryMessages[0].arguments).sort(), ['kind', 'name', 'request_id', 'scene_id']);
  assert.deepEqual(Object.keys(queryMessages[1].arguments).sort(), ['kind', 'name_contains', 'node_kind', 'request_id', 'scene_id']);
  assert(queryMessages.every(x => !('object_type' in x.arguments) && !('object_subtype' in x.arguments) && !('cursor' in x.arguments)));
  const readTools = new Set(['vsr_discover', 'vsr_inspect', 'vsr_operation_status']);
  const sceneWrites = transported.filter(x => !readTools.has(x.name)).length;
  assert.equal(sceneWrites, 0, 'fixture client may perform only read operations');
  await writeFile(output, JSON.stringify({
    client: 'installed Pi McpServerConnection + createMcpToolDefinition + stdio vsrMcpProxy',
    nativeRequired: native.inputSchema.required,
    modelToolRequired: exposed.parameters.required,
    exposedDescription: exposed.description,
    transportedInspectArguments: queryMessages.map(x => x.arguments),
    queenCount: queen.items.length, pawnTransformCount: pawns.items.length,
    pawnNames: pawns.items.map(x => x.name.text),
    queenFreshHandle: fresh.handle, queenVersion: fresh.node_edit_version,
    directReinspectionCount: direct.items.length,
    sceneWrites, externalModelProviderRequest: false,
    limitations: 'Tests the real installed Pi tool-definition/client/stdio/native path, not an upstream gateway/model-generated payload or viewport image parity.'
  }, null, 2) + '\n');
} finally {
  await connection.close();
}
