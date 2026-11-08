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
        const juce::ParameterID id { p.id, 1 };
        switch (p.kind)
        {
            case ParamKind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, p.name, p.defaultValue > 0.5f));
                break;

            case ParamKind::Choice:
            {
                juce::StringArray choices;
                if (juce::String (p.id) == ParamID::channel)        choices = toStringArray (channelNames);
                else if (juce::String (p.id) == ParamID::powerTube) choices = toStringArray (powerTubeNames);
                else if (juce::String (p.id) == ParamID::toneStack) choices = toStringArray (toneStackNames);
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, p.name, choices, static_cast<int> (p.defaultValue)));
                break;
            }

            case ParamKind::Float:
            default:
            {
                juce::NormalisableRange<float> range (p.min, p.max, p.step);
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

    raw.inputGain = get (ParamID::inputGain);   raw.gateOn = get (ParamID::gateEnabled); raw.gateThresh = get (ParamID::gateThreshold);
    raw.channel = get (ParamID::channel);       raw.boost = get (ParamID::boost);        raw.gain = get (ParamID::gain);
    raw.bass = get (ParamID::bass);             raw.middle = get (ParamID::middle);      raw.treble = get (ParamID::treble);
    raw.presence = get (ParamID::presence);     raw.resonance = get (ParamID::resonance); raw.master = get (ParamID::master);
    raw.bright = get (ParamID::bright);         raw.deep = get (ParamID::deep);          raw.powerTube = get (ParamID::powerTube);
    raw.toneStack = get (ParamID::toneStack);   raw.cabOn = get (ParamID::cabEnabled);   raw.outLevel = get (ParamID::outputLevel);

    if (! apvts.state.hasProperty ("theme"))
        apvts.state.setProperty ("theme", "dark", nullptr);
}

bool ViridianForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

void ViridianForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxChunk = juce::jmax (1, samplesPerBlock);
    monoBuffer.setSize (1, maxChunk, false, true, false);

    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        1, oversamplingFactorLog2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampling->initProcessing (static_cast<size_t> (maxChunk));
    setLatencySamples (juce::roundToInt (oversampling->getLatencyInSamples()));

    const double osRate = sampleRate * (1 << oversamplingFactorLog2);
    amp.prepare (osRate, maxChunk << oversamplingFactorLog2);
    amp.setSettings (readAmpSettings());
    gate.prepare (sampleRate);
    tuner.prepare (sampleRate);

    inputGain.reset (sampleRate, 0.03);
    outputGain.reset (sampleRate, 0.03);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw.inputGain->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw.outLevel->load()));

    loadMeasurer.reset (sampleRate, samplesPerBlock);
}

vf::dsp::AmpSettings ViridianForgeProcessor::readAmpSettings() const noexcept
{
    vf::dsp::AmpSettings s;
    s.gain = raw.gain->load();
    s.bass = raw.bass->load();
    s.middle = raw.middle->load();
    s.treble = raw.treble->load();
    s.presence = raw.presence->load();
    s.resonance = raw.resonance->load();
    s.master = raw.master->load();
    s.bright = raw.bright->load() > 0.5f;
    s.deep = raw.deep->load() > 0.5f;
    s.boost = raw.boost->load() > 0.5f;
    s.channel = static_cast<vf::dsp::AmpChannel> (juce::jlimit (0, 2, juce::roundToInt (raw.channel->load())));
    s.tube = static_cast<vf::dsp::PowerTube> (juce::jlimit (0, 6, juce::roundToInt (raw.powerTube->load())));
    s.toneStack = static_cast<vf::dsp::ToneStackVoicing> (juce::jlimit (0, 3, juce::roundToInt (raw.toneStack->load())));
    s.cabEnabled = raw.cabOn->load() > 0.5f;
    return s;
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

    amp.setSettings (readAmpSettings());
    gate.setEnabled (raw.gateOn->load() > 0.5f);
    gate.setThresholdDb (raw.gateThresh->load());
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (raw.inputGain->load()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (raw.outLevel->load()));

    // Guitar is a mono source: process input channel 1 (the instrument input),
    // then feed the result to every output channel.
    float* const out0 = buffer.getWritePointer (0);
    for (int start = 0; start < numSamples; start += maxChunk)
    {
        const int n = juce::jmin (maxChunk, numSamples - start);
        processMonoChunk (out0 + start, n);
    }

    for (int ch = 1; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);
}

void ViridianForgeProcessor::processMonoChunk (float* data, int numSamples) noexcept
{
    float* mono = monoBuffer.getWritePointer (0);

    float inPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = data[i] * inputGain.getNextValue();
        inPeak = juce::jmax (inPeak, std::abs (x));
        mono[i] = x;
    }
    tuner.pushSamples (mono, numSamples);

    for (int i = 0; i < numSamples; ++i)
        mono[i] = gate.process (mono[i]);
    gateOpen.store (gate.isOpen());

    float* channels[] = { mono };
    juce::dsp::AudioBlock<float> block (channels, 1, static_cast<size_t> (numSamples));
    auto upBlock = oversampling->processSamplesUp (block);
    amp.process (upBlock.getChannelPointer (0), static_cast<int> (upBlock.getNumSamples()));
    oversampling->processSamplesDown (block);

    float outPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float y = mono[i] * outputGain.getNextValue();
        outPeak = juce::jmax (outPeak, std::abs (y));
        data[i] = y;
    }

    // Peak-hold handoff to the UI (UI resets with exchange()).
    auto storeMax = [] (std::atomic<float>& a, float v)
    {
        float prev = a.load (std::memory_order_relaxed);
        while (v > prev && ! a.compare_exchange_weak (prev, v, std::memory_order_relaxed)) {}
    };
    storeMax (inputPeak, inPeak);
    storeMax (outputPeak, outPeak);
}

//==============================================================================
void ViridianForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("formatVersion", 1, nullptr);
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
}

juce::AudioProcessorEditor* ViridianForgeProcessor::createEditor()
{
    return new ViridianForgeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ViridianForgeProcessor();
}
