#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    /** The L2 norm of a buffer -- the square root of its total energy.

        This, and not its RMS or its peak, is what normalises a convolution. Feed noise
        of RMS s through an impulse response h and the output has RMS s * sqrt(sum of
        h squared): the *energy* of the response is its gain, and the energy of a
        reverb grows with its length. Normalising the RMS instead divides that energy
        by the sample count, so the wet path came out sqrt(N) too loud -- +23 dB on a
        1.8-second response, and louder still on a longer one, which is why the dry
        signal sounded like it had vanished. Peak is worse again: a response's peak is
        its first sample, which says nothing about the tail behind it. */
    float energyOf (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const auto numSamples = buffer.getNumSamples();
        const auto numChannels = buffer.getNumChannels();

        if (numSamples <= 0 || numChannels <= 0)
            return 0.0f;

        double sum = 0.0;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int i = 0; i < numSamples; ++i)
                sum += (double) data[i] * (double) data[i];
        }

        // Per channel, not over the whole buffer: a stereo response convolves each
        // channel with its own side, so the gain of the pair is one channel's energy
        // rather than the sum of the two.
        return (float) std::sqrt (sum / (double) numChannels);
    }
}

//==============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", Parameters::createLayout())
{
    for (const auto* id : ParamID::irShaping)
        apvts.addParameterListener (id, this);

    for (const auto* id : ParamID::eqShaping)
        apvts.addParameterListener (id, this);

    // A decaying average, so the EQ view's backdrop is a moving picture of what is
    // coming out rather than a running total of everything that ever has.
    // A slower average than the default. At 0.4 each frame replaced two fifths of
    // what was there, and with a frame arriving every 2048 samples the trace was
    // redrawn from nearly fresh numbers twenty-three times a second -- which on
    // programme material reads as flicker rather than as movement. This settles over
    // about a third of a second, slow enough to be still and fast enough to follow
    // a fader.
    for (auto* spectrum : { &analyser, &dryAnalyser })
    {
        spectrum->setExponentialMode (true, 0.12f);
        spectrum->setCapturing (true);
    }

    shapingPoll.startTimer (shapingPollMs);
}

PluginProcessor::~PluginProcessor()
{
    stopTimer();
    shapingPoll.stopTimer();

    for (const auto* id : ParamID::irShaping)
        apvts.removeParameterListener (id, this);

    for (const auto* id : ParamID::eqShaping)
        apvts.removeParameterListener (id, this);
}

//==============================================================================
double PluginProcessor::getTailLengthSeconds() const
{
    // What the host uses to decide how long to keep rendering after the transport
    // stops. Reported honestly, or a bounce cuts the reverb tail off mid-decay.
    //
    // The *shaped* length, which is not the file's. Trimming makes it shorter and Size
    // stretches it by up to four; reporting the source length meant a response at
    // Size 4 was declared at a quarter of its real length, and a bounce would clip
    // three quarters of the tail. Asked of the same code the audio runs, so the two
    // cannot drift.
    const auto shape = currentShape();
    const auto trimmed = std::abs (shape.end - shape.start) * impulse.getLengthSeconds();
    const auto stretched = trimmed * (double) juce::jlimit (0.25f, 4.0f, shape.size);

    const auto delay = apvts.getRawParameterValue (ParamID::preDelay)->load() * 0.001f;

    return juce::jmin (ImpulseResponse::maximumSeconds, stretched) + (double) delay;
}

