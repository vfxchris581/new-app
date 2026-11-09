// Viridian Forge — UI snapshot + end-to-end smoke test (developer tool).
//
// Creates the real plugin processor and editor, applies a scenario, pushes
// audio through processBlock (so NAM/IR loads complete and meters move), and
// renders the editor to a PNG. Exits non-zero if audio is silent or not finite.
//
//   vf_snapshot <scenario> <out.png> [nam-file] [ir-file]
//   scenarios: default | mark | nam | ir | light

#include <JuceHeader.h>
#include "PluginProcessor.h"

static void setParam (juce::AudioProcessorValueTreeState& s, const char* id, float value)
{
    if (auto* p = s.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

static bool runAudio (ViridianForgeProcessor& proc, int blocks, double& rmsOut)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    double sum = 0.0; long n = 0;
    bool finite = true;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = (b * 512 + i) / 48000.0;
            const float x = static_cast<float> (0.25 * std::exp (-std::fmod (t, 1.0) * 3.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t));
            buffer.setSample (0, i, x); buffer.setSample (1, i, x);
        }
        proc.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
            {
                const float y = buffer.getSample (ch, i);
                if (! std::isfinite (y)) finite = false;
                if (b > blocks / 2) { sum += static_cast<double> (y) * y; ++n; }
            }
    }
    rmsOut = n > 0 ? std::sqrt (sum / static_cast<double> (n)) : 0.0;
    return finite;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::printf ("usage: vf_snapshot <scenario> <out.png> [nam] [ir]\n"); return 2; }
    const juce::String scenario (argv[1]);
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    const juce::String namPath = argc > 3 ? argv[3] : "", irPath = argc > 4 ? argv[4] : "";

   #if JUCE_WINDOWS
    _putenv_s ("VIRIDIAN_NO_SPLASH", "1");
   #else
    setenv ("VIRIDIAN_NO_SPLASH", "1", 1);
   #endif
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& proc = dynamic_cast<ViridianForgeProcessor&> (*base);
    auto& st = proc.getState();
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    if (scenario == "mark")
    {
        setParam (st, vf::ParamID::ampModel, static_cast<float> (vf::models::AmpModelId::SierraMkIICPlus));
        setParam (st, vf::ParamID::channel, 1.0f);
        const float v[] = { 4.0f, 1.0f, -5.0f, 2.0f, 4.0f };
        for (size_t b = 0; b < 5; ++b) setParam (st, vf::ParamID::geqBands[b], v[b]);
        setParam (st, vf::ParamID::cabModel, static_cast<float> (vf::dsp::CabModel::V30_4x12));
        setParam (st, vf::ParamID::micType, 1.0f);
    }
    else if (scenario == "nam")
    {
        setParam (st, vf::ParamID::ampSource, 1.0f);
        if (namPath.isNotEmpty()) proc.loadNamModel (juce::File (namPath));
    }
    else if (scenario == "ir")
    {
        setParam (st, vf::ParamID::ampModel, static_cast<float> (vf::models::AmpModelId::NeoClassicalLead));
        setParam (st, vf::ParamID::cabSource, 2.0f);
        setParam (st, vf::ParamID::room, 3.0f);
        if (irPath.isNotEmpty()) { proc.loadImpulseResponse (0, juce::File (irPath)); proc.loadImpulseResponse (1, juce::File (irPath)); }
    }
    else if (scenario == "light")
    {
        proc.getThemeValue() = "light";
        setParam (st, vf::ParamID::ampModel, static_cast<float> (vf::models::AmpModelId::Canyon6V6));
    }

    // Let background loads finish, then run audio so swaps happen and meters move.
    for (int i = 0; i < 100; ++i)
    {
        const auto ns = proc.getNamStatus();
        if (ns.state != LoadState::Loading) break;
        juce::Thread::sleep (20);
    }
    double rms = 0.0;
    bool finite = runAudio (proc, 60, rms);
    juce::Thread::sleep (300); // convolution IR loads happen on JUCE's background thread
    finite = runAudio (proc, 120, rms) && finite;
    proc.housekeeping();

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditorIfNeeded());
    editor->setSize (1280, 800);
    for (int i = 0; i < 15; ++i) // let editor timers update meters, tuner and file slots
    {
        runAudio (proc, 4, rms);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    const bool saved = os.openedOk() && png.writeImageToStream (image, os);

    const auto ns = proc.getNamStatus();
    std::printf ("scenario %s: rms %.4f finite %d saved %d | NAM: state %d %s %s | IR A: %s\n", scenario.toRawUTF8(), rms, finite ? 1 : 0, saved ? 1 : 0,
                 static_cast<int> (ns.state), ns.description.toRawUTF8(), ns.error.toRawUTF8(), proc.getIrStatus (0).description.toRawUTF8());
    proc.editorBeingDeleted (editor.get());
    editor.reset();
    proc.releaseResources();
    return (finite && rms > 1.0e-4 && saved) ? 0 : 1;
}
