// YM303 step sequencer. Plain C++ (no JUCE) so it can be tested against the browser version.
// It turns "time" (internal tempo clock or a DAW's ppq position) into note events with sample offsets.
// The note / tie / slide / rest logic is the same as schedule() in synthesizer/yu-my-303.html.
#pragma once
#include <cstdint>
#include "Bank.h"

namespace ym303
{
struct Event
{
    int offset = 0;          // sample offset inside the block
    bool on = false;         // true: note on, false: gate off
    int note = 0;            // MIDI note (note on)
    bool accent = false;
    bool slide = false;      // legato: glide from the still-open previous note
};

class EventList
{
public:
    static constexpr int kCap = 256;
    void clear() { n = 0; }
    int size() const { return n; }
    const Event& operator[] (int i) const { return e[i]; }
    void push (const Event& ev)       // keeps the list sorted by offset (stable)
    {
        if (n >= kCap) return;
        int i = n++;
        while (i > 0 && e[i - 1].offset > ev.offset) { e[i] = e[i - 1]; --i; }
        e[i] = ev;
    }
private:
    Event e[kCap];
    int n = 0;
};

struct Selection
{
    int mode = 0;     // 0 pattern, 1 track
    int pat = 0;      // 0..31  (group * 8 + pattern - 1)
    int sec = 0;      // 0 A, 1 B, 2 A->B
};

class Sequencer
{
public:
    Sequencer (const Bank& b) : bank (b) {}
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset();

    // internal clock: steps run at `tempo` BPM (16th notes) starting with the first call
    void runInternal (int numSamples, double tempo, const Selection& sel, EventList& out);
    // DAW clock: steps lock to the 16th-note grid of the host's ppq position
    void runHost (int numSamples, double ppq, double bpm, const Selection& sel, EventList& out);
    // not running: closes an open gate once, otherwise does nothing
    void idle (int numSamples, EventList& out);

    int currentStep() const { return lastStep; }     // position inside the flat sequence, -1 when stopped

private:
    enum Clock { None, Internal, Host };

    void build (const Selection& sel);
    void fire (int pos, double triggerAbs, double stepSamples, int offset, EventList& out);
    void scheduleOff (double absPos, EventList& out);
    void begin (Clock c, EventList& out);

    const Bank& bank;
    double sr = 48000;
    Clock clock = None;
    int64_t blockStart = 0;        // absolute sample index of the current block
    int blockLen = 0;
    double pendingOff = -1;        // absolute sample position of a gate-off that falls into a later block
    bool hold = false;             // previous step kept the gate open (tie / slide)
    uint16_t seq[kMaxTrack * kSteps * 2] = {};
    int seqLen = 0;
    int posInternal = 0;
    double countdown = 0;          // internal clock: samples until the next step
    int64_t lastFired = 0;
    double expectedPpq = 0;
    bool havePpq = false;
    int lastStep = -1;
};
}
