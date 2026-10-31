// Compares the C++ sequencer with the event list the browser version's real scheduler produced
// (tests/ref_seq.js). Runs both the internal clock and the DAW (ppq) clock with irregular block sizes.
#include "../Source/Sequencer.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

using namespace ym303;
struct Ev { double t; bool on; int note; bool acc, sld; };

static std::vector<Ev> runCase (const Bank& bank, const Selection& sel, double bpm, double Tsec, bool host, const std::vector<int>& blocks)
{
    const double sr = 48000.0;
    Sequencer sq (bank); sq.prepare (sr);
    std::vector<Ev> out; EventList el;
    int64_t pos = 0; size_t bi = 0; double ppq = 0;
    while ((double) pos / sr < Tsec)
    {
        const int n = blocks[bi++ % blocks.size()];
        el.clear();
        if (host) sq.runHost (n, ppq, bpm, sel, el); else sq.runInternal (n, bpm, sel, el);
        for (int i = 0; i < el.size(); ++i) out.push_back ({ (double) (pos + el[i].offset) / sr, el[i].on, el[i].note, el[i].accent, el[i].slide });
        pos += n; ppq += n * bpm / 60.0 / sr;
    }
    return out;
}

int main (int argc, char** argv)
{
    if (argc < 2) return 1;
    std::ifstream in (argv[1]);
    std::string line; int fails = 0; Bank bank;
    const std::vector<std::vector<int>> blockSets = { { 512 }, { 64 }, { 333, 1024, 77, 4096 }, { 2048, 480 } };
    while (std::getline (in, line))
    {
        if (line.rfind ("case|", 0) != 0) continue;
        std::stringstream ss (line); std::string tok; std::vector<std::string> f;
        while (std::getline (ss, tok, '|')) f.push_back (tok);
        Selection sel; sel.mode = std::stoi (f[2]); sel.pat = std::stoi (f[3]); sel.sec = std::stoi (f[4]);
        const double bpm = std::stod (f[5]), T = std::stod (f[6]); const int cnt = std::stoi (f[7]);
        std::vector<Ev> ref;
        for (int i = 0; i < cnt; ++i)
        {
            std::getline (in, line); std::stringstream es (line); Ev e; int g, a, s;
            es >> e.t >> g >> e.note >> a >> s; e.on = g; e.acc = a; e.sld = s; ref.push_back (e);
        }
        double limit = T - 0.5;            // the browser scheduler looks ahead 150 ms past its clock
        {   // put the cut-off in the middle of a gap between two reference events so 1-sample rounding cannot straddle it
            double lastBelow = 0, firstAbove = limit;
            for (auto& e : ref) { if (e.t < limit) lastBelow = e.t; else { firstAbove = e.t; break; } }
            limit = (lastBelow + firstAbove) / 2;
        }
        std::vector<Ev> rr; for (auto& e : ref) if (e.t < limit) rr.push_back (e);
        for (int host = 0; host < 2; ++host)
            for (size_t bs = 0; bs < blockSets.size(); ++bs)
            {
                auto got = runCase (bank, sel, bpm, T, host, blockSets[bs]);
                std::vector<Ev> gg; for (auto& e : got) if (e.t < limit) gg.push_back (e);
                bool ok = gg.size() == rr.size(); double maxd = 0; size_t bad = 0;
                for (size_t i = 0; ok && i < rr.size(); ++i)
                {
                    const double d = std::fabs (gg[i].t - rr[i].t) * 48000.0;
                    maxd = std::max (maxd, d);
                    if (d > 1.01 || gg[i].on != rr[i].on || (rr[i].on && (gg[i].note != rr[i].note || gg[i].acc != rr[i].acc || gg[i].sld != rr[i].sld))) { ok = false; bad = i; }
                }
                std::printf ("%s  %-7s %-8s blocks#%zu  events %zu/%zu  max timing error %.2f samples\n", ok ? "PASS" : "FAIL", f[1].c_str(), host ? "host" : "internal", bs, gg.size(), rr.size(), maxd);
                if (! ok) { ++fails; if (gg.size() == rr.size()) std::printf ("     first mismatch at event %zu: got t=%.5f on=%d n=%d  want t=%.5f on=%d n=%d\n", bad, gg[bad].t, gg[bad].on, gg[bad].note, rr[bad].t, rr[bad].on, rr[bad].note); }
            }
    }
    // DAW loop / jump: after jumping back to the start the sequence restarts on the grid
    {
        Selection sel; Sequencer sq (bank); sq.prepare (48000);
        EventList el; double ppq = 0; const double bpm = 120; bool restarted = false;
        for (int i = 0; i < 60; ++i) { el.clear(); sq.runHost (512, ppq, bpm, sel, el); ppq += 512 * bpm / 60.0 / 48000.0; }
        ppq = 0; el.clear(); sq.runHost (512, ppq, bpm, sel, el);
        for (int i = 0; i < el.size(); ++i) if (el[i].on && el[i].offset == 0 && el[i].note == 36 && ! el[i].slide) restarted = true;
        std::printf ("%s  loop jump restarts the pattern at the first step\n", restarted ? "PASS" : "FAIL"); if (! restarted) ++fails;
    }
    // stopping closes an open gate once
    {
        Selection sel; Sequencer sq (bank); sq.prepare (48000);
        EventList el; sq.runInternal (256, 130, sel, el);
        el.clear(); sq.idle (256, el); const bool one = el.size() == 1 && ! el[0].on;
        el.clear(); sq.idle (256, el);
        std::printf ("%s  idle() closes the gate once and then stays quiet\n", (one && el.size() == 0) ? "PASS" : "FAIL"); if (! (one && el.size() == 0)) ++fails;
    }
    std::printf (fails ? "%d FAILED\n" : "all sequencer tests passed\n", fails);
    return fails ? 2 : 0;
}
