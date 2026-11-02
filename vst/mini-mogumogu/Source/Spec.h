// mini mogumogu - parameter table shared by the processor and the editor (ids / ranges mirror the browser version)
#pragma once
#include <cmath>
#include <cstring>

namespace mm {

struct PSpec {
  const char* id;
  const char* name;
  float mn, mx, def;
  bool log;
  float step;           // 0 = continuous
  const char* opts;     // non-null: choice parameter, options separated by '|'
};

#define K_(id, name, mn, mx, def, log, step) {id, name, mn, mx, def, log, step, nullptr}
#define C_(id, name, def, opts, n) {id, name, 0.f, (float)((n) - 1), (float)(def), false, 1.f, opts}

static const PSpec kSpecs[] = {
  K_("vol", "Volume", 0, 1, 0.7f, false, 0),
  K_("tune", "Tune", -100, 100, 0, false, 1),
  K_("glide", "Glide", 0, 1, 0, false, 0),
  C_("multi", "Trigger", 0, "LEGATO|MULTI", 2),
  C_("bendRange", "Bend Range", 0, "+-2|+-5|+-12", 3),
  C_("o1range", "Osc1 Range", 3, "LO|32'|16'|8'|4'|2'", 6),
  C_("o1wave", "Osc1 Wave", 2, "TRI|SHK|SAW|SQR|PLS1|PLS2", 6),
  C_("o2range", "Osc2 Range", 3, "LO|32'|16'|8'|4'|2'", 6),
  C_("o2wave", "Osc2 Wave", 2, "TRI|SHK|SAW|SQR|PLS1|PLS2", 6),
  K_("o2tune", "Osc2 Tune", -700, 700, 5, false, 1),
  C_("o3range", "Osc3 Range", 3, "LO|32'|16'|8'|4'|2'", 6),
  C_("o3wave", "Osc3 Wave", 3, "TRI|SHK|SAW|SQR|PLS1|PLS2", 6),
  K_("o3tune", "Osc3 Tune", -700, 700, 0, false, 1),
  C_("o3kbd", "Osc3 Kbd Ctl", 1, "OFF|ON", 2),
  K_("m1", "Mix Osc1", 0, 1, 0.8f, false, 0),
  K_("m2", "Mix Osc2", 0, 1, 0, false, 0),
  K_("m3", "Mix Osc3", 0, 1, 0, false, 0),
  K_("mN", "Mix Noise", 0, 1, 0, false, 0),
  C_("noiseType", "Noise Type", 0, "WHITE|PINK", 2),
  K_("cutoff", "Cutoff", 40, 16000, 1200, true, 0),
  K_("emph", "Emphasis", 0, 1, 0.1f, false, 0),
  K_("contour", "Contour", 0, 1, 0.4f, false, 0),
  K_("fdrive", "Filter Drive", 0, 1, 0.2f, false, 0),
  C_("kbd", "Kbd Track", 1, "OFF|1/3|2/3|FULL", 4),
  K_("fA", "Filter Attack", 0.002f, 10, 0.005f, true, 0),
  K_("fD", "Filter Decay", 0.005f, 10, 0.4f, true, 0),
  K_("fS", "Filter Sustain", 0, 1, 0.2f, false, 0),
  K_("aA", "Amp Attack", 0.002f, 10, 0.005f, true, 0),
  K_("aD", "Amp Decay", 0.005f, 10, 0.4f, true, 0),
  K_("aS", "Amp Sustain", 0, 1, 0.7f, false, 0),
  C_("rel", "Release", 1, "SHORT|= DECAY", 2),
  C_("modSrc", "Mod Source", 1, "OSC3|LFO TRI|LFO SQR|S&H|NOISE", 5),
  K_("modRate", "Mod Rate", 0.1f, 40, 5, true, 0),
  K_("modPitch", "Mod Pitch", 0, 1, 0, false, 0),
  K_("modFilt", "Mod Filter", 0, 1, 0, false, 0),
  K_("drive", "Drive", 0, 1, 0, false, 0),
  K_("chRate", "Chorus Rate", 0.1f, 6, 0.8f, true, 0),
  K_("chDepth", "Chorus Depth", 0, 1, 0.5f, false, 0),
  K_("chMix", "Chorus Mix", 0, 1, 0, false, 0),
  K_("dlTime", "Delay Time", 0.02f, 1.2f, 0.3f, true, 0),
  K_("dlFb", "Delay Feedback", 0, 0.92f, 0.35f, false, 0),
  K_("dlTone", "Delay Tone", 500, 12000, 4000, true, 0),
  K_("dlMix", "Delay Mix", 0, 1, 0, false, 0),
  C_("dlSync", "Delay Sync", 0, "FREE|1/8|1/8.|1/4", 4),
  K_("rvSize", "Reverb Size", 0.1f, 1, 0.5f, false, 0),
  K_("rvMix", "Reverb Mix", 0, 1, 0, false, 0),
  K_("sqBpm", "Seq Tempo", 40, 240, 110, false, 1),
  K_("sqLen", "Seq Length", 1, 16, 16, false, 1),
  K_("sqGate", "Seq Gate", 0.1f, 1, 0.55f, false, 0),
  K_("sqSwing", "Seq Swing", 0, 0.6f, 0, false, 0),
  C_("sqRate", "Seq Rate", 1, "1/8|1/16|1/32", 3),
  C_("sqDir", "Seq Direction", 0, "FWD|REV|PING|RND", 4),
  C_("sqKey", "Seq Key>Trans", 1, "OFF|ON", 2),
  C_("hostSync", "Host Sync", 1, "OFF|ON", 2),
  C_("run", "Seq Run", 0, "OFF|ON", 2),
};
static constexpr int kNumSpecs = (int)(sizeof(kSpecs) / sizeof(kSpecs[0]));

inline int specIndex(const char* id) {
  for (int i = 0; i < kNumSpecs; ++i) if (std::strcmp(kSpecs[i].id, id) == 0) return i;
  return -1;
}

// the default step pattern of the browser version
static const int kDefNote[16] = {36, 36, 48, 36, 39, 36, 43, 46, 36, 36, 48, 36, 41, 39, 43, 34};
static const int kDefOn[16]   = {1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1};

}  // namespace mm
