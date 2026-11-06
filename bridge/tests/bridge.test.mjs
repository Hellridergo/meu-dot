import {test} from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { readFileSync, readdirSync } from 'node:fs';
import { createHmac } from 'node:crypto';
import { routeDot } from '../worker/bridge/service.mjs';
import { drain } from '../worker/bridge/events.mjs';

test('0002 preserves existing subscriptions, events and pending or answered cards',()=>{
  const db=new DatabaseSync(':memory:');
  for(const file of ['0000_purple_odin.sql','0001_subscription_diagnostics.sql'])
    db.exec(readFileSync(new URL('../drizzle/'+file,import.meta.url),'utf8'));
  db.exec(`
    INSERT INTO voices VALUES('voice-old','existing transcript',2.5,100);
    INSERT INTO cards VALUES('notice-old','existing notice',0,101,NULL,NULL);
    INSERT INTO cards VALUES('decision-pending','existing decision',1,102,NULL,NULL);
    INSERT INTO cards VALUES('decision-answered','existing answer',1,103,'yes',104);
    INSERT INTO subscriptions VALUES('sub-voice','device.transcript','','sealed-existing-voice',100,9999999999999);
    INSERT INTO subscriptions VALUES('sub-reply','device.reply','','sealed-existing-reply',100,9999999999999);
    INSERT INTO deliveries VALUES('sub-voice','voice_voice-old','device.transcript','{}',100,'pending',2,120,NULL);
    INSERT INTO deliveries VALUES('sub-reply','reply_decision-answered','device.reply','{}',104,'sent',1,104,NULL);
    INSERT INTO mobile VALUES(1,110,'android-3');
    INSERT INTO subscription_checks VALUES('device.transcript',100,'active',200);
  `);
  const tables=['voices','cards','subscriptions','deliveries','mobile','subscription_checks'];
  const before=new Map(tables.map(table=>[table,db.prepare('SELECT * FROM '+table+' ORDER BY rowid').all()]));
  db.exec(readFileSync(new URL('../drizzle/0002_delivery_receipts.sql',import.meta.url),'utf8'));
  for(const [table,rows] of before){
    const columns=Object.keys(rows[0]).join(',');
    assert.deepEqual(db.prepare('SELECT '+columns+' FROM '+table+' ORDER BY rowid').all(),rows,table);
  }
  assert.equal(db.prepare('SELECT count(*) AS n FROM cards WHERE delivered IS NULL AND delivery_stage IS NULL AND recording_id IS NULL').get().n,3);
  assert.equal(db.prepare('SELECT count(*) AS n FROM deliveries WHERE attempted IS NULL AND sent IS NULL AND http IS NULL').get().n,2);
  db.close();
});