void PluginProcessor::setUiActive (bool active) noexcept
{
    const auto wasActive = uiActive.exchange (active);

    // Nothing has fed the analyser since the last editor closed, so its decaying
    // average is frozen on whatever was playing then. Start from silence.
    if (active && ! wasActive)
    {
        analyser.reset();
        dryAnalyser.reset();
    }
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    const auto channels = (juce::uint32) juce::jmax (1, getMainBusNumOutputChannels());
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, channels };

    convolution.prepare (spec);

    preDelay.prepare (spec);
    preDelay.setMaximumDelayInSamples ((int) (0.25 * sampleRate) + samplesPerBlock);
    preDelay.reset();

    for (auto* filter : { &highPass, &lowPass, &lowShelf, &peak, &highShelf })
    {
        filter->prepare (spec);
        filter->reset();
    }

    outputGain.prepare (spec);
    outputGain.setRampDurationSeconds (0.05);

    // The gain is set before the reset, not after. reset() snaps the smoother to its
    // *current target*, and Gain's target starts at zero -- so resetting first leaves
    // the plugin ramping up from silence over the ramp duration every time the host
    // prepares it, which is a 50 ms fade-in on every transport start.
    outputGain.setGainDecibels (apvts.getRawParameterValue (ParamID::outputGain)->load());
    outputGain.reset();

    // Same trap as the gain above: reset() snaps a SmoothedValue to its current
    // target, so each of these is given its value first and reset onto it. 30 ms is
    // short enough that a control still feels immediate and long enough that a drag
    // is a slide rather than a staircase.
    const auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    for (auto* smoothed : { &mixSmoothed, &widthSmoothed, &preDelaySmoothed })
        smoothed->reset (sampleRate, 0.03);

    // 50 ms for the EQ: long enough that a coefficient set never moves far in one
    // block, short enough that a control still feels connected to what it is doing.
    for (auto* smoothed : { &highPassHz, &lowPassHz, &lowFreqHz, &peakFreqHz, &peakQValue, &highFreqHz })
        smoothed->reset (sampleRate, 0.05);

    for (auto* smoothed : { &lowGainDb, &peakGainDb, &highGainDb })
        smoothed->reset (sampleRate, 0.05);

    mixSmoothed.setCurrentAndTargetValue (value (ParamID::mix));
    widthSmoothed.setCurrentAndTargetValue (value (ParamID::width));
    preDelaySmoothed.setCurrentAndTargetValue (value (ParamID::preDelay) * 0.001f * (float) sampleRate);

    wetBuffer.setSize ((int) channels, samplesPerBlock, false, false, true);

    // Generous, and never grown from processBlock: a host that overruns the block
    // size it promised must not be answered with an allocation on the audio thread.
    monoScratch.setSize (1, juce::jmax (1, samplesPerBlock) * 2, false, false, true);

    analyser.reset();
    dryAnalyser.reset();

    // The response is a recording; playing it at a rate it was not sampled at
    // transposes the room. Nothing loaded yet means the default one, so that the
    // plugin makes a sound before anybody has opened a file.
    if (impulse.isEmpty())
        impulse.loadDefault (sampleRate);
    else
        impulse.setPlaybackSampleRate (sampleRate);

    updateEq (0, true);
    refreshDisplayEq();
    rebuildConvolution();
}

void PluginProcessor::releaseResources()
{
    convolution.reset();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainIn == mainOut;
}

namespace
{
    /** The five filter coefficient sets, from the nine numbers that describe them.

        One function, called from two places -- the audio thread with ramped values,
        the message thread with the parameters themselves -- so what is drawn and what
        is heard cannot be built from different formulas. Returns by value: every
        maker here is the ArrayCoefficients form, which is a handful of floats on the
        stack rather than a reference-counted allocation. */
    struct EqCoefficients
    {
        std::array<float, 4> highPass, lowPass;
        std::array<float, 6> lowShelf, peak, highShelf;
    };

    EqCoefficients makeEq (double rate, float highPassHz, float lowPassHz,
                           float lowHz, float lowDb, float peakHz, float peakDb,
                           float q, float highHz, float highDb)
    {
        using Raw = juce::dsp::IIR::ArrayCoefficients<float>;

        const auto limit = (float) (rate * 0.49);
        const auto decibels = [] (float db) { return juce::Decibels::decibelsToGain (db); };

        return { Raw::makeFirstOrderHighPass (rate, juce::jmin (highPassHz, limit)),
                 Raw::makeFirstOrderLowPass (rate, juce::jmin (lowPassHz, limit)),
                 Raw::makeLowShelf (rate, juce::jmin (lowHz, limit), 0.7071f, decibels (lowDb)),

                 // Out of normalised units and into the Q a biquad takes.
                 Raw::makePeakFilter (rate, juce::jmin (peakHz, limit), q * butterworthQ,
                                      decibels (peakDb)),

                 Raw::makeHighShelf (rate, juce::jmin (highHz, limit), 0.7071f, decibels (highDb)) };
    }
}

