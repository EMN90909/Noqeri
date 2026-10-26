// Built-in Noqeri design tokens, compiled to CSS for standards-compliant browsers.
const DEFAULTS = Object.freeze({
  primary:'#3158d5',accent:'#15803d',background:'#f7f8fc',surface:'#ffffff',
  text:'#1b2436',muted:'#526177',border:'#d9e0eb',radius:12
});
export function compileTheme(overrides=null) {
  if(overrides!==null&&(typeof overrides!=='object'||Array.isArray(overrides)))throw Error('Noqeri theme must be a token object');
  const tokens={...DEFAULTS};
  for(const [key,value] of Object.entries(overrides||{})){
    if(!Object.hasOwn(DEFAULTS,key))throw Error('Unknown Noqeri theme token '+key);
    if(key==='radius'){
      if(!Number.isInteger(value)||value<0||value>32)throw Error('Noqeri radius must be 0..32 pixels');
    }else if(typeof value!=='string'||!/^#[0-9a-fA-F]{3}(?:[0-9a-fA-F]{3})?$/.test(value)){
      throw Error('Noqeri theme '+key+' requires a 3- or 6-digit hex color');
    }
    tokens[key]=value;
  }
  const vars=Object.entries(tokens).map(([key,value])=>'  --nq-'+key+': '+value+(key==='radius'?'px':'')+';').join('\n');
  return `:root {\n${vars}\n}
*,*::before,*::after { box-sizing:border-box; }
body { margin:0; background:var(--nq-background); color:var(--nq-text); font:16px/1.6 system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif; }
button,input { font:inherit; }
a { color:var(--nq-primary); }
:focus-visible { outline:3px solid var(--nq-accent); outline-offset:3px; }
.nq-page { width:min(100% - 2rem,72rem); margin:2rem auto; }
.nq-stack > * + * { margin-top:1rem; }
.nq-title { margin:.2rem 0 1rem; font-size:clamp(1.8rem,3vw,3rem); line-height:1.15; letter-spacing:-.035em; }
.nq-lead { color:var(--nq-muted); font-size:1.12rem; }
.nq-page-header { margin:0 0 2rem; max-width:56rem; }
.nq-eyebrow { margin:0 0 .5rem; color:var(--nq-primary); font-size:.78rem; font-weight:750; text-transform:uppercase; letter-spacing:.13em; }
.nq-section-header { margin:1.5rem 0 1rem; }
.nq-section-title { margin:0 0 .45rem; font-size:clamp(1.15rem,2vw,1.55rem); line-height:1.3; }
.nq-muted,.nq-field-help,.nq-footer { color:var(--nq-muted); }
.nq-field-help { font-size:.85rem; }
.nq-field-error { color:#a32132; font-size:.9rem; font-weight:600; }
.nq-panel { padding:1.5rem; border:1px solid var(--nq-border); border-radius:var(--nq-radius); background:var(--nq-surface); }
.nq-empty { margin:2rem 0; padding:2rem; border:1px dashed var(--nq-border); border-radius:var(--nq-radius); text-align:center; }
.nq-footer { margin-top:3rem; padding:1.5rem 0; border-top:1px solid var(--nq-border); font-size:.85rem; }

.nq-card { padding:1.5rem; background:var(--nq-surface); border:1px solid var(--nq-border); border-radius:var(--nq-radius); box-shadow:0 5px 20px rgb(21 31 56 / .05); }
.nq-card-title { margin:0 0 .35rem; font-size:1.2rem; }
.nq-button { display:inline-flex; align-items:center; justify-content:center; gap:.5rem; border:1px solid var(--nq-primary); border-radius:var(--nq-radius); padding:.65rem 1.1rem; background:var(--nq-primary); color:white; cursor:pointer; font-weight:600; text-decoration:none; }
.nq-button:hover { filter:brightness(.9); }
.nq-button:disabled { opacity:.5; cursor:not-allowed; }
.nq-button-secondary { background:transparent; color:var(--nq-primary); }
.nq-badge { display:inline-block; padding:.16rem .65rem; border-radius:999px; background:var(--nq-background); color:var(--nq-primary); border:1px solid var(--nq-border); font-size:.85rem; font-weight:600; }
.nq-alert { padding:1rem; border-left:4px solid var(--nq-accent); border-radius:var(--nq-radius); background:var(--nq-surface); }
.nq-navbar { display:flex; flex-wrap:wrap; align-items:center; gap:1rem; padding:.8rem 0; border-bottom:1px solid var(--nq-border); }
.nq-navbar a { font-weight:600; text-decoration:none; }
.nq-field { display:grid; gap:.3rem; margin-bottom:1rem; }
.nq-field label { font-weight:600; }
.nq-field input,.nq-field textarea { width:100%; min-height:2.6rem; padding:.5rem .75rem; border:1px solid var(--nq-border); border-radius:var(--nq-radius); color:var(--nq-text); background:var(--nq-surface); }
.nq-stat-value { font-size:2rem; font-weight:750; line-height:1.2; }
.nq-stat-label { font-size:.9rem; color:var(--nq-muted); }
@media (prefers-reduced-motion:no-preference) { .nq-button { transition:filter .15s ease; } }
`;
}
