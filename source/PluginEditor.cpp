#include "PluginEditor.h"

#include "ProductInfo.h"

#include "ui/EmbeddedAssets.h"
#include "ui/Fonts.h"

using namespace Celine;

namespace
{
    // Locked, so the window can only be scaled rather than reproportioned. Wide,
    // because it now holds two displays side by side and four faders; both displays
    // are time or frequency against a much smaller vertical range, so the height they
    // need is set by the faders beside them rather than by the plots.
    constexpr float aspectRatio = 2.37f;

    constexpr int defaultWidth = 1180;
    constexpr int defaultHeight = (int) (defaultWidth / aspectRatio);

    // Two displays have to stay legible at the minimum, not just fit: below this the
    // EQ's decade labels start colliding.
    constexpr int minimumWidth = 960;

    constexpr int headerHeight = Theme::toolbarHeight;

    // The strip above each display, holding its name and its buttons.
    constexpr int sectionHeaderHeight = 26;
    constexpr int sectionButtonWidth = 62;

    constexpr int gap = 10;
    constexpr int faderWidth = 76;

    const juce::String ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      sizeFader (p.getAPVTS(), ParamID::size, "Size"),
      preDelayFader (p.getAPVTS(), ParamID::preDelay, "Pre-delay"),
      widthFader (p.getAPVTS(), ParamID::width, "Width"),
      mixFader (p.getAPVTS(), ParamID::mix, "Dry/Wet"),
      peakQRow (p.getAPVTS(), ParamID::peakQ, "Q")
{
    setLookAndFeel (&lookAndFeel);

    // Read first, before anything below can fire resized(). Everything that lays the
    // window out writes the size back into the state, so reading afterwards returns
    // whatever the last of those wrote -- which, before setSize has been called, is
    // zero. Every instance then opened at nothing.
    const auto& state = processorRef.getAPVTS().state;
    const auto storedWidth = (int) state.getProperty ("uiWidth", defaultWidth);
    const auto storedHeight = (int) state.getProperty ("uiHeight", defaultHeight);

    logo = Assets::drawable ("logo.svg");

    if (logo != nullptr)
        Assets::tint (*logo, Theme::text());

    wordmark = Assets::drawable (ProductInfo::wordmarkAsset, Assets::IfMissing::returnNull);

    if (wordmark != nullptr)
        Assets::tint (*wordmark, Theme::text());

    wordmarkText.setText (juce::String (JucePlugin_Name).toLowerCase(), juce::dontSendNotification);
    wordmarkText.setFont (Fonts::logo (22.0f));
    wordmarkText.setColour (juce::Label::textColourId, Theme::text());
    wordmarkText.setJustificationType (juce::Justification::centredLeft);
    wordmarkText.setInterceptsMouseClicks (false, false);
    addChildComponent (wordmarkText);
    wordmarkText.setVisible (wordmark == nullptr);

    bypassButton.setClickingTogglesState (true);
    bypassButton.onClick = [this] { refreshBypassLook(); };
    addAndMakeVisible (bypassButton);

    bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (
        *processorRef.getAPVTS().getParameter (ParamID::bypass), bypassButton);

    refreshBypassLook();

    settingsButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (settingsButton);

    //==========================================================================
    const auto title = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (Fonts::bold (10.0f));
        label.setColour (juce::Label::textColourId, Theme::textDim());
        label.setJustificationType (juce::Justification::centredLeft);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
    };

    title (irTitle, "IMPULSE RESPONSE");
    title (eqTitle, "POST EQ");

    irStatus.setFont (Fonts::light (11.0f));
    irStatus.setColour (juce::Label::textColourId, Theme::comment());
    irStatus.setJustificationType (juce::Justification::centredLeft);
    irStatus.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (irStatus);

    // The same colours the impulse tab gave it, so the one button that changes what
    // the plugin is loaded with still reads as the one button that does.
    // A tooltip paints a rounded panel, so it must not be opaque -- an opaque component
    // has to fill every pixel it owns, and the four corners outside the rounding are
    // exactly the ones it does not paint; left opaque they came out as square spikes of
    // whatever was in the buffer. TooltipWindow sets the flag in its constructor and
    // offers no way to ask otherwise. Safe because this one is parented to the editor
    // rather than put on the desktop, so what shows through the corners is this window.
    tooltips.setOpaque (false);

