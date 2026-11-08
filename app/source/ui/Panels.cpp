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
AmpHeadPanel::AmpHeadPanel (APVTS& s)
    : channel (s, vf::ParamID::channel),
      gain (s, vf::ParamID::gain, "Gain"),
      bass (s, vf::ParamID::bass, "Bass"),
      middle (s, vf::ParamID::middle, "Middle"),
      treble (s, vf::ParamID::treble, "Treble"),
      presence (s, vf::ParamID::presence, "Presence"),
      resonance (s, vf::ParamID::resonance, "Resonance"),
      master (s, vf::ParamID::master, "Master"),
      bright (s, vf::ParamID::bright, "Bright"),
      deep (s, vf::ParamID::deep, "Deep"),
      boost (s, vf::ParamID::boost, "Boost"),
      model (s, vf::ParamID::ampModel),
      tubeSwap (s, vf::ParamID::tubeSwap, "Power tubes")
{
    for (auto* c : std::initializer_list<Component*> { &channel, &gain, &bass, &middle, &treble, &presence, &resonance, &master,
                                                       &bright, &deep, &boost, &model, &tubeSwap })
        addAndMakeVisible (c);

    model.onModelChanged = [this] (int index) { modelChanged (index); };
    modelChanged (model.getModel());
}

void AmpHeadPanel::modelChanged (int index)
{
    const auto& spec = vf::models::getAmpModel (index);
    StringArray names;
    for (int i = 0; i < spec.numChannels; ++i)
        names.add (spec.channels[static_cast<size_t> (i)].name);
    channel.setChannelNames (names);
    badgeName = String (spec.name).toUpperCase();
    badgeCategory = vf::models::categoryName (spec.category);
    repaint();
}

Rectangle<float> AmpHeadPanel::faceplateArea() const
{
    auto r = getLocalBounds().toFloat().reduced (getWidth() * 0.025f, getHeight() * 0.06f);
    return r.withTrimmedTop (r.getHeight() * 0.40f);
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

    // Grille (upper area)
    auto inner = r.reduced (getWidth() * 0.025f, getHeight() * 0.06f);
    auto grille = inner.removeFromTop (inner.getHeight() * 0.38f);
    g.setColour (t.grille);
    g.fillRoundedRectangle (grille, 6.0f);
    g.setColour (Colours::black.withAlpha (0.18f));
    for (float x = grille.getX(); x < grille.getRight(); x += 4.0f)
        g.drawLine (x, grille.getY(), x + grille.getHeight() * 0.6f, grille.getBottom(), 1.0f);
    g.setColour (Colours::black.withAlpha (0.3f));
    g.drawRoundedRectangle (grille, 6.0f, 2.0f);

    // Brand badge on the grille
    auto badge = grille.withSizeKeepingCentre (jmin (360.0f, grille.getWidth() * 0.45f), grille.getHeight() * 0.42f);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (badge.translated (0, 3), 8.0f);
    g.setGradientFill (ColourGradient (Colour (0xff2a2a2a), badge.getX(), badge.getY(), Colour (0xff0d0d0d), badge.getX(), badge.getBottom(), false));
    g.fillRoundedRectangle (badge, 8.0f);
    g.setColour (t.accent);
    g.drawRoundedRectangle (badge.reduced (2.0f), 6.0f, 1.5f);
    g.setFont (ViridianLookAndFeel::labelFont (badge.getHeight() * 0.48f).withExtraKerningFactor (0.18f));
    g.drawText ("VIRIDIAN", badge.withTrimmedBottom (badge.getHeight() * 0.25f), Justification::centred);
    g.setColour (Colour (vf::brand::brushedSteel));
    g.setFont (ViridianLookAndFeel::labelFont (badge.getHeight() * 0.17f).withExtraKerningFactor (0.25f));
    g.drawFittedText (badgeName, badge.withTrimmedTop (badge.getHeight() * 0.6f).reduced (8.0f, 0.0f).toNearestInt(), Justification::centred, 1);
    g.setColour (Colour (vf::brand::brushedSteel).withAlpha (0.75f));
    g.setFont (ViridianLookAndFeel::labelFont (11.0f, false).withExtraKerningFactor (0.3f));
    g.drawText (badgeCategory.toUpperCase(), Rectangle<float> (badge.getX() - 60.0f, badge.getBottom() + 8.0f, badge.getWidth() + 120.0f, 16.0f), Justification::centred);

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

    // Section legends
    g.setColour (t.textDim);
    g.setFont (ViridianLookAndFeel::labelFont (10.0f));
    g.drawText ("CHANNEL", channel.getBounds().toFloat().translated (26, -14).withHeight (12), Justification::centredLeft);
}

