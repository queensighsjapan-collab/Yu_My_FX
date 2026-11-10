// RANDOMIZE / MUTATE / BACK / LOCK - C++ port of the HTML version's randomizer (genPatch / mutatePatch / applyLocks).
#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include "Engine.h"
#include "Tables.inc"

namespace yb
{
using juce::String;
struct PatchState
{
    int k[NUM_P] {};
    int o[NUM_O] {};
    int ld[3] {};
    int msrc[6] {};
    int md[6][3] {};
    int ma[6][3] {};           // -100..100
    juce::String name;
};

namespace rnd
{
enum Src { S_OFF, S_L1, S_L2, S_L3, S_FE, S_AE, S_VEL, S_MW, S_KEY, S_RND, S_S1, S_S2, S_AT, S_PB, S_L1U, S_L2U, S_L3U };
enum Dst { D_OFF, D_O1P, D_O2P, D_O12P, D_O1S, D_O2S, D_PW, D_FM, D_BAL, D_SUB, D_C1, D_C2, D_C12, D_RES, D_FB, D_AMP, D_PAN,
           D_L1R, D_L2R, D_L3R, D_SAT, D_NZ, D_DS, D_RS, D_CM, D_DI, D_PH, D_FV };

inline int pIdx (const char* n)
{
    for (int i = 0; i < NUM_P; ++i) if (std::strcmp (kParamNames[i], n) == 0) return i;
    jassertfalse; return 0;
}
inline int oIdx (const char* n)
{
    for (int i = 0; i < NUM_O; ++i) if (std::strcmp (kOptNames[i], n) == 0) return i;
    jassertfalse; return 0;
}

struct Rng
{
    juce::Random r;
    double rr (double a, double b) { return a + (double) r.nextDouble() * (b - a); }
    int ri (double a, double b) { return (int) std::floor (rr (a, b) + 0.5); }
    template <typename T> T pick (std::initializer_list<T> l) { return *(l.begin() + r.nextInt ((int) l.size())); }
    template <typename T> T pickV (const std::vector<T>& l) { return l[(size_t) r.nextInt ((int) l.size())]; }
    double gauss() { return ((double) r.nextDouble() + (double) r.nextDouble() + (double) r.nextDouble() - 1.5) * 2.0; }
    bool chance (double p) { return (double) r.nextDouble() < p; }
};

inline int c127 (double v) { return (int) juce::jlimit (0.0, 127.0, std::floor (v + 0.5)); }

inline PatchState fromFactory (int idx)
{
    PatchState s; const auto& f = kFactory[juce::jlimit (0, YB_NUM_FACTORY - 1, idx)];
    for (int i = 0; i < NUM_P; ++i) s.k[i] = f.k[i];
    for (int i = 0; i < NUM_O; ++i) s.o[i] = f.o[i];
    for (int i = 0; i < 3; ++i) s.ld[i] = f.ld[i];
    for (int m = 0; m < 6; ++m)
    {
        s.msrc[m] = f.mx[m][0];
        for (int j = 0; j < 3; ++j) { s.md[m][j] = f.mx[m][1 + j]; s.ma[m][j] = f.mx[m][4 + j]; }
    }
    s.name = f.name;
    return s;
}

inline const std::vector<const char*>& vocNames()
{
    static const std::vector<const char*> v { "Vocoder Mix", "Voc Input", "Voc Release", "Voc Q", "Voc Shift", "Voc Noise", "Voc Tilt", "Voc Level" };
    return v;
}

struct Slot { int src = 0; int n = 0; int d[3] {}; int a[3] {}; };

inline Slot genMxSlot (Rng& R, bool fx, double t)
{
    const std::vector<int> srcs { S_L1, S_L2, S_L3, S_FE, S_AE, S_VEL, S_MW, S_KEY, S_RND, S_S1, S_S2, S_AT };
    const std::vector<int> dpFx { D_C12, D_O1S, D_O2S, D_PW, D_FM, D_BAL, D_RES, D_PAN, D_L1R, D_L2R, D_SAT, D_NZ, D_DS, D_RS, D_PH, D_FV, D_O12P, D_O2P };
    const std::vector<int> dpN { D_C12, D_C1, D_O1S, D_O2S, D_PW, D_FM, D_BAL, D_RES, D_PAN, D_SAT, D_DS, D_RS, D_CM, D_PH, D_FV, D_SUB };
    const auto& dp = fx ? dpFx : dpN;
    Slot s; s.src = R.pickV (srcs);
    bool used[40] {};
    const int nd = 1 + (int) std::floor ((double) R.r.nextDouble() * (1.0 + t * 2.0));
    for (int j = 0; j < nd; ++j)
    {
        const int d = R.pickV (dp);
        if (used[d]) continue;
        used[d] = true;
        int a = (R.chance (0.5) ? -1 : 1) * R.ri (8, 22 + t * 60);
        const int sg = a < 0 ? -1 : 1;
        if (d == D_O1P || d == D_O2P || d == D_O12P) a = sg * std::min (std::abs (a), fx ? 30 : 6);
        if (d == D_RES) a = sg * std::min (std::abs (a), 40);
        if (s.n < 3) { s.d[s.n] = d; s.a[s.n] = a; ++s.n; }
    }
    return s;
}

inline void putSlot (PatchState& st, int idx, const Slot& s)
{
    st.msrc[idx] = s.src;
    for (int j = 0; j < 3; ++j) { st.md[idx][j] = j < s.n ? s.d[j] : 0; st.ma[idx][j] = j < s.n ? s.a[j] : 0; }
}

// cat: 0 ANY, 1 LEAD, 2 BASS, 3 PAD, 4 PLUCK, 5 ARP, 6 FX
inline PatchState genPatch (Rng& R, int cat, int amt, const PatchState& cur, int& counter)
{
    static const char* catNames[] = { "ANY", "LEAD", "BASS", "PAD", "PLUCK", "ARP", "FX" };
    const double t = juce::jlimit (0.0, 1.0, amt / 100.0);
    if (cat <= 0) cat = 1 + R.r.nextInt (6);
    PatchState st = fromFactory (0);
    auto set = [&] (const char* n, double v) { st.k[pIdx (n)] = c127 (v); };
    auto setO = [&] (const char* n, int v) { st.o[oIdx (n)] = v; };
    for (auto n : vocNames()) st.k[pIdx (n)] = cur.k[pIdx (n)];
    set ("Osc1 Shape", R.ri (30, 127)); set ("Osc2 Shape", R.ri (30, 127));
    setO ("osc1mode", R.pick ({ 0, 0, 0, 1, 2 })); setO ("osc2mode", R.pick ({ 0, 0, 1, 2 }));
    set ("Osc2 Detune", R.ri (3, 22) + t * 10);
    set ("Osc Balance", R.ri (35, 90));
    if (cat == 1)
    {
        set ("Osc2 Semi", R.pick ({ 64, 64, 64, 71, 76, 52 })); set ("Cutoff", R.ri (65, 105)); set ("Resonance", R.ri (5, 45 + t * 20));
        set ("Filter Env Amt", R.ri (66, 100)); set ("Flt Decay", R.ri (40, 85)); set ("Flt Sustain", R.ri (30, 100));
        set ("Amp Attack", R.ri (0, 25)); set ("Amp Sustain", R.ri (95, 127)); set ("Amp Release", R.ri (40, 75));
        set ("Reverb Send", R.ri (10, 50)); set ("Delay Send", R.ri (0, 40)); set ("Delay Feedback", R.ri (30, 60));
        setO ("mono", R.pick ({ 0, 1, 1 })); setO ("uni", R.pick ({ 0, 0, 2, 3, 4 }));
        { const int a = R.ri (10, 50); const int c = R.pick ({ 0, 0, 1 }); set ("Portamento", c == 1 ? a : 0); }
    }
    else if (cat == 2)
    {
        set ("Osc1 Semi", 52); set ("Osc2 Semi", R.pick ({ 52, 52, 40, 64 })); set ("Osc2 Detune", R.ri (2, 14)); set ("Sub Osc Vol", R.ri (30, 100));
        set ("Cutoff", R.ri (25, 70)); set ("Resonance", R.ri (15, 70)); set ("Filter Env Amt", R.ri (80, 120)); set ("Flt Decay", R.ri (35, 80)); set ("Flt Sustain", R.ri (0, 60));
        set ("Amp Sustain", R.ri (85, 127)); set ("Amp Release", R.ri (20, 50)); set ("Distortion", R.ri (0, 10 + 60 * t));
        setO ("mono", 1); setO ("uni", R.pick ({ 0, 0, 1 }));
        { const int a = R.ri (20, 60); const int c = R.pick ({ 0, 0, 1 }); set ("Portamento", c == 1 ? a : 0); }
    }
    else if (cat == 3)
    {
        set ("Osc2 Semi", R.pick ({ 64, 64, 71, 76 })); set ("Cutoff", R.ri (45, 90)); set ("Resonance", R.ri (0, 35));
        set ("Flt Attack", R.ri (30, 100)); set ("Flt Decay", R.ri (60, 100)); set ("Filter Env Amt", R.ri (64, 100));
        set ("Amp Attack", R.ri (60, 110)); set ("Amp Sustain", R.ri (105, 127)); set ("Amp Release", R.ri (80, 115));
        set ("Chorus Mix", R.ri (30, 90)); set ("Reverb Send", R.ri (40, 90)); set ("Phaser Mix", R.ri (0, 50)); set ("Phaser Rate", R.ri (10, 60));
        setO ("uni", R.pick ({ 0, 2, 4, 6 })); setO ("rsize", R.pick ({ 1, 2 }));
    }
    else if (cat == 4 || cat == 5)
    {
        set ("Osc2 Semi", R.pick ({ 64, 64, 76, 71 })); set ("Amp Decay", R.ri (40, 85)); set ("Amp Sustain", 0); set ("Flt Sustain", 0);
        set ("Flt Decay", R.ri (40, 80)); set ("Filter Env Amt", R.ri (75, 115)); set ("Cutoff", R.ri (35, 80)); set ("Resonance", R.ri (10, 50)); set ("Amp Release", R.ri (25, 55));
        set ("Delay Send", R.ri (15, 50)); set ("Delay Feedback", R.ri (30, 60)); set ("Reverb Send", R.ri (10, 40));
        if (cat == 5)
        {
            setO ("arp", 1); setO ("amode", R.pick ({ 0, 1, 2, 3, 4, 5 })); setO ("ares", R.pick ({ 1, 3, 3, 4 })); setO ("alen", R.pick ({ 0, 1, 2 }));
            setO ("apat", R.pick ({ 0, 1, 2, 3, 5 }));
            set ("Arp Tempo", R.ri (50, 90)); set ("Arp Range", R.ri (16, 70)); setO ("dsync", R.pick ({ 0, 2, 4 }));
        }
    }
    else
    {
        set ("Osc2 Semi", R.ri (40, 100)); set ("Noise Vol", R.ri (0, 100)); set ("FM Amount", R.ri (0, 10 + 70 * t));
        set ("Cutoff", R.ri (35, 110)); set ("Resonance", R.ri (10, 60)); set ("Amp Attack", R.ri (0, 90)); set ("Amp Sustain", R.ri (60, 127)); set ("Amp Release", R.ri (40, 100));
        set ("Ring Mix", R.ri (0, 20 + 80 * t)); set ("Ring Freq", R.ri (10, 120)); set ("Formant Mix", R.ri (0, 70 * t)); set ("Formant Vowel", R.ri (0, 127));
        set ("Delay Send", R.ri (10, 60)); set ("Delay Feedback", R.ri (40, 80)); set ("Reverb Send", R.ri (20, 80));
        setO ("f1mode", R.pick ({ 0, 0, 1, 2 })); setO ("sync", R.chance (0.4) ? 1 : 0);
    }
    if (cat != 6)
    {
        set ("Ring Mix", R.chance (t * 0.3) ? R.ri (5, 40) : 0);
        set ("Formant Mix", R.chance (t * 0.3) ? R.ri (10, 60) : 0);
        set ("FM Amount", R.chance (t * 0.5) ? R.ri (3, 40 * t + 5) : 0);
    }
    const std::vector<int> ldD { D_C12, D_O1S, D_O2S, D_PW, D_PAN, D_RES, D_FM };
    for (int i = 0; i < 3; ++i)
        if (R.chance (0.25 + t * 0.6))
        {
            const String p = "LFO" + String (i + 1);
            set ((p + " Rate").toRawUTF8(), R.ri (15, 105)); set ((p + " Shape").toRawUTF8(), R.ri (0, 100)); set ((p + " Amount").toRawUTF8(), R.ri (8, 25 + t * 70));
            st.ld[i] = R.pickV (ldD);
        }
    const int n = 1 + (int) std::floor (t * 5 + 0.5);
    for (int i = 0; i < 6; ++i) { Slot z; putSlot (st, i, z); }
    for (int i = 0; i < n && i < 6; ++i) putSlot (st, i, genMxSlot (R, cat == 6, t));
    st.name = (String (catNames[cat]) + " RND " + String (++counter).paddedLeft ('0', 2)).substring (0, 18);
    return st;
}

inline PatchState mutatePatch (Rng& R, int amt, const PatchState& cur)
{
    const double t = juce::jlimit (0.0, 1.0, amt / 100.0);
    PatchState st = cur;
    std::vector<int> skip;
    for (auto n : { "Master Volume", "Param", "Value", "Transpose", "Bend Range", "Arp Tempo", "Init Phase" }) skip.push_back (pIdx (n));
    for (auto n : vocNames()) skip.push_back (pIdx (n));
    for (int i = 0; i < NUM_P; ++i)
    {
        if (std::find (skip.begin(), skip.end(), i) != skip.end()) continue;
        if (R.chance (0.15 + t * 0.5)) st.k[i] = c127 (st.k[i] + R.gauss() * (6 + t * 34));
    }
    { const int c = pIdx ("Cutoff"); st.k[c] = std::max (st.k[c], 25); }
    if (R.chance (t * 0.4)) st.o[oIdx ("osc1mode")] = R.pick ({ 0, 1, 2 });
    if (R.chance (t * 0.4)) st.o[oIdx ("osc2mode")] = R.pick ({ 0, 1, 2 });
    if (R.chance (t * 0.3))
        for (int i = 0; i < 3; ++i) if (R.chance (0.5)) st.ld[i] = R.pick ({ (int) D_OFF, (int) D_C12, (int) D_O1S, (int) D_PW, (int) D_PAN, (int) D_RES });
    for (int m = 0; m < 6; ++m)
        for (int j = 0; j < 3; ++j)
            st.ma[m][j] = juce::jlimit (-100, 100, (int) std::floor (st.ma[m][j] + (st.md[m][j] ? R.gauss() * (5 + t * 35) : 0.0) + 0.5));
    if (R.chance (t * 0.7))
        for (int m = 0; m < 6; ++m) if (st.msrc[m] == 0) { putSlot (st, m, genMxSlot (R, false, t)); break; }
    String base = cur.name;
    if (base.endsWith (" MUT")) base = base.dropLastCharacters (4);
    st.name = (base + " MUT").substring (0, 18);
    return st;
}

// lock groups: 0 OSC, 1 FILT, 2 ENV, 3 FX, 4 MOD, 5 ARP
inline const char* const* lockNames() { static const char* n[] = { "OSC", "FILT", "ENV", "FX", "MOD", "ARP" }; return n; }

inline void applyLocks (PatchState& st, const PatchState& cur, const bool locked[6])
{
    auto starts = [] (const char* name, std::initializer_list<const char*> pre)
    {
        for (auto p : pre) if (std::strncmp (name, p, std::strlen (p)) == 0) return true;
        return false;
    };
    for (int i = 0; i < NUM_P; ++i)
    {
        const char* n = kParamNames[i];
        if ((locked[0] && starts (n, { "Osc", "Sub Osc", "Noise", "FM Amount", "Init Phase", "Portamento", "Unison" }))
         || (locked[1] && starts (n, { "Cutoff", "Resonance", "Filter" }))
         || (locked[2] && starts (n, { "Flt ", "Amp " }))
         || (locked[3] && starts (n, { "Phaser", "Ring", "EQ", "Formant", "Delay", "Reverb", "Chorus", "Distortion" }))
         || (locked[4] && starts (n, { "LFO" }))
         || (locked[5] && starts (n, { "Arp" })))
            st.k[i] = cur.k[i];
    }
    auto keep = [&] (std::initializer_list<const char*> l) { for (auto n : l) st.o[oIdx (n)] = cur.o[oIdx (n)]; };
    if (locked[0]) keep ({ "uni", "osc2mode", "osc1mode", "sync", "ring", "mono" });
    if (locked[1]) keep ({ "f1mode", "f2mode", "routing" });
    if (locked[3]) keep ({ "cmode", "dsync", "rsize" });
    if (locked[4])
    {
        keep ({ "l1s", "l2s", "l3s", "l1k", "l2k", "l3k" });
        std::memcpy (st.msrc, cur.msrc, sizeof st.msrc); std::memcpy (st.md, cur.md, sizeof st.md); std::memcpy (st.ma, cur.ma, sizeof st.ma);
        std::memcpy (st.ld, cur.ld, sizeof st.ld);
    }
    if (locked[5]) keep ({ "arp", "ahold", "amode", "ares", "alen", "apat" });
}
} // namespace rnd
} // namespace yb
