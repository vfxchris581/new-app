#include "CabRoom.h"
#include "vf/Parameters.h"

using namespace juce;
using namespace vf::dsp;

static VFTheme themeOf (Component& c)
{
    if (auto* l = dynamic_cast<ViridianLookAndFeel*> (&c.getLookAndFeel())) return l->getTheme();
    return VFTheme::dark();
}

static const Colour kMicColour[2] { Colour (0xff3ee07a), Colour (0xffffa12e) };

CabRoomView::CabRoomView (ViridianForgeProcessor& p, int cabIndex)
    : processor (p), state (p.getState()), index (cabIndex),
      typeA (p.getState(), ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::micType, "Type"),
      typeB (p.getState(), ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::micBType, "Type"),
      levelA (p.getState(), ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::micALevel, "Level"),
      levelB (p.getState(), ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::micBLevel, "Level"),
      roomKnob (p.getState(), ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::room, "Room"),
      roomDistKnob (p.getState(), ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::roomDist, "Room Dist"),
      micBOnSwitch (p.getState(), ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::micBOn, "On"),
      phaseB (p.getState(), ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::micBPhase, "Phase"),
      align (p.getState(), ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::micAlign, "Time Align"),
      stereo (p.getState(), ViridianForgeProcessor::cabPrefix (cabIndex) + vf::ParamID::cabStereo, "A L / B R")
{
    const String key = "cab" + String (index + 1) + "_mic";
    thetaKey[0] = key + "A_theta";
    thetaKey[1] = key + "B_theta";

    micALabel.setText ("MIC A", dontSendNotification);
    micBLabel.setText ("MIC B", dontSendNotification);
    roomLabel.setText ("ROOM", dontSendNotification);
    micALabel.setColour (Label::textColourId, kMicColour[0]);
    micBLabel.setColour (Label::textColourId, kMicColour[1]);
    for (auto* c : std::initializer_list<Component*> { &micALabel, &micBLabel, &roomLabel, &typeA, &typeB, &levelA, &levelB,
                                                       &roomKnob, &roomDistKnob, &micBOnSwitch, &phaseB, &align, &stereo })
        addAndMakeVisible (c);

    align.getButton().setTooltip ("Delay the closer mic so both arrive together: removes the comb filtering of two mics at different distances");
    phaseB.getButton().setTooltip ("Flip mic B's polarity");
    stereo.getButton().setTooltip ("Mic A to the left channel, mic B to the right (otherwise both are mixed)");
    roomDistKnob.getSlider().setTooltip ("How far back the room mics stand: later, sparser, darker reflections");
    setRepaintsOnMouseActivity (false);

    preview.prepare (48000.0);
    startTimerHz (20);
    timerCallback();
}

CabRoomView::~CabRoomView() { stopTimer(); }

String CabRoomView::pid (const char* id) const { return ViridianForgeProcessor::cabPrefix (index) + id; }

float CabRoomView::value (const char* id) const
{
    if (auto* v = state.getRawParameterValue (pid (id))) return v->load();
    return 0.0f;
}

