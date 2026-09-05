#include "WaveformDisplay.h"

#include "Fonts.h"
#include "Theme.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

using namespace Celine;

namespace
{
    /** A peak drawn on a linear scale is a spike then nothing: a reverb tail spends
        most of its life 40 dB down, which is a pixel and a half. Compressed like this
        the decay is a shape rather than a cliff, and the trim handles have something
        to be positioned against. */
    float peakToHeight (float peak) noexcept
    {
        constexpr float floorDb = -60.0f;

        const auto db = juce::Decibels::gainToDecibels (peak, floorDb);
        return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
    }

    juce::String formatSeconds (double seconds)
    {
        if (seconds < 1.0)
            return juce::String (juce::roundToInt (seconds * 1000.0)) + " ms";

        return juce::String (seconds, 2) + " s";
    }
}

//==============================================================================
WaveformDisplay::WaveformDisplay()
{
    // Opaque, and paint() fills its own corners. A non-opaque child makes JUCE redraw
    // the whole editor underneath it on every repaint, which for a display that
    // updates while a control is dragged is a cost paid over and over.
    setOpaque (true);
}

void WaveformDisplay::setWaveform (std::vector<float> newPeaks)
{
    peaks = std::move (newPeaks);
    repaint();
}

void WaveformDisplay::setRegion (float start, float end)
{
    if (juce::approximatelyEqual (regionStart, start) && juce::approximatelyEqual (regionEnd, end))
        return;

    regionStart = start;
    regionEnd = end;
    repaint();
}

void WaveformDisplay::setFades (float newFadeIn, float newFadeOut)
{
    if (juce::approximatelyEqual (fadeInSeconds, newFadeIn)
        && juce::approximatelyEqual (fadeOutSeconds, newFadeOut))
        return;

    fadeInSeconds = newFadeIn;
    fadeOutSeconds = newFadeOut;
    repaint();
}

void WaveformDisplay::setZoomed (bool shouldZoom)
{
    if (zoomed == shouldZoom)
        return;

    zoomed = shouldZoom;
    repaint();
}

void WaveformDisplay::setFileName (const juce::String& name)
{
    if (fileName == name)
        return;

    fileName = name;
    repaint();
}

void WaveformDisplay::setLengthSeconds (double seconds)
{
    if (juce::approximatelyEqual (lengthSeconds, seconds))
        return;

    lengthSeconds = seconds;
    repaint();
}

//==============================================================================
juce::Rectangle<float> WaveformDisplay::getPlotArea() const
{
    auto area = getLocalBounds().toFloat().reduced (1.0f);
    area.removeFromTop (axisTop);
    area.removeFromBottom (axisBottom);
    area.removeFromLeft (axisSide);
    area.removeFromRight (axisSide);
    return area;
}

float WaveformDisplay::positionToX (float proportion, juce::Rectangle<float> area) const
{
    const auto span = getViewEnd() - getViewStart();

    if (span <= 0.0f)
        return area.getX();

    return area.getX() + (proportion - getViewStart()) / span * area.getWidth();
}

float WaveformDisplay::xToPosition (float x, juce::Rectangle<float> area) const
{
    if (area.getWidth() <= 0.0f)
        return 0.0f;

    const auto span = getViewEnd() - getViewStart();
    const auto proportion = (x - area.getX()) / area.getWidth();

    return juce::jlimit (0.0f, 1.0f, getViewStart() + proportion * span);
}

WaveformDisplay::FadeSpan WaveformDisplay::fadeSpans() const
{
    if (lengthSeconds <= 0.0)
        return { 0.0f, 0.0f };

    const auto region = juce::jmax (0.0f, regionEnd - regionStart);
    const auto half = 0.5f * region;

    // The same limit the shaping applies, for the same reason: two fades that between
    // them span more than the region would scale it to nothing.
    const auto toProportion = [this] (float seconds)
    {
        return juce::jmax (0.0f, seconds) / (float) lengthSeconds;
    };

    return { juce::jlimit (0.0f, half, toProportion (fadeInSeconds)),
             juce::jlimit (0.0f, half, toProportion (fadeOutSeconds)) };
}

float WaveformDisplay::envelopeAt (float position, FadeSpan spans) const
{
    // The same raised cosine the shaping applies -- see ImpulseResponse::shape. Drawn
    // any other way the picture would be of a fade the plugin is not running.
    const auto raisedCosine = [] (float x)
    {
        return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi
                                       * juce::jlimit (0.0f, 1.0f, x));
    };

    auto gain = 1.0f;

    if (spans.in > 0.0f)
        gain *= raisedCosine ((position - regionStart) / spans.in);

    if (spans.out > 0.0f)
        gain *= raisedCosine ((regionEnd - position) / spans.out);

    return gain;
}