    loadButton.setTooltip ("Load a response to convolve with. A file can also be "
                           "drag-and-dropped.");
    loadButton.setColour (juce::TextButton::buttonColourId, Theme::record().withAlpha (0.22f));
    loadButton.setColour (juce::TextButton::textColourOffId, Theme::record().brighter (0.35f));
    loadButton.onClick = [this] { chooseImpulseResponse(); };
    addAndMakeVisible (loadButton);

    waveform.setTooltip ("A visual of the impulse response. Drag its ends to trim it and "
                         "its corners to fade it in or out.");
    addAndMakeVisible (waveform);

    eq.setTooltip ("An EQ affecting the wet signal.");
    addAndMakeVisible (eq);

    waveform.onEdgeDragged = [this] (WaveformDisplay::Edge edge, float position) { setEdge (edge, position); };
    waveform.onEdgeGesture = [this] (WaveformDisplay::Edge edge, bool starting) { beginEdgeGesture (edge, starting); };
    waveform.onFadeDragged = [this] (WaveformDisplay::Fade fade, float seconds) { setFade (fade, seconds); };
    waveform.onFadeGesture = [this] (WaveformDisplay::Fade fade, bool starting) { beginFadeGesture (fade, starting); };
    waveform.onFileDropped = [this] (const juce::File& file) { loadImpulseResponse (file); };

    // Taken from the parameters rather than written down here, so a range that
    // changes in one place cannot leave the display clamping to the old one.
    for (const auto band : { EqDisplay::Band::low, EqDisplay::Band::peak, EqDisplay::Band::high })
    {
        if (const auto* parameter = dynamic_cast<const juce::RangedAudioParameter*> (
                processorRef.getAPVTS().getParameter (frequencyParamFor (band))))
        {
            const auto range = parameter->getNormalisableRange();
            eq.setBandRange (band, range.start, range.end);
        }
    }

    for (const auto cut : { EqDisplay::Cut::high, EqDisplay::Cut::low })
    {
        if (const auto* parameter = dynamic_cast<const juce::RangedAudioParameter*> (
                processorRef.getAPVTS().getParameter (cutParamFor (cut))))
        {
            const auto range = parameter->getNormalisableRange();
            eq.setCutRange (cut, range.start, range.end);
        }
    }

    eq.setFftSize (processorRef.getSpectrumFftSize());
    eq.magnitudeDbAt = [this] (float frequency) { return processorRef.getEqMagnitudeDb (frequency); };
    eq.onBandDragged = [this] (EqDisplay::Band band, float frequency, float gainDb) { setBand (band, frequency, gainDb); };
    eq.onBandGesture = [this] (EqDisplay::Band band, bool starting) { beginBandGesture (band, starting); };
    eq.onCutDragged = [this] (EqDisplay::Cut cut, float frequency) { setCut (cut, frequency); };
    eq.onCutGesture = [this] (EqDisplay::Cut cut, bool starting) { beginCutGesture (cut, starting); };

    zoomButton.setTooltip ("Zoom to the first few milliseconds, for precise adjustments.");
    zoomButton.setClickingTogglesState (true);
    zoomButton.setColour (juce::TextButton::buttonColourId, Theme::surface());
    zoomButton.setColour (juce::TextButton::buttonOnColourId, Theme::accent());
    zoomButton.setColour (juce::TextButton::textColourOffId, Theme::text());
    zoomButton.setColour (juce::TextButton::textColourOnId, Theme::chrome());
    zoomButton.onClick = [this]
    {
        waveform.setZoomed (zoomButton.getToggleState());
        refreshWaveform();
    };
    addAndMakeVisible (zoomButton);

    eqResetButton.setTooltip ("Reset the EQ settings.");
    eqResetButton.setColour (juce::TextButton::buttonColourId, Theme::surface());
    eqResetButton.setColour (juce::TextButton::textColourOffId, Theme::text());
    eqResetButton.onClick = [this] { resetEq(); };
    addAndMakeVisible (eqResetButton);

