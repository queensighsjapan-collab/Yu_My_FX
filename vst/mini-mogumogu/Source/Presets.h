// mini mogumogu - factory presets (same as the browser version). Unlisted parameters return to their defaults.
#pragma once

namespace mm {

struct PresetKV { const char* k; float v; };
struct Preset { const char* name; const PresetKV* kv; int n; };

#define KV_(...) __VA_ARGS__
static const PresetKV kP1[] = {{"o1range",2},{"o1wave",2},{"o2range",2},{"o2wave",2},{"o2tune",9},{"o3range",1},{"o3wave",3},{"o3tune",-5},{"m1",.8f},{"m2",.8f},{"m3",.55f},{"cutoff",260},{"emph",.35f},{"contour",.6f},{"kbd",1},{"fA",.002f},{"fD",.28f},{"fS",.12f},{"aA",.002f},{"aD",.45f},{"aS",.75f},{"fdrive",.35f},{"drive",.15f}};
static const PresetKV kP2[] = {{"o1range",3},{"o1wave",2},{"o2range",3},{"o2wave",2},{"o2tune",7},{"o3range",4},{"o3wave",3},{"o3tune",-7},{"m1",.7f},{"m2",.7f},{"m3",.25f},{"glide",.18f},{"cutoff",2200},{"emph",.3f},{"contour",.35f},{"kbd",2},{"fA",.01f},{"fD",.5f},{"fS",.5f},{"aA",.01f},{"aD",.6f},{"aS",.8f},{"modSrc",1},{"modRate",5.5f},{"modPitch",.08f},{"dlSync",2},{"dlFb",.4f},{"dlMix",.22f},{"rvMix",.2f},{"chMix",.25f}};
static const PresetKV kP3[] = {{"o1range",3},{"o1wave",2},{"o2range",3},{"o2wave",2},{"o2tune",6},{"o3range",2},{"o3wave",2},{"m1",.7f},{"m2",.7f},{"m3",.5f},{"cutoff",500},{"emph",.15f},{"contour",.55f},{"fA",.09f},{"fD",.35f},{"fS",.45f},{"aA",.06f},{"aD",.5f},{"aS",.85f},{"chMix",.3f}};
static const PresetKV kP4[] = {{"o1range",3},{"o1wave",4},{"o2range",4},{"o2wave",0},{"o2tune",3},{"m1",.8f},{"m2",.5f},{"cutoff",800},{"emph",.2f},{"contour",.7f},{"fA",.002f},{"fD",.18f},{"fS",0},{"aA",.002f},{"aD",.3f},{"aS",0},{"dlSync",2},{"dlFb",.35f},{"dlMix",.25f},{"rvMix",.25f}};
static const PresetKV kP5[] = {{"o1range",1},{"o1wave",0},{"o2range",2},{"o2wave",3},{"m1",.9f},{"m2",.35f},{"cutoff",180},{"emph",.2f},{"contour",.5f},{"fA",.002f},{"fD",.14f},{"fS",0},{"aA",.002f},{"aD",.22f},{"aS",0},{"drive",.2f}};
static const PresetKV kP6[] = {{"o1range",2},{"o1wave",2},{"m1",1},{"cutoff",400},{"emph",.85f},{"contour",.8f},{"fA",.002f},{"fD",.2f},{"fS",0},{"aA",.002f},{"aD",.2f},{"aS",.6f},{"glide",.1f},{"drive",.3f},{"fdrive",.5f}};
static const PresetKV kP7[] = {{"o1range",3},{"o1wave",2},{"o2range",4},{"o2wave",3},{"o2tune",4},{"m1",.7f},{"m2",.5f},{"cutoff",900},{"emph",.6f},{"contour",.2f},{"modSrc",3},{"modRate",9},{"modFilt",.55f},{"chMix",.4f},{"dlSync",1},{"dlFb",.55f},{"dlMix",.35f},{"rvMix",.35f},{"rvSize",.7f}};
static const PresetKV kP8[] = {{"o1range",2},{"o1wave",2},{"o2range",2},{"o2wave",3},{"o2tune",6},{"m1",.8f},{"m2",.6f},{"cutoff",350},{"emph",.5f},{"contour",.1f},{"modSrc",2},{"modRate",3.5f},{"modFilt",.75f},{"aS",.9f},{"fdrive",.4f}};
static const PresetKV kP9[] = {{"o1range",3},{"o1wave",1},{"o2range",3},{"o2wave",2},{"o2tune",8},{"o3range",4},{"o3wave",0},{"m1",.6f},{"m2",.5f},{"m3",.4f},{"cutoff",1100},{"emph",.1f},{"contour",.25f},{"fA",.6f},{"fD",1.5f},{"fS",.6f},{"aA",.5f},{"aD",1.5f},{"aS",.85f},{"chMix",.5f},{"chDepth",.6f},{"rvMix",.45f},{"rvSize",.7f}};

#define PN_(a) (int)(sizeof(a) / sizeof(a[0]))
static const Preset kPresets[] = {
  {"Init", nullptr, 0},
  {"Fat Bass", kP1, PN_(kP1)},
  {"Mogu Lead", kP2, PN_(kP2)},
  {"Brass", kP3, PN_(kP3)},
  {"Pluck", kP4, PN_(kP4)},
  {"Sub Thump", kP5, PN_(kP5)},
  {"Acid Squelch", kP6, PN_(kP6)},
  {"Sci-Fi S&H", kP7, PN_(kP7)},
  {"Wobble", kP8, PN_(kP8)},
  {"Soft Pad", kP9, PN_(kP9)},
};
static constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

}  // namespace mm
