#include "Bank.h"
#include <algorithm>
#include <cstring>

namespace ym303
{
uint32_t Bank::pack (const Step& s)
{
    return (uint32_t) (s.type & 3) | (uint32_t) ((s.note & 15) << 2) | (s.up ? 1u << 6 : 0u) | (s.dn ? 1u << 7 : 0u)
         | (s.acc ? 1u << 8 : 0u) | (s.sld ? 1u << 9 : 0u);
}

Step Bank::unpack (uint32_t v)
{
    Step s;
    s.type = (int) (v & 3); if (s.type > Tie) s.type = Rest;
    s.note = (int) ((v >> 2) & 15); if (s.note > 12) s.note = 12;
    s.up = (v >> 6) & 1; s.dn = (v >> 7) & 1; s.acc = (v >> 8) & 1; s.sld = (v >> 9) & 1;
    if (s.up && s.dn) s.dn = false;
    return s;
}

Step Bank::getStep (int pat, int sec, int i) const
{
    return unpack (steps[(size_t) (idx (pat, sec) * kSteps + i)].load (std::memory_order_relaxed));
}

void Bank::setStep (int pat, int sec, int i, const Step& s)
{
    steps[(size_t) (idx (pat, sec) * kSteps + i)].store (pack (s), std::memory_order_relaxed);
}

void Bank::setLen (int pat, int sec, int len)
{
    lens[(size_t) idx (pat, sec)].store (std::clamp (len, 1, kSteps), std::memory_order_relaxed);
}

bool Bank::hasNotes (int pat, int sec) const
{
    const int L = getLen (pat, sec);
    for (int i = 0; i < L; ++i)
        if (getStep (pat, sec, i).type != Rest) return true;
    return false;
}

void Bank::setTrack (const int* pats, int count)
{
    count = std::clamp (count, 0, kMaxTrack);
    for (int i = 0; i < count; ++i) track[(size_t) i].store (std::clamp (pats[i], 0, kPatterns - 1), std::memory_order_relaxed);
    trackLen.store (count, std::memory_order_relaxed);
}

void Bank::clearAll()
{
    for (auto& s : steps) s.store (0, std::memory_order_relaxed);
    for (auto& l : lens) l.store (kSteps, std::memory_order_relaxed);
    trackLen.store (0, std::memory_order_relaxed);
}

// Token syntax (identical to the browser version):  C  C#  ..  B   note
//   suffixes: *  accent   ~  slide   ^  octave up   v  octave down
//   -  rest      =  tie
void Bank::fillSection (int pat, int sec, const char* tokens)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int i = 0;
    const char* p = tokens;
    while (*p && i < kSteps)
    {
        while (*p == ' ') ++p;
        if (! *p) break;
        const char* e = p; while (*e && *e != ' ') ++e;
        std::string tk (p, (size_t) (e - p));
        p = e;
        Step s;
        if (tk == "-")      { s.type = Rest; }
        else if (tk == "=") { s.type = Tie; }
        else
        {
            const size_t nlen = (tk.size() > 1 && tk[1] == '#') ? 2 : 1;
            int n = -1;
            for (int k = 0; k < 12; ++k) if (tk.compare (0, nlen, names[k]) == 0 && std::strlen (names[k]) == nlen) n = k;
            if (n < 0) { ++i; continue; }
            s.type = Note; s.note = n;
            for (size_t k = nlen; k < tk.size(); ++k)
            {
                if (tk[k] == '*') s.acc = true; else if (tk[k] == '~') s.sld = true;
                else if (tk[k] == '^') s.up = true; else if (tk[k] == 'v') s.dn = true;
            }
        }
        setStep (pat, sec, i++, s);
    }
}

void Bank::loadDefaults()
{
    clearAll();
    fillSection (0, 0, "C* C C^~ D# C* - G~ F C* C A#v C D#* F~ G -");
    fillSection (1, 0, "C* - C^ C C* D#~ F C^* - C C* G~ A# C^* = C");
    fillSection (1, 1, "C C^* - C D#* - G G#~ A#* = C^ - C* C A#v G");
    fillSection (2, 0, "C*~ D#~ G~ A#* C^~ A#~ G* F~ D#~ C* = G~ A#~ C^* A#~ G");
    fillSection (3, 0, "C C - C C^* - C C C - C^ C C* - C^~ A#");
    const int t[] = { 0, 1, 2 };
    setTrack (t, 3);
}

// Compact text form: per section: 1 hex digit (length-1) + 16 x 3 hex digits; then track length (2 hex) + 2 hex per entry
std::string Bank::serialize() const
{
    static const char* H = "0123456789abcdef";
    std::string out;
    for (int p = 0; p < kPatterns; ++p)
        for (int s = 0; s < 2; ++s)
        {
            out += H[getLen (p, s) - 1];
            for (int i = 0; i < kSteps; ++i)
            {
                const uint32_t v = pack (getStep (p, s, i));
                out += H[(v >> 8) & 15]; out += H[(v >> 4) & 15]; out += H[v & 15];
            }
        }
    const int tl = trackLength();
    out += H[(tl >> 4) & 15]; out += H[tl & 15];
    for (int i = 0; i < tl; ++i) { const int v = trackAt (i); out += H[(v >> 4) & 15]; out += H[v & 15]; }
    return out;
}

bool Bank::deserialize (const std::string& str)
{
    auto hx = [] (char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    const size_t secLen = 1 + kSteps * 3, body = (size_t) kPatterns * 2 * secLen;
    if (str.size() < body + 2) return false;
    for (char c : str) if (hx (c) < 0) return false;
    const int tl = hx (str[body]) * 16 + hx (str[body + 1]);
    if (tl > kMaxTrack || str.size() != body + 2 + (size_t) tl * 2) return false;
    size_t o = 0;
    for (int p = 0; p < kPatterns; ++p)
        for (int s = 0; s < 2; ++s)
        {
            setLen (p, s, hx (str[o++]) + 1);
            for (int i = 0; i < kSteps; ++i)
            {
                const uint32_t v = (uint32_t) (hx (str[o]) << 8 | hx (str[o + 1]) << 4 | hx (str[o + 2]));
                o += 3; setStep (p, s, i, unpack (v));
            }
        }
    o = body + 2;
    int t[kMaxTrack];
    for (int i = 0; i < tl; ++i) { t[i] = hx (str[o]) * 16 + hx (str[o + 1]); o += 2; }
    setTrack (t, tl);
    return true;
}
}
