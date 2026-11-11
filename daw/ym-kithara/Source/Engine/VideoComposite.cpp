#include "VideoComposite.h"

#include <algorithm>
#include <map>
#include <set>

//==============================================================================
namespace VideoCompositing
{
    namespace
    {
        float clamp01 (double value) noexcept
        {
            return (float) juce::jlimit (0.0, 1.0, value);
        }

        /** A clip's own fade envelope at t (1 away from its edges). */
        float fadeFactor (const VideoClipData& clip, double t) noexcept
        {
            float alpha = 1.0f;

            if (clip.fadeInSeconds > 0.0)
            {
                const double into = t - clip.startSeconds;

                if (into < clip.fadeInSeconds)
                    alpha *= clamp01 (into / clip.fadeInSeconds);
            }

            if (clip.fadeOutSeconds > 0.0)
            {
                const double remaining = clip.getEndSeconds() - t;

                if (remaining < clip.fadeOutSeconds)
                    alpha *= clamp01 (remaining / clip.fadeOutSeconds);
            }

            return alpha;
        }
    }

    void computeLayers (const std::vector<VideoClipData>& clips, double t,
                        std::vector<Layer>& out)
    {
        out.clear();

        std::vector<const VideoClipData*> covering;

        for (auto& clip : clips)
            if (t >= clip.startSeconds && t < clip.getEndSeconds())
                covering.push_back (&clip);

        // Bottom → top: by start, ties by id (the later-imported clip
        // of two starting together paints on top).
        std::sort (covering.begin(), covering.end(),
                   [] (const VideoClipData* a, const VideoClipData* b)
                   {
                       if (a->startSeconds != b->startSeconds)
                           return a->startSeconds < b->startSeconds;

                       return a->id < b->id;
                   });

        out.reserve (covering.size());

        for (size_t i = 0; i < covering.size(); ++i)
        {
            const auto* clip = covering[i];

            Layer layer;
            layer.clip = clip;
            layer.sourceSeconds = clip->offsetSeconds + (t - clip->startSeconds);
            layer.alpha = fadeFactor (*clip, t);

            // Dissolve-in over the layer directly beneath: every
            // covering layer beneath this one still covers t, so its
            // end is past t and the overlap is real. Progress runs
            // 0 → 1 across [layer.start, beneath.end).
            if (i > 0)
            {
                const double beneathEnd = covering[i - 1]->getEndSeconds();
                const double overlap = beneathEnd - clip->startSeconds;

                if (overlap > 0.0)
                    layer.alpha *= clamp01 ((t - clip->startSeconds) / overlap);
            }

            out.push_back (layer);
        }
    }

    juce::Image composeFrame (int width, int height,
                              const std::vector<LayerImage>& layersBottomToTop)
    {
        juce::Image canvas (juce::Image::RGB, juce::jmax (2, width),
                            juce::jmax (2, height), true);

        juce::Graphics g (canvas);
        g.fillAll (juce::Colours::black);

        for (auto& layer : layersBottomToTop)
        {
            if (! layer.image.isValid() || layer.alpha <= 0.0f)
                continue;

            g.setOpacity (juce::jlimit (0.0f, 1.0f, layer.alpha));
            g.drawImageWithin (layer.image, 0, 0, canvas.getWidth(), canvas.getHeight(),
                               juce::RectanglePlacement::centred, false);
        }

        return canvas;
    }
}

//==============================================================================
namespace Telop
{
    const TextClipData* findActiveClip (const std::vector<TextClipData>& clips,
                                        double t)
    {
        const TextClipData* active = nullptr;

        for (auto& clip : clips)
        {
            if (t < clip.startSeconds || t >= clip.getEndSeconds())
                continue;

            // Later-starting wins; on a tie the later entry in the
            // list (the later import/edit) wins.
            if (active == nullptr || clip.startSeconds >= active->startSeconds)
                active = &clip;
        }

        return active;
    }

    float scaleForSizeIndex (int sizeIndex)
    {
        switch (juce::jlimit (0, 2, sizeIndex))
        {
            case 0:  return 0.72f;   // S
            case 2:  return 1.38f;   // L
            default: return 1.0f;    // M
        }
    }

