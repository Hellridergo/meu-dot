const enc = new TextEncoder();
export const voiceEvent = 'device.transcript';
export const replyEvent = 'device.reply';
export const packetId = /^[A-Za-z0-9_.-]{1,64}$/;
export const voiceId = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
export const query = (db,sql,...args) => db.prepare(sql).bind(...args);
export const utf8 = s => enc.encode(s);
export const iso = n => new Date(n).toISOString();
export const json = (value,status=200) => Response.json(value,{status,headers:{
  'Cache-Control':'no-store, private', 'X-Content-Type-Options':'nosniff','Referrer-Policy':'no-referrer'
}});
export class Problem extends Error {
  constructor(status,code) { super(code); this.status=status; }
}
export function fail(status,code) { throw new Problem(status,code); }
export async function readObject(req,limit=16000) {
  if(req.headers.get('content-type')?.split(';')[0].trim()!=='application/json') fail(415,'json_required');
  if(Number(req.headers.get('content-length'))>limit) fail(413,'too_large');
  const reader=req.body?.getReader(); if(!reader) fail(400,'missing_body');
  const chunks=[];let size=0;
  for(;;){
    const {value,done}=await reader.read();if(done) break;
    size+=value.byteLength;
    if(size>limit){await reader.cancel();fail(413,'too_large');}
    chunks.push(value);
  }
  const all=new Uint8Array(size);let at=0;
  for(const c of chunks){all.set(c,at);at+=c.length;}
  let data;try { data=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(all)); } catch { fail(400,'invalid_json'); }
  if(!data||typeof data!=='object'||Array.isArray(data)) fail(400,'invalid_object');
  return data;
}
export async function bearer(req,secret) {
  if(typeof secret!=='string'||secret.length<32)return false;
  const actual=req.headers.get('authorization')??'';
  if(actual.length>200) return false;
  const [a,b]=await Promise.all([actual,'Bearer '+secret].map(s=>crypto.subtle.digest('SHA-256',utf8(s))));
  const left=new Uint8Array(a),right=new Uint8Array(b);let d=0;
  for(let i=0;i<left.length;i++)d|=left[i]^right[i];
  return d===0;
}
export function isOwner(req,env) {
  const id=req.headers.get('oai-authenticated-user-id');
  const email=req.headers.get('oai-authenticated-user-email')?.trim().toLowerCase();
  return Boolean(id && email && env.DOT_OWNER_EMAIL && email===env.DOT_OWNER_EMAIL.trim().toLowerCase());
}
export function sameOrigin(req) { return req.headers.get('origin')===new URL(req.url).origin; }
export function validText(s,maxChars=600,maxBytes=1098) {
  return typeof s==='string' && s.trim().length>0 && s.length<=maxChars &&
    utf8(s).length<=maxBytes && !/[\u0000-\u0009\u000b-\u001f\u007f]/u.test(s);
}
export function decode64(s){return Uint8Array.from(atob(s), c=>c.charCodeAt(0));}
export function encode64(bytes){return btoa(String.fromCharCode(...new Uint8Array(bytes)));}
export async function digest(s){
  return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',utf8(s))),n=>n.toString(16).padStart(2,'0')).join('');
}
