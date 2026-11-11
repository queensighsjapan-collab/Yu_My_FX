#include "TelopStyleDialog.h"
#include "VideoComposite.h"   // Telop::getTypefaceForName (shared typeface cache)

#include <thread>
#include <vector>

namespace
{
    const juce::Colour panelColour   { 0xff262932 };
    const juce::Colour fieldColour   { 0xff1c1e24 };
    const juce::Colour textColour    { 0xffe8e9ec };
    const juce::Colour dimTextColour { 0xff9aa0ad };
    const juce::Colour accentColour  { 0xffd8a24a };

    // Sample text for the previews: Latin + digits + kana + kanji,
    // so a font's coverage (or lack of it) is visible at a glance.
    // M6.9: built with fromUTF8 — as plain narrow literals (the M6.6
    // code) juce::String read them as ASCII and the kana samples
    // rendered as mojibake, the same bug as the ● buttons.
    const juce::String fontSampleText = juce::String::fromUTF8 (
        "Aa Bb 123 \xe3\x81\x82 \xe3\x82\xa2 \xe4\xba\x9c \xe6\xad\x8c");
    const juce::String stripSampleText = juce::String::fromUTF8 (
        "Telop Sample\n\xe3\x81\x82 \xe3\x82\xa2 \xe4\xba\x9c \xe6\xad\x8c Aa 123");

    //==========================================================================
    /** A floating dialog window in the house style (native title
        bar, panel background, close button routed to a callback so
        the content can apply its Cancel semantics first). */
    class FloatingWindow final : public juce::DocumentWindow
    {
    public:
        FloatingWindow (const juce::String& name,
                        std::function<void()> closeCallback)
            : juce::DocumentWindow (name, panelColour,
                                    juce::DocumentWindow::closeButton),
              onCloseRequested (std::move (closeCallback))
        {
            setUsingNativeTitleBar (true);
            setResizable (false, false);
        }

        void closeButtonPressed() override
        {
            if (onCloseRequested != nullptr)
                onCloseRequested();
        }

    private:
        std::function<void()> onCloseRequested;
    };

    std::unique_ptr<FloatingWindow> styleWindowInstance;
    std::unique_ptr<FloatingWindow> fontPickerWindowInstance;
    std::unique_ptr<FloatingWindow> colourWindowInstance;

    /** Deferred destruction (the established pattern: a window is
        never destroyed from inside its own button handler). */
    void closeWindowDeferred (std::unique_ptr<FloatingWindow>& window)
    {
        if (window != nullptr)
            juce::MessageManager::callAsync ([&window] { window.reset(); });
    }

    void closeAllDialogWindows()
    {
        closeWindowDeferred (fontPickerWindowInstance);
        closeWindowDeferred (colourWindowInstance);
        closeWindowDeferred (styleWindowInstance);
    }

    //==========================================================================
    /**
        FontNameProvider — the font picker's name list.

        かいさん's PC has 500+ fonts, so the picker NEVER
        instantiates them: it enumerates NAMES only, exactly once
        per app run, on a detached background thread
        (juce::Font::findAllTypefaceNames), and caches the sorted
        list. A typeface object is created only for a name the user
        actually highlights or selects, via the shared renderer
        cache (Telop::getTypefaceForName).
    */
    class FontNameProvider
    {
    public:
        static FontNameProvider& getInstance()
        {
            static FontNameProvider instance;
            return instance;
        }

        /** Invokes onReady on the message thread once the names are
            available (promptly when already cached). */
        void ensureLoaded (std::function<void()> onReady)
        {
            bool launch = false;

            {
                const juce::ScopedLock lock (stateLock);

                if (ready)
                {
                    // Handled by the async call below.
                }
                else
                {
                    if (onReady != nullptr)
                        pendingCallbacks.push_back (std::move (onReady));

                    if (! started)
                    {
                        started = true;
                        launch = true;
                    }
                }
            }

            if (launch)
            {
                std::thread ([this] { enumerate(); }).detach();
                return;
            }

            if (isReady() && onReady != nullptr)
                juce::MessageManager::callAsync (
                    [onReady] { onReady(); });
        }

        bool isReady()
        {
            const juce::ScopedLock lock (stateLock);
            return ready;
        }

        juce::StringArray getNames()
        {
            const juce::ScopedLock lock (stateLock);
            return names;
        }

