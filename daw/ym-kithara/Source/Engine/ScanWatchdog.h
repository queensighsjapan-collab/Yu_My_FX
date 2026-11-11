#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    M6.10: stall-watchdog decision for the out-of-process plugin scan.

    The scanner rewrites its results file after every scanned plugin,
    so the results file's modification time is a live progress signal:
    while it keeps advancing, the scan is healthy however long it
    takes overall (a user folder can hold hundreds of plugins). Only
    a scan whose results file has NOT changed for stallSeconds is
    treated as hung (a plugin stuck inside the scanner), with a
    generous absolute backstop in case the mtime signal itself fails.

    Kept as a free function in its own header so the headless smoke
    suite can unit-check the decision without the UI.
*/
namespace ScanWatchdog
{
    /** Hard kill after this long with no results-file progress. */
    inline constexpr double stallSeconds = 120.0;

    /** Absolute backstop: kill no matter what after this long. */
    inline constexpr double absoluteSeconds = 60.0 * 60.0;

    /** True when the running scan should be killed: either it has
        produced no progress for stallSeconds, or it has been running
        for absoluteSeconds in total. All times in seconds. */
    inline bool shouldKill (double secondsSinceLastProgress,
                            double secondsSinceStart) noexcept
    {
        return secondsSinceLastProgress >= stallSeconds
            || secondsSinceStart >= absoluteSeconds;
    }
}