void CabRoomView::setValue (const char* id, float plain)
{
    if (auto* p = state.getParameter (pid (id)))
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

CabModel CabRoomView::currentCab() const
{
    return static_cast<CabModel> (jlimit (0, (int) CabModel::count - 1, roundToInt (value (vf::ParamID::cabModel))));
}

bool CabRoomView::builtInSource() const { return roundToInt (value (vf::ParamID::cabSource)) == 0; }

CabRoomView::MicState CabRoomView::mic (int m) const
{
    using namespace vf::ParamID;
    const auto geo = getCabGeometry (currentCab());
    MicState s {};
    if (m == 0)
        s = { roundToInt (value (micASpeaker)), value (micPosition) / 10.0f, value (micDistance) / 10.0f, value (micAAngle), 0.0f, true, roundToInt (value (micType)) };
    else
        s = { roundToInt (value (micBSpeaker)), value (micBPos) / 10.0f, value (micBDist) / 10.0f, value (micBAngle), 0.0f, value (micBOn) > 0.5f, roundToInt (value (micBType)) };
    s.speaker = jlimit (0, geo.speakers - 1, s.speaker);
    s.theta = (float) state.state.getProperty (thetaKey[m], m == 0 ? -2.3 : 0.7);
    return s;
}

//==============================================================================
// Geometry: the front view shows the baffle at a uniform cm -> px scale.

float CabRoomView::pxPerCm() const
{
    const auto geo = getCabGeometry (currentCab());
    const float w = (float) geo.columns * geo.spacingCm + 14.0f;
    const float h = (float) (geo.speakers / geo.columns) * geo.spacingCm + 14.0f;
    return jmin (frontArea.getWidth() / jmax (w, 60.0f), (frontArea.getHeight() - 22.0f) / jmax (h, 50.0f)) * 0.86f;
}

Point<float> CabRoomView::baffleToScreen (float x, float y) const
{
    const float s = pxPerCm();
    return { frontArea.getCentreX() + x * s, frontArea.getCentreY() - 10.0f + y * s };
}

Point<float> CabRoomView::screenToBaffle (Point<float> p) const
{
    const float s = pxPerCm();
    return { (p.x - frontArea.getCentreX()) / s, (p.y - frontArea.getCentreY() + 10.0f) / s };
}

Point<float> CabRoomView::micFrontPos (int m) const
{
    const auto geo = getCabGeometry (currentCab());
    const auto st = mic (m);
    float cx, cy;
    geo.centre (st.speaker, cx, cy);
    const float r = st.position * geo.coneRadiusCm;
    return baffleToScreen (cx + r * std::cos (st.theta), cy + r * std::sin (st.theta));
}

Point<float> CabRoomView::micSidePos (int m) const
{
    const auto st = mic (m);
    const auto inner = sideArea.reduced (14.0f, 26.0f);
    const float grilleX = inner.getX() + 34.0f;
    const float x = grilleX + 10.0f + st.distance * (inner.getRight() - grilleX - 40.0f);
    const float coneR = inner.getHeight() * 0.36f;
    const float y = inner.getCentreY() + st.position * coneR * std::sin (st.theta);
    return { x, y };
}

int CabRoomView::micAtFront (Point<float> p) const
{
    for (int m : { selectedMic, 1 - selectedMic })
        if (micFrontPos (m).getDistanceFrom (p) < 16.0f) return m;
    return -1;
}

int CabRoomView::micAtSide (Point<float> p) const
{
    for (int m : { selectedMic, 1 - selectedMic })
    {
        const auto c = micSidePos (m);
        if (Rectangle<float> (c.x - 6.0f, c.y - 14.0f, 70.0f, 28.0f).contains (p)) return m;
    }
    return -1;
}

void CabRoomView::placeMicFront (int m, Point<float> screen)
{
    const auto geo = getCabGeometry (currentCab());
    const auto b = screenToBaffle (screen);
    int best = 0;
    float bestD = 1.0e9f;
    for (int k = 0; k < geo.speakers; ++k)
    {
        float cx, cy;
        geo.centre (k, cx, cy);
        const float d = std::hypot (b.x - cx, b.y - cy);
        if (d < bestD) { bestD = d; best = k; }
    }
    float cx, cy;
    geo.centre (best, cx, cy);
    const float r = jlimit (0.0f, 1.0f, bestD / geo.coneRadiusCm);
    const float theta = std::atan2 (b.y - cy, b.x - cx);
    using namespace vf::ParamID;
    setValue (m == 0 ? micASpeaker : micBSpeaker, (float) best);
    setValue (m == 0 ? micPosition : micBPos, r * 10.0f);
    if (r > 0.02f) state.state.setProperty (thetaKey[m], theta, nullptr);
    if (m == 1 && value (micBOn) < 0.5f) setValue (micBOn, 1.0f); // grabbing mic B puts it up
}

void CabRoomView::placeMicSide (int m, Point<float> screen)
{
    const auto inner = sideArea.reduced (14.0f, 26.0f);
    const float grilleX = inner.getX() + 34.0f;
    const float d = jlimit (0.0f, 1.0f, (screen.x - grilleX - 10.0f) / (inner.getRight() - grilleX - 40.0f));
    const float angle = jlimit (0.0f, kMaxMicAngleDeg, std::abs (screen.y - inner.getCentreY()) / (inner.getHeight() * 0.5f) * kMaxMicAngleDeg);
    using namespace vf::ParamID;
    setValue (m == 0 ? micDistance : micBDist, d * 10.0f);
    setValue (m == 0 ? micAAngle : micBAngle, angle);
    if (m == 1 && value (micBOn) < 0.5f) setValue (micBOn, 1.0f);
}

//==============================================================================
void CabRoomView::mouseMove (const MouseEvent& e)
{
    const bool overMic = (frontArea.contains (e.position) && micAtFront (e.position) >= 0) || (sideArea.contains (e.position) && micAtSide (e.position) >= 0);
    setMouseCursor (overMic ? MouseCursor::DraggingHandCursor : (frontArea.contains (e.position) ? MouseCursor::CrosshairCursor : MouseCursor::NormalCursor));
}

void CabRoomView::mouseDown (const MouseEvent& e)
{
    dragKind = DragKind::None;
    dragMic = -1;
    if (! builtInSource()) return;
    using namespace vf::ParamID;
    if (frontArea.contains (e.position))
    {
        int m = micAtFront (e.position);
        if (m < 0) m = selectedMic;            // click on the baffle: move the selected mic there
        selectedMic = m;
        dragMic = m;
        dragKind = DragKind::Front;
        for (const char* id : { micASpeaker, micPosition, micBSpeaker, micBPos, micBOn })
            if (auto* p = state.getParameter (pid (id))) p->beginChangeGesture();
        placeMicFront (m, e.position);
    }
    else if (sideArea.contains (e.position))
    {
        int m = micAtSide (e.position);
        if (m < 0) m = selectedMic;
        selectedMic = m;
        dragMic = m;
        dragKind = DragKind::Side;
        for (const char* id : { micDistance, micAAngle, micBDist, micBAngle, micBOn })
            if (auto* p = state.getParameter (pid (id))) p->beginChangeGesture();
        placeMicSide (m, e.position);
    }
    repaint();
}

void CabRoomView::mouseDrag (const MouseEvent& e)
{
    if (dragMic < 0) return;
    if (dragKind == DragKind::Front) placeMicFront (dragMic, e.position);
    else if (dragKind == DragKind::Side) placeMicSide (dragMic, e.position);
    repaint();
}

void CabRoomView::mouseUp (const MouseEvent&)
{
    using namespace vf::ParamID;
    if (dragKind != DragKind::None)
        for (const char* id : { micASpeaker, micPosition, micBSpeaker, micBPos, micBOn, micDistance, micAAngle, micBDist, micBAngle })
            if (auto* p = state.getParameter (pid (id))) p->endChangeGesture();
    dragKind = DragKind::None;
    dragMic = -1;
}

void CabRoomView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (! builtInSource() || ! frontArea.contains (e.position)) return;
    int m = micAtFront (e.position);
    if (m < 0) m = selectedMic;
    selectedMic = m;
    const char* id = m == 0 ? vf::ParamID::micDistance : vf::ParamID::micBDist;
    if (auto* p = state.getParameter (pid (id)))
    {
        p->beginChangeGesture();
        setValue (id, jlimit (0.0f, 10.0f, value (id) - w.deltaY * 2.0f));
        p->endChangeGesture();
    }
    repaint();
}

