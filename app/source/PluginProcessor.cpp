#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace vf;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ViridianForgeProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto toStringArray = [] (const auto& names)
    {
        juce::StringArray a;
        for (auto* n : names) a.add (n);
        return a;
    };

    for (const auto& p : parameterSpecs)
    {
        const juce::ParameterID id { p.id, p.version };
        switch (p.kind)
        {
            case ParamKind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, p.name, p.defaultValue > 0.5f));
                break;

            case ParamKind::Choice:
            {
                juce::StringArray choices;
                const juce::String pid (p.id);
                if (pid == ParamID::channel)        choices = toStringArray (channelNames);
                else if (pid == ParamID::tubeSwap)  choices = toStringArray (tubeSwapNames);
                else if (pid == ParamID::cabModel)  choices = toStringArray (vf::dsp::cabModelNames);
                else if (pid == ParamID::micType)   choices = toStringArray (vf::dsp::micTypeNames);
                else if (pid == ParamID::ampSource) choices = toStringArray (ampSourceNames);
                else if (pid == ParamID::cabSource) choices = toStringArray (cabSourceNames);
                else if (pid == ParamID::ampModel)
                    for (const auto& m : vf::models::ampModels()) choices.add (m.name);
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, p.name, choices, static_cast<int> (p.defaultValue)));
                break;
            }

            case ParamKind::Float:
            default:
            {
                juce::NormalisableRange<float> range (p.min, p.max, p.step);
                if (juce::String (p.unit) == "Hz")
                    range.setSkewForCentre (std::sqrt (p.min * p.max)); // frequency knobs feel logarithmic
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, p.name, range, p.defaultValue,
                    juce::AudioParameterFloatAttributes().withLabel (p.unit)));
                break;
            }
        }
    }
    return layout;
}

//==============================================================================
ViridianForgeProcessor::ViridianForgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Guitar In", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ViridianForge", createLayout())
{
    auto get = [this] (const char* id)
    {
        auto* v = apvts.getRawParameterValue (id);
        jassert (v != nullptr);
        return v;
    };

    raw.inputGain = get (ParamID::inputGain);   raw.gateOn = get (ParamID::gateEnabled);   raw.gateThresh = get (ParamID::gateThreshold);
    raw.ampModel = get (ParamID::ampModel);     raw.channel = get (ParamID::channel);      raw.boost = get (ParamID::boost);
    raw.gain = get (ParamID::gain);             raw.bass = get (ParamID::bass);            raw.middle = get (ParamID::middle);
    raw.treble = get (ParamID::treble);         raw.presence = get (ParamID::presence);    raw.resonance = get (ParamID::resonance);
    raw.master = get (ParamID::master);         raw.bright = get (ParamID::bright);        raw.deep = get (ParamID::deep);
    raw.tubeSwap = get (ParamID::tubeSwap);     raw.cabOn = get (ParamID::cabEnabled);     raw.cabModel = get (ParamID::cabModel);
    raw.micPos = get (ParamID::micPosition);    raw.micDist = get (ParamID::micDistance);  raw.outLevel = get (ParamID::outputLevel);
    raw.ampSource = get (ParamID::ampSource);   raw.geqOn = get (ParamID::geqOn);
    for (size_t b = 0; b < 5; ++b) raw.geq[b] = get (ParamID::geqBands[b]);
    raw.cabSource = get (ParamID::cabSource);   raw.micType = get (ParamID::micType);      raw.irBlend = get (ParamID::irBlend);
    raw.irPhaseB = get (ParamID::irPhaseB);     raw.cabLowCut = get (ParamID::cabLowCut);  raw.cabHighCut = get (ParamID::cabHighCut);
    raw.room = get (ParamID::room);             raw.cabStereo = get (ParamID::cabStereo);

    if (! apvts.state.hasProperty ("theme"))
        apvts.state.setProperty ("theme", "dark", nullptr);
}

ViridianForgeProcessor::~ViridianForgeProcessor()
{
    loader.removeAllJobs (true, 10000);
}

bool ViridianForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

