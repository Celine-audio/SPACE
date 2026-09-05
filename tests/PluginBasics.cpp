#include "helpers/test_helpers.h"

#include <Parameters.h>
#include <PluginEditor.h>
#include <PluginProcessor.h>
#include <dsp/ImpulseResponse.h>
#include <ui/AboutPanel.h>
#include <ui/Theme.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    void fillWithNoise (juce::AudioBuffer<float>& buffer, int seed = 1)
    {
        juce::Random random { seed };

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);
    }

    void setParameter (PluginProcessor& plugin, const char* id, float value)
    {
        auto* parameter = plugin.getAPVTS().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    /** Runs blocks through a freshly prepared plugin and returns the last one's peak.
        Fresh every time on purpose: a convolution carries a tail, so measuring one
        setting after another on the same instance measures the previous setting too. */
    float peakAfter (const std::function<void (PluginProcessor&)>& configure, int blocks = 12)
    {
        PluginProcessor plugin;
        plugin.prepareToPlay (sampleRate, blockSize);
        configure (plugin);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int block = 0; block < blocks; ++block)
        {
            fillWithNoise (buffer, 7);
            plugin.processBlock (buffer, midi);
        }

        return buffer.getMagnitude (0, blockSize);
    }

    /** A synthetic response written to a temporary file, so the loader can be tested
        against a real file rather than against itself. */
    juce::File writeTestIr (const juce::TemporaryFile& temp, int numSamples,
                            int channels = 2, double rate = sampleRate)
    {
        juce::AudioBuffer<float> buffer (channels, numSamples);
        juce::Random random { 3 };

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, (random.nextFloat() * 2.0f - 1.0f)
                                             * std::exp (-4.0f * (float) i / (float) numSamples));

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream =
            std::make_unique<juce::FileOutputStream> (temp.getFile());

        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                       .withSampleRate (rate)
                                                       .withNumChannels (channels)
                                                       .withBitsPerSample (24));

        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
        writer.reset();

        return temp.getFile();
    }
}

//==============================================================================
TEST_CASE ("The plugin reports itself sensibly", "[instance]")
{
    PluginProcessor plugin;

    CHECK (plugin.getName().isNotEmpty());
    CHECK (plugin.hasEditor());
    CHECK_FALSE (plugin.isMidiEffect());
}

TEST_CASE ("Every declared parameter exists in the tree", "[parameters]")
{
    PluginProcessor plugin;

    // A parameter can be named in ParamID and forgotten in createLayout, and nothing
    // complains until getRawParameterValue returns null and the plugin crashes in a
    // host.
    for (const auto* id : { ParamID::bypass, ParamID::mix, ParamID::width, ParamID::outputGain,
                            ParamID::preDelay, ParamID::irStart, ParamID::irEnd,
                            ParamID::fadeIn, ParamID::fadeOut, ParamID::size,
                            ParamID::lowFreq, ParamID::lowGain, ParamID::peakFreq,
                            ParamID::peakGain, ParamID::peakQ, ParamID::highFreq, ParamID::highGain })
    {
        INFO ("parameter: " << id);
        CHECK (plugin.getAPVTS().getParameter (id) != nullptr);
        CHECK (plugin.getAPVTS().getRawParameterValue (id) != nullptr);
    }
}

TEST_CASE ("The tail length is reported to the host", "[instance]")
{
    // What a host uses to decide how long to keep rendering after the transport
    // stops. Reported as zero, a bounce cuts the reverb off mid-decay.
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    CHECK (plugin.getTailLengthSeconds() > 0.5);
}

//==============================================================================
TEST_CASE ("Processing leaves the signal finite", "[audio]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < 8; ++block)
    {
        fillWithNoise (buffer, block);
        plugin.processBlock (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE (std::isfinite (buffer.getSample (ch, i)));
    }
}

TEST_CASE ("Silence in, silence out", "[audio]")
{
    // A convolution fed silence must decay to silence. Anything that rings, feeds
    // back or reads uninitialised memory shows up here rather than as a burst of
    // noise in somebody's session.
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    buffer.clear();

    for (int block = 0; block < 16; ++block)
        plugin.processBlock (buffer, midi);

    CHECK (buffer.getMagnitude (0, blockSize) < 1.0e-6f);
}

TEST_CASE ("Mix at zero is the dry signal, untouched", "[audio]")
{
    // The one thing a reverb must get exactly right: fully dry has to be bit-for-bit
    // the input, or the plugin colours everything it is inserted on whether it is
    // being used or not.
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);
    setParameter (plugin, ParamID::mix, 0.0f);

    juce::AudioBuffer<float> buffer (2, blockSize), original (2, blockSize);
    juce::MidiBuffer midi;

    fillWithNoise (buffer, 5);
    original.makeCopyOf (buffer);

    for (int block = 0; block < 4; ++block)
        plugin.processBlock (buffer, midi);

    fillWithNoise (buffer, 5);
    plugin.processBlock (buffer, midi);

    for (int i = 0; i < blockSize; ++i)
        REQUIRE_THAT (buffer.getSample (0, i),
                      Catch::Matchers::WithinAbs (original.getSample (0, i), 1.0e-5f));
}

TEST_CASE ("Bypass passes the signal through", "[audio]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);
    setParameter (plugin, ParamID::bypass, 1.0f);
    setParameter (plugin, ParamID::outputGain, 6.0f);

    juce::AudioBuffer<float> buffer (2, blockSize), original (2, blockSize);
    juce::MidiBuffer midi;

    fillWithNoise (buffer, 9);
    original.makeCopyOf (buffer);

    plugin.processBlock (buffer, midi);

    // The trim goes with the reverb, so A/B-ing compares like with like rather than
    // comparing the effect against a dry signal that has been quietly boosted. The
    // gain ramps, so the end of the block is where it has settled.
    for (int i = blockSize - 64; i < blockSize; ++i)
        REQUIRE_THAT (buffer.getSample (0, i),
                      Catch::Matchers::WithinAbs (original.getSample (0, i), 1.0e-4f));
}

TEST_CASE ("More mix means more reverb", "[audio]")
{
    // Each measured on its own instance: a convolution carries a tail, so measuring
    // one setting after another on the same plugin measures the previous one too.
    const auto dry = peakAfter ([] (PluginProcessor& p) { setParameter (p, ParamID::mix, 0.0f); });
    const auto wet = peakAfter ([] (PluginProcessor& p) { setParameter (p, ParamID::mix, 1.0f); });

    CHECK (dry > 0.0f);
    CHECK (wet > 0.0f);
    CHECK (std::abs (wet - dry) > 0.01f);
}