    private:
        void enumerate()
        {
            auto found = juce::Font::findAllTypefaceNames();
            found.sortNatural();

            std::vector<std::function<void()>> callbacks;

            {
                const juce::ScopedLock lock (stateLock);
                names = found;
                ready = true;
                callbacks = pendingCallbacks;
                pendingCallbacks.clear();
            }

            if (! callbacks.empty())
                juce::MessageManager::callAsync (
                    [callbacks]
                    {
                        for (auto& callback : callbacks)
                            if (callback != nullptr)
                                callback();
                    });
        }

        juce::CriticalSection stateLock;
        juce::StringArray names;
        std::vector<std::function<void()>> pendingCallbacks;
        bool started = false;
        bool ready = false;
    };

    //==========================================================================
    /** The font picker's preview: the sample text in the highlighted
        font over a dark ground. A name that does not resolve on this
        machine previews in the fallback face and says so. */
    class FontSamplePreview final : public juce::Component
    {
    public:
        void setFontName (const juce::String& newName)
        {
            if (fontName != newName)
            {
                fontName = newName;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (fieldColour);

            auto area = getLocalBounds().reduced (10, 6);
            bool unavailable = false;

            juce::Font font (juce::FontOptions (26.0f).withStyle ("Bold"));

            if (fontName.isNotEmpty())
            {
                if (auto typeface = Telop::getTypefaceForName (fontName))
                    font = juce::Font (juce::FontOptions()
                                           .withTypeface (typeface)
                                           .withHeight (26.0f));
                else
                    unavailable = true;
            }

            g.setFont (font);
            g.setColour (textColour);
            g.drawText (fontSampleText, area.removeFromTop (34),
                        juce::Justification::centredLeft, false);

            g.setFont (juce::FontOptions (11.0f));
            g.setColour (unavailable ? juce::Colour (0xffe08a8a)
                                     : dimTextColour);
            g.drawText (fontName.isEmpty()
                            ? juce::String ("(Default Sans)")
                            : unavailable
                                ? fontName + "  -  not available on this machine"
                                : fontName,
                        area, juce::Justification::topLeft, false);
        }

    private:
        juce::String fontName;
    };

