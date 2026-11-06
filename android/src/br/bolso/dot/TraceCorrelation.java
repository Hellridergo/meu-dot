package br.bolso.dot;
import java.util.Map;
import java.util.regex.Pattern;
final class TraceCorrelation {
  private static final Pattern UUID=Pattern.compile("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}",Pattern.CASE_INSENSITIVE);
  static Timings reply(Map<String,Timings> inputs,String explicit,String request){
    String encoded=request.startsWith("resposta-")?request.substring(9):"";
    if(!UUID.matcher(encoded).matches())encoded="";
    if(explicit!=null&&!explicit.isEmpty()){
      if(!encoded.isEmpty()&&!encoded.equals(explicit))return null;
      Timings input=inputs.get(explicit);
      return input==null?null:input.forReply(request,"recording_id");
    }
    Timings input=inputs.get(encoded);
    return input==null?null:input.forReply(request,"request_id");
  }
}