class TestD1 {
  constructor(){this.raw=new DatabaseSync(':memory:');const files=readdirSync(new URL('../drizzle/',import.meta.url)).filter(f=>f.endsWith('.sql')).sort();
    for(const f of files)this.raw.exec(readFileSync(new URL('../drizzle/'+f,import.meta.url),'utf8'));}
  prepare(sql){
    const stmt=this.raw.prepare(sql);let values=[];
    const q={bind(...v){values=v;return q;},first(){return stmt.get(...values)??null;},run(){return {meta:{changes:stmt.run(...values).changes}};},
      all(){return {results:stmt.all(...values)};},exec(){return stmt.columns().length?q.all():q.run();}};return q;}
  batch(list){this.raw.exec('BEGIN');try{const res=list.map(q=>q.exec());this.raw.exec('COMMIT');return res;}catch(e){this.raw.exec('ROLLBACK');throw e;}}
}
function setup(){
  const db=new TestD1(),messages=[],queued=[];const key=Buffer.alloc(32,8),ownerEmail='local@example.test';
  const env={DB:db,DOT_OWNER_EMAIL:ownerEmail,EVENT_KEY_HEX:'76'.repeat(32),MOBILE_TOKEN:'f'.repeat(64),
    BUCKET:{async head(){return null;}},callbackFetch:async(url,opts)=>{
      assert.equal(new URL(url).hostname,'connectors.api.openai.com');assert.equal(opts.redirect,'manual');
      const h=opts.headers,sign=createHmac('sha256',key).update(h['webhook-id']+'.'+h['webhook-timestamp']+'.'+opts.body).digest('base64');
      assert.equal(h['webhook-signature'],'v1,'+sign);const p=JSON.parse(opts.body);
      if(p.type==='verification'){assert.match(h['webhook-id'],/^msg_verification_/);return Response.json({challenge:p.challenge});}
      messages.push(p);return new Response(null,{status:202});}};
  const own={'oai-authenticated-user-email':ownerEmail,'oai-authenticated-user-id':'owner-01'};
  const phone={authorization:'Bearer '+env.MOBILE_TOKEN,'X-Dot-Client':'android-2'};
  function request(path,{method='GET',headers={},data,raw}={}){
    return routeDot(new Request('https://site.example'+path,{method,headers:{'content-type':'application/json',...headers},
      ...(data!==undefined?{body:JSON.stringify(data)}:raw?{body:raw}:{})}),env,{waitUntil(p){queued.push(p);}});}
  async function rpc(method,params={},headers=own){return (await request('/mcp',{method:'POST',headers,data:{jsonrpc:'2.0',id:7,method,params}})).json();}
  const subscription=(name,url='https://connectors.api.openai.com/test',args={})=>({name,arguments:args,delivery:{mode:'webhook',url,secret:'whsec_'+key.toString('base64')}});
  return {env,own,phone,messages,request,rpc,subscription,flush:()=>Promise.all(queued.splice(0))};
}
test('public routes isolate phone, private installer, owner and MCP writes',async()=>{
  const {env,own,phone,request,rpc}=setup();
  assert.equal((await request('/health')).status,200);
  for(const p of ['/state','/choice','/voice/a','/transcript/a']) assert.equal((await request(p)).status,401);
  assert.equal((await request('/instalar')).status,302);assert.equal((await request('/admin/status')).status,403);
  assert.equal((await request('/admin/token',{method:'POST',headers:{...own,origin:'https://outside.example'}})).status,403);
  assert.equal((await request('/admin/token',{method:'POST',headers:{...phone,origin:'https://site.example'}})).status,403);
  assert.equal((await(await request('/admin/token',{method:'POST',headers:{...own,origin:'https://site.example'}})).json()).token,env.MOBILE_TOKEN);
  assert.equal((await request('/state',{headers:{...phone,origin:'https://outside.example'}})).status,401);
  assert.equal((await rpc('tools/call',{name:'get_device_status',arguments:{}},{})).error.code,-32001);
  assert.equal((await rpc('events/subscribe',{},{'oai-authenticated-user-email':'intruder@example.test','oai-authenticated-user-id':'intruder'})).error.code,-32001);
  assert.equal((await request('/_release/android',{method:'PUT',headers:phone})).status,401);
  assert.equal((await rpc('tools/list',{},{})).result.tools.length,5);
  assert.equal((await rpc('events/list',{},{})).result.events.length,2);
});
test('voice and decision commit once; signed callbacks, retries, filter and unsubscribe',async()=>{
  const {env,phone,request,rpc,messages,subscription,flush}=setup();
  const id=crypto.randomUUID(),data={text:'Qual é a próxima reunião?',audio_seconds:2.956};
  assert.equal((await(await request('/state',{headers:phone})).json()).reason,'dot_subscription');
  assert.equal((await request('/transcript/'+id,{method:'POST',headers:phone,data})).status,503);
  for(const bad of ['https://127.0.0.1/test','https://connectors.api.openai.com@evil.test/test','https://connectors.api.openai.com.evil.test/test','http://connectors.api.openai.com/test'])
    assert.ok((await rpc('events/subscribe',subscription('device.transcript',bad))).error);
  for(const name of ['device.transcript','device.reply'])assert.ok((await rpc('events/subscribe',subscription(name))).result.id);
  assert.equal((await(await request('/state',{headers:phone})).json()).ready,true);
  assert.equal((await(await request('/state',{headers:{authorization:phone.authorization}})).json()).reason,'client_update');
  assert.equal((await request('/voice/'+id,{method:'POST',headers:phone,data})).status,426);
  assert.equal((await request('/transcript/'+id,{method:'POST',headers:phone,data})).status,201);await flush();
  assert.equal((await request('/transcript/'+id,{method:'POST',headers:phone,data})).status,200);await flush();
  assert.equal((await request('/transcript/'+id,{method:'POST',headers:phone,data:{...data,text:'outro'}})).status,409);
  assert.equal(messages.length,1);assert.equal(messages[0].data.text,data.text);
  const a={request_id:'decisao-01',text:'Você confirma a revisão?'};
  assert.equal((await rpc('tools/call',{name:'ask_confirmation',arguments:a})).result.isError,false);
  assert.equal((await rpc('tools/call',{name:'send_message',arguments:a})).result.isError,true);
  assert.equal((await(await request('/state',{headers:phone})).json()).card.decision,true);
  const answer={id:a.request_id,option:'yes'};
  assert.equal((await request('/choice',{method:'POST',headers:phone,data:{...answer,option:'dismiss'}})).status,409);
  assert.equal((await request('/choice',{method:'POST',headers:phone,data:answer})).status,200);await flush();
  assert.equal((await request('/choice',{method:'POST',headers:phone,data:answer})).status,200);await flush();
  assert.equal(messages.length,2);assert.equal(messages[1].data.option_id,'yes');assert.equal((await(await request('/state',{headers:phone})).json()).card,null);
  const s=subscription('device.transcript');delete s.delivery.secret;assert.deepEqual((await rpc('events/unsubscribe',s)).result,{});
  assert.equal((await(await request('/state',{headers:phone})).json()).ready,false);
  const stored=env.DB.raw.prepare('SELECT sealed FROM subscriptions').get().sealed;
  assert.ok(!stored.includes('https')&&!stored.includes('whsec_'));
});
test('transient failure preserves event id and later retry delivers',async()=>{
  const {env,phone,request,rpc,subscription,flush,messages}=setup();
  const p=subscription('device.transcript');await rpc('events/subscribe',p);
  const send=env.callbackFetch;let first=true;
  env.callbackFetch=async(u,o)=>{if(first){first=false;return new Response(null,{status:503});}return send(u,o);};
  const id=crypto.randomUUID();
  await request('/transcript/'+id,{method:'POST',headers:phone,data:{text:'Teste',audio_seconds:1}});await flush();
  assert.equal(env.DB.raw.prepare('SELECT phase FROM deliveries').get().phase,'pending');
  env.DB.raw.exec("UPDATE deliveries SET due=0");
  await Promise.all([drain(env),drain(env)]);
  assert.equal(messages.length,1);assert.equal(messages[0].eventId,'voice_'+id);
});
test('registration reports verification status without leaking data or activating a failed result',async()=>{
  const {env,own,rpc,request,subscription}=setup(),normal=env.callbackFetch;
  const s=subscription('device.transcript','https://connectors.api.openai.com/private-endpoint');
  for(const [fn,reason,stage,code] of [
    [async()=>new Response('must not appear',{status:403}),'http_4xx','callback_http',403],
    [async()=>Response.redirect('https://outside.example/'),'challenge_failed','callback_redirect',302],
    [async()=>Response.json({challenge:'not the nonce'}),'challenge_failed','challenge_failed',200],
    [async()=>{throw new DOMException('private message','TimeoutError');},'timeout','timeout',null]
  ]){
    env.callbackFetch=fn;
    const r=await rpc('events/subscribe',s);
    assert.equal(r.error.code,-32015);assert.equal(r.error.data.reason,reason);
    const v=await(await request('/admin/status',{headers:own})).json();
    assert.equal(v.voice_subscriptions,0);assert.equal(v.subscription_checks.length,1);
    const c=v.subscription_checks[0];assert.deepEqual([c.name,c.stage,c.http],['device.transcript',stage,code]);
    assert.ok(!/whsec_|private-endpoint|must not appear|private message|not the nonce/.test(JSON.stringify([r,v])));
  }
  env.callbackFetch=normal;
  const r=await rpc('events/subscribe',s);assert.ok(r.result.id);
  const v=(await rpc('tools/call',{name:'get_device_status',arguments:{}})).result.structuredContent;
  assert.equal(v.subscription_checks[0].stage,'active');assert.equal(v.voice_subscriptions,1);
  assert.ok((await rpc('events/subscribe',subscription('device.reply'),{})).error);
  assert.equal((await(await request('/admin/status',{headers:own})).json()).subscription_checks.length,1);
});
test('the 0.3 handset registers as android-3, accepts 30 seconds, and rejects a longer recording',async()=>{
  const {phone,request,subscription,rpc,flush}=setup();
  const headers={...phone,'X-Dot-Client':'android-3'};
  await rpc('events/subscribe',subscription('device.transcript'));
  const state=await(await request('/state',{headers})).json();
  assert.equal(state.ready,true);
  assert.equal((await rpc('tools/call',{name:'get_device_status',arguments:{}})).result.structuredContent.client,'android-3');
  const input={text:'Verificação de trinta segundos.',audio_seconds:30};
  assert.equal((await request('/transcript/'+crypto.randomUUID(),{method:'POST',headers,data:input})).status,201);
  await flush();
  assert.equal((await request('/transcript/'+crypto.randomUUID(),{method:'POST',headers,data:{...input,audio_seconds:30.001}})).status,400);
});

