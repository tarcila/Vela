import assert from 'node:assert/strict';
import {writeFile,stat} from 'node:fs/promises';
const pi = '/nix/store/bkc4gfqj0dbfnavrz22ip7yx369f4sg7-pi-coding-agent-1.0.0/lib/pi-coding-agent';
const {McpServerConnection,createDefaultTransport} = await import(`${pi}/dist/extensions/mcp/runtime.js`);
const [descriptor, output, device, resizeMarker] = process.argv.slice(2);
const connection = new McpServerConnection({
  entry: {name:'isolated_viewport_02',config:{command:`${process.cwd()}/_out/_cmake/RelWithDebInfo/vsrMcpProxy`,args:[descriptor],timeout:20,exposure:'direct'}},
  cwd:process.cwd(),createTransport:createDefaultTransport,onTools:()=>{},onChange:()=>{}
});
try {
  const client = await connection.getClient();
  let next=1;
  let lastRef;
  const call = async (name,args) => {
    const reply = await client.callTool(name,args);
    return reply.structuredContent;
  };
  const admitted = async (name,args) => {
    const ack=await call(name,{request_id:next++,...args});
    assert(ack.operation_ref, JSON.stringify(ack));
    lastRef=ack.operation_ref;
    for(let i=0;i<400;++i) {
      const status=await call('vsr_operation_status',{operation_ref:ack.operation_ref});
      if(status.state==='terminal') {assert.equal(status.outcome,'ok');return status.result;}
      await new Promise(r=>setTimeout(r,25));
    }
    throw Error('metadata request timed out');
  };
  const discovery=await admitted('vsr_discover',{});
  const views=discovery.items.filter(x=>x.kind==='view');
  const viewport=views.find(x=>x.role==='interactive_viewport' && x.name==='Viewport');
  const agent=views.find(x=>x.role==='independent_agent');
  assert(viewport,'genuine interactive viewport must be registered');
  assert(agent,'independent AgentView must be distinct');
  assert.notEqual(viewport.id,agent.id);
  assert.notEqual(viewport.view_identity,agent.view_identity);
  let info;
  for(let i=0;i<200;++i) {
    info=await admitted('vsr_view_info',{view_id:viewport.id});
    if(info.state==='available')break;
    await new Promise(r=>setTimeout(r,50));
  }
  assert.equal(info.state,'available',JSON.stringify(info));
  assert.equal(info.device_identifier,device);
  assert.equal(info.view_identity,viewport.view_identity);
  assert(info.width>1 && info.height>1);
  assert(info.display.width>0 && info.display.height>0);
  assert.match(info.camera_handle,/^[0-9a-f]{32}$/);
  assert.match(info.renderer_handle,/^[0-9a-f]{32}$/);
  assert.equal(info.capture_policy.exposed,false);
  assert.equal(info.provenance_kind,'current_effective');
  assert.equal(info.rendered_configuration.reason,'not_observed');
  const preview=await admitted('vsr_view_info',{view_id:agent.id});
  assert.equal(preview.role,'independent_agent');
  assert.equal(preview.device_identifier,'helide');
  assert.equal(preview.width,640);assert.equal(preview.height,360);
  assert(!preview.camera_handle,'independent camera is not a Scene camera');
  const scene=discovery.items.find(x=>x.kind==='scene');
  const camera=await admitted('vsr_inspect',{scene_id:scene.id,handle:info.camera_handle});
  const renderer=await admitted('vsr_inspect',{scene_id:scene.id,handle:info.renderer_handle});
  assert.equal(camera.items.length,1);assert.equal(renderer.items.length,1);
  const proof={device,discovery,viewport:info,agent:preview,boundCamera:camera.items[0],boundRenderer:renderer.items[0],sceneWrites:0,pixelsQualified:false};
  let renderRef;
  if(device==='helide') {
    const receipt=await admitted('vsr_render',{view_id:agent.id,scene_revision:preview.scene_revision,view_revision:preview.view_revision});
    renderRef=lastRef;
    assert.equal(receipt.width,640);assert.equal(receipt.height,360);
    assert.equal(receipt.image_kind,'independent_render');
    assert.equal(receipt.role,'independent_agent');
    assert.equal(receipt.view_id,agent.id);
    assert.equal(receipt.rendered_configuration.device_identifier,'helide');
    assert.equal(receipt.rendered_configuration.provenance_kind,'rendered');
    const imageReply=await client.callTool('vsr_render_image',{operation_ref:renderRef});
    assert.equal(imageReply.content.filter(x=>x.type==='image' && x.mimeType==='image/png').length,1);
    proof.legacyRender={receipt,imageItems:1};
  }
  await writeFile(output,JSON.stringify(proof,null,2)+'\n');
  if(resizeMarker) {
    let ready=false;
    for(let i=0;i<400;++i) {
      try {await stat(resizeMarker);ready=true;break;} catch {}
      await new Promise(r=>setTimeout(r,25));
    }
    assert(ready,'private fixture resize did not occur');
    let resized;
    for(let i=0;i<200;++i) {
      resized=await admitted('vsr_view_info',{view_id:viewport.id});
      if(resized.state==='available' && (resized.width!==info.width || resized.height!==info.height))break;
      await new Promise(r=>setTimeout(r,25));
    }
    assert(resized.width!==info.width || resized.height!==info.height,'native size must follow real window resize');
    assert.notEqual(resized.view_revision,info.view_revision);
    assert.equal(resized.view_identity,info.view_identity);
    assert.equal(resized.device_identifier,device);
    assert.equal(resized.role,'interactive_viewport');
    assert.equal(resized.capture_policy.exposed,false);
    assert.equal(resized.rendered_configuration.reason,'not_observed');
    proof.resized=resized;
    if(renderRef) {
      const historical=await call('vsr_operation_status',{operation_ref:renderRef});
      assert.deepEqual(historical.result,proof.legacyRender.receipt);
      proof.legacyRender.historicalAfterResize=true;
    }
    await writeFile(output,JSON.stringify(proof,null,2)+'\n');
  }
  console.log('PASS distinct roles, actual bound references, measured sizes, real resize, closed capture');
} finally {await connection.close();}
