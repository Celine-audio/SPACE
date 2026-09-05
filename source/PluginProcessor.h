#pragma once

#include "Parameters.h"
#include "dsp/ImpulseResponse.h"
#include "dsp/SpectrumAnalyzer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>

/**
    SPACE: a convolution reverb.

    The signal path is dry, and in parallel: pre-delay, convolution, a width matrix, a
    post EQ, then a crossfade back against the dry. Only the convolution's response is
    expensive to change, which is why the parameters are split the way they are -- see
    ParamID::irShaping.
*/
class PluginProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    /** Message thread: loads a response and rebuilds the convolution. */
    juce::Result loadImpulseResponse (const juce::File&);

    /** The response as loaded, for the display to draw. Message thread only. */
    const ImpulseResponse& getImpulseResponse() const noexcept { return impulse; }

    /** Where the file came from, so a session reload can find it again. */
    juce::File getImpulseResponseFile() const;

    /** The magnitude spectrum of the plugin's output, for the EQ view's backdrop.
        Returns false when nothing has been analysed yet. */
    bool getOutputSpectrum (std::vector<float>& dest) const { return analyser.getAveragedMagnitudes (dest); }

    /** The same measurement taken of the signal on the way in, so the display can
        show what the plugin was given behind what it is handing back. */
    bool getDrySpectrum (std::vector<float>& dest) const { return dryAnalyser.getAveragedMagnitudes (dest); }
    int getSpectrumFftSize() const noexcept { return 1 << spectrumFftOrder; }

    /** How many frames the analyser has produced. The editor watches this to tell
        "quiet" from "not running", so a stopped transport fades the trace out rather
        than leaving the last one frozen on screen looking live.

        The dry one, because it is the one that runs whenever audio arrives at all:
        the wet analyser stops while the plugin is bypassed, which is not the same
        question and would fade the display out on a plugin that is working fine. */
    std::int64_t getSpectrumFrameCount() const noexcept { return dryAnalyser.getFrameCount(); }

    void setUiActive (bool active) noexcept;

    /** Message thread: rebuilds the coefficients the display reads, from the current
        parameter values.

        A separate set from the ones the audio runs, and separate on purpose. The
        running filters are ramped and are written only by the audio thread -- reading
        them from here would be a race, and would also draw a curve trailing 50 ms
        behind the control being dragged. These are built from the parameters
        themselves, by the same code, so the two cannot say different things about the
        same settings; and they stay correct with the transport stopped, when a host
        may not be calling processBlock at all. */
    void refreshDisplayEq();

    /** The magnitude response of the post EQ at a frequency, in dB. Reads the display
        set above, so it is safe to call from the message thread and only from it. */
    float getEqMagnitudeDb (float frequency) const;

