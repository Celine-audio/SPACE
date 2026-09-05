# Changelog

All notable changes to SPACE are recorded here.

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)

## [Unreleased]

### Added

- **A theming engine.** Every colour the interface draws with is editable at runtime,
  from **Theme…** in the settings menu, and can be written to and read from a `.celthm`
  file to be kept or shared. Changes show at once — the palette is what everything draws
  from, so there is no Apply to forget.
- The theme file is shared by every Céline plugin: one `theme.celthm` under the company
  folder, so theming one of them themes all of them. A key a build does not know is
  ignored and a key it knows but the file omits keeps its shipped value, which is what
  lets one file serve three plugins with different palettes.
- Plugin-specific colours are in the theme too, not just the chrome — though SPACE adds
  none of its own: everything this window draws already has a name in the shared kit. A
  palette that could not reach them could not re-skin the plugin.

### Changed

- Formats : **Fx|Reverb** to VST3, **lv2:ReverbPlugin** to LV2, **reverb** to
  CLAP, and **Reverb** to AAX.
- The CLAP build declares that it handles mono/stereo.
- `Theme`'s accessors are lookups rather than constants. The shipped values, the editor
  labels and the file keys are generated from one list (`ui/ThemeRoles.h`), so the enum,
  the table, the `.celthm` format and the editor's rows cannot drift apart.
- Every control that took its colours once in a constructor now gathers them into an
  `applyColours()` called from `lookAndFeelChanged()` as well, so a theme change reaches
  them. A snapshot does not follow a theme, and the failure is silent: half the window
  in the new colours and half in the old.
- `textDisabled` and `tabInactive` are roles of their own rather than aliases of
  `comment` and `chrome`. Shipping at the same value is not the same as being one
  colour, and a theme has to be able to pull them apart.

### Added

- Tooltips. Every control has one now.
- `ProductInfo::wordmarkAsset`, so the wordmark is named once. The editor was reaching
  for `"SPACE.svg"` as a literal while the About window asked `ProductInfo` — which is
  the split that ends with one of them drawing a file the other never had.

### Changed

- The shared house kit is now the same code as GALLERY's and AURA's..
- Dropdowns are drawn by the house look and feel rather than by JUCE.
- Text is Céline White (`F9FBFF`) throughout, where it had been the old warm off-white.
- Icon buttons are fill-only.
- Sliders take the house drag behaviour: the wheel does nothing, since a window this
  dense is one you scroll past and a wheel that edits whatever it is over is an edit
  nobody made; and the fine modifier drags finely rather than switching to JUCE's
  velocity mode.
- The About window's standardised.
- `Theme` loses `boxEnclose()` and `boxCrossing()`, two selection-box roles inherited
  from a drawing application that nothing here ever used.

### Fixed

- A shaping parameter moved by host automation no longer posts a message from the audio
  thread. `parameterChanged` runs on whichever thread moved the parameter, and
  `triggerAsyncUpdate` takes a lock and can allocate — on the one thread that must do
  neither. It sets a flag now, polled on the message thread. The rebuild was already
  deferred, so nothing about when it happens changes beyond the poll interval.

## [0.1.2]

Changes before this file existed are not recorded here. See the commit history.

[Unreleased]: https://github.com/Celine-audio/SPACE/commits/main
