// NAM loader as a chain block: one .nam capture, run mono on both channels, with a level knob.
#pragma once

#include <JuceHeader.h>
#include "vf/fx/Effect.h"

#if VF_WITH_NAM
#include "vf/nam/NamEngine.h"

#include <memory>
#include <string>
#include <vector>

class NamLoaderFx : public vf::fx::Effect
{
public:
    NamLoaderFx();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override {}
    void process (float* left, float* right, int numSamples) noexcept override;
    int latencySamples() const noexcept override { return engine.latencySamples(); }

    /** Loads a .nam file. Not real-time: call from the message thread or a loader thread.
        On failure the previously loaded model stays in place and `error` says why. */
    bool loadFile (const std::string& path, std::string& error);
    /** Removes the model (the block passes audio through). */
    void clear();

private:
    vf::nam::Engine engine;
    std::vector<float> mono;
    int pLevel = -1;
};

std::unique_ptr<vf::fx::Effect> makeNamLoader();
#endif
