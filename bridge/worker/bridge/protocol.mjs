import {json,readObject,isOwner,query,packetId,voiceId,validText,voiceEvent,replyEvent,Problem} from './common.mjs';
import {status,subscribe} from './events.mjs';

const version='2026-07-28';
const info={name:'dot-de-bolso',version:'2.2.0'};
const instructions='Personal voice bridge for the owner. Subscribe to device.transcript in the owner’s Dot chat. Use the text already in the event as the voice request. Read get_voice_input only to recover a missing transcription. Deduplicate recording_id. Include recording_id in replies to voice requests. Speech recognition can contain errors. When your answer is ready send its short spoken version first with send_message, then complete the chat response in Portuguese. For a decision subscribe to device.reply and use ask_confirmation. Only a yes on that specific decision confirms it; dismiss only marks a notice as read.';
const object=(properties,required=[])=>({type:'object',properties,required,additionalProperties:false});
const string=n=>({type:'string',minLength:1,maxLength:n});
const cardArgs=object({request_id:string(64),text:string(600),recording_id:string(64)},['request_id','text']);
const tool=(name,description,inputSchema,readOnlyHint)=>({name,description,inputSchema,annotations:{readOnlyHint,destructiveHint:false,idempotentHint:true,openWorldHint:false}});
const tools=[
  tool('send_message','Show a brief reply in Portuguese on the paired Waveshare. Reuse a stable request_id on retries.',cardArgs,false),
  tool('ask_confirmation','Ask the owner to confirm or decline on the Waveshare. Subscribe to device.reply first.',cardArgs,false),
  tool('get_request','Look up a previously queued card and its exact decision.',object({request_id:string(64)},['request_id']),true),
  tool('get_voice_input','Read a mobile transcription by its recording_id.',object({recording_id:string(64)},['recording_id']),true),
  tool('get_device_status','Check phone connectivity, active event subscriptions and delivery health.',object({}),true)
];
const events=[
  {name:voiceEvent,description:'A Portuguese push-to-talk transcription from the owner’s WaveShare and Android. Deduplicate recording_id.',
   delivery:['webhook'],inputSchema:object({}),payloadSchema:object({recording_id:string(64),text:string(4000),audio_seconds:{type:'number'}},['recording_id','text','audio_seconds'])},
  {name:replyEvent,description:'The owner answered a decision or dismissed a notice on the Waveshare.',
   delivery:['webhook'],inputSchema:object({request_id:string(64)}),payloadSchema:object({request_id:string(64),kind:{type:'string',enum:['notice','decision']},option_id:string(16),option_label:string(32)},['request_id','kind','option_id','option_label'])}
];
function rpcError(id,code,message,data){return json({jsonrpc:'2.0',id,error:{code,message,...(data?{data}:{})}});}
function toolResult(value,error=false){
  return {content:[{type:'text',text:typeof value==='string'?value:JSON.stringify(value)}],...(error?{}:{structuredContent:value}),isError:error};
}
function view(r){return {request_id:r.id,recording_id:r.recording_id??null,created_at:r.created,delivered_at:r.delivered??null,text:r.message,kind:r.decision?'decision':'notice',status:r.answer?'answered':r.delivered?'delivered':'pending',delivery_stage:r.delivery_stage??null,
                         selected_option:r.answer??null};}
