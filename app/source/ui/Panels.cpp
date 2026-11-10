#include "Panels.h"
#include "vf/Brand.h"
#include "vf/Parameters.h"
#include <BinaryData.h>

using namespace juce;

static VFTheme themeOf (Component& c)
{
    if (auto* l = dynamic_cast<ViridianLookAndFeel*> (&c.getLookAndFeel())) return l->getTheme();
    return VFTheme::dark();
}

static Image loadIcon()
{
    return ImageCache::getFromMemory (VFAssets::icon_256_png, VFAssets::icon_256_pngSize);
}

static void drawPanelBackground (Graphics& g, Rectangle<float> r, const VFTheme& t, const String& title)
{
    g.setGradientFill (ColourGradient (t.panel.brighter (0.05f), r.getX(), r.getY(), t.panel.darker (0.15f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (t.panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (t.textDim);
        g.setFont (ViridianLookAndFeel::labelFont (11.0f));
        g.drawText (title, r.reduced (12.0f, 6.0f).removeFromTop (14.0f), Justification::centredLeft);
    }
}

//==============================================================================
TopBar::TopBar()
{
    logo = ImageCache::getFromMemory (VFAssets::logo_wordmark_png, VFAssets::logo_wordmark_pngSize);
    for (auto* c : std::initializer_list<Component*> { &tuner, &cpu, &latency, &inMeter, &outMeter, &presetButton,
                                                       &themeButton, &settingsButton, &aboutButton, &liveInputButton })
        addAndMakeVisible (c);

    presetButton.setEnabled (false);
    presetButton.setTooltip ("Preset browser arrives in Phase 5. Session state is already saved by your DAW.");
    themeButton.setTooltip ("Switch light/dark theme");
    settingsButton.setTooltip ("Audio device, sample rate and buffer size (standalone app)");
    liveInputButton.setTooltip ("The standalone app mutes live input at first launch to prevent speaker feedback. Use headphones, then click to enable.");
    liveInputButton.setClickingTogglesState (false);
}

void TopBar::paint (Graphics& g)
{
    const auto t = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (t.faceplateTop, 0, r.getY(), t.faceplateBottom, 0, r.getBottom(), false));
    g.fillRect (r);
    g.setColour (t.accent.withAlpha (0.8f));
    g.fillRect (r.removeFromBottom (2.0f));

    auto logoArea = getLocalBounds().reduced (12, 8).removeFromLeft (jmin (260, getWidth() / 5)).toFloat();
    if (logo.isValid())
        g.drawImage (logo, logoArea, RectanglePlacement::xLeft | RectanglePlacement::yMid | RectanglePlacement::onlyReduceInSize);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    r.removeFromLeft (jmin (260, getWidth() / 5) + 10);

    const int h = r.getHeight();
    outMeter.setBounds (r.removeFromRight (22));
    r.removeFromRight (4);
    inMeter.setBounds (r.removeFromRight (22));
    r.removeFromRight (12);

    aboutButton.setBounds (r.removeFromRight (66).withSizeKeepingCentre (66, h * 2 / 3));
    r.removeFromRight (6);
    settingsButton.setBounds (r.removeFromRight (66).withSizeKeepingCentre (66, h * 2 / 3));
    r.removeFromRight (6);
    themeButton.setBounds (r.removeFromRight (66).withSizeKeepingCentre (66, h * 2 / 3));
    r.removeFromRight (12);
    latency.setBounds (r.removeFromRight (70));
    cpu.setBounds (r.removeFromRight (56));
    r.removeFromRight (12);

    tuner.setBounds (r.removeFromRight (jmin (300, r.getWidth() / 2)));
    r.removeFromRight (12);
    liveInputButton.setBounds (r.removeFromRight (liveInputButton.isVisible() ? 120 : 0).withSizeKeepingCentre (120, h * 2 / 3));
    r.removeFromRight (liveInputButton.isVisible() ? 8 : 0);
    presetButton.setBounds (r.withSizeKeepingCentre (r.getWidth(), h * 2 / 3));
}

//==============================================================================
AmpHeadPanel::AmpHeadPanel (APVTS& s, ViridianForgeProcessor& processor, int ampIndex)
    : state (s), index (ampIndex),
      channel (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::channel),
      source (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::ampSource),
      gain (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::gain, "Gain"),
      bass (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::bass, "Bass"),
      middle (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::middle, "Middle"),
      treble (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::treble, "Treble"),
      presence (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::presence, "Presence"),
      resonance (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::resonance, "Resonance"),
      master (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::master, "Master"),
      bright (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::bright, "Bright"),
      deep (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::deep, "Deep"),
      boost (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::boost, "Boost"),
      geqOn (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::geqOn, "GEQ"),
      model (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::ampModel),
      tubeSwap (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::tubeSwap, "Power tubes")
{
    for (auto* c : std::initializer_list<Component*> { &channel, &source, &gain, &bass, &middle, &treble, &presence, &resonance, &master,
                                                       &bright, &deep, &boost, &model, &tubeSwap })
        addAndMakeVisible (c);
    addChildComponent (geqOn);
    addChildComponent (namSlot);
    namProfiles = std::make_unique<NamProfileBar> (processor, ampIndex);
    addChildComponent (*namProfiles);

    const char* geqLabels[] = { "80", "240", "750", "2.2k", "6.6k" };
    for (size_t b = 0; b < 5; ++b)
        addChildComponent (geq.add (new LabeledFader (s, ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::geqBands[b], geqLabels[b])));

    namSlot.getStatus = [&processor, ampIndex]
    {
        const auto st = processor.getNamStatus (ampIndex);
        return FileSlot::Status { static_cast<int> (st.state), st.description, st.error };
    };
    namSlot.onLoad = [&processor, ampIndex] (const File& f) { processor.loadNamModel (f, ampIndex); };
    namSlot.onClear = [&processor, ampIndex] { processor.unloadNamModel (ampIndex); };
    namSlot.getLibraryFolder = [] { return ViridianForgeProcessor::getLibraryFolder ("NAM"); };
    namSlot.importFile = [] (const File& f) { return ViridianForgeProcessor::importIntoLibrary (f, "NAM"); };

    if (! ViridianForgeProcessor::isNamAvailable())
    {
        source.setEnabled (false);
        source.setTooltip ("This build was compiled without NAM support");
    }

    model.onModelChanged = [this] (int m) { modelChanged (m); };
    sourceWatch = std::make_unique<ParameterAttachment> (*s.getParameter (ViridianForgeProcessor::ampPrefix (ampIndex) + vf::ParamID::ampSource), [this] (float v)
    {
        namMode = v > 0.5f;
        updateMode();
    });
    sourceWatch->sendInitialUpdate();
    modelChanged (model.getModel());
}

void AmpHeadPanel::modelChanged (int modelIndex)
{
    const auto& spec = vf::models::getAmpModel (modelIndex);
    StringArray names;
    for (int i = 0; i < spec.numChannels; ++i)
        names.add (spec.channels[static_cast<size_t> (i)].name);
    channel.setChannelNames (names);
    hasGeq = spec.hasGEQ;
    badgeName = String (spec.name).toUpperCase();
    badgeCategory = vf::models::categoryName (spec.category);
    updateMode();
}

void AmpHeadPanel::updateMode()
{
    for (auto* c : std::initializer_list<Component*> { &channel, &model, &tubeSwap })
    {
        c->setEnabled (! namMode);
        c->setAlpha (namMode ? 0.35f : 1.0f);
    }
    const bool showGeq = hasGeq && ! namMode;
    geqOn.setVisible (showGeq);
    for (auto* f : geq) f->setVisible (showGeq);
    namSlot.setVisible (namMode);
    if (namProfiles) { namProfiles->setVisible (namMode); namProfiles->refresh(); }
    gain.getSlider().setTooltip (namMode ? "Drive into the NAM capture" : "Gain (double-click to reset)");
    master.getSlider().setTooltip (namMode ? "Output level after the NAM capture" : "Master: power-amp drive (double-click to reset)");
    resized();
    repaint();
}

Rectangle<float> AmpHeadPanel::faceplateArea() const
{
    auto r = getLocalBounds().toFloat().reduced (getWidth() * 0.025f, getHeight() * 0.06f);
    return r.withTrimmedTop (r.getHeight() * 0.40f);
}

Rectangle<float> AmpHeadPanel::grilleArea() const
{
    auto inner = getLocalBounds().toFloat().reduced (2.0f).reduced (getWidth() * 0.025f, getHeight() * 0.06f);
    return inner.removeFromTop (inner.getHeight() * 0.38f);
}

void AmpHeadPanel::paint (Graphics& g)
{
    const auto t = themeOf (*this);
    auto r = getLocalBounds().toFloat().reduced (2.0f);

    // Cabinet shell: tolex with subtle texture
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0, 4), 14.0f);
    g.setColour (t.tolex);
    g.fillRoundedRectangle (r, 14.0f);
    Random rng (7);
    g.setColour (Colours::white.withAlpha (0.025f));
    for (int i = 0; i < 1400; ++i)
        g.fillRect (r.getX() + rng.nextFloat() * r.getWidth(), r.getY() + rng.nextFloat() * r.getHeight(), 1.5f, 1.5f);

    // Piping
    g.setColour (t.accent.withAlpha (0.9f));
    g.drawRoundedRectangle (r.reduced (5.0f), 11.0f, 1.6f);

    // Grille
    auto grille = grilleArea();
    g.setColour (t.grille);
    g.fillRoundedRectangle (grille, 6.0f);
    g.setColour (Colours::black.withAlpha (0.18f));
    for (float x = grille.getX(); x < grille.getRight(); x += 4.0f)
        g.drawLine (x, grille.getY(), x + grille.getHeight() * 0.6f, grille.getBottom(), 1.0f);
    g.setColour (Colours::black.withAlpha (0.3f));
    g.drawRoundedRectangle (grille, 6.0f, 2.0f);

    // Panels on the grille: source switch, graphic EQ / NAM slot
    auto drawInset = [&] (Rectangle<float> area, const String& label)
    {
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (t.panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (Colour (vf::brand::brushedSteel).withAlpha (0.8f));
        g.setFont (ViridianLookAndFeel::labelFont (10.0f));
        g.drawText (label, area.reduced (8.0f, 4.0f).removeFromTop (12.0f), Justification::centredLeft);
    };
    drawInset (source.getBounds().toFloat().expanded (6.0f, 0.0f).withTop (source.getY() - 18.0f).withBottom (source.getBottom() + 6.0f), "AMP SOURCE");
    if (geqOn.isVisible())
    {
        auto area = geqOn.getBounds().getUnion (geq.getLast()->getBounds()).toFloat().expanded (6.0f, 0.0f);
        drawInset (area.withTop (area.getY() - 18.0f).withBottom (area.getBottom() + 4.0f), "GRAPHIC EQ");
    }

    // Brand badge
    const auto badgeName2 = namMode ? String ("NEURAL CAPTURE") : badgeName;
    const auto badgeCat2 = namMode ? String ("Neural Amp Modeler") : badgeCategory;
    const float badgeW = jmin (300.0f, grille.getWidth() * (namMode || geqOn.isVisible() ? 0.3f : 0.42f));
    auto badge = Rectangle<float> (badgeW, grille.getHeight() * 0.42f)
                     .withCentre ({ namMode || geqOn.isVisible() ? (float) source.getRight() + (grille.getRight() - (float) source.getRight()) * (namMode ? 0.24f : 0.30f)
                                                                 : grille.getCentreX(),
                                    grille.getCentreY() - 6.0f });
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (badge.translated (0, 3), 8.0f);
    g.setGradientFill (ColourGradient (Colour (0xff2a2a2a), badge.getX(), badge.getY(), Colour (0xff0d0d0d), badge.getX(), badge.getBottom(), false));
    g.fillRoundedRectangle (badge, 8.0f);
    g.setColour (t.accent);
    g.drawRoundedRectangle (badge.reduced (2.0f), 6.0f, 1.5f);
    g.setFont (ViridianLookAndFeel::labelFont (badge.getHeight() * 0.44f).withExtraKerningFactor (0.18f));
    g.drawFittedText ("VIRIDIAN", badge.withTrimmedBottom (badge.getHeight() * 0.25f).toNearestInt(), Justification::centred, 1);
    g.setColour (Colour (vf::brand::brushedSteel));
    g.setFont (ViridianLookAndFeel::labelFont (badge.getHeight() * 0.16f).withExtraKerningFactor (0.2f));
    g.drawFittedText (badgeName2, badge.withTrimmedTop (badge.getHeight() * 0.6f).reduced (8.0f, 0.0f).toNearestInt(), Justification::centred, 1);
    g.setColour (Colour (vf::brand::brushedSteel).withAlpha (0.75f));
    g.setFont (ViridianLookAndFeel::labelFont (11.0f, false).withExtraKerningFactor (0.3f));
    g.drawText (badgeCat2.toUpperCase(), Rectangle<float> (badge.getX() - 60.0f, badge.getBottom() + 8.0f, badge.getWidth() + 120.0f, 16.0f), Justification::centred);

    // Faceplate (brushed metal)
    auto plate = faceplateArea();
    g.setGradientFill (ColourGradient (t.faceplateTop, plate.getX(), plate.getY(), t.faceplateBottom, plate.getX(), plate.getBottom(), false));
    g.fillRoundedRectangle (plate, 6.0f);
    g.setColour (Colours::white.withAlpha (0.035f));
    for (float y = plate.getY() + 2; y < plate.getBottom(); y += 2.0f)
        g.drawHorizontalLine ((int) y, plate.getX() + 4, plate.getRight() - 4);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (plate, 6.0f, 1.5f);

    const float sr = jmax (3.0f, plate.getHeight() * 0.03f);
    ViridianLookAndFeel::drawScrew (g, { plate.getX() + sr * 2.5f, plate.getY() + sr * 2.5f }, sr);
    ViridianLookAndFeel::drawScrew (g, { plate.getRight() - sr * 2.5f, plate.getY() + sr * 2.5f }, sr);
    ViridianLookAndFeel::drawScrew (g, { plate.getX() + sr * 2.5f, plate.getBottom() - sr * 2.5f }, sr);
    ViridianLookAndFeel::drawScrew (g, { plate.getRight() - sr * 2.5f, plate.getBottom() - sr * 2.5f }, sr);

    g.setColour (t.textDim);
    g.setFont (ViridianLookAndFeel::labelFont (10.0f));
    g.drawText ("CHANNEL", channel.getBounds().toFloat().translated (26, -14).withHeight (12), Justification::centredLeft);

    // Which amp of the chain this head is editing
    auto tag = Rectangle<float> (18.0f, 12.0f, 64.0f, 20.0f);
    g.setColour (t.accent.withAlpha (0.9f));
    g.drawRoundedRectangle (tag, 4.0f, 1.2f);
    g.setFont (ViridianLookAndFeel::labelFont (11.0f));
    g.drawText (index == 0 ? "AMP 1" : "AMP 2", tag, Justification::centred);
}

NamProfileBar::NamProfileBar (ViridianForgeProcessor& p, int amp) : processor (p), ampIndex (amp)
{
    for (int i = 0; i < AmpBlock::kNamProfiles; ++i)
    {
        auto* b = buttons.add (new TextButton (String (i + 1)));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { slotClicked (i); };
        addAndMakeVisible (b);
    }
    clearButton.setTooltip ("Clear the active NAM profile");
    clearButton.onClick = [this]
    {
        const int active = processor.getNamActiveProfile (ampIndex);
        if (active >= 0) processor.clearNamProfile (ampIndex, active);
        refresh();
    };
    addAndMakeVisible (clearButton);
    refresh();
}

void NamProfileBar::refresh()
{
    const int active = processor.getNamActiveProfile (ampIndex);
    for (int i = 0; i < buttons.size(); ++i)
    {
        const auto path = processor.getNamProfilePath (ampIndex, i);
        auto* b = buttons[i];
        b->setToggleState (i == active, dontSendNotification);
        b->setAlpha (path.isEmpty() ? 0.45f : 1.0f);
        b->setTooltip (path.isEmpty() ? "Empty: click to choose a .nam capture"
                                      : "Switch to " + File (path).getFileName());
    }
}

void NamProfileBar::resized()
{
    auto r = getLocalBounds();
    clearButton.setBounds (r.removeFromRight (40).reduced (1, 0));
    const int w = r.getWidth() / buttons.size();
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds (r.removeFromLeft (w).reduced (1, 0));
}

void NamProfileBar::slotClicked (int i)
{
    if (processor.getNamProfilePath (ampIndex, i).isNotEmpty())
    {
        processor.selectNamProfile (ampIndex, i);
        refresh();
        return;
    }
    chooser = std::make_unique<FileChooser> ("Choose NAM profile " + String (i + 1), File(), "*.nam");
    SafePointer<NamProfileBar> safe (this);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
        [safe, i] (const FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (safe != nullptr && file.existsAsFile())
            {
                safe->processor.setNamProfile (safe->ampIndex, i, file);
                safe->refresh();
            }
        });
}

