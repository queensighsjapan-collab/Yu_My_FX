// YM2000 effect chain: C++ re-implementation of the Web Audio graph in buildChain() of ms2000-synth.html
//   synth -> [dry | 16-band vocoder] -> MOD FX -> DELAY FX -> EQ -> master -> limiter
// No JUCE dependency.
#pragma once
#include <array>
#include <cmath>
#include <vector>

namespace ym
{
// RBJ biquad with Web Audio's definitions (lowpass/highpass Q is given in dB there)
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }

    void setBandpass (double f, double q, double sr);
    void setLowpassDb (double f, double qDb, double sr);
    void setHighpassDb (double f, double qDb, double sr);
    void setAllpass (double f, double q, double sr);
    void setLowShelf (double f, double gainDb, double sr);
    void setHighShelf (double f, double gainDb, double sr);

private:
    void norm (double a0, double nb0, double nb1, double nb2, double na1, double na2)
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }
};

// Parameters the effect chain needs (raw panel values: 0..127 unless noted)
struct FxParams
{
    double vol = 100, ai1 = 80, ai2 = 0;
    int vmode = 0;        // 0 SINGLE, 1 VOCODER
    double vfs = 0;       // formant shift -2..2
    double vcl = 30;      // clarity
    int mft = 0;          // 0 CHO/FLG, 1 ENS, 2 PHASER
    double mfs = 64, mfd = 0;
    int dt = 0;           // 0 STEREO, 1 CROSS, 2 L/R
    double dtm = 64, ddp = 0;
    double eqhf = 80, eqhg = 0, eqlf = 40, eqlg = 0;
    std::array<double, 16> vlev, vpan;
    FxParams() { vlev.fill (100); vpan.fill (64); }
};

class Limiter
{
public:
    void prepare (double sampleRate);
    void process (float* L, float* R, int n);
private:
    double sr = 48000, k = 5, linThr = 0.4, kneeThr = 0.8, slope = 1.0 / 12, yKneeDb = -2, makeup = 1, g = 1;
    double aAtt = 0.01, aRel = 0.0001;
    int pre = 0, pos = 0, win = 0;
    std::vector<float> bufL, bufR;
    std::vector<double> dqVal;
    std::vector<long long> dqIdx;
    int dqHead = 0, dqCount = 0;
    long long nIn = 0;
    double kneeCurve (double x, double kk) const;
    double saturate (double x) const;
};

class FxChain
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();

    // Called once per block before process().
    void setParams (const FxParams& p, int blockSize);

    // OSC1 AUDIO IN: src mono * AUDIO IN 2 gain. src may be nullptr (gives silence).
    void engineInput (const float* srcMono, float* dst, int n);

    // synthL/R: engine output. srcMono: mono sum of the audio input (modulator), may be nullptr.
    void process (const float* synthL, const float* synthR, const float* srcMono, float* outL, float* outR, int n);

private:
    struct Smooth
    {
        double v = 0, t = 0;
        inline void set (double target) { t = target; }
        inline void jump (double x) { v = t = x; }
        inline double next (double a) { v += (t - v) * a; return v; }
    };
    double sr = 48000;
    double aS02 = 0, aS03 = 0; // per-sample smoothing coefficients (tau 20 ms / 30 ms)
    FxParams cur;
    bool first = true;

    // gains
    Smooth gMaster, gDry, gVoc, gAi1, gAi2, gClar;
    // vocoder
    Biquad modShelf, clarHp;
    struct Band { Biquad mb, lp, cb; Smooth lv, pl, pr; double f = 0; };
    std::array<Band, 16> bands;
    Smooth vfsS;
    double vfsApplied = 1e9;
    // mod fx
    struct Tap { double base, rm, sign, depth, panL, panR; double ph = 0; Smooth rate, gain; };
    std::array<Tap, 6> taps; // 0,1 chorus; 2,3,4 ensemble; 5 phaser LFO
    Smooth gc, ge, gp, wet;
    std::vector<double> mfBuf; int mfPos = 0;
    std::array<Biquad, 4> ap;
    std::array<double, 4> apBase { 600, 900, 1400, 2100 };
    // delay fx
    std::vector<double> dBufL, dBufR; int dPos = 0;
    Smooth dInL, dInR, dMono, fLL, fRR, fLR, fRL, tL, tR, wL, wR;
    // eq
    Biquad eqL[2], eqH[2];
    double eqLf = 40, eqHf = 1000, eqLg = 0, eqHg = 0;
    double eqLfS = 0, eqHfS = 0, eqLgS = 0, eqHgS = 0;
    Limiter lim;
    std::vector<float> tmpL, tmpR;
};
} // namespace ym
