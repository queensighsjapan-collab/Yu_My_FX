#pragma once

#include <JuceHeader.h>
#include "Session.h"

//==============================================================================
/**
    VideoComposite — the ONE timeline→frame implementation behind
    every video surface (M6.3).

    Before M6.3 the preview feed and the mp4 exporter each carried
    their own "find the clip covering t" loop. Cross-dissolves made
    that untenable: overlaps, fades and telops must behave identically
    in the monitors and in the exported file, so the mapping lives
    here, once, and both sides call it.

    The layer model (bottom → top):

      - Every clip covering t is a layer. Layers are ordered by clip
        start (ties by id): the LAST-starting clip paints on top.
      - Each layer's own fadeIn/fadeOut scale its alpha (a lone clip
        therefore fades from/to the black underneath).
      - Every layer above the bottom one additionally DISSOLVES IN
        over whatever is beneath it during their overlap: alpha is
        scaled by (t − layer.start) / (beneath.end − layer.start),
        0 at the layer's first instant and 1 exactly when the clip
        beneath it ends. With two clips this is the classic
        cross-dissolve of the spec; the rule generalises to deeper
        stacks unchanged.
      - Compositing is the standard "over" operator over black, in
        the decoded RGB/BGR domain: out = layer·a + out·(1−a).

    The preview feed uses at most the TOP TWO layers (two decoder
    services); the exporter uses them all. Anything deeper than two
    simultaneous clips is pathological on a music-video timeline, and
    the difference is a slightly-too-opaque bottom layer in preview
    only — exports are always exact.

    Telop compositing also lives here (namespace Telop below): the
    same draw routine serves the preview feed (onto its published
    copy — the decoder's cached frames are never mutated) and the
    exporter (onto each composed frame before encode).
*/
namespace VideoCompositing
{
    /** One covering clip at time t, with its source position and its
        effective opacity (fades × dissolve progress, 0..1). */
    struct Layer
    {
        const VideoClipData* clip = nullptr;
        double sourceSeconds = 0.0;   // offset + (t − start)
        float  alpha = 1.0f;
    };

    /** Fills `out` (bottom → top) with the layers covering t.
        `clips` may be in any order. Empty when t is in a gap. */
    void computeLayers (const std::vector<VideoClipData>& clips, double t,
                        std::vector<Layer>& out);

    /** A layer's decoded picture plus the alpha computeLayers gave it. */
    struct LayerImage
    {
        juce::Image image;
        float alpha = 1.0f;
    };

    /** Composes layers (bottom → top) into a new width×height RGB
        image over black. Each layer's picture is fitted inside the
        frame with its aspect preserved (centred, bars show whatever
        is beneath — the same fit the exporter always used), then
        drawn at its alpha. An empty layer list yields a plain black
        frame (the gap + telop case). */
    juce::Image composeFrame (int width, int height,
                              const std::vector<LayerImage>& layersBottomToTop);
}

//==============================================================================
/**
    Telop — timed text overlays from the session's text track (M6.3).

    M6.3 drew a fixed style: bottom-centred white bold sans with a
    black outline, font height ≈ 5.5% of the frame height (clamped so
    tiny frames stay legible), scaled by the session's S/M/L choice.
    M6.6 makes the style session-wide data (SessionData::telopStyle):
    font by NAME (resolved per machine, cached, with a default-sans
    fallback), size as a percentage, vertical position, per-line
    alignment and text colour. The TelopStyleData defaults reproduce
    the M6.3 look exactly. The text's own line breaks are honoured;
    lines are never auto-wrapped.
*/
namespace Telop
{
    /** The clip shown at time t: among the clips covering t, the one
        with the LATEST start wins (ties: the later clip in the list).
        nullptr when no text clip covers t. */
    const TextClipData* findActiveClip (const std::vector<TextClipData>& clips,
                                        double t);

    /** Font scale for a session telopSize index (0 = S, 1 = M, 2 = L). */
    float scaleForSizeIndex (int sizeIndex);

    /** The M6.7 resolution verdict, factored out as a pure function
        so the smoke suite can exercise the whole matrix. A request
        counts as RESOLVED when ANY of these holds (all comparisons
        case-insensitive):
          (a) createdFamilyName equals requestedName — the system
              created exactly the family that was asked for;
          (b) requestedName equals fallbackFamilyName — the user
              picked the substitution/default family itself, so
              there is nothing to substitute away from;
          (c) createdFamilyName differs from fallbackFamilyName —
              the system matched the request to a real, non-default
              font whose reported family is a LOCALIZED ALIAS of the
              requested name (the M6.6 equality check wrongly failed
              these: e.g. しねきゃぷしょん / Cinecaption).
        NOT resolved only when the created typeface IS the fallback
        while the request was for something else — JUCE's silent
        substitution for a name this machine does not have. */
    bool typefaceNameResolved (const juce::String& requestedName,
                               const juce::String& createdFamilyName,
                               const juce::String& fallbackFamilyName);

    /** The family name of the typeface the system substitutes for
        a deliberately nonexistent name — the silent-substitution
        baseline typefaceNameResolved compares against. Resolved
        once per app run and cached. */
    juce::String getSubstitutionFallbackFamilyName();

    /** Resolves a font NAME to its bold typeface, CACHED by name —
        the exporter draws hundreds of frames and must never re-run
        the system lookup. Returns nullptr when the name is empty
        (meaning "default sans") or cannot be resolved on THIS
        machine (the typefaceNameResolved verdict); callers then
        fall back to the default sans bold. Only names somebody
        actually asks for are ever instantiated. */
    juce::Typeface::Ptr getTypefaceForName (const juce::String& fontName);

    /** Draws `text` onto `frame` IN PLACE with the given style —
        callers pass a copy they own (never a decoder-cached frame).
        Position anchors the text block (top/centre get margins
        symmetric to the bottom one); alignment justifies each line.
        The outline colour is chosen by the text colour's luminance:
        black for light text, white for dark text. */
    void drawOnto (juce::Image& frame, const juce::String& text,
                   const TelopStyleData& style);

    /** Legacy convenience (M6.3 call shape): the default style at a
        raw font scale (1.0 = 100%). */
    void drawOnto (juce::Image& frame, const juce::String& text, float fontScale);
}
