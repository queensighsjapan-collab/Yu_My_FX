// YuMy Bacteria sound engine (C++ port of Core() from yumy-bacteria.html). See Engine.h.
#include "Engine.h"
#include <algorithm>

namespace yb
{
static const double PI2 = 6.283185307179586;

const char* const kParamNames[NUM_P] = {
#define X(id, name, def) name,
    YB_PARAM_LIST(X)
#undef X
};
const int kParamDefaults[NUM_P] = {
#define X(id, name, def) def,
    YB_PARAM_LIST(X)
#undef X
};
const char* const kOptNames[NUM_O] = {
#define X(id, def) #id,
    YB_OPT_LIST(X)
#undef X
};
const int kOptDefaults[NUM_O] = {
#define X(id, def) def,
    YB_OPT_LIST(X)
#undef X
};

static inline double clampd (double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
static inline double blep (double t, double dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}
static inline double tri (double p) { return p < 0.5 ? 4 * p - 1 : 3 - 4 * p; }
static double wave (double p, double dt, double s, double pw)
{
    if (s < 1) return std::sin (PI2 * p) * (1 - s) + tri (p) * s;
    const double sw = 2 * p - 1 - blep (p, dt);
    if (s < 2) return tri (p) * (2 - s) + sw * (s - 1);
    double q = p - pw; if (q < 0) q += 1;
    const double pl = (p < pw ? 1 : -1) + blep (p, dt) - blep (q, dt);
    return sw * (3 - s) + pl * (s - 2);
}
static inline double svf (double x, double* fs, int o, int mode, double k, double a1, double a2, double a3)
{
    const double v3 = x - fs[o + 1];
    const double v1 = a1 * fs[o] + a2 * v3;
    const double v2 = fs[o + 1] + a2 * fs[o] + a3 * v3;
    fs[o] = 2 * v1 - fs[o]; fs[o + 1] = 2 * v2 - fs[o + 1];
    return mode == 0 ? v2 : (mode == 1 ? x - k * v1 - v2 : (mode == 2 ? v1 * k : x - k * v1));
}
static inline double tm (double v) { return 0.001 * std::pow (10.0, 4.2 * v / 127); }
static inline int jsRound (double x) { return (int) std::floor (x + 0.5); }

static const int ARP_PAT_LEN[6] = { 1, 4, 4, 4, 6, 8 };
static const int ARP_PAT[6][8] = { { 1 }, { 2, 1, 1, 1 }, { 0, 1, 0, 1 }, { 1, 1, 0, 1 }, { 2, 0, 1, 0, 1, 0 }, { 2, 0, 1, 1, 0, 1, 0, 1 } };
static const double LFO_BEATS[9] = { 0, 4, 2, 1, 0.5, 0.25, 1.0 / 3, 1.0 / 6, 8 };
static const double VOWELS[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2140, 2950 }, { 450, 800, 2830 }, { 325, 700, 2700 } };
static const double VOC_COMP = 60;

// ---- wavetable-style tables (band-limited, 8 tables x 7 mip levels) ----
static const int WT_N = 2048, WT_COUNT = 8, WT_NM = 7;
static const int WT_MAXH[WT_NM] = { 64, 32, 16, 8, 4, 2, 1 };
static double wtAmp (int t, int k)
{
    switch (t)
    {
        case 0: return k == 1 ? 1 : (k == 2 ? 0.08 : 0);
        case 1: return (k & 1) ? ((((k - 1) >> 1) & 1) ? -1.0 : 1.0) / (double) (k * k) : 0;
        case 2: return 1.0 / k;
        case 3: return (k & 1) ? 1.0 / k : 0;
        case 4: { static const double t4[9] = { 0, 1, 0.8, 0.6, 0.5, 0, 0.4, 0, 0.3 }; return k < 9 ? t4[k] : 0; }
        case 5: { const double c[3] = { 4, 9, 15 }; double a = 0.5 / k;
                  for (int j = 0; j < 3; ++j) { const double d = (k - c[j]) / (1.6 + j * 0.8); a += std::exp (-d * d) * (1 - j * 0.25) * 3 / k; }
                  return a; }
        case 6: return 1.0 / std::sqrt ((double) k);
        default: { const double c[5] = { 2, 5, 7, 11, 16 }; double a = 0;
                   for (int j = 0; j < 5; ++j) { const double d = (k - c[j]) / 0.5; a += std::exp (-d * d) / (1 + j * 0.4); }
                   return a; }
    }
}
struct WTables
{
    std::vector<float> t[WT_COUNT][WT_NM];
    WTables()
    {
        for (int ti = 0; ti < WT_COUNT; ++ti)
        {
            for (int m = 0; m < WT_NM; ++m)
            {
                auto& tab = t[ti][m]; tab.assign (WT_N + 1, 0.0f);
                for (int k = 1; k <= WT_MAXH[m]; ++k)
                {
                    const double a = wtAmp (ti, k); if (a == 0) continue;
                    for (int i = 0; i < WT_N; ++i) tab[i] = (float) ((double) tab[i] + a * std::sin (PI2 * k * i / WT_N));
                }
            }
            double pk = 0; const auto& top = t[ti][0];
            for (int i = 0; i < WT_N; ++i) pk = std::max (pk, (double) std::fabs (top[i]));
            const double sc = pk > 0 ? 0.95 / pk : 1;
            for (int m = 0; m < WT_NM; ++m)
            {
                auto& tab = t[ti][m];
                for (int i = 0; i < WT_N; ++i) tab[i] = (float) ((double) tab[i] * sc);
                tab[WT_N] = tab[0];
            }
        }
    }
};
static const WTables& wtables() { static const WTables w; return w; }
static double wtRead (const WTables& T, double shape, int m, double p, double pw)
{
    const double pos = shape * (WT_COUNT - 1);
    const int i0 = (int) std::floor (pos); const double fr = pos - i0; const int i1 = i0 < WT_COUNT - 1 ? i0 + 1 : i0;
    const double q = p < pw ? 0.5 * p / pw : 0.5 + 0.5 * (p - pw) / (1 - pw);
    const double x = q * WT_N; const int xi = (int) x; const double xf = x - xi;
    const auto& a = T.t[i0][m]; const auto& b = T.t[i1][m];
    const double va = a[xi] + (a[xi + 1] - a[xi]) * xf, vb = b[xi] + (b[xi + 1] - b[xi]) * xf;
    return va + (vb - va) * fr;
}
static int mipFor (double f)
{
    const int km = (int) std::floor (0.5 / std::max (f, 1e-5));
    for (int m = 0; m < WT_NM; ++m) if (WT_MAXH[m] <= km) return m;
    return WT_NM - 1;
}

// =====================================================================
Core::Core (double sampleRate) : sr (sampleRate)
{
    for (int i = 0; i < NUM_O; ++i) O[i] = kOptDefaults[i];
    // same start values as the worklet's initial P
    P[P_Vocoder_Mix] = 0; P[P_Voc_Input] = 64; P[P_Voc_Release] = 40; P[P_Voc_Q] = 64; P[P_Voc_Shift] = 64; P[P_Voc_Noise] = 40; P[P_Voc_Tilt] = 64; P[P_Voc_Level] = 64;
    P[P_Unison_Detune] = 40; P[P_Unison_Spread] = 80; P[P_EQ_Low] = 64; P[P_EQ_Mid] = 64; P[P_EQ_High] = 64; P[P_Osc1_Fine] = 64; P[P_Phaser_Rate] = 40; P[P_Ring_Freq] = 64;
    idleMax = 6 * sr;
    initFx();
    derive();
}
double Core::rnd01()
{
    rng += 0x6D2B79F5u;
    uint32_t t = rng;
    t = (t ^ (t >> 15)) * (t | 1u);
    t = (t + ((t ^ (t >> 7)) * (t | 61u))) ^ t;
    return (double) (t ^ (t >> 14)) / 4294967296.0;
}
int Core::activeVoices() const { int n = 0; for (auto& v : V) if (v.on) ++n; return n; }

void Core::setOpt (int idx, int v)
{
    if (idx < 0 || idx >= NUM_O) return;
    O[idx] = v;
    if (idx == O_arp && ! v) arpReset();
    if (idx == O_ahold && ! v && A.down.empty()) arpReset();
}
void Core::noteOn (int n, double vel)
{
    phys.insert (n);
    susDef.erase (std::remove (susDef.begin(), susDef.end(), n), susDef.end());
    if (O[O_arp]) arpOn (n, vel); else noteOnCore (n, vel);
}
void Core::noteOff (int n)
{
    phys.erase (n);
    if (sus) { if (std::find (susDef.begin(), susDef.end(), n) == susDef.end()) susDef.push_back (n); return; }
    offNow (n);
}
void Core::sustain (bool s)
{
    if (s == sus) return;
    sus = s;
    if (! s)
    {
        auto a = susDef; susDef.clear();
        for (int n : a) if (! phys.count (n)) offNow (n);
    }
}
void Core::panic()
{
    for (auto& v : V) { v.on = false; v.gate = false; }
    held.clear(); phys.clear(); susDef.clear(); arpReset();
}
void Core::offNow (int n)
{
    if (O[O_arp]) { if (! A.down.count (n)) noteOffCore (n); arpOff (n); }
    else noteOffCore (n);
}

void Core::derive()
{
    const double* p = P; auto& g = G;
    g.shape1 = p[P_Osc1_Shape] / 127; g.pw1 = p[P_Osc1_PW] / 127;
    g.semi1 = clampd (p[P_Osc1_Semi] - 64, -48, 48); g.track1 = (p[P_Osc1_Keyfollow] - 64) / 32;
    g.shape2 = p[P_Osc2_Shape] / 127; g.pw2 = p[P_Osc2_PW] / 127;
    g.semi2 = clampd (p[P_Osc2_Semi] - 64, -48, 48); g.det2 = p[P_Osc2_Detune] / 127 * 0.6;
    g.bal = p[P_Osc_Balance] / 127; g.sub = p[P_Sub_Osc_Vol] / 127; g.noise = p[P_Noise_Vol] / 127;
    g.fm = p[P_FM_Amount] / 127; g.phase0 = p[P_Init_Phase] / 127;
    const double po = p[P_Portamento]; g.porta = po > 1 ? 1 : 0; g.portaT = 0.005 * std::pow (10.0, po / 127 * 3);
    g.cut1 = 20 * std::pow (2.0, p[P_Cutoff] / 127 * 10); g.cut2 = 20 * std::pow (2.0, p[P_Cutoff_2] / 127 * 10);
    g.res = p[P_Resonance] / 127; g.fenv = (p[P_Filter_Env_Amt] - 64) / 64;
    g.fkf = (p[P_Filter_Keyfollow] - 64) / 64; g.fbal = p[P_Filter_Balance] / 127;
    g.transpose = jsRound ((p[P_Transpose] - 64) * 24 / 64);
    g.bendRange = jsRound (p[P_Bend_Range] / 127 * 24);
    g.master = std::pow (p[P_Master_Volume] / 127, 1.5) * 0.5;
    g.soft1 = p[P_Param] / 127; g.soft2 = p[P_Value] / 127;
    g.uniDet = p[P_Unison_Detune] / 127 * 0.6; g.uniSpread = p[P_Unison_Spread] / 127;
    g.vocM = p[P_Vocoder_Mix] / 127; g.vocIn = std::pow (2.0, (p[P_Voc_Input] - 64) / 64 * 3); g.vocRel = 0.005 * std::pow (60.0, p[P_Voc_Release] / 127);
    g.vocQ = 3 + p[P_Voc_Q] / 127 * 12; g.vocSh = std::pow (2.0, (p[P_Voc_Shift] - 64) / 64 * 0.75); g.vocNz = p[P_Voc_Noise] / 127;
    g.vocTilt = (p[P_Voc_Tilt] - 64) / 64 * 12; g.vocLv = std::pow (2.0, (p[P_Voc_Level] - 64) / 64 * 2);
    g.bpm = bpmOverride > 0 ? bpmOverride : 40 + p[P_Arp_Tempo] / 127 * 160;
    g.dist = p[P_Distortion] / 127; g.chor = p[P_Chorus_Mix] / 127;
    g.dsend = p[P_Delay_Send] / 127; g.dtime = p[P_Delay_Time]; g.dfb = p[P_Delay_Feedback] / 127 * 0.92;
    g.rsend = p[P_Reverb_Send] / 127;
    g.fine1 = (p[P_Osc1_Fine] - 64) / 64;
    g.phM = p[P_Phaser_Mix] / 127; g.phR = 0.05 * std::pow (2.0, p[P_Phaser_Rate] / 127 * 7);
    g.rgM = p[P_Ring_Mix] / 127; g.rgF = 40 * std::pow (2.0, p[P_Ring_Freq] / 127 * 6.6);
    g.eqL = (p[P_EQ_Low] - 64) / 64 * 12; g.eqM = (p[P_EQ_Mid] - 64) / 64 * 12; g.eqH = (p[P_EQ_High] - 64) / 64 * 12;
    g.fmM = p[P_Formant_Mix] / 127; g.fmV = p[P_Formant_Vowel] / 127;
    g.fxOn = (g.dist > 0 || g.chor > 0 || g.dsend > 0 || g.rsend > 0 || g.phM > 0 || g.rgM > 0 || g.fmM > 0
              || std::fabs (g.eqL) > 0.05 || std::fabs (g.eqM) > 0.05 || std::fabs (g.eqH) > 0.05);
    const int lr[3] = { P_LFO1_Rate, P_LFO2_Rate, P_LFO3_Rate }, ls[3] = { P_LFO1_Shape, P_LFO2_Shape, P_LFO3_Shape },
              lp[3] = { P_LFO1_Phase, P_LFO2_Phase, P_LFO3_Phase }, lde[3] = { P_LFO1_Delay, P_LFO2_Delay, P_LFO3_Delay },
              lf[3] = { P_LFO1_Fade, P_LFO2_Fade, P_LFO3_Fade }, la[3] = { P_LFO1_Amount, P_LFO2_Amount, P_LFO3_Amount };
    for (int i = 0; i < 3; ++i)
    {
        g.lfoRate[i] = 0.05 * std::pow (2.0, p[lr[i]] / 127 * 10);
        g.lfoShape[i] = std::min (7, (int) std::floor (p[ls[i]] / 127 * 8));
        g.lfoPhase[i] = p[lp[i]] / 127; const double dl = p[lde[i]] / 127, fd = p[lf[i]] / 127;
        g.lfoDelay[i] = dl * dl * 3; g.lfoFade[i] = fd * fd * 3;
        g.lfoAmt[i] = p[la[i]] / 127;
    }
    auto envc = [&] (EnvCfg& c, int a, int d, int s, int sustime, int r)
    {
        c.a = tm (p[a]); c.d = tm (p[d]); c.s = p[s] / 127; c.r = tm (p[r]);
        const double sl = (p[sustime] - 64) / 64; c.sl = sl; c.slT = 0.5 + 30 * std::pow (1 - std::fabs (sl), 2.0);
    };
    envc (ecAmp, P_Amp_Attack, P_Amp_Decay, P_Amp_Sustain, P_Amp_Sus_Time, P_Amp_Release);
    envc (ecFlt, P_Flt_Attack, P_Flt_Decay, P_Flt_Sustain, P_Flt_Sus_Time, P_Flt_Release);
}

// ================= ARPEGGIATOR =================
void Core::arpOn (int n, double vel)
{
    idle = 0;
    if (O[O_ahold] && A.down.empty()) { arpRelease(); A.list.clear(); A.running = false; }
    if (std::find (A.list.begin(), A.list.end(), n) == A.list.end()) A.list.push_back (n);
    A.down.insert (n); A.vel = vel;
    if (! A.running) { A.running = true; A.first = true; A.step = 0; A.idx = 0; A.acc = 0; A.dur = 0; }
}
void Core::arpOff (int n)
{
    A.down.erase (n);
    if (! O[O_ahold])
    {
        auto it = std::find (A.list.begin(), A.list.end(), n);
        if (it != A.list.end()) A.list.erase (it);
        if (A.list.empty()) arpStop();
    }
}
void Core::arpRelease() { for (int n : A.sounding) noteOffCore (n); A.sounding.clear(); }
void Core::arpStop() { arpRelease(); A.running = false; A.first = false; }
void Core::arpReset() { arpStop(); A.list.clear(); A.down.clear(); }
void Core::arpFire()
{
    arpRelease();
    if (A.list.empty()) { A.running = false; return; }
    const double bpm = G.bpm;
    static const int SPB[6] = { 1, 2, 3, 4, 6, 8 };
    const int spb = (O[O_ares] >= 0 && O[O_ares] < 6) ? SPB[O[O_ares]] : 4;
    const double stepLen = sr * 60 / bpm / spb;
    const double sw = P[P_Arp_Swing] / 127 * 0.5;
    A.dur = stepLen * (A.step % 2 == 0 ? 1 + sw : 1 - sw);
    const int pi = (O[O_apat] >= 0 && O[O_apat] < 6) ? O[O_apat] : 0;
    const int pv = ARP_PAT[pi][A.step % ARP_PAT_LEN[pi]]; A.step++;
    static const double GL[5] = { 0.15, 0.35, 0.6, 0.85, 1 };
    A.gate = A.dur * ((O[O_alen] >= 0 && O[O_alen] < 5) ? GL[O[O_alen]] : 0.6);
    if (pv == 0) return;
    const int oct = std::min (4, 1 + (int) std::floor (P[P_Arp_Range] / 32));
    std::vector<int> base = A.list;
    if (O[O_amode] != 3) std::sort (base.begin(), base.end());
    std::vector<int> seq;
    for (int o = 0; o < oct; ++o) for (int n : base) seq.push_back (n + 12 * o);
    const double vel = pv == 2 ? std::min (1.0, A.vel + 0.15) : A.vel * 0.75;
    std::vector<int> notes; const int sl = (int) seq.size();
    switch (O[O_amode])
    {
        case 1: notes.push_back (seq[sl - 1 - (A.idx % sl)]); break;
        case 2: { std::vector<int> full = seq; for (int i = sl - 2; i >= 1; --i) full.push_back (seq[i]);   // seq.slice(1,-1).reverse()
                  notes.push_back (full[A.idx % full.size()]); break; }
        case 4: notes.push_back (seq[(int) (rnd01() * sl)]); break;
        case 5: for (int x : base) notes.push_back (x + 12 * (A.idx % oct)); break;
        default: notes.push_back (seq[A.idx % sl]);
    }
    A.idx++;
    for (int n : notes) { if (n > 127) continue; noteOnCore (n, vel); A.sounding.push_back (n); }
}
void Core::arpAdvance (double len)
{
    if (! O[O_arp] || ! A.running) return;
    if (A.first) { A.first = false; arpFire(); A.acc = 0; }
    if (! A.running) return;
    A.acc += len;
    if (! A.sounding.empty()) { A.gate -= len; if (A.gate <= 0) arpRelease(); }
    int guard = 0;
    while (A.running && A.acc >= A.dur && guard++ < 8) { A.acc -= A.dur; arpFire(); }
}

// ================= VOCODER =================
void Core::vocode (float* L, float* R, int N, const float* mod)
{
    const int nbT[3] = { 8, 16, 32 };
    const int vb = (O[O_vbands] >= 0 && O[O_vbands] < 3) ? O[O_vbands] : 1;
    const int nb = nbT[vb];
    Voc& V = Vc;
    if (V.nb != nb) { V.nb = nb; std::fill (V.ms, V.ms + 64, 0.0); std::fill (V.cl, V.cl + 64, 0.0); std::fill (V.cr, V.cr + 64, 0.0); std::fill (V.env, V.env + 32, 0.0); }
    const double k = 1 / std::max (1.5, G.vocQ * (nb - 1) / 15), mix = G.vocM, lim = 0.45 * sr; const int nz0 = (int) std::floor (nb * 9.0 / 16);
    for (int b = 0; b < nb; ++b)
    {
        V.f[b] = 110 * std::pow (8000.0 / 110, (double) b / (nb - 1));
        const double gm = std::tan (M_PI * std::min (V.f[b] * G.vocSh, lim) / sr), gc = std::tan (M_PI * std::min (V.f[b], lim) / sr);
        V.m1[b] = 1 / (1 + gm * (gm + k)); V.m2[b] = gm * V.m1[b]; V.m3[b] = gm * V.m2[b];
        V.c1[b] = 1 / (1 + gc * (gc + k)); V.c2[b] = gc * V.c1[b]; V.c3[b] = gc * V.c2[b];
        V.bg[b] = std::pow (10.0, G.vocTilt * ((double) b / (nb - 1) - 0.5) / 20);
    }
    const double ca = 1 - std::exp (-1 / (0.002 * sr)), cr = 1 - std::exp (-1 / (G.vocRel * sr));
    static const double LV[3] = { 0.69, 1, 1.8 };
    const double inG = G.vocIn, out = G.vocLv * VOC_COMP * mix * LV[vb], nz = G.vocNz * 0.2, dry = 1 - mix;
    for (int i = 0; i < N; ++i)
    {
        const double m = mod[i] * inG, l0 = L[i], r0 = R[i], n = (rnd01() * 2 - 1) * nz;
        double oL = 0, oR = 0;
        for (int b = 0; b < nb; ++b)
        {
            double i1 = V.ms[2 * b], i2 = V.ms[2 * b + 1];
            double v3 = m - i2, v1 = V.m1[b] * i1 + V.m2[b] * v3, v2 = i2 + V.m2[b] * i1 + V.m3[b] * v3;
            V.ms[2 * b] = 2 * v1 - i1; V.ms[2 * b + 1] = 2 * v2 - i2;
            const double rc = std::fabs (k * v1); double e = V.env[b]; e += (rc > e ? ca : cr) * (rc - e); if (e < 1e-12) e = 0; V.env[b] = e;
            const double nn = b >= nz0 ? n : 0, xl = l0 + nn, xr = r0 + nn, c1 = V.c1[b], c2 = V.c2[b], c3 = V.c3[b];
            i1 = V.cl[2 * b]; i2 = V.cl[2 * b + 1]; v3 = xl - i2; v1 = c1 * i1 + c2 * v3; v2 = i2 + c2 * i1 + c3 * v3; V.cl[2 * b] = 2 * v1 - i1; V.cl[2 * b + 1] = 2 * v2 - i2;
            const double g = e * V.bg[b] * k; oL += v1 * g;
            i1 = V.cr[2 * b]; i2 = V.cr[2 * b + 1]; v3 = xr - i2; v1 = c1 * i1 + c2 * v3; v2 = i2 + c2 * i1 + c3 * v3; V.cr[2 * b] = 2 * v1 - i1; V.cr[2 * b + 1] = 2 * v2 - i2;
            oR += v1 * g;
        }
        L[i] = (float) (l0 * dry + oL * out); R[i] = (float) (r0 * dry + oR * out);
    }
}

// ================= EFFECTS =================
void Core::initFx()
{
    Fx& f = F;
    f.dlen = (int) std::floor (sr * 2.2); f.dL.assign (f.dlen, 0.0f); f.dR.assign (f.dlen, 0.0f);
    f.dw = 0; f.dn = 0.3 * sr; f.lpL = 0; f.lpR = 0;
    f.clen = (int) std::floor (sr * 0.1); f.cL.assign (f.clen, 0.0f); f.cR.assign (f.clen, 0.0f);
    f.cw = 0; f.cph = 0;
    const double sc = sr / 44100; const int cl[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }, al[4] = { 556, 441, 341, 225 };
    for (int ch = 0; ch < 2; ++ch)
    {
        const int sp = ch ? 23 : 0;
        f.combs[ch].resize (8); f.alls[ch].resize (4);
        for (int c = 0; c < 8; ++c) { f.combs[ch][c].b.assign ((size_t) std::floor ((cl[c] + sp) * sc), 0.0f); f.combs[ch][c].i = 0; f.combs[ch][c].f = 0; }
        for (int c = 0; c < 4; ++c) { f.alls[ch][c].b.assign ((size_t) std::floor ((al[c] + sp) * sc), 0.0f); f.alls[ch][c].i = 0; }
    }
    f.pph = 0; f.rph = 0; f.fk = 0.15;
}
double Core::rd (const std::vector<float>& buf, int w, double d) const
{
    const int n = (int) buf.size(); double pos = w - d;
    while (pos < 0) pos += n;
    const int i0 = (int) std::floor (pos); const double fr = pos - i0; const int i1 = i0 + 1 >= n ? 0 : i0 + 1;
    return (double) buf[i0 >= n ? 0 : i0] * (1 - fr) + (double) buf[i1] * fr;
}
void Core::eqCoef (double* c, int type, double dB) const
{
    const double A_ = std::pow (10.0, dB / 40), f = type == 0 ? 200 : (type == 1 ? 1000 : 5000);
    const double w = PI2 * f / sr, cs = std::cos (w), sn = std::sin (w);
    double b0, b1, b2, a0, a1, a2;
    if (type == 1) { const double al = sn / (2 * 0.8); b0 = 1 + al * A_; b1 = -2 * cs; b2 = 1 - al * A_; a0 = 1 + al / A_; a1 = -2 * cs; a2 = 1 - al / A_; }
    else
    {
        const double al = sn * 0.70710678, t = 2 * std::sqrt (A_) * al;
        if (type == 0) { b0 = A_ * ((A_ + 1) - (A_ - 1) * cs + t); b1 = 2 * A_ * ((A_ - 1) - (A_ + 1) * cs); b2 = A_ * ((A_ + 1) - (A_ - 1) * cs - t); a0 = (A_ + 1) + (A_ - 1) * cs + t; a1 = -2 * ((A_ - 1) + (A_ + 1) * cs); a2 = (A_ + 1) + (A_ - 1) * cs - t; }
        else { b0 = A_ * ((A_ + 1) + (A_ - 1) * cs + t); b1 = -2 * A_ * ((A_ - 1) + (A_ + 1) * cs); b2 = A_ * ((A_ + 1) + (A_ - 1) * cs - t); a0 = (A_ + 1) - (A_ - 1) * cs + t; a1 = 2 * ((A_ - 1) - (A_ + 1) * cs); a2 = (A_ + 1) - (A_ - 1) * cs - t; }
    }
    c[0] = b0 / a0; c[1] = b1 / a0; c[2] = b2 / a0; c[3] = a1 / a0; c[4] = a2 / a0;
}
void Core::fx (float* L, float* R, int N)
{
    Fx& f = F;
    const double dist = clampd (G.dist + fxm[3], 0, 1), chor = clampd (G.chor + fxm[2], 0, 1);
    const double dsend = clampd (G.dsend + fxm[0], 0, 1), rsend = clampd (G.rsend + fxm[1], 0, 1), dfb = G.dfb;
    const double phM = clampd (G.phM + fxm[4], 0, 1), vow = clampd (G.fmV + fxm[5], 0, 1);
    const double pre = 1 + dist * dist * 40, mk = 1 / (1 + dist * 0.8);
    const bool fl = O[O_cmode] == 1;
    const double cbase = (fl ? 0.003 : 0.018) * sr, cdep = (fl ? 0.0025 : 0.004) * sr, crate = (fl ? 0.2 : 0.35) / sr, cfb = fl ? 0.55 : 0;
    static const double DS[5] = { 0, 1, 0.5, 0.75, 0.25 };
    double tt = O[O_dsync] > 0 ? DS[std::min (4, O[O_dsync])] * 60 / G.bpm : 0.02 * std::pow (50.0, G.dtime / 127);
    tt = std::min (2.0, std::max (0.01, tt)) * sr;
    static const double RFB[3] = { 0.75, 0.84, 0.91 };
    const double rfb = (O[O_rsize] >= 0 && O[O_rsize] < 3) ? RFB[O[O_rsize]] : 0.84, damp = 0.3; const int dl = f.dlen, cl = f.clen;
    const double rgM = G.rgM, rgInc = G.rgF / sr, phInc = G.phR / sr, fmM = G.fmM;
    if (fmM > 0)
    {
        const double pos = vow * 4; const int i0 = std::min (3, (int) std::floor (pos)); const double fr = pos - i0;
        for (int b = 0; b < 3; ++b)
        {
            const double fq = std::min (VOWELS[i0][b] * std::pow (VOWELS[i0 + 1][b] / VOWELS[i0][b], fr), 0.45 * sr);
            const double g = std::tan (M_PI * fq / sr), k = f.fk, a1 = 1 / (1 + g * (g + k));
            f.fc[b][0] = a1; f.fc[b][1] = g * a1; f.fc[b][2] = g * g * a1;
        }
    }
    const bool eqOn = std::fabs (G.eqL) > 0.05 || std::fabs (G.eqM) > 0.05 || std::fabs (G.eqH) > 0.05;
    if (eqOn) { eqCoef (f.eqc[0], 0, G.eqL); eqCoef (f.eqc[1], 1, G.eqM); eqCoef (f.eqc[2], 2, G.eqH); }
    for (int i = 0; i < N; ++i)
    {
        double l = L[i], r = R[i];
        if (dist > 0) { l = l * (1 - dist) + std::tanh (l * pre) * mk * dist; r = r * (1 - dist) + std::tanh (r * pre) * mk * dist; }
        if (rgM > 0) { f.rph += rgInc; if (f.rph >= 1) f.rph -= 1; const double s = std::sin (PI2 * f.rph); l = l * (1 - rgM) + l * s * rgM; r = r * (1 - rgM) + r * s * rgM; }
        if (fmM > 0)
        {
            const double k = f.fk; double wl = 0, wr = 0;
            for (int b = 0; b < 3; ++b)
            {
                const double gn = b == 0 ? 1 : (b == 1 ? 0.8 : 0.5);
                wl += svf (l, f.fs[0][b], 0, 2, k, f.fc[b][0], f.fc[b][1], f.fc[b][2]) * gn;
                wr += svf (r, f.fs[1][b], 0, 2, k, f.fc[b][0], f.fc[b][1], f.fc[b][2]) * gn;
            }
            l += (wl * 2.2 - l) * fmM; r += (wr * 2.2 - r) * fmM;
        }
        if (phM > 0)
        {
            f.pph += phInc; if (f.pph >= 1) f.pph -= 1;
            for (int ch = 0; ch < 2; ++ch)
            {
                double* z = f.ph[ch]; const double lf = std::sin (PI2 * (f.pph + ch * 0.25));
                const double fq = 250 * std::pow (2.0, 3.5 * (0.5 + 0.5 * lf)), t = std::tan (M_PI * std::min (fq, 0.45 * sr) / sr), a = (t - 1) / (t + 1);
                double x = (ch ? r : l) + z[4] * 0.5;
                for (int s = 0; s < 4; ++s) { const double y = a * x + z[s]; z[s] = x - a * y; x = y; }
                z[4] = x;
                if (ch) r += (x - r) * phM * 0.5; else l += (x - l) * phM * 0.5;
            }
        }
        f.cph += crate; if (f.cph >= 1) f.cph -= 1;
        const double wl = rd (f.cL, f.cw, cbase + cdep * std::sin (PI2 * f.cph));
        const double wr = rd (f.cR, f.cw, cbase + cdep * std::sin (PI2 * (f.cph + 0.25)));
        f.cL[f.cw] = (float) (l + wl * cfb); f.cR[f.cw] = (float) (r + wr * cfb);
        if (++f.cw >= cl) f.cw = 0;
        if (chor > 0) { l = l * (1 - chor * 0.4) + wl * chor * 0.9; r = r * (1 - chor * 0.4) + wr * chor * 0.9; }
        if (eqOn)
        {
            for (int b = 0; b < 3; ++b)
            {
                const double* c = f.eqc[b];
                double* st = f.eqs[0][b]; double y = c[0] * l + c[1] * st[0] + c[2] * st[1] - c[3] * st[2] - c[4] * st[3]; st[1] = st[0]; st[0] = l; st[3] = st[2]; st[2] = y; l = y;
                st = f.eqs[1][b]; y = c[0] * r + c[1] * st[0] + c[2] * st[1] - c[3] * st[2] - c[4] * st[3]; st[1] = st[0]; st[0] = r; st[3] = st[2]; st[2] = y; r = y;
            }
        }
        f.dn += (tt - f.dn) * 0.0003;
        const double yl = rd (f.dL, f.dw, f.dn), yr = rd (f.dR, f.dw, f.dn);
        f.lpL += (yl - f.lpL) * 0.45; f.lpR += (yr - f.lpR) * 0.45;
        f.dL[f.dw] = (float) ((l + r) * 0.5 * dsend + f.lpR * dfb); f.dR[f.dw] = (float) (f.lpL * dfb);
        if (++f.dw >= dl) f.dw = 0;
        l += yl; r += yr;
        if (rsend > 0)
        {
            const double inp = (l + r) * 0.015 * rsend; double a = 0, b = 0;
            for (int c = 0; c < 8; ++c)
            {
                Comb* k = &f.combs[0][c]; double o = k->b[k->i]; k->f = o * (1 - damp) + k->f * damp; k->b[k->i] = (float) (inp + k->f * rfb); if (++k->i >= (int) k->b.size()) k->i = 0; a += o;
                k = &f.combs[1][c]; o = k->b[k->i]; k->f = o * (1 - damp) + k->f * damp; k->b[k->i] = (float) (inp + k->f * rfb); if (++k->i >= (int) k->b.size()) k->i = 0; b += o;
            }
            for (int c = 0; c < 4; ++c)
            {
                Allp* k = &f.alls[0][c]; double bo = k->b[k->i]; const double xa = a; a = -xa + bo; k->b[k->i] = (float) (xa + bo * 0.5); if (++k->i >= (int) k->b.size()) k->i = 0;
                k = &f.alls[1][c]; bo = k->b[k->i]; const double xb = b; b = -xb + bo; k->b[k->i] = (float) (xb + bo * 0.5); if (++k->i >= (int) k->b.size()) k->i = 0;
            }
            l += a * 3; r += b * 3;
        }
        L[i] = (float) l; R[i] = (float) r;
    }
}

// ================= VOICES =================
void Core::noteOnCore (int n, double vel)
{
    derive(); tick++; idle = 0;
    const int nU = O[O_uni] > 0 ? std::min (std::min (7, O[O_uni] + 1), maxV) : 1;
    if (O[O_mono])
    {
        auto it = std::find (held.begin(), held.end(), n); if (it != held.end()) held.erase (it);
        held.push_back (n);
        for (int j = 0; j < nU; ++j)
        {
            Voice& v = V[j];
            if (v.on && v.gate && O[O_mono] != 2) { v.note = n; v.vel = vel; }
            else start (v, n, vel, j, nU);
        }
        for (int k = nU; k < 16; ++k) { Voice& o = V[k]; if (o.on && o.gate) release (o); }
        return;
    }
    for (int j = 0; j < nU; ++j)
    {
        Voice* v = nullptr; const int mv = maxV;
        for (int q = 0; q < mv; ++q) { if (! V[q].on) { v = &V[q]; break; } }
        if (! v)
        {
            double best = 1e18;
            for (int q = 0; q < mv; ++q) { Voice& x = V[q]; const double sc = (x.ae.st == 4 ? 0 : 1e9) + x.age; if (sc < best) { best = sc; v = &x; } }
        }
        start (*v, n, vel, j, nU);
    }
}
void Core::start (Voice& v, int n, double vel, int j, int nU)
{
    const bool re = v.on;
    if (! nU) nU = 1;
    idle = 0;
    v.age = tick; v.note = n; v.vel = vel; v.gate = true; v.rnd = rnd01() * 2 - 1;
    v.uDet = 0; v.uPan = 0; v.uGain = 1;
    if (nU > 1) { const double x = ((double) j / (nU - 1)) * 2 - 1; v.uDet = x * G.uniDet; v.uPan = x * G.uniSpread; v.uGain = 1 / std::sqrt ((double) nU); }
    v.pc = G.porta ? lastNote : n;
    if (! re) { std::fill (v.fs, v.fs + 4, 0.0); v.ae.lev = 0; v.fe.lev = 0; v.gPrev = 0; }
    v.ae.st = 1; v.fe.st = 1; v.ae.sus = ecAmp.s; v.fe.sus = ecFlt.s;
    v.p1 = G.phase0; v.p2 = G.phase0; v.sp = G.phase0;
    if (nU > 1) { v.p1 = std::fmod (v.p1 + rnd01(), 1.0); v.p2 = std::fmod (v.p2 + rnd01(), 1.0); v.sp = std::fmod (v.sp + rnd01(), 1.0); }
    for (int k = 0; k < 7; ++k) v.hp[k] = rnd01();
    for (int k = 0; k < 7; ++k) v.hp2[k] = rnd01();
    const int lk[3] = { O_l1k, O_l2k, O_l3k };
    for (int i = 0; i < 3; ++i)
    {
        Lfo& l = v.lfo[i];
        l.ph = O[lk[i]] ? G.lfoPhase[i] : std::fmod (gph[i] + G.lfoPhase[i], 1.0);
        l.sh = rnd01() * 2 - 1; l.tg = rnd01() * 2 - 1; l.cur = 0; l.val = 0; l.age = 0;
    }
    v.on = true; v.last2 = 0; lastNote = n;
}
void Core::noteOffCore (int n)
{
    if (O[O_mono])
    {
        auto it = std::find (held.begin(), held.end(), n); if (it != held.end()) held.erase (it);
        if (held.empty()) { for (auto& v : V) if (v.on && v.gate) release (v); }
        else { const int ln = held.back(); for (auto& v : V) if (v.on && v.gate && v.note == n) v.note = ln; }
        return;
    }
    for (auto& v : V) { if (v.on && v.gate && v.note == n) release (v); }
}
void Core::release (Voice& v) { v.gate = false; v.ae.st = 4; v.fe.st = 4; }
void Core::stepEnv (Env& e, const EnvCfg& c, double len)
{
    switch (e.st)
    {
        case 1: e.lev += len / (c.a * sr); if (e.lev >= 1) { e.lev = 1; e.st = 2; } break;
        case 2: e.lev = e.sus + (e.lev - e.sus) * std::exp (-5 * len / (c.d * sr));
                if (std::fabs (e.lev - e.sus) < 0.002) { e.lev = e.sus; e.st = 3; } break;
        case 3:
            if (c.sl < -0.02) e.sus *= std::exp (-len / (sr * c.slT));
            else if (c.sl > 0.02) e.sus += (1 - e.sus) * (1 - std::exp (-len / (sr * c.slT)));
            else e.sus = c.s;
            e.lev = e.sus; break;
        case 4: e.lev *= std::exp (-5 * len / (c.r * sr)); if (e.lev < 0.0005) { e.lev = 0; e.st = 0; } break;
    }
}

void Core::block (Voice& v, float* L, float* R, int s0, int len)
{
    const WTables& WTb = wtables();
    // ---- modulation sources ----
    src[1] = v.lfo[0].val; src[2] = v.lfo[1].val; src[3] = v.lfo[2].val;
    src[4] = v.fe.lev; src[5] = v.ae.lev; src[6] = v.vel; src[7] = mwheel;
    src[8] = (v.note - 60) / 60.0; src[9] = v.rnd; src[10] = G.soft1; src[11] = G.soft2;
    src[12] = at; src[13] = bend; src[14] = (v.lfo[0].val + 1) * 0.5; src[15] = (v.lfo[1].val + 1) * 0.5; src[16] = (v.lfo[2].val + 1) * 0.5;
    std::fill (M, M + 32, 0.0);
    for (int i = 0; i < 3; ++i) { const int d = ld[i]; if (d) M[d] += src[1 + i] * G.lfoAmt[i]; }
    for (int i = 0; i < 6; ++i)
    {
        const MxSlot& sl = mx[i]; if (! sl.src) continue;
        const double sv = src[sl.src];
        for (int j = 0; j < 3; ++j) { const int d = sl.d[j]; if (d) M[d] += sv * sl.a[j]; }
    }
    for (int q = 0; q < 6; ++q) fxs[q] += M[22 + q];
    fxn++;
    // ---- LFOs ----
    const int lsI[3] = { O_l1s, O_l2s, O_l3s };
    for (int i = 0; i < 3; ++i)
    {
        Lfo& l = v.lfo[i]; const int ls = O[lsI[i]];
        double rate = ls ? G.bpm / 60 / LFO_BEATS[ls] : G.lfoRate[i]; const double rm = M[17 + i];
        if (rm != 0) rate *= std::pow (2.0, rm * 4);
        const double adv = rate * len / sr;
        l.ph += adv;
        if (l.ph >= 1) { l.ph -= std::floor (l.ph); l.sh = rnd01() * 2 - 1; l.tg = rnd01() * 2 - 1; }
        double raw;
        switch (G.lfoShape[i])
        {
            case 0: raw = std::sin (PI2 * l.ph); break;
            case 1: raw = tri (l.ph); break;
            case 2: raw = 2 * l.ph - 1; break;
            case 3: raw = l.ph < 0.5 ? 1 : -1; break;
            case 4: raw = l.sh; break;
            case 5: l.cur += (l.tg - l.cur) * std::min (1.0, adv * 5); raw = l.cur; break;
            case 6: raw = (std::sin (PI2 * l.ph) + 0.6 * std::sin (PI2 * 3 * l.ph + 1) + 0.35 * std::sin (PI2 * 5 * l.ph + 2)) / 1.65; break;
            default: raw = std::floor (l.ph * 8) / 7 * 2 - 1;
        }
        l.age += len; const double tt = l.age / sr, dly = G.lfoDelay[i];
        double gn = 1; if (tt < dly) gn = 0; else if (G.lfoFade[i] > 0.001) gn = std::min (1.0, (tt - dly) / G.lfoFade[i]);
        l.val = raw * gn;
    }
    // ---- envelopes ----
    stepEnv (v.ae, ecAmp, len); stepEnv (v.fe, ecFlt, len);
    if (v.ae.st == 0) { v.on = false; v.gate = false; return; }
    // ---- pitch ----
    if (G.porta) v.pc += (v.note - v.pc) * (1 - std::exp (-len / (sr * G.portaT))); else v.pc = v.note;
    const double base = 60 + (v.pc - 60) * G.track1 + G.transpose + bend * G.bendRange + v.uDet;
    const double f1 = std::min (0.45, 440 * std::pow (2.0, (base + G.semi1 + G.fine1 + (M[1] + M[3]) * 24 - 69) / 12) / sr);
    const double f2 = std::min (0.45, 440 * std::pow (2.0, (base + G.semi2 + G.det2 + (M[2] + M[3]) * 24 - 69) / 12) / sr);
    const double s1 = clampd (G.shape1 + M[4], 0, 1), s2 = clampd (G.shape2 + M[5], 0, 1);
    const double pw1 = clampd (G.pw1 + M[6] * 0.5, 0.03, 0.97), pw2 = clampd (G.pw2 + M[6] * 0.5, 0.03, 0.97);
    const double fm = clampd (G.fm + M[7], 0, 1), bal = clampd (G.bal + M[8], 0, 1);
    const double sub = clampd (G.sub + M[9], 0, 1), noi = clampd (G.noise + M[21], 0, 1);
    const bool hyper = O[O_osc1mode] == 1, wt1 = O[O_osc1mode] == 2, hyper2 = O[O_osc2mode] == 2, wt2 = O[O_osc2mode] == 1;
    const int mp1 = wt1 ? mipFor (f1) : 0, mp2 = wt2 ? mipFor (f2) : 0;
    if (hyper2)
    {
        const double spread = std::pow (s2, 1.5) * 0.6, gc = 1 - 0.55 * pw2, gs = 0.1 + 0.38 * pw2, nm = 1.3 / (gc + 6 * gs);
        for (int k = 0; k < 7; ++k) { df2[k] = std::min (0.45, f2 * std::pow (2.0, (k - 3) * spread / 12)); hg2[k] = (k == 3 ? gc : gs) * nm; }
    }
    if (hyper)
    {
        const double spread = std::pow (s1, 1.5) * 0.6, gc = 1 - 0.55 * pw1, gs = 0.1 + 0.38 * pw1, nm = 1.3 / (gc + 6 * gs);
        for (int k = 0; k < 7; ++k) { df[k] = std::min (0.45, f1 * std::pow (2.0, (k - 3) * spread / 12)); hg[k] = (k == 3 ? gc : gs) * nm; }
    }
    // ---- filters ----
    const double kfo = G.fkf * (v.note - 60) / 12, eo = G.fenv * v.fe.lev * 7;
    const double fc1 = clampd (G.cut1 * std::pow (2.0, eo + kfo + (M[10] + M[12]) * 6), 18, 0.46 * sr);
    const double fc2 = clampd (G.cut2 * std::pow (2.0, eo + kfo + (M[11] + M[12]) * 6), 18, 0.46 * sr);
    const double res = clampd (G.res + M[13], 0, 1), k = 2 - 1.95 * res;
    const double g1 = std::tan (M_PI * fc1 / sr), a11 = 1 / (1 + g1 * (g1 + k)), a12 = g1 * a11, a13 = g1 * a12;
    const double g2 = std::tan (M_PI * fc2 / sr), a21 = 1 / (1 + g2 * (g2 + k)), a22 = g2 * a21, a23 = g2 * a22;
    const double fb = clampd (G.fbal + M[14], 0, 1), sat = 1 + clampd (M[20], 0, 1) * 5;
    const int rt = O[O_routing], m1 = O[O_f1mode], m2 = O[O_f2mode]; const bool sync = O[O_sync] == 1, ring = O[O_ring] == 1;
    const double ca = std::cos (bal * 1.5707963), cb = std::sin (bal * 1.5707963);
    // ---- amp / pan ----
    const double gt = v.ae.lev * v.vel * clampd (1 + M[15], 0, 1.5) * v.uGain, gp = v.gPrev;
    const double pan = clampd (M[16] + v.uPan, -1, 1), pa = (pan + 1) * 0.7853982, pl = std::cos (pa) * 1.4142, pr = std::sin (pa) * 1.4142;
    double* fs = v.fs;
    if (! (fs[0] == fs[0]) || ! (fs[2] == fs[2])) std::fill (fs, fs + 4, 0.0);
    double last2 = v.last2;
    for (int i = 0; i < len; ++i)
    {
        v.p1 += f1; bool wrap = false; if (v.p1 >= 1) { v.p1 -= 1; wrap = true; }
        double o1;
        if (hyper)
        {
            double sum = 0;
            for (int q = 0; q < 7; ++q)
            {
                double h = v.hp[q] + df[q]; if (h >= 1) h -= 1; v.hp[q] = h;
                double hh = h; if (fm > 0) { hh = h + fm * 0.8 * last2; hh -= std::floor (hh); }
                sum += (2 * hh - 1 - blep (hh, df[q])) * hg[q];
            }
            o1 = sum;
        }
        else if (wt1)
        {
            double ph = v.p1; if (fm > 0) { ph = v.p1 + fm * 0.8 * last2; ph -= std::floor (ph); }
            o1 = wtRead (WTb, s1, mp1, ph, pw1);
        }
        else
        {
            double ph = v.p1; if (fm > 0) { ph = v.p1 + fm * 0.8 * last2; ph -= std::floor (ph); }
            o1 = wave (ph, f1, s1 * 3, pw1);
        }
        v.p2 += f2; if (v.p2 >= 1) v.p2 -= 1;
        if (sync && wrap) v.p2 = 0;
        double o2;
        if (hyper2)
        {
            double sum = 0;
            for (int q = 0; q < 7; ++q) { double hh = v.hp2[q] + df2[q]; if (hh >= 1) hh -= 1; v.hp2[q] = hh; sum += (2 * hh - 1 - blep (hh, df2[q])) * hg2[q]; }
            o2 = sum;
        }
        else if (wt2) o2 = wtRead (WTb, s2, mp2, v.p2, pw2);
        else o2 = wave (v.p2, f2, s2 * 3, pw2);
        last2 = o2;
        v.sp += f1 * 0.5; if (v.sp >= 1) v.sp -= 1;
        const double sb = v.sp < 0.5 ? 0.7 : -0.7, nz = rnd01() * 2 - 1;
        double x = o1 * ca + o2 * cb;
        if (ring) x = x * 0.25 + o1 * o2 * 1.2;
        x = (x + sb * sub + nz * noi * 0.8) * 0.7;
        const double y1 = std::tanh (svf (x, fs, 0, m1, k, a11, a12, a13) * sat); double l, r;
        if (rt == 0)
        {
            const double y2 = std::tanh (svf (y1, fs, 2, m2, k, a21, a22, a23) * sat);
            l = r = y1 + (y2 - y1) * fb;
        }
        else
        {
            const double y2 = std::tanh (svf (x, fs, 2, m2, k, a21, a22, a23) * sat);
            if (rt == 1) l = r = y1 + (y2 - y1) * fb; else { l = y1; r = y2; }
        }
        const double ag = gp + (gt - gp) * ((i + 1.0) / len);
        L[s0 + i] = (float) ((double) L[s0 + i] + l * ag * pl);
        R[s0 + i] = (float) ((double) R[s0 + i] + r * ag * pr);
    }
    v.last2 = last2; v.gPrev = gt;
}

int Core::render (float* L, float* R, int N, const float* mod)
{
    if (idle > idleMax) return 0;
    derive(); std::fill (fxs, fxs + 8, 0.0); fxn = 0;
    int act = 0;
    const int lsI[3] = { O_l1s, O_l2s, O_l3s };
    for (int s0 = 0; s0 < N; s0 += 8)
    {
        const int len = std::min (8, N - s0);
        for (int i = 0; i < 3; ++i)
        {
            const int ls = O[lsI[i]]; const double r = ls ? G.bpm / 60 / LFO_BEATS[ls] : G.lfoRate[i];
            gph[i] = std::fmod (gph[i] + r * len / sr, 1.0);
        }
        arpAdvance (len);
        for (int k = 0; k < 16; ++k) { if (V[k].on) block (V[k], L, R, s0, len); }
    }
    for (int k = 0; k < 16; ++k) if (V[k].on) act++;
    if (mod && act > 0 && G.vocM > 0) vocode (L, R, N, mod);
    bool fmod = false;
    for (int q = 0; q < 8; ++q) { const double m = fxn ? fxs[q] / fxn : 0; fxm[q] = m; if (q < 6 && std::fabs (m) > 0.001) fmod = true; }
    const bool fxOn = G.fxOn || fmod;
    if (fxOn) fx (L, R, N);
    if (act || fxOn) { const double mg = G.master; for (int i = 0; i < N; ++i) { L[i] = (float) std::tanh ((double) L[i] * mg); R[i] = (float) std::tanh ((double) R[i] * mg); } }
    if (act || A.running) idle = 0; else idle += N;
    return act;
}
} // namespace yb