test('android-4 receipts advance notices, never answer a confirmation or publish a human reply',async()=>{
  const {env,phone,request,rpc,subscription,flush,messages}=setup();
  const headers={...phone,'X-Dot-Client':'android-4'};
  await rpc('events/subscribe',subscription('device.reply'));
  const add=(name,id)=>rpc('tools/call',{name,arguments:{request_id:id,text:'Mensagem '+id}});
  await add('send_message','notice-1');await add('ask_confirmation','decision-2');await add('send_message','notice-3');
  const state=()=>request('/state',{headers}).then(r=>r.json());
  assert.equal((await state()).receipts,true);assert.equal((await state()).card.id,'notice-1');
  assert.equal((await request('/receipt',{method:'POST',headers:phone,data:{id:'notice-1',stage:'played'}})).status,426);
  const receipt=id=>request('/receipt',{method:'POST',headers,data:{id,stage:'played'}});
  assert.equal((await receipt('notice-1')).status,200);assert.equal((await receipt('notice-1')).status,200);
  assert.equal((await state()).card.id,'decision-2');
  assert.equal((await receipt('decision-2')).status,409);
  assert.equal((await receipt('notice-3')).status,409);
  assert.equal((await request('/choice',{method:'POST',headers,data:{id:'decision-2',option:'dismiss'}})).status,409);
  assert.equal((await request('/choice',{method:'POST',headers,data:{id:'notice-3',option:'dismiss'}})).status,409);
  await flush();assert.equal(messages.length,0);
  const row=env.DB.raw.prepare('SELECT answer,delivered,delivery_stage FROM cards WHERE id=?').get('notice-1');
  assert.equal(row.answer,null);assert.ok(row.delivered>0);assert.equal(row.delivery_stage,'played');
  // Legacy clients preserve their explicit notice-dismiss flow during rollout.
  assert.equal((await(await request('/state',{headers:phone})).json()).card.id,'notice-1');
  assert.equal((await request('/choice',{method:'POST',headers,data:{id:'decision-2',option:'yes'}})).status,200);
  await flush();assert.equal(messages.length,1);assert.equal(messages[0].data.kind,'decision');assert.equal(messages[0].data.option_id,'yes');
  assert.equal((await state()).card.id,'notice-3');
  assert.equal((await receipt('missing')).status,404);
  assert.equal((await request('/receipt',{method:'POST',headers,data:{id:'notice-3',stage:'yes'}})).status,400);
});