//==============================================================================
void CabRoomView::timerCallback()
{
    using namespace vf::ParamID;
    String sig;
    for (const char* id : { cabModel, micType, micASpeaker, micPosition, micDistance, micAAngle, micALevel, micBOn, micBSpeaker,
                            micBPos, micBDist, micBAngle, micBType, micBLevel, micBPhase, micAlign, cabSource, room, roomDist })
        sig << String (value (id), 3) << ";";
    sig << state.state.getProperty (thetaKey[0]).toString() << state.state.getProperty (thetaKey[1]).toString();
    if (sig == lastSignature) return;
    lastSignature = sig;

    // Mirror the audio settings and recompute the response curves.
    auto settingsFor = [this] (int m)
    {
        const auto st = mic (m);
        MicSettings s;
        s.enabled = st.on;
        s.type = static_cast<MicType> (jlimit (0, 2, st.type));
        s.speaker = st.speaker;
        s.position = st.position;
        s.distance = st.distance;
        s.angle = st.angle;
        s.levelDb = value (m == 0 ? micALevel : micBLevel);
        s.invert = m == 1 && value (micBPhase) > 0.5f;
        return s;
    };
    preview.set (currentCab(), settingsFor (0), settingsFor (1), value (micAlign) > 0.5f);
    const int n = 160;
    curveA.resize ((size_t) n); curveB.resize ((size_t) n); curveMix.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double f = 50.0 * std::pow (12000.0 / 50.0, (double) i / (n - 1));
        auto dB = [] (double m) { return (float) (20.0 * std::log10 (std::max (m, 1.0e-6))); };
        curveA[(size_t) i] = dB (std::abs (preview.responseAt (0, f)));
        curveB[(size_t) i] = dB (std::abs (preview.responseAt (1, f)));
        curveMix[(size_t) i] = dB (std::abs (preview.monoResponseAt (f)));
    }
    repaint();
}

