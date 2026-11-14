#include "Sf2Font.h"

namespace
{
    struct Reader
    {
        const juce::uint8* p = nullptr;
        const juce::uint8* end = nullptr;

        bool ok (int n) const { return p + n <= end; }
        juce::uint32 u32()
        {
            if (! ok (4)) return 0;
            auto v = (juce::uint32) p[0] | ((juce::uint32) p[1] << 8)
                   | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24);
            p += 4;
            return v;
        }
        juce::uint16 u16()
        {
            if (! ok (2)) return 0;
            auto v = (juce::uint16) (p[0] | (p[1] << 8));
            p += 2;
            return v;
        }
        juce::String fourCC()
        {
            if (! ok (4)) return {};
            juce::String s = juce::String::fromUTF8 ((const char*) p, 4);
            p += 4;
            return s;
        }
        juce::String name20()
        {
            if (! ok (20)) return {};
            int len = 0;
            while (len < 20 && p[len] != 0) ++len;
            juce::String s = juce::String::fromUTF8 ((const char*) p, len);
            p += 20;
            return s;
        }
        void skip (int n) { p += n; }
    };

    struct Chunk
    {
        juce::String id;
        const juce::uint8* data = nullptr;
        int size = 0;
    };

    // Iterate the sub-chunks of [begin, begin+size).
    template <typename Fn>
    void eachChunk (const juce::uint8* begin, int size, Fn&& fn)
    {
        Reader r { begin, begin + size };
        while (r.ok (8))
        {
            Chunk c;
            c.id = r.fourCC();
            c.size = (int) r.u32();
            if (! r.ok (c.size)) return;
            c.data = r.p;
            fn (c);
            r.skip (c.size + (c.size & 1));
        }
    }
}

