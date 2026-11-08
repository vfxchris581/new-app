# Testing

## Automated

```bash
cmake -S . -B build -DVF_BUILD_PLUGIN=OFF   # core only, no JUCE download needed
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite (`tests/core_tests.cpp`, 736 checks) covers every amp model and
cabinet: passive tone stacks, 12AX7 operating points, grid bias shift and
recovery, bounded output at extreme settings, distortion rising with gain,
clean models staying clean, volume-knob cleanup, level matching, pairwise
distinctness of all models/channels, every control audible, click-free
model switching, cabinet and mic behaviour, gate, tuner and parameters.
CI runs it on Windows, macOS and Linux.

Plugin validation (from Phase 6, run locally now if you like):
[pluginval](https://github.com/Tracktion/pluginval) at strictness 5+ against
the built `Viridian Forge.vst3`.

## Manual checklist (Phase 2)

Use headphones for the standalone app.

**Standalone**
- [ ] App launches, splash shows ~2 s then fades (click dismisses it early).
- [ ] AUDIO opens device settings; pick your interface, 48 kHz, 64–256 samples.
- [ ] INPUT MUTED → click → LIVE INPUT; guitar is audible.
- [ ] IN meter moves with picking; OUT meter moves; clip LED latches on overload and clears on click.
- [ ] Tuner shows the right note and cents for each open string; LED lights green within ±3 cents.
- [ ] CPU and latency readouts show plausible values.

**Amp models**
- [ ] AMP menu lists models grouped by category; hovering shows what inspired each one.
- [ ] Switching models while playing is click-free and levels stay roughly matched.
- [ ] Channel buttons relabel per model; unavailable channels are greyed out.
- [ ] Canyon 6V6 Clean breaks up only near full gain; Canyon 100 and Glass Clean 120 stay clean.
- [ ] Velvet OD Overdrive: GAIN acts as the drive control, smooth sustain.
- [ ] Hard picking compresses and blooms; light picking is cleaner (touch sensitivity).
- [ ] POWER TUBES: Stock and each swap sound different.

**Amp controls**
- [ ] GAIN sweeps clean → saturated; rolling back guitar volume cleans up.
- [ ] BASS, MIDDLE, TREBLE each change tone; turning MIDDLE down also shifts bass/treble (interactive stack).
- [ ] PRESENCE adds top-end bite; RESONANCE adds low-end thump.
- [ ] MASTER low = tight, high = power-amp breakup and compression (sag).
- [ ] BOOST tightens lows and adds drive; BRIGHT adds sparkle mostly at low gain; DEEP adds low end.
- [ ] GATE switch + THRESHOLD silence hiss between notes without chopping sustain.
- [ ] CAB switch off gives the raw (fizzy) amp output for use with external IRs; switching is click-free.
- [ ] Each CABINET sounds different; the graphic shows the right speaker count.
- [ ] MIC POS toward 10 darkens the tone; DISTANCE toward 0 adds low end.
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
