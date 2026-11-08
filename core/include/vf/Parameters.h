// Viridian Forge — shared parameter definitions.
// Single source of truth for parameter IDs, ranges and defaults, used by the
// JUCE plugin (to build its parameter tree) and by the core tests.
// IDs are part of saved sessions/presets: never rename or reuse an ID.
// `version` is the plugin version hint the parameter first appeared in.
#pragma once

#include "dsp/AmpVoice.h"
#include "dsp/Cabinet.h"
#include "models/AmpModels.h"
#include <array>

namespace vf
{

enum class ParamKind { Float, Bool, Choice };

struct ParamSpec
{
    const char* id;
    const char* name;
    ParamKind kind;
    float min, max, defaultValue, step;
    const char* unit;
    int version;
};

namespace ParamID
{
    inline constexpr const char* inputGain     = "inputGain";
    inline constexpr const char* gateEnabled   = "gateOn";
    inline constexpr const char* gateThreshold = "gateThresh";
    inline constexpr const char* ampModel      = "ampModel";
    inline constexpr const char* channel       = "channel";
    inline constexpr const char* boost         = "boost";
    inline constexpr const char* gain          = "gain";
    inline constexpr const char* bass          = "bass";
    inline constexpr const char* middle        = "middle";
    inline constexpr const char* treble        = "treble";
    inline constexpr const char* presence      = "presence";
    inline constexpr const char* resonance     = "resonance";
    inline constexpr const char* master        = "master";
    inline constexpr const char* bright        = "bright";
    inline constexpr const char* deep          = "deep";
    inline constexpr const char* tubeSwap      = "tubeSwap";
    inline constexpr const char* cabEnabled    = "cabOn";
    inline constexpr const char* cabModel      = "cabModel";
    inline constexpr const char* micPosition   = "micPos";
    inline constexpr const char* micDistance   = "micDist";
    inline constexpr const char* outputLevel   = "outLevel";
    // Phase 3
    inline constexpr const char* ampSource     = "ampSource";
    inline constexpr const char* geqOn         = "geqOn";
    inline constexpr const char* geq80         = "geq80";
    inline constexpr const char* geq240        = "geq240";
    inline constexpr const char* geq750        = "geq750";
    inline constexpr const char* geq2200       = "geq2200";
    inline constexpr const char* geq6600       = "geq6600";
    inline constexpr const char* cabSource     = "cabSource";
    inline constexpr const char* micType       = "micType";
    inline constexpr const char* irBlend       = "irBlend";
    inline constexpr const char* irPhaseB      = "irPhaseB";
    inline constexpr const char* cabLowCut     = "cabLowCut";
    inline constexpr const char* cabHighCut    = "cabHighCut";
    inline constexpr const char* room          = "room";
    inline constexpr const char* cabStereo     = "cabStereo";

