# Changelog

All notable changes to SPACE are recorded here.

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)

## [Unreleased]

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