void AmpHeadPanel::resized()
{
    // Grille: source switch on the left, graphic EQ or NAM slot on the right
    auto grille = grilleArea().toNearestInt().reduced (14, 10);
    source.setBounds (grille.removeFromLeft (jmin (190, grille.getWidth() / 5)).withSizeKeepingCentre (jmin (190, grille.getWidth() / 5), 48).translated (0, 6));
    if (namMode)
    {
        const int w = grille.getWidth() * 52 / 100;
        auto slotArea = grille.removeFromRight (w);
        if (namProfiles) namProfiles->setBounds (slotArea.removeFromBottom (24).reduced (0, 2));
        namSlot.setBounds (slotArea.withSizeKeepingCentre (w, jmin (64, slotArea.getHeight())));
    }
    else if (hasGeq)
    {
        auto eq = grille.removeFromRight (jmin (300, grille.getWidth() * 45 / 100)).withTrimmedTop (16);
        geqOn.setBounds (eq.removeFromLeft (52).withSizeKeepingCentre (52, 56));
        const int fw = eq.getWidth() / 5;
        for (auto* f : geq) f->setBounds (eq.removeFromLeft (fw).reduced (2, 0));
    }

    auto plate = faceplateArea().toNearestInt().reduced (getWidth() / 60, getHeight() / 40);

    // Top row of the faceplate: channel switch, switches, model and tube selectors
    auto top = plate.removeFromTop (plate.getHeight() * 30 / 100);
    top.removeFromTop (14);
    channel.setBounds (top.removeFromLeft (top.getWidth() * 34 / 100));
    top.removeFromLeft (12);
    const int sw = jmin (70, top.getWidth() / 8);
    boost.setBounds (top.removeFromLeft (sw));
    bright.setBounds (top.removeFromLeft (sw));
    deep.setBounds (top.removeFromLeft (sw));
    top.removeFromLeft (12);
    model.setBounds (top.removeFromLeft (top.getWidth() * 62 / 100).reduced (4, 0));
    tubeSwap.setBounds (top.reduced (4, 0));

    // Knob row
    plate.removeFromTop (6);
    Component* knobs[] = { &gain, &bass, &middle, &treble, &presence, &resonance, &master };
    const int kw = plate.getWidth() / 7;
    for (auto* k : knobs)
        k->setBounds (plate.removeFromLeft (kw).reduced (kw / 12, 0));
}

