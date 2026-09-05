<img alt="SPACE Logo" src="/assets/SPACE.svg" title="SPACE Logo" width="250"/>

SPACE is a convolution reverb where you can load reverb impulse responses. It offers basic functionality and a post-effect EQ for the wet signal.

<img alt="SPACE Interface" src="/docs/screenshots/interface.png" title="SPACE Interface" width="1000"/>

---

## Formats

Built as **VST3®**, **AU** (macOS), **LV2** and **CLAP**, on Windows, macOS and Linux.

Nothing is code-signed, so Gatekeeper and SmartScreen will warn on first run.

<p>
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/VST.png">
    <img alt="VST Compatible. VST is a registered trademark of Steinberg Media Technologies GmbH" src="docs/logos/VST_2.png" height="78">
  </picture>
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/AU-onwhite.svg">
    <img alt="Audio Units" src="docs/logos/AU.svg" height="78">
  </picture>
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/CLAP-white.png">
    <img alt="CLAP" src="docs/logos/CLAP.svg" height="70">
  </picture>
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/lv2_white.svg">
    <img alt="LV2" src="docs/logos/lv2_black.svg" height="52">
  </picture>
</p>

---

## Built on

[JUCE](https://juce.com) 9, with the build system derived from
[Pamplejuce](https://github.com/sudara/pamplejuce). CLAP support comes from
[clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions).

---

## Building

Needs **CMake 3.25** or newer and a **C++23** compiler.

The submodules are not optional — JUCE, the shared CMake modules and the CLAP
wrapper all live in them, and one has submodules of its own:

```bash
git clone --recursive https://github.com/Celine-audio/SPACE.git
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

Then:

```bash
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build Builds
```

On macOS, `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` builds a universal binary — keep
the quotes, or the shell eats the semicolon and you get one architecture.

To run the tests:

```bash
ctest --test-dir Builds --output-on-failure
```

---

## Disclaimer

This software is provided "as is", without warranty of any kind. No liability can be
claimed for any harm or damage caused by its use.

---

## Licence and credits

SPACE being free open-source software using the [JUCE](https://juce.com)
framework, and using its free licence, it inherits its AGPLv3 terms. SPACE is then
under the [GNU AGPL v3](COPYING) licence. The full notices are in
[`LICENSE`](LICENSE) and [`THIRD-PARTY-NOTICES`](THIRD-PARTY-NOTICES), and the same
summary is available within the plugin under **Settings → About**.

<p>
  <img alt="Licensed under the GNU AGPL v3" src="docs/logos/AGPLv3.svg" height="62">
</p>

### What that means in practice

Using SPACE costs nothing and obliges nothing. The licence governs the distribution
*of the software*, not what you make with it. You may fork and modify it, provided you
do so under the AGPLv3 licence and pass the source on.

### Credits

- Build system derived from [Pamplejuce](https://github.com/sudara/pamplejuce),
  © 2022 Sudara Williams, MIT
- Icons from [Font Awesome Free](https://fontawesome.com), © Fonticons, Inc., used
  under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
- Typefaces [Jura](https://github.com/ossobuffo/jura),
  [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono) and
  [Nico Moji](https://fonts.google.com/specimen/Nico+Moji), all under the SIL OFL 1.1
- VST® is a registered trademark of Steinberg Media Technologies GmbH

### AI disclosure

SPACE contains no AI whatsoever. However, AI assistants were used alongside the authors during development; SPACE remains the authors' work.