    sizeFader.getSlider().setTooltip ("Artificially stretches the response in time.");
    preDelayFader.getSlider().setTooltip ("Adds a pre-delay to the impulse response.");
    widthFader.getSlider().setTooltip ("Controls the stereo width of the reverb.");
    mixFader.getSlider().setTooltip ("Controls the mix of the wet signal.");
    peakQRow.getSlider().setTooltip ("Controls the Q setting of the bell EQ curve.");

    for (auto* fader : { &sizeFader, &preDelayFader, &widthFader, &mixFader })
        addAndMakeVisible (fader);

    // Standing in the EQ's header rather than on the light strip it was built for.
    peakQRow.setOnDark();
    peakQRow.setCompact();
    addAndMakeVisible (peakQRow);

    refreshFromParameters();
    refreshWaveform();

    //==========================================================================
    setResizable (true, true);
    setResizeLimits (minimumWidth, (int) (minimumWidth / aspectRatio),
                     minimumWidth * 3, (int) (minimumWidth * 3 / aspectRatio));

    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) aspectRatio);

    setSize (storedWidth, storedHeight);

    processorRef.setUiActive (true);
    startTimerHz (30);
}

PluginEditor::~PluginEditor()
{
    processorRef.setUiActive (false);
    setLookAndFeel (nullptr);
}

//==============================================================================
void PluginEditor::refreshWaveform()
{
    const auto& impulse = processorRef.getImpulseResponse();

    waveform.setFileName (impulse.getName());
    waveform.setLengthSeconds (impulse.getLengthSeconds());

    // One peak per pixel column of whatever width the display currently has, over the
    // window it is showing -- so zooming re-summarises rather than stretching the
    // pixels it already had.
    // Matched to the plot area rather than the component, so a column is a pixel
    // rather than very nearly one.
    const auto columns = juce::jmax (1, waveform.getPlotWidth());

    std::vector<float> peaks;
    impulse.summarise (peaks, columns, waveform.getViewStart(), waveform.getViewEnd());
    waveform.setWaveform (std::move (peaks));
}

void PluginEditor::refreshFromParameters()
{
    auto& apvts = processorRef.getAPVTS();
    const auto value = [&apvts] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    waveform.setRegion (value (ParamID::irStart), value (ParamID::irEnd));

    // Milliseconds of the file into seconds of it, which is the axis the display
    // draws on -- and Size does not enter into it, which is the point. See the fade
    // handling in ImpulseResponse::shape.
    waveform.setFades (value (ParamID::fadeIn) * 0.001f,
                       value (ParamID::fadeOut) * 0.001f);

    eq.setCut (EqDisplay::Cut::high, value (ParamID::highPass));
    eq.setCut (EqDisplay::Cut::low, value (ParamID::lowPass));

    eq.setBand (EqDisplay::Band::low, { value (ParamID::lowFreq), value (ParamID::lowGain) });
    eq.setBand (EqDisplay::Band::peak, { value (ParamID::peakFreq), value (ParamID::peakGain) });
    eq.setBand (EqDisplay::Band::high, { value (ParamID::highFreq), value (ParamID::highGain) });

    const auto& impulse = processorRef.getImpulseResponse();

    irStatus.setText (impulse.isEmpty()
                          ? juce::String ("Nothing loaded")
                          : impulse.getName() + "   " + juce::String (impulse.getLengthSeconds(), 2) + " s",
                      juce::dontSendNotification);

    // Greyed out when there is nothing to undo. A button that is always live invites
    // the click that finds out it did nothing.
    eqResetButton.setEnabled (isEqModified());
}

bool PluginEditor::isEqModified() const
{
    const auto& apvts = processorRef.getAPVTS();

    for (const auto* id : ParamID::eqShaping)
    {
        const auto* parameter = apvts.getParameter (id);

        if (parameter == nullptr)
            continue;

        // Compared where the host compares them, on the normalised value, so a
        // parameter whose range is skewed cannot read as moved when it is not.
        if (std::abs (parameter->getValue() - parameter->getDefaultValue()) > 1.0e-4f)
            return true;
    }

    return false;
}

void PluginEditor::resetEq()
{
    auto& apvts = processorRef.getAPVTS();

    for (const auto* id : ParamID::eqShaping)
    {
        if (auto* parameter = apvts.getParameter (id))
        {
            // Bracketed one parameter at a time, so a host records seven edits it can
            // undo rather than a burst of writes with no gesture around them.
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
            parameter->endChangeGesture();
        }
    }

    refreshFromParameters();
}

