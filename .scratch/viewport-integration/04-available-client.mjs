// Private real SDL test-host sources, NOT production exposure qualification.
import assert from 'node:assert/strict';
import {writeFile,stat} from 'node:fs/promises';
import {execFileSync} from 'node:child_process';
const pi='/nix/store/bkc4gfqj0dbfnavrz22ip7yx369f4sg7-pi-coding-agent-1.0.0/lib/pi-coding-agent';
const {McpServerConnection,createDefaultTransport}=await import(`${pi}/dist/extensions/mcp/runtime.js`);
const {createMcpToolDefinition}=await import(`${pi}/dist/extensions/mcp/tools.js`);
const [descriptor,output,device,done,oracles]=process.argv.slice(2);
const transported=[];
const connection=new McpServerConnection({entry:{name:'isolated_viewport_04_available',config:{command:`${process.cwd()}/_out/_cmake/RelWithDebInfo/vsrMcpProxy`,args:[descriptor],timeout:20,exposure:'direct'}},cwd:process.cwd(),createTransport:(...args)=>{const t=createDefaultTransport(...args);const send=t.send.bind(t);t.send=async(m,...rest)=>{if(m.method==='tools/call')transported.push(structuredClone(m.params));return send(m,...rest);};return t;},onTools:()=>{},onChange:()=>{}});
try {
  const client=await connection.getClient();
  const tools=await client.listTools();
  const definitions=new Map(tools.map(tool=>[tool.name,createMcpToolDefinition({name:`mcp__isolated_viewport_04_available__${tool.name}`,server:'isolated_viewport_04_available',tool,exposure:'direct',timeoutMs:20000,getClient:()=>connection.getClient()})]));
  assert.deepEqual(definitions.get('vsr_view_frame').parameters.required,['request_id','view_id']);
  let next=1;let ref;
  const call=async(name,args)=>await definitions.get(name).execute(`fixture-${next}`,args);
  const admitted=async(name,args)=>{
    const ack=(await call(name,{request_id:next++,...args})).structuredContent.structuredContent;
    assert(ack.operation_ref,JSON.stringify(ack));ref=ack.operation_ref;
    for(let i=0;i<400;i++){const status=(await call('vsr_operation_status',{operation_ref:ack.operation_ref})).structuredContent.structuredContent;if(status.state==='terminal'){assert.equal(status.outcome,'ok');return status.result;}await new Promise(r=>setTimeout(r,25));}
    throw Error('real viewer operation timeout');
  };
  const discovery=await admitted('vsr_discover',{});
  const views=discovery.items.filter(x=>x.kind==='view'&&x.role==='interactive_viewport');
  assert(views.length===1||views.length===2);
  const evidence=[];const latencies=[];let unavailable=0;let historical;
  const lastPresentation=new Map();
  for(let i=0;i<32;i++){
    const view=views[i%views.length];const start=performance.now();
    const frame=await admitted('vsr_view_frame',{view_id:view.id});
    if(frame.state!=='available'){assert.equal(frame.state,'unavailable');unavailable++;await new Promise(r=>setTimeout(r,35));continue;}
    assert.equal(frame.presentation_id,frame.observed_presentation_id);
    assert.equal(frame.view_identity,view.view_identity);
    assert.equal(frame.image_kind,'presented_frame');
    assert.equal(frame.rendered_view_revision_basis,'anari_render_start');
    assert.equal(frame.pipeline_configuration_basis,'final_cpu_ready_upload');
    assert.equal(frame.display_configuration_basis,'checked_sdl_submission');
    assert.equal(frame.image_contract.origin,'top_left');
    assert.equal(frame.image_contract.color,'srgb_rgba8');
    assert.equal(frame.rendered_configuration.device_identifier,device);
    assert(frame.observed_monotonic_seconds>=frame.submitted_monotonic_seconds);
    assert(frame.presentation_id>=(lastPresentation.get(view.id)??0));
    lastPresentation.set(view.id,frame.presentation_id);
    if(frame.stale)assert(frame.rendered_scene_revision!==frame.current_scene_revision||frame.rendered_view_revision!==frame.current_view_revision);
    const reply=await call('vsr_view_frame_image',{operation_ref:ref});
    const images=reply.content.filter(x=>x.type==='image');
    assert.equal(images.length,1,JSON.stringify(reply.structuredContent));
    assert.equal(images[0].mimeType,'image/png');
    const png=Buffer.from(images[0].data,'base64');assert(png.length<=8388608);
    if(!historical)historical={ref:{...ref},viewId:view.id,frame:structuredClone(frame),base64:images[0].data};
    const file=`${oracles}/returned-${i}.png`;await writeFile(file,png);
    const bmp=`${oracles}/view-${view.id}-present-${frame.presentation_id}.bmp`;
    for(let n=0;n<100;n++){try{await stat(bmp);break;}catch{}await new Promise(r=>setTimeout(r,10));}
    const parity=JSON.parse(execFileSync('python',[`${process.cwd()}/.scratch/viewport-integration/04-native-oracle.py`,file,bmp],{encoding:'utf8'}));
    assert.equal(parity.native[0],frame.width);assert.equal(parity.native[1],frame.height);
    latencies.push(performance.now()-start);
    evidence.push({viewIdentity:view.view_identity,presentation:frame.presentation_id,production:frame.production_id,sourceFrame:frame.source_frame_id,stale:frame.stale,renderedScene:frame.rendered_scene_revision,currentScene:frame.current_scene_revision,renderedView:frame.rendered_view_revision,currentView:frame.current_view_revision,renderedConfiguration:frame.rendered_configuration,pngBytes:png.length,parity});
    await new Promise(r=>setTimeout(r,20));
  }
  assert(evidence.length>=12,JSON.stringify({images:evidence.length,unavailable}));
  for(const view of views)assert(evidence.some(x=>x.viewIdentity===view.view_identity));
  assert(evidence.some(x=>x.parity.flippedRgbError>x.parity.meanRgbError+0.5),'orientation negative control must differ');
  assert(evidence.some(x=>x.parity.blackRgbError>x.parity.meanRgbError+2),'color/composition negative control must differ');
  const replay=(await call('vsr_view_frame',{request_id:historical.ref.request_id,view_id:historical.viewId})).structuredContent.structuredContent;
  assert.equal(replay.replayed,true);
  const old=(await call('vsr_operation_status',{operation_ref:historical.ref})).structuredContent.structuredContent;
  assert.deepEqual(old.result,historical.frame);
  const oldImage=await call('vsr_view_frame_image',{operation_ref:historical.ref});
  assert.equal(oldImage.content.find(x=>x.type==='image').data,historical.base64);
  await writeFile(done+'-withdraw','withdraw private test source read\n');
  let protectedStatus;
  for(let i=0;i<200;i++){
    protectedStatus=(await call('vsr_operation_status',{operation_ref:historical.ref})).structuredContent.structuredContent;
    if(protectedStatus.result_visible===false)break;
    await new Promise(r=>setTimeout(r,25));
  }
  assert.equal(protectedStatus.result_visible,false);
  assert(!('result' in protectedStatus));
  const denied=await call('vsr_view_frame_image',{operation_ref:historical.ref});
  assert.equal(denied.content.filter(x=>x.type==='image').length,0);
  assert.equal(denied.structuredContent.structuredContent.result_visible,false);
  const allowed=new Set(['vsr_discover','vsr_view_frame','vsr_view_frame_image','vsr_operation_status']);
  assert(transported.every(x=>allowed.has(x.name)));
  assert(transported.filter(x=>x.name==='vsr_view_frame').every(x=>Object.keys(x.arguments).sort().join(',')==='request_id,view_id'));
  await writeFile(output,JSON.stringify({device,client:'installed PiToolDefinition/McpServerConnection/stdio proxy/native HTTP/FrameTools/real Viewport',host:'isolated real SDL test application, not vsrViewer production exposure',productionExposed:false,historicalReplaySamePixels:true,currentReadWithdrawalProtected:true,images:evidence.length,unavailable,views:views.map(v=>v.view_identity),maxRoundTripMs:Math.max(...latencies),sceneWrites:0,externalModelProviderRequest:false,evidence},null,2)+'\n');
  await writeFile(done,'native image proof complete\n');
  console.log(`PASS ${evidence.length} real native images with independent SDL oracle; production remains guarded`);
} finally {await connection.close();}