//==============================================================================
ImpulseResponse::Shape PluginProcessor::currentShape() const
{
    const auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    ImpulseResponse::Shape shape;
    shape.start = value (ParamID::irStart);
    shape.end = value (ParamID::irEnd);
    shape.fadeInMs = value (ParamID::fadeIn);
    shape.fadeOutMs = value (ParamID::fadeOut);
    shape.size = value (ParamID::size);

    return shape;
}

void PluginProcessor::rebuildConvolution()
{
    auto shaped = impulse.shape (currentShape());

    if (shaped.getNumSamples() < 16)
    {
        convolutionReady.store (false);
        return;
    }

    // Normalised so that a fully wet signal comes out at roughly the level that went
    // in, whatever response is loaded and however it has been shaped. Unity energy is
    // what does that -- see energyOf.
    if (const auto energy = energyOf (shaped); energy > 1.0e-9f)
        shaped.applyGain (1.0f / energy);

    convolution.loadImpulseResponse (std::move (shaped), currentSampleRate,
                                     juce::dsp::Convolution::Stereo::yes,
                                     juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::no);

    convolutionReady.store (true);

    // Deliberately *not* updateHostDisplay() here, which is what this used to do on
    // every throttled rebuild. Called with no arguments it sends the default change
    // set -- latencyChanged, parameterInfoChanged and programChanged -- and a host
    // that is told its latency changed re-plans the graph, which several do by
    // suspending and restarting processing. At one rebuild per 150 ms that is roughly
    // seven graph restarts a second for as long as a shaping control is held, and it
    // presented exactly as reported: the transport spiking and the audio breaking up
    // whenever Size was dragged.
    //
    // Nothing about this plugin's latency changes -- the convolution is zero-latency
    // whatever response it holds. What genuinely changes is the tail length, which
    // has no flag of its own, and which a host only needs to know once the gesture
    // has settled. requestRebuild does that.
    tailLengthAnnounced = false;
}

void PluginProcessor::requestRebuild()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto elapsed = now - lastRebuildMs;

    if (elapsed < rebuildIntervalMs)
    {
        // Too soon. Come back when the quiet period is up; a drag that keeps moving
        // keeps pushing this out, and the last position always gets built.
        rebuildPending = true;
        startTimer ((int) (rebuildIntervalMs - elapsed));
        return;
    }

    stopTimer();
    lastRebuildMs = now;
    rebuildPending = false;
    rebuildConvolution();

    // A stretch of quiet after the last rebuild is the earliest point a gesture can
    // be called finished, so the host hears about the new tail length once, then.
    if (! tailLengthAnnounced)
        startTimer ((int) settleIntervalMs);
}

void PluginProcessor::pollRebuildRequest()
{
    // Cleared before the work, not after: a move arriving while a rebuild is being
    // asked for has to leave the flag set for the next tick rather than be swallowed.
    if (rebuildRequested.exchange (false))
        requestRebuild();
}

void PluginProcessor::timerCallback()
{
    stopTimer();

    // Two jobs share the timer. A rebuild that is still owed always wins, and since
    // every rebuild re-arms the announcement, a drag that keeps moving keeps pushing
    // the announcement ahead of itself -- so the host is told once, at the end.
    if (rebuildPending)
    {
        requestRebuild();
        return;
    }

    if (! tailLengthAnnounced)
    {
        tailLengthAnnounced = true;

        // nonParameterStateChanged only: enough for a host to notice the project is
        // dirty and to re-read the tail length, without the latency flag that makes
        // it rebuild its graph.
        updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}
                               .withNonParameterStateChanged (true));
    }
}

