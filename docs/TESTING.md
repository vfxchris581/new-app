# Testing

## Automated

```bash
cmake -S . -B build -DVF_BUILD_PLUGIN=OFF   # core only, no JUCE download needed
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite (`tests/core_tests.cpp`) covers filter accuracy, smoothing, the
passive tone stack (never above unity, each knob acts on its band, for all four
voicings), bounded/finite output for every power tube at extreme settings,
distortion rising with gain, volume-knob cleanup, every channel/tube/switch
producing an audible difference, noise gate behaviour, tuner accuracy within
2 cents on standard and drop tunings, tuner safety before audio starts, and
parameter-table sanity. CI runs it on Windows, macOS and Linux.

Plugin validation (from Phase 6, run locally now if you like):
[pluginval](https://github.com/Tracktion/pluginval) at strictness 5+ against
the built `Viridian Forge.vst3`.

## Manual checklist (Phase 1)

Use headphones for the standalone app.

**Standalone**
- [ ] App launches, splash shows ~2 s then fades (click dismisses it early).
- [ ] AUDIO opens device settings; pick your interface, 48 kHz, 64–256 samples.
- [ ] INPUT MUTED → click → LIVE INPUT; guitar is audible.
- [ ] IN meter moves with picking; OUT meter moves; clip LED latches on overload and clears on click.
- [ ] Tuner shows the right note and cents for each open string; LED lights green within ±3 cents.
- [ ] CPU and latency readouts show plausible values.

**Amp**
- [ ] CLEAN / CRUNCH / LEAD switch audibly and LEDs follow.
- [ ] GAIN sweeps clean → saturated; rolling back guitar volume cleans up.
- [ ] BASS, MIDDLE, TREBLE each change tone; turning MIDDLE down also shifts bass/treble (interactive stack).
- [ ] PRESENCE adds top-end bite; RESONANCE adds low-end thump.
- [ ] MASTER low = tight, high = power-amp breakup and compression (sag).
- [ ] BOOST tightens lows and adds drive; BRIGHT adds sparkle mostly at low gain; DEEP adds low end.
- [ ] Each VOICING and POWER TUBES choice sounds different.
- [ ] GATE switch + THRESHOLD silence hiss between notes without chopping sustain.
- [ ] CAB switch off gives the raw (fizzy) amp output for use with external IRs.
- [ ] Double-click any knob resets it to default.

**UI**
- [ ] Window resizes smoothly with the corner handle, keeps proportions.
- [ ] LIGHT/DARK theme toggles and is remembered after reopening a session.
- [ ] ABOUT shows version and notices; CLOSE or clicking outside dismisses it.
- [ ] Signal chain GATE and CAB blocks toggle bypass and stay in sync with the switches.

**Plugin (VST3/AU)**
- [ ] Scans in your DAW (e.g. LUNA, Reaper) on a mono or stereo guitar track.
- [ ] Automate GAIN and MASTER; automation plays back smoothly with no clicks.
- [ ] Save the session, close, reopen: all settings and the theme are restored.
- [ ] Changing buffer size or sample rate (44.1–192 kHz) keeps audio clean.
