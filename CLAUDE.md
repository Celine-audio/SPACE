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
| This plugin's own colours | `source/PluginThemeRoles.h` (the roles) and `source/PluginTheme.h` (the accessors) |
| Product facts | `source/ProductInfo.h` — tagline, repo URL, wordmark, copyright |

## Structure

- `source/` — processor, editor, parameters, product facts
- `source/dsp/` — `ImpulseResponse` (the file and its shaping), `SpectrumAnalyzer`
- `source/ui/` — the house kit (`Theme.h`, `Fonts`, `PluginLookAndFeel`, `AboutPanel`,
  `ParameterControl`, `IconButton`, `EmbeddedAssets`, and the theming engine —
  `Theme.h`, `ThemeRoles.h`, `ThemePalette`, `ThemePanel`) plus SPACE's own:
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

## Theming

Every colour is editable at runtime, from **Theme…** in the settings menu, and a theme
can be written to and read from a `.celthm` file to be shared.

The shape of it, in four files:

- **`ui/ThemeRoles.h`** — the list, as an X-macro. One entry carries four things that
  have to agree: the identifier the code uses, the label the editor shows, the group it
  is edited under, and the value the design ships with. The enum, the info table, the
  file's keys and the editor's rows are all generated from it. A plugin's own colours go
  in **`PluginThemeRoles.h`** beside it — GALLERY's four cabinets, AURA's three curves —
  and its accessors in **`PluginTheme.h`**, which `Theme.h` includes inside the
  namespace so `Theme::irSlot(2)` reads exactly like `Theme::chrome()`.
- **`ui/ThemePalette.h/.cpp`** — the colours in force, the `.celthm` reader and writer,
  and a `ChangeBroadcaster` so a change reaches every open window. One per process, in a
  function-local static: a palette at namespace scope could be read by a look and feel
  constructed before it.
- **`ui/Theme.h`** — the accessors, each a lookup, each documented with what it is *for*.
- **`ui/ThemePanel.h/.cpp`** — the editor. Live: a colour changed there reaches the
  window behind it on the next repaint, so there is no Apply to forget.

**Renaming a role breaks every theme anybody has saved**, because the identifier is the
key in the file. Adding one is free — an unknown key is ignored and a missing one keeps
its shipped value, which is what lets a theme written by a plugin with more colours than
this one still load.

**The theme file is shared by every Céline plugin** — one `theme.celthm` under the
company folder. Theming one of them themes all of them, which is the point of a house
look, and the ignore-unknown-keys rule is what makes one file serve three plugins with
different palettes.

Two things a theme change has to do, and both are easy to leave out:
`PluginLookAndFeel::applyPalette()` re-reads everything JUCE is *told* rather than asks
for, and `sendLookAndFeelChange()` gives every child a chance to do the same. The window
does both in its `changeListenerCallback`.

## House conventions

**Assets are looked up by filename, never by the BinaryData identifier.** JUCE derives
those identifiers by *stripping* characters rather than replacing them, so
`arrow-pointer-solid-full.svg` becomes `arrowpointersolidfull_svg`. Getting it wrong is
silent — the lookup returns null and nothing draws. Use `Celine::Assets::drawable("name.svg")`.
An asset that may legitimately be absent passes `IfMissing::returnNull`, or it will
assert on every launch in Debug.

**Colours come from `Theme`, never from a hex literal at the call site**, and every one
of them is a lookup rather than a constant: what they answer is whatever theme is in
force. Two consequences, both of which are silent when broken:

- **Read them at paint time.** A colour taken once in a constructor and handed to
  `setColour` is a snapshot, and a snapshot does not follow a theme change. Where a JUCE
  widget insists on being *told* its colours, gather them into an `applyColours()` and
  call it from both the constructor and an override of `lookAndFeelChanged()` — which is
  what the window calls on every child when the theme moves.
- **A new colour goes in `ui/ThemeRoles.h`** (or the plugin's own `PluginThemeRoles.h`),
  which is the one list the enum, the `.celthm` key, the editor's label and the shipped
  value are all generated from. `Theme.h` is where it is given a name and a reason.

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