float WaveformDisplay::gripPosition (Fade which, FadeSpan spans) const
{
    return which == Fade::in ? regionStart + spans.in : regionEnd - spans.out;
}

//==============================================================================
void WaveformDisplay::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    const auto plot = getPlotArea();

    // The surround first, so the rounded corners have something behind them and this
    // component can still be opaque.
    g.fillAll (Theme::consoleBackground());

    g.setColour (Theme::background());
    g.fillRoundedRectangle (full, Theme::cornerRadius);

    if (fileHovering)
    {
        g.setColour (Theme::accent().withAlpha (0.15f));
        g.fillRoundedRectangle (full, Theme::cornerRadius);
    }

    drawTimeAxis (g, plot);

    const auto centre = plot.getCentreY();
    const auto halfHeight = plot.getHeight() * 0.5f;

    if (peaks.empty())
    {
        g.setColour (Theme::comment());
        g.setFont (Fonts::light (13.0f));
        g.drawText ("Drop an impulse response here, or press LOAD",
                    plot, juce::Justification::centred);
        return;
    }

    // The excluded head and tail, dimmed back rather than hidden: what you cut is
    // still worth seeing, or the handles have nothing to be dragged across.
    const auto startX = positionToX (regionStart, plot);
    const auto endX = positionToX (regionEnd, plot);

    const auto columns = (int) peaks.size();
    const auto columnWidth = plot.getWidth() / (float) columns;

    const auto spans = fadeSpans();

    // What has been trimmed away is a wash, not a waveform. It used to be drawn as
    // one -- dimmed, but at its true height and with its own outline -- and beside a
    // faded region that reads as broken: a fade in takes the kept material down to
    // nothing at the very point where the discarded material stands at full height,
    // so the part that is not being used towered over the part that is. Two waveforms
    // in one panel, and the louder one was the one doing nothing.
    //
    // A flat band says the same thing the shape did -- here is what you cut, here is
    // how much of it -- without competing with the response for the eye.
    const auto wash = [&] (float fromX, float toX)
    {
        const auto clipped = juce::Rectangle<float> (juce::jmax (plot.getX(), fromX), plot.getY(),
                                                     0.0f, plot.getHeight())
                                 .withRight (juce::jmin (plot.getRight(), toX));

        if (clipped.getWidth() <= 0.0f)
            return;

        g.setColour (Theme::comment().withAlpha (0.10f));
        g.fillRect (clipped);
    };

    wash (plot.getX(), startX);
    wash (endX, plot.getRight());

    // The response itself: body then edge, rather than one solid block. A dense
    // response fills every column to its envelope, so drawn at full strength the whole
    // thing is one flat shape with no decay visible inside it -- the outline is what
    // carries the shape and the fill is what gives it weight.
    {
        juce::Path envelope;
        int previousColumn = -2;

        for (int column = 0; column < columns; ++column)
        {
            const auto x = plot.getX() + (float) column * columnWidth;

            if (x < startX - columnWidth || x > endX)
                continue;

            // The fades are applied to the peak, not to the drawn height: the height
            // is already a dB mapping, and halving it is not the same as halving the
            // sample it came from. Applied here, the shape on screen is the response
            // the convolution is actually running.
            const auto peak = peaks[(size_t) column] * envelopeAt (xToPosition (x, plot), spans);
            const auto height = juce::jmax (0.75f, peakToHeight (peak) * halfHeight);

            g.setColour (Theme::accent().withAlpha (0.38f));
            g.fillRect (juce::Rectangle<float> (x, centre - height,
                                                juce::jmax (1.0f, columnWidth), height * 2.0f));

            // Only the upper half is stroked: the waveform is symmetrical, so the
            // lower edge says nothing the upper one has not already said.
            if (column != previousColumn + 1)
                envelope.startNewSubPath (x, centre - height);
            else
                envelope.lineTo (x, centre - height);

            previousColumn = column;
        }

        if (! envelope.isEmpty())
        {
            g.setColour (Theme::accent().withAlpha (0.95f));
            g.strokePath (envelope, juce::PathStrokeType (1.2f));
        }
    }

    // The centre line last, so it reads as an axis through the waveform.
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), centre, plot.getWidth(), 1.0f));

    // No curve drawn over the ramps. The response is already carrying the envelope --
    // that is what makes a fade visible -- and a second line tracing the same shape
    // over the top of it only competes with the waveform it is describing.

    // The two grips.
    const auto drawEdge = [&] (float position, Edge which)
    {
        const auto x = positionToX (position, plot);

        if (x < plot.getX() - 2.0f || x > plot.getRight() + 2.0f)
            return;

        const auto grip = which == Edge::start ? Grip::start : Grip::end;
        const auto live = dragging == grip || (! dragging.has_value() && hovered == grip);

        g.setColour (live ? Theme::accent() : Theme::line().withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (x, plot.getY(), 1.0f, plot.getHeight()));

        // Something to aim at, and the only thing that says the line moves.
        const auto tab = juce::Rectangle<float> (9.0f, 16.0f)
                             .withCentre ({ x, plot.getY() + 8.0f });

        g.setColour (live ? Theme::accent() : Theme::surface());
        g.fillRoundedRectangle (tab, 2.5f);
        g.setColour (live ? Theme::text() : Theme::line().withAlpha (0.75f));
        g.drawRoundedRectangle (tab.reduced (0.5f), 2.5f, 1.0f);
    };

    drawEdge (regionStart, Edge::start);
    drawEdge (regionEnd, Edge::end);

    // The fade grips, on the bottom edge below the trim tabs on the top one. Each is
    // tethered to the trim line it belongs to by the stretch of edge it covers, so it
    // reads as that line's fade rather than as a fifth thing floating in the panel.
    const auto drawFade = [&] (Fade which)
    {
        const auto grip = which == Fade::in ? Grip::fadeIn : Grip::fadeOut;
        const auto anchorX = positionToX (which == Fade::in ? regionStart : regionEnd, plot);
        const auto x = positionToX (gripPosition (which, spans), plot);

        if (x < plot.getX() - 2.0f || x > plot.getRight() + 2.0f)
            return;

        const auto live = dragging == grip || (! dragging.has_value() && hovered == grip);
        const auto y = plot.getBottom() - fadeGripRadius - 2.0f;

        // A bar back to the edge the fade belongs to, so at a glance you can see how
        // much of the response each one covers -- and so a fade of zero, whose grip
        // sits exactly on its own trim line, still says which line that is.
        g.setColour (Theme::accent().withAlpha (live ? 0.7f : 0.3f));
        g.fillRect (juce::Rectangle<float> (juce::jmin (anchorX, x), y - 0.75f,
                                            juce::jmax (1.5f, std::abs (x - anchorX)), 1.5f));

        const auto radius = live ? fadeGripRadius + 1.0f : fadeGripRadius;
        const auto bounds = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                                .withCentre ({ x, y });

        g.setColour (Theme::background());
        g.fillEllipse (bounds);
        g.setColour (live ? Theme::accent() : Theme::accent().withAlpha (0.7f));
        g.drawEllipse (bounds, 2.0f);
    };

    drawFade (Fade::in);
    drawFade (Fade::out);
}

