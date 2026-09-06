# Changelog

All notable changes to SPACE are recorded here.

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)

## [Unreleased]

### Added

- Closing the theme editor with colours you have not saved now asks, offering **Save**,
  **Discard** or **Cancel**. Every way out goes through it — the Close button, the escape
  key and the title bar's own close button.
- `tests/ThemeReachTests.cpp`, which renders the whole editor, moves every colour the
  theme has, renders it again, and fails if anything the design ships is still on screen.
  It found four real bugs the day it was first run across all four plugins.
- **A theme is now this plugin's own**, in `<name>.celthm` under the company folder
  rather than one file shared by the house. Every instance of it on the machine wears
  the same colours whatever host or format it is loaded as, and an existing shared theme
  is inherited on first run so nothing is lost by the split. Themes stay cross-
  compatible: one exported from another Céline plugin still loads, and the colours this
  one does not have are simply skipped.
- **Button backgrounds and text fields are separate colours in the theme.** They shipped
  as one — every button wore the same slate as every panel — so a theme could not lift
  the controls off the surfaces they sit on. Two new roles, **Button** and **Text
  field**, ship at exactly the values they replace, so nothing looks different until
  somebody moves them.
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

- The look and feel is split: `ui/LookAndFeelBase` carries everything the four plugins
  draw the same way, and `ui/PluginLookAndFeel` is a subclass for what this one does
  differently. Fifteen files under `source/ui/` are now byte-identical across all four,
  which is what makes the shared kit a move rather than a merge — see `CELINEUI.md`.
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

- **The caret in a value box was invisible on the light strip.** It was the last thing
  in the box still taking its colour from the look and feel, which sets it for the dark
  half of the design -- so on the near-white panel the text cursor was white on white.
  It takes the row's own ink now, like the text and the selection around it.
- **The value text on the light strip turned white while you edited it.** Not the text
  colour, as it looked: opening the editor selects the whole value, so what you see the
  instant you click in is `highlightedTextColourId` -- which the look and feel sets for
  the dark half of the design, because it has no way of knowing a particular row stands
  on the other one. The selection now takes the row's own ink, with a wash of that ink
  behind it, so it reads on either side of the split.
- **The header came up empty until the window was resized.** `setSize` fires `resized()`,
  which measures the logo and the wordmark to lay the header out — and those had moved
  into `applyColours`, which ran afterwards. So the first layout saw no artwork, placed
  nothing, and the header stayed blank until something else resized the window. `setSize`
  goes last in the constructor again, which is the house rule and exactly this reason.
- **Clicking into a value box no longer draws a border round it.** The slider's text box
  asked for one in the armed colour while it was being edited -- the last rule left
  anywhere in the window, and one that appeared on a click, which is exactly what made it
  read as a system control dropped into the design.
- The digits you type into a value box are the theme's ink. JUCE fills
  `textWhenEditingColourId` from its own colour scheme rather than leaving it unset, so
  the text being edited was never taking its colour from the theme.
- **Discarding a theme now puts the colours back.** It marked the change abandoned and
  left it on screen, so "discard" only meant "do not write the file" -- the window behind
  it kept the colours you had just rejected until something else reloaded the theme.
- **The theme window no longer opens behind the plugin.** Building it by hand to
  intercept every way of closing it lost the two things `DialogWindow::LaunchOptions`
  does for you: it is on top when the host keeps its own windows on top -- Ableton does,
  and the window was unreachable without closing the plugin -- and it is told the scale
  the editor is being shown at.
- The plugin name no longer sits behind its own wordmark. Loading the artwork moved into
  `applyColours` and the fallback text's visibility was left behind in the constructor,
  where the wordmark had not been read yet.
- The post-EQ's cut lines are grey at rest and the accent when pointed at, matching the
  response display's trim grips and AURA's band edges. They wore the accent in both
  states, so a line you were not touching looked like one you were.
- **The yellow ring around whatever you were editing is gone.** JUCE draws a focus
  outline as a separate desktop window, and its default is a rounded rectangle at a fixed
  radius of three — so on a field rounded to the house radius it traced a shape the
  control does not have, sitting slightly off its corners. It also lived only as long as
  that window did, which is why it appeared on one launch and not the next. Nothing here
  needs it: a field being edited says so with its caret and its selection.
- **The theme editor's own Close button follows the accent it is showing you.** Its
  colours were set once when the window opened, so picking a new accent recoloured every
  other control in the plugin and left the button next to the swatch on the old one. The
  window's title, subtitle and status line had the same fault.
- **The toolbar's mark did not follow the theme.** The logo and the wordmark were tinted
  once when the window opened, and tinting is destructive — so they stayed on whatever
  colour the theme happened to be at that moment.
- **The About window did not follow the theme at all.** It is a window of its own, so the
  editor's `sendLookAndFeelChange` never reached it; it now listens to the palette
  directly, and its marks are re-read from the binary rather than re-tinted.
- The bypass button is red, as it is in every other Céline plugin. It was left wearing
  the armed colour every other in-force control uses, which said "this is on" where the
  others say "this is passing your audio through untouched".
- **Group headings no longer escape the colour list.** They are painted by the panel in
  the scrolled list's coordinates, and nothing clipped them — so a heading scrolled past
  the top carried on being drawn above the list, over the subtitle and the footer. It
  showed up as headings appearing in the middle of the window whenever something made
  the panel repaint underneath the colour picker.
- The colour picker no longer paints a square panel inside a rounded bubble. It filled
  its own background, which met the bubble's rounded corners and lost the argument.
- **Theming one instance now reaches the others.** Each plugin format is a separately
  loaded module with its own copy of everything static, so the VST3 and the AU open in
  one session were two palettes that never met — theming one left the other on the old
  colours until it was reloaded. A window reads the saved theme when it opens, which is
  the moment it can matter; nothing watches the disk in the background. Colours you are
  in the middle of choosing are never overwritten by what another instance saved.
- Building a palette no longer schedules a save of the file it has just read. Reading
  any colour builds it, and the first read can come from a static initialiser — before
  there is a message loop for the save to wait on, which JUCE asserts about.
- **A theme you pick is kept — when you press Save.** It used to be live until you
  closed the plugin and then gone, because nothing wrote it. There is now a **Save**
  button in the theme editor, lit only while there is something to keep, and the status
  line says whether there is. Editing itself touches nothing: a colour picker sends a
  change per mouse move, and a preference is not worth a file per mouse move.
- Text fields no longer draw a ring when you click into them. The caret already says
  where the typing goes, and it was the one edge in the window that arrived on a click.
- A shaping parameter moved by host automation no longer posts a message from the audio
  thread. `parameterChanged` runs on whichever thread moved the parameter, and
  `triggerAsyncUpdate` takes a lock and can allocate — on the one thread that must do
  neither. It sets a flag now, polled on the message thread. The rebuild was already
  deferred, so nothing about when it happens changes beyond the poll interval.

## [0.1.2]

Changes before this file existed are not recorded here. See the commit history.

[Unreleased]: https://github.com/Celine-audio/SPACE/commits/main
