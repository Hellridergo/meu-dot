import { requireChatGPTUser } from "../chatgpt-auth";
import { Masthead } from "../ui";
import Panel from "./panel";
import { env } from "cloudflare:workers";
export const dynamic="force-dynamic";
export default async function OwnerPage(){
  const user=await requireChatGPTUser("/painel");
  const bound=(env as unknown as {DOT_OWNER_EMAIL?:string}).DOT_OWNER_EMAIL?.trim().toLowerCase();
  const allowed=Boolean(bound && user.email.trim().toLowerCase()===bound);
  return <div className="wrap"><Masthead/>{allowed?<Panel/>:<main className="denied"><p className="kicker">Acesso restrito</p><h1>Painel privado.</h1><p>Entre com a conta proprietária para configurar o celular.</p><a href="/signout-with-chatgpt?return_to=%2Fpainel" target="_top">Trocar de conta</a></main>}</div>;
}
