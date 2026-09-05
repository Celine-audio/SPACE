#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

/**
    The impulse response, drawn, with its start and end as things you can take hold of.

    The waveform is summarised to one peak per pixel column rather than drawn sample
    by sample: at any useful width there are hundreds of samples per column, and
    picking one of them makes a shape that shimmers when the window resizes and hides
    every transient in between.

    Zoom exists for one reason. The first few milliseconds of a response decide whether
    a reverb sounds tight or smeared, and at full width they are a sliver two pixels
    across -- so the control that matters most is the one you cannot reach.
*/
class WaveformDisplay : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        public juce::SettableTooltipClient
{
public:
    WaveformDisplay();

    /** Which edge of the used region a drag is moving. */
    enum class Edge { start, end };

    /** Which fade a drag is moving. Each one belongs to the edge it starts from --
        the in fade to Start, the out fade to End -- which is why they are grips on
        those same two lines rather than controls somewhere else. */
    enum class Fade { in, out };

    static constexpr float axisBottom = 22.0f;
    static constexpr float axisTop = 10.0f;

    // Room for the grips to sit inside the panel rather than against its edge. At 10
    // the start handle overlapped the rounded corner and read as clipped.
    static constexpr float axisSide = 26.0f;

    /** Hands over the peaks to draw, already summarised for the window in view. */
    void setWaveform (std::vector<float> peaks);

    /** The used region, as proportions of the whole file. */
    void setRegion (float start, float end);

    /** The two fades, in seconds *of the source file* -- the axis this display draws
        on. The parameters are milliseconds of the output, which Size stretches; the
        editor does that conversion so the display only ever deals in one unit. */
    void setFades (float fadeInSeconds, float fadeOutSeconds);

    /** True while the view is zoomed to the head of the response. The display does not
        own this -- the button does -- so that the button can light up. */
    void setZoomed (bool);
    bool isZoomed() const noexcept { return zoomed; }

    /** The proportion of the file the zoomed view shows. */
    static constexpr float zoomedSpan = 0.06f;

    /** What the display is showing, so whoever supplies the peaks knows what window
        to summarise. */
    float getViewStart() const noexcept { return 0.0f; }
    float getViewEnd() const noexcept { return zoomed ? zoomedSpan : 1.0f; }

    /** The width of the drawing area, which is what a summary should be sized to:
        the component is wider by the axis insets on both sides. */
    int getPlotWidth() const noexcept { return juce::jmax (1, getWidth() - (int) (2.0f * axisSide) - 2); }

    void setFileName (const juce::String&);
    void setLengthSeconds (double);

    /** Called while an edge is being dragged, naming the edge and the proportion of
        the file it has been dragged to. One edge, not both: reporting the pair means
        the editor writes and opens a host gesture on a parameter that has not moved. */
    std::function<void (Edge, float)> onEdgeDragged;

    /** Bracketing a drag, so the host records one gesture rather than a stream of
        unrelated writes. Names the same edge onEdgeDragged will. */
    std::function<void (Edge, bool starting)> onEdgeGesture;

    /** The fade equivalents of the two above, in seconds of the source file. */
    std::function<void (Fade, float seconds)> onFadeDragged;
    std::function<void (Fade, bool starting)> onFadeGesture;

    /** A file dropped on the display, which is the fastest way to load one. */
    std::function<void (const juce::File&)> onFileDropped;

    void paint (juce::Graphics&) override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;

private:
    juce::Rectangle<float> getPlotArea() const;

    /** Where a proportion of the *file* lands on screen, accounting for the zoom. */
    float positionToX (float proportion, juce::Rectangle<float> area) const;
    float xToPosition (float x, juce::Rectangle<float> area) const;

    /** The four things that can be taken hold of. One enum rather than two optionals,
        so "which grip" is a single question with a single answer. */
    enum class Grip { start, end, fadeIn, fadeOut };

    /** Which grip, if any, the pointer is close enough to take hold of. Empty rather
        than a "none" member, so the absence cannot be confused with a grip. */
    std::optional<Grip> handleAt (juce::Point<float>) const;

    void drawTimeAxis (juce::Graphics&, juce::Rectangle<float> plot) const;

    /** Where the fades reach full and start to let go, as proportions of the file.
        Clamped exactly as the audio clamps them -- neither fade may eat the other --
        so what is drawn is what is being run. */
    struct FadeSpan { float in, out; };
    FadeSpan fadeSpans() const;

    /** The gain the fades apply at a position through the file, 0 to 1. Squared, to
        match the shaping: a linear fade on a reverb tail is heard as the tail being
        switched off. */
    float envelopeAt (float position, FadeSpan) const;

    /** Where a fade's grip sits, as a proportion of the file. */
    float gripPosition (Fade, FadeSpan) const;

    /** Opens or closes the host gesture belonging to whichever grip is being held. */
    void gesture (Grip, bool starting);

    std::vector<float> peaks;

    float regionStart = 0.0f;
    float regionEnd = 1.0f;
    float fadeInSeconds = 0.0f;
    float fadeOutSeconds = 0.0f;
    bool zoomed = false;

    juce::String fileName;
    double lengthSeconds = 0.0;

    std::optional<Grip> dragging, hovered;
    bool fileHovering = false;

    /** How close the pointer has to get, in pixels. Generous: the line itself is one
        pixel and nobody can hit that. */
    static constexpr float handleReach = 8.0f;

    /** The band along the bottom of the plot that belongs to the fade grips. Above it
        the trim lines take the pointer, which is what lets a fade of zero -- whose
        grip sits exactly on its trim line -- still be picked up. */
    static constexpr float fadeBand = 22.0f;
    static constexpr float fadeGripRadius = 5.0f;



    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};
