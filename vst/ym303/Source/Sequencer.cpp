#include "Sequencer.h"
#include <algorithm>
#include <cmath>

namespace ym303
{
void Sequencer::reset()
{
    clock = None; pendingOff = -1; hold = false; seqLen = 0; posInternal = 0; countdown = 0;
    lastFired = 0; havePpq = false; lastStep = -1;
}

void Sequencer::begin (Clock c, EventList& out)
{
    if (clock == c) return;
    if (clock != None && hold) out.push ({ 0, false, 0, false, false });
    reset();
    clock = c;
}

void Sequencer::idle (int n, EventList& out)
{
    if (clock != None)
    {
        if (hold || pendingOff >= 0) out.push ({ 0, false, 0, false, false });
        reset();
    }
    blockStart += n;
}

void Sequencer::build (const Selection& sel)
{
    seqLen = 0;
    auto add = [this] (int p, int s)
    {
        const int L = bank.getLen (p, s);
        for (int i = 0; i < L && seqLen < (int) (sizeof (seq) / sizeof (seq[0])); ++i)
            seq[seqLen++] = (uint16_t) (p * 32 + s * 16 + i);
    };
    if (sel.mode == 1)
    {
        for (int k = 0; k < bank.trackLength(); ++k)
        {
            const int p = bank.trackAt (k);
            add (p, 0);
            if (bank.hasNotes (p, 1)) add (p, 1);
        }
    }
    else if (sel.sec == 0) add (sel.pat, 0);
    else if (sel.sec == 1) add (sel.pat, 1);
    else { add (sel.pat, 0); add (sel.pat, 1); }
}

void Sequencer::scheduleOff (double absPos, EventList& out)
{
    const double rel = absPos - (double) blockStart;
    if (rel < (double) blockLen) out.push ({ (int) std::max (0.0, std::floor (rel)), false, 0, false, false });
    else pendingOff = absPos;
}

void Sequencer::fire (int pos, double triggerAbs, double stepSamples, int offset, EventList& out)
{
    lastStep = pos;
    const uint16_t e = seq[pos], e2 = seq[(pos + 1) % seqLen];
    const Step st = bank.getStep (e / 32, (e / 16) & 1, e & 15);
    const Step ns = bank.getStep (e2 / 32, (e2 / 16) & 1, e2 & 15);
    const double offAt = triggerAbs + stepSamples * .56;
    if (st.type == Note)
    {
        pendingOff = -1;
        out.push ({ offset, true, st.midi(), st.acc, hold });
        const bool sustain = ns.type == Tie || (st.sld && ns.type == Note);
        if (! sustain) scheduleOff (offAt, out);
        hold = sustain;
    }
    else if (st.type == Tie)
    {
        if (hold)
        {
            const bool sustain = ns.type == Tie;
            if (! sustain) scheduleOff (offAt, out);
            hold = sustain;
        }
    }
    else
    {
        if (hold) out.push ({ offset, false, 0, false, false });
        hold = false;
    }
}

void Sequencer::runInternal (int n, double tempo, const Selection& sel, EventList& out)
{
    begin (Internal, out);
    blockLen = n;
    if (pendingOff >= 0 && pendingOff < (double) (blockStart + n)) { scheduleOff (pendingOff, out); pendingOff = -1; }
    const double stepSamples = sr * 15.0 / std::max (1.0, tempo);
    while (countdown < (double) n)
    {
        if (posInternal >= seqLen) { build (sel); posInternal = 0; }
        if (seqLen > 0)
        {
            const double t = std::max (0.0, countdown);
            fire (posInternal, (double) blockStart + t, stepSamples, (int) t, out);
            ++posInternal;
        }
        countdown += stepSamples;
    }
    countdown -= (double) n;
    blockStart += n;
}

void Sequencer::runHost (int n, double ppq0, double bpm, const Selection& sel, EventList& out)
{
    begin (Host, out);
    blockLen = n;
    if (pendingOff >= 0 && pendingOff < (double) (blockStart + n)) { scheduleOff (pendingOff, out); pendingOff = -1; }
    const double ppqPerSample = bpm / 60.0 / sr, stepSamples = sr * 15.0 / bpm;
    if (! havePpq || std::fabs (ppq0 - expectedPpq) > 1e-3)        // start, loop or jump in the DAW
    {
        lastFired = (int64_t) std::ceil (ppq0 * 4.0 - 1e-6) - 1;
        if (havePpq && hold) out.push ({ 0, false, 0, false, false });
        hold = false;
        havePpq = true;
    }
    for (;;)
    {
        const int64_t next = lastFired + 1;
        const double off = ((double) next / 4.0 - ppq0) / ppqPerSample;
        if (off >= (double) n) break;
        const double t = std::max (0.0, off);
        lastFired = next;
        if (seqLen == 0) build (sel);
        if (seqLen > 0)
        {
            int pos = (int) (((next % seqLen) + seqLen) % seqLen);
            if (pos == 0) { build (sel); if (seqLen > 0) pos = (int) (((next % seqLen) + seqLen) % seqLen); }
            if (seqLen > 0) fire (pos, (double) blockStart + t, stepSamples, std::min (n - 1, (int) std::ceil (t - 1e-9)), out);
        }
    }
    expectedPpq = ppq0 + (double) n * ppqPerSample;
    blockStart += n;
}
}