TEST_CASE ("Width at zero makes the wet signal mono", "[audio]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);
    setParameter (plugin, ParamID::mix, 1.0f);
    setParameter (plugin, ParamID::width, 0.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    // Long enough for the width ramp to finish *and* for the post-EQ to forget it.
    // Width is smoothed, so the two channels differ while it is moving; the EQ that
    // follows keeps per-channel filter state, so its memory of that outlives the ramp
    // by a few blocks. Twelve blocks left a residual of about 1e-5.
    for (int block = 0; block < 48; ++block)
    {
        fillWithNoise (buffer, block);
        plugin.processBlock (buffer, midi);
    }

    // Fully wet and fully narrow, the two channels are the same signal. Measured
    // against the level rather than as an absolute: once Width has ramped, the mid/
    // side matrix hands the EQ two bit-identical channels, but the EQ keeps separate
    // state per channel and the ramp drove those apart. Two biquads fed identical
    // input converge, and then sit on a floor of their own rounding noise -- about
    // 1e-5 of the signal, 100 dB down, and it does not decay any further.
    //
    // A hundredth of a percent is still far stricter than a width control that only
    // narrows the difference rather than closing it: at Width 1 this ratio is 1.0.
    float difference = 0.0f, level = 0.0f;

    for (int i = 0; i < blockSize; ++i)
    {
        difference = juce::jmax (difference, std::abs (buffer.getSample (0, i)
                                                       - buffer.getSample (1, i)));
        level = juce::jmax (level, std::abs (buffer.getSample (0, i)));
    }

    REQUIRE (level > 0.01f);
    REQUIRE (difference < 1.0e-4f * level);
}

//==============================================================================
TEST_CASE ("Shaping the response changes its length predictably", "[ir]")
{
    ImpulseResponse impulse;
    impulse.loadDefault (sampleRate);
    REQUIRE_FALSE (impulse.isEmpty());

    const auto full = impulse.shape ({}).getNumSamples();
    REQUIRE (full > 1000);

    SECTION ("Size stretches it")
    {
        ImpulseResponse::Shape big;
        big.size = 2.0f;
        CHECK_THAT ((float) impulse.shape (big).getNumSamples(),
                    Catch::Matchers::WithinRel ((float) full * 2.0f, 0.02f));
    }

    SECTION ("Trimming takes from both ends")
    {
        ImpulseResponse::Shape trimmed;
        trimmed.start = 0.25f;
        trimmed.end = 0.75f;
        CHECK_THAT ((float) impulse.shape (trimmed).getNumSamples(),
                    Catch::Matchers::WithinRel ((float) full * 0.5f, 0.02f));
    }

    SECTION ("Crossed handles give an empty response rather than a negative one")
    {
        // Start and End are separate parameters and nothing stops one being dragged
        // past the other. The audio must never see a negative-length response.
        ImpulseResponse::Shape crossed;
        crossed.start = 0.8f;
        crossed.end = 0.2f;
        CHECK (impulse.shape (crossed).getNumSamples() >= 0);
    }
}

TEST_CASE ("A shaped response ends in silence", "[ir]")
{
    // Cutting a tail short leaves a step at the end of the response, and convolution
    // turns a step into a click on every transient.
    ImpulseResponse impulse;
    impulse.loadDefault (sampleRate);

    ImpulseResponse::Shape shape;
    shape.fadeOutMs = 400.0f;

    const auto shaped = impulse.shape (shape);
    REQUIRE (shaped.getNumSamples() > 100);

    const auto lastSample = std::abs (shaped.getSample (0, shaped.getNumSamples() - 1));
    CHECK (lastSample < 1.0e-4f);
}

TEST_CASE ("Loading refuses what it cannot use", "[ir]")
{
    ImpulseResponse impulse;

    SECTION ("A file that is not there")
    {
        CHECK (impulse.loadFrom (juce::File ("/does/not/exist.wav")).failed());
    }

    SECTION ("A file that is not audio")
    {
        juce::TemporaryFile temp (".wav");
        temp.getFile().replaceWithText ("this is not a wave file");
        CHECK (impulse.loadFrom (temp.getFile()).failed());
    }

    SECTION ("A real file loads, and reports what it holds")
    {
        juce::TemporaryFile temp (".wav");
        const auto file = writeTestIr (temp, (int) sampleRate);

        REQUIRE (impulse.loadFrom (file).wasOk());
        CHECK (impulse.getNumChannels() == 2);
        CHECK_THAT ((float) impulse.getLengthSeconds(), Catch::Matchers::WithinRel (1.0f, 0.01f));
    }
}

TEST_CASE ("Resampling keeps the response the same length in seconds", "[ir]")
{
    // A response is a recording of a room; played at a rate it was not sampled at it
    // is the same room transposed, which people notice without knowing why.
    juce::TemporaryFile temp (".wav");
    ImpulseResponse impulse;

    REQUIRE (impulse.loadFrom (writeTestIr (temp, 44100, 2, 44100.0)).wasOk());
    REQUIRE_THAT ((float) impulse.getLengthSeconds(), Catch::Matchers::WithinRel (1.0f, 0.01f));

    impulse.setPlaybackSampleRate (96000.0);

    CHECK_THAT ((float) impulse.getLengthSeconds(), Catch::Matchers::WithinRel (1.0f, 0.01f));
    CHECK (impulse.getNumSamples() > 90000);
}

TEST_CASE ("The waveform summary follows the window asked for", "[ir]")
{
    ImpulseResponse impulse;
    impulse.loadDefault (sampleRate);

    std::vector<float> whole, head;
    impulse.summarise (whole, 200);
    impulse.summarise (head, 200, 0.0f, 0.06f);

    REQUIRE (whole.size() == 200);
    REQUIRE (head.size() == 200);

    // The head of a decaying response is louder throughout than the whole of it
    // averaged, which is the point of being able to zoom into it.
    const auto average = [] (const std::vector<float>& v)
    {
        return std::accumulate (v.begin(), v.end(), 0.0f) / (float) v.size();
    };

    CHECK (average (head) > average (whole));
}

//==============================================================================
TEST_CASE ("State survives a round trip", "[state]")
{
    PluginProcessor source;
    source.prepareToPlay (sampleRate, blockSize);
    setParameter (source, ParamID::mix, 0.8f);
    setParameter (source, ParamID::preDelay, 40.0f);
    setParameter (source, ParamID::peakGain, -6.0f);

    juce::MemoryBlock state;
    source.getStateInformation (state);

    PluginProcessor restored;
    restored.prepareToPlay (sampleRate, blockSize);
    restored.setStateInformation (state.getData(), (int) state.getSize());

    const auto value = [&restored] (const char* id)
    {
        return restored.getAPVTS().getRawParameterValue (id)->load();
    };

    CHECK_THAT (value (ParamID::mix), Catch::Matchers::WithinAbs (0.8f, 1.0e-3f));
    CHECK_THAT (value (ParamID::preDelay), Catch::Matchers::WithinAbs (40.0f, 0.1f));
    CHECK_THAT (value (ParamID::peakGain), Catch::Matchers::WithinAbs (-6.0f, 0.05f));
}

TEST_CASE ("Foreign state is ignored rather than applied", "[state]")
{
    PluginProcessor plugin;
    setParameter (plugin, ParamID::mix, 0.6f);

    juce::XmlElement foreign ("SomeOtherPlugin");
    juce::MemoryBlock block;
    plugin.copyXmlToBinary (foreign, block);

    plugin.setStateInformation (block.getData(), (int) block.getSize());

    CHECK_THAT (plugin.getAPVTS().getRawParameterValue (ParamID::mix)->load(),
                Catch::Matchers::WithinAbs (0.6f, 1.0e-3f));
}

//==============================================================================
TEST_CASE ("The editor opens at its stored size", "[ui]")
{
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto* editor = plugin.getActiveEditor();
        REQUIRE (editor != nullptr);

        // Laying the window out writes the size back into the state, so anything that
        // reads the stored size after that reads what it just wrote -- which before
        // setSize is zero, and every instance then opens at nothing.
        CHECK (editor->getWidth() >= 760);
        CHECK (editor->getHeight() > 0);
    });
}

TEST_CASE ("The palette is the palette, not a default-constructed Colour", "[ui]")
{
    using namespace Celine;

    for (const auto colour : { Theme::accent(), Theme::accentAlt(), Theme::text(),
                               Theme::background(), Theme::chrome() })
        CHECK (colour != juce::Colour());

    CHECK (Theme::accent() != Theme::accentAlt());
}

TEST_CASE ("The About window builds and names the product", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    AboutPanel panel;
    juce::TextEditor* body = nullptr;

    for (auto* child : panel.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*> (child))
            body = editor;

    REQUIRE (body != nullptr);
    CHECK (body->getText().contains (JucePlugin_Name));
    CHECK (body->getText().contains ("AGPL"));
}

