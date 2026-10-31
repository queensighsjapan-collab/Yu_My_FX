#include "Engine.h"
#include <algorithm>

namespace ym303
{
Engine::Engine (double sampleRate) : sr (sampleRate)
{
    const double PI = 3.14159265358979323846, os = 2.0;
    hpA     = 1 - std::exp (-2 * PI * 95 / (sr * os));   // feedback high-pass ~95 Hz: resonance thins the bass
    slideA  = 1 - std::exp (-1 / (sr * .024));            // ~60 ms slide
    accDec  = std::exp (-1 / (sr * .55));
    cutA    = 1 - std::exp (-1 / (sr * .0012));
    resA    = 1 - std::exp (-1 / (sr * .004));
    atk     = 1 - std::exp (-1 / (sr * .003));
    rel     = 1 - std::exp (-1 / (sr * .013));
}

double Engine::poly (double t, double dt)
{
    if (t < dt)     { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}

void Engine::noteOn (int note, bool accent, bool slide)
{
    target = note;
    if (slide && gate > 0)
    {
        accOn = accent;                                   // legato: glide, no retrigger
    }
    else
    {
        pitch = note; env = 1; accOn = accent;
        if (accent) acc += (1 - acc) * .45;               // accent capacitor charges and builds on repeats
    }
    gate = 1;
}

void Engine::noteOff() { gate = 0; }

void Engine::silence() { gate = 0; amp = 0; env = 0; acc = 0; }

void Engine::process (float* out, int n)
{
    const double PI = 3.14159265358979323846, os = 2.0, dtS = 1 / (sr * os);
    const double vol = std::pow (p.volume, 1.6);
    for (int i = 0; i < n; ++i)
    {
        pitch += (target - pitch) * slideA;
        const double freq = 440 * std::pow (2.0, (pitch - 69 + (p.tuning - .5) * 2) / 12);
        // MEG: normal notes follow the DECAY knob (200 ms - 2 s); accented notes use a short fixed decay
        const double dec = accOn ? .15 : .2 + std::pow (p.decay, 1.35) * 1.75;
        env *= std::exp (-1 / (sr * dec));
        acc *= accDec;
        const double base = 62 * std::pow (2.0, p.cutoff * 5.7);
        const double oct = (p.envmod * 4.5 + p.accent * acc * 4.0) * env + p.accent * acc * .9;
        double fc = base * std::pow (2.0, oct);
        if (fc > sr * .33) fc = sr * .33;
        cut += (fc - cut) * cutA;
        res += (std::min (4.15, p.reso * 4.05 + p.accent * acc * .4) - res) * resA;
        amp += ((gate != 0 ? 1.0 : 0.0) - amp) * (gate != 0 ? atk : rel);
        const double g = std::tan (PI * cut / (sr * os)), G = g / (1 + g);
        double sum = 0;
        for (int sub = 0; sub < (int) os; ++sub)
        {
            const double step = std::min (.45, freq * dtS), phase = ph;
            double x;
            if (p.wave == 1) { x = phase < .5 ? 1.0 : -1.0; x += poly (phase, step) - poly (std::fmod (phase + .5, 1.0), step); }
            else             { x = 2 * phase - 1; x -= poly (phase, step); }
            ph = std::fmod (phase + step, 1.0);
            // 4-pole ladder, feedback from stage 4, output tapped at stage 3 => 18 dB/oct
            fbLp += (y4 - fbLp) * hpA;
            double u = std::tanh (x * 1.15 - res * (y4 - fbLp));
            double v = (u - s0) * G; double y = v + s0; s0 = y + v; u = y;
            v = (u - s1) * G; y = v + s1; s1 = y + v; u = y;
            v = (u - s2) * G; y = v + s2; s2 = y + v; const double tap = y; u = y;
            v = (std::tanh (u * 1.1) - s3) * G; y = v + s3; s3 = y + v; y4 = y;
            if (s0 > 6 || s0 < -6 || ! (s3 == s3)) { s0 = s1 = s2 = s3 = y4 = 0; }
            sum += tap;
        }
        const double lvl = .62 * (1 + p.accent * acc * 1.0) * vol;
        double o = std::tanh (sum / os * (1.15 + .05 * res) * amp * lvl * 2.2);
        const double hp = o - dcx + .995 * dcy; dcx = o; dcy = hp;
        o = hp * .7;
        if (! (o == o)) o = 0;
        out[i] = (float) o;
    }
}
}
