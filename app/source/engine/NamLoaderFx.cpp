#include "NamLoaderFx.h"

#if VF_WITH_NAM
#include "vf/fx/Registry.h"

#include <algorithm>

NamLoaderFx::NamLoaderFx()
{
    pLevel = addParam (vf::fx::levelDb ("Level", 0.0f));
}

void NamLoaderFx::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    maxBlock = maxBlockSize;
    mono.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    engine.prepare (sampleRate, maxBlockSize);
}

void NamLoaderFx::process (float* l, float* r, int n) noexcept
{
    if (mono.empty()) return;
    const float g = juce::Decibels::decibelsToGain (v (pLevel));
    for (int start = 0; start < n; start += maxBlock)
    {
        const int m = std::min (maxBlock, n - start);
        for (int i = 0; i < m; ++i)
            mono[static_cast<size_t> (i)] = 0.5f * (l[start + i] + r[start + i]);
        engine.process (mono.data(), m);
        for (int i = 0; i < m; ++i)
            l[start + i] = r[start + i] = mono[static_cast<size_t> (i)] * g;
    }
}

bool NamLoaderFx::loadFile (const std::string& path, std::string& error)
{
    auto result = vf::nam::loadModel (path, fs, maxBlock);
    if (result.model == nullptr)
    {
        error = result.error;
        return false;
    }
    engine.setPending (std::move (result));
    return true;
}

void NamLoaderFx::clear()
{
    engine.unload();
}

std::unique_ptr<vf::fx::Effect> makeNamLoader()
{
    return std::make_unique<NamLoaderFx>();
}

// Registered at startup so it appears in the effect browser alongside the built-in types.
namespace
{
[[maybe_unused]] const bool namLoaderRegistered = []
{
    vf::fx::extraEffectTypes().push_back ({ "nam.loader", "NAM Loader", vf::fx::FxCategory::Utility,
                                            "loads a .nam capture into the chain", &makeNamLoader });
    return true;
}();
}
#endif
