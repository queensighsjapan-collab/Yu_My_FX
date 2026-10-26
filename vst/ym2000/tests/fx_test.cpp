// Offline renderer for the FX chain. usage: fx_test params.txt synth.f32(stereo interleaved) src.f32(mono) out.f32 [sr]
#include "../Source/Fx.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

static std::vector<float> readAll (const char* f)
{
    std::ifstream in (f, std::ios::binary);
    std::vector<char> b ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    std::vector<float> v (b.size() / sizeof (float));
    if (! v.empty()) std::memcpy (v.data(), b.data(), v.size() * sizeof (float));
    return v;
}

int main (int argc, char** argv)
{
    if (argc < 5) { std::fprintf (stderr, "usage\n"); return 1; }
    const double sr = argc > 5 ? std::atof (argv[5]) : 48000.0;
    ym::FxParams p;
    std::ifstream in (argv[1]);
    std::string line;
    while (std::getline (in, line))
    {
        std::istringstream ss (line);
        std::string c, id; double v;
        ss >> c >> id >> v;
        if (c != "set") continue;
        if (id == "vol") p.vol = v; else if (id == "ai1") p.ai1 = v; else if (id == "ai2") p.ai2 = v;
        else if (id == "vmode") p.vmode = (int) v; else if (id == "vfs") p.vfs = v; else if (id == "vcl") p.vcl = v;
        else if (id == "mft") p.mft = (int) v; else if (id == "mfs") p.mfs = v; else if (id == "mfd") p.mfd = v;
        else if (id == "dt") p.dt = (int) v; else if (id == "dtm") p.dtm = v; else if (id == "ddp") p.ddp = v;
        else if (id == "eqhf") p.eqhf = v; else if (id == "eqhg") p.eqhg = v; else if (id == "eqlf") p.eqlf = v; else if (id == "eqlg") p.eqlg = v;
        else if (id.rfind ("vlev", 0) == 0) p.vlev[(size_t) std::atoi (id.c_str() + 4)] = v;
        else if (id.rfind ("vpan", 0) == 0) p.vpan[(size_t) std::atoi (id.c_str() + 4)] = v;
    }
    const auto synth = readAll (argv[2]);
    const auto src = readAll (argv[3]);
    const size_t frames = synth.size() / 2;
    ym::FxChain fx;
    fx.prepare (sr, 128);
    std::vector<float> L (128), R (128), sL (128), sR (128), sm (128), oL (128), oR (128), out (frames * 2);
    for (size_t pos = 0; pos + 128 <= frames; pos += 128)
    {
        for (size_t i = 0; i < 128; ++i)
        {
            sL[i] = synth[(pos + i) * 2]; sR[i] = synth[(pos + i) * 2 + 1];
            sm[i] = pos + i < src.size() ? src[pos + i] : 0.0f;
        }
        fx.setParams (p, 128);
        fx.process (sL.data(), sR.data(), sm.data(), oL.data(), oR.data(), 128);
        for (size_t i = 0; i < 128; ++i) { out[(pos + i) * 2] = oL[i]; out[(pos + i) * 2 + 1] = oR[i]; }
    }
    std::ofstream o (argv[4], std::ios::binary);
    o.write ((const char*) out.data(), (std::streamsize) (out.size() * sizeof (float)));
    return 0;
}