    //==========================================================================
    /** Font picker content: search field + name list + preview.
        Rows are painted in the default font — painting each row in
        its own font would instantiate every font on the machine,
        which is exactly what this picker exists to avoid. Only the
        highlighted row's font is resolved (for the preview). */
    class TelopFontPickerComponent final : public juce::Component,
                                           private juce::ListBoxModel
    {
    public:
        TelopFontPickerComponent (
            const juce::String& initialName,
            std::function<void (const juce::String&)> onPickToUse,
            std::function<void()> onDoneToUse)
            : onPick (std::move (onPickToUse)), onDone (std::move (onDoneToUse))
        {
            searchBox.setTextToShowWhenEmpty ("Search fonts...", dimTextColour);
            searchBox.setColour (juce::TextEditor::backgroundColourId, fieldColour);
            searchBox.setColour (juce::TextEditor::textColourId, textColour);
            searchBox.onTextChange = [this] { refilter(); };
            addAndMakeVisible (searchBox);

            statusLabel.setFont (juce::FontOptions (11.5f));
            statusLabel.setColour (juce::Label::textColourId, dimTextColour);
            addAndMakeVisible (statusLabel);

            listBox.setModel (this);
            listBox.setRowHeight (22);
            listBox.setColour (juce::ListBox::backgroundColourId, fieldColour);
            listBox.setColour (juce::ListBox::outlineColourId,
                               juce::Colour (0xff3a3f4a));
            addAndMakeVisible (listBox);

            addAndMakeVisible (preview);

            useButton.onClick = [this]
            {
                const int row = listBox.getSelectedRow();

                if (row >= 0 && row < rows.size())
                    pick (rows[(size_t) row]);
            };
            addAndMakeVisible (useButton);

            cancelButton.onClick = [this]
            {
                if (onDone != nullptr)
                    onDone();
            };
            addAndMakeVisible (cancelButton);

            setSize (470, 540);

            rows.add (juce::String());   // row 0: the default (empty name)
            refilter();

            juce::Component::SafePointer<TelopFontPickerComponent> safeThis (this);
            FontNameProvider::getInstance().ensureLoaded (
                [safeThis, initialName]
                {
                    if (safeThis != nullptr)
                    {
                        safeThis->refilter();
                        safeThis->selectName (initialName);
                    }
                });

            if (FontNameProvider::getInstance().isReady())
                selectName (initialName);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (10, 8);
            searchBox.setBounds (area.removeFromTop (26));
            area.removeFromTop (4);
            statusLabel.setBounds (area.removeFromTop (16));
            area.removeFromTop (4);

            auto buttons = area.removeFromBottom (26);
            useButton.setBounds (buttons.removeFromRight (96));
            buttons.removeFromRight (8);
            cancelButton.setBounds (buttons.removeFromRight (76));

            area.removeFromBottom (8);
            preview.setBounds (area.removeFromBottom (62));
            area.removeFromBottom (6);
            listBox.setBounds (area);
        }

    private:
        // juce::ListBoxModel
        int getNumRows() override { return rows.size(); }

        void paintListBoxItem (int rowNumber, juce::Graphics& g,
                               int width, int height, bool rowIsSelected) override
        {
            if (rowIsSelected)
            {
                g.fillAll (accentColour.withAlpha (0.28f));
                g.setColour (textColour);
            }
            else
            {
                g.setColour (dimTextColour);
            }

            g.setFont (juce::FontOptions (13.5f));
            g.drawText (rowNumber == 0 ? juce::String ("(Default Sans)")
                                       : rows[(size_t) rowNumber],
                        8, 0, width - 8, height,
                        juce::Justification::centredLeft, false);
        }

        void selectedRowsChanged (int lastRowSelected) override
        {
            if (lastRowSelected >= 0 && lastRowSelected < rows.size())
                preview.setFontName (rows[(size_t) lastRowSelected]);
        }

        void refilter()
        {
            auto& provider = FontNameProvider::getInstance();
            const auto query = searchBox.getText().trim();

            rows.clear();
            rows.add (juce::String());   // the default always stays

            if (provider.isReady())
            {
                for (auto& name : provider.getNames())
                    if (query.isEmpty() || name.containsIgnoreCase (query))
                        rows.add (name);

                statusLabel.setText (
                    juce::String (rows.size() - 1) + " fonts on this machine",
                    juce::dontSendNotification);
            }
            else
            {
                statusLabel.setText ("Loading font list...",
                                     juce::dontSendNotification);
            }

            listBox.updateContent();
            listBox.selectRow (0);
        }

        void selectName (const juce::String& name)
        {
            const int index = rows.indexOf (name);
            listBox.selectRow (index >= 0 ? index : 0);
        }

        void pick (const juce::String& name)
        {
            if (onPick != nullptr)
                onPick (name);

            if (onDone != nullptr)
                onDone();
        }

        juce::TextEditor searchBox;
        juce::Label statusLabel;
        juce::ListBox listBox;
        FontSamplePreview preview;
        juce::TextButton useButton { "Use Font" };
        juce::TextButton cancelButton { "Cancel" };
        juce::StringArray rows;   // row 0 = "" (the default sans)
        std::function<void (const juce::String&)> onPick;
        std::function<void()> onDone;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelopFontPickerComponent)
    };

    void openFontPicker (const juce::String& initialName,
                         std::function<void (const juce::String&)> onPick)
    {
        if (fontPickerWindowInstance != nullptr)
        {
            fontPickerWindowInstance->toFront (true);
            return;
        }

        auto* content = new TelopFontPickerComponent (
            initialName, std::move (onPick),
            [] { closeWindowDeferred (fontPickerWindowInstance); });

        fontPickerWindowInstance = std::make_unique<FloatingWindow> (
            "Choose Telop Font",
            [] { closeWindowDeferred (fontPickerWindowInstance); });
        fontPickerWindowInstance->setContentOwned (content, true);
        fontPickerWindowInstance->centreWithSize (content->getWidth(),
                                                  content->getHeight());
        fontPickerWindowInstance->setVisible (true);
    }

