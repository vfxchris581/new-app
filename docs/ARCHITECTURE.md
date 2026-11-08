# Viridian Forge — Architecture, Plan and Roadmap

Viridian Forge (by Viridian Audio Works) is a guitar amp and effects modeling
platform shipped as a standalone app and a VST3 plugin (plus AU on macOS) for
Windows, macOS and Linux.

This document answers the ten "first task" questions from the project brief.
It is honest about status: anything not marked **Done** is not built yet.

---

## 1. Requirements analysis

The brief is the scope of a commercial product built by a team over years
(Helix Native, Neural DSP and TONEX each represent large teams). It breaks into
seven subsystems:

| Subsystem | Size | Hardest part |
|---|---|---|
| Real-time engine (graph, oversampling, smoothing, state) | Medium | Click-free routing changes without locks |
| Amp modeling (100+ models) | Very large | Making 100 models *genuinely distinct* and good, not 100 EQ curves |
| NAM support | Medium | Real-time-safe model swap, resampling, CPU |
| Cabinets / IR | Medium | Shipping an IR library we have the rights to |
| Effects (100+) | Very large | Quality per effect (pitch shifting, reverbs) |
| Signal chain UI (drag/drop, parallel, stereo) | Large | Graph editing that maps to a lock-free engine |
| Presets, packaging, installers, signing | Medium | Notarisation, Windows signing, DAW compatibility |

**Duplicates removed from the supplied amp list** (counted once): Marshall 1959
Super Lead "Plexi" (British list and Marshall list), Marshall JCM800 2203 (both
lists), vintage Sunn/Ampeg entries overlapping with Sunn Model T and Ampeg V-4.
In the effects list, *Rotary* appears twice and *Noise gate* and *Tuner*
overlap with amp-level features; each counts once.

**Naming.** Real product names (Fender, Marshall, Mesa/Boogie, etc.) are
trademarks. Models ship with original names (e.g. "Forge 59 Lead"), and the
manual/browser can say "inspired by …" with a no-affiliation notice, which is
common industry practice. The Yngwie-inspired model is named descriptively
(e.g. "Neo-Classical Lead") with no use of his name or likeness.

## 2. Recommended architecture

```
          ┌─────────────────────── app/ (JUCE) ─────────────────────────┐
          │ Standalone wrapper    VST3 wrapper    AU wrapper (macOS)    │
          │            └──────────┬──────────┘                          │
          │                 PluginProcessor  ←→  PluginEditor (UI)      │
          │       (APVTS params, state, oversampling, meters, tuner)    │
          └──────────────────────────┬───────────────────────────────────┘
                                     │ plain C++ calls, no JUCE types
          ┌──────────────────────────▼──────── core/ (no JUCE) ─────────┐
          │ AmpVoice  ToneStack  TriodeStage  PowerStage  NoiseGate     │
          │ Tuner(YIN + SPSC FIFO)  Biquad  Smoother  Parameters  Brand  │
          │ Phase 2+: AmpModel registry, Graph engine, Effects, IR, NAM │
          └──────────────────────────────────────────────────────────────┘
```

Key decisions:

- **C++17 + JUCE 8 + CMake.** JUCE gives one codebase for Standalone, VST3 and
  AU on all three OSes, audio device I/O, and the GUI.
- **JUCE-free DSP core.** All DSP lives in `core/`, header-only, with no JUCE
  dependency. It builds and unit-tests anywhere in seconds, and could be reused
  in another host or a hardware port.
- **One parameter table** (`core/include/vf/Parameters.h`) feeds both the plugin
  parameter tree and the tests. IDs are versioned and never renamed, so old
  sessions always load.
- **Real-time rules** enforced in `processBlock`: no allocation, no file I/O,
  no locks. Data to the UI goes through atomics and a lock-free SPSC FIFO;
  heavy analysis (tuner YIN) runs on the message thread.
- **Phase 4 graph engine:** the signal chain becomes an immutable graph
  description built on a background thread and swapped into the audio thread
  with an atomic pointer exchange. Old graphs are released off the audio
  thread. This gives drag-and-drop, parallel paths and stereo without locks.

## 3. Amplifier modeling engine

**Done (Phase 1 reference voice, `core/include/vf/dsp/AmpVoice.h`):**

1. **Preamp:** 1–3 triode stages depending on channel. Each stage has a
   cathode-bypass emphasis filter, a biased asymmetric soft clipper (even
   harmonics, pick-dynamics), a DC blocker and a Miller-capacitance roll-off,
   so distortion is frequency-dependent.
2. **Tone stack:** the passive FMV network computed from component values
   (Yeh & Smith, DAFx 2006) and discretised with the bilinear transform, so
   bass/mid/treble interact like the real circuit. Four voicings: Tweed,
   Blackface, British, Modern.
3. **Power amp:** push-pull stage with seven tube profiles (EL34, 6L6, EL84,
   6550, KT88, 6V6, 5881), supply **sag** driven by output current, and
   presence/resonance modeled as the regions where negative feedback is reduced.
