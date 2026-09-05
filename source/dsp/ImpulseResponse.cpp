#include "ImpulseResponse.h"

#include <cmath>

namespace
{
    /** Reads `source` at a fractional position, linearly. Good enough for resampling
        a reverb tail: the material is noise-like and already smoothed by the room, so
        the interpolation error sits far below anything the tail itself is doing. */
    float sampleAt (const float* data, int numSamples, double position) noexcept
    {
        if (numSamples <= 0)
            return 0.0f;

        const auto clamped = juce::jlimit (0.0, (double) (numSamples - 1), position);
        const auto lower = (int) clamped;
        const auto upper = juce::jmin (lower + 1, numSamples - 1);
        const auto fraction = (float) (clamped - (double) lower);

        return data[lower] + fraction * (data[upper] - data[lower]);
    }
}

//==============================================================================
ImpulseResponse::ImpulseResponse()
{
    formats.registerBasicFormats();
}

double ImpulseResponse::getLengthSeconds() const noexcept
{
    if (sourceSampleRate <= 0.0)
        return 0.0;

    return (double) source.getNumSamples() / sourceSampleRate;
}

//==============================================================================
juce::Result ImpulseResponse::loadFrom (const juce::File& file)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("That file no longer exists.");

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return juce::Result::fail ("Not an audio file this build can read: "
                                   + file.getFileName());

    if (reader->lengthInSamples <= 0)
        return juce::Result::fail ("That file holds no audio.");

    // Guard rather than trust. A convolution's cost is linear in the length of its
    // response, so a file somebody dropped in by mistake -- a whole song, say -- would
    // not fail, it would just make the plugin unusable and leave them wondering why.
    // Not the same limit as maximumSeconds, which caps the response after shaping:
    // this one is about what is worth loading at all, and a file can be longer than
    // the response taken from it because Start and End cut into it.
    constexpr double maximumFileSeconds = 30.0;
    const auto seconds = (double) reader->lengthInSamples / reader->sampleRate;

    if (seconds > maximumFileSeconds)
        return juce::Result::fail ("That is " + juce::String (seconds, 1)
                                   + " seconds long. Impulse responses over "
                                   + juce::String ((int) maximumFileSeconds)
                                   + " seconds are refused: convolution costs scale with length.");

    // At most stereo. A four-channel true-stereo response is a real thing, but it
    // needs a 2x2 convolution matrix rather than two channels, and quietly using the
    // first two would be wrong in a way nobody could hear the cause of.
    const auto channels = (int) juce::jmin (reader->numChannels, 2u);

    juce::AudioBuffer<float> loaded (channels, (int) reader->lengthInSamples);

    if (! reader->read (&loaded, 0, (int) reader->lengthInSamples, 0, true, channels > 1))
        return juce::Result::fail ("Could not read " + file.getFileName() + ".");

    source = std::move (loaded);
    sourceSampleRate = reader->sampleRate;
    name = file.getFileNameWithoutExtension();

    return juce::Result::ok();
}

void ImpulseResponse::loadDefault (double sampleRate)
{
    if (sampleRate <= 0.0)
        sampleRate = 48000.0;

    constexpr double seconds = 1.8;
    const auto numSamples = (int) (seconds * sampleRate);

    source.setSize (2, numSamples);
    juce::Random random { 20260902 };

    for (int channel = 0; channel < 2; ++channel)
    {
        auto* data = source.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
        {
            // Exponential decay over noise: the crudest thing that is recognisably a
            // room. It exists so the plugin makes a sound the moment it is loaded,
            // not as a reverb anybody would keep.
            const auto t = (double) i / (double) numSamples;
            const auto envelope = std::exp (-6.5 * t);
            data[i] = (float) ((random.nextDouble() * 2.0 - 1.0) * envelope);
        }

        // A short fade in, so the response does not begin with a click of its own.
        const auto fade = juce::jmin (64, numSamples);

        for (int i = 0; i < fade; ++i)
            data[i] *= (float) i / (float) fade;
    }

    sourceSampleRate = sampleRate;
    name = "Default";
}

void ImpulseResponse::setPlaybackSampleRate (double newRate)
{
    if (newRate <= 0.0 || sourceSampleRate <= 0.0 || isEmpty())
        return;

    if (std::abs (newRate - sourceSampleRate) < 1.0e-6)
        return;

    // A response is a recording of a room. Played at a different rate it is the same
    // room transposed -- shorter and brighter, or longer and duller -- which is a
    // thing people notice without being able to say why.
    const auto ratio = newRate / sourceSampleRate;
    const auto numSamples = juce::jmax (1, (int) std::llround ((double) source.getNumSamples() * ratio));

    juce::AudioBuffer<float> resampled (source.getNumChannels(), numSamples);

    for (int channel = 0; channel < source.getNumChannels(); ++channel)
    {
        const auto* in = source.getReadPointer (channel);
        auto* out = resampled.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
            out[i] = sampleAt (in, source.getNumSamples(), (double) i / ratio);
    }

    source = std::move (resampled);
    sourceSampleRate = newRate;
}