TEST_CASE ("Fully wet comes out at roughly the level that went in", "[audio]")
{
    // The gain of a convolution is the *energy* of its response, not its RMS, and the
    // energy of a reverb grows with its length. Normalising the RMS instead divided
    // that energy by the sample count and left the wet path sqrt(N) too loud -- +23 dB
    // on a 1.8-second response -- which is why the dry signal sounded like it had gone.
    const auto level = [] (float mix)
    {
        PluginProcessor plugin;
        plugin.prepareToPlay (sampleRate, blockSize);
        setParameter (plugin, ParamID::mix, mix);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double sum = 0.0;
        int counted = 0;

        // Well past the response's length, so the tail has fully built up.
        for (int block = 0; block < 220; ++block)
        {
            fillWithNoise (buffer, block);
            plugin.processBlock (buffer, midi);

            if (block > 180)
            {
                for (int i = 0; i < blockSize; ++i)
                    sum += (double) buffer.getSample (0, i) * buffer.getSample (0, i);

                counted += blockSize;
            }
        }

        return (float) std::sqrt (sum / (double) counted);
    };

    const auto dry = level (0.0f);
    const auto wet = level (1.0f);

    REQUIRE (dry > 0.0f);
    INFO ("dry RMS " << dry << ", wet RMS " << wet << ", ratio " << (wet / dry));

    // Within 6 dB either way. Exact equality is not the goal -- a response is not a
    // unity filter -- but a factor of fifteen is a bug, not a character.
    CHECK (wet / dry > 0.5f);
    CHECK (wet / dry < 2.0f);
}

TEST_CASE ("Shaping controls can be swept together without falling over", "[ir]")
{
    // Reported as a crash: adjusting Size while dragging both bounds on the graph.
    // Every combination the interface can produce, including the degenerate ones a
    // drag passes through on its way somewhere sensible.
    ImpulseResponse impulse;
    impulse.loadDefault (sampleRate);

    juce::Random random { 42 };

    for (int i = 0; i < 400; ++i)
    {
        ImpulseResponse::Shape shape;
        shape.start = random.nextFloat();
        shape.end = random.nextFloat();
        shape.size = 0.25f + random.nextFloat() * 3.75f;
        shape.fadeInMs = random.nextFloat() * 1000.0f;
        shape.fadeOutMs = random.nextFloat() * 4000.0f;

        const auto shaped = impulse.shape (shape);

        for (int ch = 0; ch < shaped.getNumChannels(); ++ch)
            for (int n = 0; n < shaped.getNumSamples(); ++n)
                REQUIRE (std::isfinite (shaped.getSample (ch, n)));
    }
}

TEST_CASE ("The fades shape the ends of the response", "[ir]")
{
    ImpulseResponse ir;
    ir.loadDefault (48000.0);

    ImpulseResponse::Shape shape;
    shape.fadeInMs = 0.0f;
    shape.fadeOutMs = 0.0f;

    const auto plain = ir.shape (shape);
    REQUIRE (plain.getNumSamples() > 0);

    // Measured over a window, not at one sample: the source is noise, so a single
    // sample says as much about which random value landed there as about the fade.
    const auto energyOver = [] (const juce::AudioBuffer<float>& buffer, int from, int count)
    {
        double sum = 0.0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int i = from; i < from + count && i < buffer.getNumSamples(); ++i)
                sum += (double) data[i] * (double) data[i];
        }

        return sum;
    };

    SECTION ("A fade in quietens the head and leaves the middle alone")
    {
        shape.fadeInMs = 500.0f;
        const auto faded = ir.shape (shape);

        REQUIRE (faded.getNumSamples() == plain.getNumSamples());

        const auto head = 4800;  // 100 ms, well inside a 500 ms fade
        CHECK (energyOver (faded, 0, head) < 0.05 * energyOver (plain, 0, head));

        // The middle is past the fade and must be untouched, or the control is a
        // volume knob wearing a fade's name.
        const auto middle = plain.getNumSamples() / 2;
        CHECK (energyOver (faded, middle, 4800)
               == Catch::Approx (energyOver (plain, middle, 4800)));
    }

    SECTION ("A longer fade out reaches further back into the tail")
    {
        shape.fadeOutMs = 1000.0f;
        const auto faded = ir.shape (shape);

        REQUIRE (faded.getNumSamples() == plain.getNumSamples());

        // 200 ms from the end: inside a one-second fade, far outside the 64-sample
        // floor the short setting gets.
        const auto from = plain.getNumSamples() - 9600;
        CHECK (energyOver (faded, from, 9600) < 0.2 * energyOver (plain, from, 9600));
    }
}

//==============================================================================
namespace
{
    /** A mouse event at a point in a component, for driving a display headlessly.
        The alternative is asserting on private geometry, which tests the arithmetic
        rather than the thing the pointer actually does. */
    juce::MouseEvent eventAt (juce::Component& component, juce::Point<float> position)
    {
        return { juce::Desktop::getInstance().getMainMouseSource(),
                 position, juce::ModifierKeys::currentModifiers,
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 &component, &component, juce::Time::getCurrentTime(),
                 position, juce::Time::getCurrentTime(), 1, false };
    }
}

TEST_CASE ("The fade grips are reachable and move the right fade", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    WaveformDisplay display;
    display.setSize (600, 300);
    display.setLengthSeconds (2.0);
    display.setWaveform (std::vector<float> (400, 0.5f));
    display.setRegion (0.0f, 1.0f);
    display.setFades (0.2f, 0.4f);

    std::optional<WaveformDisplay::Fade> moved;
    float seconds = 0.0f;
    int gesturesOpened = 0;

    display.onFadeDragged = [&] (WaveformDisplay::Fade fade, float s) { moved = fade; seconds = s; };
    display.onFadeGesture = [&] (WaveformDisplay::Fade, bool starting) { gesturesOpened += starting ? 1 : -1; };

    // The plot, worked out the same way the display does, so the test names positions
    // in the display's own terms rather than in pixels that a layout change invalidates.
    const auto plot = display.getLocalBounds().toFloat().reduced (1.0f)
                          .withTrimmedTop (WaveformDisplay::axisTop)
                          .withTrimmedBottom (WaveformDisplay::axisBottom)
                          .withTrimmedLeft (WaveformDisplay::axisSide)
                          .withTrimmedRight (WaveformDisplay::axisSide);

    const auto atPosition = [&plot] (float proportion) { return plot.getX() + proportion * plot.getWidth(); };

    SECTION ("The in fade's grip is on the bottom edge at the end of its ramp")
    {
        // 0.2 s of a 2 s file, so the grip sits a tenth of the way across.
        const juce::Point<float> grip { atPosition (0.1f), plot.getBottom() - 8.0f };

        display.mouseDown (eventAt (display, grip));
        REQUIRE (gesturesOpened == 1);

        display.mouseDrag (eventAt (display, grip.withX (atPosition (0.25f))));

        REQUIRE (moved.has_value());
        CHECK (*moved == WaveformDisplay::Fade::in);
        CHECK (seconds == Catch::Approx (0.5f).margin (0.02f));

        display.mouseUp (eventAt (display, grip));
        CHECK (gesturesOpened == 0);
    }

    SECTION ("The out fade's grip measures back from the end")
    {
        // 0.4 s back from the end of a 2 s file: four fifths of the way across.
        const juce::Point<float> grip { atPosition (0.8f), plot.getBottom() - 8.0f };

        display.mouseDown (eventAt (display, grip));
        display.mouseDrag (eventAt (display, grip.withX (atPosition (0.7f))));

        REQUIRE (moved.has_value());
        CHECK (*moved == WaveformDisplay::Fade::out);
        CHECK (seconds == Catch::Approx (0.6f).margin (0.02f));
    }

    SECTION ("Above the fade band the same x belongs to the trim line")
    {
        // The start trim line and the in fade's grip are both reachable; which one you
        // get is decided by height, which is what lets a fade of zero be picked up at
        // all. Here the pointer is over the region's start, high up.
        std::optional<WaveformDisplay::Edge> edge;
        display.onEdgeDragged = [&] (WaveformDisplay::Edge e, float) { edge = e; };

        const juce::Point<float> onLine { atPosition (0.0f), plot.getY() + 30.0f };

        display.mouseDown (eventAt (display, onLine));
        display.mouseDrag (eventAt (display, onLine.withX (atPosition (0.05f))));

        CHECK (edge.has_value());
        CHECK_FALSE (moved.has_value());
    }
}