private:
    void parameterChanged (const juce::String&, float) override;
    void timerCallback() override;

    /** Set when a shaping parameter moved on a thread that may not ask for a rebuild
        itself -- which is the audio thread, under host automation. Posting a message,
        which is what juce::AsyncUpdater does, takes a lock and can allocate, so a flag
        is the only thing that thread may leave behind. */
    std::atomic<bool> rebuildRequested { false };

    /** Turns that flag back into a call on the message thread.

        Its own timer, not the one above: that one is a one-shot used to *delay* two
        different jobs, and it stops itself when they are done. This one has to keep
        looking. Cheap while the flag is clear, which is nearly always. */
    juce::TimedCallback shapingPoll { [this] { pollRebuildRequest(); } };
    void pollRebuildRequest();

    static constexpr int shapingPollMs = 30;

    /** Rebuilds the shaped response and hands it to the convolution. Message thread;
        throttled, because dragging a shaping control fires this per mouse move and
        each one is a resample plus a convolution reload. */
    void rebuildConvolution();
    void requestRebuild();

    /** Sums a buffer to mono through the scratch buffer and hands it to an analyser.
        Audio thread; allocates nothing. */
    void analyse (SpectrumAnalyzer&, const juce::AudioBuffer<float>&, int numChannels, int numSamples);

    /** Moves the EQ toward the parameters and rewrites the filter coefficients.

        Audio thread, once a block. `snap` jumps straight to the parameters, for
        prepareToPlay; otherwise the smoothed values advance by numSamples and the
        coefficients follow them.

        Allocation-free, which is the reason it is written against ArrayCoefficients
        rather than the Coefficients makers everyone reaches for first: those return a
        newly allocated, reference-counted object. ArrayCoefficients returns the six
        numbers by value, and assigning them into a Coefficients that already holds a
        set of the same length reuses its storage. */
    void updateEq (int numSamples, bool snap);
    ImpulseResponse::Shape currentShape() const;

    /** The wet path for one slice of a block. Sliced rather than whole so a host that
        hands over a bigger block than it promised is answered by going round twice,
        not by growing a buffer on the audio thread. */
    void processWet (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                     int numChannels, float mixTarget);

    juce::AudioProcessorValueTreeState apvts;

    ImpulseResponse impulse;
    juce::String impulseFilePath;

    // Non-uniform partitioning, and this is the single biggest thing in the plugin.
    // The default constructor partitions the response uniformly at the host's block
    // size, which for a reverb-length response is the pathological case: the cost per
    // block is proportional to the number of partitions, so halving the block size
    // doubles the work per block *and* doubles the number of blocks. Measured at 48
    // kHz on a ten-second response it came to 137% of a core at a 64-sample block --
    // more than a core, which is why the meter pinned and the audio broke up.
    //
    // Non-uniform partitioning handles the head of the response in small partitions
    // and the tail in large ones, which is the same arithmetic every convolution
    // reverb does. Same 137% case: 3.9%. Still zero latency, and the output matches
    // the uniform engine to within 5e-7 -- a convolution is a convolution, only the
    // bookkeeping changes.
    //
    // 1024 rather than larger: a bigger head keeps improving the steady state (4096
    // reaches 1.2%) but each step costs time in loadImpulseResponse, and that runs
    // repeatedly while a shaping control is being dragged.
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 1024 } };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelay { 96000 };

    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;
    juce::dsp::ProcessorDuplicator<Filter, Coefficients> highPass, lowPass;
    juce::dsp::ProcessorDuplicator<Filter, Coefficients> lowShelf, peak, highShelf;

    // The display's own copies -- see refreshDisplayEq.
    Coefficients displayHighPass, displayLowPass, displayLowShelf, displayPeak, displayHighShelf;

    juce::dsp::Gain<float> outputGain;

    // Ramped, all three. Each of these was read once per block and applied as a
    // constant, so dragging one stepped the signal at every block boundary -- which
    // is a click on a gain, and a jump in the read pointer on the delay. Pre-delay is
    // the worst of the three: a linear-interpolated delay line whose delay moves in
    // jumps does not glide, it tears.
    juce::SmoothedValue<float> mixSmoothed, widthSmoothed, preDelaySmoothed;

    // The EQ, ramped as well, and for a louder reason than the three above. Its
    // coefficients used to be rewritten the moment a parameter moved, which on a
    // running biquad is a discontinuity: sweeping the peak gain produced steps of
    // 0.185 between adjacent samples where the signal itself was stepping 0.0098.
    // That is the static -- a click per mouse event, at whatever rate the mouse
    // reports.
    //
    // Frequencies and Q ramp multiplicatively, so a sweep moves at a constant number
    // of octaves per second rather than crawling at the bottom and rushing at the top.
    using LogSmoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;

    LogSmoothed highPassHz, lowPassHz, lowFreqHz, peakFreqHz, peakQValue, highFreqHz;
    juce::SmoothedValue<float> lowGainDb, peakGainDb, highGainDb;

    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> monoScratch;

    // Two: what came in, and what is going out. The input has to be measured before
    // the wet path touches the buffer, which is why the capture is split across the
    // two ends of processBlock rather than done in one place.
    SpectrumAnalyzer analyser, dryAnalyser;

    double currentSampleRate = 48000.0;
    std::atomic<bool> uiActive { false };
    std::atomic<bool> convolutionReady { false };

    // Throttles the rebuild. A shaping drag fires a callback per mouse move, and each
    // rebuild resamples the response and reloads the convolution; without this a drag
    // spends the whole gesture doing that and the interface stops answering.
    static constexpr juce::uint32 rebuildIntervalMs = 150;
    juce::uint32 lastRebuildMs = 0;

    // Whether a rebuild is still owed, and whether the host has been told the tail
    // length changed since the last one. Two separate questions sharing one timer --
    // see rebuildConvolution for why the second is not simply done every time.
    static constexpr juce::uint32 settleIntervalMs = 600;
    bool rebuildPending = false;
    bool tailLengthAnnounced = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
