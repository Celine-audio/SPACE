#include "EqDisplay.h"

#include "Fonts.h"
#include "Theme.h"

#include <cmath>

using namespace Celine;

namespace
{
    constexpr float magnitudeEpsilon = 1.0e-7f;

    float magnitudeToDb (float magnitude) noexcept
    {
        return 20.0f * std::log10 (juce::jmax (magnitude, magnitudeEpsilon));
    }
}

//==============================================================================
EqDisplay::EqDisplay()
{
    // Opaque, painting its own corners -- see WaveformDisplay for why that is worth
    // the two extra lines.
    setOpaque (true);
}

void EqDisplay::setBand (Band band, BandState state)
{
    auto& target = band == Band::low ? low : band == Band::peak ? peak : high;

    if (juce::approximatelyEqual (target.frequency, state.frequency)
        && juce::approximatelyEqual (target.gainDb, state.gainDb))
        return;

    target = state;
    repaint();
}

const EqDisplay::BandState& EqDisplay::stateFor (Band band) const noexcept
{
    return band == Band::low ? low : band == Band::peak ? peak : high;
}

void EqDisplay::setBandRange (Band band, float lowestHz, float highestHz)
{
    auto& target = band == Band::low ? lowLimits : band == Band::peak ? peakLimits : highLimits;

    target.lowest = juce::jmin (lowestHz, highestHz);
    target.highest = juce::jmax (lowestHz, highestHz);
}

const EqDisplay::Limits& EqDisplay::limitsFor (Band band) const noexcept
{
    return band == Band::low ? lowLimits : band == Band::peak ? peakLimits : highLimits;
}

//==============================================================================
void EqDisplay::setCut (Cut cut, float frequency)
{
    auto& target = cut == Cut::high ? highPassHz : lowPassHz;

    if (juce::approximatelyEqual (target, frequency))
        return;

    target = frequency;
    repaint();
}

float EqDisplay::cutFrequency (Cut cut) const noexcept
{
    return cut == Cut::high ? highPassHz : lowPassHz;
}

void EqDisplay::setCutRange (Cut cut, float lowestHz, float highestHz)
{
    auto& target = cut == Cut::high ? highPassLimits : lowPassLimits;

    target.lowest = juce::jmin (lowestHz, highestHz);
    target.highest = juce::jmax (lowestHz, highestHz);
}

const EqDisplay::Limits& EqDisplay::limitsFor (Cut cut) const noexcept
{
    return cut == Cut::high ? highPassLimits : lowPassLimits;
}

void EqDisplay::drawCuts (juce::Graphics& g, PlotGeometry plot) const
{
    for (const auto cut : { Cut::high, Cut::low })
    {
        // Always, including parked at the end of its travel. Hiding an idle one meant
        // the two controls did not exist until you found them, and the only way to
        // find them was to drag a bar that was not drawn.
        const auto x = plot.freqToX (cutFrequency (cut));
        const auto live = draggingCut == cut || (! draggingCut.has_value() && hoveredCut == cut);

        // The side that is being removed, shaded back. Which side that is says which
        // filter it is without either of them needing a label.
        const auto removed = cut == Cut::high
                                 ? juce::Rectangle<float> (plot.getX(), plot.getY(),
                                                           x - plot.getX(), plot.getHeight())
                                 : juce::Rectangle<float> (x, plot.getY(),
                                                           plot.getRight() - x, plot.getHeight());

        g.setColour (Theme::consoleBackground().withAlpha (0.45f));
        g.fillRect (removed);

        g.setColour (live ? Theme::accent() : Theme::accent().withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, plot.getY(), 1.5f, plot.getHeight()));

        // A grip at the top, the same idea as the response display's trim tabs.
        const auto tab = juce::Rectangle<float> (9.0f, 15.0f)
                             .withCentre ({ x, plot.getY() + 7.5f });

        g.setColour (live ? Theme::accent() : Theme::surface());
        g.fillRoundedRectangle (tab, 2.5f);
        g.setColour (live ? Theme::text() : Theme::accent().withAlpha (0.7f));
        g.drawRoundedRectangle (tab.reduced (0.5f), 2.5f, 1.0f);
    }
}

std::optional<EqDisplay::Cut> EqDisplay::cutAt (juce::Point<float> position) const
{
    const auto plot = getPlot();

    if (position.y < plot.getY() || position.y > plot.getBottom())
        return {};

    std::optional<Cut> nearest;
    auto best = cutReach;

    for (const auto cut : { Cut::high, Cut::low })
    {
        // An unengaged cut is still reachable, but only right at the edge it is parked
        // against -- so it can be brought back in without being in the way until then.
        const auto distance = std::abs (position.x - plot.freqToX (cutFrequency (cut)));

        if (distance <= best)
        {
            best = distance;
            nearest = cut;
        }
    }

    return nearest;
}