TEST_CASE ("The fades are visible on the response", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    WaveformDisplay display;
    display.setSize (600, 300);
    display.setLengthSeconds (2.0);
    display.setRegion (0.0f, 1.0f);

    // A flat response, so anything the picture shows is the fade and not the shape of
    // the material.
    display.setWaveform (std::vector<float> (400, 0.5f));

    /** How tall the drawn response is in a column, measured as the run of lit pixels
        about the centre line. Measuring the response rather than hashing the image is
        what separates "the fade is drawn on top" from "the fade is applied": a guide
        curve alone would change the picture without changing this. */
    const auto heightAt = [&display] (float proportionAcross)
    {
        const auto image = display.createComponentSnapshot (display.getLocalBounds());
        const auto plotLeft = (int) WaveformDisplay::axisSide + 1;
        const auto plotWidth = display.getWidth() - 2 * plotLeft;
        const auto x = plotLeft + (int) (proportionAcross * (float) plotWidth);

        const auto centre = display.getHeight() / 2;

        // Against the empty panel rather than against a named colour: the fill, the
        // outline and the centre line are three different colours and the only thing
        // they have in common is not being the background.
        const auto empty = image.getPixelAt (x, (int) WaveformDisplay::axisTop + 3);
        auto lit = 0;

        // From just above the centre line, which is drawn over the response and is
        // not part of it.
        for (int y = centre - 3; y >= 0; --y)
        {
            const auto pixel = image.getPixelAt (x, y);
            const auto difference = std::abs (pixel.getRed() - empty.getRed())
                                  + std::abs (pixel.getGreen() - empty.getGreen())
                                  + std::abs (pixel.getBlue() - empty.getBlue());

            if (difference < 12)
                break;

            ++lit;
        }

        return lit;
    };

    display.setFades (0.0f, 0.0f);
    const auto plainEarly = heightAt (0.1f);
    const auto plainLate = heightAt (0.9f);

    REQUIRE (plainEarly > 10);
    REQUIRE (plainLate > 10);

    SECTION ("A fade in pulls the head of the response down, and only the head")
    {
        // A tenth of the way into a 2 s file is a quarter of the way through a 0.8 s
        // fade, where the squared ramp stands at 1/16 -- 24 dB down, which on this
        // display's 60 dB axis is a little under half the height it had.
        display.setFades (0.8f, 0.0f);
        const auto quarterWay = heightAt (0.1f);

        CHECK (quarterWay < plainEarly);
        CHECK (heightAt (0.9f) == plainLate);

        // Longer fade, same place, less of it left: the one relationship that cannot
        // hold unless the envelope really is being applied to the response.
        display.setFades (1.6f, 0.0f);
        CHECK (heightAt (0.1f) < quarterWay);
    }

    SECTION ("A fade out pulls the tail down, and only the tail")
    {
        display.setFades (0.0f, 0.8f);
        const auto quarterWay = heightAt (0.9f);

        CHECK (quarterWay < plainLate);
        CHECK (heightAt (0.1f) == plainEarly);

        display.setFades (0.0f, 1.6f);
        CHECK (heightAt (0.9f) < quarterWay);
    }
}

TEST_CASE ("The wordmark is centred on its letters, not its bounding box", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    auto wordmark = Celine::Assets::drawable ("SPACE.svg", Celine::Assets::IfMissing::returnNull);

    if (wordmark == nullptr)
        SUCCEED ("No wordmark in this build.");
    else
    {
        constexpr int height = 60;
        juce::Image image (juce::Image::ARGB, 300, height, true);

        {
            juce::Graphics g (image);
            g.setColour (juce::Colours::white);
            Celine::Assets::drawWordmark (g, *wordmark,
                                          image.getBounds().toFloat().reduced (10.0f));
        }

        // Where the ink actually is, rather than where the artwork's box is. The
        // descender puts the box's centre below the letters', so centring the box
        // leaves the drawn letters sitting high -- which is the bug this fixes.
        int top = height, bottom = -1;

        for (int y = 0; y < height; ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (image.getPixelAt (x, y).getAlpha() > 40)
                {
                    top = juce::jmin (top, y);
                    bottom = juce::jmax (bottom, y);
                    break;
                }

        REQUIRE (bottom > top);

        // The x-height band -- everything above the baseline -- is what the eye
        // centres on. Only the `p` reaches below it, so the baseline is found by
        // asking where the ink stops being full width.
        int baseline = bottom;

        for (int y = bottom; y > top; --y)
        {
            int inkColumns = 0;

            for (int x = 0; x < image.getWidth(); ++x)
                if (image.getPixelAt (x, y).getAlpha() > 40)
                    ++inkColumns;

            // The descender is one stem; the letter band is five glyphs' worth.
            if (inkColumns > 12)
            {
                baseline = y;
                break;
            }
        }

        const auto lettersCentre = 0.5f * (float) (top + baseline);
        CHECK (std::abs (lettersCentre - 0.5f * (float) height) < 2.0f);
    }
}

//==============================================================================
TEST_CASE ("Reshaping the response does not tell the host its latency changed", "[instance]")
{
    struct CountingListener : juce::AudioProcessorListener
    {
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
        {
            if (details.latencyChanged)     ++latency;
            if (details.parameterInfoChanged) ++parameterInfo;
        }

        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}

        int latency = 0, parameterInfo = 0;
    };

    PluginProcessor plugin;
    CountingListener listener;
    plugin.addListener (&listener);

    plugin.prepareToPlay (sampleRate, blockSize);

    // Every path that rebuilds the response, short of the throttled one a drag goes
    // through -- which cannot run here because it needs a message loop.
    juce::MemoryBlock state;
    plugin.getStateInformation (state);

    for (int i = 0; i < 20; ++i)
    {
        setParameter (plugin, ParamID::size, 0.5f + 0.15f * (float) i);
        plugin.setStateInformation (state.getData(), (int) state.getSize());
    }

    plugin.removeListener (&listener);

    // Neither flag, and latency is the one that matters. A host told its latency has
    // changed re-plans its graph, and several do that by suspending processing --
    // so sending it once per rebuild, as this used to, meant a drag on a shaping
    // control restarted the audio engine several times a second. That is what the
    // spiking and the broken-up sound were.
    CHECK (listener.latency == 0);
    CHECK (listener.parameterInfo == 0);
}

//==============================================================================
TEST_CASE ("A long response runs far inside real time", "[audio]")
{
    // A performance test, and it is here because the failure it guards against is not
    // a wrong answer but an unusable one. Partitioning the response uniformly at the
    // host's block size -- juce::dsp::Convolution's default, and what this plugin used
    // to do -- cost 100% of a core for a ten-second response at a 64-sample block,
    // measured. Non-uniform partitioning puts the same case near 4%.
    //
    // The threshold is loose on purpose: a tenth of real time still leaves an order of
    // magnitude either side of both numbers, so this fails when the partitioning is
    // wrong and not when the machine is busy.
    constexpr int smallBlock = 64;
    constexpr double seconds = 2.0;

    PluginProcessor plugin;

    // Set before preparing, and that order is load-bearing. A parameter change defers
    // its rebuild to the message thread, which does not run here -- so setting Size
    // afterwards would leave the default response in place and this would cheerfully
    // time the wrong thing. prepareToPlay rebuilds on the spot.
    setParameter (plugin, ParamID::size, 4.0f);
    setParameter (plugin, ParamID::mix, 1.0f);

    plugin.prepareToPlay (sampleRate, smallBlock);

    // Four times the 1.8 second default: seven seconds of response.
    REQUIRE (plugin.getTailLengthSeconds() > 5.0);

    juce::AudioBuffer<float> buffer (2, smallBlock);
    juce::MidiBuffer midi;
    fillWithNoise (buffer);

    const auto blocks = (int) (seconds * sampleRate) / smallBlock;

    // Warm up: the engine is prepared on a background thread and installed by
    // processBlock, so the first blocks would be timing a passthrough.
    for (int i = 0; i < 200; ++i)
    {
        plugin.processBlock (buffer, midi);
        juce::Thread::sleep (1);
    }

    const auto start = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < blocks; ++i)
        plugin.processBlock (buffer, midi);

    const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - start) * 0.001;
    const auto fractionOfRealTime = elapsed / seconds;

    INFO ("rendered " << seconds << " s in " << elapsed << " s -- "
          << (fractionOfRealTime * 100.0) << "% of a core at a " << smallBlock
          << " sample block");

    // Two thresholds, because an unoptimised build is measuring a different program:
    // the same case runs at 4% in Release and 17% in Debug. Both leave the guard a
    // wide margin -- uniform partitioning put Release at 68%, and Debug proportionally
    // worse -- so this still fails on the mistake it is here for, in either build.
   #if JUCE_DEBUG
    CHECK (fractionOfRealTime < 0.5);
   #else
    CHECK (fractionOfRealTime < 0.1);
   #endif
}

