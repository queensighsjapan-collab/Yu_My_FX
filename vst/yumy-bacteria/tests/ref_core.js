// Runs the worklet Core() extracted from yumy-bacteria.html with the same script as engine_test.cpp.
// usage: node ref_core.js yumy-bacteria.html script out.f32 [samplerate]
const fs = require('fs');
const [,, html, script, outf, srArg] = process.argv;
const sr = srArg ? +srArg : 48000;
const src = fs.readFileSync(html, 'utf8');
// deterministic RNG shared with Engine.cpp (mulberry32, seed 0x12345678)
let rs = 0x12345678 | 0;
Math.random = function () { rs = (rs + 0x6D2B79F5) | 0; let t = Math.imul(rs ^ (rs >>> 15), 1 | rs); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
const wk = src.match(/<script type="text\/plain" id="wk">([\s\S]*?)<\/script>/)[1];
const a = src.indexOf('const S={OFF:0'), b = src.indexOf('// ---------- messaging');
const tbl = src.slice(a, b).replace(/let FACTORY=0;/, '');
const OPT_KEYS = ['vbands','uni','osc2mode','l1s','l2s','l3s','l1k','l2k','l3k','mono','osc1mode','sync','ring','f1mode','f2mode','routing','arp','ahold','amode','ares','alen','apat','cmode','dsync','rsize'];
const env = new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', wk + ';' + tbl + '; return {Core,INIT,OINIT,PRESETS};')(sr, class {}, () => {});
const { Core, INIT, OINIT, PRESETS } = env;
const K = new Core(sr);
const idToName = {}; for (const n in INIT) idToName[n.replace(/[^A-Za-z0-9]+/g, '_')] = n;
for (const n in INIT) K.msg({ t: 'p', k: n, v: INIT[n] });
for (const k of OPT_KEYS) K.msg({ t: 'o', k, v: OINIT[k] });
for (let i = 0; i < 3; i++) K.msg({ t: 'o', k: 'ld', i, v: 0 });
let modOn = false, gi = 0;
const chunks = [];
function loadPreset(idx) {
  const p = PRESETS[idx];
  for (const n in INIT) K.msg({ t: 'p', k: n, v: (p.k && n in p.k) ? p.k[n] : INIT[n] });
  const o = Object.assign({}, OINIT, p.o || {});
  for (const k of OPT_KEYS) K.msg({ t: 'o', k, v: o[k] });
  for (let i = 0; i < 3; i++) K.msg({ t: 'o', k: 'ld', i, v: (o.ld || [0, 0, 0])[i] });
  const m = [];
  for (let i = 0; i < 6; i++) { const sl = p.mx && p.mx[i]; const d = [0, 0, 0], am = [0, 0, 0];
    for (let j = 0; j < 3; j++) { const e = (sl && sl[1 + j]) || [0, 0]; d[j] = e[0]; am[j] = e[1] / 100; }
    m.push({ src: sl ? sl[0] : 0, d, a: am }); }
  K.msg({ t: 'mx', m });
}
for (const line of fs.readFileSync(script, 'utf8').split('\n')) {
  const t = line.trim().split(/\s+/);
  if (!t[0] || t[0][0] === '#') continue;
  if (t[0] === 'p') K.msg({ t: 'p', k: idToName[t[1]], v: +t[2] });
  else if (t[0] === 'o') K.msg({ t: 'o', k: t[1], v: +t[2] });
  else if (t[0] === 'ld') K.msg({ t: 'o', k: 'ld', i: +t[1], v: +t[2] });
  else if (t[0] === 'mx') { const sl = +t[1]; const m = K.mx.map(x => ({ src: x.src, d: x.d.slice(), a: x.a.slice() }));
    m[sl] = { src: +t[2], d: [+t[3], +t[5], +t[7]], a: [+t[4] / 100, +t[6] / 100, +t[8] / 100] }; K.msg({ t: 'mx', m }); }
  else if (t[0] === 'on') K.msg({ t: 'on', n: +t[1], v: +t[2] / 127 });
  else if (t[0] === 'off') K.msg({ t: 'off', n: +t[1] });
  else if (t[0] === 'sus') K.msg({ t: 'sus', v: +t[1] });
  else if (t[0] === 'panic') K.msg({ t: 'panic' });
  else if (t[0] === 'wheels') K.msg({ t: 'w', pb: +t[1], mw: +t[2], at: +t[3] });
  else if (t[0] === 'maxv') K.maxV = +t[1];
  else if (t[0] === 'load') loadPreset(+t[1]);
  else if (t[0] === 'mod') modOn = +t[1] === 1;
  else if (t[0] === 'render') {
    const frames = +t[1];
    for (let d = 0; d < frames; d += 128) {
      const L = new Float32Array(128), R = new Float32Array(128);
      let mod = null;
      if (modOn) { mod = new Float32Array(128); for (let i = 0; i < 128; i++, gi++) mod[i] = 0.3 * Math.sin(2 * Math.PI * 150 * gi / sr) * (0.6 + 0.4 * Math.sin(2 * Math.PI * 3 * gi / sr)) + 0.1 * Math.sin(2 * Math.PI * 1900 * gi / sr); }
      K.render(L, R, 128, mod);
      const o = new Float32Array(256);
      for (let i = 0; i < 128; i++) { o[2 * i] = L[i]; o[2 * i + 1] = R[i]; }
      chunks.push(Buffer.from(o.buffer));
    }
  }
}
fs.writeFileSync(outf, Buffer.concat(chunks));
