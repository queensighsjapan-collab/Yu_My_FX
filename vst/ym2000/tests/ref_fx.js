// Reference FX render: runs the ORIGINAL buildChain()/native()/updMod()/updDelay() from ms2000-synth.html in an OfflineAudioContext.
// usage: node ref_fx.js ms2000-synth.html params.txt synth.f32 src.f32 out.f32 [sr]
const fs = require('fs');
const { OfflineAudioContext } = require('node-web-audio-api');
const [,, html, pfile, synthf, srcf, outf, srArg] = process.argv;
const sr = srArg ? +srArg : 48000;
const src = fs.readFileSync(html, 'utf8');
const code = src.slice(src.indexOf('function buildChain(ctx)'), src.indexOf('/* modulator source'));
const rd = f => { const b = fs.readFileSync(f); return new Float32Array(b.buffer, b.byteOffset, b.length / 4); };
const synth = rd(synthf), mod = rd(srcf);
const frames = synth.length / 2;
const V = { vol: 100, ai1: 80, ai2: 0, vmode: 0, vfs: 0, vcl: 30, mft: 0, mfs: 64, mfd: 0, dt: 0, dtm: 64, ddp: 0, eqhf: 80, eqhg: 0, eqlf: 40, eqlg: 0, vsrc: 2 };
const vLev = Array(16).fill(100), vPan = Array(16).fill(64);
for (const line of fs.readFileSync(pfile, 'utf8').split('\n')) {
  const t = line.trim().split(/\s+/);
  if (t[0] !== 'set') continue;
  if (/^vlev\d+$/.test(t[1])) vLev[+t[1].slice(4)] = +t[2];
  else if (/^vpan\d+$/.test(t[1])) vPan[+t[1].slice(4)] = +t[2];
  else V[t[1]] = +t[2];
}
(async () => {
  const ctx = new OfflineAudioContext(2, frames, sr);
  const F = new Function('ctx', 'V', 'vLev', 'vPan', 'setSource',
    'let N = null; ' + code + '; return { buildChain, updMod, updDelay, native, setN: n => { N = n; } };')(ctx, V, vLev, vPan, () => {});
  const N = F.buildChain(ctx);
  F.setN(N);
  for (const k in V) if (typeof V[k] === 'number') F.native(k, V[k]);
  F.updMod(); F.updDelay();
  N.voc.bands.forEach((b, i) => { b.lv.gain.value = vLev[i] / 127 * 1.4; b.pn.pan.value = (vPan[i] - 64) / 64; });
  // synth input
  const sb = ctx.createBuffer(2, frames, sr);
  const l = sb.getChannelData(0), r = sb.getChannelData(1);
  for (let i = 0; i < frames; i++) { l[i] = synth[2 * i]; r[i] = synth[2 * i + 1]; }
  const ss = ctx.createBufferSource(); ss.buffer = sb; ss.connect(N.synth); ss.start();
  // modulator input
  const mb = ctx.createBuffer(1, frames, sr);
  const md = mb.getChannelData(0);
  for (let i = 0; i < frames; i++) md[i] = i < mod.length ? mod[i] : 0;
  const ms = ctx.createBufferSource(); ms.buffer = mb; ms.connect(N.src); ms.start();
  const out = await ctx.startRendering();
  const a = out.getChannelData(0), b = out.getChannelData(1);
  const o = new Float32Array(frames * 2);
  for (let i = 0; i < frames; i++) { o[2 * i] = a[i]; o[2 * i + 1] = b[i]; }
  fs.writeFileSync(outf, Buffer.from(o.buffer));
})().catch(e => { console.error(e); process.exit(1); });
