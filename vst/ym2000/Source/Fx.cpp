#include "Fx.h"
#include <algorithm>

namespace ym
{
namespace
{
constexpr double PI = 3.14159265358979323846;
inline double clampd (double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline double dbToLin (double db) { return std::pow (10.0, db / 20.0); }
inline double linToDb (double x) { return 20.0 * std::log10 (x); }
inline double smoothA (double sr, double tau) { return 1.0 - std::exp (-1.0 / (sr * tau)); }
inline void panGains (double pan, double& l, double& r)
{
    const double x = (pan + 1) * 0.5; // mono-input equal-power panner, like StereoPannerNode
    l = std::cos (x * PI * 0.5); r = std::sin (x * PI * 0.5);
}
inline double readLin (const std::vector<double>& b, int wp, double d)
{
    const int size = (int) b.size();
    double r = wp - d;
    while (r < 0) r += size;
    int i0 = (int) r; const double f = r - i0;
    if (i0 >= size) i0 -= size;
    int i1 = i0 + 1; if (i1 >= size) i1 = 0;
    return b[(size_t) i0] * (1 - f) + b[(size_t) i1] * f;
}
} // namespace

// ---------------------------------------------------------------- Biquad
void Biquad::setBandpass (double f, double q, double sr)
{
    f = clampd (f, 1, sr * 0.499);
    const double w0 = 2 * PI * f / sr, al = std::sin (w0) / (2 * q), c = std::cos (w0);
    norm (1 + al, al, 0, -al, -2 * c, 1 - al);
}
void Biquad::setLowpassDb (double f, double qDb, double sr)
{
    f = clampd (f, 1, sr * 0.499);
    const double w0 = 2 * PI * f / sr, al = std::sin (w0) / (2 * std::pow (10.0, qDb / 20)), c = std::cos (w0);
    norm (1 + al, (1 - c) / 2, 1 - c, (1 - c) / 2, -2 * c, 1 - al);
}
void Biquad::setHighpassDb (double f, double qDb, double sr)
{
    f = clampd (f, 1, sr * 0.499);
    const double w0 = 2 * PI * f / sr, al = std::sin (w0) / (2 * std::pow (10.0, qDb / 20)), c = std::cos (w0);
    norm (1 + al, (1 + c) / 2, -(1 + c), (1 + c) / 2, -2 * c, 1 - al);
}
void Biquad::setAllpass (double f, double q, double sr)
{
    f = clampd (f, 1, sr * 0.499);
    const double w0 = 2 * PI * f / sr, al = std::sin (w0) / (2 * q), c = std::cos (w0);
    norm (1 + al, 1 - al, -2 * c, 1 + al, -2 * c, 1 - al);
}
void Biquad::setLowShelf (double f, double gainDb, double sr)
{
    f = clampd (f, 1, sr * 0.499);
    const double A = std::pow (10.0, gainDb / 40), w0 = 2 * PI * f / sr, c = std::cos (w0);
    const double al = std::sin (w0) / 2 * std::sqrt (2.0); // S = 1
    const double s = 2 * std::sqrt (A) * al;
    norm ((A + 1) + (A - 1) * c + s,
          A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s),
          -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
}
void Biquad::setHighShelf (double f, double gainDb, double sr)
{
    f = clampd (f, 1, sr * 0.499);
    const double A = std::pow (10.0, gainDb / 40), w0 = 2 * PI * f / sr, c = std::cos (w0);
    const double al = std::sin (w0) / 2 * std::sqrt (2.0);
    const double s = 2 * std::sqrt (A) * al;
    norm ((A + 1) - (A - 1) * c + s,
          A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
          2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
}

// ---------------------------------------------------------------- Limiter (Web Audio DynamicsCompressor-like)
double Limiter::kneeCurve (double x, double kk) const
{
    if (x < linThr) return x;
    return linThr + (1 - std::exp (-kk * (x - linThr))) / kk;
}
double Limiter::saturate (double x) const
{
    if (x < kneeThr) return kneeCurve (x, k);
    const double xDb = linToDb (x);
    const double yDb = yKneeDb + slope * (xDb - linToDb (kneeThr));
    return dbToLin (yDb);
}
void Limiter::prepare (double sampleRate)
{
    sr = sampleRate;
    const double dbThr = -8, dbKnee = 6, ratio = 12;
    linThr = dbToLin (dbThr);
    kneeThr = dbToLin (dbThr + dbKnee);
    slope = 1.0 / ratio;
    auto slopeAt = [&] (double x, double kk)
    {
        if (x < linThr) return 1.0;
        const double x2 = x * 1.001;
        const double xDb = linToDb (x), x2Db = linToDb (x2);
        const double yDb = linToDb (kneeCurve (x, kk)), y2Db = linToDb (kneeCurve (x2, kk));
        return (y2Db - yDb) / (x2Db - xDb);
    };
    {
        const double x = dbToLin (dbThr + dbKnee);
        double minK = 0.1, maxK = 10000;
        k = 5;
        for (int i = 0; i < 15; ++i)
        {
            if (slopeAt (x, k) < slope) maxK = k; else minK = k;
            k = std::sqrt (minK * maxK);
        }
    }
    yKneeDb = linToDb (kneeCurve (kneeThr, k));
    makeup = std::pow (1.0 / saturate (1.0), 0.6);
    // Constants tuned against Chrome's DynamicsCompressorNode (average error about 2 % on loud material)
    aAtt = smoothA (sr, 0.003);
    aRel = smoothA (sr, 0.05);
    pre = std::max (1, (int) std::lround (0.006 * sr)); // output latency (look-ahead delay)
    win = std::max (1, (int) std::lround (0.003 * sr)); // sliding-minimum window
    bufL.assign ((size_t) pre, 0.0f); bufR.assign ((size_t) pre, 0.0f);
    pos = 0; g = 1;
    dqVal.assign ((size_t) pre + 2, 1.0); dqIdx.assign ((size_t) pre + 2, 0);
    dqHead = 0; dqCount = 0; nIn = 0;
}
void Limiter::process (float* L, float* R, int n)
{
    // Look-ahead limiter: the gain follows the minimum of the required gain over the look-ahead window,
    // so peaks are already attenuated when the (delayed) audio arrives. Sliding minimum via monotonic deque.
    const int cap = (int) dqVal.size();
    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R[i];
        const double x = std::max (std::fabs (inL), std::fabs (inR));
        const double target = x > linThr ? saturate (x) / x : 1.0;
        // push
        while (dqCount > 0)
        {
            const int back = (dqHead + dqCount - 1) % cap;
            if (dqVal[(size_t) back] >= target) --dqCount; else break;
        }
        const int tail = (dqHead + dqCount) % cap;
        dqVal[(size_t) tail] = target; dqIdx[(size_t) tail] = nIn; ++dqCount;
        while (dqIdx[(size_t) dqHead] <= nIn - win) { dqHead = (dqHead + 1) % cap; --dqCount; }
        ++nIn;
        const double gmin = dqVal[(size_t) dqHead];
        g += (gmin - g) * (gmin < g ? aAtt : aRel);
        const double oL = bufL[(size_t) pos], oR = bufR[(size_t) pos];
        bufL[(size_t) pos] = (float) inL; bufR[(size_t) pos] = (float) inR;
        if (++pos >= pre) pos = 0;
        L[i] = (float) (oL * g * makeup); R[i] = (float) (oR * g * makeup);
    }
}

// ---------------------------------------------------------------- FxChain
void FxChain::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    aS02 = smoothA (sr, 0.02); aS03 = smoothA (sr, 0.03);
    for (int i = 0; i < 16; ++i)
    {
        auto& b = bands[(size_t) i];
        b.f = 120 * std::pow (7000.0 / 120.0, i / 15.0);
        b.mb.setBandpass (b.f, 7, sr);
        b.lp.setLowpassDb (40, 0.5, sr);
        b.cb.setBandpass (b.f, 7, sr);
    }
    modShelf.setHighShelf (1200, 8, sr);
    clarHp.setHighpassDb (4500, 1, sr);
    const double base[5] = { 0.007, 0.007, 0.011, 0.015, 0.019 };
    const double rm[5] = { 1, 1, 1, 1.27, 0.83 };
    const double sg[5] = { 1, -1, 1, -1, 1 };
    const double dp[5] = { 0.0035, 0.0035, 0.003, 0.003, 0.003 };
    const double pn[5] = { -1, 1, -0.8, 0, 0.8 };
    for (int i = 0; i < 5; ++i)
    {
        auto& t = taps[(size_t) i];
        t.base = base[i]; t.rm = rm[i]; t.sign = sg[i]; t.depth = dp[i];
        panGains (pn[i], t.panL, t.panR);
    }
    taps[5].base = 0; taps[5].rm = 1; taps[5].sign = 1; taps[5].depth = 450; taps[5].panL = taps[5].panR = 0;
    mfBuf.assign ((size_t) (0.12 * sr) + 8, 0.0);
    dBufL.assign ((size_t) (1.3 * sr) + 8, 0.0);
    dBufR.assign ((size_t) (1.3 * sr) + 8, 0.0);
    lim.prepare (sr);
    tmpL.assign ((size_t) std::max (maxBlock, 1), 0.0f);
    tmpR.assign ((size_t) std::max (maxBlock, 1), 0.0f);
    first = true;
    reset();
}

void FxChain::reset()
{
    for (auto& b : bands) { b.mb.reset(); b.lp.reset(); b.cb.reset(); }
    modShelf.reset(); clarHp.reset();
    std::fill (mfBuf.begin(), mfBuf.end(), 0.0); mfPos = 0;
    for (auto& a : ap) a.reset();
    for (auto& t : taps) t.ph = 0;
    std::fill (dBufL.begin(), dBufL.end(), 0.0); std::fill (dBufR.begin(), dBufR.end(), 0.0); dPos = 0;
    for (int i = 0; i < 2; ++i) { eqL[i].reset(); eqH[i].reset(); }
    lim.prepare (sr);
    first = true;
}

void FxChain::setParams (const FxParams& p, int n)
{
    cur = p;
    const double d = p.mfd / 127;
    gMaster.set (std::pow (p.vol / 127, 1.5));
    gAi1.set (p.ai1 / 127 * 4); gAi2.set (p.ai2 / 127 * 4);
    gDry.set (p.vmode == 0 ? 1 : 0); gVoc.set (p.vmode == 1 ? 1 : 0);
    gClar.set (p.vcl / 127 * 2);
    vfsS.set (p.vfs);
    for (int i = 0; i < 16; ++i)
    {
        auto& b = bands[(size_t) i];
        b.lv.set (p.vlev[(size_t) i] / 127 * 1.4);
        double l, r; panGains ((p.vpan[(size_t) i] - 64) / 64, l, r);
        b.pl.set (l); b.pr.set (r);
    }
    gc.set (p.mft == 0 ? 1 : 0); ge.set (p.mft == 1 ? 1 : 0); gp.set (p.mft == 2 ? 1 : 0);
    wet.set (0.65 * std::min (1.0, d * 3));
    const double rate = 0.1 * std::pow (2.0, p.mfs / 127 * 6.3);
    for (auto& t : taps) { t.rate.set (rate * t.rm); t.gain.set (d * t.depth * t.sign); }
    const bool cross = p.dt == 1;
    const double dd = p.ddp / 127, T = 0.02 * std::pow (2.0, p.dtm / 127 * 5.6), fb = dd * 0.6;
    dInL.set (cross ? 0 : 1); dInR.set (cross ? 0 : 1); dMono.set (cross ? 0.5 : 0);
    fLL.set (cross ? 0 : fb); fRR.set (cross ? 0 : fb); fLR.set (cross ? fb : 0); fRL.set (cross ? fb : 0);
    tL.set (T); tR.set (p.dt == 2 ? T * 0.66 : T);
    const double w = std::min (1.0, dd * 2.5) * 0.7;
    wL.set (w); wR.set (w);

    if (first)
    {
        for (Smooth* s : { &gMaster, &gDry, &gVoc, &gAi1, &gAi2, &gClar, &vfsS, &gc, &ge, &gp, &wet,
                           &dInL, &dInR, &dMono, &fLL, &fRR, &fLR, &fRL, &tL, &tR, &wL, &wR })
            s->jump (s->t);
        for (auto& b : bands) { b.lv.jump (b.lv.t); b.pl.jump (b.pl.t); b.pr.jump (b.pr.t); }
        for (auto& t : taps) { t.rate.jump (t.rate.t); t.gain.jump (t.gain.t); }
        eqLfS = 40 * std::pow (2.0, p.eqlf / 127 * 4.64); eqHfS = 1000 * std::pow (2.0, p.eqhf / 127 * 4.17);
        eqLgS = p.eqlg; eqHgS = p.eqhg;
        vfsApplied = 1e9;
        first = false;
    }

    // block-rate smoothing for the parameters that need filter coefficient updates
    const double a = 1.0 - std::exp (-(double) n / (sr * 0.02));
    vfsS.v += (vfsS.t - vfsS.v) * a;
    if (std::fabs (vfsS.v - vfsApplied) > 1e-4)
    {
        vfsApplied = vfsS.v;
        for (auto& b : bands) b.cb.setBandpass (b.f * std::pow (2.0, vfsS.v * 0.25), 7, sr);
    }
    eqLfS += (40 * std::pow (2.0, p.eqlf / 127 * 4.64) - eqLfS) * a;
    eqHfS += (1000 * std::pow (2.0, p.eqhf / 127 * 4.17) - eqHfS) * a;
    eqLgS += (p.eqlg - eqLgS) * a; eqHgS += (p.eqhg - eqHgS) * a;
    for (int i = 0; i < 2; ++i)
    {
        // keep each channel's state, only the coefficients change
        Biquad t;
        t.setLowShelf (eqLfS, eqLgS, sr);
        eqL[i].b0 = t.b0; eqL[i].b1 = t.b1; eqL[i].b2 = t.b2; eqL[i].a1 = t.a1; eqL[i].a2 = t.a2;
        t.setHighShelf (eqHfS, eqHgS, sr);
        eqH[i].b0 = t.b0; eqH[i].b1 = t.b1; eqH[i].b2 = t.b2; eqH[i].a1 = t.a1; eqH[i].a2 = t.a2;
    }
}

void FxChain::engineInput (const float* src, float* dst, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const double g = gAi2.next (aS02);
        dst[i] = src ? (float) (src[i] * g) : 0.0f;
    }
}