    inline constexpr std::array<const char*, 5> geqBands { geq80, geq240, geq750, geq2200, geq6600 };
}

inline constexpr std::array<const char*, 3> channelNames   { "Channel 1", "Channel 2", "Channel 3" };
inline constexpr std::array<const char*, 8> tubeSwapNames  { "Stock", "EL34", "6L6", "EL84", "6550", "KT88", "6V6", "5881" };
inline constexpr std::array<const char*, 2> ampSourceNames { "Model", "NAM" };
inline constexpr std::array<const char*, 3> cabSourceNames { "Built-in", "IR A", "IR A + B" };

inline constexpr float lastAmpModel = static_cast<float> (models::kNumAmpModels) - 1.0f;
inline constexpr float lastCabModel = static_cast<float> (dsp::CabModel::count) - 1.0f;
inline constexpr float defaultAmpModel = static_cast<float> (models::AmpModelId::Plate59Lead);
inline constexpr float defaultCabModel = static_cast<float> (dsp::CabModel::Greenback4x12);

inline constexpr std::array<ParamSpec, 36> parameterSpecs {{
    { ParamID::inputGain,     "Input",          ParamKind::Float,  -24.0f, 24.0f,  0.0f,  0.1f, "dB", 1 },
    { ParamID::gateEnabled,   "Gate On",        ParamKind::Bool,     0.0f,  1.0f,  1.0f,  1.0f, "",   1 },
    { ParamID::gateThreshold, "Gate Threshold", ParamKind::Float,  -96.0f, -20.0f, -62.0f, 0.1f, "dB", 1 },
    { ParamID::ampModel,      "Amp Model",      ParamKind::Choice,   0.0f, lastAmpModel, defaultAmpModel, 1.0f, "", 2 },
    { ParamID::channel,       "Channel",        ParamKind::Choice,   0.0f,  2.0f,  0.0f,  1.0f, "",   1 },
    { ParamID::boost,         "Boost",          ParamKind::Bool,     0.0f,  1.0f,  0.0f,  1.0f, "",   1 },
    { ParamID::gain,          "Gain",           ParamKind::Float,    0.0f, 10.0f,  5.5f, 0.01f, "",   1 },
    { ParamID::bass,          "Bass",           ParamKind::Float,    0.0f, 10.0f,  5.0f, 0.01f, "",   1 },
    { ParamID::middle,        "Middle",         ParamKind::Float,    0.0f, 10.0f,  6.0f, 0.01f, "",   1 },
    { ParamID::treble,        "Treble",         ParamKind::Float,    0.0f, 10.0f,  6.0f, 0.01f, "",   1 },
    { ParamID::presence,      "Presence",       ParamKind::Float,    0.0f, 10.0f,  5.0f, 0.01f, "",   1 },
    { ParamID::resonance,     "Resonance",      ParamKind::Float,    0.0f, 10.0f,  4.0f, 0.01f, "",   1 },
    { ParamID::master,        "Master",         ParamKind::Float,    0.0f, 10.0f,  5.0f, 0.01f, "",   1 },
    { ParamID::bright,        "Bright",         ParamKind::Bool,     0.0f,  1.0f,  0.0f,  1.0f, "",   1 },
    { ParamID::deep,          "Deep",           ParamKind::Bool,     0.0f,  1.0f,  0.0f,  1.0f, "",   1 },
    { ParamID::tubeSwap,      "Power Tubes",    ParamKind::Choice,   0.0f,  7.0f,  0.0f,  1.0f, "",   2 },
    { ParamID::cabEnabled,    "Cab On",         ParamKind::Bool,     0.0f,  1.0f,  1.0f,  1.0f, "",   1 },
    { ParamID::cabModel,      "Cabinet",        ParamKind::Choice,   0.0f, lastCabModel, defaultCabModel, 1.0f, "", 2 },
    { ParamID::micPosition,   "Mic Position",   ParamKind::Float,    0.0f, 10.0f,  3.0f, 0.01f, "",   2 },
    { ParamID::micDistance,   "Mic Distance",   ParamKind::Float,    0.0f, 10.0f,  2.0f, 0.01f, "",   2 },
    { ParamID::outputLevel,   "Output",         ParamKind::Float,  -36.0f, 12.0f, -6.0f,  0.1f, "dB", 1 },
    { ParamID::ampSource,     "Amp Source",     ParamKind::Choice,   0.0f,  1.0f,  0.0f,  1.0f, "",   3 },
    { ParamID::geqOn,         "Graphic EQ On",  ParamKind::Bool,     0.0f,  1.0f,  1.0f,  1.0f, "",   3 },
    { ParamID::geq80,         "GEQ 80 Hz",      ParamKind::Float,  -12.0f, 12.0f,  0.0f,  0.1f, "dB", 3 },
    { ParamID::geq240,        "GEQ 240 Hz",     ParamKind::Float,  -12.0f, 12.0f,  0.0f,  0.1f, "dB", 3 },
    { ParamID::geq750,        "GEQ 750 Hz",     ParamKind::Float,  -12.0f, 12.0f,  0.0f,  0.1f, "dB", 3 },
    { ParamID::geq2200,       "GEQ 2.2 kHz",    ParamKind::Float,  -12.0f, 12.0f,  0.0f,  0.1f, "dB", 3 },
    { ParamID::geq6600,       "GEQ 6.6 kHz",    ParamKind::Float,  -12.0f, 12.0f,  0.0f,  0.1f, "dB", 3 },
    { ParamID::cabSource,     "Cab Source",     ParamKind::Choice,   0.0f,  2.0f,  0.0f,  1.0f, "",   3 },
    { ParamID::micType,       "Mic Type",       ParamKind::Choice,   0.0f,  2.0f,  0.0f,  1.0f, "",   3 },
    { ParamID::irBlend,       "IR Blend",       ParamKind::Float,    0.0f, 10.0f,  5.0f, 0.01f, "",   3 },
    { ParamID::irPhaseB,      "IR B Phase",     ParamKind::Bool,     0.0f,  1.0f,  0.0f,  1.0f, "",   3 },
    { ParamID::cabLowCut,     "Cab Low Cut",    ParamKind::Float,   20.0f, 400.0f, 40.0f, 1.0f, "Hz", 3 },
    { ParamID::cabHighCut,    "Cab High Cut",   ParamKind::Float, 2000.0f, 20000.0f, 12000.0f, 10.0f, "Hz", 3 },
    { ParamID::room,          "Room",           ParamKind::Float,    0.0f, 10.0f,  0.0f, 0.01f, "",   3 },
    { ParamID::cabStereo,     "Stereo",         ParamKind::Bool,     0.0f,  1.0f,  0.0f,  1.0f, "",   3 },
}};

} // namespace vf
