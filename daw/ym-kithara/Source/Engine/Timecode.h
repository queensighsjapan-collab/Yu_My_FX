#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Timecode conversion for the transport display (M6).

    The timeline is seconds end to end; a timecode is just a way of
    NAMING a position at a given frame rate. This file converts a
    position in seconds to a frame label at a rational rate:

      - Integer rates (24, 25, 30, 50, 60) and 24000/1001 ("23.976",
        counted at 24) use NON-drop counting with a ':' separator: the
        frame index at time t is floor (t * rate) and the label is that
        index decomposed at the nominal (rounded) rate.
      - 30000/1001 ("29.97") and 60000/1001 ("59.94") use DROP-FRAME
        counting with a ';' separator: labels advance at the nominal
        rate (30 / 60 per second) but the first 2 (resp. 4) frame
        numbers of every minute are skipped, except every tenth minute.
        That keeps the label glued to wall-clock time: exactly one real
        hour reads 01:00:00;00.

    Drop-frame label math (nominal rate N, drop count D = N / 15):
    in frame space a ten-minute block holds 600*N - 9*D real frames —
    the first minute of the block has all 60*N (its start is a tenth
    minute, nothing dropped) and the other nine have 60*N - D each.
    The label of real frame n is found by walking that structure and
    adding D back for each dropped minute start it has passed.

    Reference points for 29.97 DF, derived from that structure (and
    asserted by the smoke tests): frame 1798 (t = 60.000 s) is labelled
    00:00:59;28 — the often-quoted 00:01:00;02 is the first label of
    minute 1 and belongs to real frame 1800, at t ≈ 60.06 s, because
    minute 0 is a tenth minute and drops nothing. One real hour is
    exactly 01:00:00;00.
*/
namespace Timecode
{
    struct Fps
    {
        int num = 30;
        int den = 1;

        double rate() const noexcept
        {
            return den > 0 ? (double) num / (double) den : 30.0;
        }

        /** The whole-number rate the label counts at (30 for 29.97,
            24 for 23.976, the rate itself for integer rates). */
        int nominalRate() const noexcept
        {
            return juce::jmax (1, (int) std::llround (rate()));
        }

        /** True only for the two NTSC fractional rates that have a
            standard drop-frame counting: 30000/1001 and 60000/1001. */
        bool isDropFrame() const noexcept
        {
            return den == 1001 && (num == 30000 || num == 60000);
        }

        bool operator== (const Fps& other) const noexcept
        {
            return num == other.num && den == other.den;
        }

        bool operator!= (const Fps& other) const noexcept { return ! operator== (other); }
    };

    /** The 0-based index of the frame displayed at time t — the count
        of whole frames elapsed since 0 at the real rate. */
    inline juce::int64 frameIndexAt (double seconds, const Fps& fps) noexcept
    {
        if (! (seconds > 0.0))
            return 0;

        return (juce::int64) std::floor (seconds * fps.rate());
    }

    /** The label (as a nominal-frame count) for a real frame index,
        applying drop-frame skips when the rate calls for them. */
    inline juce::int64 labelFrameAt (juce::int64 frameIndex, const Fps& fps) noexcept
    {
        if (frameIndex <= 0)
            return 0;

        if (! fps.isDropFrame())
            return frameIndex;

        const juce::int64 nominal = fps.nominalRate();          // 30 or 60
        const juce::int64 drop = nominal / 15;                  // 2 or 4
        const juce::int64 framesFirstMinute = 60 * nominal;     // tenth minute: no drop
        const juce::int64 framesOtherMinute = framesFirstMinute - drop;
        const juce::int64 framesPerTenMinutes = framesFirstMinute + 9 * framesOtherMinute;

        const juce::int64 tenBlocks = frameIndex / framesPerTenMinutes;
        juce::int64 remainder = frameIndex % framesPerTenMinutes;
        juce::int64 label = tenBlocks * 600 * nominal;

        if (remainder < framesFirstMinute)
            return label + remainder;

        remainder -= framesFirstMinute;
        const juce::int64 minute = 1 + remainder / framesOtherMinute;
        const juce::int64 withinMinute = remainder % framesOtherMinute + drop;

        return label + minute * framesFirstMinute + withinMinute;
    }

    /** "HH:MM:SS;FF" (drop-frame) or "HH:MM:SS:FF" (non-drop). */
    inline juce::String format (double seconds, const Fps& fps)
    {
        const juce::int64 nominal = fps.nominalRate();
        const juce::int64 label = labelFrameAt (frameIndexAt (seconds, fps), fps);

        const juce::int64 frames = label % nominal;
        const juce::int64 totalSeconds = label / nominal;
        const juce::int64 secs = totalSeconds % 60;
        const juce::int64 mins = (totalSeconds / 60) % 60;
        const juce::int64 hours = totalSeconds / 3600;

        return juce::String::formatted ("%02d:%02d:%02d%c%02d",
                                        (int) hours, (int) mins, (int) secs,
                                        fps.isDropFrame() ? ';' : ':',
                                        (int) frames);
    }
}