void FxChain::process (const float* synthL, const float* synthR, const float* srcMono, float* outL, float* outR, int n)
{
    const bool vocActive = gVoc.v > 1e-5 || gVoc.t > 0;
    const bool phaseActive = gp.v > 1e-5 || gp.t > 0;
    const double sr2 = sr;
    if (! vocActive)
    {
        for (auto& b : bands) { b.lv.jump (b.lv.t); b.pl.jump (b.pl.t); b.pr.jump (b.pr.t); }
        gAi1.jump (gAi1.t); gClar.jump (gClar.t);
    }
    const int dSize = (int) dBufL.size();

    for (int i = 0; i < n; ++i)
    {
        const double sL = synthL[i], sR = synthR[i];
        const double gd = gDry.next (aS02), gv = gVoc.next (aS02);
        double xL = gd * sL, xR = gd * sR;

        // ---- 16-band vocoder (carrier = synth, modulator = audio input)
        if (vocActive)
        {
            const double src = srcMono ? srcMono[i] : 0.0;
            const double m = modShelf.process (src * gAi1.next (aS02));
            const double car = 0.5 * (sL + sR);
            double sumL = 0, sumR = 0;
            for (auto& b : bands)
            {
                double e = std::min (std::fabs (b.mb.process (m)), 1.0); // full-wave rectifier (WaveShaper curve [1,0,1])
                e = b.lp.process (e) * 5.0;
                const double y = b.cb.process (car) * e * b.lv.next (aS02);
                sumL += y * b.pl.next (aS02); sumR += y * b.pr.next (aS02);
            }
            const double clar = clarHp.process (m) * gClar.next (aS02);
            xL += gv * (sumL * 3.5 + clar); xR += gv * (sumR * 3.5 + clar);
        }

        // ---- MOD FX
        const double mono = 0.5 * (xL + xR);
        mfBuf[(size_t) mfPos] = mono;
        const double g0 = gc.next (aS02), g1 = ge.next (aS02), g2 = gp.next (aS02), w = wet.next (aS02);
        double wL_ = 0, wR_ = 0;
        for (int k = 0; k < 6; ++k)
        {
            auto& t = taps[(size_t) k];
            const double rate = t.rate.next (aS02), gain = t.gain.next (aS02);
            t.ph += rate / sr2; if (t.ph >= 1) t.ph -= 1;
            const double lfo = std::sin (2 * PI * t.ph);
            if (k < 5)
            {
                const double dsec = clampd (t.base + lfo * gain, 0, 0.1);
                const double y = readLin (mfBuf, mfPos, dsec * sr2);
                const double gate = k < 2 ? g0 : g1;
                wL_ += y * t.panL * gate; wR_ += y * t.panR * gate;
            }
            else if (phaseActive)
            {
                double y = mono;
                for (int s = 0; s < 4; ++s)
                {
                    ap[(size_t) s].setAllpass (apBase[(size_t) s] + lfo * gain, 0.8, sr2);
                    y = ap[(size_t) s].process (y);
                }
                wL_ += y * g2; wR_ += y * g2;
            }
        }
        if (++mfPos >= (int) mfBuf.size()) mfPos = 0;
        xL += w * wL_; xR += w * wR_;

        // ---- DELAY FX
        {
            const double iL = dInL.next (aS03), iR = dInR.next (aS03), iM = dMono.next (aS03);
            const double fll = fLL.next (aS03), frr = fRR.next (aS03), flr = fLR.next (aS03), frl = fRL.next (aS03);
            const double tl = tL.next (aS03), tr = tR.next (aS03), wl = wL.next (aS03), wr = wR.next (aS03);
            const double yL = readLin (dBufL, dPos, tl * sr2), yR = readLin (dBufR, dPos, tr * sr2);
            // Chrome adds one render quantum (128 samples) to feedback paths that close a cycle of DelayNodes
            // (self-feedback and L->R); the R->L hop adds none. Reproduced so the sound matches the HTML version.
            const double yLq = readLin (dBufL, dPos, tl * sr2 + 128), yRq = readLin (dBufR, dPos, tr * sr2 + 128);
            dBufL[(size_t) dPos] = iL * xL + iM * (xL + xR) + fll * yLq + frl * yR;
            dBufR[(size_t) dPos] = iR * xR + frr * yRq + flr * yLq;
            if (++dPos >= dSize) dPos = 0;
            xL += wl * yL; xR += wr * yR;
        }

        // ---- EQ and master
        const double mg = gMaster.next (aS02);
        outL[i] = (float) (eqH[0].process (eqL[0].process (xL)) * mg);
        outR[i] = (float) (eqH[1].process (eqL[1].process (xR)) * mg);
    }
    lim.process (outL, outR, n);
}
} // namespace ym