//==============================================================================
struct ViridianForgeProcessor::Settings
{
    vf::dsp::AmpSettings amp;
    bool nam = false;
    float namDriveDb = 0.0f, namLevelDb = 0.0f;
    vf::dsp::ToneShaperSettings tone;
    int cabSource = 0;
    bool irPhaseB = false, stereo = false;
};

void ViridianForgeProcessor::readSettings (Settings& s) const noexcept
{
    auto& a = s.amp;
    a.model = juce::jlimit (0, vf::models::numAmpModels() - 1, juce::roundToInt (raw.ampModel->load()));
    a.channel = juce::jlimit (0, 2, juce::roundToInt (raw.channel->load()));
    a.tubeSwap = juce::roundToInt (raw.tubeSwap->load()) - 1; // 0 = Stock -> -1
    a.gain = raw.gain->load();
    a.bass = raw.bass->load();
    a.middle = raw.middle->load();
    a.treble = raw.treble->load();
    a.presence = raw.presence->load();
    a.resonance = raw.resonance->load();
    a.master = raw.master->load();
    a.bright = raw.bright->load() > 0.5f;
    a.deep = raw.deep->load() > 0.5f;
    a.boost = raw.boost->load() > 0.5f;
    a.geqOn = raw.geqOn->load() > 0.5f;
    for (size_t b = 0; b < 5; ++b) a.geq[b] = raw.geq[b]->load();

    s.nam = isNamAvailable() && juce::roundToInt (raw.ampSource->load()) == 1;
    s.namDriveDb = -12.0f + 3.0f * a.gain;          // Gain 0..10 -> -12..+18 dB into the capture
    s.namLevelDb = -18.0f + 3.0f * a.master;        // Master 0..10 -> -18..+12 dB after it
    s.tone = { a.bass, a.middle, a.treble, a.presence, a.resonance, a.bright, a.deep };

    // An IR mode with no IR loaded falls back to the built-in cab.
    int src = juce::jlimit (0, 2, juce::roundToInt (raw.cabSource->load()));
    if (src >= 1 && ! irReady[0].load()) src = 0;
    if (src == 2 && ! irReady[1].load()) src = 1;
    s.cabSource = src;
    s.irPhaseB = raw.irPhaseB->load() > 0.5f;
    s.stereo = raw.cabStereo->load() > 0.5f;
}

void ViridianForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    maxChunk = juce::jmax (1, samplesPerBlock);
    work.setSize (5, maxChunk, false, true, false);

    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        1, oversamplingFactorLog2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampling->initProcessing (static_cast<size_t> (maxChunk));
    setLatencySamples (juce::roundToInt (oversampling->getLatencyInSamples()));

    Settings s;
    readSettings (s);
    amp.setSettings (s.amp); // configure directly (no switch fade on the first block)
    amp.prepare (sampleRate * (1 << oversamplingFactorLog2), maxChunk << oversamplingFactorLog2);

   #if VF_WITH_NAM
    nam.prepare (sampleRate, maxChunk);
   #endif
    namTone.prepare (sampleRate);
    namBoostLow.setCoeffs (vf::dsp::BiquadCoeffs::highPass (sampleRate, 720.0, 0.5));
    namBoostHigh.setCoeffs (vf::dsp::BiquadCoeffs::lowPass (sampleRate, 6000.0, 0.7));
    namDrive.reset (sampleRate, 0.03);
    namLevel.reset (sampleRate, 0.03);
    namDrive.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (s.namDriveDb));
    namLevel.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (s.namLevelDb));

    gate.prepare (sampleRate);
    tuner.prepare (sampleRate);
    inputGain.reset (sampleRate, 0.03);
    outputGain.reset (sampleRate, 0.03);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw.inputGain->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw.outLevel->load()));

    cabinet.prepare (sampleRate);
    const juce::dsp::ProcessSpec mono { sampleRate, static_cast<juce::uint32> (maxChunk), 1 };
    irA.prepare (mono);
    irB.prepare (mono);
    roomL.prepare (sampleRate);
    roomR.prepare (sampleRate);
    for (auto* b : { &lowCutL, &lowCutR, &highCutL, &highCutR }) b->reset();
    lastLowCut = lastHighCut = -1.0f;

    cabMix.reset (sampleRate, 0.02);
    cabMix.setCurrentAndTargetValue (raw.cabOn->load() > 0.5f ? 1.0f : 0.0f);
    irBlend.reset (sampleRate, 0.03);
    irBlend.setCurrentAndTargetValue (raw.irBlend->load() / 10.0f);
    roomAmount.reset (sampleRate, 0.05);
    roomAmount.setCurrentAndTargetValue (raw.room->load() / 10.0f * 0.8f);

    ampSwitch.reset (sampleRate, s.nam ? 1 : 0);
    cabSwitch.reset (sampleRate, s.cabSource);

    loadMeasurer.reset (sampleRate, samplesPerBlock);
}

void ViridianForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    if (numIn == 0 || numSamples == 0 || oversampling == nullptr)
    {
        buffer.clear();
        return;
    }

    Settings s;
    readSettings (s);
    amp.setSettings (s.amp);
    namTone.update (s.tone);
    namDrive.setTargetValue (juce::Decibels::decibelsToGain (s.namDriveDb));
    namLevel.setTargetValue (juce::Decibels::decibelsToGain (s.namLevelDb));
    ampSwitch.target = s.nam ? 1 : 0;
    cabSwitch.target = s.cabSource;

    gate.setEnabled (raw.gateOn->load() > 0.5f);
    gate.setThresholdDb (raw.gateThresh->load());
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (raw.inputGain->load()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (raw.outLevel->load()));

    cabinet.setParams (static_cast<vf::dsp::CabModel> (juce::jlimit (0, static_cast<int> (vf::dsp::CabModel::count) - 1, juce::roundToInt (raw.cabModel->load()))),
                       raw.micPos->load() / 10.0f, raw.micDist->load() / 10.0f,
                       static_cast<vf::dsp::MicType> (juce::jlimit (0, 2, juce::roundToInt (raw.micType->load()))));
    cabinet.update();
    cabMix.setTargetValue (raw.cabOn->load() > 0.5f ? 1.0f : 0.0f);
    irBlend.setTargetValue (raw.irBlend->load() / 10.0f);
    roomAmount.setTargetValue (raw.room->load() / 10.0f * 0.8f);

    const float lowCut = raw.cabLowCut->load(), highCut = raw.cabHighCut->load();
    if (! juce::approximatelyEqual (lowCut, lastLowCut))
    {
        lastLowCut = lowCut;
        const auto c = vf::dsp::BiquadCoeffs::highPass (currentRate, lowCut, 0.707);
        lowCutL.setCoeffs (c); lowCutR.setCoeffs (c);
    }
    if (! juce::approximatelyEqual (highCut, lastHighCut))
    {
        lastHighCut = highCut;
        const auto c = vf::dsp::BiquadCoeffs::lowPass (currentRate, juce::jmin (static_cast<double> (highCut), currentRate * 0.45), 0.707);
        highCutL.setCoeffs (c); highCutR.setCoeffs (c);
    }

    // Guitar is a mono source: process input channel 1 (the instrument input).
    const float* in = buffer.getReadPointer (0);
    float* outL = work.getWritePointer (3);
    float* outR = work.getWritePointer (4);
    for (int start = 0; start < numSamples; start += maxChunk)
    {
        const int n = juce::jmin (maxChunk, numSamples - start);
        processChunk (in + start, outL, outR, n, s);

        if (numOut >= 2)
        {
            buffer.copyFrom (0, start, outL, n);
            buffer.copyFrom (1, start, outR, n);
            for (int ch = 2; ch < numOut; ++ch) buffer.copyFrom (ch, start, outL, n);
        }
        else
        {
            float* o = buffer.getWritePointer (0, start);
            for (int i = 0; i < n; ++i) o[i] = 0.5f * (outL[i] + outR[i]);
        }
    }
}