    //==========================================================================
    /** Custom colour content: a juce::ColourSelector with OK /
        Cancel. Changes stream live through onLive (so the telop
        preview behind follows); Cancel — or the window's close
        button — replays the colour the selector was opened with. */
    class TelopColourComponent final : public juce::Component,
                                       private juce::ChangeListener
    {
    public:
        TelopColourComponent (juce::Colour initialColour,
                              std::function<void (juce::Colour)> onLiveToUse,
                              std::function<void()> onDoneToUse)
            : selector (juce::ColourSelector::showColourAtTop
                        | juce::ColourSelector::showSliders
                        | juce::ColourSelector::showColourspace),
              initial (initialColour),
              onLive (std::move (onLiveToUse)), onDone (std::move (onDoneToUse))
        {
            selector.setCurrentColour (initialColour, juce::dontSendNotification);
            selector.addChangeListener (this);
            addAndMakeVisible (selector);

            okButton.onClick = [this]
            {
                if (onDone != nullptr)
                    onDone();
            };
            addAndMakeVisible (okButton);

            cancelButton.onClick = [this] { cancelAndClose(); };
            addAndMakeVisible (cancelButton);

            setSize (360, 420);
        }

        ~TelopColourComponent() override
        {
            selector.removeChangeListener (this);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (10, 8);
            auto buttons = area.removeFromBottom (26);
            okButton.setBounds (buttons.removeFromRight (76));
            buttons.removeFromRight (8);
            cancelButton.setBounds (buttons.removeFromRight (76));
            area.removeFromBottom (8);
            selector.setBounds (area);
        }

        /** The window's close button takes the Cancel path. */
        void cancelFromWindowClose() { cancelAndClose(); }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            if (onLive != nullptr)
                onLive (selector.getCurrentColour());
        }

        void cancelAndClose()
        {
            if (onLive != nullptr)
                onLive (initial);

            if (onDone != nullptr)
                onDone();
        }

