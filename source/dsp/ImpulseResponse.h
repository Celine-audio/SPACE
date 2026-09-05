#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>

/**
    The loaded impulse response, and the shaped version of it the convolution runs.

    Two buffers, and the split is the point. `source` is the file as it was read,
    untouched — trimming and stretching are destructive, so doing them in place would
    mean a control could only ever be dragged one way. `shape()` builds the processed
    response from it each time, which is what lets Start, End, Length and Size be
    dragged back and forth without the file being re-read.

    Nothing here is realtime-safe. It all runs on the message thread; the audio thread
    only ever sees the finished buffer, handed over by the convolver.
*/
class ImpulseResponse
{
public:
    /** How the loaded response should be reshaped. Proportions rather than times, so
        the same settings mean the same thing after loading a different file. */
    struct Shape
    {
        float start = 0.0f;      // 0..1 through the file
        float end = 1.0f;        // 0..1, and always past start
        float fadeInMs = 0.0f;   // eased in over this much of the *file*
        float fadeOutMs = 150.0f; // ...and out over this; both stretch with size
        float size = 1.0f;       // resampling factor; >1 is a bigger, slower space
    };

    /** The longest response shape() will produce, whatever it is asked for. Public
        because the tail length reported to the host has to agree with it. */
    static constexpr double maximumSeconds = 10.0;

    ImpulseResponse();

    /** Reads a file. Returns a failure result with something worth showing a user:
        a missing codec and a corrupt file are different problems. */
    juce::Result loadFrom (const juce::File&);

    /** Replaces the response with a synthetic one, so the plugin makes a sound before
        anybody has loaded anything. Exponentially decaying noise is the cheapest thing
        that is recognisably a room rather than a test tone. */
    void loadDefault (double sampleRate);

    /** Resamples the source to a new rate, in place. Called when the host's rate
        changes: an impulse response is a recording, and playing it at the wrong rate
        transposes the room. */
    void setPlaybackSampleRate (double);

    bool isEmpty() const noexcept { return source.getNumSamples() == 0; }
    int getNumSamples() const noexcept { return source.getNumSamples(); }
    int getNumChannels() const noexcept { return source.getNumChannels(); }
    double getSampleRate() const noexcept { return sourceSampleRate; }

    /** Seconds of the file as loaded, before any shaping. */
    double getLengthSeconds() const noexcept;

    const juce::String& getName() const noexcept { return name; }
    const juce::AudioBuffer<float>& getSource() const noexcept { return source; }

    /** Builds the response the convolution should run, from the source and a shape.
        Returns an empty buffer when there is nothing loaded. */
    juce::AudioBuffer<float> shape (const Shape&) const;

    /** A peak-per-column summary of the source for drawing, at whatever width the
        display has. `from` and `to` are proportions of the file, so the display can
        ask for a zoomed window without knowing anything about sample counts.

        Peaks rather than samples: at any useful width there are hundreds of samples
        per pixel, and picking one of them to draw makes a waveform that shimmers as
        the window resizes and hides transients entirely. */
    void summarise (std::vector<float>& magnitudes, int numColumns,
                    float from = 0.0f, float to = 1.0f) const;

private:
    juce::AudioFormatManager formats;

    juce::AudioBuffer<float> source;
    double sourceSampleRate = 0.0;
    juce::String name;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImpulseResponse)
};