void AmpHeadPanel::resized()
{
    auto plate = faceplateArea().toNearestInt().reduced (getWidth() / 60, getHeight() / 40);

    // Top row of the faceplate: channel switch, switches, voicing selectors
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
CabPanel::CabPanel (APVTS& s)
    : cabOn (s, vf::ParamID::cabEnabled, "Cab"),
      cabModel (s, vf::ParamID::cabModel, "Cabinet"),
      micPosition (s, vf::ParamID::micPosition, "Mic Pos"),
      micDistance (s, vf::ParamID::micDistance, "Distance")
{
    for (auto* c : std::initializer_list<Component*> { &cabOn, &cabModel, &micPosition, &micDistance })
        addAndMakeVisible (c);
    micPosition.getSlider().setTooltip ("Mic position: 0 = cone centre (bright), 10 = cone edge (dark)");
    micDistance.getSlider().setTooltip ("Mic distance: 0 = on the grille (more bass), 10 = further back");

    modelWatch = std::make_unique<ParameterAttachment> (*s.getParameter (vf::ParamID::cabModel), [this] (float v)
    {
        currentCab = roundToInt (v);
        repaint();
    });
    modelWatch->sendInitialUpdate();
}

Rectangle<float> CabPanel::cabArea() const
{
    auto r = getLocalBounds().toFloat().reduced (14.0f);
    r.removeFromTop (78.0f);                       // title + selector
    r.removeFromBottom (r.getHeight() * 0.36f);    // mic knobs + note
    return r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight() * 1.1f), r.getHeight());
}

void CabPanel::paint (Graphics& g)
{
    const auto t = themeOf (*this);
    drawPanelBackground (g, getLocalBounds().toFloat(), t, "CABINET");

    const String name (vf::dsp::cabModelNames[static_cast<size_t> (jlimit (0, (int) vf::dsp::CabModel::count - 1, currentCab))]);
    const int speakers = name.startsWith ("4") ? 4 : (name.startsWith ("2") ? 2 : 1);
    const bool tens = name.contains ("x10");

    auto cab = cabArea();
    if (speakers == 1) cab = cab.withSizeKeepingCentre (cab.getWidth() * 0.8f, cab.getHeight() * 0.8f);
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

    g.setColour (t.textDim);
    g.setFont (ViridianLookAndFeel::labelFont (10.0f, false));
    g.drawFittedText (String ("Speaker voice: ") + vf::dsp::getCabinetSpec (static_cast<vf::dsp::CabModel> (jlimit (0, (int) vf::dsp::CabModel::count - 1, currentCab))).speaker
                      + ".  IR loader: Phase 3", getLocalBounds().reduced (10).removeFromBottom (24), Justification::centred, 2);
}

void CabPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto header = r.removeFromTop (34);
    cabOn.setBounds (header.removeFromRight (64));
    cabModel.setBounds (r.removeFromTop (40).reduced (2, 0));
    r.removeFromBottom (26);
    const auto cab = cabArea();
    auto knobs = r.withTop (roundToInt (cab.getBottom()) + 4);
    micPosition.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2).reduced (4, 0));
    micDistance.setBounds (knobs.reduced (4, 0));
}