void EqDisplay::setSpectrum (const std::vector<float>& magnitudes)
{
    // Assigned into the vector already here rather than replaced, so the storage is
    // reused instead of a new one being allocated thirty times a second.
    spectrum = magnitudes;
}

void EqDisplay::setDrySpectrum (const std::vector<float>& magnitudes)
{
    drySpectrum = magnitudes;
}

void EqDisplay::setSpectrumFade (float fade) noexcept
{
    spectrumFade = juce::jlimit (0.0f, 1.0f, fade);
}

//==============================================================================
PlotGeometry EqDisplay::getPlot() const
{
    auto area = getLocalBounds().toFloat().reduced (1.0f);
    area.removeFromTop (axisTop);
    area.removeFromBottom (axisBottom);
    area.removeFromLeft (axisLeft);
    area.removeFromRight (axisRight);
    return { area };
}

float EqDisplay::gainToY (float db, PlotGeometry plot) const
{
    const auto clamped = juce::jlimit (-gainRangeDb, gainRangeDb, db);
    return plot.proportionToY ((clamped + gainRangeDb) / (2.0f * gainRangeDb));
}

float EqDisplay::yToGain (float y, PlotGeometry plot) const
{
    if (plot.getHeight() <= 0.0f)
        return 0.0f;

    const auto proportion = (plot.getBottom() - y) / plot.getHeight();
    return juce::jlimit (-gainRangeDb, gainRangeDb,
                         proportion * 2.0f * gainRangeDb - gainRangeDb);
}

juce::Point<float> EqDisplay::bandCentre (Band band, PlotGeometry plot) const
{
    const auto& state = stateFor (band);
    return { plot.freqToX (state.frequency), gainToY (state.gainDb, plot) };
}

//==============================================================================
void EqDisplay::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    const auto plot = getPlot();

    g.fillAll (Theme::consoleBackground());

    g.setColour (Theme::background());
    g.fillRoundedRectangle (full, Theme::cornerRadius);

    drawGrid (g, plot, full);

    // Dry first and fainter, so the processed trace reads as sitting on top of where
    // the signal started rather than as a second thing of equal standing.
    drawSpectrum (g, plot, drySpectrum, Theme::comment(), 0.0f, 0.30f);
    drawSpectrum (g, plot, spectrum, Theme::accentAlt(), 0.16f, 0.45f);

    drawCurve (g, plot);
    drawCuts (g, plot);

    // The handles last, over everything, since they are what you aim at.
    for (const auto band : { Band::low, Band::peak, Band::high })
    {
        const auto centre = bandCentre (band, plot);
        const auto live = dragging == band || (! dragging.has_value() && hovered == band);
        const auto radius = live ? handleRadius + 1.5f : handleRadius;

        g.setColour (Theme::background());
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

        g.setColour (live ? Theme::accent() : Theme::accent().withAlpha (0.75f));
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 2.0f);
    }
}