    bool typefaceNameResolved (const juce::String& requestedName,
                               const juce::String& createdFamilyName,
                               const juce::String& fallbackFamilyName)
    {
        // (a) The system created exactly the family that was asked
        // for — the M6.6 fast path, still the common case.
        if (createdFamilyName.equalsIgnoreCase (requestedName))
            return true;

        // (b) The request names the fallback family itself: that
        // family is what the user asked for, whatever face the
        // system hands back for it.
        if (requestedName.equalsIgnoreCase (fallbackFamilyName))
            return true;

        // (c) The created family is NOT the fallback: the system
        // matched the request to a real, non-default font. Its
        // reported family may be a localized alias of the requested
        // name — one locale's name went in, another locale's family
        // name came back — but it is the requested font, not a
        // silent substitution.
        if (! createdFamilyName.equalsIgnoreCase (fallbackFamilyName))
            return true;

        // The created typeface IS the fallback while the request
        // was for something else: the name is not on this machine.
        return false;
    }

    juce::String getSubstitutionFallbackFamilyName()
    {
        // Resolved once per app run: ask for a font that cannot
        // exist and note which family the system substitutes for
        // it. A request for any missing name comes back with that
        // same family, so it is the baseline typefaceNameResolved
        // compares against. Probed with the same Bold shape as the
        // real lookups in getTypefaceForName.
        static juce::CriticalSection fallbackLock;
        static juce::String fallbackFamily;
        static bool computed = false;

        const juce::ScopedLock lock (fallbackLock);

        if (computed)
            return fallbackFamily;

        computed = true;

        const juce::Font probe (
            juce::FontOptions().withName ("YM Orpheus No Such Font 6d2f9a")
                               .withStyle ("Bold")
                               .withHeight (12.0f));

        if (auto typeface = juce::Typeface::createSystemTypefaceFor (probe))
            fallbackFamily = typeface->getName();

        if (fallbackFamily.isEmpty())
        {
            // Some backends (FreeType on Linux) refuse an unknown
            // family outright instead of substituting, so the probe
            // above names nothing. The family JUCE itself falls
            // back to is the default sans: ask a nameless font for
            // its own resolved typeface.
            const juce::Font defaultFont (
                juce::FontOptions().withStyle ("Bold").withHeight (12.0f));

            if (auto defaultTypeface = defaultFont.getTypefacePtr())
                fallbackFamily = defaultTypeface->getName();
        }

        return fallbackFamily;
    }

    juce::Typeface::Ptr getTypefaceForName (const juce::String& fontName)
    {
        if (fontName.isEmpty())
            return nullptr;

        // Static cache: resolved typefaces by name, plus the names
        // that failed to resolve (so a missing font costs one system
        // lookup per app run, not one per frame). Guarded because
        // drawOnto runs on the message thread (preview) AND the
        // exporter's thread.
        static juce::CriticalSection cacheLock;
        static std::map<juce::String, juce::Typeface::Ptr> cache;
        static std::set<juce::String> unresolved;

        const juce::ScopedLock lock (cacheLock);

        const auto cached = cache.find (fontName);

        if (cached != cache.end())
            return cached->second;

        if (unresolved.find (fontName) != unresolved.end())
            return nullptr;

        // Ask the system for this family's bold face, then judge
        // the answer with typefaceNameResolved: the created face
        // counts as the requested font unless it is the known
        // substitution fallback for a name this machine lacks
        // (M6.6 compared family names for equality, which wrongly
        // rejected fonts whose family is reported under a localized
        // alias). Unresolved names fall back to the default sans at
        // the call site, as before.
        const juce::Font probe (
            juce::FontOptions().withName (fontName).withStyle ("Bold")
                               .withHeight (12.0f));
        auto typeface = juce::Typeface::createSystemTypefaceFor (probe);

        if (typeface != nullptr
            && typefaceNameResolved (fontName, typeface->getName(),
                                     getSubstitutionFallbackFamilyName()))
        {
            cache[fontName] = typeface;
            return typeface;
        }

        unresolved.insert (fontName);
        return nullptr;
    }

