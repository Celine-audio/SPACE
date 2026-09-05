#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

/**
    SPACE's parameters: their IDs in one place, and the layout that declares them.
    Everything that reads a parameter goes through ParamID rather than a string
    literal, so a typo is a compile error instead of a null pointer at runtime.
*/
namespace ParamID
{
    inline constexpr auto bypass     = "bypass";
    inline constexpr auto mix        = "mix";
    inline constexpr auto width      = "width";
    inline constexpr auto outputGain = "outputGain";

    // --- the shape of the impulse response ------------------------------------
    inline constexpr auto preDelay   = "preDelay";
    inline constexpr auto irStart    = "irStart";
    inline constexpr auto irEnd      = "irEnd";
    inline constexpr auto fadeIn     = "fadeIn";
    inline constexpr auto fadeOut    = "fadeOut";
    inline constexpr auto size       = "size";

    // --- the post EQ ----------------------------------------------------------
    inline constexpr auto highPass = "highPass";
    inline constexpr auto lowPass  = "lowPass";

    inline constexpr auto lowFreq    = "lowFreq";
    inline constexpr auto lowGain    = "lowGain";
    inline constexpr auto peakFreq   = "peakFreq";
    inline constexpr auto peakGain   = "peakGain";
    inline constexpr auto peakQ      = "peakQ";
    inline constexpr auto highFreq   = "highFreq";
    inline constexpr auto highGain   = "highGain";

    /** The parameters that change the impulse response itself, and therefore cost a
        rebuild of the convolution rather than a coefficient update.

        Pre-delay, width and mix are deliberately absent: they are a delay line, a
        mid/side matrix and a crossfade applied around the convolver, so they can move
        freely without the filter being touched. Keeping that list short is what keeps
        a knob drag from sounding like a series of edits. */
    inline constexpr std::array irShaping { irStart, irEnd, fadeIn, fadeOut, size };

    /** The post EQ's bands. A coefficient update, cheap enough to do per change. */
    inline constexpr std::array eqShaping { highPass, lowPass, lowFreq, lowGain, peakFreq, peakGain,
                                           peakQ, highFreq, highGain };
}

/** The Q of a Butterworth response, which is what a normalised Q of 1 means.
    The Peak Q parameter is stored in those units -- see its declaration. */
inline constexpr float butterworthQ = 0.70710678f;

namespace Parameters
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
