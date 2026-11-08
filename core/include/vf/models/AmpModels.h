// Viridian Forge — amp model registry.
//
// Every amp is a *data description of its circuit*, evaluated by the shared
// engine in AmpVoice: how many triode stages each channel has and how they
// are biased, where the gain pot, bright cap and tone stack sit, which tone
// stack (computed from component values), the power tubes, the amount of
// negative feedback, and the rectifier/supply sag.
//
// Names are original. `inspiredBy` describes the tone that guided the
// voicing; those names are trademarks of their owners and no affiliation or
// endorsement is implied. IDs are stored in sessions and presets: append new
// models at the end, never reorder or delete.
#pragma once

#include "../dsp/ToneStack.h"
#include "../dsp/Triode.h"
#include "../dsp/TubeStage.h"
#include <array>

namespace vf::models
{

using namespace vf::dsp;

enum class AmpCategory
{
    AmericanClean, BritishClean, VintageCrunch, ClassicRock, BritishHighGain, AmericanHighGain,
    ModernMetal, Boutique, Vintage, SignatureInspired, MesaMarkCollection, MarshallCollection, count
};

inline const char* categoryName (AmpCategory c)
{
    static const char* names[] = { "American Clean", "British Clean", "Vintage Crunch", "Classic Rock", "British High Gain",
                                   "American High Gain", "Modern Metal", "Boutique", "Vintage", "Signature-Inspired",
                                   "Mesa Mark Collection", "Marshall Collection" };
    return names[static_cast<int> (c)];
}

struct ChannelSpec
{
    const char* name = "Normal";
    int numStages = 2;
    std::array<StageSpec, 4> stages {};
    int gainPotBeforeStage = 1;   // the gain/volume pot sits in front of this stage
    int toneStackBeforeStage = 2; // == numStages means after the last stage
    float gainPotMinDb = -40.0f;  // attenuation at gain 0 (audio taper up to 0 dB at 10)
    float brightHz = 1800.0f, brightDb = 6.0f;   // bright cap across the gain pot
    float stackMakeupDb = 0.0f;   // only for active/solid-state EQs
};

struct PowerSpec
{
    PowerTube tube = PowerTube::EL34;
    float nfb = 0.5f;         // 0 = no negative feedback (Vox/Class A style, Cut control)
    float sagMul = 1.0f;      // >1 tube rectifier, <1 solid-state rectifier
    float headroom = 1.0f;    // >1 = louder, cleaner power section
    bool solidState = false;
};

struct AmpModelSpec
{
    const char* name;
    const char* inspiredBy;
    AmpCategory category;
    ToneStackComponents stack;
    PowerSpec power;
    int numChannels;
    std::array<ChannelSpec, 3> channels;
};

namespace detail
{
    inline StageSpec stage (TriodeCircuit c, float inputScale, float millerHz = 12000.0f, float cathodeHz = 0.0f, float cathodeDb = 0.0f,
                            float couplingHz = 30.0f, float recoveryMs = 60.0f)
    {
        StageSpec s;
        s.circuit = c; s.inputScale = inputScale; s.millerHz = millerHz; s.cathodeShelfHz = cathodeHz;
        s.cathodeShelfDb = cathodeDb; s.couplingHz = couplingHz; s.recoveryMs = recoveryMs;
        return s;
    }

    inline ChannelSpec channel (const char* name, int n, std::array<StageSpec, 4> st, int potBefore, int stackBefore,
                                float brightHz = 1800.0f, float brightDb = 6.0f, float potMinDb = -40.0f)
    {
        ChannelSpec c;
        c.name = name; c.numStages = n; c.stages = st; c.gainPotBeforeStage = potBefore; c.toneStackBeforeStage = stackBefore;
        c.brightHz = brightHz; c.brightDb = brightDb; c.gainPotMinDb = potMinDb;
        return c;
    }

