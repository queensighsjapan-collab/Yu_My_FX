// For every factory preset: play a chord, release, All Notes Off; report how long the FX tail lasts and whether voices are left.
#include "../Source/Engine.h"
#include <cstdio>
using namespace yb;
int main()
{
    const double sr = 48000;
    for (int pi = 0; pi < YB_NUM_FACTORY; ++pi)
    {
        Core K (sr); const FactoryPreset& p = kFactory[pi];
        for (int i = 0; i < NUM_P; ++i) K.setParam (i, p.k[i]);
        for (int i = 0; i < NUM_O; ++i) K.setOpt (i, p.o[i]);
        for (int i = 0; i < 3; ++i) K.setLfoDest (i, p.ld[i]);
        for (int s = 0; s < 6; ++s) { MxSlot m; m.src = p.mx[s][0]; for (int j = 0; j < 3; ++j) { m.d[j] = p.mx[s][1 + j]; m.a[j] = p.mx[s][4 + j] / 100.0; } K.setMatrix (s, m); }
        K.noteOn (48, 1); K.noteOn (55, 1); K.noteOn (60, 1);
        float L[256], R[256];
        for (int b = 0; b < 400; ++b) { for (int i = 0; i < 256; ++i) L[i] = R[i] = 0; K.render (L, R, 256, nullptr); }
        K.panic();
        double lastLoud = 0; int voicesEnd = 0;
        for (int b = 0; b < 48000 * 60 / 256; ++b)
        {
            for (int i = 0; i < 256; ++i) L[i] = R[i] = 0;
            K.render (L, R, 256, nullptr);
            double e = 0; for (int i = 0; i < 256; ++i) e += L[i] * L[i] + R[i] * R[i];
            if (std::sqrt (e / 512) > 0.001) lastLoud = b * 256 / sr;
            voicesEnd = K.activeVoices();
        }
        if (lastLoud > 3.0 || voicesEnd) std::printf ("%2d %-22s tail>0.001 until %.1fs  voices=%d  fb=%d dsend=%d rsend=%d\n", pi, p.name, lastLoud, voicesEnd, (int) p.k[P_Delay_Feedback], (int) p.k[P_Delay_Send], (int) p.k[P_Reverb_Send]);
    }
}