void PluginProcessor::parameterChanged (const juce::String& id, float)
{
    // Called on whatever thread moved the parameter, which for host automation is the
    // audio thread. Nothing heavy happens here: the EQ is a handful of coefficients,
    // and the convolution rebuild is deferred to the message thread.
    // Nothing here for the EQ any more: its filters are updated from processBlock,
    // by the audio thread, reading the parameters directly.
    // Only the parameters that describe the response itself. This used to rebuild for
    // anything that was not an EQ control, which meant dragging Mix -- or Width, or
    // Output, or toggling Bypass -- reloaded the convolution several times a second
    // to arrive at exactly the response it already had. Mix and Width are applied to
    // the finished wet signal; they have nothing to say about the impulse.
    for (const auto* shapingId : ParamID::irShaping)
    {
        if (id == shapingId)
        {
            // A flag, and nothing else. This runs on whichever thread moved the
            // parameter -- the audio thread, under host automation -- and the rebuild
            // was always deferred anyway, so nothing about when it happens changes
            // beyond the poll interval.
            rebuildRequested.store (true);
            return;
        }
    }
}

void PluginProcessor::updateEq (int numSamples, bool snap)
{
    const auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    const auto rate = currentSampleRate;

    if (rate <= 0.0)
        return;

    // Read here rather than in parameterChanged, so the coefficients are written by
    // the audio thread and only by it. Host automation used to arrive on the audio
    // thread and the interface on the message thread, both writing the same filters.
    struct { LogSmoothed& smoothed; const char* id; } logs[] = {
        { highPassHz, ParamID::highPass }, { lowPassHz, ParamID::lowPass },
        { lowFreqHz, ParamID::lowFreq },   { peakFreqHz, ParamID::peakFreq },
        { peakQValue, ParamID::peakQ },    { highFreqHz, ParamID::highFreq },
    };

    struct { juce::SmoothedValue<float>& smoothed; const char* id; } linears[] = {
        { lowGainDb, ParamID::lowGain }, { peakGainDb, ParamID::peakGain },
        { highGainDb, ParamID::highGain },
    };

    auto moving = false;

    for (auto& entry : logs)
    {
        if (snap) entry.smoothed.setCurrentAndTargetValue (value (entry.id));
        else      entry.smoothed.setTargetValue (value (entry.id));

        moving = moving || entry.smoothed.isSmoothing();
    }

    for (auto& entry : linears)
    {
        if (snap) entry.smoothed.setCurrentAndTargetValue (value (entry.id));
        else      entry.smoothed.setTargetValue (value (entry.id));

        moving = moving || entry.smoothed.isSmoothing();
    }

    // Nothing is moving, so the coefficients already say what they should. Skipping
    // here is what keeps this off the profile when the EQ is simply sitting still,
    // which is nearly always.
    if (! snap && ! moving)
        return;

    if (! snap)
    {
        for (auto& entry : logs)
            entry.smoothed.skip (numSamples);

        for (auto& entry : linears)
            entry.smoothed.skip (numSamples);
    }

    const auto coefficients = makeEq (rate,
                                      highPassHz.getCurrentValue(), lowPassHz.getCurrentValue(),
                                      lowFreqHz.getCurrentValue(), lowGainDb.getCurrentValue(),
                                      peakFreqHz.getCurrentValue(), peakGainDb.getCurrentValue(),
                                      peakQValue.getCurrentValue(),
                                      highFreqHz.getCurrentValue(), highGainDb.getCurrentValue());

    *highPass.state = coefficients.highPass;
    *lowPass.state = coefficients.lowPass;
    *lowShelf.state = coefficients.lowShelf;
    *peak.state = coefficients.peak;
    *highShelf.state = coefficients.highShelf;
}

void PluginProcessor::refreshDisplayEq()
{
    if (currentSampleRate <= 0.0)
        return;

    const auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    const auto coefficients = makeEq (currentSampleRate,
                                      value (ParamID::highPass), value (ParamID::lowPass),
                                      value (ParamID::lowFreq), value (ParamID::lowGain),
                                      value (ParamID::peakFreq), value (ParamID::peakGain),
                                      value (ParamID::peakQ),
                                      value (ParamID::highFreq), value (ParamID::highGain));

    displayHighPass = coefficients.highPass;
    displayLowPass = coefficients.lowPass;
    displayLowShelf = coefficients.lowShelf;
    displayPeak = coefficients.peak;
    displayHighShelf = coefficients.highShelf;
}

