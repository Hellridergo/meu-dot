import { voiceEvent,replyEvent,packetId,query,utf8,encode64,decode64,digest,fail,iso,Problem } from './common.mjs';

const callbackHost='connectors.api.openai.com';
function checkedCallback(raw) {
  let url; try {url=new URL(raw);} catch {fail(400,'invalid_callback');}
  if(url.protocol!=='https:' || url.hostname!==callbackHost || url.username || url.password ||
     url.port || url.hash || url.pathname==='/' || url.href.length>2048)fail(400,'invalid_callback');
  return url.href;
}
function signingKey(secret){
  // Accept the standard and URL-safe representations of the same key bytes.
  if(typeof secret!=='string'||secret.length>96||!/^whsec_[A-Za-z0-9+/_-]+={0,2}$/.test(secret))fail(400,'invalid_signing_key');
  let key;try{key=decode64(secret.slice(6).replace(/-/g,'+').replace(/_/g,'/'));}catch{fail(400,'invalid_signing_key');}
  if(key.length<24||key.length>64)fail(400,'invalid_signing_key');
  return key;
}
async function cryptKey(env){
  if(!/^[0-9a-f]{64}$/i.test(env.EVENT_KEY_HEX??''))fail(503,'credentials_unavailable');
  const bytes=Uint8Array.from(env.EVENT_KEY_HEX.match(/../g),x=>parseInt(x,16));
  return crypto.subtle.importKey('raw',bytes,'AES-GCM',false,['encrypt','decrypt']);
}
async function seal(env,id,data){
  const iv=crypto.getRandomValues(new Uint8Array(12));
  const cipher=await crypto.subtle.encrypt({name:'AES-GCM',iv,additionalData:utf8(id)},await cryptKey(env),utf8(JSON.stringify(data)));
  return encode64(iv)+'.'+encode64(cipher);
}
async function unseal(env,id,data){
  const [i,c]=data.split('.');
  const plain=await crypto.subtle.decrypt({name:'AES-GCM',iv:decode64(i),additionalData:utf8(id)},await cryptKey(env),decode64(c));
  return JSON.parse(new TextDecoder().decode(plain));
}
function identityInputs(p){
  const args=p.arguments??{}, delivery=p.delivery??{};
  if(![voiceEvent,replyEvent].includes(p.name) || typeof args!=='object' || args===null || Array.isArray(args) ||
     Object.keys(args).some(k=>!(p.name===replyEvent&&k==='request_id')) ||
     ('request_id' in args && (typeof args.request_id!=='string'||!packetId.test(args.request_id))) ||
     delivery.mode!=='webhook')fail(400,'unsupported_subscription');
  const filter=args.request_id??'';
  const url=checkedCallback(delivery.url);
  return {url,filter};
}
async function sendSigned(env,sub,id,body){
  const url=checkedCallback(sub.url);
  const stamp=String(Math.floor(Date.now()/1000));
  const secret=await crypto.subtle.importKey('raw',signingKey(sub.secret),{name:'HMAC',hash:'SHA-256'},false,['sign']);
  const sign=await crypto.subtle.sign('HMAC',secret,utf8(id+'.'+stamp+'.'+body));
  // Pin the origin to the verified OpenAI event receiver. Never follow redirects.
  const options={method:'POST',redirect:'manual',signal:AbortSignal.timeout(10000),
    headers:{'content-type':'application/json','webhook-id':id,'webhook-timestamp':stamp,
      'webhook-signature':'v1,'+encode64(sign),'X-MCP-Subscription-Id':sub.id},body};
  return env.callbackFetch ? env.callbackFetch(url,options) : fetch(url,options);
}
function callbackError(reason,http=null){
  const e=new Problem(502,reason);e.callbackStatus=http;throw e;
}
function compareNonce(a,b){
  if(typeof a!=='string'||a.length!==b.length)return false;
  let difference=0;for(let i=0;i<b.length;i++)difference|=a.charCodeAt(i)^b.charCodeAt(i);
  return difference===0;
}
async function verifyCallback(env,credential){
  const challenge=crypto.randomUUID()+crypto.randomUUID();
  let reply;
  try{
    reply=await sendSigned(env,credential,'msg_verification_'+crypto.randomUUID(),JSON.stringify({type:'verification',challenge}));
  }catch(e){
    if(e instanceof Problem)throw e;
    callbackError(['AbortError','TimeoutError'].includes(e?.name)?'timeout':'callback_unreachable');
  }
  if(!reply.ok){
    await reply.body?.cancel().catch(()=>{});
    callbackError(reply.status>=300&&reply.status<400?'callback_redirect':'callback_http',reply.status);
  }
  let body;try {body=await readChallenge(reply);}catch(e){
    callbackError(['AbortError','TimeoutError'].includes(e?.name)?'timeout':'challenge_failed',reply.status);
  }
  if(!compareNonce(body?.challenge,challenge))callbackError('challenge_failed',reply.status);
}
async function readChallenge(reply){
  // Bound both streamed and declared sizes before decoding a receiver reply.
  const reader=reply.body?.getReader();if(!reader)throw Error();
  if(Number(reply.headers.get('content-length'))>16384){await reader.cancel();throw Error();}
  const decoder=new TextDecoder();let size=0,text='';
  for(;;){const {done,value}=await reader.read();if(done)break;
    if((size+=value.length)>16384){await reader.cancel();throw Error();}
    text+=decoder.decode(value,{stream:true});}
  return JSON.parse(text+decoder.decode());
}
async function recordCheck(env,name,stage,http=null){
  if(![voiceEvent,replyEvent].includes(name))return;
  try{
    await query(env.DB,`INSERT INTO subscription_checks(name,at,stage,http) VALUES(?,?,?,?)
      ON CONFLICT(name) DO UPDATE SET at=excluded.at,stage=excluded.stage,http=excluded.http`,name,Date.now(),stage,http).run();
  }catch{console.warn('dot_subscription_diagnostic_unavailable');}
}
export async function subscribe(env,p,remove=false){
  if(remove)return subscription(env,p,true);
  try {
    const result=await subscription(env,p,false);await recordCheck(env,p.name,'active');return result;
  }catch(e){
    const stage=e instanceof Problem?e.message:'storage_unavailable';
    await recordCheck(env,p.name,stage,e instanceof Problem?(e.callbackStatus??null):null);
    if(!(e instanceof Problem))throw new Problem(503,stage);
    throw e;
  }
}
async function subscription(env,p,remove) {
  const {url,filter}=identityInputs(p);
  const id='sub_'+await digest(JSON.stringify([p.name,filter,url]));
  if(remove){
    await env.DB.batch([query(env.DB,'DELETE FROM subscriptions WHERE id=?',id),
      query(env.DB,"UPDATE deliveries SET phase='stopped',lease=NULL WHERE sub=? AND phase='pending'",id)]);
    return {};
  }
  if(p.cursor!=null)fail(400,'replay_unsupported');
  signingKey(p.delivery.secret);
  const ttl=p.ttlMs??7*86400000;
  if(!Number.isSafeInteger(ttl)||ttl<=0)fail(400,'invalid_ttl');
  const grant=Math.max(60000,Math.min(30*86400000,ttl));
  const credential={id,url,secret:p.delivery.secret};
  await verifyCallback(env,credential);
  const now=Date.now(),expires=now+grant;
  await query(env.DB,`INSERT INTO subscriptions(id,name,filter_id,sealed,created,expires) VALUES(?,?,?,?,?,?)
    ON CONFLICT(id) DO UPDATE SET sealed=excluded.sealed,
      created=CASE WHEN subscriptions.expires<=excluded.created THEN excluded.created ELSE subscriptions.created END,
      expires=excluded.expires`,id,p.name,filter,await seal(env,id,credential),now,expires).run();
  return {id,refreshBefore:iso(expires),cursor:null,truncated:false};
}
export async function status(env,at=Date.now()){
  const num=await query(env.DB,`SELECT
    (SELECT COUNT(*) FROM subscriptions WHERE name=? AND expires>?) AS voice_subscriptions,
    (SELECT COUNT(*) FROM subscriptions WHERE name=? AND expires>?) AS reply_subscriptions,
    (SELECT last_seen FROM mobile WHERE id=1) AS last_seen,
    (SELECT client FROM mobile WHERE id=1) AS client,
    (SELECT COUNT(*) FROM deliveries WHERE phase='pending') AS pending,
    (SELECT COUNT(*) FROM cards WHERE decision=0 AND answer IS NULL AND delivered IS NULL) AS queued_notices,
    (SELECT COUNT(*) FROM cards WHERE decision=1 AND answer IS NULL) AS pending_decisions,
    (SELECT COUNT(*) FROM deliveries WHERE phase='failed') AS failed`,
    voiceEvent,at,replyEvent,at).first();
  const checks=await query(env.DB,'SELECT name,at,stage,http FROM subscription_checks ORDER BY name').all();
  return {...num,phone_online:!!num.last_seen&&at-num.last_seen<30000,subscription_checks:checks.results};
}
export function enqueueVoice(db,id,now) {
  return query(db,`INSERT OR IGNORE INTO deliveries(sub,event,name,data,created)
    SELECT s.id,'voice_'||v.id,?,json_object('recording_id',v.id,'text',v.message,'audio_seconds',v.seconds),v.created
    FROM voices v JOIN subscriptions s ON s.name=? AND s.created<=v.created AND s.expires>?
    WHERE v.id=?`,voiceEvent,voiceEvent,now,id);
}
export function enqueueChoice(db,id,now){
  return query(db,`INSERT OR IGNORE INTO deliveries(sub,event,name,data,created)
    SELECT s.id,'reply_'||c.id,?,json_object('request_id',c.id,'kind',CASE WHEN c.decision=1 THEN 'decision' ELSE 'notice' END,
      'option_id',c.answer,'option_label',CASE c.answer WHEN 'yes' THEN 'Confirmar' WHEN 'no' THEN 'Recusar' ELSE 'Concluir' END),c.answered
    FROM cards c JOIN subscriptions s ON s.name=? AND s.created<=c.answered AND s.expires>?
    AND (s.filter_id='' OR s.filter_id=c.id) WHERE c.id=? AND c.answer IS NOT NULL`,replyEvent,replyEvent,now,id);
}
export async function drain(env){
  const db=env.DB;
  // A lease prevents simultaneous polls from posting the same event. A crash can
  // retry it with the same webhook ID, so the receiver can deduplicate.
  for(let i=0;i<3;i++){
    const now=Date.now(),lease=crypto.randomUUID();
    const row=await query(db,`UPDATE deliveries SET tries=tries+1,due=?,lease=?,attempted=?
      WHERE (sub,event)=(SELECT sub,event FROM deliveries WHERE phase='pending' AND due<=?
        ORDER BY created,event LIMIT 1) AND phase='pending' AND due<=?
      RETURNING sub,event,name,data,created,tries`,now+30000,lease,now,now,now).first();
    if(!row)break;
    const sub=await query(db,'SELECT sealed,expires FROM subscriptions WHERE id=?',row.sub).first();
    let code=0;
    if(sub&&sub.expires>now){
      try{
        const cred=await unseal(env,row.sub,sub.sealed);
        const body=JSON.stringify({eventId:row.event,name:row.name,timestamp:iso(row.created),data:JSON.parse(row.data),cursor:null});
        const reply=await sendSigned(env,cred,row.event,body);
        code=reply.status;await reply.body?.cancel();
      }catch{code=0;}
    }else code=410;
    const ok=code>=200&&code<300;
    const terminal=code>=400&&code<500&&code!==408&&code!==429;
    const phase=ok?'sent':code===410?'stopped':terminal||row.tries>=10?'failed':'pending';
    const tasks=[query(db,'UPDATE deliveries SET phase=?,due=?,lease=NULL,http=?,sent=? WHERE sub=? AND event=? AND lease=?',
      phase,now+Math.min(300000,1000*2**row.tries),code,ok?Date.now():null,row.sub,row.event,lease)];
    if(code===410&&sub)tasks.push(query(db,'UPDATE subscriptions SET expires=0 WHERE id=? AND sealed=?',row.sub,sub.sealed));
    await db.batch(tasks);
  }
}