TEST_CASE ("A fade covers the same part of the response at any Size", "[ir]")
{
    // The invariant behind the handles staying put. The display's axis is the file,
    // and it draws a fade as a span of that file -- so the fade has to occupy a fixed
    // proportion of the response regardless of Size, or the drawn handle and the audio
    // disagree and one of them slides. Fades used to be a fixed duration of the
    // output, which made that proportion depend on Size: at Size 4 the same setting
    // faded a quarter of what it faded at Size 1, and the handle moved on its own
    // while Size was being dragged.
    ImpulseResponse ir;
    ir.loadDefault (48000.0);

    /** How far into the response the fade in has finished, as a proportion of its
        length. Found from the samples rather than from the settings, so it is the
        shaping being measured and not the arithmetic being restated. */
    const auto kneeProportion = [] (const juce::AudioBuffer<float>& buffer)
    {
        const auto total = buffer.getNumSamples();
        REQUIRE (total > 0);

        // The envelope is squared, so it approaches full smoothly. Peak over short
        // windows, and the knee is where that peak stops climbing.
        constexpr int window = 512;
        float best = 0.0f;
        int knee = 0;

        for (int start = 0; start + window < total / 2; start += window)
        {
            const auto peak = buffer.getMagnitude (0, start, window);

            if (peak > best * 1.02f)
            {
                best = peak;
                knee = start + window;
            }
        }

        return (float) knee / (float) total;
    };

    ImpulseResponse::Shape shape;
    shape.fadeInMs = 300.0f;
    shape.fadeOutMs = 0.0f;

    shape.size = 1.0f;
    const auto atUnit = kneeProportion (ir.shape (shape));

    shape.size = 4.0f;
    const auto atFour = kneeProportion (ir.shape (shape));

    shape.size = 0.25f;
    const auto atQuarter = kneeProportion (ir.shape (shape));

    INFO ("knee at size 0.25 / 1 / 4: " << atQuarter << " / " << atUnit << " / " << atFour);

    REQUIRE (atUnit > 0.02f);
    CHECK (atFour == Catch::Approx (atUnit).margin (0.02f));
    CHECK (atQuarter == Catch::Approx (atUnit).margin (0.02f));
}

TEST_CASE ("The EQ reset puts every band back and reports when there is nothing to do", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());
    REQUIRE (editor != nullptr);

    /** The reset button, found by its label rather than by index: the bottom row's
        contents change with the stage, and a positional lookup would silently start
        testing a different button. */
    const std::function<juce::Button* (juce::Component&)> findReset =
        [&findReset] (juce::Component& parent) -> juce::Button*
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* button = dynamic_cast<juce::Button*> (child))
                if (button->getButtonText() == "RESET")
                    return button;

            if (auto* found = findReset (*child))
                return found;
        }

        return nullptr;
    };

    auto* reset = findReset (*editor);
    REQUIRE (reset != nullptr);

    const auto value = [&plugin] (const char* id)
    {
        return plugin.getAPVTS().getRawParameterValue (id)->load();
    };

    const auto defaults = std::vector<float> { value (ParamID::lowFreq), value (ParamID::lowGain),
                                               value (ParamID::peakFreq), value (ParamID::peakGain),
                                               value (ParamID::peakQ),
                                               value (ParamID::highFreq), value (ParamID::highGain) };

    SECTION ("Nothing to reset while the EQ is untouched")
    {
        CHECK_FALSE (reset->isEnabled());
    }

    SECTION ("A moved frequency counts as modified, not only a moved gain")
    {
        setParameter (plugin, ParamID::peakFreq, 4000.0f);
        editor->resized();

        // The editor polls on a timer; the test drives that by hand rather than
        // waiting on a clock.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);

        CHECK (reset->isEnabled());
    }

    SECTION ("Reset returns every band, and the button goes quiet again")
    {
        setParameter (plugin, ParamID::lowGain, 9.0f);
        setParameter (plugin, ParamID::peakGain, -7.0f);
        setParameter (plugin, ParamID::peakFreq, 3000.0f);
        setParameter (plugin, ParamID::peakQ, 4.0f);
        setParameter (plugin, ParamID::highGain, 5.0f);
        setParameter (plugin, ParamID::highFreq, 12000.0f);

        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        REQUIRE (reset->isEnabled());

        reset->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);

        const auto after = std::vector<float> { value (ParamID::lowFreq), value (ParamID::lowGain),
                                                value (ParamID::peakFreq), value (ParamID::peakGain),
                                                value (ParamID::peakQ),
                                                value (ParamID::highFreq), value (ParamID::highGain) };

        for (size_t i = 0; i < defaults.size(); ++i)
            CHECK_THAT (after[i], Catch::Matchers::WithinRel (defaults[i], 1.0e-4f));

        CHECK_FALSE (reset->isEnabled());
    }

    SECTION ("Reset leaves everything that is not the EQ alone")
    {
        setParameter (plugin, ParamID::lowGain, 9.0f);
        setParameter (plugin, ParamID::mix, 0.8f);
        setParameter (plugin, ParamID::size, 2.5f);
        setParameter (plugin, ParamID::fadeOut, 900.0f);

        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        reset->triggerClick();

        CHECK (value (ParamID::mix) == Catch::Approx (0.8f).margin (1.0e-3f));
        CHECK (value (ParamID::size) == Catch::Approx (2.5f).margin (1.0e-3f));
        CHECK (value (ParamID::fadeOut) == Catch::Approx (900.0f).margin (1.0f));
    }
}


TEST_CASE ("The excluded outline stops at the region rather than crossing it", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    WaveformDisplay display;
    display.setSize (600, 300);
    display.setLengthSeconds (2.0);
    display.setFades (0.0f, 0.0f);

    // Three levels, so the excluded outline would have to cross the region as a
    // diagonal rather than hug an edge where it could hide. Loud before the region,
    // near-silent inside it, quiet after.
    std::vector<float> peaks ((size_t) 400, 1.0f);

    for (size_t i = 160; i < 240; ++i)
        peaks[i] = 0.004f;

    for (size_t i = 240; i < peaks.size(); ++i)
        peaks[i] = 0.03f;

    display.setWaveform (peaks);
    display.setRegion (0.4f, 0.6f);

    const auto image = display.createComponentSnapshot (display.getLocalBounds());
    const auto plotLeft = (int) WaveformDisplay::axisSide + 1;
    const auto plotWidth = display.getWidth() - 2 * plotLeft;

    const auto top = (int) WaveformDisplay::axisTop + 1;
    const auto centre = display.getHeight() / 2;
    const auto background = Celine::Theme::background();

    // Half way through the region, and off the eighth-divisions of the time axis so a
    // grid line cannot be mistaken for the outline.
    const auto x = plotLeft + (int) (0.47f * (float) plotWidth);

    auto highestInk = centre;

    for (int y = centre - 3; y >= top; --y)
    {
        const auto pixel = image.getPixelAt (x, y);
        const auto difference = std::abs (pixel.getRed() - background.getRed())
                              + std::abs (pixel.getGreen() - background.getGreen())
                              + std::abs (pixel.getBlue() - background.getBlue());

        if (difference >= 10)
            highestInk = y;
    }

    const auto reach = (float) (centre - highestInk) / (float) (centre - top);

    INFO ("ink reaches " << (reach * 100.0f) << "% of the way up the plot, mid-region");

    // Inside the region the response is 48 dB down, a fifth of the way up a 60 dB
    // axis. The stray line ran between a full-height point and a much lower one, so
    // mid-region it sat far above anything the response itself draws.
    CHECK (reach < 0.4f);
}