4. **Bright cap** whose effect fades as gain rises (as on the real circuit),
   **Deep** low shelf, mid-forward **Boost**, gate, built-in 4x12 response.
5. Runs at **4x oversampling** (JUCE polyphase IIR) to control aliasing.

Measured by the test suite: raising gain from 1 to 9 increases THD ~19x;
rolling the guitar input back cuts THD by ~3x (volume-knob cleanup); every
channel, tube type and switch produces an audible difference.

**Phase 2–3 plan (per-model circuits):**

- Replace static curves with stateful triode models (Koren-style plate curves
  solved per sample, with grid-conduction) and a coupling-cap bias-shift model
  for realistic blocking distortion and recovery.
- Add a **model registry**: each of the 100+ amps is a data description
  (stage count and topology, coupling/cathode component values, tone-stack
  type and values incl. Mesa-style post-gain stacks and 5-band graphic EQ,
  power section, NFB amount, rectifier/sag) evaluated by the same engine.
  "Genuinely distinct" is enforced by an automated test that renders every
  model with a reference DI and fails if any two are spectrally too close.
- Mesa Mark series gets its own topology (cascading lead stage, post-gain
  tone stack + 5-band GEQ, Simul-Class/Class A variants); Rectifier family gets
  diode vs tube rectifier sag modes.

## 4. NAM integration

- Use the official **NeuralAmpModelerCore** C++ library (MIT license) with
  Eigen (MPL-2.0) and nlohmann/json (MIT), added as git submodules.
- `.nam` files are JSON (an openly documented format): architecture, weights,
  expected sample rate and loudness metadata.
- **Loading:** file chooser → background thread parses the file and builds
  the model → the finished model is published to the audio thread with an
  atomic pointer swap and a short crossfade; the old model is destroyed on the
  background thread. The audio thread never parses or allocates.
- **Sample rate:** most captures expect 48 kHz. If the host runs at another
  rate, the NAM block is wrapped in a resampler (latency reported to the host).
- **Integration:** a NAM capture is just another "amp" block type in the
  graph, so it can sit in parallel with modeled amps, be saved in presets, and
  be browsed/tagged in a NAM library view (Phase 3).
- **Modular engine interface:** `IAmpEngine { prepare, process, latency,
  stateToJson }` — modeled amps, NAM and future engines (e.g. other open
  neural formats) all implement it.

## 5. VST3 and standalone architecture

- One `AudioProcessor` (`app/source/PluginProcessor.*`) shared by every
  format; JUCE generates the Standalone app, VST3 bundle and AU component.
- Bus layout: mono or stereo in, mono or stereo out. The guitar is taken from
  input 1 and the amp output is sent to all outputs (stereo effects in Phase 5).
- State: full parameter tree serialised to XML in `getStateInformation`
  (session recall in every DAW), with a format version for migrations.
- Latency (oversampling filters, later NAM resampling and lookahead) is
  reported to the host via `setLatencySamples`.
- Standalone: JUCE's standalone wrapper provides device selection (ASIO on
  Windows when built with the ASIO SDK, CoreAudio, ALSA/JACK). The app's
  **AUDIO** button opens device settings and the **LIVE INPUT** switch controls
  JUCE's feedback-protection mute (input is muted at first launch so laptop
  speakers don't howl).
- Plugin identifiers: manufacturer `Vrdn`, plugin `VFg1`, bundle ID
  `com.viridianaudioworks.forge`. These must never change after release.

## 6. Licensing and compatibility limitations

| Item | License / status | Consequence |
|---|---|---|
| JUCE 8 | AGPLv3 **or** commercial licence (free Starter tier below a revenue cap) | Closed-source/commercial release needs a JUCE commercial licence; an open-source release must be AGPLv3. **Your decision** — no LICENSE file is added until you choose. |
| VST3 SDK | MIT since VST 3.8 (Oct 2025) | Free to use; Steinberg's VST trademark/logo guidelines still apply. |
| AU | Apple SDK | macOS only; needs codesigning + notarisation to distribute. |
| ASIO (Windows) | Steinberg ASIO SDK (now GPL-compatible / proprietary dual) | Optional; needed for low-latency standalone on Windows. |
| NAM core | MIT | Fine for any use; keep notices. |
| User `.nam` models / IRs | Each creator's own terms | Users import their own; we only ship captures/IRs we made or licensed. |
| Helix / TONEX / Quad Cortex model files | Proprietary, undocumented | **Not supported.** No compatibility claims. |
| Amp/pedal/speaker/mic names | Trademarks of their owners | Original model names + "inspired by" + disclaimer (already on About screen). |
| Third-party plugin hosting | JUCE `AudioPluginFormatManager` | Possible later as a separate sandboxed feature. |

## 7. Main user interface

**Done (Phase 1):** resizable (fixed 16:10, 960×600 to 2560×1600), dark and
light themes saved with the session, and:

