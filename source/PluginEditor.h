#pragma once

#include "PluginProcessor.h"
#include "ui/AboutPanel.h"
#include "ui/EqDisplay.h"
#include "ui/IconButton.h"
#include "ui/ParameterControl.h"
#include "ui/PluginLookAndFeel.h"
#include "ui/Theme.h"
#include "ui/WaveformDisplay.h"

/**
    SPACE's window.

    Toolbar across the top, then one row: the response's own controls at the left, the
    impulse response, the post EQ, and the controls for what comes out at the right.
    Both displays are on screen at once and each carries a header naming it and holding
    the one or two buttons that belong to it.

    This replaced a tab bar. Tabs were the wrong shape for the plugin: the two views
    are not alternatives, they are two ends of one signal path, and shaping a response
    while listening to what the EQ does to it meant switching back and forth to see
    either half of what you were doing.
*/
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void refreshWaveform();
    void refreshFromParameters();

    void chooseImpulseResponse();
    void loadImpulseResponse (const juce::File&);

    void showSettingsMenu();
    void refreshBypassLook();

    /** Only the edge that moved, so a drag of one does not record host automation for
        the other at a value nobody asked to change. */
    void setEdge (WaveformDisplay::Edge, float position);
    void beginEdgeGesture (WaveformDisplay::Edge, bool starting);

    /** The fades, in seconds of the source file, which is what both the display and
        the parameters deal in -- so this is a change of unit and nothing more. */
    void setFade (WaveformDisplay::Fade, float seconds);
    void beginFadeGesture (WaveformDisplay::Fade, bool starting);

    void setBand (EqDisplay::Band, float frequency, float gainDb);
    void beginBandGesture (EqDisplay::Band, bool starting);

    void setCut (EqDisplay::Cut, float frequency);
    void beginCutGesture (EqDisplay::Cut, bool starting);

    static const char* cutParamFor (EqDisplay::Cut) noexcept;

    static const char* frequencyParamFor (EqDisplay::Band) noexcept;
    static const char* gainParamFor (EqDisplay::Band) noexcept;

    /** True when any EQ parameter sits away from where it started. Every one of them,
        not just the gains: a band whose frequency has been moved is a band somebody
        has touched, and a reset that left it there would not be one. */
    bool isEqModified() const;
    void resetEq();

    PluginProcessor& processorRef;
    PluginLookAndFeel lookAndFeel;

    // Every control in the window has a tooltip and none of them can show one without
    // this: JUCE needs a window to draw them in, and there is no default.
    juce::TooltipWindow tooltips { this, 600 };

    Celine::IconButton bypassButton { "Bypass", "power-off-solid-full.svg" };
    Celine::IconButton settingsButton { "Settings", "gear-solid-full.svg" };
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;

    WaveformDisplay waveform;
    EqDisplay eq;

    // The section headers: what each panel is, what it currently holds, and the
    // buttons belonging to it.
    // No status line under the EQ title: whether the curve is doing anything is
    // already on the curve, and the reset button beside it goes live only when there
    // is something to reset.
    juce::Label irTitle, irStatus, eqTitle;

    // Loading used to hang off the impulse tab. With the tabs gone it belongs where
    // everything else about the response is.
    juce::TextButton loadButton { "LOAD" };

    // The head of a response is a sliver at full width, and it is the part that
    // decides whether a reverb sounds tight. This is how you reach it.
    juce::TextButton zoomButton { "ZOOM" };

    // Its opposite number on the EQ stage, in the same corner. Disabled while the EQ
    // is already flat, so the button reports the state as well as changing it.
    juce::TextButton eqResetButton { "RESET" };

    // Flanking the pair of displays: what shapes the room on the left, what comes out
    // of it on the right. Four faders rather than two, which is what the bottom row
    // used to be for -- a control that shapes the sound belongs beside the picture of
    // it, not in a strip underneath.
    //
    // No fader for the fades: those are dragged on the response itself, where you can
    // see what they are doing to it. And none for output gain, which was a trim on
    // top of a normalised response and a mix control that already sets the level.
    FaderControl sizeFader, preDelayFader;
    FaderControl widthFader, mixFader;

    // Q belongs to one band rather than to the curve, so it has no handle to be
    // dragged by and needs a control of its own. It rides in the EQ's header.
    SliderRowControl peakQRow;

    std::unique_ptr<juce::FileChooser> fileChooser;

    std::unique_ptr<juce::Drawable> logo, wordmark;
    juce::Rectangle<int> logoBounds, wordmarkBounds, toolbarBand;
    juce::Label wordmarkText;

    // Fade state for the spectrum: the last frame count seen from the analyser, and
    // how far the trace has decayed since that stopped changing.
    std::int64_t lastFrameCount = -1;
    int ticksWithoutFrames = 0;
    float spectrumFade = 1.0f;

    std::vector<float> spectrumScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