//==============================================================================
SignalChainStrip::SignalChainStrip (APVTS& s) : state (s)
{
    blocks.add ({ "IN", {}, true });
    blocks.add ({ "GATE", vf::ParamID::gateEnabled, true });
    blocks.add ({ "FX", {}, false });
    blocks.add ({ "AMP", {}, true });
    blocks.add ({ "CAB", vf::ParamID::cabEnabled, true });
    blocks.add ({ "FX", {}, false });
    blocks.add ({ "FX", {}, false });
    blocks.add ({ "OUT", {}, true });
    state.addParameterListener (vf::ParamID::gateEnabled, this);
    state.addParameterListener (vf::ParamID::cabEnabled, this);
    setTooltip ("Click GATE or CAB to bypass. Drag-and-drop effects, parallel paths and 100+ effects arrive in Phase 4.");
}

SignalChainStrip::~SignalChainStrip()
{
    state.removeParameterListener (vf::ParamID::gateEnabled, this);
    state.removeParameterListener (vf::ParamID::cabEnabled, this);
}

void SignalChainStrip::paint (Graphics& g)
{
    const auto t = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    drawPanelBackground (g, r, t, "SIGNAL CHAIN");
    r = r.reduced (16.0f).withTrimmedTop (14.0f);

    const int n = blocks.size();
    const float gap = r.getWidth() * 0.025f;
    const float bw = (r.getWidth() - gap * (float) (n - 1)) / (float) n;
    const float bh = jmin (r.getHeight(), bw * 0.7f);
    const float y = r.getCentreY() - bh / 2;

    g.setColour (t.textDim);
    g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 2.0f);

    blockBounds.clearQuick();
    for (int i = 0; i < n; ++i)
    {
        const auto& b = blocks.getReference (i);
        Rectangle<float> box (r.getX() + (float) i * (bw + gap), y, bw, bh);
        blockBounds.add (box);

        bool enabled = true;
        if (b.paramId.isNotEmpty())
            if (auto* v = state.getRawParameterValue (b.paramId)) enabled = v->load() > 0.5f;

        if (b.live)
        {
            g.setGradientFill (ColourGradient ((enabled ? t.accentDeep : t.panel).brighter (0.1f), box.getX(), box.getY(),
                                               (enabled ? t.accentDeep : t.panel).darker (0.4f), box.getX(), box.getBottom(), false));
            g.fillRoundedRectangle (box, 6.0f);
            g.setColour (enabled ? t.accent : t.panelEdge);
            g.drawRoundedRectangle (box, 6.0f, 1.5f);
            g.setColour (enabled ? Colours::white : t.textDim);
        }
        else
        {
            g.setColour (t.panel.darker (0.2f));
            g.fillRoundedRectangle (box, 6.0f);
            Path dashed;
            dashed.addRoundedRectangle (box, 6.0f);
            const float dashes[] = { 4.0f, 4.0f };
            PathStrokeType (1.0f).createDashedStroke (dashed, dashed, dashes, 2);
            g.setColour (t.panelEdge);
            g.fillPath (dashed);
            g.setColour (t.textDim.withAlpha (0.6f));
        }

        g.setFont (ViridianLookAndFeel::labelFont (jmin (14.0f, bh * 0.28f)));
        g.drawText (b.live ? b.name : "+", box.withTrimmedBottom (b.live ? 0.0f : bh * 0.3f), Justification::centred);
        if (! b.live)
        {
            g.setFont (ViridianLookAndFeel::labelFont (9.0f, false));
            g.drawText ("PHASE 4", box.withTrimmedTop (bh * 0.6f), Justification::centred);
        }
    }
}

void SignalChainStrip::mouseUp (const MouseEvent& e)
{
    for (int i = 0; i < blockBounds.size() && i < blocks.size(); ++i)
    {
        const auto& b = blocks.getReference (i);
        if (b.paramId.isNotEmpty() && blockBounds[i].contains (e.position))
            if (auto* p = state.getParameter (b.paramId))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
                p->endChangeGesture();
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