//==============================================================================
juce::AudioBuffer<float> ImpulseResponse::shape (const Shape& shapeToApply) const
{
    if (isEmpty())
        return {};

    const auto total = source.getNumSamples();

    // Start and End are independent controls and nothing stops a user dragging one
    // past the other. Ordering them here rather than constraining the parameters
    // keeps the two honest: the display can show them crossed while a drag is in
    // flight without the audio ever seeing a negative-length response.
    const auto lower = juce::jlimit (0.0f, 1.0f, juce::jmin (shapeToApply.start, shapeToApply.end));
    const auto upper = juce::jlimit (0.0f, 1.0f, juce::jmax (shapeToApply.start, shapeToApply.end));

    const auto first = (int) ((float) total * lower);
    const auto last = (int) ((float) total * upper);
    const auto trimmed = last - first;

    if (trimmed < 16)
        return {};

    // Size stretches what the handles left. The response is resampled, so the output
    // is longer or shorter than the region it came from.
    const auto stretch = juce::jlimit (0.25f, 4.0f, shapeToApply.size);

    // Capped, and the cap is the point. The loader accepts up to 30 seconds and Size
    // multiplies by four, so an untrimmed long file at maximum Size asked for a
    // two-minute response -- five and three quarter million samples, forty-odd
    // megabytes, handed to the convolution engine again on every throttled rebuild
    // while a control is being dragged. That is not a filter, it is a way to run a
    // host out of memory, and it presented as the plugin crashing.
    //
    // Ten seconds is past any reverb tail worth having; beyond it the response is
    // truncated rather than the control refusing to move, so a drag stays smooth.
    const auto maximumSamples = (int) (maximumSeconds * sourceSampleRate);
    const auto outputLength = juce::jlimit (16, maximumSamples,
                                            (int) ((double) trimmed * (double) stretch));

    juce::AudioBuffer<float> result (source.getNumChannels(), outputLength);

    // Milliseconds of the *file*, converted to output samples by the same stretch as
    // everything else. Denominating them in output time instead meant a fade held a
    // fixed duration while Size changed the length of what it was fading, so the same
    // setting covered a quarter of the response at Size 4 and all of it at Size 0.25
    // -- and on a display whose axis is the file, the handle slid away under the
    // pointer whenever Size moved. A bigger room fades over a longer time, which is
    // both what a room does and what keeps the handle where it was put.
    const auto msToSamples = [this, stretch] (float ms)
    {
        return (int) ((double) juce::jmax (0.0f, ms) * 0.001 * sourceSampleRate
                      * (double) stretch);
    };

    // The tail is always brought to silence, however short the fade is asked to be:
    // cutting a response off leaves a step at its end, and convolution turns a step
    // into a click on every transient. The control sets how much longer than this
    // floor the fade runs, not whether there is one.
    constexpr int minimumFadeOut = 64;

    // Neither fade may eat the other, or a short response with two long fades would
    // be scaled to nothing and the reverb would simply vanish.
    const auto half = outputLength / 2;
    const auto fadeIn = juce::jlimit (0, half, msToSamples (shapeToApply.fadeInMs));
    const auto fadeOut = juce::jlimit (minimumFadeOut, half, msToSamples (shapeToApply.fadeOutMs));

    for (int channel = 0; channel < source.getNumChannels(); ++channel)
    {
        const auto* in = source.getReadPointer (channel);
        auto* out = result.getWritePointer (channel);

        for (int i = 0; i < outputLength; ++i)
        {
            const auto position = (double) first + (double) i / (double) stretch;
            out[i] = sampleAt (in, total, position);
        }

        // A raised cosine, and the shape is the point. These were squared ramps, which
        // are almost flat for the first half of the fade and then fall off a cliff:
        // a quarter of the way through a fade out a squared ramp has given up 5 dB,
        // half way through 12, and the rest arrives in a rush at the end. The fade
        // read as holding on and then being switched off.
        //
        // A raised cosine spreads the same span far more evenly -- 1.4 dB, then 6 --
        // and it leaves and arrives with zero slope, so neither end has a corner in it
        // for a transient to find.
        const auto raisedCosine = [] (float x)
        {
            return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi
                                           * juce::jlimit (0.0f, 1.0f, x));
        };

        for (int i = 0; i < fadeIn; ++i)
            out[i] *= raisedCosine ((float) i / (float) fadeIn);

        for (int i = 0; i < fadeOut; ++i)
        {
            const auto index = outputLength - fadeOut + i;
            out[index] *= raisedCosine (1.0f - (float) i / (float) fadeOut);
        }
    }

    return result;
}

//==============================================================================
void ImpulseResponse::summarise (std::vector<float>& magnitudes, int numColumns,
                                 float from, float to) const
{
    magnitudes.assign ((size_t) juce::jmax (0, numColumns), 0.0f);

    if (isEmpty() || numColumns <= 0)
        return;

    const auto total = source.getNumSamples();
    const auto firstSample = (int) ((float) total * juce::jlimit (0.0f, 1.0f, from));
    const auto lastSample = (int) ((float) total * juce::jlimit (0.0f, 1.0f, to));
    const auto span = lastSample - firstSample;

    if (span <= 0)
        return;

    for (int column = 0; column < numColumns; ++column)
    {
        const auto begin = firstSample + span * column / numColumns;
        const auto end = juce::jmax (begin + 1, firstSample + span * (column + 1) / numColumns);

        float peak = 0.0f;

        for (int channel = 0; channel < source.getNumChannels(); ++channel)
        {
            const auto* data = source.getReadPointer (channel);

            for (int i = begin; i < end && i < total; ++i)
                peak = juce::jmax (peak, std::abs (data[i]));
        }

        magnitudes[(size_t) column] = peak;
    }
}
