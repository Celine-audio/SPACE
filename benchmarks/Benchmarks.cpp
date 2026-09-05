#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <catch2/catch_test_macros.hpp>

/*
    The three numbers worth watching from day one. Add benchmarks for your own DSP
    beside them -- measuring before optimising is the only way to find out that the
    slow part is somewhere you were not looking.

    Run with:  ./Builds/Benchmarks
*/

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
}

TEST_CASE ("Boot performance")
{
    // A host instantiates a plugin to scan it. Slow here is slow for every plugin
    // in the folder, every time the user opens their DAW.
    BENCHMARK_ADVANCED ("Processor constructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::storage_for<PluginProcessor>> storage (size_t (meter.runs()));
        meter.measure ([&] (int i) { storage[(size_t) i].construct(); });
    };

    BENCHMARK_ADVANCED ("Editor open and close")
    (Catch::Benchmark::Chronometer meter)
    {
        PluginProcessor plugin;
        meter.measure ([&] (int)
        {
            auto* editor = plugin.createEditorAndMakeActive();
            plugin.editorBeingDeleted (editor);
            delete editor;
            return 0;
        });
    };
}

TEST_CASE ("Audio thread performance")
{
    // The number that decides whether the plugin is usable. At 48 kHz a 512-sample
    // block is 10.7 ms of audio, so anything over about 1 ms here is 10% of a core
    // for one instance.
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> source (2, blockSize);
    juce::Random random { 1 };

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < blockSize; ++i)
            source.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

    juce::AudioBuffer<float> work (2, blockSize);
    juce::MidiBuffer midi;

    BENCHMARK_ADVANCED ("processBlock")
    (Catch::Benchmark::Chronometer meter)
    {
        meter.measure ([&] (int)
        {
            work.makeCopyOf (source);
            plugin.processBlock (work, midi);
            return work.getSample (0, 0);
        });
    };
}

TEST_CASE ("UI frame performance")
{
    // Repainting is what makes an interface feel cheap or expensive. This is the
    // whole editor; in practice only the part that changed repaints, but if the
    // whole thing is slow then so is a resize.
    PluginProcessor plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());

    juce::Image canvas (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);

    BENCHMARK ("Editor repaint")
    {
        juce::Graphics g (canvas);
        editor->paintEntireComponent (g, true);
        return canvas.getWidth();
    };
}
