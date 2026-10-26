#include "Engine.h"
#include <algorithm>
#include <cstring>

namespace ym
{
const char* const kEngineParamNames[NUM_EP] = {
    "o1w", "o1c1", "o1c2", "dwgs", "o2w", "o2m", "o2s", "o2t", "mx1", "mx2", "mxn", "ft", "cut", "res", "f1", "fv", "fk", "al", "ap", "dist", "ak",
    "e1a", "e1d", "e1s", "e1r", "e2a", "e2d", "e2s", "e2r", "l1w", "l1f", "l1k", "l1t", "l2w", "l2f", "l2k", "ptn", "pbr", "ptr", "pvi", "ppo",
    "arptm", "vs1", "vs2", "vs3", "vs4", "vd1", "vd2", "vd3", "vd4", "vi1", "vi2", "vi3", "vi4", "msq", "msd", "seqv", "bend", "mod"
};

int engineParamIndex (const char* name)
{
    for (int i = 0; i < NUM_EP; ++i)
        if (std::strcmp (kEngineParamNames[i], name) == 0) return i;
    return -1;
}

namespace
{
constexpr double TAU = 6.283185307179586476925286766559;
constexpr double PI = 3.14159265358979323846;
inline double clampd (double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline double pow2 (double x) { return std::exp2 (x); }
inline double mtof (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

inline double blep (double t, double dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}
inline double sawf (double ph, double dt) { return 2 * ph - 1 - blep (ph, dt); }
inline double pulsef (double ph, double dt, double pw)
{
    double x = ph < pw ? 1 : -1;
    x += blep (ph, dt);
    double t = ph - pw; if (t < 0) t += 1;
    x -= blep (t, dt);
    return x;
}
inline double trif (double ph) { return 4 * std::fabs (ph - 0.5) - 1; }

const double VW[5][3] = { { 800, 1150, 2900 }, { 400, 1700, 2600 }, { 270, 2140, 2950 }, { 450, 800, 2830 }, { 325, 700, 2700 } };
const int NH[4] = { 48, 24, 12, 6 };

// trapezoidal state-variable filter
inline double svf (double* fs, int o, const double* cf, int c, double x, int mode)
{
    const double a1 = cf[c], a2 = cf[c + 1], a3 = cf[c + 2], k = cf[c + 3];
    const double v3 = x - fs[o + 1];
    const double v1 = a1 * fs[o] + a2 * v3;
    const double v2 = fs[o + 1] + a2 * fs[o] + a3 * v3;
    fs[o] = 2 * v1 - fs[o];
    fs[o + 1] = 2 * v2 - fs[o + 1];
    return mode == 0 ? v2 : (mode == 1 ? v1 * k : x - k * v1 - v2);
}
} // namespace

double Engine::specAmp (int k, int n)
{
    const int fam = k >> 4, j = k & 15;
    if (fam == 0) return ((n & 1) ? 1.0 : j / 15.0) / std::pow ((double) n, 0.5 + j * 0.03);
    if (fam == 1)
    {
        const double c = 2 + j * 1.3, w = 1.2 + j * 0.12, d = (n - c) / w;
        return (std::exp (-d * d) + 0.15 / n) / std::pow ((double) n, 0.2);
    }
    if (fam == 2)
    {
        const double h = std::sin (k * 12.9898 + n * 78.233) * 43758.5453;
        return (h - std::floor (h)) / std::pow ((double) n, 0.85);
    }
    return ((n % (2 + (j % 6))) == 0 ? 1.0 : 0.08) / std::pow ((double) n, 0.7);
}

double Engine::phase0 (int k, int n)
{
    if ((k >> 4) != 2) return 0;
    const double h = std::sin (k * 39.346 + n * 11.135) * 24634.6345;
    return (h - std::floor (h)) * TAU;
}

void Engine::buildTables()
{
    tabs.assign (64 * 4, {});
    for (int k = 0; k < 64; ++k)
        for (int m = 0; m < 4; ++m)
        {
            std::vector<float>& t = tabs[(size_t) (k * 4 + m)];
            t.assign ((size_t) TS + 1, 0.0f);
            const int nh = NH[m];
            double mx = 0;
            std::vector<double> amp ((size_t) nh + 1), ph ((size_t) nh + 1);
            for (int n = 1; n <= nh; ++n) { amp[(size_t) n] = specAmp (k, n); ph[(size_t) n] = phase0 (k, n); }
            for (int i = 0; i < TS; ++i)
            {
                double s = 0;
                const double x = (double) i / TS * TAU;
                for (int n = 1; n <= nh; ++n) s += amp[(size_t) n] * std::sin (n * x + ph[(size_t) n]);
                t[(size_t) i] = (float) s;
                if (std::fabs (s) > mx) mx = std::fabs (s);
            }
            const double d = mx != 0 ? mx : 1;
            for (int i = 0; i < TS; ++i) t[(size_t) i] = (float) ((double) t[(size_t) i] / d);
            t[(size_t) TS] = t[0];
        }
}

Engine::Engine (double sampleRate) : sr (sampleRate)
{
    p[ap] = 64; p[arptm] = 120; p[seqv] = 64; p[dwgs] = 1;
    buildTables();
}

double Engine::rnd()
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (double) seed / 4294967296.0;
}

void Engine::setCf (double* cf, int c, double fc, double k) const
{
    const double g = std::tan (PI * clampd (fc, 10, sr * 0.45) / sr);
    const double a1 = 1 / (1 + g * (g + k));
    cf[c] = a1; cf[c + 1] = g * a1; cf[c + 2] = g * g * a1; cf[c + 3] = k;
}

void Engine::noteOn (int n, double vel)
{
    Voice* v = nullptr;
    for (auto& x : vs) if (x.on && x.gate && x.note == n) { v = &x; break; }
    if (! v) for (auto& x : vs) if (! x.on) { v = &x; break; }
    if (! v)
    {
        Voice* best = nullptr;
        for (auto& x : vs)
            if (! best || (best->gate && ! x.gate) || (best->gate == x.gate && x.age < best->age)) best = &x;
        v = best;
    }
    const bool fresh = ! v->on;
    v->on = true; v->gate = true; v->note = n; v->vel = vel; v->age = ++ageCounter;
    v->pf = (p[ppo] > 0 && lastNote >= 0) ? lastNote : n;
    lastNote = n;
    if (fresh)
    {
        v->ph1 = 0; v->ph2 = 0; v->ph1b = 0; v->sq = 1; v->e1 = 0; v->e2 = 0;
        std::fill (std::begin (v->fs), std::end (v->fs), 0.0);
    }
    v->s1 = 1; v->s2 = 1;
    v->l1 = p[l1k] > 0 ? 0 : gph1; v->l2 = p[l2k] > 0 ? 0 : gph2;
    v->sh1 = rnd() * 2 - 1; v->sh2 = rnd() * 2 - 1;
    v->ml2 = 0; v->cnt = 0;
}

void Engine::noteOff (int n)
{
    for (auto& v : vs)
        if (v.on && v.gate && v.note == n) { v.gate = false; if (v.s1) v.s1 = 4; if (v.s2) v.s2 = 4; }
}

void Engine::panic()
{
    for (auto& v : vs) if (v.on) { v.gate = false; v.s1 = 4; v.s2 = 4; }
}

void Engine::ctrl (Voice& v)
{
    const double dtc = 4.0 / sr;
    v.l1 += B.l1f * dtc; if (v.l1 >= 1) { v.l1 -= 1; v.sh1 = rnd() * 2 - 1; }
    v.l2 += B.l2fBase * pow2 (v.ml2 * 3) * dtc; if (v.l2 >= 1) { v.l2 -= 1; v.sh2 = rnd() * 2 - 1; }
    const double lf1 = B.l1w == 0 ? 2 * v.l1 - 1 : B.l1w == 1 ? (v.l1 < 0.5 ? 1 : -1) : B.l1w == 2 ? trif (v.l1) : v.sh1;
    const double lf2 = B.l2w == 0 ? 2 * v.l2 - 1 : B.l2w == 1 ? (v.l2 < 0.5 ? 1 : 0) : B.l2w == 2 ? std::sin (TAU * v.l2) : v.sh2;
    v.pf += (v.note - v.pf) * B.pc;
    sv[0] = v.e1; sv[1] = v.e2; sv[2] = lf1; sv[3] = lf2; sv[4] = v.vel; sv[5] = sm.bend; sv[6] = sm.modw; sv[7] = (v.note - 60) / 60.0;
    double mp = 0, m2 = 0, mc1 = 0, mn = 0, mcut = 0, ma = 0, mpan = 0, ml2 = 0;
    for (int j = 0; j < 4; ++j)
    {
        const double it = B.VI[j];
        if (it == 0) continue;
        const double s = sv[B.VS[j] & 7] * it;
        switch (B.VD[j])
        {
            case 0: mp += s * 24; break;
            case 1: m2 += s * 24; break;
            case 2: mc1 += s; break;
            case 3: mn += s; break;
            case 4: mcut += s * 5; break;
            case 5: ma += s; break;
            case 6: mpan += s; break;
            default: ml2 += s;
        }
    }
    if (B.seqOn)
    {
        const double q = (sqv - 64) / 64;
        switch (B.seqD)
        {
            case 0: mcut += q * 3; break;
            case 1: mp += q * 12; break;
            case 2: mc1 += q * 0.5; break;
            case 3: ma += sqv / 127 - 1; break;
            case 4: mpan += q; break;
            default: ml2 += q * 0.7;
        }
    }
    v.ml2 = ml2;
    const double semi = v.pf + B.trans + sm.tune100 + sm.bend * B.bendR + lf1 * sm.modw * sm.vibInt + mp;
    v.f1 = mtof (semi);
    const double f2 = mtof (semi + B.o2s + sm.o2t126 + m2);
    v.dt1 = std::min (0.45, v.f1 / sr); v.dt2 = std::min (0.45, f2 / sr);
    v.c1 = clampd (sm.c1b + mc1, 0, 1);
    v.nl = clampd (sm.mxn + mn, 0, 1);
    v.pw = clampd (0.5 - 0.45 * v.c1 + 0.4 * sm.c2b * lf2, 0.05, 0.95);
    if (B.osw == 0) v.dtb = v.dt1 * pow2 (v.c1 * 30 / 1200);
    if (B.osw == 4)
    {
        const double pos = v.c1 * 4;
        const int i0 = std::min (3, (int) pos);
        const double fr = pos - i0, sh = pow2 (sm.c2b * 0.6);
        for (int q = 0; q < 3; ++q) setCf (v.cf, 12 + q * 4, (VW[i0][q] * (1 - fr) + VW[i0 + 1][q] * fr) * sh, 0.12);
    }
    if (B.osw == 6) setCf (v.cf, 8, 500 * pow2 (v.c1 * 6), 1.414 - sm.c2b * 1.39);
    const double fc = clampd (20 * pow2 (sm.cutB + v.e1 * sm.f1k + v.vel * sm.fvk + ((v.note - 60) / 12.0) * sm.fkk + mcut), 20, sr * 0.45);
    if (B.ftype == 0) { setCf (v.cf, 0, fc, 1.1); setCf (v.cf, 4, fc, sm.kres); }
    else setCf (v.cf, 0, fc, sm.kres);
    v.ga = sm.alv * pow2 (((v.note - 60) / 12.0) * sm.akk) * (0.35 + 0.65 * std::pow (v.vel, 0.7)) * std::max (0.0, 1 + ma);
    const double pn = clampd (sm.panB + mpan, -1, 1), th = (pn + 1) * PI / 4;
    v.pl = std::cos (th); v.pr = std::sin (th);
}

void Engine::process (float* L, float* R, int len, const float* inp)
{
    B.osw = (int) p[o1w]; B.o2w = (int) p[o2w];
    const int o2mode = (int) p[o2m];
    const bool ring = o2mode == 1 || o2mode == 3, sync = o2mode == 2 || o2mode == 3;
    tg.c1b = p[o1c1] / 127; tg.c2b = p[o1c2] / 127;
    tg.mx1 = p[mx1] / 127; tg.mx2 = p[mx2] / 127; tg.mxn = p[mxn] / 127;
    B.ftype = (int) p[ft];
    const double resv = clampd (p[res] / 127, 0, 1);
    tg.kres = 1.414 - resv * 1.394;
    const bool distOn = p[dist] > 0;
    tg.cutB = p[cut] / 127 * 10; tg.f1k = p[f1] / 63 * 6; tg.fvk = p[fv] / 63 * 3; tg.fkk = p[fk] / 63;
    tg.alv = std::pow (p[al] / 127, 1.5); tg.akk = p[ak] / 63 * 0.5;
    const double ar1 = 1 / (0.0008 * pow2 (p[e1a] / 127 * 13) * sr);
    const double dc1 = 1 - std::exp (-1 / (0.003 * pow2 (p[e1d] / 127 * 11) * sr));
    const double su1 = p[e1s] / 127;
    const double rc1 = 1 - std::exp (-1 / (0.003 * pow2 (p[e1r] / 127 * 11) * sr));
    const double ar2 = 1 / (0.0008 * pow2 (p[e2a] / 127 * 13) * sr);
    const double dc2 = 1 - std::exp (-1 / (0.003 * pow2 (p[e2d] / 127 * 11) * sr));
    const double su2 = p[e2s] / 127;
    const double rc2 = 1 - std::exp (-1 / (0.003 * pow2 (p[e2r] / 127 * 11) * sr));
    const double tempo = p[arptm] != 0 ? p[arptm] : 120;
    auto lfoRate = [&] (double f, double sy)
    {
        return sy > 0 ? (tempo / 60) * pow2 (std::floor (f / 127 * 6) - 2) : 0.05 * pow2 (f / 127 * 9.5);
    };
    B.l1f = lfoRate (p[l1f], p[l1t]); B.l2fBase = lfoRate (p[l2f], 0);
    B.l1w = (int) p[l1w]; B.l2w = (int) p[l2w];
    tg.bend = p[bend]; tg.modw = p[mod]; tg.tune100 = p[ptn] / 100; tg.vibInt = p[pvi] / 63 * 1.5; tg.o2t126 = p[o2t] / 63;
    tg.panB = (p[ap] - 64) / 64;
    B.trans = p[ptr]; B.bendR = p[pbr]; B.o2s = p[o2s];
    if (! smInit) { smInit = true; sm = tg; }
    const double scS = 1 - std::exp (-1 / (sr * 0.006)), scB = 1 - std::exp (-1 / (sr * 0.003));
    B.pc = p[ppo] > 0 ? 1 - std::exp (-4 / (sr * (0.005 + p[ppo] / 127 * 0.6))) : 1;
    const int vsI[4] = { (int) p[vs1], (int) p[vs2], (int) p[vs3], (int) p[vs4] };
    const int vdI[4] = { (int) p[vd1], (int) p[vd2], (int) p[vd3], (int) p[vd4] };
    const double viI[4] = { p[vi1] / 63, p[vi2] / 63, p[vi3] / 63, p[vi4] / 63 };
    for (int j = 0; j < 4; ++j) { B.VS[j] = vsI[j]; B.VD[j] = vdI[j]; B.VI[j] = viI[j]; }
    B.seqOn = p[msq] > 0; B.seqD = (int) p[msd];
    const double sqt = p[seqv], sqc = 1 - std::exp (-1 / (sr * 0.005));
    const int dwgsK = (int) clampd ((int) p[dwgs] - 1, 0, 63);

    for (int i = 0; i < len; ++i)
    {
        const double x = inp ? inp[i] : 0.0;
        sqv += (sqt - sqv) * sqc;
        sm.c1b += (tg.c1b - sm.c1b) * scS; sm.c2b += (tg.c2b - sm.c2b) * scS;
        sm.mx1 += (tg.mx1 - sm.mx1) * scS; sm.mx2 += (tg.mx2 - sm.mx2) * scS; sm.mxn += (tg.mxn - sm.mxn) * scS;
        sm.kres += (tg.kres - sm.kres) * scS; sm.cutB += (tg.cutB - sm.cutB) * scS;
        sm.f1k += (tg.f1k - sm.f1k) * scS; sm.fvk += (tg.fvk - sm.fvk) * scS; sm.fkk += (tg.fkk - sm.fkk) * scS;
        sm.alv += (tg.alv - sm.alv) * scS; sm.akk += (tg.akk - sm.akk) * scS;
        sm.bend += (tg.bend - sm.bend) * scB; sm.modw += (tg.modw - sm.modw) * scB;
        sm.tune100 += (tg.tune100 - sm.tune100) * scS; sm.vibInt += (tg.vibInt - sm.vibInt) * scS;
        sm.o2t126 += (tg.o2t126 - sm.o2t126) * scS; sm.panB += (tg.panB - sm.panB) * scS;
        double oL = 0, oR = 0;
        for (int vi = 0; vi < NV; ++vi)
        {
            Voice& v = vs[vi];
            if (! v.on) continue;
            if (v.s1 == 1) { v.e1 += ar1; if (v.e1 >= 1) { v.e1 = 1; v.s1 = 2; } }
            else if (v.s1 == 2) { v.e1 += (su1 - v.e1) * dc1; if (std::fabs (v.e1 - su1) < 0.0005) { v.e1 = su1; v.s1 = 3; } }
            else if (v.s1 == 3) v.e1 = su1;
            else if (v.s1 == 4) { v.e1 -= v.e1 * rc1; if (v.e1 < 0.0002) { v.e1 = 0; v.s1 = 0; } }
            if (v.s2 == 1) { v.e2 += ar2; if (v.e2 >= 1) { v.e2 = 1; v.s2 = 2; } }
            else if (v.s2 == 2) { v.e2 += (su2 - v.e2) * dc2; if (std::fabs (v.e2 - su2) < 0.0005) { v.e2 = su2; v.s2 = 3; } }
            else if (v.s2 == 3) v.e2 = su2;
            else if (v.s2 == 4) { v.e2 -= v.e2 * rc2; if (v.e2 < 0.0002) { v.e2 = 0; v.s2 = 0; } }
            if (v.s2 == 0) { v.on = false; std::fill (std::begin (v.fs), std::end (v.fs), 0.0); continue; }
            if ((v.cnt++ & 3) == 0) ctrl (v);

            const double dt1 = v.dt1, dt2 = v.dt2;
            v.ph1 += dt1;
            bool wr = false;
            if (v.ph1 >= 1) { v.ph1 -= 1; wr = true; v.sq = -v.sq; }
            v.ph2 += dt2; if (v.ph2 >= 1) v.ph2 -= 1;
            if (wr && sync) v.ph2 = v.ph1 * (dt2 / dt1);
            const double o2 = B.o2w == 0 ? sawf (v.ph2, dt2) : B.o2w == 1 ? pulsef (v.ph2, dt2, 0.5) : trif (v.ph2);

            double s1 = 0;
            const double ph = v.ph1;
            switch (B.osw)
            {
                case 0:
                {
                    s1 = sawf (ph, dt1);
                    if (v.c1 > 0.002)
                    {
                        v.ph1b += v.dtb; if (v.ph1b >= 1) v.ph1b -= 1;
                        const double mu = std::min (1.0, v.c1 * 6);
                        s1 = (s1 + mu * sawf (v.ph1b, v.dtb)) / (1 + 0.7 * mu);
                    }
                    if (sm.c2b > 0) s1 += sm.c2b * 0.6 * v.sq;
                    break;
                }
                case 1: s1 = pulsef (ph, dt1, v.pw); break;
                case 2:
                {
                    double t = trif (ph);
                    if (v.c1 > 0) t = t * (1 - v.c1) + v.c1 * sawf (ph, dt1);
                    if (sm.c2b > 0) t = std::sin (t * PI * 0.5 * (1 + sm.c2b * 3));
                    s1 = t; break;
                }
                case 3: s1 = (std::sin (TAU * ph) + v.c1 * 0.5 * std::sin (2 * TAU * ph) + sm.c2b * 0.4 * std::sin (3 * TAU * ph)) / (1 + 0.5 * v.c1 + 0.4 * sm.c2b); break;
                case 4:
                {
                    const double src = sawf (ph, dt1);
                    s1 = (svf (v.fs, 4, v.cf, 12, src, 1) + 0.7 * svf (v.fs, 6, v.cf, 16, src, 1) + 0.45 * svf (v.fs, 8, v.cf, 20, src, 1)) * 2.2;
                    break;
                }
                case 5:
                {
                    const int k = dwgsK;
                    const int nh = (int) std::floor (sr * 0.45 / v.f1);
                    const int m = nh >= 48 ? 0 : nh >= 24 ? 1 : nh >= 12 ? 2 : 3;
                    auto rd = [&] (const std::vector<float>& t)
                    {
                        const double xx = ph * TS; const int ii = (int) xx; const double f = xx - ii;
                        return (double) t[(size_t) ii] + ((double) t[(size_t) ii + 1] - (double) t[(size_t) ii]) * f;
                    };
                    double s = rd (tab (k, m));
                    if (v.c1 > 0.01) s = s * (1 - v.c1) + rd (tab (std::min (63, k + 1), m)) * v.c1;
                    s1 = s; break;
                }
                case 6: s1 = svf (v.fs, 10, v.cf, 8, rnd() * 2 - 1, 0) * 2.5; break;
                default: s1 = x * 2;
            }

            const double nz1 = rnd() * 2 - 1; // fixed evaluation order (matches the JS version)
            const double nz2 = (rnd() - 0.5) * 1e-5;
            const double m = sm.mx1 * (ring ? s1 * o2 : s1) + sm.mx2 * o2 + v.nl * nz1 + nz2;
            double y;
            if (B.ftype == 0) { y = svf (v.fs, 0, v.cf, 0, m, 0); y = svf (v.fs, 2, v.cf, 4, y, 0); }
            else y = svf (v.fs, 0, v.cf, 0, m, B.ftype == 1 ? 0 : B.ftype == 2 ? 1 : 2);
            if (y != y) { std::fill (std::begin (v.fs), std::end (v.fs), 0.0); y = 0; }
            if (distOn) y = std::tanh (y * 5) * 0.6;
            y *= v.e2 * v.ga;
            oL += y * v.pl; oR += y * v.pr;
        }
        L[i] = (float) (oL * 0.4); R[i] = (float) (oR * 0.4);
    }
    auto fix = [] (double& a, double b) { if (a != a) a = b; };
    fix (sm.c1b, tg.c1b); fix (sm.c2b, tg.c2b); fix (sm.mx1, tg.mx1); fix (sm.mx2, tg.mx2); fix (sm.mxn, tg.mxn);
    fix (sm.kres, tg.kres); fix (sm.cutB, tg.cutB); fix (sm.f1k, tg.f1k); fix (sm.fvk, tg.fvk); fix (sm.fkk, tg.fkk);
    fix (sm.alv, tg.alv); fix (sm.akk, tg.akk); fix (sm.bend, tg.bend); fix (sm.modw, tg.modw); fix (sm.tune100, tg.tune100);
    fix (sm.vibInt, tg.vibInt); fix (sm.o2t126, tg.o2t126); fix (sm.panB, tg.panB);
    gph1 = std::fmod (gph1 + len * B.l1f / sr, 1.0);
    gph2 = std::fmod (gph2 + len * B.l2fBase / sr, 1.0);
    if (gph1 != gph1) gph1 = 0;
    if (gph2 != gph2) gph2 = 0;
    if (sqv != sqv) sqv = 64;
}
} // namespace ym