void EqDisplay::drawGrid (juce::Graphics& g, PlotGeometry plot, juce::Rectangle<float> full) const
{
    struct FreqLine { float hz; const char* label; };
    static constexpr FreqLine freqLines[] = {
        { 20.0f, "20" },     { 50.0f, "50" },      { 100.0f, "100" },  { 200.0f, "200" },
        { 500.0f, "500" },   { 1000.0f, "1k" },    { 2000.0f, "2k" },  { 5000.0f, "5k" },
        { 10000.0f, "10k" }, { 20000.0f, "20k" },
    };

    // Sub-pixel rather than drawVerticalLine's integer x: the window resizes to
    // arbitrary widths, and truncating each line to an int makes the grid crawl as it
    // moves rather than slide.
    const auto hairline = [&g] (float x, float y, float w, float h)
    {
        g.fillRect (juce::Rectangle<float> (x, y, w, h));
    };

    for (const auto& line : freqLines)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        hairline (plot.freqToX (line.hz), plot.getY(), 1.0f, plot.getHeight());
    }

    // Every 6 dB, with the zero line carrying the weight: it is the only one that
    // says whether the EQ is doing anything at all.
    for (int db = -18; db <= 18; db += 6)
    {
        const auto y = gainToY ((float) db, plot);
        const auto isZero = db == 0;

        g.setColour (juce::Colours::white.withAlpha (isZero ? 0.22f : 0.055f));
        hairline (plot.getX(), y, plot.getWidth(), 1.0f);

        g.setColour (Theme::text().withAlpha (0.7f));
        g.setFont (Fonts::light (9.5f));
        g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                    juce::Rectangle<float> (full.getX() + 2.0f, y - 7.0f, axisLeft - 6.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    g.setFont (Fonts::light (9.5f));
    g.setColour (Theme::textDim().withAlpha (0.6f));

    for (const auto& line : freqLines)
    {
        const auto x = plot.freqToX (line.hz);
        const auto left = juce::jlimit (full.getX(), full.getRight() - 40.0f, x - 20.0f);

        g.drawText (line.label,
                    juce::Rectangle<float> (left, plot.getBottom() + 3.0f, 40.0f, 12.0f),
                    juce::Justification::centred);
    }
}

void EqDisplay::drawSpectrum (juce::Graphics& g, PlotGeometry plot,
                              const std::vector<float>& magnitudes, juce::Colour colour,
                              float fillAlpha, float strokeAlpha) const
{
    if (magnitudes.size() < 2 || spectrumFade <= 0.004f)
        return;

    const auto binHz = (float) (sampleRate / (double) fftSize);

    if (binHz <= 0.0f)
        return;

    // Its own dB window, wider than the EQ's: a spectrum on a +/-18 dB axis is a
    // block against the ceiling. The two scales share the panel, not the axis.
    constexpr float floorDb = -84.0f;
    constexpr float topDb = 0.0f;

    const auto levelToY = [&] (float db)
    {
        const auto clamped = juce::jlimit (floorDb, topDb, db);
        return plot.proportionToY ((clamped - floorDb) / (topDb - floorDb));
    };

    juce::Path path;
    int column = std::numeric_limits<int>::min();
    float columnX = 0.0f, columnY = 0.0f;
    bool started = false;

    const auto emit = [&]
    {
        if (! started) { path.startNewSubPath (columnX, columnY); started = true; }
        else            path.lineTo (columnX, columnY);
    };

    // The ends are read off the axis, not off whichever bin happens to fall nearest
    // it. Starting at the first bin at or above 20 Hz began the trace at 23.4 Hz on a
    // 4096-point FFT at 48k -- a visible gap inside the left edge -- and the same at
    // the top. Interpolating the value at the limit itself puts it on the border
    // where the axis says it should be.
    const auto levelAt = [&] (float frequency)
    {
        const auto position = juce::jlimit (0.0f, (float) (magnitudes.size() - 1), frequency / binHz);
        const auto lower = (size_t) position;
        const auto upper = juce::jmin (lower + 1, magnitudes.size() - 1);
        const auto fraction = position - (float) lower;

        return levelToY (magnitudeToDb (magnitudes[lower]
                                        + fraction * (magnitudes[upper] - magnitudes[lower])));
    };

    // Honest about the top when there is nothing up there: at rates whose Nyquist is
    // under 20 kHz the trace stops where the data does rather than running out flat.
    const auto highest = juce::jmin (PlotGeometry::maxFreq,
                                     (float) (magnitudes.size() - 1) * binHz);

    column = (int) plot.getX();
    columnX = plot.getX();
    columnY = levelAt (PlotGeometry::minFreq);

    // Peak per pixel column: above a couple of kHz there are hundreds of bins to a
    // column, and stroking all of them costs more than everything else here together.
    for (size_t k = 1; k < magnitudes.size(); ++k)
    {
        const auto freq = (float) k * binHz;

        if (freq < PlotGeometry::minFreq) continue;
        if (freq > PlotGeometry::maxFreq) break;

        const auto x = plot.freqToX (freq);
        const auto y = levelToY (magnitudeToDb (magnitudes[k]));
        const auto thisColumn = (int) x;

        if (thisColumn != column)
        {
            if (column != std::numeric_limits<int>::min())
                emit();

            column = thisColumn;
            columnX = x;
            columnY = y;
        }
        else
        {
            columnY = juce::jmin (columnY, y);
        }
    }

    if (column != std::numeric_limits<int>::min())
        emit();

    // And close on the axis limit the same way.
    columnX = plot.freqToX (highest);
    columnY = levelAt (highest);
    emit();

    if (path.isEmpty())
        return;

    if (fillAlpha > 0.0f)
    {
        juce::Path filled (path);
        filled.lineTo (path.getBounds().getRight(), plot.getBottom());
        filled.lineTo (path.getBounds().getX(), plot.getBottom());
        filled.closeSubPath();

        g.setColour (colour.withAlpha (fillAlpha * spectrumFade));
        g.fillPath (filled);
    }

    g.setColour (colour.withAlpha (strokeAlpha * spectrumFade));
    g.strokePath (path, juce::PathStrokeType (1.1f));
}

void EqDisplay::drawCurve (juce::Graphics& g, PlotGeometry plot) const
{
    if (magnitudeDbAt == nullptr || plot.getWidth() <= 1.0f)
        return;

    juce::Path curve;

    for (int x = 0; x <= (int) plot.getWidth(); ++x)
    {
        const auto px = plot.getX() + (float) x;
        const auto y = gainToY (magnitudeDbAt (plot.xToFreq (px)), plot);

        if (x == 0) curve.startNewSubPath (px, y);
        else        curve.lineTo (px, y);
    }

    // Filled back to the zero line, so a boost and a cut are told apart at a glance
    // rather than by reading the axis.
    juce::Path filled (curve);
    filled.lineTo (plot.getRight(), gainToY (0.0f, plot));
    filled.lineTo (plot.getX(), gainToY (0.0f, plot));
    filled.closeSubPath();

    g.setColour (Theme::accent().withAlpha (0.18f));
    g.fillPath (filled);

    g.setColour (Theme::accent());
    g.strokePath (curve, juce::PathStrokeType (2.0f));
}

//==============================================================================
std::optional<EqDisplay::Band> EqDisplay::handleAt (juce::Point<float> position) const
{
    const auto plot = getPlot();

    if (! plot.bounds.expanded (handleReach).contains (position))
        return {};

    std::optional<Band> nearest;
    auto nearestDistance = handleReach;

    for (const auto band : { Band::low, Band::peak, Band::high })
    {
        const auto distance = position.getDistanceFrom (bandCentre (band, plot));

        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearest = band;
        }
    }

    return nearest;
}

