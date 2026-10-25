// Runs Core() extracted from ms2000-synth.html with the same script as engine_test.cpp.
// usage: node ref_core.js ms2000-synth.html script out.f32 [samplerate]
const fs = require('fs');
const [,, html, script, outf, srArg] = process.argv;
const sr = srArg ? +srArg : 48000;
const src = fs.readFileSync(html, 'utf8');
const a = src.indexOf('function Core(sr) {');
const b = src.indexOf('/* ================= audio graph');
const Core = new Function(src.slice(a, b) + '; return Core;')();
const K = Core(sr);
const chunks = [];
for (const line of fs.readFileSync(script, 'utf8').split('\n')) {
  const t = line.trim().split(/\s+/);
  if (t[0] === 'set') K.set(t[1], +t[2]);
  else if (t[0] === 'on') K.on(+t[1], +t[2] / 127);
  else if (t[0] === 'off') K.off(+t[1]);
  else if (t[0] === 'panic') K.panic();
  else if (t[0] === 'render') {
    const frames = +t[1];
    for (let d = 0; d < frames; d += 128) {
      const L = new Float32Array(128), R = new Float32Array(128);
      K.process(L, R, 128, null);
      const o = new Float32Array(256);
      for (let i = 0; i < 128; i++) { o[2 * i] = L[i]; o[2 * i + 1] = R[i]; }
      chunks.push(Buffer.from(o.buffer));
    }
  }
}
fs.writeFileSync(outf, Buffer.concat(chunks));
