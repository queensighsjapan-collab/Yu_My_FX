// mini mogumogu - synth engine (no JUCE dependency).
// A C++ port of the Web Audio graph in synthesizer/mini-mogumogu.html:
//   3 VCO -> mixer(+noise) -> 24dB/oct ladder (tanh) -> VCA -> drive -> chorus -> delay -> [reverb] -> master -> compressor
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mm {

constexpr int kNumSteps = 16;

struct Params {
  float vol = 0.7f, tune = 0.f, glide = 0.f;
  int multi = 0;          // 0 = LEGATO, 1 = MULTI
  int bendRange = 0;      // 0:+-2  1:+-5  2:+-12
  int range[3] = {3, 3, 3};   // 0:LO 1:32' 2:16' 3:8' 4:4' 5:2'
  int wave[3] = {2, 2, 3};    // 0:TRI 1:SHK 2:SAW 3:SQR 4:PLS1 5:PLS2
  float otune[3] = {0.f, 5.f, 0.f};  // cents (index 0 unused)
  int o3kbd = 1;
  float m1 = 0.8f, m2 = 0.f, m3 = 0.f, mN = 0.f;
  int noiseType = 0;      // 0 white 1 pink
  float cutoff = 1200.f, emph = 0.1f, contour = 0.4f, fdrive = 0.2f;
  int kbd = 1;            // 0:OFF 1:1/3 2:2/3 3:FULL
  float fA = 0.005f, fD = 0.4f, fS = 0.2f, aA = 0.005f, aD = 0.4f, aS = 0.7f;
  int rel = 1;            // 0 SHORT, 1 = DECAY
  int modSrc = 1;         // 0 OSC3 1 LFO TRI 2 LFO SQR 3 S&H 4 NOISE
  float modRate = 5.f, modPitch = 0.f, modFilt = 0.f;
  float drive = 0.f, chRate = 0.8f, chDepth = 0.5f, chMix = 0.f;
  float dlTime = 0.3f, dlFb = 0.35f, dlTone = 4000.f, dlMix = 0.f;
  int dlSync = 0;         // 0 FREE 1 1/8 2 1/8. 3 1/4
  float rvSize = 0.5f, rvMix = 0.f;
  double bpm = 110.0;     // tempo used for the delay sync
};

// Band-limited wave tables built from the same Fourier coefficients as the browser version's PeriodicWaves.
class WaveBank {
 public:
  static constexpr int N = 4096, NB = 24, kHarm = 96;
  void build(double sampleRate);
  inline float read(int w, double phase01, int band) const {
    const float* t = tab_[(size_t)w * NB + band].data();
    double x = phase01 * N;
    int i = (int)x;
    float f = (float)(x - i);
    return t[i] + (t[i + 1] - t[i]) * f;
  }
  inline int bandFor(double hz) const {
    if (hz < 28.28) return 0;
    int b = (int)(2.0 * std::log2(hz / 20.0));
    return b >= NB ? NB - 1 : b;
  }
 private:
  std::array<std::vector<float>, 6 * NB> tab_;
};

struct Smooth {
  float v = 0.f, t = 0.f, k = 1.f;
  void setTc(double tc, double sr) { k = (float)(1.0 - std::exp(-1.0 / (tc * sr))); }
  void snap(float x) { v = t = x; }
  inline float tick() { v += (t - v) * k; return v; }
};

struct Env {
  float v = 0.f, inc = 0.f, peak = 1.f, sus = 0.f, kd = 0.f, kr = 0.f;
  int stage = 0, n = 0;  // 0 idle/held, 1 attack, 2 decay, 3 release
  void on(float pk, float A, float D, float S, double sr);
  void off(float R, double sr);
  inline float tick() {
    switch (stage) {
      case 1: v += inc; if (--n <= 0) { v = peak; stage = 2; } break;
      case 2: v += (sus - v) * kd; break;
      case 3: v += (0.f - v) * kr; break;
      default: break;
    }
    return v;
  }
};

class Engine {
 public:
  void prepare(double sampleRate);
  void reset();
  void setParams(const Params& p);
  void noteOn(int note, float vel, bool retrig);
  void noteOff();
  void setBend(float x01bipolar);  // -1..1
  void setModWheel(float x);       // 0..1
  // voice + drive + chorus + delay (stereo, replaces the buffers)
  void processVoice(float* L, float* R, int n);
  // master gain + compressor (after the reverb)
  void processMaster(float* L, float* R, int n);
  bool voiceActive() const { return aEnv_.v > 1e-5f || aEnv_.stage == 1; }

 private:
  Params p_;
  double sr_ = 48000.0;
  WaveBank bank_;
  bool first_ = true;

  // voice
  double phase_[3] = {0, 0, 0};
  float pitchCents_ = 0.f, pitchTarget_ = 0.f, pitchK_ = 1.f;
  Env fEnv_, aEnv_;
  float velPeak_ = 0.935f;
  float bendTarget_ = 0.f;
  float wheel_ = 0.f;
  Smooth sTune_, sBend_, sM1_, sM2_, sM3_, sMN_, sCut_, sRes_, sDrv_, sContour_, sModP_, sModF_;
  // noise / mod
  uint32_t rng_ = 22222u;
  float pink_[7] = {0, 0, 0, 0, 0, 0, 0};
  double lfoPhase_ = 0.0, shPhase_ = 0.0;
  float shValue_ = 0.f;
  Smooth sModRate_;
  // ladder
  float ly_[4] = {0, 0, 0, 0}, lt_[4] = {0, 0, 0, 0}, lpi_ = 0.f;
  // fx
  Smooth sDriveAmt_, sChWet_, sChDry_, sChRate_, sChDepth_, sDlTime_, sDlFb_, sDlWet_, sDlTone_, sVol_;
  float adaPrev_ = 0.f; float adaF_ = 0.f; bool adaInit_ = false;
  std::vector<float> chBuf_; int chPos_ = 0; double chPhase_ = 0.0;
  std::vector<float> dlBuf_[2]; int dlPos_ = 0; float dlZ_[2][2] = {{0, 0}, {0, 0}};
  float dlB_[5] = {1, 0, 0, 0, 0}; float dlCoefTone_ = -1.f;
  // compressor
  float cEnv_ = 0.f;

  inline float rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (float)rng_ * (2.0f / 4294967296.0f) - 1.0f; }
  void updateDelayFilter(float fc);
};

// static compressor curve measured from the browser's DynamicsCompressorNode (threshold -8 dB, knee 30, ratio 6)
float compressorOutDb(float inDb);

}  // namespace mm
