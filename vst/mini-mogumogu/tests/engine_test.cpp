// Offline renderer for the C++ engine. Reads a script (same format as ref_render.py) and writes interleaved float32 stereo.
//   usage: engine_test script.txt out.f32
#include "../Source/Engine.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace mm;

static void setKey(Params& p, const std::string& k, double v) {
  auto I = [&](int& x) { x = (int)v; };
  auto F = [&](float& x) { x = (float)v; };
  if (k == "vol") F(p.vol); else if (k == "tune") F(p.tune); else if (k == "glide") F(p.glide);
  else if (k == "multi") I(p.multi); else if (k == "bendRange") I(p.bendRange);
  else if (k == "o1range") I(p.range[0]); else if (k == "o2range") I(p.range[1]); else if (k == "o3range") I(p.range[2]);
  else if (k == "o1wave") I(p.wave[0]); else if (k == "o2wave") I(p.wave[1]); else if (k == "o3wave") I(p.wave[2]);
  else if (k == "o2tune") F(p.otune[1]); else if (k == "o3tune") F(p.otune[2]); else if (k == "o3kbd") I(p.o3kbd);
  else if (k == "m1") F(p.m1); else if (k == "m2") F(p.m2); else if (k == "m3") F(p.m3); else if (k == "mN") F(p.mN);
  else if (k == "noiseType") I(p.noiseType);
  else if (k == "cutoff") F(p.cutoff); else if (k == "emph") F(p.emph); else if (k == "contour") F(p.contour);
  else if (k == "fdrive") F(p.fdrive); else if (k == "kbd") I(p.kbd);
  else if (k == "fA") F(p.fA); else if (k == "fD") F(p.fD); else if (k == "fS") F(p.fS);
  else if (k == "aA") F(p.aA); else if (k == "aD") F(p.aD); else if (k == "aS") F(p.aS); else if (k == "rel") I(p.rel);
  else if (k == "modSrc") I(p.modSrc); else if (k == "modRate") F(p.modRate); else if (k == "modPitch") F(p.modPitch); else if (k == "modFilt") F(p.modFilt);
  else if (k == "drive") F(p.drive); else if (k == "chRate") F(p.chRate); else if (k == "chDepth") F(p.chDepth); else if (k == "chMix") F(p.chMix);
  else if (k == "dlTime") F(p.dlTime); else if (k == "dlFb") F(p.dlFb); else if (k == "dlTone") F(p.dlTone); else if (k == "dlMix") F(p.dlMix);
  else if (k == "dlSync") I(p.dlSync); else if (k == "rvSize") F(p.rvSize); else if (k == "rvMix") F(p.rvMix);
  else if (k == "sqBpm") p.bpm = v;
  else { fprintf(stderr, "unknown key %s\n", k.c_str()); exit(2); }
}

struct Ev { long pos; int type; int note; float vel; bool retrig; float x; };

int main(int argc, char** argv) {
  if (argc < 3) return 1;
  std::ifstream in(argv[1]);
  std::string line;
  double sr = 48000, dur = 1.0;
  Params p;
  std::vector<Ev> evs;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string c;
    if (!(ss >> c) || c[0] == '#') continue;
    if (c == "sr") ss >> sr;
    else if (c == "dur") ss >> dur;
    else if (c == "set") { std::string k; double v; ss >> k >> v; setKey(p, k, v); }
    else if (c == "on") { double t; int n; double vel; int r; ss >> t >> n >> vel >> r; evs.push_back({std::lround(t * sr), 0, n, (float)vel, r != 0, 0}); }
    else if (c == "off") { double t; ss >> t; evs.push_back({std::lround(t * sr), 1, 0, 0, false, 0}); }
    else if (c == "bend") { double t, x; ss >> t >> x; evs.push_back({std::lround(t * sr), 2, 0, 0, false, (float)x}); }
    else if (c == "wheel") { double t, x; ss >> t >> x; evs.push_back({std::lround(t * sr), 3, 0, 0, false, (float)x}); }
  }
  std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.pos < b.pos; });
  Engine e;
  e.prepare(sr);
  e.setParams(p);
  const long total = (long)std::ceil(sr * dur);
  std::vector<float> L(total), R(total);
  long pos = 0; size_t ei = 0;
  while (pos < total) {
    long next = total;
    if (ei < evs.size()) next = std::min(next, std::max(evs[ei].pos, pos));
    while (ei < evs.size() && evs[ei].pos <= pos) {
      const Ev& v = evs[ei++];
      if (v.type == 0) e.noteOn(v.note, v.vel, v.retrig);
      else if (v.type == 1) e.noteOff();
      else if (v.type == 2) e.setBend(v.x);
      else e.setModWheel(v.x);
    }
    long end = total;
    if (ei < evs.size()) end = std::min(end, evs[ei].pos);
    end = std::min(end, pos + 128);
    if (end <= pos) end = pos + 1;
    e.processVoice(&L[pos], &R[pos], (int)(end - pos));
    e.processMaster(&L[pos], &R[pos], (int)(end - pos));
    pos = end;
  }
  FILE* f = fopen(argv[2], "wb");
  for (long i = 0; i < total; ++i) { float s[2] = {L[i], R[i]}; fwrite(s, 4, 2, f); }
  fclose(f);
  return 0;
}
