// YM2000 sound engine: C++ port of Core() from synthesizer/ms2000-synth.html
// No JUCE dependency, so it can be unit-tested offline.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace ym
{
// Engine parameters (same names as the HTML version). Order must match kEngineParamNames.
enum EP
{
    o1w, o1c1, o1c2, dwgs, o2w, o2m, o2s, o2t, mx1, mx2, mxn, ft, cut, res, f1, fv, fk, al, ap, dist, ak,
    e1a, e1d, e1s, e1r, e2a, e2d, e2s, e2r, l1w, l1f, l1k, l1t, l2w, l2f, l2k, ptn, pbr, ptr, pvi, ppo,
    arptm, vs1, vs2, vs3, vs4, vd1, vd2, vd3, vd4, vi1, vi2, vi3, vi4, msq, msd, seqv, bend, mod,
    NUM_EP
};
extern const char* const kEngineParamNames[NUM_EP];
int engineParamIndex (const char* name); // -1 when not an engine parameter

class Engine
{
public:
    explicit Engine (double sampleRate);

    void noteOn (int note, double vel01);
    void noteOff (int note);
    void panic();
    void set (int idx, double v) { if (idx >= 0 && idx < NUM_EP && v == v) p[idx] = v; }
    double get (int idx) const { return p[idx]; }

    // inp may be nullptr (OSC1 AUDIO IN source, mono)
    void process (float* L, float* R, int len, const float* inp);

private:
    static constexpr int NV = 4;
    static constexpr int TS = 2048;

    struct Voice
    {
        bool on = false, gate = false;
        int note = 60, age = 0, cnt = 0, s1 = 0, s2 = 0;
        double vel = 1, pf = 60;
        double ph1 = 0, ph1b = 0, ph2 = 0, sq = 1, l1 = 0, l2 = 0, sh1 = 0, sh2 = 0;
        double e1 = 0, e2 = 0;
        double fs[12] = {}, cf[24] = {};
        double f1 = 440, dt1 = 0.01, dt2 = 0.01, dtb = 0.01, pl = 0.7, pr = 0.7, ga = 0, ml2 = 0, pw = 0.5, c1 = 0, nl = 0;
    };

    // smoothed (click-prone) parameters
    struct Sm
    {
        double c1b = 0, c2b = 0, mx1 = 0, mx2 = 0, mxn = 0, kres = 0, cutB = 0, f1k = 0, fvk = 0, fkk = 0, alv = 0, akk = 0,
               bend = 0, modw = 0, tune100 = 0, vibInt = 0, o2t126 = 0, panB = 0;
    };

    double sr;
    double p[NUM_EP] = {};
    Sm sm, tg;
    bool smInit = false;
    uint32_t seed = 123456789u;
    int ageCounter = 0, lastNote = -1;
    double gph1 = 0, gph2 = 0, sqv = 64;
    Voice vs[NV];
    double sv[8] = {};

    // per-block constants used by ctrl()
    struct Blk
    {
        int osw = 0, o2w = 0, ftype = 0, l1w = 0, l2w = 0;
        double l1f = 0, l2fBase = 0, trans = 0, bendR = 0, o2s = 0, pc = 1;
        int VS[4] = {}, VD[4] = {};
        double VI[4] = {};
        bool seqOn = false;
        int seqD = 0;
    } B;

    std::vector<std::vector<float>> tabs; // 64 waves x 4 band-limit levels

    double rnd();
    void ctrl (Voice& v);
    void buildTables();
    const std::vector<float>& tab (int k, int m) const { return tabs[(size_t) (k * 4 + m)]; }
    static double specAmp (int k, int n);
    static double phase0 (int k, int n);
    void setCf (double* cf, int c, double fc, double k) const;
};
} // namespace ym
