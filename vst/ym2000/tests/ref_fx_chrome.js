// Reference FX render in real Chromium (Blink Web Audio): runs the ORIGINAL buildChain()/native()/updMod()/updDelay().
// usage: node ref_fx_chrome.js ms2000-synth.html params.txt synth.f32 src.f32 out.f32 [sr]
const fs = require('fs');
const { chromium } = require('playwright');
const [,, html, pfile, synthf, srcf, outf, srArg] = process.argv;
const sr = srArg ? +srArg : 48000;
const src = fs.readFileSync(html, 'utf8');
const code = src.slice(src.indexOf('function buildChain(ctx)'), src.indexOf('/* modulator source'));
const rd = f => { const b = fs.readFileSync(f); return Array.from(new Float32Array(b.buffer, b.byteOffset, b.length / 4)); };
const params = {};
for (const line of fs.readFileSync(pfile, 'utf8').split('\n')) { const t = line.trim().split(/\s+/); if (t[0] === 'set') params[t[1]] = +t[2]; }
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.setContent('<html><body></body></html>');
  const out = await page.evaluate(async ({ code, params, synth, mod, sr }) => {
    const V = { vol: 100, ai1: 80, ai2: 0, vmode: 0, vfs: 0, vcl: 30, mft: 0, mfs: 64, mfd: 0, dt: 0, dtm: 64, ddp: 0, eqhf: 80, eqhg: 0, eqlf: 40, eqlg: 0, vsrc: 2 };
    const vLev = Array(16).fill(100), vPan = Array(16).fill(64);
    for (const k in params) {
      if (/^vlev\d+$/.test(k)) vLev[+k.slice(4)] = params[k];
      else if (/^vpan\d+$/.test(k)) vPan[+k.slice(4)] = params[k];
      else V[k] = params[k];
    }
    // remove the start-up ramp: apply targets immediately (C++ side starts from the target values as well)
    AudioParam.prototype.setTargetAtTime = function (v) { this.value = v; return this; };
    const frames = synth.length / 2;
    const ctx = new OfflineAudioContext(2, frames, sr);
    const F = new Function('ctx', 'V', 'vLev', 'vPan', 'setSource',
      'let N = null; ' + code + '; return { buildChain, updMod, updDelay, native, setN: n => { N = n; } };')(ctx, V, vLev, vPan, () => {});
    const N = F.buildChain(ctx); F.setN(N);
    for (const k in V) if (typeof V[k] === 'number') F.native(k, V[k]);
    F.updMod(); F.updDelay();
    N.voc.bands.forEach((b, i) => { b.lv.gain.value = vLev[i] / 127 * 1.4; b.pn.pan.value = (vPan[i] - 64) / 64; });
    const sb = ctx.createBuffer(2, frames, sr), l = sb.getChannelData(0), r = sb.getChannelData(1);
    for (let i = 0; i < frames; i++) { l[i] = synth[2 * i]; r[i] = synth[2 * i + 1]; }
    const ss = ctx.createBufferSource(); ss.buffer = sb; ss.connect(N.synth); ss.start();
    const mb = ctx.createBuffer(1, frames, sr), md = mb.getChannelData(0);
    for (let i = 0; i < frames; i++) md[i] = i < mod.length ? mod[i] : 0;
    const ms = ctx.createBufferSource(); ms.buffer = mb; ms.connect(N.src); ms.start();
    const res = await ctx.startRendering();
    const a = res.getChannelData(0), b = res.getChannelData(1);
    const o = new Array(frames * 2);
    for (let i = 0; i < frames; i++) { o[2 * i] = a[i]; o[2 * i + 1] = b[i]; }
    return o;
  }, { code, params, synth: rd(synthf), mod: rd(srcf), sr });
  fs.writeFileSync(outf, Buffer.from(new Float32Array(out).buffer));
  await browser.close();
})().catch(e => { console.error(e); process.exit(1); });
