// Viridian Forge — shared parameter definitions.
// Single source of truth for parameter IDs, ranges and defaults, used by the
// JUCE plugin (to build its parameter tree) and by the core tests.
// IDs are part of saved sessions/presets: never rename or reuse an ID.
#pragma once

#include "dsp/AmpVoice.h"
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
};

namespace ParamID
{
    inline constexpr const char* inputGain     = "inputGain";
    inline constexpr const char* gateEnabled   = "gateOn";
    inline constexpr const char* gateThreshold = "gateThresh";
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
    inline constexpr const char* powerTube     = "powerTube";
    inline constexpr const char* toneStack     = "toneStack";
    inline constexpr const char* cabEnabled    = "cabOn";
    inline constexpr const char* outputLevel   = "outLevel";
}

inline constexpr std::array<const char*, 3> channelNames   { "Clean", "Crunch", "Lead" };
inline constexpr std::array<const char*, 7> powerTubeNames { "EL34", "6L6", "EL84", "6550", "KT88", "6V6", "5881" };
inline constexpr std::array<const char*, 4> toneStackNames { "Tweed", "Blackface", "British", "Modern" };

inline constexpr std::array<ParamSpec, 18> parameterSpecs {{
    { ParamID::inputGain,     "Input",          ParamKind::Float,  -24.0f, 24.0f,  0.0f,  0.1f, "dB" },
    { ParamID::gateEnabled,   "Gate On",        ParamKind::Bool,     0.0f,  1.0f,  1.0f,  1.0f, ""   },
    { ParamID::gateThreshold, "Gate Threshold", ParamKind::Float,  -96.0f, -20.0f, -62.0f, 0.1f, "dB" },
    { ParamID::channel,       "Channel",        ParamKind::Choice,   0.0f,  2.0f,  1.0f,  1.0f, ""   },
    { ParamID::boost,         "Boost",          ParamKind::Bool,     0.0f,  1.0f,  0.0f,  1.0f, ""   },
    { ParamID::gain,          "Gain",           ParamKind::Float,    0.0f, 10.0f,  5.5f, 0.01f, ""   },
    { ParamID::bass,          "Bass",           ParamKind::Float,    0.0f, 10.0f,  5.0f, 0.01f, ""   },
    { ParamID::middle,        "Middle",         ParamKind::Float,    0.0f, 10.0f,  6.0f, 0.01f, ""   },
    { ParamID::treble,        "Treble",         ParamKind::Float,    0.0f, 10.0f,  6.0f, 0.01f, ""   },
    { ParamID::presence,      "Presence",       ParamKind::Float,    0.0f, 10.0f,  5.0f, 0.01f, ""   },
    { ParamID::resonance,     "Resonance",      ParamKind::Float,    0.0f, 10.0f,  4.0f, 0.01f, ""   },
    { ParamID::master,        "Master",         ParamKind::Float,    0.0f, 10.0f,  5.0f, 0.01f, ""   },
    { ParamID::bright,        "Bright",         ParamKind::Bool,     0.0f,  1.0f,  0.0f,  1.0f, ""   },
    { ParamID::deep,          "Deep",           ParamKind::Bool,     0.0f,  1.0f,  0.0f,  1.0f, ""   },
    { ParamID::powerTube,     "Power Tubes",    ParamKind::Choice,   0.0f,  6.0f,  0.0f,  1.0f, ""   },
    { ParamID::toneStack,     "Tone Stack",     ParamKind::Choice,   0.0f,  3.0f,  2.0f,  1.0f, ""   },
    { ParamID::cabEnabled,    "Cab Sim",        ParamKind::Bool,     0.0f,  1.0f,  1.0f,  1.0f, ""   },
    { ParamID::outputLevel,   "Output",         ParamKind::Float,  -36.0f, 12.0f, -6.0f,  0.1f, "dB" },
}};

} // namespace vf
