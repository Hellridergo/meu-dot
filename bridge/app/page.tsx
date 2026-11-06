import { Masthead, Mascot } from "./ui";
export default function Landing(){
  return <div className="wrap"><Masthead/><main className="hero"><div><p className="kicker">Ponte de voz</p><h1>Dot de <em>bolso.</em></h1><p className="intro">Fale na Waveshare. O Android leva o seu pedido ao Dot e traz a resposta para o visor.</p><a className="primary" href="/painel">Abrir meu painel <span aria-hidden="true">↗</span></a><p className="hint">Use a conta ChatGPT proprietária da ponte.</p></div><Mascot/></main><footer><span>Waveshare &nbsp;→&nbsp; Android &nbsp;→&nbsp; Dot</span><span>Bluetooth LE · HTTPS</span></footer></div>;
}
