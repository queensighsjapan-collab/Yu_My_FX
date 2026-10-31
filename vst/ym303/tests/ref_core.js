// Runs AcidCore extracted from yu-my-303.html with the same script as engine_test.cpp (mono float32 out).
// usage: node ref_core.js yu-my-303.html script out.f32 [samplerate]
const fs = require('fs');
const [,, html, script, outf, srArg] = process.argv;
const sr = srArg ? +srArg : 48000;
const src = fs.readFileSync(html, 'utf8');
const m = src.match(/class AcidCore\{[\s\S]*?\n\}\n\n\/\*/);
if (!m) { console.error('AcidCore not found'); process.exit(1); }
const AcidCore = new Function(m[0].replace(/\n\n\/\*$/, '') + '; return AcidCore;')();
const K = new AcidCore(sr);
const chunks = [];
let cnt = 0, done = 0;
for (const line of fs.readFileSync(script, 'utf8').split('\n')) {
  const t = line.trim().split(/\s+/);
  const at = (cnt - 0.5) / sr;      // fires on the first sample >= cnt (scripts use multiples of 128)
  if (t[0] === 'set') K.p[t[1]] = +t[2];
  else if (t[0] === 'on') K.msg({ type: 'note', time: at, note: +t[1], gate: true, accent: +t[2] !== 0, slide: +t[3] !== 0 });
  else if (t[0] === 'off') K.msg({ type: 'note', time: at, note: 0, gate: false, accent: false, slide: false });
  else if (t[0] === 'silence') K.msg({ type: 'silence' });
  else if (t[0] === 'render') {
    const frames = +t[1];
    for (let d = 0; d < frames; d += 128) {
      const L = new Float32Array(128);
      K.render(L, null, done / sr);
      done += 128;
      chunks.push(Buffer.from(L.buffer));
    }
    cnt += frames;
  }
}
fs.writeFileSync(outf, Buffer.concat(chunks));
