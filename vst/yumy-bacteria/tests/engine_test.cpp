// Runs the C++ engine with the same script as ref_core.js.   usage: engine_test script out.f32 [samplerate]
#include "../Source/Engine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
using namespace yb;
int main (int argc, char** argv)
{
    if (argc < 3) return 1;
    const double sr = argc > 3 ? atof (argv[3]) : 48000;
    Core K (sr);
    std::map<std::string, int> pid, oid;
    for (int i = 0; i < NUM_P; ++i) { std::string n = kParamNames[i]; for (auto& c : n) if (c == ' ') c = '_'; pid[n] = i; }
    for (int i = 0; i < NUM_O; ++i) oid[kOptNames[i]] = i;
    for (int i = 0; i < NUM_P; ++i) K.setParam (i, kParamDefaults[i]);
    for (int i = 0; i < NUM_O; ++i) K.setOpt (i, kOptDefaults[i]);
    for (int i = 0; i < 3; ++i) K.setLfoDest (i, 0);
    K.seed (0x12345678u);
    MxSlot mxs[6];
    bool modOn = false; long gi = 0;
    std::vector<float> out;
    std::ifstream in (argv[1]); std::string line;
    while (std::getline (in, line))
    {
        std::istringstream ss (line); std::string c; ss >> c;
        if (c.empty() || c[0] == '#') continue;
        if (c == "p") { std::string n; double v; ss >> n >> v; if (! pid.count (n)) { fprintf (stderr, "unknown param %s\n", n.c_str()); return 3; } K.setParam (pid[n], v); }
        else if (c == "o") { std::string n; int v; ss >> n >> v; if (! oid.count (n)) { fprintf (stderr, "unknown opt %s\n", n.c_str()); return 3; } K.setOpt (oid[n], v); }
        else if (c == "ld") { int i, v; ss >> i >> v; K.setLfoDest (i, v); }
        else if (c == "mx") { int sl; ss >> sl; MxSlot s; double a[3]; ss >> s.src >> s.d[0] >> a[0] >> s.d[1] >> a[1] >> s.d[2] >> a[2]; for (int j = 0; j < 3; ++j) s.a[j] = a[j] / 100; mxs[sl] = s; K.setMatrix (sl, s); }
        else if (c == "on") { int n; double v; ss >> n >> v; K.noteOn (n, v / 127); }
        else if (c == "off") { int n; ss >> n; K.noteOff (n); }
        else if (c == "sus") { int v; ss >> v; K.sustain (v != 0); }
        else if (c == "panic") K.panic();
        else if (c == "wheels") { double a, b, d; ss >> a >> b >> d; K.wheels (a, b, d); }
        else if (c == "maxv") { int v; ss >> v; K.maxV = v; }
        else if (c == "mod") { int v; ss >> v; modOn = v == 1; }
        else if (c == "load")
        {
            int idx; ss >> idx; const FactoryPreset& p = kFactory[idx];
            for (int i = 0; i < NUM_P; ++i) K.setParam (i, p.k[i]);
            for (int i = 0; i < NUM_O; ++i) K.setOpt (i, p.o[i]);
            for (int i = 0; i < 3; ++i) K.setLfoDest (i, p.ld[i]);
            for (int s = 0; s < 6; ++s) { MxSlot m; m.src = p.mx[s][0]; for (int j = 0; j < 3; ++j) { m.d[j] = p.mx[s][1 + j]; m.a[j] = p.mx[s][4 + j] / 100.0; } K.setMatrix (s, m); }
        }
        else if (c == "render")
        {
            int frames; ss >> frames;
            for (int d = 0; d < frames; d += 128)
            {
                float L[128] = {}, R[128] = {}, mod[128];
                if (modOn) for (int i = 0; i < 128; ++i, ++gi) mod[i] = (float) (0.3 * std::sin (2 * M_PI * 150 * gi / sr) * (0.6 + 0.4 * std::sin (2 * M_PI * 3 * gi / sr)) + 0.1 * std::sin (2 * M_PI * 1900 * gi / sr));
                K.render (L, R, 128, modOn ? mod : nullptr);
                for (int i = 0; i < 128; ++i) { out.push_back (L[i]); out.push_back (R[i]); }
            }
        }
    }
    std::ofstream o (argv[2], std::ios::binary); o.write ((const char*) out.data(), out.size() * 4);
    return 0;
}