        juce::ColourSelector selector;
        const juce::Colour initial;
        juce::TextButton okButton { "OK" };
        juce::TextButton cancelButton { "Cancel" };
        std::function<void (juce::Colour)> onLive;
        std::function<void()> onDone;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelopColourComponent)
    };

    void openColourPicker (juce::Colour initialColour,
                           std::function<void (juce::Colour)> onLive)
    {
        if (colourWindowInstance != nullptr)
        {
            colourWindowInstance->toFront (true);
            return;
        }

        auto* content = new TelopColourComponent (
            initialColour, std::move (onLive),
            [] { closeWindowDeferred (colourWindowInstance); });

        colourWindowInstance = std::make_unique<FloatingWindow> (
            "Telop Colour",
            [content] { content->cancelFromWindowClose(); });
        colourWindowInstance->setContentOwned (content, true);
        colourWindowInstance->centreWithSize (content->getWidth(),
                                              content->getHeight());
        colourWindowInstance->setVisible (true);
    }

    //==========================================================================
    /** The style dialog's live preview: a small dusk "scene" (sky
        gradient, a lit stage band, a moon dot) with the pending
        style drawn over it by the renderer's own Telop::drawOnto —
        what you see is literally what the export will draw. */
    class TelopPreviewStrip final : public juce::Component
    {
    public:
        void setStyle (const TelopStyleData& newStyle)
        {
            style = newStyle;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const int width = juce::jmax (2, getWidth());
            const int height = juce::jmax (2, getHeight());

            juce::Image scene (juce::Image::RGB, width, height, true);

            {
                juce::Graphics sg (scene);
                juce::ColourGradient gradient (
                    juce::Colour (0xff2c3a55), 0.0f, 0.0f,
                    juce::Colour (0xff0c0e13), 0.0f, (float) height, false);
                sg.setGradientFill (gradient);
                sg.fillAll();

                sg.setColour (juce::Colour (0xff46586e).withAlpha (0.55f));
                sg.fillRect (0, height * 55 / 100, width, height * 18 / 100);

                sg.setColour (juce::Colour (0xffd8c9a0).withAlpha (0.85f));
                sg.fillEllipse (width * 0.78f, height * 0.12f, 13.0f, 13.0f);
            }

            Telop::drawOnto (scene, stripSampleText, style);
            g.drawImageAt (scene, 0, 0);
        }

    private:
        TelopStyleData style;
    };

    //==========================================================================
    /** Style dialog content. Holds a PENDING copy of the style:
        every control edits it and pushes it into the session at
        once (live apply — the preview window behind the dialog
        follows), while the snapshot taken on open is what Cancel
        restores. */
    class TelopStyleComponent final : public juce::Component
    {
    public:
        TelopStyleComponent (TimelineEngine& engineToUse,
                             std::function<void()> onDoneToUse)
            : engine (engineToUse), onDone (std::move (onDoneToUse)),
              snapshot (engineToUse.getTelopStyle()),
              pending (snapshot)
        {
            auto setupLabel = [this] (juce::Label& label, const juce::String& text)
            {
                label.setText (text, juce::dontSendNotification);
                label.setFont (juce::FontOptions (13.0f));
                label.setColour (juce::Label::textColourId, dimTextColour);
                addAndMakeVisible (label);
            };

            setupLabel (fontTitle, "Font");
            fontNameLabel.setFont (juce::FontOptions (13.0f));
            fontNameLabel.setColour (juce::Label::textColourId, textColour);
            addAndMakeVisible (fontNameLabel);

            chooseFontButton.onClick = [this]
            {
                juce::Component::SafePointer<TelopStyleComponent> safeThis (this);
                openFontPicker (pending.fontName,
                                [safeThis] (const juce::String& name)
                                {
                                    if (safeThis != nullptr)
                                    {
                                        safeThis->pending.fontName = name;
                                        safeThis->pushChange();
                                    }
                                });
            };
            addAndMakeVisible (chooseFontButton);

            setupLabel (sizeTitle, "Size");
            sizeSlider.setRange (40.0, 250.0, 1.0);
            sizeSlider.setValue (pending.scalePercent, juce::dontSendNotification);
            sizeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            sizeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
            sizeSlider.setTextValueSuffix ("%");
            sizeSlider.setColour (juce::Slider::thumbColourId, accentColour);
            sizeSlider.onValueChange = [this]
            {
                pending.scalePercent = (float) sizeSlider.getValue();
                pushChange();
            };
            addAndMakeVisible (sizeSlider);

            setupLabel (positionTitle, "Position");
            positionBox.addItem ("Bottom", 1);
            positionBox.addItem ("Centre", 2);
            positionBox.addItem ("Top", 3);
            positionBox.setSelectedId ((int) pending.vertical + 1,
                                       juce::dontSendNotification);
            positionBox.onChange = [this]
            {
                pending.vertical = (TelopVerticalPosition)
                    juce::jlimit (0, 2, positionBox.getSelectedId() - 1);
                pushChange();
            };
            addAndMakeVisible (positionBox);

            setupLabel (alignTitle, "Align");
            alignBox.addItem ("Left", 1);
            alignBox.addItem ("Centre", 2);
            alignBox.addItem ("Right", 3);
            alignBox.setSelectedId ((int) pending.align + 1,
                                    juce::dontSendNotification);
            alignBox.onChange = [this]
            {
                pending.align = (TelopHorizontalAlign)
                    juce::jlimit (0, 2, alignBox.getSelectedId() - 1);
                pushChange();
            };
            addAndMakeVisible (alignBox);

            setupLabel (colourTitle, "Colour");

            for (auto preset : presetColours)
            {
                auto swatch = std::make_unique<juce::TextButton>();
                swatch->setColour (juce::TextButton::buttonColourId, preset);
                swatch->setColour (juce::TextButton::buttonOnColourId, preset);
                swatch->onClick = [this, preset]
                {
                    pending.colourArgb = preset.getARGB();
                    pushChange();
                };
                addAndMakeVisible (*swatch);
                swatches.push_back (std::move (swatch));
            }

            customColourButton.onClick = [this]
            {
                juce::Component::SafePointer<TelopStyleComponent> safeThis (this);
                openColourPicker (juce::Colour (pending.colourArgb),
                                  [safeThis] (juce::Colour colour)
                                  {
                                      if (safeThis != nullptr)
                                      {
                                          safeThis->pending.colourArgb = colour.getARGB();
                                          safeThis->pushChange();
                                      }
                                  });
            };
            addAndMakeVisible (customColourButton);

            addAndMakeVisible (previewStrip);

            okButton.onClick = [this]
            {
                if (onDone != nullptr)
                    onDone();
            };
            addAndMakeVisible (okButton);

            cancelButton.onClick = [this] { cancelAndClose(); };
            addAndMakeVisible (cancelButton);

            setSize (500, 478);
            refreshLabels();
            previewStrip.setStyle (pending);
            refreshSwatches();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12, 10);

            auto buttons = area.removeFromBottom (26);
            okButton.setBounds (buttons.removeFromRight (76));
            buttons.removeFromRight (8);
            cancelButton.setBounds (buttons.removeFromRight (76));
            area.removeFromBottom (10);

            previewStrip.setBounds (area.removeFromBottom (128));
            area.removeFromBottom (10);

            auto fontRow = area.removeFromTop (26);
            fontTitle.setBounds (fontRow.removeFromLeft (64));
            chooseFontButton.setBounds (fontRow.removeFromRight (92));
            fontRow.removeFromRight (8);
            fontNameLabel.setBounds (fontRow);
            area.removeFromTop (8);

            auto sizeRow = area.removeFromTop (26);
            sizeTitle.setBounds (sizeRow.removeFromLeft (64));
            sizeSlider.setBounds (sizeRow);
            area.removeFromTop (8);

            auto positionRow = area.removeFromTop (26);
            positionTitle.setBounds (positionRow.removeFromLeft (64));
            positionBox.setBounds (positionRow.removeFromLeft (160));
            area.removeFromTop (8);

            auto alignRow = area.removeFromTop (26);
            alignTitle.setBounds (alignRow.removeFromLeft (64));
            alignBox.setBounds (alignRow.removeFromLeft (160));
            area.removeFromTop (8);

            auto colourRow = area.removeFromTop (26);
            colourTitle.setBounds (colourRow.removeFromLeft (64));

            for (auto& swatch : swatches)
            {
                swatch->setBounds (colourRow.removeFromLeft (24));
                colourRow.removeFromLeft (5);
            }

            colourRow.removeFromLeft (3);
            customColourButton.setBounds (colourRow.removeFromLeft (84));
        }

        /** The window's close button takes the Cancel path. */
        void cancelFromWindowClose() { cancelAndClose(); }

    private:
        void pushChange()
        {
            engine.setTelopStyle (pending);
            previewStrip.setStyle (pending);
            refreshLabels();
            refreshSwatches();
        }

        void cancelAndClose()
        {
            engine.setTelopStyle (snapshot);

            if (onDone != nullptr)
                onDone();
        }

        void refreshLabels()
        {
            if (pending.fontName.isEmpty())
            {
                fontNameLabel.setText ("(Default Sans)",
                                       juce::dontSendNotification);
                return;
            }

            // The selected font is the ONE name this dialog resolves
            // (through the renderer's cache): when a project arrives
            // from a machine that has a font this one lacks, the
            // label says so — the telop itself falls back to the
            // default sans, exactly as the renderer does.
            const bool available =
                Telop::getTypefaceForName (pending.fontName) != nullptr;

            fontNameLabel.setText (
                available ? pending.fontName
                          : pending.fontName + "  (not on this machine)",
                juce::dontSendNotification);
        }

        void refreshSwatches()
        {
            for (size_t i = 0; i < swatches.size(); ++i)
            {
                const bool selected =
                    presetColours[i].getARGB() == pending.colourArgb;

                // A check mark in the contrasting shade marks the
                // active preset (custom colours match none). M6.9:
                // fromUTF8, like the other non-ASCII labels (a plain
                // literal would mojibake — see the sample texts).
                swatches[i]->setButtonText (selected ? juce::String::fromUTF8 ("\xe2\x9c\x93")
                                                     : juce::String());
                swatches[i]->setColour (
                    juce::TextButton::textColourOffId,
                    presetColours[i].getPerceivedBrightness() >= 0.5f
                        ? juce::Colours::black : juce::Colours::white);
            }
        }

        static inline const juce::Colour presetColours[] = {
            juce::Colour (0xffffffff),   // white
            juce::Colour (0xffffff00),   // yellow
            juce::Colour (0xff00ffff),   // cyan
            juce::Colour (0xff00ff00),   // green
            juce::Colour (0xffff0000),   // red
            juce::Colour (0xffff00ff),   // magenta
            juce::Colour (0xff000000),   // black
        };

        TimelineEngine& engine;
        std::function<void()> onDone;
        const TelopStyleData snapshot;
        TelopStyleData pending;

        juce::Label fontTitle, fontNameLabel, sizeTitle,
                    positionTitle, alignTitle, colourTitle;
        juce::TextButton chooseFontButton { "Choose..." };
        juce::Slider sizeSlider;
        juce::ComboBox positionBox, alignBox;
        std::vector<std::unique_ptr<juce::TextButton>> swatches;
        juce::TextButton customColourButton { "Custom..." };
        TelopPreviewStrip previewStrip;
        juce::TextButton okButton { "OK" };
        juce::TextButton cancelButton { "Cancel" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelopStyleComponent)
    };
}

//==============================================================================
namespace TelopStyleUI
{
    void showDialog (TimelineEngine& engine)
    {
        if (styleWindowInstance != nullptr)
        {
            styleWindowInstance->toFront (true);
            return;
        }

        auto* content = new TelopStyleComponent (
            engine, [] { closeAllDialogWindows(); });

        styleWindowInstance = std::make_unique<FloatingWindow> (
            "Telop Style",
            [content] { content->cancelFromWindowClose(); });
        styleWindowInstance->setContentOwned (content, true);
        styleWindowInstance->centreWithSize (content->getWidth(),
                                             content->getHeight());
        styleWindowInstance->setVisible (true);
    }
}