    void drawOnto (juce::Image& frame, const juce::String& text,
                   const TelopStyleData& style)
    {
        if (! frame.isValid() || text.isEmpty())
            return;

        const int frameWidth = frame.getWidth();
        const int frameHeight = frame.getHeight();

        // Split into lines, PRESERVING empty lines (they still take
        // vertical space) but dropping one trailing empty line — the
        // artefact of a final newline.
        std::vector<juce::String> lines;
        int lineStart = 0;

        for (int i = 0; i <= text.length(); ++i)
        {
            if (i == text.length() || text[i] == '\n')
            {
                auto line = text.substring (lineStart, i);

                if (line.endsWithChar ('\r'))
                    line = line.dropLastCharacters (1);

                lines.push_back (line);
                lineStart = i + 1;
            }
        }

        while (lines.size() > 1 && lines.back().isEmpty())
            lines.pop_back();

        if (lines.empty())
            return;

        const float fontScale = style.scalePercent / 100.0f;
        const float fontHeight = juce::jmax (10.0f,
                                             (float) frameHeight * 0.055f * fontScale);
        const float lineHeight = fontHeight * 1.28f;
        const float blockHeight = lineHeight * (float) lines.size();
        const float edgeMargin = (float) frameHeight * 0.045f;

        // The style anchors the whole text block: bottom keeps the
        // M6.3 placement, top mirrors it, centre centres the block.
        float blockTop = 0.0f;

        switch (style.vertical)
        {
            case TelopVerticalPosition::top:
                blockTop = edgeMargin;
                break;

            case TelopVerticalPosition::centre:
                blockTop = ((float) frameHeight - blockHeight) * 0.5f;
                break;

            case TelopVerticalPosition::bottom:
            default:
                blockTop = (float) frameHeight - edgeMargin - blockHeight;
                break;
        }

        const float outline = juce::jmax (1.25f, fontHeight * 0.055f);

        // The named font when it resolves on this machine, else the
        // default sans bold — the M6.3 face (also the empty-name
        // case). Named faces come from the cache, never re-created.
        juce::Graphics g (frame);

        if (auto typeface = getTypefaceForName (style.fontName))
            g.setFont (juce::Font (juce::FontOptions().withTypeface (typeface)
                                                       .withHeight (fontHeight)));
        else
            g.setFont (juce::Font (juce::FontOptions (fontHeight).withStyle ("Bold")));

        // Alignment justifies each line inside the block's row.
        // Centred lines keep the full-width row (the M6.3 geometry);
        // left/right rows are inset by a small side margin.
        const float sideMargin = style.align == TelopHorizontalAlign::centre
                                     ? 0.0f : (float) frameWidth * 0.04f;

        juce::Justification justification (juce::Justification::centredTop);

        if (style.align == TelopHorizontalAlign::left)
            justification = juce::Justification (juce::Justification::topLeft);
        else if (style.align == TelopHorizontalAlign::right)
            justification = juce::Justification (juce::Justification::topRight);

        const juce::Colour textColour (style.colourArgb);

        // Outline colour by the text colour's luminance: black
        // around light text (the M6.3 default), white around dark
        // text so a navy telop on a dark scene stays readable.
        const juce::Colour outlineColour =
            textColour.getPerceivedBrightness() >= 0.5f
                ? juce::Colours::black : juce::Colours::white;

        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (lines[i].isEmpty())
                continue;

            const float y = blockTop + lineHeight * (float) i;
            const auto lineRect = juce::Rectangle<float> (
                sideMargin, y, (float) frameWidth - 2.0f * sideMargin, lineHeight);

            // Outline/shadow first (8 offsets), then the face on top.
            g.setColour (outlineColour.withAlpha (0.9f));

            for (float dx : { -outline, 0.0f, outline })
                for (float dy : { -outline, 0.0f, outline })
                    if (dx != 0.0f || dy != 0.0f)
                        g.drawText (lines[i], lineRect.translated (dx, dy),
                                    justification, false);

            g.setColour (textColour);
            g.drawText (lines[i], lineRect, justification, false);
        }
    }

    void drawOnto (juce::Image& frame, const juce::String& text, float fontScale)
    {
        TelopStyleData style;
        style.scalePercent = fontScale * 100.0f;
        drawOnto (frame, text, style);
    }
}
