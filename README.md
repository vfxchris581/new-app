<p align="center"><img src="resources/branding/icon_256.png" width="128" alt="Viridian Forge icon"></p>

# Viridian Forge

**Guitar amp and effects modeling — standalone app and VST3/AU plugin for Windows, macOS and Linux.**
By Viridian Audio Works.

![Viridian Forge Phase 1 interface](docs/images/ui-phase1.png)

> **Status: Phase 1 of 7.** The app, plugin, interface, branding and a working
> reference amp voice are built. The 100+ amp catalog, NAM, IR loading, the
> effects library and presets are planned for later phases and are not
> implemented yet. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full plan.

## What works today

- Standalone app (with audio device settings) and VST3 plugin; AU on macOS.
- Reference amp voice with real DSP behind every control: 3 channels, gain, circuit-derived
  passive tone stack (4 voicings), presence, resonance, master with power-tube breakup and
  supply sag, 7 power-tube types, bright/deep/boost, noise gate, built-in 4x12 cab response,
  4x oversampling.
- Chromatic tuner, input/output meters with clip LEDs, CPU and latency readouts.
- Resizable UI with dark and light themes, splash and About screens.
- Full session save/restore and automation of every parameter.
- 454 automated DSP checks, run on all three OSes by CI.

## Build

Requirements on every OS: **CMake 3.22+**, **Git**, and a C++17 compiler. The first configure
downloads JUCE 8.0.4 automatically (or point to your own copy with `-DJUCE_DIR=/path/to/JUCE`).

### macOS (Xcode command-line tools)

```bash
xcode-select --install          # once
git clone https://github.com/vfxchris581/new-app.git && cd new-app
cmake -S . -B build -G Xcode    # or -G Ninja
cmake --build build --config Release
ctest --test-dir build -C Release
```

Results in `build/app/ViridianForge_artefacts/Release/`:
- `Standalone/Viridian Forge.app` → run it directly, or copy to `/Applications`
- `VST3/Viridian Forge.vst3` → copy to `~/Library/Audio/Plug-Ins/VST3/`
- `AU/Viridian Forge.component` → copy to `~/Library/Audio/Plug-Ins/Components/`

Builds are universal (Apple Silicon + Intel). Local builds are unsigned: if macOS blocks the app,
right-click it and choose **Open** once.

### Windows (Visual Studio 2022, "Desktop development with C++")

```powershell
git clone https://github.com/vfxchris581/new-app.git
cd new-app
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

Results in `build\app\ViridianForge_artefacts\Release\`:
- `Standalone\Viridian Forge.exe`
- `VST3\Viridian Forge.vst3` → copy to `C:\Program Files\Common Files\VST3\`

For lowest standalone latency on Windows, use your interface's ASIO driver (building JUCE with
ASIO support requires the Steinberg ASIO SDK; WASAPI exclusive mode works without it).

### Linux (Ubuntu/Debian)

```bash
sudo apt install build-essential cmake ninja-build git libasound2-dev libjack-jackd2-dev \
  libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev
git clone https://github.com/vfxchris581/new-app.git && cd new-app
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Results in `build/app/ViridianForge_artefacts/Release/`:
- `Standalone/Viridian Forge` → run it
- `VST3/Viridian Forge.vst3` → copy to `~/.vst3/`

## Running the standalone app

1. Use headphones. Plug your guitar into input 1 of your audio interface.
2. Click **AUDIO** to choose the interface, sample rate (48 kHz is a good default) and a buffer of
   64–256 samples.
3. Click **INPUT MUTED** to switch to **LIVE INPUT** (input starts muted to prevent speaker feedback).
4. Tune with the tuner in the top bar, then play.

Building only the DSP core and tests (no JUCE download): `cmake -S . -B build -DVF_BUILD_PLUGIN=OFF`.

## Project layout

| Path | Contents |
|---|---|
| `core/` | JUCE-free DSP core (amp voice, tone stack, tubes, gate, tuner, parameters) |
| `app/` | JUCE processor, editor and UI components |
| `tests/` | Core unit tests (CTest) |
| `resources/branding/` | Icon (SVG + PNG), wordmark, reference guitar photo |
| `scripts/make_branding.py` | Regenerates the icon and wordmark |
| `docs/` | Architecture & roadmap, testing checklist |

## Licensing note

No license has been chosen yet. JUCE 8 is available under AGPLv3 or a commercial licence, so the
license for this project depends on how you plan to release it (see
[docs/ARCHITECTURE.md §6](docs/ARCHITECTURE.md#6-licensing-and-compatibility-limitations)).

Amp, pedal, speaker and microphone names mentioned in the docs are trademarks of their owners and
are used only to describe the tones that inspired original models. No affiliation or endorsement
is implied. VST is a registered trademark of Steinberg Media Technologies GmbH.
