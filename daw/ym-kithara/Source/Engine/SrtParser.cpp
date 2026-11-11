#include "SrtParser.h"

//==============================================================================
namespace SrtParser
{
    namespace
    {
        /** "HH:MM:SS,mmm" (or with '.' / 1-2 digit hours) → seconds.
            False when the shape is wrong. Extra trailing characters
            are the caller's concern (the timing line is split first). */
        bool parseTimestamp (juce::String text, double& secondsOut)
        {
            text = text.trim();

            const int colon1 = text.indexOfChar (':');

            if (colon1 < 0)
                return false;

            const int colon2 = text.indexOfChar (colon1 + 1, ':');

            if (colon2 < 0)
                return false;

            const auto hoursText   = text.substring (0, colon1);
            const auto minutesText = text.substring (colon1 + 1, colon2);
            auto secondsText       = text.substring (colon2 + 1);

            if (hoursText.isEmpty() || hoursText.length() > 2
                 || minutesText.length() != 2)
                return false;

            // Split "SS,mmm" / "SS.mmm" (a missing fraction reads 0).
            int separator = secondsText.indexOfChar (',');

            if (separator < 0)
                separator = secondsText.indexOfChar ('.');

            juce::String fractionText;

            if (separator >= 0)
            {
                fractionText = secondsText.substring (separator + 1);
                secondsText = secondsText.substring (0, separator);
            }

            if (secondsText.length() != 2)
                return false;

            for (auto part : { hoursText, minutesText, secondsText, fractionText })
                if (part.isNotEmpty() && ! part.containsOnly ("0123456789"))
                    return false;

            double fraction = 0.0;

            if (fractionText.isNotEmpty())
            {
                // Pad/truncate to milliseconds: ",5" means 500 ms.
                auto millis = (fractionText + "000").substring (0, 3);
                fraction = millis.getDoubleValue() / 1000.0;
            }

            const int minutes = minutesText.getIntValue();
            const int seconds = secondsText.getIntValue();

            if (minutes > 59 || seconds > 59)
                return false;

            secondsOut = hoursText.getIntValue() * 3600.0
                         + minutes * 60.0 + seconds + fraction;
            return true;
        }

        /** Removes SRT/ASS inline markup: <...> tags and {\...}
            override blocks. The telop renderer draws plain text. */
        juce::String stripTags (const juce::String& line)
        {
            juce::String result;
            bool inTag = false, inOverride = false;

            for (int i = 0; i < line.length(); ++i)
            {
                const auto c = line[i];

                if (inTag)
                {
                    if (c == '>')
                        inTag = false;

                    continue;
                }

                if (inOverride)
                {
                    if (c == '}')
                        inOverride = false;

                    continue;
                }

                if (c == '<') { inTag = true;      continue; }
                if (c == '{') { inOverride = true; continue; }

                result += c;
            }

            return result;
        }
    }

    std::vector<TextClipData> parse (const juce::String& fileContent,
                                     int& skippedOut)
    {
        skippedOut = 0;
        std::vector<TextClipData> clips;

        auto content = fileContent;

        // A decoded byte-order mark as the first character (a raw
        // UTF-8 BOM is already stripped by loadFileAsString).
        if (content.startsWithChar ((juce::juce_wchar) 0xFEFF))
            content = content.substring (1);

        content = content.replace ("\r\n", "\n").replace ("\r", "\n");

        // Blocks are separated by blank lines (a line holding only
        // spaces/tabs counts as blank).
        std::vector<std::vector<juce::String>> blocks;
        std::vector<juce::String> current;

        auto flushBlock = [&]
        {
            if (! current.empty())
            {
                blocks.push_back (current);
                current.clear();
            }
        };

        int lineStart = 0;

        for (int i = 0; i <= content.length(); ++i)
        {
            if (i == content.length() || content[i] == '\n')
            {
                auto line = content.substring (lineStart, i);
                lineStart = i + 1;

                if (line.trim().isEmpty())
                    flushBlock();
                else
                    current.push_back (line);
            }
        }

        flushBlock();

        for (auto& block : blocks)
        {
            // Find the timing line: the first line containing the
            // arrow. Lines before it (the numeric index, or junk) are
            // ignored; lines after it are the text.
            int timingLine = -1;

            for (int i = 0; i < (int) block.size(); ++i)
            {
                if (block[(size_t) i].contains ("-->"))
                {
                    timingLine = i;
                    break;
                }
            }

            if (timingLine < 0)
            {
                ++skippedOut;   // no timing: an index-only / junk block
                continue;
            }

            const auto timing = block[(size_t) timingLine];
            const int arrow = timing.indexOf ("-->");
            const auto startText = timing.substring (0, arrow);
            auto endText = timing.substring (arrow + 3).trim();

            // Trailing position metadata ("X1:... X2:...") after the
            // end timestamp is allowed by some exporters — drop it.
            const int space = endText.indexOfChar (' ');

            if (space >= 0)
                endText = endText.substring (0, space);

            double start = 0.0, end = 0.0;

            if (! parseTimestamp (startText, start)
                 || ! parseTimestamp (endText, end)
                 || end <= start)
            {
                ++skippedOut;
                continue;
            }

            juce::StringArray textLines;

            for (int i = timingLine + 1; i < (int) block.size(); ++i)
                textLines.add (stripTags (block[(size_t) i]).trimEnd());

            // Trailing empty lines (from stripped tags) don't count.
            while (! textLines.isEmpty() && textLines[textLines.size() - 1].isEmpty())
                textLines.remove (textLines.size() - 1);

            const auto text = textLines.joinIntoString ("\n");

            if (text.trim().isEmpty())
            {
                ++skippedOut;
                continue;
            }

            TextClipData clip;
            clip.startSeconds = start;
            clip.durationSeconds = end - start;
            clip.text = text;
            clips.push_back (std::move (clip));
        }

        return clips;
    }
}
