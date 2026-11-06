import {bearer,fail,isOwner,json,packetId,Problem,query,readObject,sameOrigin,validText,voiceEvent,voiceId} from './common.mjs';
import {drain,enqueueVoice,enqueueChoice,status} from './events.mjs';
import {mcp} from './protocol.mjs';

const apkKey='releases/android-v3.apk';
const authorizedDownload=(req,env)=>isOwner(req,env);
function method(req,wanted){if(req.method!==wanted)fail(405,'method_not_allowed');}
export function belongs(path){
  return ['/health','/state','/choice','/receipt','/mcp','/instalar','/_release/android','/admin/status','/admin/token'].includes(path)||
    path.startsWith('/voice/')||path.startsWith('/transcript/');
}
async function receiveVoice(req,env,id){
  if(!voiceId.test(id))fail(400,'invalid_id');
  const data=await readObject(req);const msg=data.text;
  if(!validText(msg,4000,12000)||typeof data.audio_seconds!=='number'||!Number.isFinite(data.audio_seconds)||
     data.audio_seconds<0.20||data.audio_seconds>30)fail(400,'invalid_transcript');
  const now=Date.now();
  const result=await env.DB.batch([
    query(env.DB,`INSERT OR IGNORE INTO voices(id,message,seconds,created)
      SELECT ?,?,?,? WHERE EXISTS(SELECT 1 FROM subscriptions WHERE name=? AND expires>?)`,id,msg,data.audio_seconds,now,voiceEvent,now),
    enqueueVoice(env.DB,id,now),
    query(env.DB,'SELECT message,seconds FROM voices WHERE id=?',id)
  ]);
  const row=result[2].results[0];
  if(!row)fail(503,'dot_subscription');
  if(row.message!==msg||row.seconds!==data.audio_seconds)fail(409,'id_conflict');
  return json({id,queued:true},result[0].meta.changes?201:200);
}
async function receiveReceipt(req,env){
  const {id,stage}=await readObject(req,2048);
  if(typeof id!=='string'||!packetId.test(id)||!['displayed','played','speech_failed'].includes(stage))fail(400,'invalid_receipt');
  // A transport receipt never records a human decision or emits device.reply.
  const r=await env.DB.batch([
    query(env.DB,`UPDATE cards SET delivered=?,delivery_stage=? WHERE id=? AND decision=0 AND delivered IS NULL AND answer IS NULL
      AND id=(SELECT id FROM cards WHERE answer IS NULL AND (decision=1 OR delivered IS NULL) ORDER BY created,rowid LIMIT 1)`,Date.now(),stage,id),
    query(env.DB,'SELECT decision,answer,delivered FROM cards WHERE id=?',id)
  ]);
  const card=r[1].results[0];
  if(!card)fail(404,'unknown_card');
  if(card.decision)fail(409,'explicit_decision_required');
  if(!card.delivered&&!card.answer)fail(409,'receipt_out_of_order');
  return json({saved:true});
}
async function receiveChoice(req,env){
  const modern=req.headers.get("X-Dot-Client")==="android-4";
  const {id,option}=await readObject(req,2048);
  if(typeof id!=='string'||!packetId.test(id)||!['yes','no','dismiss'].includes(option))fail(400,'invalid_choice');
  const now=Date.now(),db=env.DB;
  const r=await db.batch([
    query(db,`UPDATE cards SET answer=?,answered=? WHERE id=? AND answer IS NULL AND
      ((decision=1 AND ? IN ('yes','no')) OR (decision=0 AND ?='dismiss')) AND
      id=(SELECT id FROM cards WHERE answer IS NULL AND (?=0 OR decision=1 OR delivered IS NULL) ORDER BY created,rowid LIMIT 1)`,option,now,id,option,option,modern?1:0),
    enqueueChoice(db,id,now),
    query(db,'SELECT answer FROM cards WHERE id=?',id)
  ]);
  if(!r[2].results.length)fail(404,'unknown_card');
  if(r[2].results[0].answer!==option)fail(409,'choice_conflict');
  return json({saved:true});
}
async function phone(req,env,ctx,path){
  if(req.headers.has('origin') || !await bearer(req,env.MOBILE_TOKEN))fail(401,'mobile_credential_required');
  const header=req.headers.get('X-Dot-Client');
  const modern=header==='android-4';
  const cur=modern||header==='android-2'||header==='android-3';
  if(path==='/state'){
    method(req,'GET');const now=Date.now(),client=cur?header:'legacy';
    const r=await env.DB.batch([
      query(env.DB,`INSERT INTO mobile(id,last_seen,client) VALUES(1,?,?) ON CONFLICT(id) DO UPDATE
        SET last_seen=excluded.last_seen,client=excluded.client WHERE mobile.last_seen<=? OR mobile.client<>excluded.client`,now,client,now-15000),
      query(env.DB,`SELECT id,message AS text,decision,recording_id FROM cards WHERE answer IS NULL
        AND (?=0 OR decision=1 OR delivered IS NULL) ORDER BY created,rowid LIMIT 1`,modern?1:0),
      query(env.DB,'SELECT COUNT(*) AS n FROM subscriptions WHERE name=? AND expires>?',voiceEvent,now),
      query(env.DB,'SELECT EXISTS(SELECT 1 FROM voices WHERE created>?) AS recent',now-30000),
      query(env.DB,"SELECT EXISTS(SELECT 1 FROM deliveries WHERE phase='pending' AND due<=?) AS due",now)
    ]);
    const active=r[2].results[0].n>0,x=r[1].results[0];
    if(r[4].results[0].due)ctx.waitUntil(drain(env));
    return json({ready:cur&&active,reason:!cur?'client_update':!active?'dot_subscription':'ready',input:'text-v1',
                 receipts:modern,card:x?{...x,decision:!!x.decision}:null,has_recent_voice:!!r[3].results[0].recent});
  }
  method(req,'POST');
  if(path.startsWith('/voice/')||(!cur&&path.startsWith('/transcript/')))fail(426,'client_update');
  if(path==='/receipt'){if(!modern)fail(426,'client_update');return receiveReceipt(req,env);}
  const response=path==='/choice'?await receiveChoice(req,env):await receiveVoice(req,env,path.slice('/transcript/'.length));
  ctx.waitUntil(drain(env));return response;
}
async function admin(req,env,path){
  if(!isOwner(req,env))fail(403,'owner_required');
  if(path==='/admin/token'){
    method(req,'POST');if(!sameOrigin(req))fail(403,'same_origin_required');
    if(!env.MOBILE_TOKEN)fail(503,'credential_unavailable');
    return json({token:env.MOBILE_TOKEN});
  }
  method(req,'GET');const state=await status(env);
  const installer=await env.BUCKET.head(apkKey);
  return json({...state,installer:!!installer,version:'0.3',installer_version:'0.3',bridge_release:'0.4.0',input:'text-v1'});
}
async function installer(req,env){
  if(!['GET','HEAD'].includes(req.method))fail(405,'method_not_allowed');
  if(!authorizedDownload(req,env)){
    if(!req.headers.get('oai-authenticated-user-id'))return new Response(null,{status:302,headers:{
      Location:'/signin-with-chatgpt?return_to=%2Finstalar','Cache-Control':'no-store'}});
    fail(403,'owner_required');
  }
  const blob=req.method==='HEAD'?await env.BUCKET.head(apkKey):await env.BUCKET.get(apkKey);
  if(!blob)fail(404,'installer_pending');
  return new Response(req.method==='HEAD'?null:blob.body,{headers:{'Content-Type':'application/vnd.android.package-archive',
    'Content-Disposition':'attachment; filename="Dot-de-bolso-Android-0.3.apk"','Content-Length':String(blob.size),
    'Cache-Control':'no-store, private','X-Content-Type-Options':'nosniff','Referrer-Policy':'no-referrer'}});
}
async function receiveRelease(req,env){
  method(req,'PUT');
  if(req.headers.has('origin')||!await bearer(req,env.RELEASE_TOKEN))fail(401,'release_credential_required');
  const size=Number(req.headers.get('Content-Length')),sha=req.headers.get('X-Content-SHA256')??'';
  if(!Number.isSafeInteger(size)||size<8192||size>24*1024*1024||!req.body||!/^[a-f0-9]{64}$/.test(sha))fail(400,'invalid_package');
  const existing=await env.BUCKET.head(apkKey);
  if(existing){
    if(existing.customMetadata?.sha256!==sha)fail(409,'immutable_release');
    return json({stored:true,size:existing.size,sha256:sha});
  }
  // R2 verifies the digest while streaming. The object key is fixed and released once.
  await env.BUCKET.put(apkKey,req.body,{sha256:sha,onlyIf:{etagDoesNotMatch:'*'},customMetadata:{sha256:sha},
    httpMetadata:{contentType:'application/vnd.android.package-archive'}});
  const saved=await env.BUCKET.head(apkKey);
  if(saved?.size!==size||saved.customMetadata?.sha256!==sha)fail(409,'release_conflict');
  return json({stored:true,size:saved.size,sha256:sha},201);
}
export async function routeDot(req,env,ctx){
  const path=new URL(req.url).pathname;
  try{
    if(path==='/mcp')return await mcp(req,env);
    if(path==='/health'){method(req,'GET');return json({service:'dot-de-bolso',version:2,release:'0.4.0',clients:['android-2','android-3','android-4'],receipts:true});}
    if(path==='/instalar')return await installer(req,env);
    if(path==='/_release/android')return await receiveRelease(req,env);
    if(path.startsWith('/admin/'))return await admin(req,env,path);
    if(path==='/state'||path==='/choice'||path==='/receipt'||path.startsWith('/voice/')||path.startsWith('/transcript/'))return await phone(req,env,ctx,path);
    return json({error:'not_found'},404);
  }catch(e){
    if(e instanceof Problem)return json({error:e.message},e.status);
    return json({error:'bridge_unavailable'},503);
  }
}