float PluginProcessor::getEqMagnitudeDb (float frequency) const
{
    if (currentSampleRate <= 0.0)
        return 0.0f;

    // Asked of the coefficients the audio is actually running, so the curve on screen
    // cannot drift from the filter. Deriving it separately is how a display ends up
    // confidently drawing something the plugin is not doing.
    const auto magnitude =
        displayHighPass.getMagnitudeForFrequency ((double) frequency, currentSampleRate)
        * displayLowPass.getMagnitudeForFrequency ((double) frequency, currentSampleRate)
        * displayLowShelf.getMagnitudeForFrequency ((double) frequency, currentSampleRate)
        * displayPeak.getMagnitudeForFrequency ((double) frequency, currentSampleRate)
        * displayHighShelf.getMagnitudeForFrequency ((double) frequency, currentSampleRate);

    return juce::Decibels::gainToDecibels ((float) magnitude, -60.0f);
}

//==============================================================================
void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = juce::jmin (buffer.getNumChannels(), getMainBusNumOutputChannels());

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, numSamples);

    const auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    const auto bypassed = value (ParamID::bypass) > 0.5f;

    // The dry measurement, taken here because the wet path mixes into this same
    // buffer further down -- a moment later and it would be measuring the result.
    if (uiActive.load() && numChannels > 0)
        analyse (dryAnalyser, buffer, numChannels, numSamples);

    // Once a block, before the slices, so the coefficients move by the same step
    // whatever the host's block size does. Outside the bypass check on purpose: a
    // filter left behind while the plugin is bypassed would jump on the way back in.
    updateEq (numSamples, false);

    if (! bypassed && convolutionReady.load() && numChannels > 0)
    {
        preDelaySmoothed.setTargetValue (value (ParamID::preDelay) * 0.001f * (float) currentSampleRate);
        widthSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, value (ParamID::width)));

        const auto mix = juce::jlimit (0.0f, 1.0f, value (ParamID::mix));

        // Sliced to whatever was prepared for. A host is entitled to overrun the
        // block size it promised, and the answer to that is another turn round this
        // loop -- never setSize on the audio thread, which is what this used to do.
        const auto slice = juce::jmax (1, wetBuffer.getNumSamples());

        for (int offset = 0; offset < numSamples; offset += slice)
            processWet (buffer, offset, juce::jmin (slice, numSamples - offset),
                        numChannels, mix);
    }

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    // Bypass takes the trim out with the reverb, so A/B-ing compares like with like.
    outputGain.setGainDecibels (bypassed ? 0.0f : value (ParamID::outputGain));
    outputGain.process (context);

}

void PluginProcessor::analyse (SpectrumAnalyzer& target, const juce::AudioBuffer<float>& buffer,
                               int numChannels, int numSamples)
{
    // Summed to mono through the scratch buffer, which is never grown here: a block
    // longer than the one prepared for is analysed as far as there is room and the
    // rest skipped, because the alternative is allocating on the audio thread for the
    // sake of a picture.
    const auto toAnalyse = juce::jmin (numSamples, monoScratch.getNumSamples());
    auto* mono = monoScratch.getWritePointer (0);

    juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), toAnalyse);

    for (int channel = 1; channel < numChannels; ++channel)
        juce::FloatVectorOperations::add (mono, buffer.getReadPointer (channel), toAnalyse);

    if (numChannels > 1)
        juce::FloatVectorOperations::multiply (mono, 1.0f / (float) numChannels, toAnalyse);

    target.pushBlock (mono, toAnalyse);
}