async function callTool(env,name,a){
  if(!a||typeof a!=='object'||Array.isArray(a))return toolResult('Invalid arguments.',true);
  const def=tools.find(t=>t.name===name);
  if(!def||Object.keys(a).some(k=>!(k in def.inputSchema.properties)) ||
    def.inputSchema.required.some(k=>!(k in a)))return toolResult('Check the tool name and arguments.',true);
  if(name==='get_device_status')return toolResult(await status(env));
  if(name==='get_voice_input'){
    if(typeof a.recording_id!=='string'||!voiceId.test(a.recording_id))return toolResult('Invalid recording ID.',true);
    const r=await query(env.DB,'SELECT id AS recording_id,message AS text,seconds AS audio_seconds,created AS received_at FROM voices WHERE id=?',a.recording_id).first();
    if(!r)return toolResult('Recording not found.',true);
    const dispatches=await query(env.DB,'SELECT created,attempted,sent,http,phase,tries FROM deliveries WHERE event=? ORDER BY created','voice_'+a.recording_id).all();
    return toolResult({...r,dispatches:dispatches.results});
  }
  if(typeof a.request_id!=='string'||!packetId.test(a.request_id))return toolResult('Use a stable ASCII request ID of 1 to 64 characters.',true);
  if(name==='get_request'){
    const r=await query(env.DB,'SELECT * FROM cards WHERE id=?',a.request_id).first();
    return r?toolResult(view(r)):toolResult('Card not found.',true);
  }
  if(!validText(a.text))return toolResult('Use a shorter printable message in Portuguese.',true);
  const decision=name==='ask_confirmation'?1:0;
  if(a.recording_id!=null&&(typeof a.recording_id!=='string'||!voiceId.test(a.recording_id)||!await query(env.DB,'SELECT id FROM voices WHERE id=?',a.recording_id).first()))return toolResult('Unknown recording ID.',true);
  // Idempotent for reconnections and webhook retries.
  await query(env.DB,'INSERT INTO cards(id,message,decision,created,recording_id) VALUES(?,?,?,?,?) ON CONFLICT(id) DO NOTHING',
              a.request_id,a.text,decision,Date.now(),a.recording_id??null).run();
  const r=await query(env.DB,'SELECT * FROM cards WHERE id=?',a.request_id).first();
  if(!r || r.message!==a.text || r.decision!==decision || (r.recording_id??null)!==(a.recording_id??null))return toolResult('That ID belongs to a different card.',true);
  return toolResult(view(r));
}
export async function mcp(req,env){
  if(req.method!=='POST')return json({error:'post_required'},405);
  let msg;try{msg=await readObject(req,65536);}catch(e){return rpcError(null,-32700,'Invalid JSON-RPC request.');}
  const id=msg.id??null, method=msg.method,p=msg.params??{};
  if(msg.jsonrpc!=='2.0'||typeof method!=='string'||typeof p!=='object'||p===null||Array.isArray(p))
    return rpcError(id,-32600,'Invalid request.');
  if(!Object.hasOwn(msg,'id'))return new Response(null,{status:202,headers:{'Cache-Control':'no-store'}});
  const isModern=method==='server/discover'||p._meta?.['io.modelcontextprotocol/protocolVersion']===version||req.headers.get('MCP-Protocol-Version')===version;
  const cap={tools:{},events:{}};
  let result;
  if(method==='server/discover')result={supportedVersions:[version],capabilities:cap,instructions,cacheScope:'private',ttlMs:0};
  else if(method==='initialize')result={protocolVersion:['2025-11-25','2025-06-18','2025-03-26'].includes(p.protocolVersion)?p.protocolVersion:'2025-11-25',capabilities:cap,serverInfo:info,instructions};
  else if(method==='tools/list')result={tools};
  else if(method==='events/list')result={events};
  else if(method==='ping')result={};
  else if(method==='tools/call'||method==='events/subscribe'||method==='events/unsubscribe'){
    if(!isOwner(req,env)){
      // No request content or identity: distinguish a refused account from a
      // subscription never forwarded by the plugin. Never let anonymous calls
      // change any owner's stored diagnostic or register a listener.
      if(method==='events/subscribe')console.warn('dot_subscription_identity_refused');
      return rpcError(id,-32001,'Owner authorization is required.',{reason:'owner_required'});
    }
    try{
      result=method==='tools/call'?await callTool(env,p.name,p.arguments??{}):await subscribe(env,p,method==='events/unsubscribe');
    }catch(e){
      if(e instanceof Problem){
        if(e.status===502){
          const reason=e.message==='timeout'?'timeout':e.message==='callback_unreachable'?'connection_refused':
            e.callbackStatus>=500?'http_5xx':e.callbackStatus>=400?'http_4xx':'challenge_failed';
          return rpcError(id,-32015,'CallbackEndpointError',{reason,stage:e.message,...(e.callbackStatus?{http_status:e.callbackStatus}:{})});
        }
        return rpcError(id,e.status>=500?-32603:-32602,e.message,{reason:e.message});
      }
      return rpcError(id,-32603,'Bridge unavailable.');
    }
  }else return rpcError(id,-32601,'Unknown method.');
  if(isModern)result={...result,resultType:'complete',_meta:{'io.modelcontextprotocol/serverInfo':info}};
  return json({jsonrpc:'2.0',id,result});
}