TEST_CASE ("Readouts and ranges say what the controls are", "[parameters]")
{
    PluginProcessor plugin;
    auto& apvts = plugin.getAPVTS();

    SECTION ("Times read as whole milliseconds, with the unit")
    {
        auto* preDelay = apvts.getParameter (ParamID::preDelay);
        REQUIRE (preDelay != nullptr);

        const auto textAt = [preDelay] (float ms)
        {
            return preDelay->getText (preDelay->convertTo0to1 (ms), 0);
        };

        CHECK (textAt (0.0f) == "0 ms");
        CHECK (textAt (40.0f) == "40 ms");

        // The readout used to carry a decimal below ten, where a tenth of a
        // millisecond is four samples and the digit only flickered while dragging.
        CHECK_FALSE (textAt (4.4f).containsChar ('.'));
        CHECK (textAt (4.4f) == "4 ms");

        // And back again, so typing the readout in returns the value it came from.
        CHECK (preDelay->getValueForText ("40 ms")
               == Catch::Approx (preDelay->convertTo0to1 (40.0f)).margin (1.0e-4f));
    }

    SECTION ("Output trims by twelve dB either way")
    {
        auto* output = apvts.getParameter (ParamID::outputGain);
        REQUIRE (output != nullptr);

        const auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (output);
        REQUIRE (ranged != nullptr);

        const auto range = ranged->getNormalisableRange();
        CHECK (range.start == Catch::Approx (-12.0f));
        CHECK (range.end == Catch::Approx (12.0f));

        // Zero still sits dead centre, which is what makes the default findable.
        CHECK (ranged->convertTo0to1 (0.0f) == Catch::Approx (0.5f).margin (1.0e-4f));
    }
}

TEST_CASE ("Both displays are on screen at once, side by side", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());
    REQUIRE (editor != nullptr);

    const std::function<juce::Component* (juce::Component&, const juce::String&)> findByType =
        [&findByType] (juce::Component& parent, const juce::String& kind) -> juce::Component*
    {
        for (auto* child : parent.getChildren())
        {
            if ((kind == "waveform" && dynamic_cast<WaveformDisplay*> (child) != nullptr)
                || (kind == "eq" && dynamic_cast<EqDisplay*> (child) != nullptr))
                return child;

            if (auto* found = findByType (*child, kind))
                return found;
        }

        return nullptr;
    };

    auto* waveform = findByType (*editor, "waveform");
    auto* eq = findByType (*editor, "eq");

    REQUIRE (waveform != nullptr);
    REQUIRE (eq != nullptr);

    // The point of the change: neither one waits behind a tab for the other.
    CHECK (waveform->isVisible());
    CHECK (eq->isVisible());

    SECTION ("Laid out beside each other, at matching size")
    {
        const auto left = waveform->getBounds();
        const auto right = eq->getBounds();

        CHECK (left.getRight() <= right.getX());
        CHECK (left.getWidth() == right.getWidth());
        CHECK (left.getY() == right.getY());
        CHECK (left.getHeight() == right.getHeight());
    }

    SECTION ("They stay beside each other, and inside the window, at any size")
    {
        for (const int width : { 960, 1180, 1600 })
        {
            editor->setSize (width, (int) ((float) width / 2.37f));

            const auto left = waveform->getBounds();
            const auto right = eq->getBounds();

            INFO ("at " << width << " wide: waveform " << left.toString()
                  << ", eq " << right.toString());

            CHECK (left.getWidth() > 200);
            CHECK (left.getRight() <= right.getX());
            CHECK (editor->getLocalBounds().contains (right));
        }
    }
}

//==============================================================================
TEST_CASE ("Peak Q is stored in Butterworth units", "[parameters]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    setParameter (plugin, ParamID::peakGain, 12.0f);
    setParameter (plugin, ParamID::peakFreq, 1000.0f);
    setParameter (plugin, ParamID::peakQ, 1.0f);

    // What the editor does once a frame before it draws. The curve is built from the
    // parameters rather than read out of the ramped filters the audio thread owns.
    plugin.refreshDisplayEq();

    // The curve the plugin reports, against filters built the way JUCE expects -- the
    // peak with an actual Q of 1/sqrt(2), which is what a normalised 1 has to mean, or
    // the readout is telling a different story from the filter.
    //
    // The two cuts are in the reference as well, parked at their defaults. They are
    // barely doing anything there, but "barely" is not "nothing": a first order high
    // pass at 20 Hz still takes a sixth of a dB off at 100, which is more than this
    // test's margin. Leaving them out would mean tightening the margin until the test
    // stopped saying anything about Q.
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    const auto peak = Coefficients::makePeakFilter (
        sampleRate, 1000.0f, butterworthQ, juce::Decibels::decibelsToGain (12.0f));
    const auto highPass = Coefficients::makeFirstOrderHighPass (sampleRate, 20.0f);
    const auto lowPass = Coefficients::makeFirstOrderLowPass (sampleRate, 20000.0f);

    for (const float frequency : { 100.0f, 500.0f, 1000.0f, 2000.0f, 8000.0f })
    {
        const auto magnitude = peak->getMagnitudeForFrequency ((double) frequency, sampleRate)
                             * highPass->getMagnitudeForFrequency ((double) frequency, sampleRate)
                             * lowPass->getMagnitudeForFrequency ((double) frequency, sampleRate);

        const auto expected = juce::Decibels::gainToDecibels ((float) magnitude, -60.0f);

        INFO ("at " << frequency << " Hz");
        CHECK (plugin.getEqMagnitudeDb (frequency) == Catch::Approx (expected).margin (0.05f));
    }

    SECTION ("And the readout is the normalised number, not the filter's")
    {
        auto* q = plugin.getAPVTS().getParameter (ParamID::peakQ);
        REQUIRE (q != nullptr);

        CHECK (q->getText (q->convertTo0to1 (1.0f), 0) == "1.00");
        CHECK (q->getText (q->convertTo0to1 (4.0f), 0) == "4.00");
    }
}

TEST_CASE ("An EQ handle dragged past its range stops where the parameter does", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    EqDisplay display;
    display.setSize (600, 300);

    // The low band's own range, which is narrower than the axis it is drawn on.
    constexpr float lowest = 20.0f;
    constexpr float highest = 1000.0f;
    display.setBandRange (EqDisplay::Band::low, lowest, highest);
    display.setBand (EqDisplay::Band::low, { 120.0f, 0.0f });

    float reported = 0.0f;
    display.onBandDragged = [&reported] (EqDisplay::Band, float frequency, float) { reported = frequency; };

    const auto at = [&display] (juce::Point<float> position)
    {
        return juce::MouseEvent { juce::Desktop::getInstance().getMainMouseSource(),
                                  position, juce::ModifierKeys::currentModifiers,
                                  1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &display, &display, juce::Time::getCurrentTime(),
                                  position, juce::Time::getCurrentTime(), 1, false };
    };

    // Take hold of the handle, then drag it far past the top of its range.
    const auto plot = PlotGeometry (display.getLocalBounds().toFloat().reduced (1.0f)
                                        .withTrimmedTop (8.0f).withTrimmedBottom (20.0f)
                                        .withTrimmedLeft (30.0f).withTrimmedRight (30.0f));

    display.mouseDown (at ({ plot.freqToX (120.0f), plot.getCentreY() }));
    display.mouseDrag (at ({ plot.getRight() - 2.0f, plot.getCentreY() }));

    // Both halves clamped: what is reported to the parameter, and what the display
    // has drawn. They used to disagree -- the handle followed the pointer out to
    // 20 kHz while the parameter stopped at a kilohertz, and the editor's poll pulled
    // it back a frame later, so it sat flickering between the two.
    CHECK (reported <= highest + 1.0f);
    CHECK (display.stateFor (EqDisplay::Band::low).frequency <= highest + 1.0f);
    CHECK (display.stateFor (EqDisplay::Band::low).frequency == Catch::Approx (reported).margin (1.0f));
}