void PluginEditor::timerCallback()
{
    refreshFromParameters();

    // The drawn curve is built from the parameters, on this thread, once a frame --
    // not read out of the running filters, which the audio thread owns and which lag
    // the controls by the length of their ramp.
    processorRef.refreshDisplayEq();

    eq.setSampleRate (processorRef.getSampleRate());

    // The trace fades out when the analyser stops producing frames, rather than
    // standing still on the last one. Hosts differ on what they do to a plugin when
    // the transport stops -- some keep calling processBlock with silence, some stop
    // calling it at all -- and driving this off the frame counter covers both,
    // because it asks the question that matters: is anything still arriving?
    const auto frames = processorRef.getSpectrumFrameCount();

    // The grace period is the whole trick. This timer runs at 30 Hz and the analyser
    // produces a frame every half window -- 23 a second at 48 kHz -- so one tick in
    // five finds no new frame even while audio is streaming perfectly well. Dimming on
    // the first such tick and snapping back on the next flickered the entire trace
    // several times a second, which is what this looked like.
    //
    // Eight ticks is a quarter of a second: far longer than the gap between frames,
    // far shorter than anyone would call a hang.
    constexpr int ticksBeforeFading = 8;

    if (frames != lastFrameCount)
    {
        lastFrameCount = frames;
        ticksWithoutFrames = 0;
        spectrumFade = 1.0f;
    }
    else if (++ticksWithoutFrames > ticksBeforeFading)
    {
        spectrumFade *= 0.86f;
    }

    eq.setSpectrumFade (spectrumFade);

    if (processorRef.getOutputSpectrum (spectrumScratch))
        eq.setSpectrum (spectrumScratch);

    if (processorRef.getDrySpectrum (spectrumScratch))
        eq.setDrySpectrum (spectrumScratch);

    if (eq.isVisible())
        eq.repaint();
}

//==============================================================================
void PluginEditor::chooseImpulseResponse()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load an impulse response",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aiff;*.aif;*.flac;*.ogg");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (file.existsAsFile())
            loadImpulseResponse (file);
    });
}

void PluginEditor::loadImpulseResponse (const juce::File& file)
{
    const auto result = processorRef.loadImpulseResponse (file);

    if (result.failed())
    {
        // Said out loud rather than swallowed. A response that silently fails to load
        // leaves the plugin sounding like the previous one, which is the most
        // confusing thing it could do.
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Could not load that impulse response",
            result.getErrorMessage(),
            this);
        return;
    }

    refreshWaveform();
    refreshFromParameters();
}

//==============================================================================
const char* PluginEditor::frequencyParamFor (EqDisplay::Band band) noexcept
{
    switch (band)
    {
        case EqDisplay::Band::low:  return ParamID::lowFreq;
        case EqDisplay::Band::peak: return ParamID::peakFreq;
        case EqDisplay::Band::high: return ParamID::highFreq;
    }

    return ParamID::peakFreq;
}

const char* PluginEditor::gainParamFor (EqDisplay::Band band) noexcept
{
    switch (band)
    {
        case EqDisplay::Band::low:  return ParamID::lowGain;
        case EqDisplay::Band::peak: return ParamID::peakGain;
        case EqDisplay::Band::high: return ParamID::highGain;
    }

    return ParamID::peakGain;
}

void PluginEditor::setBand (EqDisplay::Band band, float frequency, float gainDb)
{
    auto& apvts = processorRef.getAPVTS();

    const auto write = [&apvts] (const char* id, float value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    write (frequencyParamFor (band), frequency);
    write (gainParamFor (band), gainDb);
}

void PluginEditor::beginBandGesture (EqDisplay::Band band, bool starting)
{
    auto& apvts = processorRef.getAPVTS();

    // Both, here, because dragging a band on the curve genuinely moves both.
    for (const auto* id : { frequencyParamFor (band), gainParamFor (band) })
        if (auto* parameter = apvts.getParameter (id))
            starting ? parameter->beginChangeGesture() : parameter->endChangeGesture();
}

void PluginEditor::setEdge (WaveformDisplay::Edge edge, float position)
{
    const auto* id = edge == WaveformDisplay::Edge::start ? ParamID::irStart : ParamID::irEnd;

    if (auto* parameter = processorRef.getAPVTS().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (position));
}

void PluginEditor::beginEdgeGesture (WaveformDisplay::Edge edge, bool starting)
{
    const auto* id = edge == WaveformDisplay::Edge::start ? ParamID::irStart : ParamID::irEnd;

    if (auto* parameter = processorRef.getAPVTS().getParameter (id))
        starting ? parameter->beginChangeGesture() : parameter->endChangeGesture();
}

const char* PluginEditor::cutParamFor (EqDisplay::Cut cut) noexcept
{
    return cut == EqDisplay::Cut::high ? ParamID::highPass : ParamID::lowPass;
}

void PluginEditor::setCut (EqDisplay::Cut cut, float frequency)
{
    if (auto* parameter = processorRef.getAPVTS().getParameter (cutParamFor (cut)))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (frequency));
}

