// YuMy Bacteria sound engine: C++ port of Core() from synthesizer/yumy-bacteria.html (AudioWorklet).
// No JUCE dependency, so it can be unit-tested offline against the JavaScript original.
#pragma once
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

namespace yb
{
#include "Tables.inc"

enum PId
{
#define X(id, name, def) P_##id,
    YB_PARAM_LIST(X)
#undef X
    NUM_P
};
enum OId
{
#define X(id, def) O_##id,
    YB_OPT_LIST(X)
#undef X
    NUM_O
};
extern const char* const kParamNames[NUM_P];
extern const int kParamDefaults[NUM_P];
extern const char* const kOptNames[NUM_O];
extern const int kOptDefaults[NUM_O];

struct MxSlot
{
    int src = 0;
    int d[3] = { 0, 0, 0 };
    double a[3] = { 0, 0, 0 };
};

class Core
{
public:
    explicit Core (double sampleRate);

    // ---- messages (same meaning as the worklet's msg()) ----
    void setParam (int idx, double v) { if (idx >= 0 && idx < NUM_P && v == v) P[idx] = v; }
    void setOpt (int idx, int v);
    void setLfoDest (int i, int v) { ld[i] = v; }
    void setMatrix (int slot, const MxSlot& s) { mx[slot] = s; }
    void wheels (double pb, double mw, double at) { bend = pb; mwheel = mw; this->at = at; }
    void noteOn (int n, double vel);   // 'on' message
    void noteOff (int n);              // 'off' message
    void sustain (bool on);            // CC64
    void panic();
    void setBpmOverride (double bpm) { bpmOverride = bpm; }   // host tempo (<=0: use ARP TEMPO)
    void seed (uint32_t s) { rng = s; }

    // L/R must be zeroed by the caller. mod may be nullptr (vocoder modulator, mono). Returns active voice count.
    int render (float* L, float* R, int N, const float* mod);

    int activeVoices() const;
    bool arpRunning() const { return A.running; }
    double sampleRate() const { return sr; }
    int maxV = 16;

private:
    // ---------- state ----------
    struct Lfo { double ph = 0, sh = 0, tg = 0, cur = 0, val = 0, age = 0; };
    struct Env { int st = 0; double lev = 0, sus = 1; };
    struct Voice
    {
        bool on = false, gate = false;
        int note = 60; double pc = 60, vel = 1; int age = 0;
        double p1 = 0, p2 = 0, sp = 0, hp[7] = {}, hp2[7] = {}, fs[4] = {};
        double last2 = 0, rnd = 0, gPrev = 0, uDet = 0, uPan = 0, uGain = 1;
        Lfo lfo[3];
        Env ae, fe;
    };
    struct EnvCfg { double a = 0, d = 0, s = 0, r = 0, sl = 0, slT = 0; };
    struct Derived
    {
        double shape1, pw1, semi1, track1, shape2, pw2, semi2, det2, bal, sub, noise, fm, phase0;
        int porta; double portaT, cut1, cut2, res, fenv, fkf, fbal; int transpose, bendRange; double master, soft1, soft2;
        double uniDet, uniSpread, vocM, vocIn, vocRel, vocQ, vocSh, vocNz, vocTilt, vocLv, bpm;
        double dist, chor, dsend, dtime, dfb, rsend, fine1, phM, phR, rgM, rgF, eqL, eqM, eqH, fmM, fmV;
        bool fxOn;
        double lfoRate[3], lfoAmt[3], lfoPhase[3], lfoDelay[3], lfoFade[3]; int lfoShape[3];
    };
    struct Arp
    {
        std::vector<int> list; std::set<int> down; bool running = false, first = false;
        double acc = 0, dur = 0; int step = 0, idx = 0; double gate = 0; std::vector<int> sounding; double vel = 0.9;
    };
    struct Comb { std::vector<float> b; int i = 0; double f = 0; };
    struct Allp { std::vector<float> b; int i = 0; };
    struct Fx
    {
        int dlen = 0, clen = 0, dw = 0, cw = 0; double dn = 0, lpL = 0, lpR = 0, cph = 0, pph = 0, rph = 0, fk = 0.15;
        std::vector<float> dL, dR, cL, cR;
        std::vector<Comb> combs[2]; std::vector<Allp> alls[2];
        double ph[2][5] = {}, fs[2][3][2] = {}, fc[3][3] = {}, eqc[3][5] = {}, eqs[2][3][4] = {};
    };
    struct Voc
    {
        int nb = 0; double f[32] = {}, ms[64] = {}, cl[64] = {}, cr[64] = {}, env[32] = {};
        double m1[32] = {}, m2[32] = {}, m3[32] = {}, c1[32] = {}, c2[32] = {}, c3[32] = {}, bg[32] = {};
    };

    double sr;
    uint32_t rng = 0x12345678u;
    double P[NUM_P] = {};
    int O[NUM_O] = {};
    int ld[3] = { 0, 0, 0 };
    MxSlot mx[6];
    Voice V[16];
    double bend = 0, mwheel = 0, at = 0, bpmOverride = 0;
    std::vector<int> held;
    std::vector<int> susDef;
    std::set<int> phys;
    bool sus = false;
    int lastNote = 60, tick = 0;
    double idle = 0, idleMax;
    Derived G;
    EnvCfg ecAmp, ecFlt;
    double src[17] = {}, M[32] = {};
    double df[7] = {}, hg[7] = {}, df2[7] = {}, hg2[7] = {};
    double gph[3] = { 0, 0, 0 }, fxs[8] = {}, fxm[8] = {}; int fxn = 0;
    Arp A; Fx F; Voc Vc; bool vcInit = false;
    const float* const* WT = nullptr;   // unused (tables are static)

    double rnd01();
    void derive();
    void offNow (int n);
    void arpOn (int n, double vel);
    void arpOff (int n);
    void arpRelease();
    void arpStop();
    void arpReset();
    void arpFire();
    void arpAdvance (double len);
    void vocode (float* L, float* R, int N, const float* mod);
    void initFx();
    double rd (const std::vector<float>& buf, int w, double d) const;
    void eqCoef (double* c, int type, double dB) const;
    void fx (float* L, float* R, int N);
    void noteOnCore (int n, double vel);
    void start (Voice& v, int n, double vel, int j, int nU);
    void noteOffCore (int n);
    void release (Voice& v);
    void stepEnv (Env& e, const EnvCfg& c, double len);
    void block (Voice& v, float* L, float* R, int s0, int len);
};
} // namespace yb