TEST_CASE ("The dry signal is measured separately from the processed one", "[audio]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);
    plugin.setUiActive (true);

    // A cut deep enough that the two spectra cannot be confused for one another.
    setParameter (plugin, ParamID::peakGain, -18.0f);
    setParameter (plugin, ParamID::peakFreq, 1000.0f);
    setParameter (plugin, ParamID::mix, 1.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < 200; ++block)
    {
        fillWithNoise (buffer, block);
        plugin.processBlock (buffer, midi);
    }

    std::vector<float> wet, dry;
    REQUIRE (plugin.getOutputSpectrum (wet));
    REQUIRE (plugin.getDrySpectrum (dry));
    REQUIRE (wet.size() == dry.size());

    const auto binAt = [&plugin] (float frequency)
    {
        const auto binHz = (float) sampleRate / (float) plugin.getSpectrumFftSize();
        return (size_t) juce::roundToInt (frequency / binHz);
    };

    // Around the cut the processed trace has to sit well below the dry one; well away
    // from it the two should be in the same neighbourhood. That pair is what says the
    // dry analyser is measuring the input and not a second copy of the output.
    const auto cut = binAt (1000.0f);
    CHECK (wet[cut] < 0.5f * dry[cut]);

    const auto away = binAt (60.0f);
    CHECK (wet[away] > 0.5f * dry[away]);
}

//==============================================================================
TEST_CASE ("The cut filters roll off at six dB an octave", "[audio]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    setParameter (plugin, ParamID::highPass, 500.0f);
    setParameter (plugin, ParamID::lowPass, 2000.0f);
    plugin.refreshDisplayEq();

    // One pole is 6 dB an octave, which is what these are specified as. Two octaves
    // below the corner the high pass has to be about 12 dB down; a biquad would be
    // 24, so this separates "a filter is there" from "the right filter is there".
    const auto atOctavesBelow = [&plugin] (float corner, float octaves)
    {
        return plugin.getEqMagnitudeDb (corner * std::pow (2.0f, -octaves));
    };

    const auto atOctavesAbove = [&plugin] (float corner, float octaves)
    {
        return plugin.getEqMagnitudeDb (corner * std::pow (2.0f, octaves));
    };

    SECTION ("The high pass")
    {
        CHECK (plugin.getEqMagnitudeDb (500.0f) == Catch::Approx (-3.0f).margin (0.6f));
        CHECK (atOctavesBelow (500.0f, 2.0f) == Catch::Approx (-12.3f).margin (1.0f));
        CHECK (atOctavesBelow (500.0f, 3.0f) == Catch::Approx (-18.1f).margin (1.0f));
    }

    SECTION ("The low pass")
    {
        CHECK (plugin.getEqMagnitudeDb (2000.0f) == Catch::Approx (-3.0f).margin (0.6f));
        CHECK (atOctavesAbove (2000.0f, 2.0f) == Catch::Approx (-12.3f).margin (1.0f));
    }

    SECTION ("And they are audible, not just drawn")
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        setParameter (plugin, ParamID::mix, 1.0f);
        plugin.setUiActive (true);

        for (int block = 0; block < 200; ++block)
        {
            fillWithNoise (buffer, block);
            plugin.processBlock (buffer, midi);
        }

        std::vector<float> wet;
        REQUIRE (plugin.getOutputSpectrum (wet));

        const auto binAt = [&plugin] (float frequency)
        {
            const auto binHz = (float) sampleRate / (float) plugin.getSpectrumFftSize();
            return (size_t) juce::roundToInt (frequency / binHz);
        };

        // Inside the pass band against well outside it, at both ends.
        CHECK (wet[binAt (60.0f)] < 0.5f * wet[binAt (1000.0f)]);
        CHECK (wet[binAt (12000.0f)] < 0.5f * wet[binAt (1000.0f)]);
    }
}

TEST_CASE ("A cut bar can be dragged and stops at its range", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    EqDisplay display;
    display.setSize (600, 300);
    display.setCutRange (EqDisplay::Cut::high, 20.0f, 2000.0f);
    display.setCut (EqDisplay::Cut::high, 200.0f);
    display.setCutRange (EqDisplay::Cut::low, 1000.0f, 20000.0f);
    display.setCut (EqDisplay::Cut::low, 20000.0f);

    std::optional<EqDisplay::Cut> moved;
    float reported = 0.0f;
    int gestures = 0;

    display.onCutDragged = [&] (EqDisplay::Cut cut, float hz) { moved = cut; reported = hz; };
    display.onCutGesture = [&] (EqDisplay::Cut, bool starting) { gestures += starting ? 1 : -1; };

    const auto plot = PlotGeometry (display.getLocalBounds().toFloat().reduced (1.0f)
                                        .withTrimmedTop (8.0f).withTrimmedBottom (20.0f)
                                        .withTrimmedLeft (30.0f).withTrimmedRight (30.0f));

    const auto at = [&display] (juce::Point<float> position)
    {
        return juce::MouseEvent { juce::Desktop::getInstance().getMainMouseSource(),
                                  position, juce::ModifierKeys::currentModifiers,
                                  1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &display, &display, juce::Time::getCurrentTime(),
                                  position, juce::Time::getCurrentTime(), 1, false };
    };

    // Grab the high pass bar low down, where no band handle sits.
    const auto grip = juce::Point<float> (plot.freqToX (200.0f), plot.getBottom() - 20.0f);

    display.mouseDown (at (grip));
    REQUIRE (gestures == 1);

    display.mouseDrag (at (grip.withX (plot.freqToX (800.0f))));

    REQUIRE (moved.has_value());
    CHECK (*moved == EqDisplay::Cut::high);
    CHECK (reported == Catch::Approx (800.0f).epsilon (0.05));
    CHECK (display.cutFrequency (EqDisplay::Cut::high) == Catch::Approx (reported).epsilon (0.01));

    SECTION ("And it will not go past the top of its range")
    {
        display.mouseDrag (at (grip.withX (plot.getRight() - 1.0f)));

        CHECK (reported <= 2000.0f + 1.0f);
        CHECK (display.cutFrequency (EqDisplay::Cut::high) <= 2000.0f + 1.0f);
    }

    display.mouseUp (at (grip));
    CHECK (gestures == 0);
}

TEST_CASE ("The fades spread evenly rather than holding on and dropping", "[ir]")
{
    ImpulseResponse ir;
    ir.loadDefault (48000.0);

    // A flat response, so the envelope is the only thing shaping what comes back.
    ImpulseResponse::Shape shape;
    shape.fadeInMs = 400.0f;
    shape.fadeOutMs = 0.0f;

    const auto faded = ir.shape (shape);
    REQUIRE (faded.getNumSamples() > 0);

    shape.fadeInMs = 0.0f;
    const auto plain = ir.shape (shape);

    /** The fade's gain at a point, read back as the ratio of two RMS windows -- the
        source is noise, so a single sample says more about which random value landed
        there than about the envelope. */
    const auto gainAt = [&] (float proportionOfFade)
    {
        const auto fadeSamples = (int) (0.4 * 48000.0);
        const auto centre = (int) (proportionOfFade * (float) fadeSamples);
        constexpr int window = 2048;
        const auto from = juce::jmax (0, centre - window / 2);

        return faded.getRMSLevel (0, from, window) / juce::jmax (1.0e-9f,
                                                                 plain.getRMSLevel (0, from, window));
    };

    // Half way through, a raised cosine stands at 0.5 -- 6 dB down. The squared ramp
    // this replaced stood at 0.25, which is 12, and that is what made the fade read as
    // holding on and then being switched off.
    CHECK (juce::Decibels::gainToDecibels (gainAt (0.5f)) == Catch::Approx (-6.0f).margin (1.5f));

    // A quarter of the way in, 16.7 dB down against the squared ramp's 24. Every point
    // along a raised cosine sits above the squared ramp, which is the whole change:
    // the same span, spread instead of saved up for the end.
    CHECK (juce::Decibels::gainToDecibels (gainAt (0.25f)) == Catch::Approx (-16.7f).margin (1.5f));
}

