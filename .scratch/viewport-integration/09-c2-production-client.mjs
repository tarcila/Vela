// Ticket 09 c2: actual production vsrViewer (Helide/VisRTX), native Pi 1.0.2
// MCP client. Ordinary notified node edit through vsr_edit; no model values.
import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
const pi='/nix/store/b7knmqjxmyir4m2gmij9cvv9g2ja9hmg-pi-coding-agent-1.0.2/lib/pi-coding-agent';
const {McpServerConnection,createDefaultTransport}=await import(`${pi}/dist/extensions/mcp/runtime.js`);
const {createMcpToolDefinition}=await import(`${pi}/dist/extensions/mcp/tools.js`);
const [descriptor,output,device,oracle]=process.argv.slice(2);
const transported=[];
const connection=new McpServerConnection({entry:{name:'bounds09c2',config:{command:`${process.cwd()}/_out/_cmake/RelWithDebInfo/vsrMcpProxy`,args:[descriptor],timeout:20,exposure:'direct'}},cwd:process.cwd(),createTransport:(...args)=>{const t=createDefaultTransport(...args);const send=t.send.bind(t);t.send=async(m,...rest)=>{if(m.method==='tools/call')transported.push(structuredClone(m.params));return send(m,...rest);};return t;},onTools:()=>{},onChange:()=>{}});
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const near=(a,b)=>a.length===b.length&&a.every((x,i)=>Math.abs(x-b[i])<=1e-4*Math.max(1,Math.abs(b[i])));
try {
  const client=await connection.getClient();
  const definitions=new Map((await client.listTools()).map(tool=>[tool.name,createMcpToolDefinition({name:`mcp__bounds09c2__${tool.name}`,server:'bounds09c2',tool,exposure:'direct',timeoutMs:20000,getClient:()=>connection.getClient()})]));
  assert.deepEqual(definitions.get('vsr_bounds').parameters.required,['request_id','scene_id']);
  let next=1,ref;
  const call=async(name,args)=>{for(let n=0;n<80;n++){try{return await definitions.get(name).execute(`b-${next}`,args);}catch(e){if(e?.data?.code!=='busy'||n===79)throw e;await sleep(25);}}};
  const admitted=async(name,args)=>{
    const ack=(await call(name,{request_id:next++,...args})).structuredContent.structuredContent;
    assert(ack.operation_ref,JSON.stringify(ack));ref=ack.operation_ref;
    for(let i=0;i<400;i++){const status=(await call('vsr_operation_status',{operation_ref:ref})).structuredContent.structuredContent;if(status.state==='terminal'){assert.equal(status.outcome,'ok',JSON.stringify(status));return status.result;}await sleep(25);}
    throw Error('operation polling expired');
  };
  let discovery,view;
  for(let n=0;n<100;n++){discovery=await admitted('vsr_discover',{});view=discovery.items.find(x=>x.kind==='view'&&x.role==='interactive_viewport');if(view)break;await sleep(50);}
  assert(view);
  const agent=discovery.items.find(x=>x.kind==='view'&&x.role==='independent_agent');
  const scene=discovery.items.find(x=>x.kind==='scene');assert(scene);
  const sceneId=scene.scene_id??scene.id;
  const before=await admitted('vsr_view_info',{view_id:view.id});
  const expected0=[[-2,-3,-4],[5,6,7]],delta=[50,0,0];
  const expected1=[expected0[0].map((x,i)=>x+delta[i]),expected0[1].map((x,i)=>x+delta[i])];
  const evidence=[];let pending=0,firstAvailableMs=null;const t0=performance.now();
  const bounds=async(phase)=>{const s=performance.now();const b=await admitted('vsr_bounds',{scene_id:sceneId});evidence.push({phase,bounds:b,roundTripMs:performance.now()-s,ref:{...ref}});return b;};
  for(let n=0;n<200;n++){const b=await bounds('readiness');if(b.state==='available'){firstAvailableMs=performance.now()-t0;break;}assert.equal(b.state,'pending',JSON.stringify(b));assert.equal(b.reason,'source_transition');pending++;await sleep(25);}
  assert(firstAvailableMs!==null,'source never ready');
  const check=(b,exp)=>{
    assert.equal(b.state,'available',JSON.stringify(b));
    assert(near(b.min,exp[0])&&near(b.max,exp[1]),JSON.stringify(b));
    assert.equal(b.coverage,'viewport_world');assert.equal(b.covered_scene_revision,null);assert.equal(b.revision_basis,'observation_only');
    assert.equal(b.source.identity,view.view_identity);assert.notEqual(b.source.identity,agent?.view_identity);
    assert.equal(b.source.device_identifier,device);assert.equal(b.source.device_subtype,'default');
    assert.equal(b.source.kind,'interactive_viewport');assert.equal(b.source.selection,'primary_live_interactive');
    for(const k of ['camera','name','pointer','visited','vertex_values'])assert(!(k in b)&&!(k in b.source));
  };
  for(let n=0;n<6;n++){check(await bounds('before_edit'),expected0);await sleep(30);}
  const historical={ref:{...ref},result:evidence.at(-1).bounds};
  const nodes=await admitted('vsr_inspect',{scene_id:sceneId,kind:'nodes',name:'09-c2-literal',node_kind:'transform'});
  assert.equal(nodes.items.length,1,JSON.stringify(nodes));const node=nodes.items[0];
  const edit=await admitted('vsr_edit',{scene_id:sceneId,edits:[{target_kind:'node',handle:node.handle,version:node.node_edit_version,structural_version:node.structural_version,parameter:'position',value:delta}]});
  const editDone=performance.now();let stale=0,updatedMs=null;
  for(let n=0;n<200;n++){const b=await bounds('after_edit_wait');assert.equal(b.state,'available');if(near(b.min,expected1[0])&&near(b.max,expected1[1])){updatedMs=performance.now()-editDone;break;}assert(near(b.min,expected0[0])&&near(b.max,expected0[1]),JSON.stringify(b));stale++;await sleep(25);}
  assert(updatedMs!==null,'edited world never observed');
  for(let n=0;n<6;n++){check(await bounds('after_edit'),expected1);await sleep(30);}
  const after=await admitted('vsr_view_info',{view_id:view.id});
  assert.deepEqual(after.camera,before.camera);
  const replay=(await call('vsr_bounds',{request_id:historical.ref.request_id,scene_id:sceneId})).structuredContent.structuredContent;
  assert.equal(replay.replayed,true);
  const old=(await call('vsr_operation_status',{operation_ref:historical.ref})).structuredContent.structuredContent;
  assert.deepEqual(old.result,historical.result);
  const calls=(await readFile(`${oracle}/calls.tsv`,'utf8')).trim().split('\n').map(l=>l.split('\t'));
  assert(!calls.some(l=>l[0]==='render_inside_bounds'));
  const driver=calls.filter(l=>l[0]==='bounds').map(l=>({durationMs:(Number(l[2])-Number(l[1]))*1000,status:Number(l[3])}));
  const hb=calls.filter(l=>l[0]==='heartbeat').map(l=>Number(l[1]));assert(hb.length>5);
  const gaps=hb.slice(1).map((x,i)=>(x-hb[i])*1000);
  const writes=transported.filter(x=>x.name==='vsr_edit').length;assert.equal(writes,1);
  assert(transported.filter(x=>x.name==='vsr_bounds').every(x=>Object.keys(x.arguments).sort().join(',')==='request_id,scene_id'));
  await writeFile(output,JSON.stringify({device,client:'Pi 1.0.2 native ToolDefinition / MCP stdio proxy / actual vsrViewer',fixture:'private glTF literal triangle; importer transform node edited via vsr_edit local position',expected:{before:expected0,after:expected1},sourceIdentity:view.view_identity,independentAgentIdentity:agent?.view_identity??null,pendingBeforeReady:pending,firstAvailableMs,staleObservationsAfterEdit:stale,updatedObservedMs:updatedMs,cameraUnchanged:true,historicalReplay:true,forcedRenderInsideBounds:false,sceneWrites:writes,editResult:edit,driverCalls:driver.length,driverMaxMs:Math.max(...driver.map(d=>d.durationMs)),driverStatuses:[...new Set(driver.map(d=>d.status))],heartbeats:hb.length,maxHeartbeatGapMs:Math.max(...gaps),observations:evidence.length,evidence:evidence.filter(e=>e.phase!=='readiness'&&e.phase!=='after_edit_wait').map(e=>({phase:e.phase,min:e.bounds.min,max:e.bounds.max,generation:e.bounds.source.generation,roundTripMs:e.roundTripMs}))},null,2)+'\n');
  console.log(`PASS ${device}: pending=${pending} stale=${stale} observations=${evidence.length}`);
} finally {await connection.close();}
