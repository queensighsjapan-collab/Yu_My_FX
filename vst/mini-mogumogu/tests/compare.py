import sys, numpy as np
a = np.fromfile(sys.argv[1], dtype=np.float32).reshape(-1, 2)   # C++ engine
b = np.fromfile(sys.argv[2], dtype=np.float32).reshape(-1, 2)   # browser
name = sys.argv[3]
tol_corr = float(sys.argv[4]) if len(sys.argv) > 4 else 0.98
n = min(len(a), len(b)); a = a[:n]; b = b[:n]
rms = lambda x: float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))
ra, rb = rms(a), rms(b)
if rb < 1e-6 and ra < 1e-6:
    print(f'{name:14s} both silent'); sys.exit(0)
ok = True
msg = []
for ch, nm in ((0, 'L'), (1, 'R')):
    x, y = a[:, ch].astype(np.float64), b[:, ch].astype(np.float64)
    if rms(y) < 1e-6: continue
    # free-running oscillators have an arbitrary start phase in the browser: search a lag of +-600 samples
    best, bl = -2.0, 0
    for lag in range(-600, 601, 1):
        xs, ys = (x[lag:], y[:len(y) - lag]) if lag >= 0 else (x[:lag], y[-lag:])
        cc = float(np.dot(xs, ys) / (np.linalg.norm(xs) * np.linalg.norm(ys) + 1e-30))
        if cc > best: best, bl = cc, lag
    c = best
    err = float(bl)
    msg.append(f'{nm}: corr={c:.4f} lag={err:.0f}')
    if c < tol_corr: ok = False
dbdiff = 20 * np.log10((ra + 1e-12) / (rb + 1e-12))
if abs(dbdiff) > 1.0: ok = False
print(f'{name:14s} rms cpp={ra:.4f} web={rb:.4f} ({dbdiff:+.2f} dB)  ' + '  '.join(msg) + ('   OK' if ok else '   <-- DIFF'))
sys.exit(0 if ok else 1)