void WaveformDisplay::drawTimeAxis (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    if (lengthSeconds <= 0.0)
        return;

    const auto viewStart = getViewStart() * lengthSeconds;
    const auto viewEnd = getViewEnd() * lengthSeconds;
    const auto span = viewEnd - viewStart;

    if (span <= 0.0)
        return;

    // A division count rather than a fixed interval: zoomed in on 60 ms and out on
    // four seconds are the same picture at different scales, and a fixed interval is
    // either one line or four hundred.
    constexpr int divisions = 8;

    g.setFont (Fonts::light (9.5f));

    for (int i = 0; i <= divisions; ++i)
    {
        const auto proportion = (float) i / (float) divisions;
        const auto x = plot.getX() + proportion * plot.getWidth();

        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.fillRect (juce::Rectangle<float> (x, plot.getY(), 1.0f, plot.getHeight()));

        if (i % 2 != 0 && divisions > 4)
            continue;

        g.setColour (Theme::textDim().withAlpha (0.55f));
        g.drawText (formatSeconds (viewStart + span * (double) proportion),
                    juce::Rectangle<float> (x - 30.0f, plot.getBottom() + 3.0f, 60.0f, 12.0f),
                    juce::Justification::centred);
    }

}

//==============================================================================
std::optional<WaveformDisplay::Grip> WaveformDisplay::handleAt (juce::Point<float> position) const
{
    const auto plot = getPlotArea();

    if (peaks.empty() || ! plot.expanded (handleReach, 0.0f).contains (position))
        return {};

    const auto nearest = [&position] (float x) { return std::abs (position.x - x); };

    // The bottom band belongs to the fades. Splitting by height rather than by
    // distance is what makes a fade of zero reachable at all: its grip sits exactly on
    // its own trim line, and on distance alone the two would be the same target.
    if (position.y >= plot.getBottom() - fadeBand)
    {
        const auto spans = fadeSpans();
        const auto inDistance = nearest (positionToX (gripPosition (Fade::in, spans), plot));
        const auto outDistance = nearest (positionToX (gripPosition (Fade::out, spans), plot));

        if (juce::jmin (inDistance, outDistance) <= handleReach)
            return inDistance <= outDistance ? Grip::fadeIn : Grip::fadeOut;
    }

    const auto startDistance = nearest (positionToX (regionStart, plot));
    const auto endDistance = nearest (positionToX (regionEnd, plot));

    if (juce::jmin (startDistance, endDistance) > handleReach)
        return {};

    // The nearer one, so the two are still separable once they are dragged together.
    return startDistance <= endDistance ? Grip::start : Grip::end;
}

