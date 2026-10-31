// Drives the REAL scheduler of yu-my-303.html in headless Chromium with a fake audio clock and records
// every Audio303.note() call. usage: node ref_seq.js yu-my-303.html out.txt
const { chromium } = require('playwright');
const fs = require('fs');
const [,, html, outf] = process.argv;
const cases = [
  { name: 'I-1 A',   mode: 3, group: 0, pat: 0, sec: 'A',  bpm: 130 },
  { name: 'I-2 AB',  mode: 3, group: 0, pat: 1, sec: 'AB', bpm: 130 },
  { name: 'I-2 B',   mode: 3, group: 0, pat: 1, sec: 'B',  bpm: 100 },
  { name: 'I-3 A',   mode: 3, group: 0, pat: 2, sec: 'A',  bpm: 150 },
  { name: 'I-4 A',   mode: 3, group: 0, pat: 3, sec: 'A',  bpm: 117 },
  { name: 'TRACK',   mode: 1, group: 0, pat: 0, sec: 'A',  bpm: 130 },
];
(async () => {
  const b = await chromium.launch();
  let out = '';
  for (const c of cases) {
    const p = await b.newPage();
    await p.route('**/fonts.g*/**', r => r.abort());
    await p.route('**/lamejs@1.2.1/**', r => r.abort());
    await p.goto('file://' + require('path').resolve(html));
    await p.waitForTimeout(300);
    const res = await p.evaluate(async (c) => {
      let fake = 0;
      Audio303.now = () => fake; Audio303.init = async () => true; Audio303.ready = true; Audio303.canRec = false;
      Audio303.silence = () => {};
      const log = [];
      Audio303.note = (time, note, gate, accent, slide) => log.push({ t: time, note, gate, accent: !!accent, slide: !!slide });
      S.mode = c.mode; S.group = c.group; S.pat = c.pat; S.secMode = c.sec;
      vals.tempo = (c.bpm - 60) / 140;
      const len = buildSeq().length, dur = 60 / bpmOf() / 4, T = (len * 3 + 2) * dur;
      await startRun();
      clearInterval(timer);
      while (fake < T) { fake += 0.01; schedule(); }
      S.running = false;
      return { log, len, T, bpm: bpmOf() };
    }, c);
    const t0 = 0.08;
    out += `case|${c.name}|${c.mode === 1 ? 1 : 0}|${c.group * 8 + c.pat}|${c.sec === 'A' ? 0 : c.sec === 'B' ? 1 : 2}|${res.bpm}|${res.T.toFixed(6)}|${res.log.length}\n`;
    for (const e of res.log) out += `${(e.t - t0).toFixed(9)} ${e.gate ? 1 : 0} ${e.note} ${e.accent ? 1 : 0} ${e.slide ? 1 : 0}\n`;
    await p.close();
  }
  fs.writeFileSync(outf, out);
  await b.close();
})();
