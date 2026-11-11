#pragma once

#include <JuceHeader.h>
#include "Session.h"

//==============================================================================
/**
    SrtParser — SubRip (.srt) subtitle files → telop clips (M6.3).

    Standard SRT is a sequence of blocks separated by blank lines:

        1
        00:00:01,000 --> 00:00:03,500
        First lyric line
        (optionally more lines)

    The parser is deliberately tolerant, because real-world SRT files
    (hand-written, exported from other tools, converted from other
    subtitle formats) are sloppy:

      - a leading UTF-8 BOM is stripped, and CRLF / CR / LF line
        endings are all accepted;
      - the numeric index line may be missing (a block may start
        straight at its timing line) — the parser scans each block
        for the first line containing " --> " and treats everything
        after it as text;
      - the millisecond separator may be ',' (standard) or '.';
      - hours may be one or two digits; a timing may carry trailing
        position metadata after the end time (ignored);
      - inline tags (<i>…</i>, <font …>, {\…} override blocks) are
        stripped from the text.

    A block is SKIPPED (and counted) when: no timing line is found,
    either timestamp is malformed, the end is not after the start, or
    the text is empty once tags are stripped. Callers report the
    imported/skipped counts to the user (TimelineEngine::importSrtFile
    does), so a partly-broken file still yields its good entries.

    Parsed clips have id 0 — the engine assigns real ids on import.
*/
namespace SrtParser
{
    /** Parses whole-file content. Returns the clips parsed;
        skippedOut receives the number of blocks that looked like
        entries but could not be used (blank-only blocks are not
        counted either way). */
    std::vector<TextClipData> parse (const juce::String& fileContent,
                                     int& skippedOut);
}