- **Top bar:** wordmark, preset slot (Phase 5), live-input switch (standalone),
  chromatic tuner with cents needle and in-tune LED, CPU and latency readouts,
  theme / audio settings / About, input and output peak meters with clip LEDs.
- **Amp head:** tolex, piping, grille with badge, brushed faceplate with
  screws; Clean/Crunch/Lead channel switches with LEDs; Boost, Bright, Deep;
  tone-stack voicing and power-tube selectors; seven metal knobs.
- **Input/Output panel:** input trim, gate on/off and threshold, output level.
- **Cabinet panel:** cab on/off and a 4x12 graphic (IR/mic controls in Phase 3).
- **Signal chain strip:** live IN → GATE → AMP → CAB → OUT blocks (click to
  bypass); empty dashed slots labelled *Phase 4* for the pedalboard.
- **Splash screen** (first open) and **About** screen with credits and notices.

**Planned layout additions:** amp browser by category (Phase 3), drag-and-drop
pedalboard with parallel lanes (Phase 4), preset browser with tags/favourites
(Phase 5).

## 8. Project directory structure

```
new-app/
├── CMakeLists.txt              top-level build (fetches JUCE 8.0.4 or uses -DJUCE_DIR)
├── core/                       JUCE-free DSP core (header-only)
│   ├── CMakeLists.txt
│   └── include/vf/
│       ├── Brand.h             names, version, palette
│       ├── Parameters.h        parameter IDs, ranges, defaults (single source of truth)
│       └── dsp/                Smoother, Biquad, ToneStack, TubeStage, NoiseGate, Tuner, AmpVoice
├── app/                        JUCE app + plugins
│   ├── CMakeLists.txt          juce_add_plugin (VST3, Standalone, AU on macOS)
│   └── source/
│       ├── PluginProcessor.*   audio engine host, state, meters, tuner feed
│       ├── PluginEditor.*      window, layout, themes, timers
│       └── ui/                 LookAndFeel, widgets (knobs, meters, tuner), panels
├── tests/                      core unit tests (CTest)
├── resources/branding/         icon (SVG + PNGs), wordmark, reference photo
├── scripts/make_branding.py    regenerates icon/wordmark
├── docs/                       this file, TESTING.md
└── .github/workflows/build.yml Windows / macOS / Linux CI builds + tests
```

Planned additions: `core/include/vf/models/` (amp registry + data),
`core/include/vf/fx/` (effects), `core/include/vf/graph/` (routing engine),
`third_party/` (NAM core, Eigen, json as submodules), `resources/irs/`,
`presets/factory/`, `installers/`.

## 9. Realistic roadmap

Effort estimates assume one experienced developer working with AI assistance;
quality bars (every model distinct, every effect production-grade) dominate.

| Phase | Scope | Exit criteria | Estimate |
|---|---|---|---|
| **1 — Shell** | Architecture, CMake, app/plugin shell, UI, branding, icon, reference amp voice, tuner, tests, CI | Builds on 3 OSes, tests pass, every visible control works | **Done** |
| 2 — Engine | Stateful triode/power models, model registry, ~12 clean/crunch/OD models, cab filters → minimum-phase IR convolution | Blind A/B notes from you on real guitar; latency < 3 ms extra | 3–5 weeks |
| 3 — Catalog | NAM loader, user IR loader (WAV) with dual mic blend, phase, HPF/LPF; amp catalog to 100+ incl. Mesa Mark + Marshall collections; amp browser | 100+ models pass the distinctness test; NAM + IR load without dropouts | 6–10 weeks |
| 4 — Effects | Effect framework, 100+ effects across 6 categories, graph engine, drag-and-drop pedalboard, parallel/split/merge | Each effect has tests; routing changes are click-free | 8–14 weeks |
| 5 — Presets & plugin | Preset system (tags, favourites, search, import/export), factory presets per amp family, automation polish, stereo routing | Sessions restore exactly in Reaper, Logic, Ableton, Cubase, LUNA | 3–5 weeks |
| 6 — Optimisation & QA | SIMD, CPU profiling, latency tests, pluginval, DAW matrix on 3 OSes | pluginval strictness 10 passes; CPU budget met | 3–4 weeks |
| 7 — Release | Installers (pkg/notarised, Inno Setup/MSI, Linux tarball/deb), docs, manual | Signed installers, manual, release notes | 2–3 weeks |

## 10. Phase 1 status

See the README for build instructions and `docs/TESTING.md` for the test
checklist. Verified in this phase:

- Core tests: 454 checks pass (filters, smoothing, passive tone stack, bounded
  output for all tubes, THD vs gain, volume-knob cleanup, distinct
  channels/tubes/switches, gate, tuner on standard and drop tunings, tuner
  safety before audio starts, parameter table sanity).
- The JUCE Standalone app and VST3 build with zero warnings in project code on
  Linux and the UI was launched and inspected. Windows and macOS builds are
  run by the GitHub Actions workflow on every push.