//==============================================================================
bool Sf2Font::loadFromFile (const juce::File& file)
{
    loaded = false;
    errorMessage = {};
    sampleData.clear();
    presetNames.clear();
    presetPrograms.clear();
    presetMaps.clear();

    juce::MemoryBlock block;
    if (! file.loadFileAsData (block))
    {
        errorMessage = "cannot read file";
        return false;
    }

    const auto* base = static_cast<const juce::uint8*> (block.getData());
    const int total = (int) block.getSize();

    if (total < 12 || juce::String::fromUTF8 ((const char*) base, 4) != "RIFF"
                   || juce::String::fromUTF8 ((const char*) base + 8, 4) != "sfbk")
    {
        errorMessage = "not an SF2 (sfbk) file";
        return false;
    }

    const juce::uint8* smplData = nullptr;
    int smplSize = 0;
    const juce::uint8* pdtaData = nullptr;
    int pdtaSize = 0;

    eachChunk (base + 12, total - 12, [&] (const Chunk& list)
    {
        if (list.id != "LIST" || list.size < 4)
            return;
        const juce::String type = juce::String::fromUTF8 ((const char*) list.data, 4);
        if (type == "sdta")
        {
            eachChunk (list.data + 4, list.size - 4, [&] (const Chunk& c)
            {
                if (c.id == "smpl") { smplData = c.data; smplSize = c.size; }
            });
        }
        else if (type == "pdta")
        {
            pdtaData = list.data + 4;
            pdtaSize = list.size - 4;
        }
    });

    if (smplData == nullptr || pdtaData == nullptr)
    {
        errorMessage = "missing sdta/pdta";
        return false;
    }

    sampleData.resize ((size_t) smplSize / 2);
    std::memcpy (sampleData.data(), smplData, (size_t) smplSize & ~ (size_t) 1);

    // ---- pdta sub-chunks -------------------------------------------------
    std::map<juce::String, Chunk> pd;
    eachChunk (pdtaData, pdtaSize, [&] (const Chunk& c) { pd[c.id] = c; });

    struct Rec { const juce::uint8* p; };
    auto count = [&] (const char* id, int recSize)
    {
        auto it = pd.find (id);
        return it == pd.end() ? 0 : it->second.size / recSize;
    };
    auto recAt = [&] (const char* id, int recSize, int i) -> const juce::uint8*
    {
        auto it = pd.find (id);
        if (it == pd.end()) return nullptr;
        return it->second.data + (size_t) i * recSize;
    };

    const int nPhdr = count ("phdr", 38);
    const int nInst = count ("inst", 22);
    const int nShdr = count ("shdr", 46);
    const int nPbag = count ("pbag", 4);
    const int nIbag = count ("ibag", 4);
    const int nPgen = count ("pgen", 4);
    const int nIgen = count ("igen", 4);

    if (nPhdr < 2 || nInst < 2 || nShdr < 2 || nPbag < 1 || nIbag < 1 || nPgen < 1 || nIgen < 1)
    {
        errorMessage = "pdta records missing";
        return false;
    }

    auto u16at = [] (const juce::uint8* p) { return (int) (p[0] | (p[1] << 8)); };
    auto u32at = [] (const juce::uint8* p)
    {
        return (int) ((juce::uint32) p[0] | ((juce::uint32) p[1] << 8)
                    | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24));
    };

    // sample headers
    struct Shdr { juce::String name; int start, end, rate, key; };
    std::vector<Shdr> shdr;
    for (int i = 0; i < nShdr - 1; ++i)
    {
        const auto* r = recAt ("shdr", 46, i);
        Reader rd { r, r + 46 };
        Shdr s;
        s.name = rd.name20();
        s.start = (int) rd.u32();
        s.end = (int) rd.u32();
        rd.u32(); rd.u32();                       // loop start/end (unused: one-shot)
        s.rate = (int) rd.u32();
        s.key = (int) (juce::int8) rd.p[0]; rd.skip (1);
        shdr.push_back (s);
    }

    auto genAt = [&] (bool presetLevel, int i, int& op, int& amount)
    {
        const auto* r = recAt (presetLevel ? "pgen" : "igen", 4, i);
        if (r == nullptr) { op = -1; amount = 0; return; }
        op = u16at (r);
        amount = u16at (r + 2);
    };

    // instrument zones: inst i -> map key -> (sampleRef, rangeWidth)
    std::vector<std::map<int, SampleRef>> instMaps ((size_t) (nInst - 1));
    std::vector<std::map<int, int>> instWidths ((size_t) (nInst - 1));

    for (int i = 0; i < nInst - 1; ++i)
    {
        const auto* ir = recAt ("inst", 22, i);
        const int bag0 = u16at (ir + 20);
        const auto* irNext = recAt ("inst", 22, i + 1);
        const int bag1 = u16at (irNext + 20);

        for (int b = bag0; b < bag1 && b + 1 < nIbag + 1; ++b)
        {
            const auto* br = recAt ("ibag", 4, b);
            const auto* brNext = recAt ("ibag", 4, b + 1);
            if (br == nullptr || brNext == nullptr) break;
            const int g0 = u16at (br);
            const int g1 = u16at (brNext);

            int lo = 0, hi = 127, root = -1, sampleId = -1;
            for (int g = g0; g < g1; ++g)
            {
                int op, amt;
                genAt (false, g, op, amt);
                if (op == 43) { lo = amt & 0xFF; hi = (amt >> 8) & 0xFF; }
                else if (op == 58) root = amt;
                else if (op == 53) sampleId = amt;
            }

            if (sampleId < 0 || sampleId >= (int) shdr.size())
                continue;   // global zone or bad ref

            const auto& s = shdr[(size_t) sampleId];
            SampleRef ref;
            ref.start = s.start;
            ref.length = juce::jmax (0, s.end - s.start);
            ref.rootKey = root >= 0 ? root : s.key;
            ref.sampleRate = s.rate > 0 ? s.rate : 44100;

            const int width = hi - lo;
            for (int k = lo; k <= hi; ++k)
            {
                auto it = instWidths[(size_t) i].find (k);
                if (it == instWidths[(size_t) i].end() || width <= it->second)
                {
                    instWidths[(size_t) i][k] = width;
                    instMaps[(size_t) i][k] = ref;
                }
            }
        }
    }

    // presets
    for (int i = 0; i < nPhdr - 1; ++i)
    {
        const auto* pr = recAt ("phdr", 38, i);
        Reader rd { pr, pr + 38 };
        const juce::String name = rd.name20();
        const int program = rd.u16();
        rd.u16();                                 // bank
        const int bag = rd.u16();

        const auto* brNext = recAt ("pbag", 4, bag + 1);
        if (brNext == nullptr) continue;
        const auto* br = recAt ("pbag", 4, bag);
        const int g0 = u16at (br);
        const int g1 = u16at (brNext);

        int instIdx = -1;
        for (int g = g0; g < g1; ++g)
        {
            int op, amt;
            genAt (true, g, op, amt);
            if (op == 41) instIdx = amt;
        }
        if (instIdx < 0 || instIdx >= (int) instMaps.size())
            continue;

        presetNames.add (name);
        presetPrograms.push_back (program);
        presetMaps.push_back (instMaps[(size_t) instIdx]);
    }

    if (presetMaps.empty())
    {
        errorMessage = "no presets found";
        return false;
    }

    loaded = true;
    return true;
}

juce::String Sf2Font::getPresetName (int preset) const
{
    return preset >= 0 && preset < getNumPresets() ? presetNames[preset] : juce::String();
}

int Sf2Font::getPresetProgram (int preset) const
{
    return preset >= 0 && preset < (int) presetPrograms.size() ? presetPrograms[(size_t) preset] : -1;
}

int Sf2Font::findPresetByProgram (int program) const
{
    for (int i = 0; i < (int) presetPrograms.size(); ++i)
        if (presetPrograms[(size_t) i] == program)
            return i;
    return -1;
}

const Sf2Font::SampleRef* Sf2Font::getSample (int preset, int midiKey) const
{
    if (preset < 0 || preset >= (int) presetMaps.size())
        return nullptr;
    const auto& m = presetMaps[(size_t) preset];
    const auto it = m.find (midiKey);
    return it == m.end() ? nullptr : &it->second;
}
