import assert from 'node:assert/strict';
import {writeFile,stat} from 'node:fs/promises';
const pi='/nix/store/bkc4gfqj0dbfnavrz22ip7yx369f4sg7-pi-coding-agent-1.0.0/lib/pi-coding-agent';
const {McpServerConnection,createDefaultTransport}=await import(`${pi}/dist/extensions/mcp/runtime.js`);
const [descriptor,output,device,marker]=process.argv.slice(2);
const connection=new McpServerConnection({entry:{name:'isolated_viewport_04',config:{command:`${process.cwd()}/_out/_cmake/RelWithDebInfo/vsrMcpProxy`,args:[descriptor],timeout:20,exposure:'direct'}},cwd:process.cwd(),createTransport:createDefaultTransport,onTools:()=>{},onChange:()=>{}});
try {
  const client=await connection.getClient(); let next=1; let ref;
  const call=async(name,args)=>(await client.callTool(name,args)).structuredContent;
  const admitted=async(name,args)=>{
    const ack=await call(name,{request_id:next++,...args}); assert(ack.operation_ref,JSON.stringify(ack)); ref=ack.operation_ref;
    for(let i=0;i<400;i++) {const status=await call('vsr_operation_status',{operation_ref:ack.operation_ref});if(status.state==='terminal'){assert.equal(status.outcome,'ok');return status.result;}await new Promise(r=>setTimeout(r,25));}
    throw Error('own viewer operation timeout');
  };
  const discovery=await admitted('vsr_discover',{});
  const interactive=discovery.items.filter(x=>x.kind==='view' && x.role==='interactive_viewport');
  // Closed/hidden windows are retired; only live registrations are discoverable.
  assert(interactive.length>=1 && interactive.length<=2);
  const viewport=interactive.find(x=>x.name==='Viewport');
  const agent=discovery.items.find(x=>x.kind==='view' && x.role==='independent_agent');
  assert(viewport && agent);assert.notEqual(viewport.view_identity,agent.view_identity);
  let info;
  for(let i=0;i<200;i++){info=await admitted('vsr_view_info',{view_id:viewport.id});if(info.state==='available')break;await new Promise(r=>setTimeout(r,25));}
  assert.equal(info.state,'available');assert.equal(info.device_identifier,device);
  assert.equal(info.capture_policy.exposed,false);
  const latencies=[];let observations=0;
  for(let i=0;i<12;i++) {
    const target=interactive[i%interactive.length];const start=performance.now();
    const frame=await admitted('vsr_view_frame',{view_id:target.id});
    latencies.push(performance.now()-start);
    assert.equal(frame.state,'unavailable');assert.equal(frame.reason,'viewer_acquisition_unqualified');
    const image=await client.callTool('vsr_view_frame_image',{operation_ref:ref});
    assert.equal(image.content.filter(x=>x.type==='image').length,0);
    observations++;
  }
  const unchanged=await admitted('vsr_view_info',{view_id:viewport.id});
  assert.equal(unchanged.state,'available');
  for(const field of ['scene_revision','view_revision','device_identifier','camera_handle','renderer_handle','width','height'])assert.equal(unchanged[field],info[field],`observation changed ${field}`);
  const proof={device,interactiveViewCount:interactive.length,viewportIdentity:viewport.view_identity,agentIdentity:agent.view_identity,native:[info.width,info.height],display:info.display,negativeFrameObservations:observations,rpcRoundTripMs:{max:Math.max(...latencies),mean:latencies.reduce((a,b)=>a+b,0)/latencies.length},readbackQualified:false,imageParityQualified:false,sceneWrites:0,observationMetadataUnchanged:true};
  await writeFile(output,JSON.stringify(proof,null,2)+'\n');
  let resized;
  for(let i=0;i<400;i++){try{await stat(marker);break;}catch{}await new Promise(r=>setTimeout(r,25));}
  for(let i=0;i<200;i++){resized=await admitted('vsr_view_info',{view_id:viewport.id});if(resized.state==='available' && (resized.width!==info.width || resized.height!==info.height))break;await new Promise(r=>setTimeout(r,25));}
  assert.equal(resized.state,'available',JSON.stringify(resized));
  assert(resized.width!==info.width || resized.height!==info.height,JSON.stringify(resized));
  assert.equal(resized.view_identity,info.view_identity);assert.equal(resized.device_identifier,device);
  assert.equal(resized.capture_policy.exposed,false);
  assert.notEqual(resized.view_revision,info.view_revision);
  proof.resizedNative=[resized.width,resized.height];proof.resizedDisplay=resized.display;
  const frame=await admitted('vsr_view_frame',{view_id:viewport.id});
  assert.equal(frame.reason,'viewer_acquisition_unqualified');
  proof.negativeFrameObservations=++observations;
  proof.unavailableImageRetrievals=12;
  if(device==='helide') {
    const preview=await admitted('vsr_view_info',{view_id:agent.id});
    const render=await admitted('vsr_render',{view_id:agent.id,scene_revision:preview.scene_revision,view_revision:preview.view_revision});
    assert.equal(render.image_kind,'independent_render');assert.equal(render.view_identity,agent.view_identity);
    const image=await client.callTool('vsr_render_image',{operation_ref:ref});
    assert.equal(image.content.filter(x=>x.type==='image').length,1);
    proof.legacyImageItems=1;
  }
  await writeFile(output,JSON.stringify(proof,null,2)+'\n');
  console.log('PASS actual native MCP selected views, truthful unqualified reasons, no substitute image, real resize');
} finally {await connection.close();}
