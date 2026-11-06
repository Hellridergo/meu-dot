export function Masthead(){
  return <header><a className="brand" href="/" aria-label="Início do Dot de bolso"><img src="/marca.svg" alt="" width="30" height="30"/>dot de bolso</a><span className="spec">Waveshare 1.75C</span></header>;
}
export function Mascot(){
  return <div className="dial" aria-hidden="true"><div className="ticks"/><div className="character"><img src="/dot/olhar.webp" alt="" width="319" height="286"/><img className="blink" src="/dot/piscar.webp" alt="" width="319" height="286"/></div></div>;
}
