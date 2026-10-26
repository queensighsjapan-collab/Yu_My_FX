// Offline renderer for the engine. Reads a script, writes interleaved float32 stereo to stdout file.
// Script lines:  set <id> <value> | on <note> <vel0-127> | off <note> | panic | render <frames>
#include "../Source/Engine.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: engine_test script out.f32 [samplerate]\n"); return 1; }
    const double sr = argc > 3 ? std::atof (argv[3]) : 48000.0;
    ym::Engine e (sr);
    std::ifstream in (argv[1]);
    std::ofstream out (argv[2], std::ios::binary);
    std::string line;
    std::vector<float> L (128), R (128), inter;
    while (std::getline (in, line))
    {
        std::istringstream ss (line);
        std::string cmd; ss >> cmd;
        if (cmd == "set") { std::string id; double v; ss >> id >> v; const int i = ym::engineParamIndex (id.c_str()); if (i >= 0) e.set (i, v); }
        else if (cmd == "on") { int n; double v; ss >> n >> v; e.noteOn (n, v / 127.0); }
        else if (cmd == "off") { int n; ss >> n; e.noteOff (n); }
        else if (cmd == "panic") e.panic();
        else if (cmd == "render")
        {
            int frames; ss >> frames;
            for (int done = 0; done < frames; done += 128)
            {
                e.process (L.data(), R.data(), 128, nullptr);
                inter.resize (256);
                for (int i = 0; i < 128; ++i) { inter[(size_t) i * 2] = L[(size_t) i]; inter[(size_t) i * 2 + 1] = R[(size_t) i]; }
                out.write ((const char*) inter.data(), (std::streamsize) (256 * sizeof (float)));
            }
        }
    }
    return 0;
}