void EqDisplay::mouseMove (const juce::MouseEvent& event)
{
    // Bands first, and that order matters: a band handle is a small circle while a cut
    // is a line the height of the plot, so wherever the two overlap the one that is
    // harder to hit has to win or it becomes unreachable.
    hovered = handleAt (event.position);
    hoveredCut = hovered.has_value() ? std::nullopt : cutAt (event.position);

    setMouseCursor (hovered.has_value()  ? juce::MouseCursor::DraggingHandCursor
                    : hoveredCut.has_value() ? juce::MouseCursor::LeftRightResizeCursor
                                             : juce::MouseCursor::NormalCursor);
    repaint();
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    hovered.reset();
    hoveredCut.reset();
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void EqDisplay::mouseDown (const juce::MouseEvent& event)
{
    dragging = handleAt (event.position);
    draggingCut = dragging.has_value() ? std::nullopt : cutAt (event.position);

    if (dragging.has_value() && onBandGesture != nullptr)
        onBandGesture (*dragging, true);

    if (draggingCut.has_value() && onCutGesture != nullptr)
        onCutGesture (*draggingCut, true);

    repaint();
}

void EqDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingCut.has_value())
    {
        const auto& limits = limitsFor (*draggingCut);
        const auto frequency = juce::jlimit (limits.lowest, limits.highest,
                                             getPlot().xToFreq (event.position.x));

        // Moved and reported, both clamped -- the same arrangement the bands use, and
        // for the same two reasons: the bar keeps up with the pointer, and it cannot
        // come to rest anywhere the parameter will not follow it.
        setCut (*draggingCut, frequency);

        if (onCutDragged != nullptr)
            onCutDragged (*draggingCut, frequency);

        return;
    }

    if (! dragging.has_value() || onBandDragged == nullptr)
        return;

    const auto plot = getPlot();
    const auto& limits = limitsFor (*dragging);

    // Clamped to what the band can actually hold, before it is either drawn or
    // reported. Dragged past the end of its range the handle used to follow the
    // pointer while the parameter stopped at its limit, and the editor's poll pulled
    // it back a frame later -- so it sat there flickering between the two.
    const auto frequency = juce::jlimit (limits.lowest, limits.highest,
                                         plot.xToFreq (event.position.x));
    const auto gainDb = yToGain (event.position.y, plot);

    // Moved here, not just reported. The band's drawn position used to come back
    // round through the parameter and the editor's 30 Hz timer, so the handle trailed
    // the pointer by up to a frame and felt like it was being dragged through syrup.
    // The parameter is still what everything else reads; this is the display keeping
    // up with the hand that is moving it.
    setBand (*dragging, { frequency, gainDb });

    onBandDragged (*dragging, frequency, gainDb);
}

void EqDisplay::mouseUp (const juce::MouseEvent& event)
{
    if (draggingCut.has_value() && onCutGesture != nullptr)
        onCutGesture (*draggingCut, false);

    draggingCut.reset();
    hoveredCut = cutAt (event.position);

    if (dragging.has_value() && onBandGesture != nullptr)
        onBandGesture (*dragging, false);

    dragging.reset();
    hovered = handleAt (event.position);
    repaint();
}
