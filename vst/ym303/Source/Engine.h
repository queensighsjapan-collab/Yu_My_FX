// YM303 sound engine: a C++ port of AcidCore in synthesizer/yu-my-303.html (the browser version).
// Everything is computed in double precision with the same formulas as the JavaScript, so the two
// can be compared sample by sample (see tests/run_tests.sh).
#pragma once
#include <cmath>

namespace ym303
{
struct EngineParams
{
    double cutoff = .3, reso = .7, envmod = .62, decay = .3, accent = .62, tuning = .5, volume = .75;   // all 0..1
    int wave = 0;                                                                                         // 0 saw, 1 square
};

class Engine
{
public:
    explicit Engine (double sampleRate);

    EngineParams p;

    // slide = legato: glide to the new pitch without retriggering (only if the gate is still open)
    void noteOn (int note, bool accent, bool slide);
    void noteOff();
    void silence();                       // hard stop (gate, level and envelopes to zero)
    void process (float* out, int numSamples);

private:
    static double poly (double t, double dt);   // PolyBLEP residual

    double sr;
    double ph = 0, pitch = 36, target = 36, gate = 0, env = 0, acc = 0, amp = 0;
    bool accOn = false;
    double s0 = 0, s1 = 0, s2 = 0, s3 = 0, y4 = 0, fbLp = 0, cut = 500, res = 0, dcx = 0, dcy = 0;
    double hpA, slideA, accDec, cutA, resA, atk, rel;
};
}
