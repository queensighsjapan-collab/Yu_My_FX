#!/usr/bin/env python3
"""Render a script with the browser version's real Web Audio graph (OfflineAudioContext).
   usage: ref_render.py mini-mogumogu.html script.txt out.f32   (interleaved float32 stereo)"""
import sys, struct, array
from playwright.sync_api import sync_playwright

html, script, out = sys.argv[1:4]
sr, dur, sets, events = 48000, 1.0, [], []
for line in open(script):
    t = line.split()
    if not t or t[0].startswith('#'): continue
    if t[0] == 'sr': sr = int(t[1])
    elif t[0] == 'dur': dur = float(t[1])
    elif t[0] == 'set': sets.append([t[1], float(t[2])])
    elif t[0] == 'on': events.append({'type': 'on', 't': float(t[1]), 'note': int(t[2]), 'vel': float(t[3]), 'retrig': bool(int(t[4]))})
    elif t[0] == 'off': events.append({'type': 'off', 't': float(t[1])})
    elif t[0] == 'bend': events.append({'type': 'bend', 't': float(t[1]), 'x': float(t[2])})
    elif t[0] == 'wheel': events.append({'type': 'wheel', 't': float(t[1]), 'x': float(t[2])})

JS = """async (a) => {
  ctx = new OfflineAudioContext(2, Math.ceil(a.sr * a.dur), a.sr);
  const src = document.querySelector('#wl').textContent;
  await ctx.audioWorklet.addModule('data:text/javascript;charset=utf-8,' + encodeURIComponent(src));
  buildGraph();
  loadPreset(FACTORY[0]);                       // "Init" = every parameter at its default
  for (const [k, v] of a.sets) setParam(k, v);
  Object.keys(meta).forEach(k => apply(k, S[k]));
  for (const e of a.events) {
    if (e.type === 'on') voiceOn(e.note, e.t, e.vel, e.retrig);
    else if (e.type === 'off') voiceOff(e.t);
    else if (e.type === 'bend') { G.bend.offset.setValueAtTime(e.x * [2,5,12][S.bendRange] * 100, e.t); }
    else if (e.type === 'wheel') { wheelMod = e.x; updMod(); }
  }
  const buf = await ctx.startRendering();
  const l = buf.getChannelData(0), r = buf.getChannelData(1), o = new Float32Array(l.length * 2);
  for (let i = 0; i < l.length; i++) { o[2*i] = l[i]; o[2*i+1] = r[i]; }
  let s = ''; const u8 = new Uint8Array(o.buffer);
  for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
  return btoa(s);
}"""
import base64
with sync_playwright() as p:
    b = p.chromium.launch()
    pg = b.new_page()
    errs = []
    pg.on('pageerror', lambda e: errs.append(str(e)))
    pg.goto('file://' + html)
    pg.wait_for_timeout(300)
    data = pg.evaluate(JS, {'sr': sr, 'dur': dur, 'sets': sets, 'events': events})
    open(out, 'wb').write(base64.b64decode(data))
    if errs: print('page errors:', errs)
    b.close()