//==============================================================================
IOPanel::IOPanel (APVTS& s)
    : input (s, vf::ParamID::inputGain, "Input"),
      gateThreshold (s, vf::ParamID::gateThreshold, "Threshold"),
      output (s, vf::ParamID::outputLevel, "Output"),
      gateOn (s, vf::ParamID::gateEnabled, "Gate")
{
    for (auto* c : std::initializer_list<Component*> { &input, &gateThreshold, &output, &gateOn })
        addAndMakeVisible (c);
}

void IOPanel::paint (Graphics& g) { drawPanelBackground (g, getLocalBounds().toFloat(), themeOf (*this), "INPUT / OUTPUT"); }

void IOPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (18);
    const int h = r.getHeight() / 4;
    input.setBounds (r.removeFromTop (h));
    gateOn.setBounds (r.removeFromTop (h * 2 / 3).withSizeKeepingCentre (jmin (80, r.getWidth()), h * 2 / 3));
    gateThreshold.setBounds (r.removeFromTop (h));
    output.setBounds (r);
}

//==============================================================================
CabPanel::CabPanel (APVTS& s, ViridianForgeProcessor& processor, int cabIndex)
    : index (cabIndex),
      cabOn (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::cabEnabled, "Cab"),
      phaseB (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::irPhaseB, "Phase B"),
      stereo (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::cabStereo, "Stereo"),
      source (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::cabSource),
      cabModel (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::cabModel, "Cabinet"),
      micType (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::micType, "Microphone"),
      micPosition (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::micPosition, "Mic Pos"),
      micDistance (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::micDistance, "Distance"),
      blend (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::irBlend, "A / B"),
      lowCut (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::cabLowCut, "Low Cut"),
      highCut (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::cabHighCut, "High Cut"),
      room (s, ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::room, "Room")
{
    for (auto* c : std::initializer_list<Component*> { &cabOn, &source, &lowCut, &highCut, &room, &stereo })
        addAndMakeVisible (c);
    for (auto* c : std::initializer_list<Component*> { &cabModel, &micType, &micPosition, &micDistance, &irA, &irB, &blend, &phaseB })
        addChildComponent (c);

    micPosition.getSlider().setTooltip ("Mic position: 0 = cone centre (bright), 10 = cone edge (dark)");
    micDistance.getSlider().setTooltip ("Mic distance: 0 = on the grille (more bass), 10 = further back");
    stereo.getButton().setTooltip ("IR A+B: A left / B right. Otherwise the room adds the stereo image.");

    for (int slot = 0; slot < 2; ++slot)
    {
        auto& fs = slot == 0 ? irA : irB;
        fs.getStatus = [&processor, slot, cabIndex]
        {
            const auto st = processor.getIrStatus (slot, cabIndex);
            return FileSlot::Status { static_cast<int> (st.state), st.description, st.error };
        };
        fs.onLoad = [&processor, slot, cabIndex] (const File& f) { processor.loadImpulseResponse (slot, f, cabIndex); };
        fs.onClear = [&processor, slot, cabIndex] { processor.clearImpulseResponse (slot, cabIndex); };
        fs.getLibraryFolder = [] { return ViridianForgeProcessor::getLibraryFolder ("IRs"); };
        fs.importFile = [] (const File& f) { return ViridianForgeProcessor::importIntoLibrary (f, "IRs"); };
    }

    modelWatch = std::make_unique<ParameterAttachment> (*s.getParameter (ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::cabModel), [this] (float v)
    {
        currentCab = roundToInt (v);
        repaint();
    });
    modelWatch->sendInitialUpdate();
    sourceWatch = std::make_unique<ParameterAttachment> (*s.getParameter (ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::cabSource), [this] (float v)
    {
        mode = roundToInt (v);
        updateMode();
    });
    sourceWatch->sendInitialUpdate();
}

