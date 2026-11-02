#include "Engine.h"
#include <algorithm>
#include <cmath>

namespace mm {

static constexpr double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------ wave tables
static double coefSaw(int n) { return ((n % 2) ? 1.0 : -1.0) / n; }
static double coefTri(int n) { return (n % 2) ? ((((n - 1) / 2) % 2 == 0) ? 1.0 : -1.0) / ((double)n * n) : 0.0; }

static void coeffs(int w, int n, double& re, double& im) {
  re = 0.0; im = 0.0;
  auto pulse = [&](double width) {
    re = std::sin(2.0 * kPi * n * width) / (n * kPi);
    im = (1.0 - std::cos(2.0 * kPi * n * width)) / (n * kPi);
  };
  switch (w) {
    case 0: im = coefTri(n); break;
    case 1: im = 0.5 * coefSaw(n) + 0.9 * coefTri(n); break;
    case 2: im = coefSaw(n); break;
    case 3: im = (n % 2) ? 1.0 / n : 0.0; break;
    case 4: pulse(0.33); break;
    default: pulse(0.15); break;
  }
}

void WaveBank::build(double sr) {
  const int M = N;
  std::vector<double> sinT(M), cosT(M);
  for (int i = 0; i < M; ++i) { sinT[i] = std::sin(2.0 * kPi * i / M); cosT[i] = std::cos(2.0 * kPi * i / M); }
  const double nyq = sr * 0.5;
  for (int w = 0; w < 6; ++w) {
    // peak of the full-harmonic table -> normalisation (the browser normalises PeriodicWaves the same way)
    double peak = 0.0;
    {
      std::vector<double> t(M, 0.0);
      for (int n = 1; n <= kHarm; ++n) {
        double re, im; coeffs(w, n, re, im);
        for (int i = 0; i < M; ++i) { int k = (int)(((long long)n * i) % M); t[i] += re * cosT[k] + im * sinT[k]; }
      }
      for (double v : t) peak = std::max(peak, std::fabs(v));
    }
    const double scale = peak > 1e-9 ? 1.0 / peak : 1.0;
    for (int b = 0; b < NB; ++b) {
      const double fTop = 20.0 * std::pow(2.0, (b + 1) / 2.0);
      int maxH = std::max(1, std::min(kHarm, (int)std::floor(nyq / fTop)));
      std::vector<double> t(M, 0.0);
      for (int n = 1; n <= maxH; ++n) {
        double re, im; coeffs(w, n, re, im);
        if (re == 0.0 && im == 0.0) continue;
        for (int i = 0; i < M; ++i) { int k = (int)(((long long)n * i) % M); t[i] += re * cosT[k] + im * sinT[k]; }
      }
      auto& dst = tab_[(size_t)w * NB + b];
      dst.assign(M + 1, 0.f);
      for (int i = 0; i < M; ++i) dst[i] = (float)(t[i] * scale);
      dst[M] = dst[0];
    }
  }
}

// ------------------------------------------------------------------ envelopes
void Env::on(float pk, float A, float D, float S, double sr) {
  peak = pk; stage = 1;
  const double a = std::max((double)A, 0.002);
  n = std::max(1, (int)std::lround(a * sr));
  inc = (peak - v) / (float)n;
  sus = peak * S;
  kd = (float)(1.0 - std::exp(-1.0 / (std::max((double)D, 0.005) / 3.0 * sr)));
}
void Env::off(float R, double sr) {
  stage = 3;
  kr = (float)(1.0 - std::exp(-1.0 / (std::max((double)R, 0.005) / 3.0 * sr)));
}

// ------------------------------------------------------------------ helpers
static inline float tanhq(float x) {  // same rational approximation as the browser's ladder worklet
  if (x < -3.f) return -1.f;
  if (x > 3.f) return 1.f;
  const float x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}
static inline float logcosh(float x) {
  const float a = std::fabs(x);
  return a + std::log1p(std::exp(-2.f * a)) - 0.69314718f;
}

float compressorOutDb(float x) {
  static const float tin[] = {-8.f, -6.f, -4.f, -2.f, 0.f, 2.f, 4.f, 6.f, 8.f, 10.f, 12.f};
  static const float tout[] = {-7.79f, -5.80f, -3.87f, -1.97f, -0.13f, 1.66f, 3.38f, 5.02f, 6.58f, 8.02f, 9.33f};
  constexpr int n = 11;
  if (x <= tin[0]) return x + 0.21f;
  if (x >= tin[n - 1]) return tout[n - 1] + (x - tin[n - 1]) * 0.65f;
  int i = 0;
  while (x > tin[i + 1]) ++i;
  const float f = (x - tin[i]) / (tin[i + 1] - tin[i]);
  return tout[i] + (tout[i + 1] - tout[i]) * f;
}

// ------------------------------------------------------------------ engine
void Engine::prepare(double sr) {
  sr_ = sr;
  bank_.build(sr);
  chBuf_.assign((size_t)(sr * 0.06) + 8, 0.f);
  const size_t dlen = (size_t)(sr * 2.05) + 8;
  dlBuf_[0].assign(dlen, 0.f); dlBuf_[1].assign(dlen, 0.f);
  sTune_.setTc(0.02, sr); sBend_.setTc(0.008, sr);
  sM1_.setTc(0.01, sr); sM2_.setTc(0.01, sr); sM3_.setTc(0.01, sr); sMN_.setTc(0.01, sr);
  sCut_.setTc(0.015, sr); sRes_.setTc(0.015, sr); sDrv_.setTc(0.01, sr); sContour_.setTc(0.01, sr);
  sModP_.setTc(0.02, sr); sModF_.setTc(0.02, sr); sModRate_.setTc(0.01, sr);
  sDriveAmt_.setTc(0.01, sr); sChWet_.setTc(0.01, sr); sChDry_.setTc(0.01, sr);
  sChRate_.setTc(0.01, sr); sChDepth_.setTc(0.02, sr);
  sDlTime_.setTc(0.03, sr); sDlFb_.setTc(0.01, sr); sDlWet_.setTc(0.01, sr); sDlTone_.setTc(0.01, sr);
  sVol_.setTc(0.02, sr);
  first_ = true;
  reset();
}

void Engine::reset() {
  for (int i = 0; i < 3; ++i) phase_[i] = 0.0;
  for (int i = 0; i < 4; ++i) { ly_[i] = 0.f; lt_[i] = 0.f; }
  lpi_ = 0.f; pitchCents_ = pitchTarget_ = 0.f; pitchK_ = 1.f;
  fEnv_ = Env(); aEnv_ = Env();
  for (float& p : pink_) p = 0.f;
  lfoPhase_ = shPhase_ = 0.0; shValue_ = 0.f;
  std::fill(chBuf_.begin(), chBuf_.end(), 0.f); chPos_ = 0; chPhase_ = 0.0;
  for (auto& d : dlBuf_) std::fill(d.begin(), d.end(), 0.f);
  dlPos_ = 0; for (auto& z : dlZ_) { z[0] = z[1] = 0.f; }
  adaInit_ = false; cEnv_ = 0.f;
}

void Engine::updateDelayFilter(float fc) {
  // RBJ low-pass, Q = 1 dB (Web Audio default)
  const double w0 = 2.0 * kPi * std::min((double)fc, sr_ * 0.45) / sr_;
  const double Q = std::pow(10.0, 1.0 / 20.0);
  const double alpha = std::sin(w0) / (2.0 * Q), c = std::cos(w0);
  const double a0 = 1.0 + alpha;
  dlB_[0] = (float)(((1.0 - c) * 0.5) / a0);
  dlB_[1] = (float)((1.0 - c) / a0);
  dlB_[2] = dlB_[0];
  dlB_[3] = (float)((-2.0 * c) / a0);
  dlB_[4] = (float)((1.0 - alpha) / a0);
}

void Engine::setParams(const Params& p) {
  p_ = p;
  const float dp = p.modPitch + wheel_ * (1.f - p.modPitch);
  const float df = p.modFilt + wheel_ * (1.f - p.modFilt);
  sTune_.t = p.tune; sM1_.t = p.m1; sM2_.t = p.m2; sM3_.t = p.m3; sMN_.t = p.mN;
  sCut_.t = p.cutoff; sRes_.t = p.emph * 1.15f; sDrv_.t = 0.6f + p.fdrive * 4.f; sContour_.t = p.contour * 5.f;
  sModP_.t = dp * dp * 1200.f; sModF_.t = df * 4.f; sModRate_.t = p.modRate;
  sDriveAmt_.t = p.drive;
  sChWet_.t = p.chMix * 0.9f; sChDry_.t = 1.f - p.chMix * 0.4f; sChRate_.t = p.chRate; sChDepth_.t = p.chDepth;
  float dt = p.dlTime;
  if (p.dlSync > 0) {
    const double beat = 60.0 / std::max(p.bpm, 20.0);
    const double v[4] = {0.0, beat * 0.5, beat * 0.75, beat};
    dt = (float)v[p.dlSync];
  }
  sDlTime_.t = std::min(std::max(dt, 0.01f), 1.9f);
  sDlFb_.t = p.dlFb; sDlWet_.t = p.dlMix * 0.9f; sDlTone_.t = p.dlTone;
  sVol_.t = p.vol * p.vol * 1.1f;
  if (first_) {
    for (Smooth* s : {&sTune_, &sM1_, &sM2_, &sM3_, &sMN_, &sCut_, &sRes_, &sDrv_, &sContour_, &sModP_, &sModF_, &sModRate_,
                      &sDriveAmt_, &sChWet_, &sChDry_, &sChRate_, &sChDepth_, &sDlTime_, &sDlFb_, &sDlWet_, &sDlTone_, &sVol_})
      s->snap(s->t);
    updateDelayFilter(sDlTone_.v);
    dlCoefTone_ = sDlTone_.v;
    first_ = false;
  }
}

void Engine::noteOn(int note, float vel, bool retrig) {
  const float tgt = (float)(note - 60) * 100.f;
  pitchTarget_ = tgt;
  const double g = (double)p_.glide * p_.glide * 4.0;
  if (g > 0.003) pitchK_ = (float)(1.0 - std::exp(-1.0 / ((g / 4.0) * sr_)));
  else { pitchK_ = 1.f; pitchCents_ = tgt; }
  if (retrig) {
    fEnv_.on(1.f, p_.fA, p_.fD, p_.fS, sr_);
    aEnv_.on(0.35f + 0.65f * vel, p_.aA, p_.aD, p_.aS, sr_);
  }
}

void Engine::noteOff() {
  aEnv_.off(p_.rel ? p_.aD : 0.012f, sr_);
  fEnv_.off(p_.rel ? p_.fD : 0.012f, sr_);
}

void Engine::setBend(float x) {
  static const float r[3] = {2.f, 5.f, 12.f};
  bendTarget_ = x * r[std::min(std::max(p_.bendRange, 0), 2)] * 100.f;
}
void Engine::setModWheel(float x) { wheel_ = std::min(std::max(x, 0.f), 1.f); setParams(p_); }

void Engine::processVoice(float* L, float* R, int n) {
  static const float rangeOct[6] = {-9.f, -2.f, -1.f, 0.f, 1.f, 2.f};
  static const float kbdT[4] = {0.f, 1.f / 3.f, 2.f / 3.f, 1.f};
  const double inv = 1.0 / sr_;
  const float kbdG = kbdT[std::min(std::max(p_.kbd, 0), 3)];
  float baseCents[3];
  float kb[3] = {1.f, 1.f, p_.o3kbd ? 1.f : 0.f};
  for (int i = 0; i < 3; ++i)
    baseCents[i] = rangeOct[std::min(std::max(p_.range[i], 0), 5)] * 1200.f + (kb[i] > 0.f ? -900.f : 0.f) + (i > 0 ? p_.otune[i] : 0.f);
  const float sr2 = (float)(sr_ * 2.0);
  float bendNow = sBend_.v;

  for (int s = 0; s < n; ++s) {
    // ---- smoothers & pitch
    pitchCents_ += (pitchTarget_ - pitchCents_) * pitchK_;
    bendNow += (bendTarget_ - bendNow) * sBend_.k;
    const float tune = sTune_.tick(), modP = sModP_.tick(), modF = sModF_.tick();
    const float m1 = sM1_.tick(), m2 = sM2_.tick(), m3 = sM3_.tick(), mN = sMN_.tick();
    const float cutoff = sCut_.tick(), res = sRes_.tick(), drv = sDrv_.tick(), contour = sContour_.tick();
    const float modRate = sModRate_.tick();

    // ---- modulation sources
    lfoPhase_ += modRate * inv; if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
    shPhase_ += modRate * inv; if (shPhase_ >= 1.0) { shPhase_ -= 1.0; shValue_ = rnd(); }
    // noise
    const float white = rnd();
    float noiseSig;
    {
      const float w = white;
      pink_[0] = 0.99886f * pink_[0] + w * 0.0555179f;
      pink_[1] = 0.99332f * pink_[1] + w * 0.0750759f;
      pink_[2] = 0.96900f * pink_[2] + w * 0.1538520f;
      pink_[3] = 0.86650f * pink_[3] + w * 0.3104856f;
      pink_[4] = 0.55000f * pink_[4] + w * 0.5329522f;
      pink_[5] = -0.7616f * pink_[5] - w * 0.0168980f;
      const float pk = (pink_[0] + pink_[1] + pink_[2] + pink_[3] + pink_[4] + pink_[5] + pink_[6] + w * 0.5362f) * 0.11f;
      pink_[6] = w * 0.115926f;
      noiseSig = 0.5f * (p_.noiseType ? 1.6f * pk : white);
    }

    // ---- oscillators. Osc 3 is computed first because it can be the modulation source.
    // (In the browser graph osc3 cannot modulate its own detune: the cycle is cut, so we do the same.)
    float osc[3];
    auto oscTick = [&](int i, float modBus) {
      const float cents = baseCents[i] + kb[i] * pitchCents_ + bendNow + tune + modBus * modP;
      double hz = 440.0 * std::exp2((double)cents / 1200.0);
      if (hz < 0.0) hz = -hz;
      if (hz > sr_ * 0.5) hz = sr_ * 0.5;
      const int band = bank_.bandFor(hz);
      osc[i] = bank_.read(std::min(std::max(p_.wave[i], 0), 5), phase_[i], band);
      phase_[i] += hz * inv; if (phase_[i] >= 1.0) phase_[i] -= 1.0;
    };
    float bus = 0.f;
    switch (p_.modSrc) {
      case 0: oscTick(2, 0.f); bus = osc[2]; break;
      case 1: bus = (float)((2.0 / kPi) * std::asin(std::sin(2.0 * kPi * lfoPhase_))); oscTick(2, bus); break;
      case 2: bus = lfoPhase_ < 0.5 ? 1.f : -1.f; oscTick(2, bus); break;
      case 3: bus = shValue_; oscTick(2, bus); break;
      default: bus = noiseSig; oscTick(2, bus); break;
    }
    oscTick(0, bus);
    oscTick(1, bus);

    // ---- mixer
    const float mixed = (osc[0] * m1 + osc[1] * m2 + osc[2] * m3 + noiseSig * mN) * 0.6f;

    // ---- filter env + ladder
    const float fEnv = fEnv_.tick();
    const float aEnv = aEnv_.tick();
    const float cv = fEnv * contour + pitchCents_ * (kbdG / 1200.f) + bus * modF;
    float fc = cutoff * std::exp2(cv);
    fc = std::min(fc, sr2 * 0.22f); fc = std::max(fc, 8.f);
    const float g = 1.f - std::exp(-2.f * (float)kPi * fc / sr2);
    const float k = res * 4.f;
    float y3 = 0.f;
    for (int sub = 0; sub < 2; ++sub) {
      const float xi = sub == 0 ? (mixed + lpi_) * 0.5f : mixed;
      const float u = tanhq(drv * xi * (1.f + 0.35f * k) - k * ly_[3]);
      ly_[0] += g * (u - lt_[0]); lt_[0] = tanhq(ly_[0]);
      ly_[1] += g * (lt_[0] - lt_[1]); lt_[1] = tanhq(ly_[1]);
      ly_[2] += g * (lt_[1] - lt_[2]); lt_[2] = tanhq(ly_[2]);
      ly_[3] += g * (lt_[2] - lt_[3]); lt_[3] = tanhq(ly_[3]);
    }
    lpi_ = mixed;
    if (!std::isfinite(ly_[3])) { for (int i = 0; i < 4; ++i) { ly_[i] = 0.f; lt_[i] = 0.f; } }
    y3 = ly_[3] * 0.9f;

    // ---- VCA
    float x = y3 * aEnv;

    // ---- drive (tanh, first-order antiderivative anti-aliasing instead of the browser's 2x oversampling)
    {
      const float amt = sDriveAmt_.tick();
      const float kk = 0.5f + amt * 8.f;
      const float pre = (1.f + amt * 3.f);
      const float post = 1.f / (1.f + amt * 1.4f);
      const float xin = x * pre * kk;
      float y;
      if (!adaInit_) { adaPrev_ = xin; adaF_ = logcosh(xin); adaInit_ = true; }
      const float F = logcosh(xin);
      const float d = xin - adaPrev_;
      if (std::fabs(d) < 1e-4f) y = std::tanh(0.5f * (xin + adaPrev_));
      else y = (F - adaF_) / d;
      adaPrev_ = xin; adaF_ = F;
      y /= std::tanh(kk);
      x = y * post;
    }

    // ---- chorus (mono in, stereo out)
    float cl, cr;
    {
      const float rate = sChRate_.tick(), depth = sChDepth_.tick(), wet = sChWet_.tick(), dry = sChDry_.tick();
      chPhase_ += rate * inv; if (chPhase_ >= 1.0) chPhase_ -= 1.0;
      const float lfo = (float)std::sin(2.0 * kPi * chPhase_);
      const int len = (int)chBuf_.size();
      chBuf_[chPos_] = x;
      auto tap = [&](float sec) {
        float pos = (float)chPos_ - sec * (float)sr_;
        while (pos < 0.f) pos += (float)len;
        int i0 = (int)pos; float f = pos - i0; int i1 = i0 + 1; if (i1 >= len) i1 -= len;
        return chBuf_[i0] * (1.f - f) + chBuf_[i1] * f;
      };
      const float d0 = tap(0.013f + 0.0035f * depth * lfo);
      const float d1 = tap(0.019f - 0.0035f * depth * lfo);
      // StereoPanner (mono input): pan -0.8 / +0.8
      const float aL = std::cos(0.1f * (float)kPi * 0.5f), aR = std::sin(0.1f * (float)kPi * 0.5f);
      cl = x * dry + wet * (d0 * aL + d1 * aR);
      cr = x * dry + wet * (d0 * aR + d1 * aL);
      chPos_ = (chPos_ + 1) % len;
    }

    // ---- delay (stereo, feedback through a low-pass)
    {
      const float dtm = sDlTime_.tick(), fb = sDlFb_.tick(), wet = sDlWet_.tick(), tone = sDlTone_.tick();
      if (std::fabs(tone - dlCoefTone_) > 1.f) { updateDelayFilter(tone); dlCoefTone_ = tone; }
      const int len = (int)dlBuf_[0].size();
      float pos = (float)dlPos_ - dtm * (float)sr_;
      while (pos < 0.f) pos += (float)len;
      const int i0 = (int)pos; const float f = pos - i0; int i1 = i0 + 1; if (i1 >= len) i1 -= len;
      float in[2] = {cl, cr}, out[2];
      for (int c = 0; c < 2; ++c) {
        const float d = dlBuf_[c][i0] * (1.f - f) + dlBuf_[c][i1] * f;
        // direct form II transposed
        const float yb = dlB_[0] * d + dlZ_[c][0];
        dlZ_[c][0] = dlB_[1] * d - dlB_[3] * yb + dlZ_[c][1];
        dlZ_[c][1] = dlB_[2] * d - dlB_[4] * yb;
        dlBuf_[c][dlPos_] = in[c] + fb * yb;
        out[c] = in[c] + wet * yb;
      }
      dlPos_ = (dlPos_ + 1) % len;
      L[s] = out[0]; R[s] = out[1];
    }
  }
  sBend_.v = bendNow;
}

void Engine::processMaster(float* L, float* R, int n) {
  const float atk = (float)(1.0 - std::exp(-1.0 / (0.003 * sr_)));
  const float rel = (float)(1.0 - std::exp(-1.0 / (0.150 * sr_)));
  for (int s = 0; s < n; ++s) {
    const float vol = sVol_.tick();
    float l = L[s] * vol, r = R[s] * vol;
    const float a = std::max(std::fabs(l), std::fabs(r));
    cEnv_ += (a - cEnv_) * (a > cEnv_ ? atk : rel);
    const float e = std::max(cEnv_, 1e-6f);
    const float inDb = 20.f * std::log10(e);
    const float gdb = compressorOutDb(inDb) - inDb;
    const float gain = std::pow(10.f, gdb * 0.05f);
    L[s] = l * gain; R[s] = r * gain;
  }
}

}  // namespace mm
