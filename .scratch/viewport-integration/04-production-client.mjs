// Actual vsrViewer production path; isolated controller, no model/user session.
import assert from 'node:assert/strict';
import {readFile,writeFile,copyFile} from 'node:fs/promises';
import {execFileSync} from 'node:child_process';
const pi='/nix/store/bkc4gfqj0dbfnavrz22ip7yx369f4sg7-pi-coding-agent-1.0.0/lib/pi-coding-agent';
const {McpServerConnection,createDefaultTransport}=await import(`${pi}/dist/extensions/mcp/runtime.js`);
const {createMcpToolDefinition}=await import(`${pi}/dist/extensions/mcp/tools.js`);
const [descriptor,output,device,resize]=process.argv.slice(2);
const oracles=process.env.VSR_PRODUCTION_ORACLE;
const transported=[];
const connection=new McpServerConnection({entry:{name:'production04',config:{command:`${process.cwd()}/_out/_cmake/RelWithDebInfo/vsrMcpProxy`,args:[descriptor],timeout:20,exposure:'direct'}},cwd:process.cwd(),createTransport:(...args)=>{const t=createDefaultTransport(...args);const send=t.send.bind(t);t.send=async(m,...rest)=>{if(m.method==='tools/call')transported.push(structuredClone(m.params));return send(m,...rest);};return t;},onTools:()=>{},onChange:()=>{}});
try {
  const client=await connection.getClient();
  const definitions=new Map((await client.listTools()).map(tool=>[tool.name,createMcpToolDefinition({name:`mcp__production04__${tool.name}`,server:'production04',tool,exposure:'direct',timeoutMs:20000,getClient:()=>connection.getClient()})]));
  let next=1,ref,busyRetries=0;
  const observations=[];
  const call=async(name,args)=>{for(let j=0;j<80;j++){try{return await definitions.get(name).execute(`production-${next}`,args);}catch(e){if(e?.data?.code!=='busy'||j===79){console.error('native call failed',name,JSON.stringify({args,busyRetries,observations:observations.length,last:observations.slice(-3)}));throw e;}busyRetries++;await new Promise(r=>setTimeout(r,25));}}};
  const admitted=async(name,args)=>{
    const ack=(await call(name,{request_id:next++,...args})).structuredContent.structuredContent;
    assert(ack.operation_ref,JSON.stringify(ack));ref=ack.operation_ref;
    for(let i=0;i<400;i++){const s=(await call('vsr_operation_status',{operation_ref:ref})).structuredContent.structuredContent;if(s.state==='terminal'){assert.equal(s.outcome,'ok');if(name==='vsr_view_frame'){observations.push({ref:{...ref},state:s.result.state,reason:s.result.reason,presentation:s.result.presentation_id,submitted:s.result.submitted_monotonic_seconds,observed:s.result.observed_monotonic_seconds});await writeFile(`${oracles}/observations.json`,JSON.stringify(observations,null,2));}return s.result;}await new Promise(r=>setTimeout(r,25));}
    throw Error('production operation timeout');
  };
  const discovery=await admitted('vsr_discover',{});
  const views=discovery.items.filter(x=>x.kind==='view'&&x.role==='interactive_viewport');
  assert(views.length>=1);const view=views[0];
  const agent=discovery.items.find(x=>x.kind==='view'&&x.role==='independent_agent');assert(agent);
  const evidence=[];let cold=0,historical;
  for(let i=0;i<32;i++){
    const start=performance.now();
    const frame=await admitted('vsr_view_frame',{view_id:view.id});
    if(frame.state!=='available'){cold++;await new Promise(r=>setTimeout(r,40));continue;}
    assert.equal(frame.presentation_id,frame.observed_presentation_id);
    assert.equal(frame.view_identity,view.view_identity);
    assert.equal(frame.rendered_configuration.device_identifier,device);
    assert.equal(frame.image_contract.origin,'top_left');assert.equal(frame.image_contract.color,'srgb_rgba8');
    assert.equal(frame.rendered_view_revision_basis,'anari_render_start');
    assert.equal(frame.pipeline_configuration_basis,'final_cpu_ready_upload');
    assert.equal(frame.display_configuration_basis,'checked_sdl_submission');
    const reply=await call('vsr_view_frame_image',{operation_ref:ref});
    const images=reply.content.filter(x=>x.type==='image');assert.equal(images.length,1);
    const png=Buffer.from(images[0].data,'base64');assert(png.length<=8388608);
    const path=`${oracles}/returned-${i}.png`;await writeFile(path,png);
    await writeFile(`${oracles}/frame-${i}.json`,JSON.stringify(frame,null,2));
    const allLines=(await readFile(`${oracles}/manifest.tsv`,'utf8')).trim().split('\n').map(l=>l.split('\t'));
    // Ring paths are reusable; old manifest rows cannot name their old bytes.
    const lines=[...new Map(allLines.map(l=>[l[5],l])).values()];
    const candidates=lines.filter(l=>Number(l[2])<=frame.submitted_monotonic_seconds+0.002).sort((a,b)=>Math.abs(Number(a[2])-frame.submitted_monotonic_seconds)-Math.abs(Number(b[2])-frame.submitted_monotonic_seconds));
    assert(candidates.length);const closest=candidates[0];
    assert(Math.abs(Number(closest[2])-frame.submitted_monotonic_seconds)<0.1);
    const bmp=`${oracles}/held-${i}.bmp`;await copyFile(closest[5],bmp);
    const parity=JSON.parse(execFileSync('python',[`${process.cwd()}/.scratch/viewport-integration/04-native-oracle.py`,path,bmp],{encoding:'utf8'}));
    assert.deepEqual(parity.native,[frame.width,frame.height]);
    evidence.push({frame,parity,pngBytes:png.length,roundTripMs:performance.now()-start,oracleDeltaSeconds:frame.submitted_monotonic_seconds-Number(closest[2])});
    if(!historical)historical={ref:{...ref},frame:structuredClone(frame),base64:images[0].data};
    if(evidence.length===8){await writeFile(output,JSON.stringify({phase:'await-private-resize'}));for(let j=0;j<200;j++){try{await readFile(resize);break;}catch{}await new Promise(r=>setTimeout(r,25));}await new Promise(r=>setTimeout(r,400));}
  }
  assert(evidence.length>=16,JSON.stringify({images:evidence.length,cold}));
  assert(new Set(evidence.map(e=>`${e.frame.width}x${e.frame.height}`)).size>=2);
  assert(evidence.some(e=>e.parity.flippedRgbError>e.parity.meanRgbError+0.5));
  assert(evidence.some(e=>e.parity.blackRgbError>e.parity.meanRgbError+2));
  const replay=(await call('vsr_view_frame',{request_id:historical.ref.request_id,view_id:view.id})).structuredContent.structuredContent;assert.equal(replay.replayed,true);
  const old=(await call('vsr_operation_status',{operation_ref:historical.ref})).structuredContent.structuredContent;assert.deepEqual(old.result,historical.frame);
  const oldImage=await call('vsr_view_frame_image',{operation_ref:historical.ref});assert.equal(oldImage.content.find(x=>x.type==='image').data,historical.base64);
  let lifecycle;
  if(process.env.VSR_PRODUCTION_LIFECYCLE){
    const {unlink,rename}=await import('node:fs/promises');
    const controlReceipts=[];
    const command=async(text)=>{
      if(text.startsWith('click ')){
        execFileSync('python',[`${process.cwd()}/.scratch/viewport-integration/04-private-input.py`,...text.split(' ').slice(1)]);
        return;
      }
      await unlink(`${oracles}/command-done`).catch(()=>{});
      await unlink(`${oracles}/scale-presented`).catch(()=>{});
      if(process.env.VSR_PRODUCTION_FRAGMENT_CONTROL&&text==='scale 2'){
        // Deterministic negative control for the former truncate/read/ack race.
        await writeFile(`${oracles}/command`,'');
        let attempted=false;
        for(let j=0;j<80;j++){
          let attempts='';try{attempts=await readFile(`${oracles}/command.tsv`,'utf8');}catch{}
          if(attempts.split('\n').some(l=>l.split('\t')[1]==='-1')){attempted=true;break;}
          await new Promise(r=>setTimeout(r,25));
        }
        assert(attempted,'negative control did not encounter a partial command');
        let acked=false;try{await readFile(`${oracles}/command-done`);acked=true;}catch{}
        assert.equal(acked,false,'incomplete control must not be acknowledged/applied');
        const beforeApplication=await admitted('vsr_view_frame',{view_id:reopened.id});
        assert.equal(beforeApplication.state,'available');
        assert.deepEqual(beforeApplication.rendered_configuration.display.framebuffer_scale,[1,1]);
        controlReceipts.push({incompleteCommandNotApplied:true,lastActuallyPresentedUnitFrame:beforeApplication.presentation_id,observed:beforeApplication.observed_monotonic_seconds});
      }
      // Publish a complete command; the UI cannot consume a truncated file.
      await writeFile(`${oracles}/command.tmp`,text);
      await rename(`${oracles}/command.tmp`,`${oracles}/command`);
      let applied;
      for(let j=0;j<400;j++){
        try{applied=JSON.parse(await readFile(`${oracles}/command-done`,'utf8'));break;}catch{}
        await new Promise(r=>setTimeout(r,25));
      }
      assert(applied,'private UI application receipt timeout');
      assert.equal(applied.action,'scale');assert.equal(applied.scale,Number(text.split(' ')[1]));
      // Application is not presentation: await the actual successful viewport
      // quad at this command's profile, not a blind delay or frame-state retry.
      for(let j=0;j<400;j++){
        let fields;try{fields=(await readFile(`${oracles}/scale-presented`,'utf8')).trim().split('\t').map(Number);}catch{}
        if(fields&&fields[0]===applied.sequence&&fields[3]===applied.scale){
          const receipt={...applied,oraclePresentation:fields[1],submitted:fields[2]};
          controlReceipts.push(receipt);return receipt;
        }
        await new Promise(r=>setTimeout(r,25));
      }
      throw Error('private UI matching presentation receipt timeout');
    };
    // Coordinates belong to this fresh private 1280x960 fixture only. Input
    // uses actual XTest events and the real production Window/menu handlers.
    await command('click 1261 41');
    let closed;
    for(let j=0;j<80;j++){closed=await admitted('vsr_discover',{});if(!closed.items.some(x=>x.kind==='view'&&x.id===view.id))break;await new Promise(r=>setTimeout(r,25));}
    assert(!closed.items.some(x=>x.kind==='view'&&x.id===view.id),'production close did not unregister');
    const closedStatus=(await call('vsr_operation_status',{operation_ref:historical.ref})).structuredContent.structuredContent;
    assert.equal(closedStatus.result_visible,false);assert(!closedStatus.result);
    const closedImage=await call('vsr_view_frame_image',{operation_ref:historical.ref});assert.equal(closedImage.content.filter(x=>x.type==='image').length,0);
    await command('click 273 12');await command('click 330 150');await command('click 800 800');
    let reopened;
    for(let j=0;j<80;j++){const d=await admitted('vsr_discover',{});reopened=d.items.find(x=>x.kind==='view'&&x.role==='interactive_viewport');if(reopened)break;await new Promise(r=>setTimeout(r,25));}
    assert(reopened,'production reopen did not register');assert.notEqual(reopened.id,view.id);assert.notEqual(reopened.view_identity,view.view_identity);
    let fresh;
    for(let j=0;j<80;j++){fresh=await admitted('vsr_view_frame',{view_id:reopened.id});if(fresh.state==='available')break;await new Promise(r=>setTimeout(r,25));}
    assert.equal(fresh.state,'available');assert.equal(fresh.view_identity,reopened.view_identity);
    const freshRef={...ref};const freshImage=await call('vsr_view_frame_image',{operation_ref:freshRef});assert.equal(freshImage.content.filter(x=>x.type==='image').length,1);
    const oldAfterReopen=(await call('vsr_operation_status',{operation_ref:historical.ref})).structuredContent.structuredContent;assert.equal(oldAfterReopen.result_visible,false);assert(!oldAfterReopen.result);
    const unsupportedReceipt=await command('scale 2');
    const scaled=await admitted('vsr_view_frame',{view_id:reopened.id});await writeFile(`${oracles}/scale-observation.json`,JSON.stringify(scaled,null,2));assert.equal(scaled.state,'unavailable');assert.equal(scaled.reason,'unsupported_display_scale_profile');
    const scaleImage=await call('vsr_view_frame_image',{operation_ref:ref});assert.equal(scaleImage.content.filter(x=>x.type==='image').length,0);
    const supportedReceipt=await command('scale 1');
    let restored;
    for(let j=0;j<80;j++){restored=await admitted('vsr_view_frame',{view_id:reopened.id});if(restored.state==='available')break;await new Promise(r=>setTimeout(r,25));}assert.equal(restored.state,'available');
    assert(restored.observed_monotonic_seconds>=supportedReceipt.submitted);
    assert(restored.submitted_monotonic_seconds>=supportedReceipt.submitted);
    assert.deepEqual(restored.rendered_configuration.display.framebuffer_scale,[1,1]);
    // The real production UI provides local whole-session read withdrawal.
    // Narrow Scene READ withdrawal is separately covered by the same adapter
    // on the actual SDL/public native fixture; do not pretend this is narrow.
    await command('click 377 162');await command('click 400 162');
    let withdrawn;
    try{withdrawn=await call('vsr_view_frame_image',{operation_ref:freshRef});assert.equal(withdrawn.content.filter(x=>x.type==='image').length,0);assert(!JSON.stringify(withdrawn).includes('rendered_configuration'));}
    catch(e){assert(/unauthorized|revoked|session|401|403/i.test(String(e)),String(e));withdrawn={transportDenied:true};}
    lifecycle={controlReceipts,fragmentedControlRejected:!!process.env.VSR_PRODUCTION_FRAGMENT_CONTROL,closedOldIdentityRetired:true,closedStatusMinimal:true,oldImageDenied:true,reopenedId:reopened.id,reopenedIdentity:reopened.view_identity,freshNativePng:true,oldIdentityNotRevived:true,actualSDLScale2Rejected:scaled,unitScaleRestored:true,unitScaleRestoredFrame:restored,currentReadWithdrawal:'actual production UI whole-session disconnect',withdrawn};
  }
  assert(transported.every(x=>['vsr_discover','vsr_view_frame','vsr_view_frame_image','vsr_operation_status'].includes(x.name)));
  await writeFile(output,JSON.stringify({device,host:'actual installed vsrViewer production',productionExposed:true,lifecycle,busyRetries,images:evidence.length,cold,historicalReplaySamePixels:true,sceneWrites:0,extraRenderRequests:0,agentIdentity:agent.view_identity,viewIdentity:view.view_identity,evidence},null,2)+'\n');
  console.log(`PASS ${evidence.length} actual production native PNGs, resize, historical replay and independent diagnostic SDL framebuffer parity`);
}finally{await connection.close();}