void PluginEditor::beginCutGesture (EqDisplay::Cut cut, bool starting)
{
    if (auto* parameter = processorRef.getAPVTS().getParameter (cutParamFor (cut)))
        starting ? parameter->beginChangeGesture() : parameter->endChangeGesture();
}

void PluginEditor::setFade (WaveformDisplay::Fade fade, float seconds)
{
    const auto* id = fade == WaveformDisplay::Fade::in ? ParamID::fadeIn : ParamID::fadeOut;

    if (auto* parameter = processorRef.getAPVTS().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (seconds * 1000.0f));
}

void PluginEditor::beginFadeGesture (WaveformDisplay::Fade fade, bool starting)
{
    const auto* id = fade == WaveformDisplay::Fade::in ? ParamID::fadeIn : ParamID::fadeOut;

    if (auto* parameter = processorRef.getAPVTS().getParameter (id))
        starting ? parameter->beginChangeGesture() : parameter->endChangeGesture();
}

//==============================================================================
void PluginEditor::showSettingsMenu()
{
    juce::PopupMenu menu;

    juce::PopupMenu::Item about ("About " + juce::String (JucePlugin_Name) + ellipsis);
    about.setAction ([this] { showAboutWindow (this); });
    menu.addItem (about);

    // A menu has no parent to inherit a look and feel from.
    menu.setLookAndFeel (&lookAndFeel);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton));
}

void PluginEditor::refreshBypassLook()
{
    bypassButton.setActive (bypassButton.getToggleState());
}

//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    // The surround is the darkest thing here, so the displays and the faders read as
    // openings rather than as boxes.
    g.fillAll (Theme::consoleBackground());

    g.setColour (Theme::chrome());
    g.fillRect (toolbarBand);

    // Both marks drawn off their ink rather than their viewBox: artwork is rarely
    // centred in the box it was exported in, so placing it by the box sits it high.
    if (logo != nullptr && ! logoBounds.isEmpty())
        logo->drawWithin (g, logoBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);

    if (wordmark != nullptr && ! wordmarkBounds.isEmpty())
        Assets::drawWordmark (g, *wordmark, wordmarkBounds.toFloat());
}