TEST_CASE ("Q travels logarithmically with one at the centre", "[parameters]")
{
    PluginProcessor plugin;
    auto* q = plugin.getAPVTS().getParameter (ParamID::peakQ);
    REQUIRE (q != nullptr);

    // Half way along the travel is 1, and each half of it covers a factor of ten. On
    // a linear range everything broader than Butterworth lived in the first tenth of
    // the slider.
    CHECK (q->convertTo0to1 (1.0f) == Catch::Approx (0.5f).margin (0.02f));
    CHECK (q->convertFrom0to1 (0.0f) == Catch::Approx (0.1f).margin (0.01f));
    CHECK (q->convertFrom0to1 (1.0f) == Catch::Approx (10.0f).margin (0.01f));

    // And it is a skew rather than two straight halves: a quarter along lands near the
    // geometric middle of the lower decade, not its arithmetic one.
    CHECK (q->convertFrom0to1 (0.25f) < 0.5f);
}

//==============================================================================
TEST_CASE ("An abrupt EQ change is ramped rather than switched", "[audio]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    setParameter (plugin, ParamID::mix, 1.0f);
    setParameter (plugin, ParamID::peakGain, 0.0f);
    setParameter (plugin, ParamID::peakFreq, 8000.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    double phase = 0.0;
    const auto fillWithSine = [&]
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto s = (float) std::sin (phase) * 0.25f;
            phase += 2.0 * juce::MathConstants<double>::pi * 300.0 / sampleRate;

            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.setSample (c, i, s);
        }
    };

    for (int i = 0; i < 600; ++i) { fillWithSine(); plugin.processBlock (buffer, midi); }

    auto settled = 0.0f;

    for (int i = 0; i < 20; ++i)
    {
        fillWithSine();
        plugin.processBlock (buffer, midi);
        settled = juce::jmax (settled, buffer.getMagnitude (0, 0, blockSize));
    }

    REQUIRE (settled > 0.01f);

    SECTION ("The level walks toward the new setting instead of arriving at it")
    {
        // The tone sits at the bell's centre, so a gain change moves it directly.
        setParameter (plugin, ParamID::peakFreq, 300.0f);
        setParameter (plugin, ParamID::peakQ, 1.0f);

        for (int i = 0; i < 600; ++i) { fillWithSine(); plugin.processBlock (buffer, midi); }

        auto before = 0.0f;

        for (int i = 0; i < 20; ++i)
        {
            fillWithSine();
            plugin.processBlock (buffer, midi);
            before = juce::jmax (before, buffer.getMagnitude (0, 0, blockSize));
        }

        setParameter (plugin, ParamID::peakGain, 18.0f);

        std::vector<float> levels;

        for (int i = 0; i < 30; ++i)
        {
            fillWithSine();
            plugin.processBlock (buffer, midi);
            levels.push_back (buffer.getMagnitude (0, 0, blockSize));
        }

        // Two blocks in, a 50 ms ramp has barely started; thirty blocks in it has
        // arrived. Without the ramp the first block would already be there.
        const auto target = before * juce::Decibels::decibelsToGain (18.0f);

        INFO ("before " << before << ", after two blocks " << levels[1]
              << ", after thirty " << levels.back() << ", target " << target);

        CHECK (levels[1] < 0.5f * target);
        CHECK (levels.back() > 0.8f * target);
    }

    SECTION ("But the drawn curve follows the control at once, not the ramp")
    {
        setParameter (plugin, ParamID::peakGain, 18.0f);
        plugin.refreshDisplayEq();

        // No audio has been processed since the change, so the running filters have
        // not moved at all -- and the curve is still right, because it is built from
        // the parameters. A curve that lagged the ramp would trail the pointer by the
        // length of it.
        CHECK (plugin.getEqMagnitudeDb (8000.0f) == Catch::Approx (18.0f).margin (0.5f));
    }

    SECTION ("And the signal does not blow up on the way")
    {
        // The whole filter jumped four octaves at once. Rewriting the coefficients
        // under a running biquad threw a transient far above the signal that produced
        // it -- a steady sine peaking at 0.25 came out at 1.03, which is the static.
        setParameter (plugin, ParamID::peakFreq, 500.0f);
        setParameter (plugin, ParamID::peakGain, 18.0f);

        auto worst = 0.0f;

        for (int i = 0; i < 60; ++i)
        {
            fillWithSine();
            plugin.processBlock (buffer, midi);
            worst = juce::jmax (worst, buffer.getMagnitude (0, 0, blockSize));
        }

        // A +18 dB bell centred near the tone legitimately makes it louder; what it
        // must not do is overshoot that on the way there.
        const auto boosted = settled * juce::Decibels::decibelsToGain (18.0f);
        CHECK (worst < 1.4f * boosted);
    }
}

TEST_CASE ("The processed trace measures the wet path, not the mix", "[audio]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);
    plugin.setUiActive (true);

    // Fully dry: nothing of the wet path reaches the output at all. The trace still
    // has to show what the EQ is doing to the reverb, because that is what it is a
    // picture of -- measured at the output it would have shown the dry signal instead
    // and the EQ would have appeared to stop working as Mix came down.
    setParameter (plugin, ParamID::mix, 0.0f);
    setParameter (plugin, ParamID::peakGain, -18.0f);
    setParameter (plugin, ParamID::peakFreq, 1000.0f);
    setParameter (plugin, ParamID::peakQ, 3.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < 300; ++block)
    {
        fillWithNoise (buffer, block);
        plugin.processBlock (buffer, midi);
    }

    std::vector<float> wet, dry;
    REQUIRE (plugin.getOutputSpectrum (wet));
    REQUIRE (plugin.getDrySpectrum (dry));

    const auto binAt = [&plugin] (float frequency)
    {
        const auto binHz = (float) sampleRate / (float) plugin.getSpectrumFftSize();
        return (size_t) juce::roundToInt (frequency / binHz);
    };

    const auto cut = binAt (1000.0f);
    const auto reference = binAt (200.0f);

    CHECK (wet[cut] < 0.6f * wet[reference]);

    // And the dry trace, measured on the same signal at the same moment, shows no
    // such notch -- which is what says the two are measuring different points.
    CHECK (dry[cut] > 0.7f * dry[reference]);
}

TEST_CASE ("The cut bars are drawn even when parked at their limits", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    EqDisplay display;
    display.setSize (600, 300);
    display.setCutRange (EqDisplay::Cut::high, 20.0f, 2000.0f);
    display.setCutRange (EqDisplay::Cut::low, 1000.0f, 20000.0f);

    // The defaults: right at the ends of their travel, doing nothing. They used to be
    // hidden there, which meant the only way to find the control was to drag a bar
    // that was not drawn.
    display.setCut (EqDisplay::Cut::high, 20.0f);
    display.setCut (EqDisplay::Cut::low, 20000.0f);

    const auto image = display.createComponentSnapshot (display.getLocalBounds());
    const auto background = Celine::Theme::background();

    /** Ink in the strip where the grips sit, in a narrow column around x. */
    const auto inkNear = [&] (int x)
    {
        auto count = 0;

        for (int dx = -5; dx <= 5; ++dx)
            for (int y = 10; y < 28; ++y)
            {
                const auto p = image.getPixelAt (juce::jlimit (0, image.getWidth() - 1, x + dx), y);

                if (std::abs (p.getRed() - background.getRed())
                    + std::abs (p.getGreen() - background.getGreen())
                    + std::abs (p.getBlue() - background.getBlue()) > 20)
                    ++count;
            }

        return count;
    };

    // The plot's own edges, which is where a cut parked at its limit sits.
    const auto plotLeft = 31;
    const auto plotRight = display.getWidth() - 31;

    CHECK (inkNear (plotLeft) > 20);
    CHECK (inkNear (plotRight) > 20);
}
