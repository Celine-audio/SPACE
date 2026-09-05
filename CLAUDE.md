# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

**SPACE** — a convolution reverb, built on Céline Audio's house plugin template
(derived from [Pamplejuce](https://github.com/sudara/pamplejuce)), carrying the house
look, About window, licensing and CI.

A response is loaded and convolved with the input. The waveform on the left is that
response and is edited directly — drag its ends to trim it, its corners to fade it in
or out — while `size` stretches it in time and `pre-delay` holds it off. The graph on
the right is a post EQ applied to the wet signal only, and it is dragged the same way.
Four faders carry size, pre-delay, width and dry/wet.

## Build commands

CLion's default directories are used so CLI and IDE builds share one cache.

```bash
cmake -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

```bash
cmake --build cmake-build-debug
```

```bash
./cmake-build-debug/Tests
```

```bash
./cmake-build-release/Benchmarks
```

Release builds go in `cmake-build-release`. `ctest --test-dir cmake-build-debug`
runs tests and benchmarks together.

## Where code goes

| | |
|---|---|
| The convolution and the wet mix | `PluginProcessor::processBlock` |
| Shaping the response | `source/dsp/ImpulseResponse` — trim, fades, size |
| Parameters | `source/Parameters.h` (ID) and `source/Parameters.cpp` (declaration) — **both**, they are separate lists |
| Product facts | `source/ProductInfo.h` — tagline, repo URL, wordmark, copyright |

## Structure

- `source/` — processor, editor, parameters, product facts
- `source/dsp/` — `ImpulseResponse` (the file and its shaping), `SpectrumAnalyzer`
- `source/ui/` — the house kit (`Theme.h`, `Fonts`, `PluginLookAndFeel`, `AboutPanel`,
  `ParameterControl`, `IconButton`, `EmbeddedAssets`) plus SPACE's own:
  `WaveformDisplay`, `EqDisplay`, `PlotGeometry`
- `tests/` — Catch2; `benchmarks/` — Catch2 benchmarks
- `assets/` — embedded as BinaryData by `cmake/Assets.cmake`, everything in the folder
- `JUCE/`, `cmake/`, `modules/clap-juce-extensions` — submodules

## The house kit

`source/ui/` is shared, near-verbatim, with the other Céline plugins — the same files
are in GALLERY and AURA. Treat a change to any of them as a change to all of them:

`Theme.h`, `Fonts`, `EmbeddedAssets`, `IconButton`, `PluginLookAndFeel`,
`ParameterControl`, `AboutPanel`.

What is *not* shared is anything a plugin decides for itself: `PlotGeometry` and the
two displays above, and whatever roles a plugin adds to `Theme`.

**Every control gets a tooltip**, and the editor owns one `juce::TooltipWindow`
parented to itself, with `setOpaque(false)`: a tooltip paints a rounded panel, and an
opaque component must fill every pixel it owns, so the corners outside the rounding
come out as square spikes of whatever was in the buffer.

`SharedCode` is an INTERFACE library linking the source into both the plugin and the
test targets, which is what keeps them from violating the ODR.

## House conventions

**Assets are looked up by filename, never by the BinaryData identifier.** JUCE derives
those identifiers by *stripping* characters rather than replacing them, so
`arrow-pointer-solid-full.svg` becomes `arrowpointersolidfull_svg`. Getting it wrong is
silent — the lookup returns null and nothing draws. Use `Celine::Assets::drawable("name.svg")`.
An asset that may legitimately be absent passes `IfMissing::returnNull`, or it will
assert on every launch in Debug.

**Colours come from `Theme`, never from a hex literal at the call site.** They are
function-local statics on purpose: held as static `Colour` objects, nothing orders
their initialisation against another translation unit's, and whichever link order won
got an opaque black.

**A component's `setSize` goes last in its constructor.** It fires `resized()`, and
`resized()` measures artwork and children that must exist by then. Called early, it
silently places everything at zero size — and a window that later opens at a different
size hides the bug completely.

## Code quality

Resolve every compile warning. Warnings are errors here.

LSP/clangd reports false positives against JUCE's module system ("undeclared
identifier", "file not found"). Ignore those unless the build actually fails.

## Threading

Two threads:

- **Audio** — `processBlock`. Never allocate, lock, or block.
- **Message** — UI, parameter listeners, timers.

Between them: `std::atomic` or JUCE's parameter types for scalars; a lock-free queue
for anything larger; `juce::Timer` on the message thread to poll state for the UI.
Never call UI code from the audio thread.

## Realtime safety

In `processBlock` and anything it calls: allocate in `prepareToPlay`, not here. No
container growth, no `push_back`, no string building. Prefer fixed-size storage. If a
host hands you a bigger block than it promised, do less work — never grow a buffer.

## Verifying UI work

Render it rather than describing it. A throwaway test using
`createComponentSnapshot`, written to `/tmp` and read back, settles questions about
layout that reasoning does not. Delete the file afterwards. For a refactor that should
change nothing, snapshot before and after and compare the hashes — that turns "it
looks the same" into proof.

## Adding dependencies

JUCE modules go in `modules/` as submodules, then `add_subdirectory` and link to
`SharedCode`. Everything else goes through CPM, already configured:

```cmake
CPMAddPackage("gh:nlohmann/json@3.11.3")
```

## Code style

`.clang-format`: Allman braces, 4-space indent, no column limit.