void WaveformDisplay::mouseMove (const juce::MouseEvent& event)
{
    hovered = handleAt (event.position);

    setMouseCursor (hovered.has_value() ? juce::MouseCursor::LeftRightResizeCursor
                                        : juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveformDisplay::mouseExit (const juce::MouseEvent&)
{
    hovered.reset();
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveformDisplay::gesture (Grip grip, bool starting)
{
    switch (grip)
    {
        case Grip::start:
        case Grip::end:
            if (onEdgeGesture != nullptr)
                onEdgeGesture (grip == Grip::start ? Edge::start : Edge::end, starting);
            break;

        case Grip::fadeIn:
        case Grip::fadeOut:
            if (onFadeGesture != nullptr)
                onFadeGesture (grip == Grip::fadeIn ? Fade::in : Fade::out, starting);
            break;
    }
}

void WaveformDisplay::mouseDown (const juce::MouseEvent& event)
{
    dragging = handleAt (event.position);

    if (dragging.has_value())
        gesture (*dragging, true);

    repaint();
}

void WaveformDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragging.has_value())
        return;

    const auto position = xToPosition (event.position.x, getPlotArea());

    switch (*dragging)
    {
        case Grip::start:
        case Grip::end:
        {
            // The two may not cross: an inverted region would mean no response at
            // all, which looks like the plugin has stopped working. Only the edge
            // being dragged is reported -- the other is read here purely as the limit
            // on this one.
            const auto clamped = *dragging == Grip::start ? juce::jmin (position, regionEnd)
                                                          : juce::jmax (position, regionStart);

            if (onEdgeDragged != nullptr)
                onEdgeDragged (*dragging == Grip::start ? Edge::start : Edge::end, clamped);

            break;
        }

        case Grip::fadeIn:
        case Grip::fadeOut:
        {
            // A fade is the distance from its own edge inwards, and it stops at the
            // middle of the region -- past that the two fades would be fighting over
            // the same samples, which is where the shaping clamps them anyway.
            const auto isIn = *dragging == Grip::fadeIn;
            const auto span = isIn ? position - regionStart : regionEnd - position;
            const auto half = 0.5f * juce::jmax (0.0f, regionEnd - regionStart);

            if (onFadeDragged != nullptr)
                onFadeDragged (isIn ? Fade::in : Fade::out,
                               juce::jlimit (0.0f, half, span) * (float) lengthSeconds);

            break;
        }
    }

    repaint();
}

void WaveformDisplay::mouseUp (const juce::MouseEvent& event)
{
    if (dragging.has_value())
        gesture (*dragging, false);

    dragging.reset();
    hovered = handleAt (event.position);
    repaint();
}

//==============================================================================
bool WaveformDisplay::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (juce::File (path).hasFileExtension ("wav;aiff;aif;flac;ogg;mp3"))
            return true;

    return false;
}

void WaveformDisplay::filesDropped (const juce::StringArray& files, int, int)
{
    fileHovering = false;
    repaint();

    for (const auto& path : files)
    {
        const juce::File file (path);

        if (file.hasFileExtension ("wav;aiff;aif;flac;ogg;mp3"))
        {
            if (onFileDropped != nullptr)
                onFileDropped (file);

            return;
        }
    }
}

void WaveformDisplay::fileDragEnter (const juce::StringArray&, int, int)
{
    fileHovering = true;
    repaint();
}

void WaveformDisplay::fileDragExit (const juce::StringArray&)
{
    fileHovering = false;
    repaint();
}
