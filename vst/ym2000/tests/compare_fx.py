import sys, numpy as np
a = np.fromfile(sys.argv[1], dtype=np.float32).reshape(-1,2).astype(np.float64)
b = np.fromfile(sys.argv[2], dtype=np.float32).reshape(-1,2).astype(np.float64)
sk = int(0.6*48000); n = 40000
# find the lag between the two renders (processing latency differs between implementations)
best = None
for lag in range(-600, 601):
    x = a[sk:sk+n, 0]; y = b[sk+lag:sk+lag+n, 0]
    d = np.sqrt(np.dot(x,x)*np.dot(y,y))
    c = np.dot(x,y)/d if d > 0 else 0
    if best is None or c > best[0]: best = (c, lag)
lag = best[1]
a2 = a[sk:sk+n]; b2 = b[sk+lag:sk+lag+n]
rms = np.sqrt(np.mean(b2**2)) or 1e-12
err = np.sqrt(np.mean((a2-b2)**2))
g = (a2*b2).sum()/max((a2*a2).sum(),1e-18)
err2 = np.sqrt(np.mean((g*a2-b2)**2))
gdb = 20*np.log10(abs(g)) if g != 0 else -999
print(f"{sys.argv[3]:10s} lag={lag:4d} corr={best[0]:.5f} ref_rms={rms:.4f} rel_err={err/rms:.3e} gain={gdb:+.2f}dB rel_err_after_gain={err2/rms:.3e}")
