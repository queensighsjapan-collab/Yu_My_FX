// YM2000 parameter table (ids match the HTML version: synthesizer/ms2000-synth.html)
#pragma once
#include <cstddef>

namespace ym
{
enum class Kind { Float, Int, Bool, Choice };

struct PDef
{
    const char* id;
    const char* name;     // host-visible name
    const char* section;  // parameter group
    Kind kind;
    float mn, mx, def;
    const char* choices;  // '|' separated, Choice only
};

#define SRC_LABELS "EG1|EG2|LFO1|LFO2|VELOCITY|P.BEND|MOD WHL|KEY TRK"
#define DST_LABELS "PITCH|OSC2 PCH|OSC1 CT1|NOISE|CUTOFF|AMP|PAN|LFO2 FRQ"

inline const PDef kParams[] = {
    // MASTER
    { "vol", "Master Volume", "MASTER", Kind::Float, 0, 127, 100, nullptr },
    { "ai1", "Audio In 1 (Vocoder Mod)", "MASTER", Kind::Float, 0, 127, 80, nullptr },
    { "ai2", "Audio In 2 (OSC1 Audio In)", "MASTER", Kind::Float, 0, 127, 0, nullptr },
    // OSC 1
    { "o1w", "OSC1 Wave", "OSC1", Kind::Choice, 0, 7, 0, "SAW|PULSE|TRI|SIN|VOX|DWGS|NOISE|AUDIO IN" },
    { "o1c1", "OSC1 Ctrl 1", "OSC1", Kind::Float, 0, 127, 0, nullptr },
    { "o1c2", "OSC1 Ctrl 2", "OSC1", Kind::Float, 0, 127, 0, nullptr },
    { "dwgs", "OSC1 DWGS Wave", "OSC1", Kind::Int, 1, 64, 1, nullptr },
    // OSC 2
    { "o2w", "OSC2 Wave", "OSC2", Kind::Choice, 0, 2, 0, "SAW|SQR|TRI" },
    { "o2m", "OSC2 Mod Select", "OSC2", Kind::Choice, 0, 3, 0, "OFF|RING|SYNC|R+S" },
    { "o2s", "OSC2 Semitone", "OSC2", Kind::Int, -24, 24, 0, nullptr },
    { "o2t", "OSC2 Tune", "OSC2", Kind::Float, -63, 63, 0, nullptr },
    // MIXER
    { "mx1", "Mixer OSC1", "MIXER", Kind::Float, 0, 127, 127, nullptr },
    { "mx2", "Mixer OSC2", "MIXER", Kind::Float, 0, 127, 0, nullptr },
    { "mxn", "Mixer Noise", "MIXER", Kind::Float, 0, 127, 0, nullptr },
    // FILTER
    { "ft", "Filter Type", "FILTER", Kind::Choice, 0, 3, 0, "24 LPF|12 LPF|BPF|HPF" },
    { "cut", "Filter Cutoff", "FILTER", Kind::Float, 0, 127, 90, nullptr },
    { "res", "Filter Resonance", "FILTER", Kind::Float, 0, 127, 20, nullptr },
    { "f1", "Filter EG1 Int", "FILTER", Kind::Float, -63, 63, 0, nullptr },
    { "fv", "Filter Velocity", "FILTER", Kind::Float, -63, 63, 0, nullptr },
    { "fk", "Filter Kbd Track", "FILTER", Kind::Float, -63, 63, 0, nullptr },
    // AMP
    { "al", "Amp Level", "AMP", Kind::Float, 0, 127, 110, nullptr },
    { "ap", "Amp Pan", "AMP", Kind::Float, 0, 127, 64, nullptr },
    { "dist", "Amp Distortion", "AMP", Kind::Bool, 0, 1, 0, nullptr },
    { "ak", "Amp Kbd Track", "AMP", Kind::Float, -63, 63, 0, nullptr },
    // EG 1 / EG 2
    { "e1a", "EG1 Attack", "EG1", Kind::Float, 0, 127, 5, nullptr },
    { "e1d", "EG1 Decay", "EG1", Kind::Float, 0, 127, 60, nullptr },
    { "e1s", "EG1 Sustain", "EG1", Kind::Float, 0, 127, 90, nullptr },
    { "e1r", "EG1 Release", "EG1", Kind::Float, 0, 127, 40, nullptr },
    { "e2a", "EG2 Attack", "EG2", Kind::Float, 0, 127, 0, nullptr },
    { "e2d", "EG2 Decay", "EG2", Kind::Float, 0, 127, 50, nullptr },
    { "e2s", "EG2 Sustain", "EG2", Kind::Float, 0, 127, 127, nullptr },
    { "e2r", "EG2 Release", "EG2", Kind::Float, 0, 127, 30, nullptr },
    // LFO 1 / LFO 2
    { "l1w", "LFO1 Wave", "LFO1", Kind::Choice, 0, 3, 2, "SAW|SQR|TRI|S&H" },
    { "l1f", "LFO1 Freq", "LFO1", Kind::Float, 0, 127, 70, nullptr },
    { "l1k", "LFO1 Key Sync", "LFO1", Kind::Bool, 0, 1, 0, nullptr },
    { "l1t", "LFO1 Tempo Sync", "LFO1", Kind::Bool, 0, 1, 0, nullptr },
    { "l2w", "LFO2 Wave", "LFO2", Kind::Choice, 0, 3, 2, "SAW|SQR+|SIN|S&H" },
    { "l2f", "LFO2 Freq", "LFO2", Kind::Float, 0, 127, 40, nullptr },
    { "l2k", "LFO2 Key Sync", "LFO2", Kind::Bool, 0, 1, 0, nullptr },
    // VIRTUAL PATCH
    { "vs1", "VP1 Source", "VIRTUAL PATCH", Kind::Choice, 0, 7, 0, SRC_LABELS },
    { "vd1", "VP1 Destination", "VIRTUAL PATCH", Kind::Choice, 0, 7, 4, DST_LABELS },
    { "vi1", "VP1 Intensity", "VIRTUAL PATCH", Kind::Float, -63, 63, 0, nullptr },
    { "vs2", "VP2 Source", "VIRTUAL PATCH", Kind::Choice, 0, 7, 1, SRC_LABELS },
    { "vd2", "VP2 Destination", "VIRTUAL PATCH", Kind::Choice, 0, 7, 0, DST_LABELS },
    { "vi2", "VP2 Intensity", "VIRTUAL PATCH", Kind::Float, -63, 63, 0, nullptr },
    { "vs3", "VP3 Source", "VIRTUAL PATCH", Kind::Choice, 0, 7, 2, SRC_LABELS },
    { "vd3", "VP3 Destination", "VIRTUAL PATCH", Kind::Choice, 0, 7, 5, DST_LABELS },
    { "vi3", "VP3 Intensity", "VIRTUAL PATCH", Kind::Float, -63, 63, 0, nullptr },
    { "vs4", "VP4 Source", "VIRTUAL PATCH", Kind::Choice, 0, 7, 3, SRC_LABELS },
    { "vd4", "VP4 Destination", "VIRTUAL PATCH", Kind::Choice, 0, 7, 6, DST_LABELS },
    { "vi4", "VP4 Intensity", "VIRTUAL PATCH", Kind::Float, -63, 63, 0, nullptr },
    // MOD FX / DELAY FX
    { "mft", "Mod FX Type", "MOD FX", Kind::Choice, 0, 2, 0, "CHO/FLG|ENS|PHASER" },
    { "mfs", "Mod FX Speed", "MOD FX", Kind::Float, 0, 127, 64, nullptr },
    { "mfd", "Mod FX Depth", "MOD FX", Kind::Float, 0, 127, 0, nullptr },
    { "dt", "Delay Type", "DELAY FX", Kind::Choice, 0, 2, 0, "STEREO|CROSS|L/R" },
    { "dtm", "Delay Time", "DELAY FX", Kind::Float, 0, 127, 64, nullptr },
    { "ddp", "Delay Depth", "DELAY FX", Kind::Float, 0, 127, 0, nullptr },
    // PITCH
    { "ptn", "Pitch Tune", "PITCH", Kind::Float, -50, 50, 0, nullptr },
    { "pbr", "Pitch Bend Range", "PITCH", Kind::Int, -12, 12, 2, nullptr },
    { "ptr", "Pitch Transpose", "PITCH", Kind::Int, -24, 24, 0, nullptr },
    { "pvi", "Pitch Vibrato Int", "PITCH", Kind::Float, -63, 63, 0, nullptr },
    { "ppo", "Pitch Portamento", "PITCH", Kind::Float, 0, 127, 0, nullptr },
    // EQ
    { "eqhf", "EQ Hi Freq", "EQ", Kind::Float, 0, 127, 80, nullptr },
    { "eqhg", "EQ Hi Gain (dB)", "EQ", Kind::Int, -12, 12, 0, nullptr },
    { "eqlf", "EQ Low Freq", "EQ", Kind::Float, 0, 127, 40, nullptr },
    { "eqlg", "EQ Low Gain (dB)", "EQ", Kind::Int, -12, 12, 0, nullptr },
    // ARPEGGIATOR
    { "arpon", "Arp On", "ARPEGGIATOR", Kind::Bool, 0, 1, 0, nullptr },
    { "arplt", "Arp Latch", "ARPEGGIATOR", Kind::Bool, 0, 1, 0, nullptr },
    { "arpt", "Arp Type", "ARPEGGIATOR", Kind::Choice, 0, 5, 0, "UP|DOWN|ALT1|ALT2|RANDOM|TRIGGER" },
    { "arpr", "Arp Range", "ARPEGGIATOR", Kind::Int, 1, 4, 1, nullptr },
    { "arpg", "Arp Gate Time", "ARPEGGIATOR", Kind::Float, 0, 127, 64, nullptr },
    { "arptm", "Arp / Seq Tempo (BPM)", "ARPEGGIATOR", Kind::Int, 20, 300, 120, nullptr },
    // VOCODER
    { "vmode", "Voice Mode", "VOCODER", Kind::Choice, 0, 1, 0, "SINGLE|VOCODER" },
    { "vfs", "Vocoder Formant Shift", "VOCODER", Kind::Int, -2, 2, 0, nullptr },
    { "vcl", "Vocoder Clarity", "VOCODER", Kind::Float, 0, 127, 30, nullptr },
    // MOD SEQUENCE
    { "msq", "Mod Seq On", "MOD SEQ", Kind::Bool, 0, 1, 0, nullptr },
    { "msk", "Mod Seq Key Sync", "MOD SEQ", Kind::Bool, 0, 1, 0, nullptr },
};
inline constexpr size_t kNumParams = sizeof (kParams) / sizeof (kParams[0]);

// 16-step arrays (ids: seq1..seq16, vlev1..vlev16, vpan1..vpan16)
inline const int kSeqDefaults[16] = { 64, 96, 40, 112, 64, 24, 100, 72, 50, 120, 36, 88, 64, 104, 28, 80 };

// Factory programs (same as FACTORY in the HTML version). Unlisted parameters use their defaults.
struct PV { const char* id; float v; };
struct Factory { const char* name; const PV* v; int n; };

inline const PV kFInit[] = { { "vol", 100 } };
inline const PV kFBass[] = { { "o1w", 0 }, { "o1c1", 40 }, { "o1c2", 60 }, { "o2w", 0 }, { "o2t", 10 }, { "mx1", 110 }, { "mx2", 90 }, { "ft", 0 },
    { "cut", 45 }, { "res", 30 }, { "f1", 35 }, { "e1a", 0 }, { "e1d", 50 }, { "e1s", 20 }, { "e1r", 20 }, { "e2a", 0 }, { "e2d", 60 },
    { "e2s", 100 }, { "e2r", 20 }, { "ptr", -12 } };
inline const PV kFPad[] = { { "o1w", 0 }, { "o1c1", 30 }, { "o2w", 0 }, { "o2t", 12 }, { "mx1", 100 }, { "mx2", 100 }, { "ft", 1 }, { "cut", 70 },
    { "res", 15 }, { "f1", 10 }, { "e1a", 60 }, { "e1d", 70 }, { "e1s", 90 }, { "e1r", 70 }, { "e2a", 70 }, { "e2d", 70 }, { "e2s", 110 },
    { "e2r", 80 }, { "mft", 1 }, { "mfs", 40 }, { "mfd", 70 }, { "dt", 0 }, { "dtm", 70 }, { "ddp", 40 } };
inline const PV kFSync[] = { { "o1w", 1 }, { "o1c1", 60 }, { "o1c2", 40 }, { "o2w", 1 }, { "o2s", 7 }, { "o2m", 2 }, { "mx1", 110 }, { "mx2", 40 },
    { "ft", 0 }, { "cut", 95 }, { "res", 40 }, { "e2a", 0 }, { "e2d", 50 }, { "e2s", 127 }, { "e2r", 30 }, { "pvi", 20 }, { "ppo", 40 } };
inline const PV kFArp[] = { { "o1w", 0 }, { "ft", 0 }, { "cut", 55 }, { "res", 50 }, { "f1", 40 }, { "e1a", 0 }, { "e1d", 30 }, { "e1s", 0 },
    { "e1r", 20 }, { "e2a", 0 }, { "e2d", 40 }, { "e2s", 60 }, { "e2r", 30 }, { "arpon", 1 }, { "arpt", 0 }, { "arpr", 2 }, { "arpg", 40 },
    { "arptm", 125 }, { "dt", 0 }, { "dtm", 60 }, { "ddp", 40 } };
inline const PV kFVox[] = { { "o1w", 4 }, { "o1c1", 64 }, { "o1c2", 0 }, { "l1w", 2 }, { "l1f", 55 }, { "vs1", 2 }, { "vd1", 2 }, { "vi1", 50 },
    { "e2a", 5 }, { "e2d", 50 }, { "e2s", 127 }, { "e2r", 40 } };
inline const PV kFVoc[] = { { "vmode", 1 }, { "o1w", 0 }, { "o1c1", 25 }, { "o2w", 0 }, { "o2t", 15 }, { "mx1", 100 }, { "mx2", 100 }, { "ft", 0 },
    { "cut", 110 }, { "res", 0 }, { "e2a", 0 }, { "e2s", 127 }, { "e2r", 30 }, { "ai1", 80 }, { "vcl", 40 }, { "vfs", 0 } };
inline const PV kFSeq[] = { { "o1w", 0 }, { "o1c1", 35 }, { "ft", 0 }, { "cut", 60 }, { "res", 40 }, { "msq", 1 }, { "msk", 1 }, { "arptm", 120 } };

inline const Factory kFactory[] = {
    { "Init Prog", kFInit, 1 },
    { "Bass", kFBass, (int) (sizeof (kFBass) / sizeof (PV)) },
    { "Pad", kFPad, (int) (sizeof (kFPad) / sizeof (PV)) },
    { "Sync Lead", kFSync, (int) (sizeof (kFSync) / sizeof (PV)) },
    { "Arp Pluck", kFArp, (int) (sizeof (kFArp) / sizeof (PV)) },
    { "Vox Lead", kFVox, (int) (sizeof (kFVox) / sizeof (PV)) },
    { "Vocoder", kFVoc, (int) (sizeof (kFVoc) / sizeof (PV)) },
    { "Mod Seq", kFSeq, (int) (sizeof (kFSeq) / sizeof (PV)) },
};
inline constexpr int kNumFactory = (int) (sizeof (kFactory) / sizeof (kFactory[0]));
} // namespace ym
