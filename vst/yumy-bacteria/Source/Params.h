// Plug-in parameter metadata that is not part of the engine tables (labels for choice parameters, matrix, LFO destinations).
#pragma once
namespace yb
{
struct OptMeta { const char* label; const char* choices; bool isBool; };   // order = YB_OPT_LIST
static const OptMeta kOptMeta[] = {
    { "Vocoder Bands", "8|16|32", false },
    { "Unison", "OFF|2 VOICES|3 VOICES|4 VOICES|5 VOICES|6 VOICES|7 VOICES", false },
    { "Osc2 Mode", "CLASSIC|WAVETABLE|HYPERSAW", false },
    { "LFO1 Sync", "FREE|1 BAR|1/2|1/4|1/8|1/16|1/8 TRIPLET|1/16 TRIPLET|2 BARS", false },
    { "LFO2 Sync", "FREE|1 BAR|1/2|1/4|1/8|1/16|1/8 TRIPLET|1/16 TRIPLET|2 BARS", false },
    { "LFO3 Sync", "FREE|1 BAR|1/2|1/4|1/8|1/16|1/8 TRIPLET|1/16 TRIPLET|2 BARS", false },
    { "LFO1 Key Sync", "", true }, { "LFO2 Key Sync", "", true }, { "LFO3 Key Sync", "", true },
    { "Mono", "OFF|LEGATO|RETRIGGER", false },
    { "Osc1 Mode", "CLASSIC|HYPERSAW|WAVETABLE", false },
    { "Osc Sync", "", true }, { "Ring Mod", "", true },
    { "Filter1 Mode", "LOWPASS|HIGHPASS|BANDPASS|BANDSTOP", false },
    { "Filter2 Mode", "LOWPASS|HIGHPASS|BANDPASS|BANDSTOP", false },
    { "Filter Routing", "SERIAL|PARALLEL|SPLIT L/R", false },
    { "Arp On", "", true }, { "Arp Hold", "", true },
    { "Arp Mode", "UP|DOWN|UP & DOWN|AS PLAYED|RANDOM|CHORD", false },
    { "Arp Resolution", "1/4|1/8|1/8 TRIPLET|1/16|1/16 TRIPLET|1/32", false },
    { "Arp Note Length", "STACCATO|SHORT|NORMAL|LONG|LEGATO", false },
    { "Arp Pattern", "STRAIGHT|ACCENT 1/4|OFFBEAT|GALLOP|DOTTED 3+3|SYNCOPATED", false },
    { "Chorus Type", "CHORUS|FLANGER", false },
    { "Delay Sync", "FREE (KNOB)|1/4|1/8|DOTTED 1/8|1/16", false },
    { "Reverb Size", "SMALL|MEDIUM|LARGE", false },
};
static const char* const kSources = "OFF|LFO 1|LFO 2|LFO 3|FILTER ENV|AMP ENV|VELOCITY|MOD WHEEL|KEYTRACK|RANDOM|SOFT 1|SOFT 2|AFTERTOUCH|PITCH BEND|LFO 1 (+)|LFO 2 (+)|LFO 3 (+)";
static const char* const kDests = "OFF|OSC1 PITCH|OSC2 PITCH|OSC1+2 PITCH|OSC1 SHAPE|OSC2 SHAPE|PULSE WIDTH|FM AMOUNT|OSC BALANCE|SUB LEVEL|CUTOFF 1|CUTOFF 2|CUTOFF 1+2|RESONANCE|FILTER BAL|AMP LEVEL|PAN|LFO1 RATE|LFO2 RATE|LFO3 RATE|SATURATION|NOISE LEVEL|DELAY SEND|REVERB SEND|CHORUS MIX|DISTORTION|PHASER MIX|FORMANT VOWEL";
}
