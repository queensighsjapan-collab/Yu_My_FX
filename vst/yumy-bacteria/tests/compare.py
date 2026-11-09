import sys, numpy as np
a = np.fromfile(sys.argv[1], dtype=np.float32)
b = np.fromfile(sys.argv[2], dtype=np.float32)
n = min(len(a), len(b))
if len(a) != len(b):
    print("LENGTH MISMATCH", len(a), len(b))
a, b = a[:n], b[:n]
rms = np.sqrt(np.mean(b.astype(np.float64) ** 2)) or 1e-12
err = np.sqrt(np.mean((a.astype(np.float64) - b) ** 2))
print(f"{sys.argv[3] if len(sys.argv) > 3 else ''} rms_ref={rms:.5f} rms_err={err:.3e} rel={err / rms:.3e} maxdiff={np.max(np.abs(a - b)):.3e}")
sys.exit(0 if err / rms < 1e-3 else 2)