void ViridianForgeProcessor::processChunk (const float* in, float* outL, float* outR, int numSamples, const Settings& s) noexcept
{
    float* mono = work.getWritePointer (0);

    // Front end: input trim, meter, tuner tap, gate
    float inPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = in[i] * inputGain.getNextValue();
        inPeak = juce::jmax (inPeak, std::abs (x));
        mono[i] = x;
    }
    tuner.pushSamples (mono, numSamples);
    for (int i = 0; i < numSamples; ++i)
        mono[i] = gate.process (mono[i]);
    gateOpen.store (gate.isOpen());

    // Amp source
    if (ampSwitch.active == 1)
    {
       #if VF_WITH_NAM
        for (int i = 0; i < numSamples; ++i)
        {
            float x = mono[i];
            if (s.amp.boost) x = namBoostHigh.process (namBoostLow.process (x)) * 3.5f + x * 0.3f;
            mono[i] = x * namDrive.getNextValue();
        }
        nam.process (mono, numSamples);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = namTone.process (mono[i]) * namLevel.getNextValue();
       #endif
    }
    else
    {
        float* channels[] = { mono };
        juce::dsp::AudioBlock<float> block (channels, 1, static_cast<size_t> (numSamples));
        auto upBlock = oversampling->processSamplesUp (block);
        amp.process (upBlock.getChannelPointer (0), static_cast<int> (upBlock.getNumSamples()));
        oversampling->processSamplesDown (block);
    }
    for (int i = 0; i < numSamples; ++i)
        mono[i] *= ampSwitch.next();

    // Cab section
    const int src = cabSwitch.active;
    float* a = work.getWritePointer (1);
    float* b = work.getWritePointer (2);
    if (src >= 1)
    {
        std::copy (mono, mono + numSamples, a);
        float* chA[] = { a };
        juce::dsp::AudioBlock<float> blockA (chA, 1, static_cast<size_t> (numSamples));
        irA.process (juce::dsp::ProcessContextReplacing<float> (blockA));
    }
    if (src == 2)
    {
        std::copy (mono, mono + numSamples, b);
        float* chB[] = { b };
        juce::dsp::AudioBlock<float> blockB (chB, 1, static_cast<size_t> (numSamples));
        irB.process (juce::dsp::ProcessContextReplacing<float> (blockB));
    }

    float outPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float dry = mono[i];
        float l, r;
        if (src == 0)
        {
            l = r = cabinet.process (dry);
        }
        else if (src == 1)
        {
            l = r = a[i];
        }
        else
        {
            const float bb = s.irPhaseB ? -b[i] : b[i];
            const float blend = irBlend.getNextValue();
            if (s.stereo) { l = a[i]; r = bb; }
            else          { l = r = a[i] * (1.0f - blend) + bb * blend; }
        }
        const float sw = cabSwitch.next();
        l *= sw; r *= sw;

        l = highCutL.process (lowCutL.process (l));
        r = highCutR.process (lowCutR.process (r));

        const float amt = roomAmount.getNextValue();
        float l1, r1, l2, r2;
        roomL.process (l, amt, l1, r1);
        roomR.process (r, amt, l2, r2);
        l = l1; r = r2;

        // Cab bypass: click-free crossfade to the raw amp output
        const float mix = cabMix.getNextValue();
        const float og = outputGain.getNextValue();
        outL[i] = (dry + mix * (l - dry)) * og;
        outR[i] = (dry + mix * (r - dry)) * og;
        outPeak = juce::jmax (outPeak, std::abs (outL[i]), std::abs (outR[i]));
    }

    auto storeMax = [] (std::atomic<float>& at, float v)
    {
        float prev = at.load (std::memory_order_relaxed);
        while (v > prev && ! at.compare_exchange_weak (prev, v, std::memory_order_relaxed)) {}
    };
    storeMax (inputPeak, inPeak);
    storeMax (outputPeak, outPeak);
}

//==============================================================================
juce::File ViridianForgeProcessor::getLibraryFolder (const juce::String& kind)
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Viridian Forge").getChildFile (kind);
    dir.createDirectory();
    return dir;
}

juce::File ViridianForgeProcessor::importIntoLibrary (const juce::File& source, const juce::String& kind)
{
    auto dest = getLibraryFolder (kind).getChildFile (source.getFileName());
    if (dest == source) return source;
    if (dest.existsAsFile()) dest = dest.getNonexistentSibling();
    return source.copyFileTo (dest) ? dest : source;
}

