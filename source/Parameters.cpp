#include "Parameters.h"

namespace Parameters
{
    namespace
    {
        // Attribute helpers. JUCE will happily render a mix parameter as "0.7071068",
        // so every parameter a human reads gets a string function. They live here
        // rather than at each declaration so two parameters of the same kind cannot
        // drift into formatting themselves differently.

        juce::AudioParameterFloatAttributes percent()
        {
            return juce::AudioParameterFloatAttributes()
                .withLabel ("%")
                .withStringFromValueFunction ([] (float v, int)
                {
                    return juce::String (juce::roundToInt (v * 100.0f));
                })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() * 0.01f; });
        }

        juce::AudioParameterFloatAttributes decibels()
        {
            return juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int)
                {
                    return (v > 0.0f ? "+" : "") + juce::String (v, 1);
                })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
        }

        juce::AudioParameterFloatAttributes milliseconds()
        {
            return juce::AudioParameterFloatAttributes()
                .withLabel ("ms")
                .withStringFromValueFunction ([] (float v, int)
                {
                    // Whole milliseconds, and the unit written out. withLabel alone
                    // does not reach the readout: a slider attached to a parameter
                    // takes its text from getText, which is this function and nothing
                    // else -- so the label showed up in a host's automation list and
                    // nowhere in the plugin. A tenth of a millisecond of pre-delay is
                    // four samples, which is not a number anybody is setting on
                    // purpose, and the decimal only made the readout jitter.
                    return juce::String (juce::roundToInt (v)) + " ms";
                })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
        }

        /** A frequency control wants far more resolution at the bottom than the top:
            without a skew, half the travel is spent above 10 kHz. The centre is the
            frequency that lands under the middle of the knob. */
        juce::NormalisableRange<float> frequencyRange (float low, float high, float centre)
        {
            juce::NormalisableRange<float> range { low, high };
            range.setSkewForCentre (centre);
            return range;
        }

        juce::AudioParameterFloatAttributes frequency()
        {
            return juce::AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int)
                {
                    if (v < 1000.0f)
                        return juce::String (juce::roundToInt (v));

                    return juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " k";
                })
                .withValueFromStringFunction ([] (const juce::String& t)
                {
                    const auto value = t.getFloatValue();
                    return t.containsIgnoreCase ("k") ? value * 1000.0f : value;
                });
        }

        //======================================================================
        auto boolParam (const char* id, const char* name, bool defaultValue)
        {
            return std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { id, 1 }, name, defaultValue);
        }

        auto floatParam (const char* id, const char* name,
                         juce::NormalisableRange<float> range, float defaultValue,
                         juce::AudioParameterFloatAttributes attributes = {})
        {
            return std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, name, range, defaultValue, std::move (attributes));
        }

        juce::NormalisableRange<float> unit() { return { 0.0f, 1.0f, 0.0001f }; }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (boolParam (ParamID::bypass, "Bypass", false));

        // Fully wet by default would be a strange first impression for a reverb, and
        // fully dry would look broken. A third is the usual send-ish starting point.
        layout.add (floatParam (ParamID::mix, "Mix", unit(), 0.35f, percent()));

        // 1 is the file as recorded; 0 collapses it to mono. Narrower than the source
        // only -- a width control that invents stereo from a mono file is inventing
        // information, and on a reverb tail that reads as phasiness rather than space.
        layout.add (floatParam (ParamID::width, "Width", unit(), 1.0f, percent()));

        // Twelve either way. This is a trim on the end of a reverb, not a gain stage:
        // the useful travel is a few dB, and a wider range only makes the middle of
        // the control harder to land on.
        layout.add (floatParam (ParamID::outputGain, "Output",
                                { -12.0f, 12.0f, 0.1f }, 0.0f, decibels()));

        //======================================================================
        // 250 ms is past the point where pre-delay stops reading as space and starts
        // reading as a slap, which is far enough for anything musical.
        layout.add (floatParam (ParamID::preDelay, "Pre-delay",
                                frequencyRange (0.0f, 250.0f, 40.0f), 0.0f, milliseconds()));

        // Where the used part of the file begins and ends, as a proportion of it. The
        // start is what the zoom on the display exists for: the first few milliseconds
        // decide whether a reverb sounds tight or smeared, and they are a sliver of
        // the file's width at any sane zoom level.
        layout.add (floatParam (ParamID::irStart, "IR Start", unit(), 0.0f, percent()));
        layout.add (floatParam (ParamID::irEnd, "IR End", unit(), 1.0f, percent()));

        // Fades over the trimmed region, measured in milliseconds of the file so that
        // Size stretches them along with everything else -- see the fade handling in
        // ImpulseResponse::shape. The out fade is what stops a response that has been
        // cut short from ending in a step -- convolution turns a step into a click on
        // every transient -- so it is never allowed all the way to zero; the control
        // sets how much more than that minimum you want, and long fades are what turn
        // a room into a swell.
        layout.add (floatParam (ParamID::fadeIn, "Fade In",
                                frequencyRange (0.0f, 1000.0f, 100.0f), 0.0f, milliseconds()));
        layout.add (floatParam (ParamID::fadeOut, "Fade Out",
                                frequencyRange (0.0f, 4000.0f, 400.0f), 150.0f, milliseconds()));

        // Resamples the response. Bigger is slower and longer, which is what a bigger
        // room does; the range is what stays plausible before it reads as a pitch
        // effect rather than a room.
        layout.add (floatParam (ParamID::size, "Size",
                                { 0.25f, 4.0f, 0.001f }, 1.0f,
                                juce::AudioParameterFloatAttributes()
                                    .withLabel ("x")
                                    .withStringFromValueFunction ([] (float v, int)
                                    {
                                        return juce::String (v, 2);
                                    })));

        //======================================================================
        // Six dB an octave, one pole each. A steeper slope on a reverb tail rings at
        // its own corner, which on a decaying signal is heard as a pitch rather than
        // as a filter; gentle slopes are what these are for. Parked at the ends of
        // their ranges by default, which is as close to out of the way as a first
        // order filter gets.
        layout.add (floatParam (ParamID::highPass, "High Pass",
                                frequencyRange (20.0f, 2000.0f, 200.0f), 20.0f, frequency()));
        layout.add (floatParam (ParamID::lowPass, "Low Pass",
                                frequencyRange (1000.0f, 20000.0f, 6000.0f), 20000.0f, frequency()));

        layout.add (floatParam (ParamID::lowFreq, "Low Freq",
                                frequencyRange (20.0f, 1000.0f, 150.0f), 120.0f, frequency()));
        layout.add (floatParam (ParamID::lowGain, "Low Gain",
                                { -18.0f, 18.0f, 0.1f }, 0.0f, decibels()));

        layout.add (floatParam (ParamID::peakFreq, "Peak Freq",
                                frequencyRange (100.0f, 10000.0f, 1000.0f), 1000.0f, frequency()));
        layout.add (floatParam (ParamID::peakGain, "Peak Gain",
                                { -18.0f, 18.0f, 0.1f }, 0.0f, decibels()));
        // Normalised, so 1 is the Butterworth width rather than an arbitrary 0.707.
        // The number stored here is what the readout shows; the filter multiplies it
        // by butterworthQ to get the Q a biquad actually takes. Expressing it any
        // other way means the one value people reach for -- "flat, no resonance" --
        // sits at a decimal nobody remembers.
        // Skewed, so 1 sits under the middle of the travel and each half covers a
        // factor of ten. On a linear range the whole bottom decade -- everything
        // broader than Butterworth -- lived in the first tenth of the slider.
        layout.add (floatParam (ParamID::peakQ, "Peak Q",
                                frequencyRange (0.1f, 10.0f, 1.0f), 1.0f,
                                juce::AudioParameterFloatAttributes()
                                    .withStringFromValueFunction ([] (float v, int)
                                    {
                                        return juce::String (v, 2);
                                    })));

        layout.add (floatParam (ParamID::highFreq, "High Freq",
                                frequencyRange (1000.0f, 20000.0f, 6000.0f), 8000.0f, frequency()));
        layout.add (floatParam (ParamID::highGain, "High Gain",
                                { -18.0f, 18.0f, 0.1f }, 0.0f, decibels()));

        return layout;
    }
}