void PluginProcessor::processWet (juce::AudioBuffer<float>& buffer, int startSample,
                                  int numSamples, int numChannels, float mixTarget)
{
    // The wet path is built beside the dry rather than over it: a convolution reverb
    // is a parallel effect, and Mix has to be able to reach fully dry without the tail
    // being cut off mid-decay.
    for (int channel = 0; channel < numChannels; ++channel)
        wetBuffer.copyFrom (channel, 0, buffer, channel, startSample, numSamples);

    juce::dsp::AudioBlock<float> wet (wetBuffer.getArrayOfWritePointers(),
                                      (size_t) numChannels, (size_t) numSamples);

    // Pre-delay first, so the room is heard to start late rather than the room's own
    // early reflections being delayed within it. The delay is set per sample, not per
    // block: a block-rate step in the read position of an interpolating delay line is
    // a discontinuity, and dragging the control clicked on every block boundary.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto delay = preDelaySmoothed.getNextValue();
        preDelay.setDelay (delay);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* samples = wetBuffer.getWritePointer (channel);
            preDelay.pushSample (channel, samples[i]);
            samples[i] = preDelay.popSample (channel);
        }
    }

    {
        juce::dsp::ProcessContextReplacing<float> context (wet);
        convolution.process (context);
    }

    // Width, as a mid/side matrix on the wet only. Narrowing the dry as well would be
    // a stereo control masquerading as a reverb control.
    if (numChannels >= 2)
    {
        auto* left = wetBuffer.getWritePointer (0);
        auto* right = wetBuffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto width = widthSmoothed.getNextValue();
            const auto mid = 0.5f * (left[i] + right[i]);
            const auto side = 0.5f * (left[i] - right[i]) * width;

            left[i] = mid + side;
            right[i] = mid - side;
        }
    }
    else
    {
        widthSmoothed.skip (numSamples);
    }

    {
        juce::dsp::ProcessContextReplacing<float> context (wet);
        highPass.process (context);
        lowPass.process (context);
        lowShelf.process (context);
        peak.process (context);
        highShelf.process (context);
    }

    // Measured here, on the wet path alone, and not at the end of processBlock where
    // it used to be. Down there the signal is already crossfaded against the dry, so
    // at any mix short of fully wet the trace was partly the ghost drawn behind it --
    // the two converged as Mix came down and the EQ's effect appeared to shrink with
    // it, which is not what the EQ is doing.
    if (uiActive.load())
        analyse (analyser, wetBuffer, numChannels, numSamples);

    // Equal-gain rather than equal-power. The wet and dry of a reverb are not
    // correlated, but they are not independent either -- the wet is derived from the
    // dry -- and equal power leaves a bump in the middle of the control.
    mixSmoothed.setTargetValue (mixTarget);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto mix = mixSmoothed.getNextValue();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* dry = buffer.getWritePointer (channel);
            dry[startSample + i] = dry[startSample + i] * (1.0f - mix)
                                 + wetBuffer.getReadPointer (channel)[i] * mix;
        }
    }
}

//==============================================================================
juce::Result PluginProcessor::loadImpulseResponse (const juce::File& file)
{
    const auto result = impulse.loadFrom (file);

    if (result.failed())
        return result;

    impulseFilePath = file.getFullPathName();
    impulse.setPlaybackSampleRate (currentSampleRate);
    rebuildConvolution();

    return juce::Result::ok();
}

juce::File PluginProcessor::getImpulseResponseFile() const
{
    return impulseFilePath.isEmpty() ? juce::File() : juce::File (impulseFilePath);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    if (! state.isValid())
        return;

    // The path, not the audio. Embedding a response would make presets self-contained
    // but also enormous, and a reverb library is a thing people keep on disk anyway.
    state.setProperty ("impulseFile", impulseFilePath, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    const auto stored = apvts.state.getProperty ("impulseFile").toString();

    // A missing file is not an error worth refusing the whole session over: the
    // default response keeps the plugin making a sound, and the name in the interface
    // is what tells somebody the file has moved.
    if (stored.isNotEmpty())
    {
        if (const juce::File file (stored); file.existsAsFile())
            loadImpulseResponse (file);
    }

    updateEq (0, true);
    refreshDisplayEq();
    rebuildConvolution();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
