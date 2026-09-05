#pragma once

#include "PlotGeometry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

/**
    The post EQ: its curve, over a moving picture of what is coming out of the plugin.

    Deliberately the same footprint and the same ground as the waveform view, so that
    switching tabs moves the contents of one panel rather than replacing the panel.
    The two share PlotGeometry's frequency axis for the same reason.

    The three bands are draggable on the curve -- across for frequency, up and down for
    gain -- because a parametric EQ whose only controls are numbered sliders makes you
    do the translation from a shape you can hear to a number you cannot.
*/
class EqDisplay : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    EqDisplay();

    enum class Band { low, peak, high };

    struct BandState
    {
        float frequency = 1000.0f;
        float gainDb = 0.0f;
    };

    void setBand (Band, BandState);

    /** The two cut filters, which are lines rather than points: a first order slope
        has a corner frequency and nothing else to move, so a handle with a gain would
        be offering a control that does not exist. */
    enum class Cut { high, low };

    void setCut (Cut, float frequency);
    float cutFrequency (Cut) const noexcept;

    /** As for the bands: the range its parameter will actually hold. */
    void setCutRange (Cut, float lowestHz, float highestHz);

    std::function<void (Cut, float frequency)> onCutDragged;
    std::function<void (Cut, bool starting)> onCutGesture;

    /** Where a band is currently drawn. Public because it is not always the same as
        the parameter -- during a drag the display leads it -- so "what is on screen"
        is a question worth being able to ask. */
    const BandState& stateFor (Band) const noexcept;

    /** The frequencies a band is allowed to hold, which are its parameter's range.

        The display has to know these. Its own axis runs 20 Hz to 20 kHz, but no band
        covers all of it -- so dragging the low band past a kilohertz used to move the
        handle to the pointer while the parameter clamped, and the editor's poll put it
        straight back. The handle flickered between the two several times a second. */
    void setBandRange (Band, float lowestHz, float highestHz);

    /** The plugin's output spectrum, as linear magnitudes per FFT bin. Empty clears
        it, which is what a stopped transport eventually amounts to. */
    void setSpectrum (const std::vector<float>& magnitudes);

    /** The signal as it arrived, drawn as a ghost behind the processed one -- so what
        the EQ is doing is visible as the difference between two traces rather than
        having to be remembered from before the control was moved. */
    void setDrySpectrum (const std::vector<float>& magnitudes);

    /** How solidly the spectrum is drawn, 1 down to 0. The editor winds this down
        when the analyser stops producing frames, so a stopped transport dissolves the
        trace instead of leaving the last one frozen there looking live. */
    void setSpectrumFade (float) noexcept;

    void setSampleRate (double rate) noexcept { sampleRate = rate > 0.0 ? rate : sampleRate; }
    void setFftSize (int size) noexcept { fftSize = size > 0 ? size : fftSize; }

    /** Supplies the EQ's own magnitude response, asked of the filters the audio is
        running rather than recomputed here -- a display that derives the curve
        separately is one that can confidently draw something the plugin is not doing. */
    std::function<float (float frequency)> magnitudeDbAt;

    /** Called while a band is being dragged. Both values every time, because dragging
        a band on the curve genuinely moves both. */
    std::function<void (Band, float frequency, float gainDb)> onBandDragged;
    std::function<void (Band, bool starting)> onBandGesture;

    void paint (juce::Graphics&) override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    PlotGeometry getPlot() const;

    /** The gain axis. Narrower than the spectrum's, because an EQ curve that shares a
        90 dB axis with a spectrum is a flat line whatever it is doing. */
    static constexpr float gainRangeDb = 18.0f;
    float gainToY (float db, PlotGeometry) const;
    float yToGain (float y, PlotGeometry) const;

    std::optional<Band> handleAt (juce::Point<float>) const;
    juce::Point<float> bandCentre (Band, PlotGeometry) const;

    void drawGrid (juce::Graphics&, PlotGeometry, juce::Rectangle<float> full) const;
    /** One trace. Called twice: the dry signal behind, the processed one in front.
        Both are drawn the same way -- what separates them is colour and weight, so
        that the pair reads as before and after rather than as two measurements. */
    void drawSpectrum (juce::Graphics&, PlotGeometry, const std::vector<float>& magnitudes,
                       juce::Colour, float fillAlpha, float strokeAlpha) const;
    void drawCurve (juce::Graphics&, PlotGeometry) const;

    BandState low { 120.0f, 0.0f }, peak { 1000.0f, 0.0f }, high { 8000.0f, 0.0f };

    struct Limits { float lowest = PlotGeometry::minFreq, highest = PlotGeometry::maxFreq; };
    Limits lowLimits, peakLimits, highLimits;
    Limits highPassLimits { 20.0f, 2000.0f }, lowPassLimits { 1000.0f, 20000.0f };

    const Limits& limitsFor (Band) const noexcept;
    const Limits& limitsFor (Cut) const noexcept;

    float highPassHz = 20.0f, lowPassHz = 20000.0f;

    void drawCuts (juce::Graphics&, PlotGeometry) const;

    /** Which cut, if either, the pointer is close enough to take hold of. */
    std::optional<Cut> cutAt (juce::Point<float>) const;

    std::optional<Cut> draggingCut, hoveredCut;

    static constexpr float cutReach = 7.0f;

    std::vector<float> spectrum, drySpectrum;
    float spectrumFade = 1.0f;

    double sampleRate = 48000.0;
    int fftSize = 4096;

    std::optional<Band> dragging, hovered;

    static constexpr float handleRadius = 7.0f;
    static constexpr float handleReach = 14.0f;

    static constexpr float axisBottom = 20.0f;
    static constexpr float axisTop = 8.0f;
    static constexpr float axisLeft = 30.0f;
    static constexpr float axisRight = 30.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqDisplay)
};