test('recording correlation and dispatch timestamps remain identifiable and idempotent',async()=>{
  const {env,phone,rpc,request,subscription,flush}=setup();
  const headers={...phone,'X-Dot-Client':'android-4'},recording=crypto.randomUUID();
  await rpc('events/subscribe',subscription('device.transcript'));
  await request('/transcript/'+recording,{method:'POST',headers,data:{text:'Teste local',audio_seconds:2}});await flush();
  const delivery=env.DB.raw.prepare('SELECT created,attempted,sent,http,tries FROM deliveries').get();
  assert.ok(delivery.attempted>=delivery.created);assert.ok(delivery.sent>=delivery.attempted);assert.equal(delivery.http,202);assert.equal(delivery.tries,1);
  const voice=(await rpc('tools/call',{name:'get_voice_input',arguments:{recording_id:recording}})).result.structuredContent;
  assert.equal(voice.received_at,delivery.created);assert.equal(voice.dispatches[0].sent,delivery.sent);
  assert.ok(!JSON.stringify(voice.dispatches).includes('connectors.api'));
  const args={request_id:'response-1',text:'Entendido.',recording_id:recording};
  assert.equal((await rpc('tools/call',{name:'send_message',arguments:args})).result.isError,false);
  assert.equal((await rpc('tools/call',{name:'send_message',arguments:args})).result.isError,false);
  const state=await(await request('/state',{headers})).json();assert.equal(state.card.recording_id,recording);
  assert.equal((await rpc('tools/call',{name:'send_message',arguments:{...args,recording_id:crypto.randomUUID()}})).result.isError,true);
  const view=(await rpc('tools/call',{name:'get_request',arguments:{request_id:'response-1'}})).result.structuredContent;
  assert.equal(view.recording_id,recording);assert.equal(view.status,'pending');assert.ok(view.created_at>0);
});

test('receipt is authenticated and does not consume an unreceived notice',async()=>{
  const {phone,request,rpc}=setup();const headers={...phone,'X-Dot-Client':'android-4'};
  await rpc('tools/call',{name:'send_message',arguments:{request_id:'notice-waiting',text:'Ainda na fila.'}});
  assert.equal((await request('/receipt',{method:'POST',data:{id:'notice-waiting',stage:'played'}})).status,401);
  assert.equal((await(await request('/state',{headers})).json()).card.id,'notice-waiting');
  assert.equal((await request('/receipt',{method:'POST',headers,data:{id:'notice-waiting',stage:'displayed'}})).status,200);
  assert.equal((await(await request('/state',{headers})).json()).card,null);
  const result=(await rpc('tools/call',{name:'get_request',arguments:{request_id:'notice-waiting'}})).result.structuredContent;
  assert.equal(result.status,'delivered');assert.equal(result.selected_option,null);assert.equal(result.delivery_stage,'displayed');
});