//==============================================================================
void CabRoomView::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    r.removeFromTop (26);
    auto left = r.removeFromLeft (r.getWidth() * 56 / 100);
    r.removeFromLeft (12);
    responseArea = left.removeFromBottom (left.getHeight() * 28 / 100).toFloat();
    left.removeFromBottom (8);
    frontArea = left.toFloat();

    sideArea = r.removeFromTop (r.getHeight() * 40 / 100).toFloat();
    r.removeFromTop (8);

    // Controls: two mic rows + room/alignment row
    const int rowH = r.getHeight() / 3;
    auto rowA = r.removeFromTop (rowH), rowB = r.removeFromTop (rowH), rowC = r;
    auto layoutMic = [] (Rectangle<int> row, Label& label, Component* on, ChoiceBox& type, LabeledKnob& level, Component* phase)
    {
        label.setBounds (row.removeFromLeft (56).withSizeKeepingCentre (56, 20));
        if (on != nullptr) on->setBounds (row.removeFromLeft (50).withSizeKeepingCentre (48, 40));
        else row.removeFromLeft (50);
        const int typeW = row.getWidth() * 50 / 100;
        type.setBounds (row.removeFromLeft (typeW).withSizeKeepingCentre (typeW - 6, 46));
        level.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2));
        if (phase != nullptr) phase->setBounds (row.withSizeKeepingCentre (jmin (64, row.getWidth()), 40));
    };
    layoutMic (rowA, micALabel, nullptr, typeA, levelA, nullptr);
    layoutMic (rowB, micBLabel, &micBOnSwitch, typeB, levelB, &phaseB);
    roomLabel.setBounds (rowC.removeFromLeft (56).withSizeKeepingCentre (56, 30));
    roomLabel.setJustificationType (Justification::centredLeft);
    const int w = rowC.getWidth() / 4;
    align.setBounds (rowC.removeFromLeft (w).withSizeKeepingCentre (jmin (w - 4, 92), 40));
    stereo.setBounds (rowC.removeFromLeft (w).withSizeKeepingCentre (jmin (w - 4, 92), 40));
    roomKnob.setBounds (rowC.removeFromLeft (w).reduced (2));
    roomDistKnob.setBounds (rowC.reduced (2));
}