    inline ToneStackComponents topBoost()   { return { "Top Boost",  1e6,   1e6,   10e3, 100e3,  50e-12,  22e-9, 22e-9 }; }
    inline ToneStackComponents dumbleish()  { return { "OD Special", 250e3, 250e3, 25e3, 100e3, 470e-12,  22e-9, 22e-9 }; }
    inline ToneStackComponents californian(){ return { "Classic",    250e3, 1e6,   25e3, 68e3,  470e-12,  47e-9, 22e-9 }; }
    inline ToneStackComponents flatSS()     { return { "Solid State",250e3, 250e3, 25e3, 47e3,  220e-12, 100e-9, 47e-9 }; }
}

enum class AmpModelId
{
    Canyon6V6 = 0, Canyon100, TweedForge59, GlassClean120, Mayfair30, Plate59Lead, Forge82Lead,
    VelvetOD, RidgelineClassic, Junkyard30, count
};

inline const std::array<AmpModelSpec, static_cast<size_t> (AmpModelId::count)>& ampModels()
{
    using namespace detail;
    using TC = TriodeCircuit;

    static const std::array<AmpModelSpec, static_cast<size_t> (AmpModelId::count)> models {{
        { "Canyon 6V6 Clean", "'65 blackface Deluxe Reverb", AmpCategory::AmericanClean,
          ToneStackComponents::blackface(), { PowerTube::SixV6, 0.45f, 1.5f, 0.9f, false }, 1,
          {{ channel ("Vibrato", 2, {{ stage (TC::Standard, 1.0f, 14000.0f), stage (TC::Standard, 1.0f, 10000.0f) }}, 1, 1, 1500.0f, 7.0f) }} },

        { "Canyon 100 Clean", "'65 blackface Twin Reverb", AmpCategory::AmericanClean,
          ToneStackComponents::blackface(), { PowerTube::SixL6, 0.6f, 0.6f, 1.6f, false }, 1,
          {{ channel ("Normal", 2, {{ stage (TC::Standard, 1.0f, 15000.0f), stage (TC::Standard, 0.35f, 12000.0f) }}, 1, 1, 1300.0f, 8.0f) }} },

        { "Tweed Forge 59", "'59 tweed Bassman", AmpCategory::Vintage,
          ToneStackComponents::tweedBassman(), { PowerTube::T5881, 0.4f, 1.4f, 1.0f, false }, 1,
          {{ channel ("Bright", 2, {{ stage (TC::Standard, 1.0f, 11000.0f), stage (TC::Standard, 1.0f, 8000.0f, 0.0f, 0.0f, 20.0f, 90.0f) }}, 1, 2, 2200.0f, 5.0f) }} },

        { "Glass Clean 120", "solid-state stereo jazz combos of the 1970s", AmpCategory::Vintage,
          detail::flatSS(), { PowerTube::SixL6, 0.9f, 0.0f, 2.0f, true }, 1,
          {{ [] { auto c = channel ("Clean", 0, {}, 0, 0, 2500.0f, 4.0f, -30.0f); c.stackMakeupDb = 16.0f; return c; }() }} },

        { "Mayfair 30 TB", "Vox AC30 Top Boost", AmpCategory::BritishClean,
          detail::topBoost(), { PowerTube::EL84, 0.0f, 1.6f, 0.9f, false }, 2,
          {{ channel ("Normal", 2, {{ stage (TC::Soft, 1.0f, 13000.0f), stage (TC::Standard, 0.6f, 11000.0f) }}, 1, 2, 2000.0f, 4.0f),
             channel ("Top Boost", 3, {{ stage (TC::Soft, 1.0f, 13000.0f), stage (TC::Hot, 0.5f, 9000.0f, 400.0f, 3.0f), stage (TC::Standard, 0.06f, 9000.0f) }}, 1, 3, 2400.0f, 5.0f) }} },

        { "Plate 59 Lead", "Marshall 1959 Super Lead (Plexi)", AmpCategory::MarshallCollection,
          ToneStackComponents::british(), { PowerTube::EL34, 0.5f, 0.9f, 1.1f, false }, 2,
          {{ channel ("Normal", 2, {{ stage (TC::Standard, 1.0f, 9000.0f), stage (TC::Standard, 1.0f, 9500.0f) }}, 1, 2, 2600.0f, 3.0f),
             channel ("Bright", 2, {{ stage (TC::Standard, 1.0f, 12000.0f, 650.0f, 4.0f), stage (TC::Standard, 1.0f, 9500.0f) }}, 1, 2, 1600.0f, 8.0f) }} },

        { "Forge 82 Lead", "Marshall JCM800 2203", AmpCategory::ClassicRock,
          ToneStackComponents::british(), { PowerTube::EL34, 0.55f, 0.9f, 1.0f, false }, 1,
          {{ channel ("Lead", 3, {{ stage (TC::Standard, 1.0f, 12000.0f, 700.0f, 5.0f), stage (TC::Cold, 1.0f, 8000.0f, 0.0f, 0.0f, 120.0f, 40.0f),
                                    stage (TC::Standard, 0.7f, 7000.0f) }}, 1, 3, 1800.0f, 6.0f) }} },

        { "Velvet OD", "Dumble Overdrive Special", AmpCategory::Boutique,
          detail::dumbleish(), { PowerTube::SixL6, 0.65f, 0.7f, 1.4f, false }, 2,
          {{ channel ("Clean", 2, {{ stage (TC::Standard, 1.0f, 14000.0f), stage (TC::Standard, 0.6f, 11000.0f) }}, 1, 1, 1400.0f, 5.0f),
             channel ("Overdrive", 4, {{ stage (TC::Standard, 1.0f, 14000.0f), stage (TC::Standard, 0.06f, 11000.0f),
                                         stage (TC::Hot, 0.5f, 6500.0f, 300.0f, 4.0f, 60.0f, 80.0f), stage (TC::Soft, 0.05f, 5500.0f, 0.0f, 0.0f, 60.0f, 120.0f) }}, 2, 1, 1400.0f, 5.0f, -60.0f) }} },

        { "Ridgeline Classic", "Two-Rock Classic Reverb", AmpCategory::Boutique,
          detail::californian(), { PowerTube::SixL6, 0.55f, 0.8f, 1.5f, false }, 2,
          {{ channel ("Clean", 2, {{ stage (TC::Standard, 1.0f, 15000.0f), stage (TC::Standard, 0.45f, 12000.0f) }}, 1, 1, 1700.0f, 6.0f),
             channel ("Lead", 3, {{ stage (TC::Standard, 1.0f, 15000.0f), stage (TC::Standard, 0.45f, 12000.0f),
                                    stage (TC::Hot, 0.3f, 7000.0f, 500.0f, 3.0f, 50.0f, 70.0f) }}, 1, 1, 1700.0f, 6.0f) }} },

        { "Junkyard 30", "Dr. Z Z-Wreck", AmpCategory::Boutique,
          ToneStackComponents::tweedBassman(), { PowerTube::EL84, 0.25f, 1.3f, 1.0f, false }, 1,
          {{ channel ("Normal", 3, {{ stage (TC::Soft, 1.0f, 12000.0f), stage (TC::Hot, 0.5f, 9000.0f, 450.0f, 3.0f), stage (TC::Standard, 0.08f, 8500.0f) }}, 1, 3, 2000.0f, 4.0f) }} },
    }};
    return models;
}

inline const AmpModelSpec& getAmpModel (int index)
{
    const auto& m = ampModels();
    return m[static_cast<size_t> (index < 0 ? 0 : (index >= static_cast<int> (m.size()) ? static_cast<int> (m.size()) - 1 : index))];
}

inline int numAmpModels() { return static_cast<int> (ampModels().size()); }

} // namespace vf::models