void PluginEditor::resized()
{
    auto& state = processorRef.getAPVTS().state;

    // Zero is not a size worth remembering. resized() runs during construction,
    // before setSize, and storing what it sees then is how the stored size becomes
    // the reason the window will not open.
    if (state.isValid() && getWidth() > 0 && getHeight() > 0)
    {
        state.setProperty ("uiWidth", getWidth(), nullptr);
        state.setProperty ("uiHeight", getHeight(), nullptr);
    }

    auto area = getLocalBounds();
    toolbarBand = area.removeFromTop (headerHeight);

    {
        auto header = toolbarBand.reduced (gap + 2, 0);

        const auto place = [&header] (const std::unique_ptr<juce::Drawable>& art,
                                      int height, juce::Rectangle<int>& out)
        {
            if (art == nullptr)
                return;

            const auto ink = art->getDrawableBounds();
            const auto aspect = ink.getHeight() > 0.0f ? ink.getWidth() / ink.getHeight() : 1.0f;
            const auto width = juce::roundToInt ((float) height * aspect);

            out = header.removeFromLeft (width).withSizeKeepingCentre (width, height);
        };

        place (logo, 20, logoBounds);
        header.removeFromLeft (14);

        if (wordmark != nullptr)
            place (wordmark, 14, wordmarkBounds);
        else
            wordmarkText.setBounds (header.removeFromLeft (200));

        const auto square = [&header] (juce::Component& c)
        {
            c.setBounds (header.removeFromRight (Theme::buttonSize)
                             .withSizeKeepingCentre (Theme::buttonSize, Theme::buttonSize));
            header.removeFromRight (Theme::buttonGap);
        };

        square (settingsButton);
        square (bypassButton);
    }

    area = area.reduced (gap, 0);
    area.removeFromTop (gap);
    area.removeFromBottom (gap);

    // The faders first, from the outside in. Size and Pre-delay shape the room; Width
    // and Dry/Wet decide what arrives of it. Reading left to right across the window
    // is then the signal path: what the room is, what it does, what you hear of it.
    //
    // Stacked, two to a column rather than four abreast. A single column of faders is
    // narrow enough to read as an edge to the window, and what it gives up in fader
    // travel it hands to the displays, which are the part worth the width.
    const auto stack = [] (juce::Rectangle<int> column, juce::Component& upper,
                           juce::Component& lower)
    {
        const auto half = (column.getHeight() - gap) / 2;
        upper.setBounds (column.removeFromTop (half));
        column.removeFromTop (gap);
        lower.setBounds (column);
    };

    stack (area.removeFromLeft (faderWidth), sizeFader, preDelayFader);
    area.removeFromLeft (gap);

    stack (area.removeFromRight (faderWidth), widthFader, mixFader);
    area.removeFromRight (gap);

    // What is left is split evenly between the two displays. Evenly rather than by
    // what each needs: they are the two halves of one path, and giving one of them
    // more room says it is the more important half.
    const auto columnWidth = (area.getWidth() - gap) / 2;
    auto irColumn = area.removeFromLeft (columnWidth);
    area.removeFromLeft (gap);
    auto eqColumn = area;

    /** Lays out one section: its header strip, then the panel under it. Returns what
        is left for the display, so the two columns cannot drift apart by a pixel. */
    const auto layOutHeader = [] (juce::Rectangle<int>& column, juce::Label& sectionTitle,
                                  int titleWidth)
    {
        auto header = column.removeFromTop (sectionHeaderHeight);
        column.removeFromTop (gap / 2);

        sectionTitle.setBounds (header.removeFromLeft (titleWidth));
        header.removeFromLeft (gap);

        return header;
    };

    /** Puts a status line in whatever the buttons left of a header.

        Height first: a Label fits as many lines as it has room for, and a 26 pixel
        strip has room for two -- so a name that would not fit came out stacked on top
        of its own length rather than trimmed. Sixteen pixels is one line and no more.

        Then whether to draw it at all. Squeezed to nothing the text does not vanish,
        it becomes two or three letters that read as a glitch. */
    const auto layOutStatus = [] (juce::Label& label, juce::Rectangle<int> strip)
    {
        label.setVisible (strip.getWidth() >= 70);
        label.setBounds (strip.withSizeKeepingCentre (strip.getWidth(), 16));
    };

    {
        auto header = layOutHeader (irColumn, irTitle, 118);

        // Right to left, so the buttons keep their places and the status takes
        // whatever is left -- which at the narrow end is nothing, and it simply goes
        // away rather than pushing a button off the edge.
        zoomButton.setBounds (header.removeFromRight (sectionButtonWidth));
        header.removeFromRight (gap / 2);
        loadButton.setBounds (header.removeFromRight (sectionButtonWidth));
        header.removeFromRight (gap);

        layOutStatus (irStatus, header);
        waveform.setBounds (irColumn);
    }

    {
        auto header = layOutHeader (eqColumn, eqTitle, 58);

        eqResetButton.setBounds (header.removeFromRight (sectionButtonWidth));
        header.removeFromRight (gap / 2);

        // Centred in what the title and the button leave, rather than pushed up
        // against the button. The title takes about as much off the left as the
        // button does off the right, so centring here also lands it in the middle of
        // the panel -- and with the status line gone there is nothing else in this
        // strip to balance the empty half against.
        peakQRow.setBounds (header.withSizeKeepingCentre (juce::jmin (190, header.getWidth()),
                                                          header.getHeight()));

        eq.setBounds (eqColumn);
    }

    refreshWaveform();
}
