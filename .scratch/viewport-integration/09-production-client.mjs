// Actual production viewer, isolated native Pi 1.0.2 client; no model values.
import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
const pi='/nix/store/b7knmqjxmyir4m2gmij9cvv9g2ja9hmg-pi-coding-agent-1.0.2/lib/pi-coding-agent';
const {McpServerConnection,createDefaultTransport}=await import(`${pi}/dist/extensions/mcp/runtime.js`);
const {createMcpToolDefinition}=await import(`${pi}/dist/extensions/mcp/tools.js`);
const [descriptor,output,device,oracle]=process.argv.slice(2);
const transported=[];
const connection=new McpServerConnection({entry:{name:'bounds09',config:{command:`${process.cwd()}/_out/_cmake/RelWithDebInfo/vsrMcpProxy`,args:[descriptor],timeout:20,exposure:'direct'}},cwd:process.cwd(),createTransport:(...args)=>{const t=createDefaultTransport(...args);const send=t.send.bind(t);t.send=async(m,...rest)=>{if(m.method==='tools/call')transported.push(structuredClone(m.params));return send(m,...rest);};return t;},onTools:()=>{},onChange:()=>{}});
try {
  const client=await connection.getClient();
  const definitions=new Map((await client.listTools()).map(tool=>[tool.name,createMcpToolDefinition({name:`mcp__bounds09__${tool.name}`,server:'bounds09',tool,exposure:'direct',timeoutMs:20000,getClient:()=>connection.getClient()})]));
  assert.deepEqual(definitions.get('vsr_bounds').parameters.required,['request_id','scene_id']);
  let next=1,ref;
  const call=async(name,args)=>{for(let n=0;n<80;n++){try{return await definitions.get(name).execute(`bounds-${next}`,args);}catch(e){if(e?.data?.code!=='busy'||n===79)throw e;await new Promise(r=>setTimeout(r,25));}}};
  const admitted=async(name,args)=>{
    const ack=(await call(name,{request_id:next++,...args})).structuredContent.structuredContent;
    assert(ack.operation_ref,JSON.stringify(ack));ref=ack.operation_ref;
    for(let i=0;i<400;i++){const status=(await call('vsr_operation_status',{operation_ref:ref})).structuredContent.structuredContent;if(status.state==='terminal'){assert.equal(status.outcome,'ok',JSON.stringify(status));return status.result;}await new Promise(r=>setTimeout(r,25));}
    throw Error('operation polling expired');
  };
  let discovery,view;
  for(let n=0;n<100;n++){
    discovery=await admitted('vsr_discover',{});
    view=discovery.items.find(x=>x.kind==='view'&&x.role==='interactive_viewport');
    if(view)break;
    await new Promise(r=>setTimeout(r,50));
  }
  assert(view);
  const scene=discovery.items.find(x=>x.kind==='scene');assert(scene);
  await new Promise(r=>setTimeout(r,800)); // natural viewing only
  const before=await admitted('vsr_view_info',{view_id:view.id});
  assert.equal(before.state,'available',JSON.stringify(before));
  assert(before.camera&&before.camera.position&&before.camera.direction&&before.camera.up);
  const evidence=[];
  for(let n=0;n<12;n++){
    const start=performance.now();
    const bounds=await admitted('vsr_bounds',{scene_id:scene.id});
    if(device==='helide'){
      assert.equal(bounds.state,'available',JSON.stringify(bounds));
      assert.deepEqual(bounds.min,[-2,-3,-4]);assert.deepEqual(bounds.max,[5,6,7]);
      assert.equal(bounds.coverage,'viewport_world');assert.equal(bounds.covered_scene_revision,null);
      assert.equal(bounds.source.identity,view.view_identity);
      assert.equal(bounds.source.device_identifier,'helide');assert.equal(bounds.source.device_subtype,'default');
      assert.equal(bounds.source.kind,'interactive_viewport');
      assert.equal(bounds.revision_basis,'observation_only');
    } else {
      assert.equal(bounds.state,'unavailable');assert.equal(bounds.reason,'unqualified_source');
      assert(!bounds.min&&!bounds.max&&!bounds.source);
    }
    evidence.push({bounds,roundTripMs:performance.now()-start});
    await new Promise(r=>setTimeout(r,30));
  }
  const historicalRef={...ref},historical=evidence.at(-1).bounds;
  const after=await admitted('vsr_view_info',{view_id:view.id});
  assert.deepEqual(after.camera,before.camera);
  const replay=(await call('vsr_bounds',{request_id:historicalRef.request_id,scene_id:scene.id})).structuredContent.structuredContent;
  assert.equal(replay.replayed,true);
  const old=(await call('vsr_operation_status',{operation_ref:historicalRef})).structuredContent.structuredContent;
  assert.deepEqual(old.result,historical);
  const calls=(await readFile(`${oracle}/calls.tsv`,'utf8')).trim().split('\n').map(l=>l.split('\t'));
  assert(!calls.some(l=>l[0]==='render_inside_bounds'));
  const driver=calls.filter(l=>l[0]==='bounds').map(l=>({durationMs:(Number(l[2])-Number(l[1]))*1000,status:Number(l[3]),box:l.slice(4).map(Number)}));
  const heartbeats=calls.filter(l=>l[0]==='heartbeat').map(l=>Number(l[1]));
  assert(heartbeats.length>5);
  assert(transported.every(x=>['vsr_discover','vsr_view_info','vsr_bounds','vsr_operation_status'].includes(x.name)));
  assert(transported.filter(x=>x.name==='vsr_bounds').every(x=>Object.keys(x.arguments).sort().join(',')==='request_id,scene_id'));
  const agent=discovery.items.find(x=>x.kind==='view'&&x.role==='independent_agent');assert(agent);
  await writeFile(output,JSON.stringify({device,client:'Pi 1.0.2 native ToolDefinition / MCP stdio proxy / actual vsrViewer',fixture:'private OBJ literal vertices; no user Scene or controller',expectedMin:[-2,-3,-4],expectedMax:[5,6,7],sourceIdentity:view.view_identity,independentAgentIdentity:agent.view_identity,cameraUnchanged:true,historicalReplay:true,forcedRenderInsideBounds:false,sceneWrites:0,afterEditProductionProof:false,barneyQualified:false,driver,heartbeats:heartbeats.length,maxHeartbeatGapMs:Math.max(...heartbeats.slice(1).map((x,i)=>(x-heartbeats[i])*1000)),evidence},null,2)+'\n');
  console.log(`PASS ${device}: ${evidence.length} native production observations; barney remains unqualified`);
} finally {await connection.close();}
