// Pattern storage for YM303: 32 patterns (groups I-IV x 1-8), each with an A and a B section of up to
// 16 steps, plus the track (song) list. Every value is an atomic so the audio thread can read while
// the UI / parameter thread edits (no locks).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace ym303
{
constexpr int kPatterns = 32, kSteps = 16, kMaxTrack = 64;

enum StepType { Rest = 0, Note = 1, Tie = 2 };

struct Step
{
    int type = Rest;          // Rest / Note / Tie
    int note = 0;             // 0..12  (C .. high C)
    bool up = false, dn = false, acc = false, sld = false;

    int midi() const { return 36 + note + (up ? 12 : 0) - (dn ? 12 : 0); }
};

class Bank
{
public:
    Bank() { loadDefaults(); }

    Step getStep (int pat, int sec, int i) const;
    void setStep (int pat, int sec, int i, const Step& s);
    int  getLen (int pat, int sec) const         { return lens[idx (pat, sec)].load (std::memory_order_relaxed); }
    void setLen (int pat, int sec, int len);
    bool hasNotes (int pat, int sec) const;      // any non-rest step inside the section length

    int  trackLength() const                     { return trackLen.load (std::memory_order_relaxed); }
    int  trackAt (int i) const                   { return track[(size_t) i].load (std::memory_order_relaxed); }
    void setTrack (const int* pats, int count);

    void clearAll();
    void loadDefaults();                         // the same demo patterns as the browser version
    void fillSection (int pat, int sec, const char* tokens);   // "C* C C^~ D# - = ..." (see Bank.cpp)

    std::string serialize() const;
    bool deserialize (const std::string& s);     // returns false (and changes nothing) if malformed

private:
    static int idx (int pat, int sec) { return (pat * 2 + sec); }
    static uint32_t pack (const Step&);
    static Step unpack (uint32_t);

    std::array<std::atomic<uint32_t>, kPatterns * 2 * kSteps> steps {};
    std::array<std::atomic<int>, kPatterns * 2> lens {};
    std::array<std::atomic<int>, kMaxTrack> track {};
    std::atomic<int> trackLen { 0 };
};
}