void CabPanel::updateMode()
{
    const bool builtIn = mode == 0;
    for (auto* c : std::initializer_list<Component*> { &cabModel, &micType, &micPosition, &micDistance })
        c->setVisible (builtIn);
    irA.setVisible (! builtIn);
    irB.setVisible (mode == 2);
    blend.setVisible (mode == 2);
    phaseB.setVisible (mode == 2);
    resized();
    repaint();
}

Rectangle<float> CabPanel::cabArea() const
{
    if (mode != 0 || ! micType.isVisible()) return {};
    auto top = micType.getBottom() + 6, bottom = micPosition.getY() - 4;
    auto r = Rectangle<float> ((float) getX() * 0.0f + 14.0f, (float) top, (float) getWidth() - 28.0f, (float) jmax (0, bottom - top));
    return r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight() * 1.1f), r.getHeight());
}

void CabPanel::paint (Graphics& g)
{
    const auto t = themeOf (*this);
    drawPanelBackground (g, getLocalBounds().toFloat(), t, index == 0 ? "CABINET 1" : "CABINET 2");

    if (mode == 0)
    {
        const String name (vf::dsp::cabModelNames[static_cast<size_t> (jlimit (0, (int) vf::dsp::CabModel::count - 1, currentCab))]);
        const int speakers = name.startsWith ("4") ? 4 : (name.startsWith ("2") ? 2 : 1);
        const bool tens = name.contains ("x10");

        auto cab = cabArea();
        if (! cab.isEmpty())
        {
            if (speakers == 1) cab = cab.withSizeKeepingCentre (cab.getWidth() * 0.8f, cab.getHeight() * 0.85f);
            if (speakers == 2) cab = cab.withSizeKeepingCentre (cab.getWidth(), cab.getHeight() * 0.62f);
            g.setColour (t.tolex);
            g.fillRoundedRectangle (cab, 8.0f);
            auto cloth = cab.reduced (cab.getWidth() * 0.06f);
            g.setColour (t.grille);
            g.fillRoundedRectangle (cloth, 4.0f);
            const float d = jmin (cloth.getWidth(), cloth.getHeight()) * (speakers == 1 ? 0.78f : (speakers == 2 ? 0.8f : 0.44f)) * (tens ? 0.85f : 1.0f);
            g.setColour (Colours::black.withAlpha (0.22f));
            for (int i = 0; i < speakers; ++i)
            {
                float cx = cloth.getCentreX(), cy = cloth.getCentreY();
                if (speakers == 2) cx = cloth.getX() + cloth.getWidth() * (i == 0 ? 0.27f : 0.73f);
                if (speakers == 4)
                {
                    cx = cloth.getX() + cloth.getWidth() * (i % 2 == 0 ? 0.27f : 0.73f);
                    cy = cloth.getY() + cloth.getHeight() * (i < 2 ? 0.27f : 0.73f);
                }
                g.drawEllipse (cx - d / 2, cy - d / 2, d, d, 2.0f);
                g.fillEllipse (cx - d * 0.1f, cy - d * 0.1f, d * 0.2f, d * 0.2f);
            }
        }
    }

    g.setColour (t.textDim);
    g.setFont (ViridianLookAndFeel::labelFont (10.0f, false));
    const String note = mode == 0 ? String ("Speaker voice: ") + vf::dsp::getCabinetSpec (static_cast<vf::dsp::CabModel> (jlimit (0, (int) vf::dsp::CabModel::count - 1, currentCab))).speaker
                                  : String ("Load WAV/AIFF impulse responses (any rate)");
    g.drawFittedText (note, getLocalBounds().reduced (10).removeFromBottom (14), Justification::centred, 1);
}

void CabPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto header = r.removeFromTop (30);
    cabOn.setBounds (header.removeFromRight (60));
    r.removeFromBottom (16); // note

    source.setBounds (r.removeFromTop (46));
    r.removeFromTop (6);

    // Shared bottom row: low cut, high cut, room (+ stereo switch)
    const int knobH = jlimit (60, 90, r.getHeight() / 6);
    auto bottom = r.removeFromBottom (knobH);
    auto stereoArea = bottom.removeFromRight (bottom.getWidth() / 4);
    stereo.setBounds (stereoArea.withSizeKeepingCentre (stereoArea.getWidth(), jmin (52, stereoArea.getHeight())));
    const int bw = bottom.getWidth() / 3;
    lowCut.setBounds (bottom.removeFromLeft (bw));
    highCut.setBounds (bottom.removeFromLeft (bw));
    room.setBounds (bottom);
    r.removeFromBottom (6);

    if (mode == 0)
    {
        cabModel.setBounds (r.removeFromTop (40).reduced (2, 0));
        micType.setBounds (r.removeFromTop (40).reduced (2, 0));
        auto knobs = r.removeFromBottom (knobH);
        micPosition.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2).reduced (4, 0));
        micDistance.setBounds (knobs.reduced (4, 0));
    }
    else
    {
        irA.setBounds (r.removeFromTop (48));
        r.removeFromTop (6);
        if (mode == 2)
        {
            irB.setBounds (r.removeFromTop (48));
            r.removeFromTop (6);
            auto row = r.removeFromTop (knobH);
            blend.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (4, 0));
            phaseB.setBounds (row.withSizeKeepingCentre (jmin (80, row.getWidth()), jmin (52, row.getHeight())));
        }
    }
}

