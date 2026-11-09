#include "CabBlock.h"

CabBlock::CabBlock (juce::AudioProcessorValueTreeState& state, const juce::String& prefix, const juce::String& keyPrefix)
    : apvts (state)
{
    using namespace vf;
    irKey[0] = keyPrefix + "irA";
    irKey[1] = keyPrefix + "irB";
    auto get = [this, &prefix] (const char* id)
    {
        auto* v = apvts.getRawParameterValue (prefix + id);
        jassert (v != nullptr);
        return v;
    };
    raw.cabOn = get (ParamID::cabEnabled);     raw.cabModel = get (ParamID::cabModel);   raw.micPos = get (ParamID::micPosition);
    raw.micDist = get (ParamID::micDistance);  raw.cabSource = get (ParamID::cabSource); raw.micType = get (ParamID::micType);
    raw.irBlend = get (ParamID::irBlend);      raw.irPhaseB = get (ParamID::irPhaseB);   raw.cabLowCut = get (ParamID::cabLowCut);
    raw.cabHighCut = get (ParamID::cabHighCut); raw.room = get (ParamID::room);          raw.cabStereo = get (ParamID::cabStereo);
}

int CabBlock::sourceFromParams() const noexcept
{
    // An IR mode with no IR loaded falls back to the built-in cab.
    int src = juce::jlimit (0, 2, juce::roundToInt (raw.cabSource->load()));
    if (src >= 1 && ! irReady[0].load()) src = 0;
    if (src == 2 && ! irReady[1].load()) src = 1;
    return src;
}

void CabBlock::prepare (double sampleRate, int maxBlockSize)
{
    currentRate = sampleRate;
    fs = sampleRate;
    blockSize = juce::jmax (1, maxBlockSize);
    maxBlock = blockSize;
    work.setSize (3, blockSize, false, true, false);

    cabinet.prepare (sampleRate);
    const juce::dsp::ProcessSpec mono { sampleRate, static_cast<juce::uint32> (blockSize), 1 };
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
    cabSwitch.reset (sampleRate, sourceFromParams());
    prepared = true;
}

void CabBlock::reset()
{
    irA.reset();
    irB.reset();
    for (auto* b : { &lowCutL, &lowCutR, &highCutL, &highCutR }) b->reset();
    roomL.prepare (currentRate);
    roomR.prepare (currentRate);
}

void CabBlock::process (float* l, float* r, int n) noexcept
{
    if (! prepared) return;

    cabinet.setParams (static_cast<vf::dsp::CabModel> (juce::jlimit (0, static_cast<int> (vf::dsp::CabModel::count) - 1, juce::roundToInt (raw.cabModel->load()))),
                       raw.micPos->load() / 10.0f, raw.micDist->load() / 10.0f,
                       static_cast<vf::dsp::MicType> (juce::jlimit (0, 2, juce::roundToInt (raw.micType->load()))));
    cabinet.update();
    cabMix.setTargetValue (raw.cabOn->load() > 0.5f ? 1.0f : 0.0f);
    irBlend.setTargetValue (raw.irBlend->load() / 10.0f);
    roomAmount.setTargetValue (raw.room->load() / 10.0f * 0.8f);
    cabSwitch.target = sourceFromParams();
    phaseB = raw.irPhaseB->load() > 0.5f;
    stereo = raw.cabStereo->load() > 0.5f;

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

    for (int start = 0; start < n; start += blockSize)
        processChunk (l + start, r + start, juce::jmin (blockSize, n - start));
}

void CabBlock::processChunk (float* outL, float* outR, int n) noexcept
{
    float* mono = work.getWritePointer (0);
    for (int i = 0; i < n; ++i) mono[i] = 0.5f * (outL[i] + outR[i]);

    const int src = cabSwitch.active;
    float* a = work.getWritePointer (1);
    float* b = work.getWritePointer (2);
    if (src >= 1)
    {
        std::copy (mono, mono + n, a);
        float* chA[] = { a };
        juce::dsp::AudioBlock<float> blockA (chA, 1, static_cast<size_t> (n));
        irA.process (juce::dsp::ProcessContextReplacing<float> (blockA));
    }
    if (src == 2)
    {
        std::copy (mono, mono + n, b);
        float* chB[] = { b };
        juce::dsp::AudioBlock<float> blockB (chB, 1, static_cast<size_t> (n));
        irB.process (juce::dsp::ProcessContextReplacing<float> (blockB));
    }

    for (int i = 0; i < n; ++i)
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
            const float bb = phaseB ? -b[i] : b[i];
            const float blend = irBlend.getNextValue();
            if (stereo) { l = a[i]; r = bb; }
            else        { l = r = a[i] * (1.0f - blend) + bb * blend; }
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

        // Cab off: click-free crossfade to the raw (uncabbed) signal
        const float mix = cabMix.getNextValue();
        outL[i] = dry + mix * (l - dry);
        outR[i] = dry + mix * (r - dry);
    }
}

//==============================================================================
void CabBlock::loadImpulseResponse (int slot, const juce::File& file)
{
    slot = juce::jlimit (0, 1, slot);
    auto& conv = slot == 0 ? irA : irB;

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
    apvts.state.setProperty (irKey[slot], file.getFullPathName(), nullptr);

    const juce::ScopedLock sl (fileLock);
    irStatus[slot] = { LoadState::Ready, file.getFullPathName(),
                       file.getFileNameWithoutExtension() + juce::String::formatted ("  (%.0f ms, %.1f kHz)", lengthMs, reader->sampleRate / 1000.0), {} };
}

void CabBlock::clearImpulseResponse (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    irReady[slot].store (false); // the audio thread falls back immediately
    apvts.state.removeProperty (irKey[slot], nullptr);
    const juce::ScopedLock sl (fileLock);
    irStatus[slot] = {};
}

FileSlotStatus CabBlock::getIrStatus (int slot) const
{
    const juce::ScopedLock sl (fileLock);
    return irStatus[juce::jlimit (0, 1, slot)];
}

void CabBlock::restoreFromState()
{
    for (int slot = 0; slot < 2; ++slot)
    {
        const auto path = apvts.state.getProperty (irKey[slot]).toString();
        if (path.isNotEmpty()) loadImpulseResponse (slot, juce::File (path));
        else clearImpulseResponse (slot);
    }
}