void CabRoomView::paint (Graphics& g)
{
    const auto t = themeOf (*this);
    g.setColour (t.panel.darker (0.1f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);

    g.setColour (t.textDim);
    g.setFont (ViridianLookAndFeel::labelFont (11.0f));
    const String title = "CABINET " + String (index + 1) + "  -  " + String (cabModelNames[(size_t) currentCab()]).toUpperCase() + "  -  MIC PLACEMENT";
    g.drawText (title, getLocalBounds().reduced (14, 10).removeFromTop (20), Justification::centredLeft);
    g.setFont (ViridianLookAndFeel::labelFont (10.0f, false));
    g.drawText ("Drag mics on the speakers | wheel = distance | side view: drag left/right = distance, up/down = angle",
                getLocalBounds().reduced (14, 10).removeFromTop (20), Justification::centredRight);

    drawFront (g, t);
    drawSide (g, t);
    drawResponse (g, t);

    if (! builtInSource())
    {
        auto a = frontArea.getUnion (sideArea).getUnion (responseArea).expanded (6.0f);
        g.setColour (t.background.withAlpha (0.78f));
        g.fillRoundedRectangle (a, 8.0f);
        g.setColour (t.text);
        g.setFont (ViridianLookAndFeel::labelFont (14.0f));
        g.drawFittedText ("This cab is playing impulse responses.\nIRs carry the mic placement they were recorded with.\n"
                          "Set the cab source to BUILT-IN to place mics.", a.reduced (30.0f).toNearestInt(), Justification::centred, 4);
    }
}

void CabRoomView::drawMicTop (Graphics& g, Point<float> c, float size, Colour colour, const String& label, bool selected, bool on, float tiltDeg, float theta)
{
    const float alpha = on ? 1.0f : 0.35f;
    // aim line: an angled mic points across the cone; show which way and how far
    if (tiltDeg > 0.5f)
    {
        const float len = size * 0.9f + size * 1.6f * tiltDeg / kMaxMicAngleDeg;
        g.setColour (colour.withAlpha (0.6f * alpha));
        const Point<float> dir (-std::cos (theta), -std::sin (theta));
        g.drawLine (Line<float> (c, c + dir * len), 2.0f);
    }
    g.setColour (Colours::black.withAlpha (0.5f * alpha));
    g.fillEllipse (Rectangle<float> (size * 1.1f, size * 1.1f).withCentre (c.translated (1.5f, 2.5f)));
    g.setGradientFill (ColourGradient (Colour (0xffe6e6e6).withAlpha (alpha), c.x - size * 0.4f, c.y - size * 0.4f,
                                       Colour (0xff5a5a5a).withAlpha (alpha), c.x + size * 0.5f, c.y + size * 0.5f, true));
    g.fillEllipse (Rectangle<float> (size, size).withCentre (c));
    // grille mesh
    g.setColour (Colours::black.withAlpha (0.25f * alpha));
    for (float k = -0.3f; k <= 0.31f; k += 0.15f)
    {
        g.drawLine (c.x + k * size, c.y - size * 0.42f, c.x + k * size, c.y + size * 0.42f, 0.7f);
        g.drawLine (c.x - size * 0.42f, c.y + k * size, c.x + size * 0.42f, c.y + k * size, 0.7f);
    }
    g.setColour (colour.withAlpha (alpha));
    g.drawEllipse (Rectangle<float> (size, size).withCentre (c), selected ? 3.0f : 2.0f);
    if (selected)
    {
        g.setColour (colour.withAlpha (0.35f));
        g.drawEllipse (Rectangle<float> (size + 10.0f, size + 10.0f).withCentre (c), 1.5f);
    }
    g.setColour (Colours::black.withAlpha (0.7f));
    g.fillRoundedRectangle (Rectangle<float> (18.0f, 14.0f).withCentre (c.translated (size * 0.62f, -size * 0.62f)), 3.0f);
    g.setColour (colour.withAlpha (alpha));
    g.setFont (ViridianLookAndFeel::labelFont (10.0f));
    g.drawText (label, Rectangle<float> (18.0f, 14.0f).withCentre (c.translated (size * 0.62f, -size * 0.62f)), Justification::centred);
}

void CabRoomView::drawFront (Graphics& g, const VFTheme& t)
{
    const auto geo = getCabGeometry (currentCab());
    const float s = pxPerCm();
    const int rows = geo.speakers / geo.columns;
    const float wCm = (float) geo.columns * geo.spacingCm + 12.0f, hCm = (float) rows * geo.spacingCm + 12.0f;
    auto baffle = Rectangle<float> (wCm * s, hCm * s).withCentre (frontArea.getCentre().translated (0.0f, -10.0f));
    auto cabinet = baffle.expanded (10.0f * s / 3.0f + 6.0f);

    // tolex box + piping
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (cabinet.translated (0.0f, 5.0f), 10.0f);
    g.setColour (t.tolex);
    g.fillRoundedRectangle (cabinet, 10.0f);
    g.setColour (Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (cabinet.reduced (4.0f), 8.0f, 1.0f);

    // grille cloth (drawn semi-transparent: we are looking "through" it at the speakers)
    g.setColour (t.grille.darker (0.35f));
    g.fillRoundedRectangle (baffle, 4.0f);
    g.setColour (Colours::black.withAlpha (0.08f));
    for (float y = baffle.getY(); y < baffle.getBottom(); y += 3.0f) g.drawHorizontalLine ((int) y, baffle.getX(), baffle.getRight());

    for (int k = 0; k < geo.speakers; ++k)
    {
        float cx, cy;
        geo.centre (k, cx, cy);
        const auto c = baffleToScreen (cx, cy);
        const float R = geo.coneRadiusCm * s;
        // frame / surround / cone / dust cap
        g.setColour (Colour (0xff1c1c1c));
        g.fillEllipse (Rectangle<float> (R * 2.3f, R * 2.3f).withCentre (c));
        g.setGradientFill (ColourGradient (Colour (0xff3b3b3b), c.x, c.y, Colour (0xff141414), c.x + R, c.y + R, true));
        g.fillEllipse (Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c));
        g.setColour (Colours::white.withAlpha (0.06f));
        for (float rr = 0.45f; rr < 1.0f; rr += 0.18f)
            g.drawEllipse (Rectangle<float> (R * 2.0f * rr, R * 2.0f * rr).withCentre (c), 1.0f);
        g.setGradientFill (ColourGradient (Colour (0xff5a5a5a), c.x - R * 0.12f, c.y - R * 0.12f, Colour (0xff202020), c.x + R * 0.2f, c.y + R * 0.2f, true));
        g.fillEllipse (Rectangle<float> (R * 0.62f, R * 0.62f).withCentre (c));
        g.setColour (t.textDim.withAlpha (0.8f));
        g.setFont (ViridianLookAndFeel::labelFont (10.0f, false));
        if (geo.speakers > 1)
            g.drawText (String (k + 1), Rectangle<float> (20.0f, 14.0f).withCentre (c.translated (-R * 0.92f, -R * 0.92f)), Justification::centred);
    }

    // mics (unselected first so the selected one draws on top)
    for (int m : { 1 - selectedMic, selectedMic })
    {
        const auto st = mic (m);
        const float size = jlimit (18.0f, 34.0f, 4.2f * s) * (1.0f + 0.35f * st.distance); // further back = "higher" in top view
        drawMicTop (g, micFrontPos (m), size, kMicColour[m], m == 0 ? "A" : "B", m == selectedMic, st.on, st.angle, st.theta);
    }

    // readout
    const auto st = mic (selectedMic);
    const String where = st.position < 0.12f ? "dust cap" : (st.position < 0.45f ? "cap edge" : (st.position < 0.8f ? "mid cone" : "cone edge"));
    String text = String ("MIC ") + (selectedMic == 0 ? "A" : "B") + ": " + (geo.speakers > 1 ? "speaker " + String (st.speaker + 1) + ", " : String())
                + where + " (" + String (roundToInt (st.position * 100.0f)) + "%), "
                + String (st.distance * kMaxMicDistanceCm, 1) + " cm, " + String (roundToInt (st.angle)) + " deg off-axis";
    if (selectedMic == 1 && ! st.on) text << "  (off)";
    g.setColour (kMicColour[selectedMic]);
    g.setFont (ViridianLookAndFeel::labelFont (11.0f));
    g.drawText (text, frontArea.withTop (frontArea.getBottom() - 16.0f), Justification::centred);
}

void CabRoomView::drawSide (Graphics& g, const VFTheme& t)
{
    g.setColour (t.panel.darker (0.3f));
    g.fillRoundedRectangle (sideArea, 8.0f);
    g.setColour (t.textDim);
    g.setFont (ViridianLookAndFeel::labelFont (10.0f));
    g.drawText ("SIDE VIEW", sideArea.reduced (10.0f, 6.0f).removeFromTop (12.0f), Justification::centredLeft);

    const auto inner = sideArea.reduced (14.0f, 26.0f);
    const float grilleX = inner.getX() + 34.0f;
    const float coneR = inner.getHeight() * 0.36f;
    const float cy = inner.getCentreY();

    // cabinet side + speaker cone profile + grille
    g.setColour (t.tolex);
    g.fillRect (Rectangle<float> (inner.getX(), inner.getY(), grilleX - inner.getX() - 4.0f, inner.getHeight()));
    Path cone;
    cone.startNewSubPath (grilleX - 4.0f, cy - coneR);
    cone.lineTo (grilleX - 22.0f, cy - coneR * 0.25f);
    cone.lineTo (grilleX - 22.0f, cy + coneR * 0.25f);
    cone.lineTo (grilleX - 4.0f, cy + coneR);
    g.setColour (Colour (0xff2a2a2a));
    g.strokePath (cone, PathStrokeType (3.0f));
    g.setColour (t.grille);
    g.fillRect (Rectangle<float> (grilleX - 3.0f, inner.getY(), 3.0f, inner.getHeight()));

    // distance scale
    g.setColour (t.textDim.withAlpha (0.6f));
    g.setFont (ViridianLookAndFeel::labelFont (9.0f, false));
    const float span = inner.getRight() - grilleX - 40.0f;
    for (int cm = 0; cm <= 30; cm += 5)
    {
        const float x = grilleX + 10.0f + span * (float) cm / kMaxMicDistanceCm;
        g.drawVerticalLine ((int) x, inner.getBottom() + 2.0f, inner.getBottom() + 6.0f);
        g.drawText (String (cm) + (cm == 30 ? " cm" : ""), Rectangle<float> (x - 18.0f, inner.getBottom() + 6.0f, 36.0f, 12.0f), Justification::centred);
    }

    for (int m : { 1 - selectedMic, selectedMic })
    {
        const auto st = mic (m);
        const auto p = micSidePos (m);
        const float alpha = st.on ? 1.0f : 0.35f;
        // the mic: capsule at the front (left), body behind it, tilted by its angle
        const float a = degreesToRadians (st.angle) * (st.theta < 0.0f ? 1.0f : -1.0f);
        Path body;
        body.addRoundedRectangle (0.0f, -7.0f, 56.0f, 14.0f, 6.0f);
        body.addEllipse (-4.0f, -9.0f, 18.0f, 18.0f);
        const auto xf = AffineTransform::rotation (a).translated (p.x, p.y);
        g.setColour (Colours::black.withAlpha (0.4f * alpha));
        g.fillPath (body, xf.translated (1.5f, 2.5f));
        g.setGradientFill (ColourGradient (Colour (0xffcfcfcf).withAlpha (alpha), p.x, p.y - 8.0f, Colour (0xff4a4a4a).withAlpha (alpha), p.x, p.y + 8.0f, false));
        g.fillPath (body, xf);
        g.setColour (kMicColour[m].withAlpha (alpha));
        g.strokePath (body, PathStrokeType (m == selectedMic ? 2.2f : 1.4f), xf);
        // axis line toward the speaker
        g.setColour (kMicColour[m].withAlpha (0.4f * alpha));
        const Point<float> dir (-std::cos (a), -std::sin (a));
        g.drawLine (Line<float> (p, p + dir * jmax (8.0f, p.x - grilleX)), 1.0f);
        g.setFont (ViridianLookAndFeel::labelFont (10.0f));
        g.drawText (m == 0 ? "A" : "B", Rectangle<float> (16.0f, 12.0f).withCentre (p.translated (30.0f, -16.0f)), Justification::centred);
    }
}

void CabRoomView::drawResponse (Graphics& g, const VFTheme& t)
{
    g.setColour (t.panel.darker (0.3f));
    g.fillRoundedRectangle (responseArea, 8.0f);
    g.setColour (t.textDim);
    g.setFont (ViridianLookAndFeel::labelFont (10.0f));
    g.drawText ("RESPONSE", responseArea.reduced (10.0f, 6.0f).removeFromTop (12.0f), Justification::centredLeft);
    if (curveMix.empty()) return;

    auto plot = responseArea.reduced (12.0f, 8.0f).withTrimmedTop (12.0f).withTrimmedBottom (10.0f);
    const float top = *std::max_element (curveMix.begin(), curveMix.end()) + 3.0f, range = 36.0f;
    g.setFont (ViridianLookAndFeel::labelFont (9.0f, false));
    for (double f : { 100.0, 1000.0, 10000.0 })
    {
        const float x = plot.getX() + plot.getWidth() * (float) (std::log (f / 50.0) / std::log (12000.0 / 50.0));
        g.setColour (t.panelEdge.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (t.textDim);
        g.drawText (f >= 1000.0 ? String ((int) (f / 1000.0)) + "k" : String ((int) f), Rectangle<float> (x - 14.0f, plot.getBottom(), 28.0f, 10.0f), Justification::centred);
    }
    auto curve = [&] (const std::vector<float>& c, Colour col, float width)
    {
        Path p;
        for (size_t i = 0; i < c.size(); ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (c.size() - 1);
            const float y = plot.getY() + plot.getHeight() * jlimit (0.0f, 1.0f, (top - c[i]) / range);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (col);
        g.strokePath (p, PathStrokeType (width));
    };
    const bool bOn = mic (1).on;
    if (bOn)
    {
        curve (curveA, kMicColour[0].withAlpha (0.55f), 1.2f);
        curve (curveB, kMicColour[1].withAlpha (0.55f), 1.2f);
    }
    curve (curveMix, bOn ? Colours::white.withAlpha (0.9f) : kMicColour[0], 2.0f);
    g.setColour (t.textDim);
    g.drawText (bOn ? "A + B mix (white)" : "Mic A", plot.removeFromTop (12.0f), Justification::centredRight);
}
