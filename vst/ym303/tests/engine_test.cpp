// Offline renderer for the engine (mono float32 out). Script lines:
//   set <cutoff|reso|envmod|decay|accent|tuning|volume|wave> <value> | on <note> <accent 0/1> <slide 0/1> | off | silence | render <frames, multiple of 128>
#include "../Source/Engine.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: engine_test script out.f32 [samplerate]\n"); return 1; }
    const double sr = argc > 3 ? std::atof (argv[3]) : 48000.0;
    ym303::Engine e (sr);
    std::ifstream in (argv[1]);
    std::ofstream out (argv[2], std::ios::binary);
    std::string line;
    std::vector<float> buf (128);
    while (std::getline (in, line))
    {
        std::istringstream ss (line);
        std::string cmd; ss >> cmd;
        if (cmd == "set")
        {
            std::string id; double v; ss >> id >> v;
            if (id == "cutoff") e.p.cutoff = v; else if (id == "reso") e.p.reso = v; else if (id == "envmod") e.p.envmod = v;
            else if (id == "decay") e.p.decay = v; else if (id == "accent") e.p.accent = v; else if (id == "tuning") e.p.tuning = v;
            else if (id == "volume") e.p.volume = v; else if (id == "wave") e.p.wave = (int) v;
        }
        else if (cmd == "on") { int n, a, s; ss >> n >> a >> s; e.noteOn (n, a != 0, s != 0); }
        else if (cmd == "off") e.noteOff();
        else if (cmd == "silence") e.silence();
        else if (cmd == "render")
        {
            int frames; ss >> frames;
            for (int d = 0; d < frames; d += 128)
            {
                e.process (buf.data(), 128);
                out.write ((const char*) buf.data(), 128 * sizeof (float));
            }
        }
    }
    return 0;
}