//==============================================================================
SplashOverlay::SplashOverlay()
{
    icon = loadIcon();
    setInterceptsMouseClicks (true, false);
    startTimerHz (60);
}

void SplashOverlay::paint (Graphics& g)
{
    g.setOpacity (alpha);
    g.fillAll (Colour (0xf20b0d0c));
    auto r = getLocalBounds().toFloat();
    const float s = jmin (r.getWidth(), r.getHeight()) * 0.32f;
    const auto iconArea = Rectangle<float> (s, s).withCentre ({ r.getCentreX(), r.getCentreY() - s * 0.3f });
    if (icon.isValid())
        g.drawImage (icon, iconArea, RectanglePlacement::centred);

    auto textArea = Rectangle<float> (r.getX(), iconArea.getBottom() + s * 0.08f, r.getWidth(), s * 0.24f);
    g.setColour (Colour (vf::brand::viridian).withMultipliedAlpha (alpha));
    g.setFont (ViridianLookAndFeel::labelFont (s * 0.18f).withExtraKerningFactor (0.2f));
    g.drawText ("VIRIDIAN FORGE", textArea, Justification::centred);
    g.setColour (Colours::white.withAlpha (0.6f * alpha));
    g.setFont (ViridianLookAndFeel::labelFont (s * 0.07f, false));
    g.drawText (String (vf::brand::tagline) + "   v" + vf::brand::versionText,
                textArea.translated (0, s * 0.24f).withHeight (s * 0.1f), Justification::centred);
}