void ViridianForgeProcessor::loadNamModel (const juce::File& file)
{
   #if VF_WITH_NAM
    {
        const juce::ScopedLock sl (fileLock);
        namStatus = { LoadState::Loading, file.getFullPathName(), file.getFileNameWithoutExtension(), {} };
    }
    apvts.state.setProperty ("namPath", file.getFullPathName(), nullptr);
    const double rate = currentRate;
    const int block = maxChunk;
    loader.addJob ([this, path = file.getFullPathName().toStdString(), rate, block]
    {
        auto result = vf::nam::loadModel (path, rate, block);
        const juce::ScopedLock sl (fileLock);
        if (result.model == nullptr)
        {
            namStatus.state = LoadState::Failed;
            namStatus.error = result.error;
            return;
        }
        namStatus.state = LoadState::Ready;
        namStatus.description = result.info.describe();
        namReady.store (true);
        nam.setPending (std::move (result));
    });
   #else
    juce::ignoreUnused (file);
   #endif
}

void ViridianForgeProcessor::unloadNamModel()
{
   #if VF_WITH_NAM
    nam.unload();
   #endif
    const juce::ScopedLock sl (fileLock);
    namStatus = {};
    namReady.store (false);
    apvts.state.removeProperty ("namPath", nullptr);
}

ViridianForgeProcessor::FileSlotStatus ViridianForgeProcessor::getNamStatus() const
{
    const juce::ScopedLock sl (fileLock);
    return namStatus;
}

void ViridianForgeProcessor::loadImpulseResponse (int slot, const juce::File& file)
{
    slot = juce::jlimit (0, 1, slot);
    auto& conv = slot == 0 ? irA : irB;
    const char* key = slot == 0 ? "irA" : "irB";

    if (! file.existsAsFile())
    {
        const juce::ScopedLock sl (fileLock);
        irStatus[slot] = { LoadState::Failed, file.getFullPathName(), {}, "file not found" };
        return;
    }

    // Validate it is a readable audio file before handing it to the convolution engine.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 2)
    {
        const juce::ScopedLock sl (fileLock);
        irStatus[slot] = { LoadState::Failed, file.getFullPathName(), {}, "not a readable WAV/AIFF impulse response" };
        return;
    }

    const double lengthMs = 1000.0 * static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
    // JUCE's convolution loads, trims, normalises and resamples the IR on its own background thread.
    conv.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes, 0,
                              juce::dsp::Convolution::Normalise::yes);
    irReady[slot].store (true);
    apvts.state.setProperty (key, file.getFullPathName(), nullptr);

    const juce::ScopedLock sl (fileLock);
    irStatus[slot] = { LoadState::Ready, file.getFullPathName(),
                       file.getFileNameWithoutExtension() + juce::String::formatted ("  (%.0f ms, %.1f kHz)", lengthMs, reader->sampleRate / 1000.0), {} };
}

void ViridianForgeProcessor::clearImpulseResponse (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    irReady[slot].store (false); // the audio thread falls back immediately
    apvts.state.removeProperty (slot == 0 ? "irA" : "irB", nullptr);
    const juce::ScopedLock sl (fileLock);
    irStatus[slot] = {};
}

ViridianForgeProcessor::FileSlotStatus ViridianForgeProcessor::getIrStatus (int slot) const
{
    const juce::ScopedLock sl (fileLock);
    return irStatus[juce::jlimit (0, 1, slot)];
}

void ViridianForgeProcessor::housekeeping()
{
   #if VF_WITH_NAM
    nam.collectGarbage();
   #endif
}

void ViridianForgeProcessor::restoreFilesFromState()
{
    const auto namPath = apvts.state.getProperty ("namPath").toString();
    if (namPath.isNotEmpty()) loadNamModel (juce::File (namPath));
    for (int slot = 0; slot < 2; ++slot)
    {
        const auto path = apvts.state.getProperty (slot == 0 ? "irA" : "irB").toString();
        if (path.isNotEmpty()) loadImpulseResponse (slot, juce::File (path));
    }
}

//==============================================================================
void ViridianForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("formatVersion", 3, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ViridianForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

    if (! apvts.state.hasProperty ("theme"))
        apvts.state.setProperty ("theme", "dark", nullptr);

    restoreFilesFromState();
}

juce::AudioProcessorEditor* ViridianForgeProcessor::createEditor()
{
    return new ViridianForgeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ViridianForgeProcessor();
}
