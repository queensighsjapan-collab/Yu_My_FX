#pragma once

#include <JuceHeader.h>
#include "TimelineEngine.h"

//==============================================================================
/**
    TelopStyleDialog — session-wide telop styling (M6.6).

    Opened from the text track header's "Style..." button. One
    dialog edits the whole TelopStyleData (Session.h): the font
    (chosen by NAME from the fonts installed on this machine — see
    the picker notes in the .cpp: names are enumerated once on a
    background thread and only a selected name is ever instantiated),
    the size (40–250%, kept in sync with the header's S/M/L
    selector), the vertical position, the per-line alignment and the
    text colour (presets + a custom colour selector).

    Changes are applied to the session LIVE while the dialog is open
    (the preview window behind it follows along); Cancel — or the
    window's close button — restores the style snapshot taken on
    open, OK keeps the edits.
*/
namespace TelopStyleUI
{
    /** Opens the style dialog, or brings it to the front when it is
        already open. */
    void showDialog (TimelineEngine& engine);
}