void SplashOverlay::timerCallback()
{
    if (! fading && ++ticks > 100) // ~1.7 s
        fading = true;
    if (fading)
    {
        alpha -= 0.06f;
        if (alpha <= 0.0f) { stopTimer(); setVisible (false); if (onDismissed) onDismissed(); return; }
    }
    repaint();
}

void SplashOverlay::dismiss() { fading = true; }

//==============================================================================
AboutOverlay::AboutOverlay()
{
    icon = loadIcon();
    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { setVisible (false); };
}

Rectangle<int> AboutOverlay::card() const
{
    return getLocalBounds().withSizeKeepingCentre (jmin (560, getWidth() - 40), jmin (440, getHeight() - 40));
}

void AboutOverlay::paint (Graphics& g)
{
    const auto t = themeOf (*this);
    g.fillAll (Colours::black.withAlpha (0.6f));
    auto c = card().toFloat();
    g.setColour (t.panel);
    g.fillRoundedRectangle (c, 12.0f);
    g.setColour (t.accent);
    g.drawRoundedRectangle (c.reduced (1.0f), 12.0f, 1.5f);

    auto r = c.reduced (24.0f);
    auto head = r.removeFromTop (110.0f);
    if (icon.isValid())
        g.drawImage (icon, head.removeFromLeft (110.0f), RectanglePlacement::centred);
    head.removeFromLeft (18.0f);
    g.setColour (t.accent);
    g.setFont (ViridianLookAndFeel::labelFont (28.0f).withExtraKerningFactor (0.12f));
    g.drawText ("VIRIDIAN FORGE", head.removeFromTop (44.0f), Justification::bottomLeft);
    g.setColour (t.text);
    g.setFont (ViridianLookAndFeel::labelFont (14.0f, false));
    g.drawText (String ("Version ") + vf::brand::versionText, head.removeFromTop (22.0f), Justification::centredLeft);
    g.setColour (t.textDim);
    g.drawText (String (vf::brand::company) + "  -  " + vf::brand::tagline, head.removeFromTop (22.0f), Justification::centredLeft);

    r.removeFromTop (12.0f);
    const String legal =
        "Guitar amp and effects modeling platform. Standalone app, VST3"
#if JUCE_MAC
        " and AU"
#endif
        ".\n\n"
        "Built with JUCE (juce.com). VST is a registered trademark of Steinberg Media Technologies GmbH.\n\n"
        "All amp, effect, speaker and microphone model names referenced by this product are trademarks of their "
        "respective owners, used only to describe the tones that inspired our original DSP models. Their use does not "
        "imply any affiliation with or endorsement by those owners.\n\n"
        "Icon artwork inspired by the founder's green dual-humbucker guitar.";
    g.setColour (t.text);
    g.setFont (ViridianLookAndFeel::labelFont (12.5f, false));
    g.drawFittedText (legal, r.withTrimmedBottom (40.0f).toNearestInt(), Justification::topLeft, 16);
}

void AboutOverlay::resized()
{
    closeButton.setBounds (card().reduced (24).removeFromBottom (32).removeFromRight (100));
}

void AboutOverlay::mouseUp (const MouseEvent& e)
{
    if (! card().contains (e.getPosition()))
        setVisible (false);
}
