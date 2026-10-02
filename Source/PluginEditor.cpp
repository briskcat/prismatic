#include "PluginEditor.h"
#include "ui/Sketches.h"

using namespace prism;
using namespace prism::ui;

namespace
{
// logical layout, in CSS-like px at 100% scale
constexpr int kW = 1280, kH = 548;
constexpr int kPadTop = 10, kPadSide = 20, kPadBottom = 16;
constexpr int kStatusH = 30, kHeaderH = 64, kChainH = 40, kRow1H = 140;
constexpr int kCell = 72; // one knob
constexpr int kModPadTop = 10, kModPadSide = 14, kModPadBottom = 12, kModHeaderH = 26, kLineGap = 8;
constexpr int kCollapsedW = 36;
constexpr int kKnobD = 46, kHeaderKnobD = 36;

/** A 1px rule on whole physical pixels */
void Rule(juce::Graphics& g, float x, float y, float w, float h)
{
    const float s    = g.getInternalContext().getPhysicalPixelScaleFactor();
    auto        snap = [s](float v) { return std::round(v * s) / s; };
    const float px   = juce::jmax(1.f, std::round(s)) / s; // whole device pixels, about 1 px at any scale
    if(w > h)
        g.fillRect(snap(x), snap(y), snap(x + w) - snap(x), px);
    else
        g.fillRect(snap(x), snap(y), px, snap(y + h) - snap(y));
}

const char* BlockLabel(Block b)
{
    switch(b)
    {
        case Block::Filter: return "Filter";
        case Block::Drive: return "Drive";
        case Block::Tape: return "Tape";
        case Block::Crush: return "Crush";
        case Block::Delay: return "Delay";
        case Block::Glitch: return "Glitch";
        case Block::Reverb: return "Reverb";
        case Block::Looper: return "Looper";
    }
    return "";
}

const char* OnParam(Block b)
{
    switch(b)
    {
        case Block::Filter: return ids::filterOn;
        case Block::Drive: return ids::driveOn;
        case Block::Tape: return ids::tapeOn;
        case Block::Crush: return ids::crushOn;
        case Block::Delay: return ids::delayOn;
        case Block::Glitch: return ids::glitchOn;
        case Block::Reverb: return ids::reverbOn;
        case Block::Looper: return nullptr;
    }
    return nullptr;
}

/** Text with extra tracking (letter spacing) in px */
void DrawTracked(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float tracking,
                 juce::Justification j)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(g.getCurrentFont(), text, 0.f, 0.f);
    for(int i = 0; i < ga.getNumGlyphs(); ++i)
        ga.getGlyph(i).moveBy(tracking * (float)i, 0.f);
    const auto bb = ga.getBoundingBox(0, -1, true);
    float      x  = r.getX();
    if(j.testFlags(juce::Justification::horizontallyCentred))
        x = r.getCentreX() - bb.getWidth() * .5f;
    else if(j.testFlags(juce::Justification::right))
        x = r.getRight() - bb.getWidth();
    const auto  f = g.getCurrentFont();
    float       y = r.getCentreY() + (f.getAscent() - f.getDescent()) * .5f;
    if(j.testFlags(juce::Justification::bottom))
        y = r.getBottom() - f.getDescent();
    else if(j.testFlags(juce::Justification::top))
        y = r.getY() + f.getAscent();
    ga.moveRangeOfGlyphs(0, -1, x - bb.getX(), y);
    ga.draw(g);
}

float TrackedWidth(const juce::Font& f, const juce::String& text, float tracking)
{
    return juce::GlyphArrangement::getStringWidth(f, text) + tracking * (float)juce::jmax(0, text.length() - 1);
}
} // namespace

// ---------------------------------------------------------------- Look

Look::Look() { Recolour(); }

void Look::Recolour()
{
    setColour(juce::Slider::textBoxTextColourId, Ink());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, Ink());
    setColour(juce::ComboBox::textColourId, Ink());
    setColour(juce::PopupMenu::backgroundColourId, Bg());
    setColour(juce::PopupMenu::textColourId, Ink());
    setColour(juce::PopupMenu::headerTextColourId, Muted());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Ink().withAlpha(.08f));
    setColour(juce::PopupMenu::highlightedTextColourId, Accent());
    setColour(juce::TextEditor::backgroundColourId, Bg());
    setColour(juce::TextEditor::textColourId, Ink());
    setColour(juce::TextEditor::highlightColourId, Accent().withAlpha(.35f));
    setColour(juce::CaretComponent::caretColourId, Ink());
    setColour(juce::TooltipWindow::backgroundColourId, Bg());
    setColour(juce::TooltipWindow::textColourId, Ink());
    setColour(juce::TooltipWindow::outlineColourId, Ink());
}

void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                            juce::Slider& s)
{
    // geometry from a 48-unit box: track and value arc at r 20, face at r 14
    const float size   = (float)juce::jmin(w, h);
    const float u      = size / 48.f;
    const auto  c      = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).getCentre();
    const float alpha  = s.isEnabled() ? 1.f : .35f;
    const bool  header = s.getProperties()["header"];
    const float arcW   = (header ? 5.f : 4.f) * u;

    juce::Path track;
    track.addCentredArc(c.x, c.y, 20.f * u, 20.f * u, 0.f, start, end, true);
    g.setColour(T().track.withMultipliedAlpha(alpha));
    g.strokePath(track, juce::PathStrokeType(1.f));

    const bool  bipolar = s.getProperties()["bipolar"];
    const float angle   = start + pos * (end - start);
    const float from    = bipolar ? 0.f : start;
    if(std::abs(angle - from) > .01f)
    {
        juce::Path value;
        value.addCentredArc(c.x, c.y, 20.f * u, 20.f * u, 0.f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        g.setColour(Accent().withMultipliedAlpha(alpha));
        g.strokePath(value, juce::PathStrokeType(arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour(Ink().withMultipliedAlpha(alpha));
    g.drawEllipse(c.x - 14.f * u, c.y - 14.f * u, 28.f * u, 28.f * u, 1.f);
    juce::Path pointer;
    pointer.startNewSubPath(c);
    pointer.lineTo(c.x + 14.f * u * std::sin(angle), c.y - 14.f * u * std::cos(angle));
    g.strokePath(pointer, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Slider::SliderLayout Look::getSliderLayout(juce::Slider& s)
{
    juce::Slider::SliderLayout l;
    auto                       b = s.getLocalBounds();
    if(s.getTextBoxPosition() == juce::Slider::TextBoxBelow)
    {
        l.textBoxBounds = b.removeFromBottom(16);
        b.removeFromBottom(2);
    }
    l.sliderBounds = b;
    return l;
}

void Look::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto kind = (int)b.getProperties()["kind"];
    if(kind == (int)Pill::Kind::Text)
        return;
    const float alpha  = b.isEnabled() ? 1.f : .4f;
    const auto  r      = b.getLocalBounds().toFloat().withSizeKeepingCentre((float)b.getWidth() - 1.f, 22.f).reduced(.5f);
    const float rad    = r.getHeight() * .5f;
    const bool  action = kind == (int)Pill::Kind::Action;
    const bool  filled = action ? (down || b.getToggleState()) : b.getToggleState();
    const auto  colour = action ? Red() : (b.getToggleState() ? Accent() : Ink());

    if(filled)
    {
        g.setColour(colour.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(r, rad);
    }
    else if(over)
    {
        g.setColour(Ink().withAlpha(.08f));
        g.fillRoundedRectangle(r, rad);
    }
    g.setColour(colour.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(r, rad, 1.f);
}

void Look::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    const auto  kind  = (int)b.getProperties()["kind"];
    const float alpha = b.isEnabled() ? 1.f : .4f;
    if(kind == (int)Pill::Kind::Text)
    {
        g.setFont(Fonts::Serif(22.f));
        g.setColour((over ? Muted() : Ink()).withMultipliedAlpha(alpha));
        g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centredRight);
        return;
    }
    const bool  action = kind == (int)Pill::Kind::Action;
    const bool  filled = action ? (down || b.getToggleState()) : b.getToggleState();
    const auto  ink    = filled ? Bg() : (action ? Red() : Ink());
    const auto  font   = Fonts::Caps(11.f, true);
    const auto  text   = b.getButtonText().toUpperCase();
    const bool  dot    = b.getProperties()["dot"];
    const float tw     = TrackedWidth(font, text, .8f) + (dot ? 12.f : 0.f);
    auto        r      = b.getLocalBounds().toFloat().withSizeKeepingCentre(tw, (float)b.getHeight());
    if(dot)
    {
        g.setColour((filled ? Bg() : Red()).withMultipliedAlpha(alpha));
        g.fillEllipse(r.getX(), r.getCentreY() - 3.5f, 7.f, 7.f);
        r.removeFromLeft(12.f);
    }
    g.setFont(font);
    g.setColour(ink.withMultipliedAlpha(alpha));
    DrawTracked(g, text, r, .8f, juce::Justification::centredLeft);
}

void Look::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const float alpha = box.isEnabled() ? 1.f : .4f;
    g.setColour(Ink().withMultipliedAlpha(alpha));
    Rule(g, 0.f, (float)h - 1.f, (float)w, 1.f);
    juce::Path  ch;
    const float cx = (float)w - 7.f, cy = h * .5f;
    ch.startNewSubPath(cx - 4.f, cy - 2.f);
    ch.lineTo(cx, cy + 2.f);
    ch.lineTo(cx + 4.f, cy - 2.f);
    g.strokePath(ch, juce::PathStrokeType(1.2f));
}

void Look::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(0, 0, box.getWidth() - 18, box.getHeight() - 2);
    label.setBorderSize({0, 0, 0, 0});
    label.setFont(getComboBoxFont(box));
}

juce::Font Look::getComboBoxFont(juce::ComboBox&) { return Fonts::Serif(19.f); }
juce::Font Look::getPopupMenuFont() { return Fonts::Serif(17.f); }

void Look::drawPopupMenuBackgroundWithOptions(juce::Graphics& g, int w, int h, const juce::PopupMenu::Options&)
{
    g.fillAll(Bg());
    g.setColour(Ink());
    g.drawRect(0, 0, w, h, 1);
}

void Look::drawPopupMenuSectionHeaderWithOptions(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                 const juce::String& text, const juce::PopupMenu::Options&)
{
    // section headers in the same mono caps as the knob labels
    g.setColour(Muted());
    g.setFont(Fonts::Caps(11.f));
    DrawTracked(g, text.toUpperCase(), area.toFloat().withTrimmedLeft(12.f).withTrimmedTop(6.f), .8f,
                juce::Justification::centredLeft);
}

juce::Font Look::getLabelFont(juce::Label& label)
{
    if(dynamic_cast<juce::Slider*>(label.getParentComponent()) != nullptr)
        return Fonts::Mono(13.f, true); // values: fixed-width, so they don't jitter while dragging
    if(dynamic_cast<juce::ComboBox*>(label.getParentComponent()) != nullptr)
        return Fonts::Serif(19.f);
    return label.getFont();
}

juce::Label* Look::createSliderTextBox(juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(Fonts::Mono(13.f, true));
    l->setJustificationType(juce::Justification::centred);
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

// ---------------------------------------------------------------- knob

PrismKnob::PrismKnob(APVTS& apvts, const char* id, const juce::String& name, bool header) : header_(header)
{
    setName(id);
    slider_.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider_.setTextBoxStyle(header ? juce::Slider::NoTextBox : juce::Slider::TextBoxBelow, true, kCell, 16);
    slider_.setRotaryParameters(juce::degreesToRadians(-135.f), juce::degreesToRadians(135.f), true);
    slider_.getProperties().set("header", header);
    slider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    // shift or cmd-drag for fine control; the wheel works by default
    slider_.setVelocityModeParameters(.4, 1, 0., true, (juce::ModifierKeys::Flags)(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::commandModifier));
    auto* p = apvts.getParameter(id);
    // only controls with a real centre fill outwards from noon: the filter (low-pass / high-pass)
    // and the gains (cut / boost). Everything else fills from the left.
    const auto pid = juce::String(id);
    slider_.getProperties().set("bipolar", pid == ids::cutoff || pid == ids::input || pid == ids::output);
    addAndMakeVisible(slider_);

    if(!header)
    {
        name_.setText(name.toLowerCase(), juce::dontSendNotification);
        name_.setFont(Fonts::Serif(17.f));
        name_.setJustificationType(juce::Justification::centred);
        name_.setMinimumHorizontalScale(.7f);
        name_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(name_);
    }
    headerLabel_ = name.toUpperCase();

    attach_ = std::make_unique<APVTS::SliderAttachment>(apvts, id, slider_);
    if(p != nullptr)
        slider_.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));

    // the value turns accent while dragging
    slider_.onDragStart = [this] {
        dragging_ = true;
        Recolour();
    };
    slider_.onDragEnd = [this] {
        dragging_ = false;
        Recolour();
    };
    Recolour();
    slider_.onValueChange = [this] {
        if(header_)
            repaint();
    };
}

void PrismKnob::Recolour()
{
    // set explicitly: the value box keeps whatever colours it was created with otherwise
    slider_.setColour(juce::Slider::textBoxTextColourId, dragging_ ? Accent() : Ink());
    name_.setColour(juce::Label::textColourId, Ink());
    repaint();
}

void PrismKnob::SetHeaderLabel(const juce::String& s)
{
    if(s != headerLabel_)
    {
        headerLabel_ = s;
        repaint();
    }
}

void PrismKnob::resized()
{
    auto b = getLocalBounds();
    if(header_)
    {
        slider_.setBounds(b.removeFromLeft(kHeaderKnobD).withSizeKeepingCentre(kHeaderKnobD, kHeaderKnobD));
        return;
    }
    // label, dial and value as one group, centred in the cell
    b = b.withSizeKeepingCentre(b.getWidth(), juce::jmin(b.getHeight(), 19 + 2 + kKnobD + 18));
    name_.setBounds(b.removeFromTop(19));
    b.removeFromTop(2);
    slider_.setBounds(b);
}

void PrismKnob::paint(juce::Graphics& g)
{
    if(!header_)
        return;
    // header knobs: caps label above a serif value, to the right of the dial
    auto r = getLocalBounds().toFloat().withTrimmedLeft(kHeaderKnobD + 8.f);
    g.setColour(Muted());
    g.setFont(Fonts::Caps(11.f));
    DrawTracked(g, headerLabel_, r.removeFromTop(r.getHeight() * .5f), .8f, juce::Justification::bottomLeft);
    g.setColour(slider_.isMouseButtonDown() ? Accent() : Ink());
    g.setFont(Fonts::Serif(19.f));
    g.drawText(slider_.getTextFromValue(slider_.getValue()), r, juce::Justification::topLeft);
}

SwapKnob::SwapKnob(APVTS& apvts, const char* switchId, const char* offId, const char* onId, const juce::String& name)
    : switch_(apvts.getRawParameterValue(switchId)), off_(apvts, offId, name), on_(apvts, onId, name)
{
    addChildComponent(off_);
    addChildComponent(on_);
    Update();
}

void SwapKnob::resized()
{
    off_.setBounds(getLocalBounds());
    on_.setBounds(getLocalBounds());
}

void SwapKnob::Update()
{
    const bool on = switch_->load() > .5f;
    on_.setVisible(on);
    off_.setVisible(!on);
}

// ---------------------------------------------------------------- pills, dropdowns

Pill::Pill(const juce::String& text, Kind kind, bool dot) : juce::TextButton(text)
{
    getProperties().set("kind", (int)kind);
    getProperties().set("dot", dot);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

std::unique_ptr<Pill> Pill::Toggle(APVTS& apvts, const char* id, const juce::String& text)
{
    auto p = std::make_unique<Pill>(text, Kind::Toggle);
    p->setClickingTogglesState(true);
    p->attach_ = std::make_unique<APVTS::ButtonAttachment>(apvts, id, *p);
    return p;
}

std::unique_ptr<Pill> Pill::Action(APVTS& apvts, const char* id, const juce::String& text, bool dot)
{
    auto p        = std::make_unique<Pill>(text, Kind::Action, dot);
    p->momentary_ = apvts.getParameter(id);
    return p;
}

int Pill::NaturalWidth() const
{
    if((int)getProperties()["kind"] == (int)Kind::Text)
        return juce::roundToInt(juce::GlyphArrangement::getStringWidth(Fonts::Serif(22.f), getButtonText())) + 4;
    const bool dot = getProperties()["dot"];
    return juce::roundToInt(TrackedWidth(Fonts::Caps(11.f, true), getButtonText().toUpperCase(), .8f)) + 22 + (dot ? 12 : 0);
}

void Pill::buttonStateChanged()
{
    if(momentary_ == nullptr)
        return;
    const bool down = isDown();
    if(down == down_)
        return;
    down_ = down;
    momentary_->beginChangeGesture();
    momentary_->setValueNotifyingHost(down ? 1.f : 0.f);
    momentary_->endChangeGesture();
}

UnderlineCombo::UnderlineCombo(APVTS* apvts, const char* id, const juce::String& label) : label_(label.toUpperCase())
{
    if(apvts != nullptr && id != nullptr)
        if(auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts->getParameter(id)))
            box.addItemList(p->choices, 1);
    addAndMakeVisible(box);
    if(apvts != nullptr && id != nullptr)
        attach_ = std::make_unique<APVTS::ComboBoxAttachment>(*apvts, id, box);
}

void UnderlineCombo::resized() { box.setBounds(getLocalBounds().withTrimmedTop(15)); }

void UnderlineCombo::paint(juce::Graphics& g)
{
    g.setColour(Muted().withMultipliedAlpha(isEnabled() ? 1.f : .5f));
    g.setFont(Fonts::Caps(11.f));
    DrawTracked(g, label_, getLocalBounds().toFloat().removeFromTop(13.f), .8f, juce::Justification::centredLeft);
}

// ---------------------------------------------------------------- module

Module::Module(APVTS& apvts, const juce::String& title, const char* onParam) : apvts_(apvts), title_(title), onId_(onParam)
{
    if(onParam != nullptr)
        on_ = apvts.getRawParameterValue(onParam);
    lineH_.push_back(kModHeaderH); // line 0 is the header
}

void Module::SetNumber(int n)
{
    if(n != number_)
    {
        number_ = n;
        repaint();
    }
}

void Module::AddHeader(juce::Component& c, int width)
{
    addAndMakeVisible(c);
    headers_.push_back({&c, width});
}

void Module::SetSketch(Sketch::Draw draw, Sketch::State state)
{
    sketch_ = std::make_unique<Sketch>(std::move(draw), std::move(state));
    addAndMakeVisible(*sketch_);
}

void Module::NewLine(int height) { lineH_.push_back(height); }

void Module::AddKnob(juce::Component& c)
{
    addAndMakeVisible(c);
    items_.push_back({Item::Knob, &c, kCell, (int)lineH_.size() - 1});
}

void Module::AddFixed(juce::Component& c, int width)
{
    addAndMakeVisible(c);
    items_.push_back({Item::Fixed, &c, width, (int)lineH_.size() - 1});
}

void Module::AddFlexible(juce::Component& c, int minWidth)
{
    addAndMakeVisible(c);
    items_.push_back({Item::Flexible, &c, minWidth, (int)lineH_.size() - 1});
}

int Module::LineWidth(int line) const
{
    int w = 0, n = 0;
    for(auto& it : items_)
        if(it.line == line)
            w += it.width + (n++ > 0 && it.kind != Item::Knob ? 8 : 0);
    return w;
}

float Module::TitleRight() const
{
    float x = kModPadSide + juce::GlyphArrangement::getStringWidth(Fonts::Mono(11.f), "00") + 8.f;
    x += juce::GlyphArrangement::getStringWidth(Fonts::Serif(23.f), title_.toLowerCase());
    if(note_)
        x += 8.f + juce::GlyphArrangement::getStringWidth(Fonts::Mono(12.f), note_());
    return x;
}

bool Module::OnTitle(juce::Point<float> p) const
{
    return p.y < kModPadTop + kModHeaderH && p.x < TitleRight() && p.x > kModPadSide;
}

int Module::PreferredWidth() const
{
    if(collapsed_)
        return kCollapsedW;
    int w = 0;
    for(int l = 1; l < (int)lineH_.size(); ++l)
        w = juce::jmax(w, LineWidth(l));
    int header = juce::roundToInt(TitleRight()) - kModPadSide;
    for(auto& h : headers_)
        header += 6 + h.second;
    return juce::jmax(w, header) + 2 * kModPadSide;
}

void Module::Refresh()
{
    const bool off = on_ != nullptr && on_->load() < .5f;
    if(off == off_)
        return;
    off_ = off;
    if(!folds_)
    {
        // stay put: dim the controls (they still work, so it can be set up before switching on)
        for(auto* c : getChildren())
            c->setAlpha(off ? .35f : 1.f);
        repaint();
        return;
    }
    const bool collapsed = off;
    collapsed_ = collapsed;
    for(auto* c : getChildren())
        c->setVisible(!collapsed);
    if(onCollapse)
        onCollapse();
    resized();
    repaint();
}

void Module::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(on_ && (collapsed_ || OnTitle(e.position)) ? juce::MouseCursor::PointingHandCursor
                                                              : juce::MouseCursor::NormalCursor);
}

void Module::mouseUp(const juce::MouseEvent& e)
{
    // click a folded module to bring it back; click the title to switch it off
    if(on_ == nullptr || e.mouseWasDraggedSinceMouseDown() || !(collapsed_ || OnTitle(e.position)))
        return;
    if(auto* p = apvts_.getParameter(onId_))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(off_ ? 1.f : 0.f);
        p->endChangeGesture();
    }
    Refresh();
}

void Module::paint(juce::Graphics& g)
{
    const auto num = juce::String(number_).paddedLeft('0', 2);
    if(collapsed_)
    {
        g.setColour(Muted());
        g.setFont(Fonts::Mono(11.f));
        g.drawText(num, getLocalBounds().withHeight(kModPadTop + kModHeaderH), juce::Justification::centred);
        // the title runs up the strip, struck through
        juce::Graphics::ScopedSaveState state(g);
        const auto c = getLocalBounds().toFloat().withTrimmedTop(kModPadTop + kModHeaderH).getCentre();
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, c.x, c.y));
        const auto  font = Fonts::Serif(21.f);
        const float tw   = juce::GlyphArrangement::getStringWidth(font, title_.toLowerCase());
        g.setFont(font);
        const auto tr = juce::Rectangle<float>(tw + 4.f, 26.f).withCentre(c);
        g.drawText(title_.toLowerCase(), tr, juce::Justification::centred);
        g.fillRect(tr.getX(), c.y + 1.f, tr.getWidth(), 1.f);
        return;
    }

    auto line = juce::Rectangle<float>(kModPadSide, kModPadTop, (float)getWidth() - 2 * kModPadSide, kModHeaderH);
    g.setColour(Muted());
    g.setFont(Fonts::Mono(11.f));
    const float nw = juce::GlyphArrangement::getStringWidth(Fonts::Mono(11.f), "00") + 8.f;
    g.drawText(num, line.removeFromLeft(nw).withTrimmedTop(5.f), juce::Justification::centredLeft);
    g.setColour(off_ ? Muted() : Ink());
    g.setFont(Fonts::Serif(23.f));
    const float tw = juce::GlyphArrangement::getStringWidth(Fonts::Serif(23.f), title_.toLowerCase());
    const auto  tr = line.removeFromLeft(tw + 2.f);
    g.drawText(title_.toLowerCase(), tr, juce::Justification::centredLeft);
    if(off_)
        g.fillRect(tr.getX(), tr.getCentreY() + 2.f, tw, 1.2f); // struck through, like the chain bar
    if(note_)
    {
        g.setColour(Muted());
        g.setFont(Fonts::Mono(12.f));
        g.drawText(note_(), line.withTrimmedLeft(6.f).withTrimmedTop(4.f), juce::Justification::centredLeft);
    }
}

void Module::resized()
{
    if(collapsed_)
        return;
    auto b = getLocalBounds()
                 .withTrimmedLeft(kModPadSide)
                 .withTrimmedRight(kModPadSide)
                 .withTrimmedTop(kModPadTop)
                 .withTrimmedBottom(kModPadBottom);
    auto header = b.removeFromTop(kModHeaderH);
    for(auto& h : headers_)
    {
        h.first->setBounds(header.removeFromRight(h.second).withSizeKeepingCentre(h.second, 22));
        header.removeFromRight(6);
    }
    if(sketch_)
    {
        // a small diagram in the header's spare room
        header.removeFromLeft(juce::roundToInt(TitleRight()) - kModPadSide + 12);
        header.removeFromRight(6);
        const int w = juce::jmin(header.getWidth(), 150);
        sketch_->setVisible(w >= 44);
        sketch_->setBounds(header.removeFromLeft(juce::jmax(0, w)).withSizeKeepingCentre(juce::jmax(0, w), 24));
    }

    // lines: fixed heights first, the rest share what's left
    int fixed = 0, fills = 0;
    for(size_t l = 1; l < lineH_.size(); ++l)
        (lineH_[l] > 0 ? fixed += lineH_[l] : ++fills);
    const int gaps  = kLineGap * (int)(lineH_.size() - 1);
    const int fillH = fills > 0 ? juce::jmax(0, b.getHeight() - fixed - gaps) / fills : 0;

    for(int l = 1; l < (int)lineH_.size(); ++l)
    {
        b.removeFromTop(kLineGap);
        auto area = b.removeFromTop(lineH_[(size_t)l] > 0 ? lineH_[(size_t)l] : fillH);

        int count = 0, flex = 0;
        for(auto& it : items_)
            if(it.line == l)
            {
                ++count;
                flex += it.kind == Item::Flexible ? 1 : 0;
            }
        const int spare = juce::jmax(0, area.getWidth() - LineWidth(l));
        // flexible items share the spare room; otherwise the controls spread across the width
        const int extraFlex = flex > 0 ? spare / flex : 0;
        const int extraKnob = flex == 0 && count > 0 ? spare / count : 0;
        int       x         = area.getX();
        bool      first     = true;
        for(auto& it : items_)
        {
            if(it.line != l)
                continue;
            if(!first && it.kind != Item::Knob)
                x += 8;
            first = false;
            const int w = it.width + (it.kind == Item::Flexible ? extraFlex : extraKnob);
            it.c->setBounds(x, area.getY(), w, area.getHeight());
            x += w;
        }
    }
}

// ---------------------------------------------------------------- chain bar

ChainBar::ChainBar(PrismProcessor& p) : proc_(p), order_(p.GetOrder())
{
    setTooltip("Click a name to switch it on or off. Drag a name to move it.");
}

void ChainBar::ResetOrder()
{
    order_ = DefaultOrder();
    proc_.SetOrder(order_);
    repaint();
}

bool ChainBar::IsOn(int slot) const
{
    const char* id = slot == kNumBlocks ? ids::squashOn : OnParam(order_[(size_t)slot]);
    return id == nullptr || proc_.apvts.getRawParameterValue(id)->load() > .5f;
}

uint32_t ChainBar::Mask() const
{
    uint32_t m = 0;
    for(int i = 0; i <= kNumBlocks; ++i)
        m |= IsOn(i) ? (1u << i) : 0u;
    return m;
}

void ChainBar::Sync()
{
    const auto m = Mask();
    if(dragSlot_ < 0 && (order_ != proc_.GetOrder() || m != mask_))
    {
        order_ = proc_.GetOrder();
        mask_  = m;
        repaint();
    }
}

void ChainBar::Toggle(int slot)
{
    const char* id = slot == kNumBlocks ? ids::squashOn : OnParam(order_[(size_t)slot]);
    if(id == nullptr)
        return;
    if(auto* p = proc_.apvts.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(IsOn(slot) ? 0.f : 1.f);
        p->endChangeGesture();
    }
    Sync();
}

std::vector<ChainBar::Word> ChainBar::Words() const
{
    const auto        font  = Fonts::Serif(22.f);
    const float       space = juce::GlyphArrangement::getStringWidth(font, " ");
    std::vector<Word> words;
    float             x = 0.f;
    auto add = [&](const juce::String& t, int slot) {
        const float w = juce::GlyphArrangement::getStringWidth(font, t);
        words.push_back({{x, 0.f, w, (float)getHeight()}, t, slot});
        x += w + space;
    };
    add(juce::String::fromUTF8("in \xe2\x86\x92"), -1);
    for(int i = 0; i < kNumBlocks; ++i)
        add(juce::String(BlockLabel(order_[(size_t)i])) + (i < kNumBlocks - 1 ? "," : ""), i);
    add(juce::String::fromUTF8("\xe2\x86\x92"), -1);
    add("Squash", kNumBlocks);
    add(juce::String::fromUTF8("\xe2\x86\x92 out"), -1);
    return words;
}

int ChainBar::SlotAt(juce::Point<float> p) const
{
    for(auto& w : Words())
        if(w.slot >= 0 && w.r.contains(p))
            return w.slot;
    return -1;
}

int ChainBar::InsertAt(float x) const
{
    // the slot the dragged name would land in
    int i = 0;
    for(auto& w : Words())
        if(w.slot >= 0 && w.slot < kNumBlocks && x > w.r.getCentreX())
            i = w.slot + 1;
    return juce::jlimit(0, kNumBlocks - 1, i > dragSlot_ ? i - 1 : i);
}

void ChainBar::paint(juce::Graphics& g)
{
    g.setFont(Fonts::Serif(22.f));
    for(auto& w : Words())
    {
        const bool name = w.slot >= 0;
        const bool on   = name && IsOn(w.slot);
        g.setColour(!name || !on ? Muted() : Ink());
        if(name && w.slot == dragSlot_ && moved_)
            g.setColour(Accent());
        g.drawText(w.text, w.r, juce::Justification::centredLeft);
        if(name && !on)
        {
            // struck through; the comma stays clear
            const float tw = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), w.text.trimCharactersAtEnd(","));
            g.fillRect(w.r.getX(), std::round(w.r.getCentreY() + 1.f), tw, 1.f);
        }
    }
    if(caret_ >= 0 && moved_)
    {
        // where the dragged name will land
        float x = 0.f;
        for(auto& w : Words())
            if(w.slot == caret_)
                x = caret_ > dragSlot_ ? w.r.getRight() + 3.f : w.r.getX() - 4.f;
        g.setColour(Accent());
        g.fillRect(std::round(x), 8.f, 1.f, (float)getHeight() - 16.f);
    }
}

void ChainBar::mouseMove(const juce::MouseEvent& e)
{
    const int s = SlotAt(e.position);
    setMouseCursor(s < 0 ? juce::MouseCursor::NormalCursor
                         : (s == kNumBlocks ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor));
}

void ChainBar::mouseDown(const juce::MouseEvent& e)
{
    downAt_     = e.position;
    moved_      = false;
    const int s = SlotAt(e.position);
    dragSlot_   = s >= 0 && s < kNumBlocks ? s : -1;
}

void ChainBar::mouseDrag(const juce::MouseEvent& e)
{
    if(dragSlot_ < 0)
        return;
    if(e.position.getDistanceFrom(downAt_) > 4.f)
        moved_ = true;
    if(moved_)
    {
        caret_ = InsertAt(e.position.x);
        repaint();
    }
}

void ChainBar::mouseUp(const juce::MouseEvent& e)
{
    if(moved_ && dragSlot_ >= 0 && caret_ >= 0 && caret_ != dragSlot_)
    {
        const auto moving = order_[(size_t)dragSlot_];
        auto       list   = std::vector<Block>(order_.begin(), order_.end());
        list.erase(list.begin() + dragSlot_);
        list.insert(list.begin() + caret_, moving);
        std::copy(list.begin(), list.end(), order_.begin());
        proc_.SetOrder(order_);
    }
    else if(!moved_)
    {
        if(const int s = SlotAt(e.position); s >= 0)
            Toggle(s);
    }
    dragSlot_ = caret_ = -1;
    moved_    = false;
    repaint();
}

// ---------------------------------------------------------------- status line

void StatusLine::SetTouched(const juce::String& name, const juce::String& value, float norm)
{
    if(name == name_ && value == value_ && std::abs(norm - norm_) < 1e-4f)
        return;
    name_  = name;
    value_ = value;
    norm_  = norm;
    repaint();
}

void StatusLine::SetContext(const juce::String& context)
{
    if(context != context_)
    {
        context_ = context;
        repaint();
    }
}

void StatusLine::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // right: tempo and what the looper is doing
    g.setColour(Muted());
    g.setFont(Fonts::Serif(19.f, true));
    g.drawText(context_, r, juce::Justification::centredRight);

    // left: LAST TOUCHED  name  [----|      ]  value
    g.setFont(Fonts::Caps(11.f));
    const juce::String caps = "LAST TOUCHED";
    DrawTracked(g, caps, r, .8f, juce::Justification::centredLeft);
    r.removeFromLeft(TrackedWidth(Fonts::Caps(11.f), caps, .8f) + 12.f);

    g.setColour(Ink());
    g.setFont(Fonts::Serif(21.f));
    const float nw = juce::GlyphArrangement::getStringWidth(Fonts::Serif(21.f), name_);
    g.drawText(name_, r.removeFromLeft(nw + 2.f), juce::Justification::centredLeft);
    if(norm_ < 0.f)
        return;

    // where it's set, as a hairline with an accent fill
    r.removeFromLeft(12.f);
    auto bar = r.removeFromLeft(64.f).withSizeKeepingCentre(64.f, 3.f);
    g.setColour(T().track);
    g.fillRect(bar.withSizeKeepingCentre(bar.getWidth(), 1.f));
    g.setColour(Accent());
    g.fillRect(bar.withWidth(std::round(bar.getWidth() * juce::jlimit(0.f, 1.f, norm_))));
    r.removeFromLeft(10.f);
    g.setColour(Ink());
    g.setFont(Fonts::Mono(13.f, true));
    g.drawText(value_, r, juce::Justification::centredLeft);
}

// ---------------------------------------------------------------- step lane

StepLane::StepLane(PrismProcessor& p) : proc_(p) {}

uint32_t StepLane::Key() const
{
    auto& s = proc_.apvts;
    auto  v = [&](const char* id) { return s.getRawParameterValue(id)->load(); };
    return sketch::Key({(float)proc_.uiGlitchStep.load() / 64.f, v(ids::glitchChaos), v(ids::glitchSeed) / 64.f,
                        v(ids::glitchPattern) / 4.f, v(ids::glitchRate) / 4.f, v(ids::glitchMode), v(ids::glitchRetrig),
                        v(ids::glitchReverse), v(ids::glitchOctUp), v(ids::glitchOctDown), v(ids::classic), v(ids::glitchOn),
                        Accent().getBrightness()});
}

void StepLane::Poll()
{
    if(const auto k = Key(); k != last_)
    {
        last_ = k;
        repaint();
    }
}

void StepLane::paint(juce::Graphics& g)
{
    auto&          s       = proc_.apvts;
    auto           v       = [&](const char* id) { return s.getRawParameterValue(id)->load(); };
    const int      patIdx  = juce::jlimit(0, (int)std::size(kPatternBars) - 1, (int)v(ids::glitchPattern));
    const int      bars    = kPatternBars[patIdx];
    const float    beats   = kGlitchRates[juce::jlimit(0, (int)std::size(kGlitchRates) - 1, (int)v(ids::glitchRate))].beats;
    const int      steps   = bars > 0 ? juce::roundToInt(bars * 4 / beats) : 8;
    const int      cur     = proc_.uiGlitchStep.load() % juce::jmax(1, steps);
    const int      page    = cur / 8;
    const bool     classic = v(ids::classic) > .5f, shimmer = v(ids::glitchMode) > .5f; // as the engine decides
    const unsigned mask    = classic ? 0xFu
                                     : (v(ids::glitchRetrig) > .5f ? 1u : 0u) | (v(ids::glitchReverse) > .5f ? 2u : 0u)
                                        | (v(ids::glitchOctUp) > .5f ? 4u : 0u) | (v(ids::glitchOctDown) > .5f ? 8u : 0u);
    const bool on = v(ids::glitchOn) > .5f;

    for(int i = 0; i < 8; ++i)
    {
        const int  step = page * 8 + i;
        const auto r    = juce::Rectangle<float>(i * 22.f + 1.f, getHeight() * .5f - 6.f + .5f, 18.f, 12.f);
        if(step >= steps)
        {
            g.setColour(T().track);
            g.drawRoundedRectangle(r, 6.f, 1.f);
            continue;
        }
        // the same dice the glitch delay rolls, so filled steps are the ones that will glitch
        prism::Rng  dice(sketch::StepSeed(bars > 0 ? (uint32_t)v(ids::glitchSeed) : 977u, step, steps));
        const float roll = dice.Uniform();
        dice.Uniform(); // pan
        const uint32_t pick = dice.Next();
        int enabled[4], n = 0;
        for(int e = 0; e < 4; ++e)
            if(mask & (1u << e))
                enabled[n++] = e;
        const int  ev  = shimmer ? 2 : (n > 0 ? enabled[pick % (uint32_t)n] : -1);
        const bool hit = roll < .5f * v(ids::glitchChaos) && ev >= 0;
        const bool now = on && step == cur;
        if(now || hit)
        {
            g.setColour(now ? Red() : Ink());
            g.fillRoundedRectangle(r, 6.f);
        }
        g.setColour(now ? Red() : Ink());
        g.drawRoundedRectangle(r, 6.f, 1.f);
        if(hit)
        {
            // what this step does: retrigger, reverse, octave up, octave down
            const auto c = r.getCentre();
            juce::Path sym;
            if(ev == 0)
                for(float dx : {-3.f, 0.f, 3.f})
                {
                    sym.startNewSubPath(c.x + dx, c.y - 2.5f);
                    sym.lineTo(c.x + dx, c.y + 2.5f);
                }
            else if(ev == 1)
            {
                sym.startNewSubPath(c.x + 2.f, c.y - 3.f);
                sym.lineTo(c.x - 2.f, c.y);
                sym.lineTo(c.x + 2.f, c.y + 3.f);
            }
            else
            {
                const float d = ev == 2 ? -1.f : 1.f;
                sym.startNewSubPath(c.x - 3.f, c.y - d * 1.5f);
                sym.lineTo(c.x, c.y + d * 1.5f);
                sym.lineTo(c.x + 3.f, c.y - d * 1.5f);
            }
            g.setColour(Bg());
            g.strokePath(sym, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
}

// ---------------------------------------------------------------- loop strip

LoopStrip::LoopStrip(PrismProcessor& p) : proc_(p) {}

juce::Rectangle<float> LoopStrip::Wave() const
{
    return getLocalBounds().toFloat().withSizeKeepingCentre((float)getWidth(), 26.f).reduced(14.f, 4.f);
}

float LoopStrip::XFor(float frac) const
{
    const auto w = Wave();
    return w.getX() + w.getWidth() * frac;
}

float LoopStrip::FracAt(float x) const
{
    const auto w = Wave();
    return juce::jlimit(0.f, 1.f, (x - w.getX()) / w.getWidth());
}

const char* LoopStrip::HandleAt(float x) const
{
    const auto&  looper = proc_.Looper();
    const size_t len    = looper.GetLength();
    if(len == 0 || looper.GetState() == TapeLooper::State::Recording)
        return nullptr;
    const float xs = XFor(looper.GetWindowStart() / (float)len);
    const float xe = XFor(looper.GetWindowEnd() / (float)len);
    if(std::abs(x - xs) < 6.f && std::abs(x - xs) <= std::abs(x - xe))
        return ids::loopStart;
    if(std::abs(x - xe) < 6.f)
        return ids::loopEnd;
    return nullptr;
}

void LoopStrip::paint(juce::Graphics& g)
{
    const auto pill = getLocalBounds().toFloat().withSizeKeepingCentre((float)getWidth() - 1.f, 26.f).reduced(.5f);
    g.setColour(Ink());
    g.drawRoundedRectangle(pill, pill.getHeight() * .5f, 1.f);

    const auto&  looper = proc_.Looper();
    const auto   state  = looper.GetState();
    const size_t len    = looper.GetLength();
    if(state == TapeLooper::State::Empty)
    {
        g.setColour(Muted());
        g.setFont(Fonts::Serif(15.5f, true));
        g.drawText(proc_.LooperWaiting() ? "waiting for the beat..." : "press rec to start a loop", pill,
                   juce::Justification::centred);
        return;
    }

    const auto  wave   = Wave();
    const float mid    = wave.getCentreY(), halfH = wave.getHeight() * .5f;
    const auto  bins   = (len + TapeLooper::kPeakBin - 1) / TapeLooper::kPeakBin;
    const bool  window = state != TapeLooper::State::Recording && len > 0;
    const float xs     = window ? XFor(looper.GetWindowStart() / (float)len) : wave.getX();
    const float xe     = window ? XFor(looper.GetWindowEnd() / (float)len) : wave.getRight();
    const bool  hot    = state == TapeLooper::State::Recording || looper.IsDubbing();
    for(int x = 0; x < (int)wave.getWidth(); ++x)
    {
        const size_t b0   = bins * (size_t)x / (size_t)wave.getWidth();
        const size_t b1   = juce::jmax(b0 + 1, bins * (size_t)(x + 1) / (size_t)wave.getWidth());
        float        peak = 0.f;
        for(size_t b = b0; b < b1; ++b)
            peak = std::fmax(peak, looper.Peak(b));
        const float px = wave.getX() + x;
        const bool  in = px >= xs && px < xe;
        g.setColour(hot ? Red() : in ? Ink() : Muted().withAlpha(.5f));
        const float h = juce::jlimit(.5f, halfH, std::sqrt(peak) * halfH);
        g.fillRect(px, mid - h, 1.f, h * 2.f);
    }
    if(window)
    {
        g.setColour(Accent());
        for(float hx : {xs, xe})
            g.fillRect(hx - .75f, pill.getY() + 3.f, 1.5f, pill.getHeight() - 6.f);
        const float px = XFor(looper.GetPosition() / (float)len);
        g.fillRect(px - 1.f, pill.getY() + 1.f, 2.f, pill.getHeight() - 2.f);
    }
}

void LoopStrip::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(HandleAt(e.position.x) != nullptr ? juce::MouseCursor::LeftRightResizeCursor
                                                     : juce::MouseCursor::NormalCursor);
}

void LoopStrip::mouseDown(const juce::MouseEvent& e)
{
    dragging_ = HandleAt(e.position.x);
    if(dragging_ == nullptr)
        dragging_ = ids::loopScrub;
    if(auto* p = proc_.apvts.getParameter(dragging_))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(FracAt(e.position.x));
    }
}

void LoopStrip::mouseDrag(const juce::MouseEvent& e)
{
    if(dragging_ != nullptr)
        if(auto* p = proc_.apvts.getParameter(dragging_))
            p->setValueNotifyingHost(FracAt(e.position.x));
}

void LoopStrip::mouseUp(const juce::MouseEvent&)
{
    if(dragging_ != nullptr)
        if(auto* p = proc_.apvts.getParameter(dragging_))
            p->endChangeGesture();
    dragging_ = nullptr;
}

// ---------------------------------------------------------------- editor

namespace
{
const juce::Identifier kThemeProp{"uiTheme"}, kScaleProp{"uiScale"};

constexpr float kMinScale = .75f, kMaxScale = 2.f;
} // namespace

PrismEditor::PrismEditor(PrismProcessor& p) : AudioProcessorEditor(&p), proc_(p)
{
    auto& s = p.apvts;
    SetCream(s.state.getProperty(kThemeProp, "night").toString() == "cream");
    look_.Recolour();
    setLookAndFeel(&look_);

    auto keep = [this](auto ptr) -> auto& {
        auto& ref = *ptr;
        owned_.push_back(std::move(ptr));
        return ref;
    };
    auto knob   = [&](const char* id, const juce::String& name) -> PrismKnob& { return keep(std::make_unique<PrismKnob>(s, id, name)); };
    auto toggle = [&](const char* id, const juce::String& text) -> Pill& { return keep(Pill::Toggle(s, id, text)); };

    // ---- status line and header
    canvas_.addAndMakeVisible(status_);

    macros_ = std::make_unique<UnderlineCombo>(nullptr, nullptr, "Macros");
    macros_->box.addItem("Off", 1);
    macros_->box.addItem("Classic", 2);
    macros_->box.setTooltip("Classic: the hardware's one-knob macros (feedback also sets the delay's mix, and so on)");
    macrosAttach_ = std::make_unique<juce::ParameterAttachment>(
        *s.getParameter(ids::classic),
        [this](float val) { macros_->box.setSelectedId(val > .5f ? 2 : 1, juce::dontSendNotification); });
    macros_->box.onChange = [this] { macrosAttach_->setValueAsCompleteGesture(macros_->box.getSelectedId() == 2 ? 1.f : 0.f); };
    macrosAttach_->sendInitialUpdate();
    canvas_.addAndMakeVisible(*macros_);

    input_  = std::make_unique<PrismKnob>(s, ids::input, "Input", true);
    dryWet_ = std::make_unique<PrismKnob>(s, ids::dryWet, "Dry / Wet", true);
    output_ = std::make_unique<PrismKnob>(s, ids::output, "Output", true);
    squash_ = std::make_unique<PrismKnob>(s, ids::squash, "Squash", true);
    for(auto* k : {input_.get(), dryWet_.get(), output_.get(), squash_.get()})
        canvas_.addAndMakeVisible(*k);

    chain_ = std::make_unique<ChainBar>(p);
    canvas_.addAndMakeVisible(*chain_);
    reset_.setTooltip("Put the effects back in their default order");
    reset_.onClick = [this] { chain_->ResetOrder(); };
    view_.setTooltip("Theme and size");
    view_.onClick = [this] { ShowViewMenu(); };
    canvas_.addAndMakeVisible(reset_);
    canvas_.addAndMakeVisible(view_);

    // ---- row 1
    filter_ = std::make_unique<Module>(s, "Filter", ids::filterOn);
    filter_->NewLine(0);
    filter_->AddKnob(knob(ids::cutoff, "lp / hp"));
    filter_->AddKnob(knob(ids::resonance, "Resonance"));

    drive_ = std::make_unique<Module>(s, "Drive", ids::driveOn);
    drive_->NewLine(0);
    drive_->AddKnob(knob(ids::drive, "Drive"));

    tape_ = std::make_unique<Module>(s, "Tape", ids::tapeOn);
    tape_->NewLine(0);
    tape_->AddKnob(knob(ids::warbleDepth, "Warble"));
    classicOff_.push_back(&knob(ids::warbleRate, "Rate"));
    tape_->AddKnob(*classicOff_.back());

    crush_ = std::make_unique<Module>(s, "Crush", ids::crushOn);
    crush_->NewLine(0);
    crush_->AddKnob(knob(ids::crushRate, "Rate"));
    crush_->AddKnob(knob(ids::crushBits, "Bits"));
    crush_->AddKnob(knob(ids::crushMix, "Mix"));

    delay_ = std::make_unique<Module>(s, "Delay", ids::delayOn);
    {
        auto& vari = toggle(ids::delayVari, "Varispeed");
        auto& sync = toggle(ids::delaySync, "Sync");
        vari.setTooltip("Varispeed: changing the time re-pitches the repeats, like tape. Off: TAPE's original delay.");
        delay_->AddHeader(vari, vari.NaturalWidth());
        delay_->AddHeader(sync, sync.NaturalWidth());
    }
    delay_->NewLine(0);
    swaps_.push_back(&keep(std::make_unique<SwapKnob>(s, ids::delaySync, ids::delayTime, ids::delayDiv, "Time")));
    delay_->AddKnob(*swaps_.back());
    delay_->AddKnob(knob(ids::delayFeedback, "Feedback"));
    classicOff_.push_back(&knob(ids::delayMix, "Mix"));
    delay_->AddKnob(*classicOff_.back());

    reverb_ = std::make_unique<Module>(s, "Reverb", ids::reverbOn);
    {
        auto& fr = toggle(ids::reverbFreeze, "Freeze");
        reverb_->AddHeader(fr, fr.NaturalWidth());
    }
    reverb_->NewLine(0);
    reverb_->AddKnob(knob(ids::reverbMix, "Mix"));
    for(auto [id, name] : {std::pair{ids::reverbDecay, "Decay"}, std::pair{ids::reverbTone, "Tone"},
                           std::pair{ids::reverbDiffusion, "Diffusion"}})
    {
        classicOff_.push_back(&knob(id, name));
        reverb_->AddKnob(*classicOff_.back());
    }

    // ---- glitch
    glitch_ = std::make_unique<Module>(s, "Glitch Delay", ids::glitchOn);
    glitch_->SetFolds(false); // too big to fold: a 36 px strip next to a stretched looper looks odd
    {
        auto div = s.getRawParameterValue(ids::glitchDiv), rate = s.getRawParameterValue(ids::glitchRate),
             pat = s.getRawParameterValue(ids::glitchPattern), mode = s.getRawParameterValue(ids::glitchMode);
        auto* divP = dynamic_cast<juce::AudioParameterChoice*>(s.getParameter(ids::glitchDiv));
        glitch_->SetHeaderNote([=] {
            const auto dot  = juce::String::fromUTF8(" \xc2\xb7 ");
            const int  bars = kPatternBars[juce::jlimit(0, (int)std::size(kPatternBars) - 1, (int)pat->load())];
            juce::String n  = divP->choices[juce::jlimit(0, divP->choices.size() - 1, (int)div->load())];
            n << dot << "rolls " << kGlitchRates[juce::jlimit(0, (int)std::size(kGlitchRates) - 1, (int)rate->load())].name;
            n << dot << (bars > 0 ? juce::String(bars) + (bars == 1 ? " bar" : " bars") : juce::String("free"));
            if(mode->load() > .5f)
                n << dot << "shimmer";
            return n;
        });
    }
    {
        auto& fr = toggle(ids::glitchFreeze, "Freeze");
        glitch_->AddHeader(fr, fr.NaturalWidth());
    }
    glitch_->NewLine(28);
    stepLane_ = &keep(std::make_unique<StepLane>(p));
    glitch_->AddFixed(*stepLane_, 8 * 22);
    glitch_->AddFlexible(keep(std::make_unique<juce::Component>()), 0);
    for(auto [id, text] : {std::pair{ids::glitchRetrig, "Retrig"}, std::pair{ids::glitchReverse, "Reverse"},
                           std::pair{ids::glitchOctUp, "Oct up"}, std::pair{ids::glitchOctDown, "Oct down"}})
    {
        auto& pill = toggle(id, text);
        glitchEvents_.push_back(&pill);
        glitch_->AddFixed(pill, pill.NaturalWidth());
    }
    glitch_->NewLine(0);
    glitch_->AddKnob(knob(ids::glitchChaos, "Chaos"));
    glitch_->AddKnob(knob(ids::glitchFeedback, "Feedback"));
    glitch_->AddKnob(knob(ids::glitchMix, "Mix"));
    classicOff_.push_back(&knob(ids::glitchSpread, "Spread"));
    glitch_->AddKnob(*classicOff_.back());
    glitch_->AddKnob(knob(ids::glitchSeed, "Seed"));
    {
        auto& hint = keep(std::make_unique<juce::Label>());
        hint.setText("free pattern: new rolls every time", juce::dontSendNotification);
        hint.setFont(Fonts::Serif(15.f, true));
        hint.setJustificationType(juce::Justification::centredLeft);
        hint.setComponentID("patternHint");
        glitch_->AddFixed(hint, 92);
    }
    glitch_->NewLine(44);
    for(auto [id, label] : {std::pair{ids::glitchMode, "Mode"}, std::pair{ids::glitchDiv, "Division"},
                            std::pair{ids::glitchRate, "Dice roll"}, std::pair{ids::glitchPattern, "Pattern"}})
        glitch_->AddFlexible(keep(std::make_unique<UnderlineCombo>(&s, id, label)), 90);

    // ---- looper
    looper_ = std::make_unique<Module>(s, "Tape Looper", nullptr);
    {
        auto freeSp = s.getRawParameterValue(ids::loopSpeed), stepsOn = s.getRawParameterValue(ids::loopSpeedSteps),
             step = s.getRawParameterValue(ids::loopSpeedStep);
        auto revP = s.getRawParameterValue(ids::loopReverse);
        PrismProcessor* proc = &p;
        looper_->SetHeaderNote([=] {
            const float sp = stepsOn->load() > .5f
                                 ? kSpeedSteps[juce::jlimit(0, (int)std::size(kSpeedSteps) - 1, (int)step->load())]
                                 : freeSp->load();
            const auto   dot = juce::String::fromUTF8(" \xc2\xb7 ");
            juce::String n   = juce::String(sp, sp < 1.f ? 2 : 1) + "x";
            if(revP->load() > .5f)
                n << dot << "rev";
            const auto& lp = proc->Looper();
            if(lp.GetState() == TapeLooper::State::Recording)
                n << dot << "rec";
            else if(lp.IsDubbing())
                n << dot << "dub";
            return n;
        });
        auto& save = toggle(ids::loopSave, "Save loop");
        save.setTooltip("Store the loop's audio in the project");
        auto& steps = toggle(ids::loopSpeedSteps, "Oct / 5th");
        looper_->AddHeader(save, save.NaturalWidth());
        looper_->AddHeader(steps, steps.NaturalWidth());
    }
    looper_->NewLine(28);
    rec_        = &keep(Pill::Action(s, ids::loopRec, "Rec", true));
    play_       = &keep(Pill::Action(s, ids::loopPlay, "Play"));
    auto& clear = keep(Pill::Action(s, ids::loopClear, "Clear"));
    auto& rev   = toggle(ids::loopReverse, "Reverse");
    for(Pill* b : {rec_, play_, &clear, &rev})
        looper_->AddFixed(*b, b->NaturalWidth());
    loopStrip_ = &keep(std::make_unique<LoopStrip>(p));
    looper_->AddFlexible(*loopStrip_, 120);
    looper_->NewLine(0);
    looper_->AddKnob(knob(ids::loopStart, "Start"));
    looper_->AddKnob(knob(ids::loopEnd, "End"));
    swaps_.push_back(&keep(std::make_unique<SwapKnob>(s, ids::loopSpeedSteps, ids::loopSpeed, ids::loopSpeedStep, "Speed")));
    looper_->AddKnob(*swaps_.back());
    looper_->AddKnob(knob(ids::loopGlide, "Glide"));
    looper_->AddKnob(knob(ids::loopDub, "Dub keep"));
    looper_->AddKnob(knob(ids::loopLevel, "Level"));
    looper_->NewLine(44);
    for(auto [id, label] : {std::pair{ids::loopSnap, "Snap points"}, std::pair{ids::loopLength, "Rec length"},
                            std::pair{ids::loopQuantize, "Quantize"}})
        looper_->AddFlexible(keep(std::make_unique<UnderlineCombo>(&s, id, label)), 90);

    // ---- diagrams beside the titles, following each effect's controls
    auto v = [&s](const char* id) { return s.getRawParameterValue(id); };
    {
        auto c = v(ids::cutoff), res = v(ids::resonance);
        filter_->SetSketch([c, res](juce::Graphics& g, auto r) { sketch::Filter(g, r, c->load(), res->load()); },
                           [c, res] { return sketch::Key({c->load(), res->load()}); });
    }
    {
        auto dep = v(ids::warbleDepth), rate = v(ids::warbleRate);
        tape_->SetSketch([dep, rate](juce::Graphics& g, auto r) { sketch::Tape(g, r, dep->load(), rate->load()); },
                         [dep, rate] { return sketch::Key({dep->load(), rate->load()}); });
    }
    {
        auto rate = v(ids::crushRate), bits = v(ids::crushBits), mix = v(ids::crushMix);
        crush_->SetSketch([rate, bits, mix](juce::Graphics& g, auto r) { sketch::Crush(g, r, rate->load(), bits->load(), mix->load()); },
                          [rate, bits, mix] { return sketch::Key({rate->load() / 48000.f, bits->load() / 16.f, mix->load()}); });
    }
    {
        auto time = v(ids::delayTime), div = v(ids::delayDiv), sync = v(ids::delaySync), fb = v(ids::delayFeedback),
             mix = v(ids::delayMix), vari = v(ids::delayVari);
        auto norm = [time, div, sync] {
            const float secs = sync->load() > .5f
                                   ? kDelayDivs[juce::jlimit(0, (int)std::size(kDelayDivs) - 1, (int)div->load())].beats * .5f
                                   : time->load() * .001f;
            return juce::jlimit(0.f, 1.f, std::log(secs / .01f) / std::log(200.f));
        };
        delay_->SetSketch(
            [norm, fb, mix, vari](juce::Graphics& g, auto r) {
                sketch::Delay(g, r, norm(), fb->load(), mix->load(), vari->load() > .5f, vari->load() > .5f);
            },
            [norm, fb, mix, vari] { return sketch::Key({norm(), fb->load(), mix->load(), vari->load()}); });
    }
    {
        auto mix = v(ids::reverbMix), dec = v(ids::reverbDecay), tone = v(ids::reverbTone), dif = v(ids::reverbDiffusion),
             frz = v(ids::reverbFreeze);
        reverb_->SetSketch([=](juce::Graphics& g, auto r) { sketch::Reverb(g, r, mix->load(), dec->load(), tone->load(), dif->load(), frz->load() > .5f); },
                           [=] { return sketch::Key({mix->load(), dec->load(), tone->load(), dif->load(), frz->load()}); });
    }
    {
        auto freeSp = v(ids::loopSpeed), stepsOn = v(ids::loopSpeedSteps), step = v(ids::loopSpeedStep), rev2 = v(ids::loopReverse),
             glide = v(ids::loopGlide), dub = v(ids::loopDub), lvl = v(ids::loopLevel), len = v(ids::loopLength), q = v(ids::loopQuantize);
        PrismProcessor* proc = &p;
        auto deck = [=] {
            sketch::DeckState d;
            d.steps    = stepsOn->load() > .5f;
            d.stepIdx  = juce::jlimit(0, (int)std::size(kSpeedSteps) - 1, (int)step->load());
            d.speed    = d.steps ? kSpeedSteps[d.stepIdx] : freeSp->load();
            d.reverse  = rev2->load() > .5f;
            d.glide    = glide->load();
            d.dub      = dub->load();
            d.level    = lvl->load();
            d.recBars  = kLoopBars[juce::jlimit(0, (int)std::size(kLoopBars) - 1, (int)len->load())];
            d.quantize = (int)q->load();
            const auto& lp = proc->Looper();
            const auto  st = lp.GetState();
            d.recording = st == TapeLooper::State::Recording;
            d.playing   = st == TapeLooper::State::Playing;
            d.dubbing   = lp.IsDubbing();
            d.hasLoop   = st != TapeLooper::State::Empty && st != TapeLooper::State::Recording && lp.GetLength() > 0;
            d.pos       = d.hasLoop ? lp.GetPosition() / (float)lp.GetLength() : 0.f;
            return d;
        };
        looper_->SetSketch([deck](juce::Graphics& g, auto r) { sketch::Deck(g, r, deck()); },
                           [deck] {
                               const auto d = deck();
                               return sketch::Key({d.speed / 4.f, (float)d.reverse, d.glide, d.dub, d.level, (float)d.recBars / 8.f,
                                                   (float)d.quantize / 2.f, (float)d.recording, (float)d.dubbing, (float)d.hasLoop,
                                                   std::floor(d.pos * 32.f) / 64.f, (float)d.steps});
                           });
    }

    for(auto* m : {filter_.get(), drive_.get(), tape_.get(), crush_.get(), delay_.get(), reverb_.get(), glitch_.get(), looper_.get()})
    {
        canvas_.addAndMakeVisible(*m);
        m->Refresh();
        m->onCollapse = [this] {
            LayoutCanvas();
            canvas_.repaint();
        };
    }
    Renumber();

    // "last touched" in the status line
    for(auto* param : p.getParameters())
        if(auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
        {
            paramIds_.add(rp->getParameterID());
            s.addParameterListener(rp->getParameterID(), this);
        }

    canvas_.onPaint  = [this](juce::Graphics& g) { PaintCanvas(g); };
    canvas_.onResize = [this] { LayoutCanvas(); };
    addAndMakeVisible(canvas_);
    canvas_.setBounds(0, 0, kW, kH);

    // drag the corner to any size between 75% and 200%; the shape stays the same.
    // Read the saved size first: setting up the constrainer resizes the window, which would overwrite it.
    const float savedScale = (float)(double)s.state.getProperty(kScaleProp, 1.0);
    snap_ = std::make_unique<juce::ComponentBoundsConstrainer>();
    snap_->setFixedAspectRatio((double)kW / (double)kH);
    snap_->setSizeLimits(juce::roundToInt(kW * kMinScale), juce::roundToInt(kH * kMinScale),
                         juce::roundToInt(kW * kMaxScale), juce::roundToInt(kH * kMaxScale));
    setResizable(true, true);
    setConstrainer(snap_.get());
    ApplyScale(savedScale);

    timerCallback();
    startTimerHz(30);
}

PrismEditor::~PrismEditor()
{
    for(auto& id : paramIds_)
        proc_.apvts.removeParameterListener(id, this);
    setLookAndFeel(nullptr);
}

void PrismEditor::parameterChanged(const juce::String& id, float) { touched_.store(paramIds_.indexOf(id), std::memory_order_relaxed); }

void PrismEditor::Renumber()
{
    const auto order = proc_.GetOrder();
    auto       mod   = [this](Block b) -> Module* {
        switch(b)
        {
            case Block::Filter: return filter_.get();
            case Block::Drive: return drive_.get();
            case Block::Tape: return tape_.get();
            case Block::Crush: return crush_.get();
            case Block::Delay: return delay_.get();
            case Block::Glitch: return glitch_.get();
            case Block::Reverb: return reverb_.get();
            case Block::Looper: return looper_.get();
        }
        return nullptr;
    };
    for(int i = 0; i < kNumBlocks; ++i)
        if(auto* m = mod(order[(size_t)i]))
            m->SetNumber(i + 1);
}

void PrismEditor::ApplyTheme(bool cream)
{
    SetCream(cream);
    look_.Recolour();
    sendLookAndFeelChange();
    for(auto* m : {filter_.get(), drive_.get(), tape_.get(), crush_.get(), delay_.get(), reverb_.get(), glitch_.get(), looper_.get()})
        m->InvalidateSketch();
    proc_.apvts.state.setProperty(kThemeProp, cream ? "cream" : "night", nullptr);
    repaint();
}

void PrismEditor::ApplyScale(float scale)
{
    scale_ = juce::jlimit(kMinScale, kMaxScale, scale);
    setSize(juce::roundToInt(kW * scale_), juce::roundToInt(kH * scale_));
}

void PrismEditor::ShowViewMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel(&look_);
    m.addSectionHeader("Theme");
    m.addItem("Night", true, !IsCream(), [this] { ApplyTheme(false); });
    m.addItem("Cream", true, IsCream(), [this] { ApplyTheme(true); });
    m.addSectionHeader("Size");
    for(float sc : {.75f, 1.f, 1.25f, 1.5f})
        m.addItem(juce::String(juce::roundToInt(sc * 100.f)) + "%", true, std::abs(scale_ - sc) < .02f, [this, sc] { ApplyScale(sc); });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&view_));
}

void PrismEditor::timerCallback()
{
    auto&      s       = proc_.apvts;
    auto       v       = [&](const char* id) { return s.getRawParameterValue(id)->load(); };
    const bool classic = v(ids::classic) > .5f;
    const bool shimmer = v(ids::glitchMode) > .5f;

    for(auto* c : classicOff_)
        c->setEnabled(!classic);
    for(auto* c : glitchEvents_)
        c->setEnabled(!classic && !shimmer);
    for(auto* k : swaps_)
        k->Update();

    // squash: dimmed and labelled when it's off
    const bool squashOn = v(ids::squashOn) > .5f;
    squash_->setAlpha(squashOn ? 1.f : .4f);
    squash_->SetHeaderLabel(juce::String::fromUTF8(squashOn ? "SQUASH" : "SQUASH \xc2\xb7 OFF"));

    // looper buttons follow its state; a quantised press blinks rec until it lands
    const auto& looper = proc_.Looper();
    const auto  state  = looper.GetState();
    blink_             = (blink_ + 1) % 16;
    const bool recLit  = state == TapeLooper::State::Recording || looper.IsDubbing();
    rec_->setToggleState(recLit || (proc_.LooperWaiting() && blink_ < 8), juce::dontSendNotification);
    const bool playing = state == TapeLooper::State::Playing;
    if(play_->getButtonText() != (playing ? "Stop" : "Play"))
        play_->setButtonText(playing ? "Stop" : "Play");
    if(state != TapeLooper::State::Empty || proc_.LooperWaiting())
        loopStrip_->repaint();

    // the pattern hint only makes sense in free mode
    for(auto* c : glitch_->getChildren())
        if(c->getComponentID() == "patternHint")
        {
            c->setVisible(!glitch_->IsCollapsed() && (int)v(ids::glitchPattern) == 0);
            c->setColour(juce::Label::textColourId, Muted());
        }

    for(auto* m : {filter_.get(), drive_.get(), tape_.get(), crush_.get(), delay_.get(), reverb_.get(), glitch_.get(), looper_.get()})
    {
        m->Refresh();
        m->PollSketch();
    }
    // header notes (glitch timing, looper speed and state)
    for(auto* m : {glitch_.get(), looper_.get()})
        m->repaint(0, 0, m->getWidth(), kModPadTop + kModHeaderH);
    stepLane_->Poll();
    chain_->Sync();
    Renumber();

    // status line: the last control touched, and the tempo and looper on the right
    if(const int t = touched_.exchange(-1); t >= 0)
        if(auto* param = s.getParameter(paramIds_[t]))
        {
            const bool isSwitch = dynamic_cast<juce::AudioParameterBool*>(param) != nullptr;
            status_.SetTouched(param->getName(40).toLowerCase(), param->getCurrentValueAsText().toLowerCase(),
                               isSwitch ? -1.f : param->getValue());
        }
    {
        const auto   dot = juce::String::fromUTF8(" \xc2\xb7 ");
        juce::String loop;
        switch(state)
        {
            case TapeLooper::State::Empty: loop = proc_.LooperWaiting() ? "waiting for the beat" : "looper empty"; break;
            case TapeLooper::State::Recording: loop = "recording"; break;
            case TapeLooper::State::Playing: loop = looper.IsDubbing() ? "overdubbing" : "looping"; break;
            case TapeLooper::State::Stopped: loop = "looper stopped"; break;
        }
        status_.SetContext(juce::String(juce::roundToInt(proc_.uiBpm.load())) + " bpm" + dot + loop);
    }
}

void PrismEditor::paint(juce::Graphics& g) { g.fillAll(Bg()); }

void PrismEditor::resized()
{
    if(getWidth() <= 0)
        return;
    const float sc = (float)getWidth() / (float)kW;
    canvas_.setTransform(juce::AffineTransform::scale(sc));
    // remember the size, whether it came from the menu or a corner drag
    scale_ = sc;
    proc_.apvts.state.setProperty(kScaleProp, (double)sc, nullptr);
}

void PrismEditor::PaintCanvas(juce::Graphics& g)
{
    g.fillAll(Bg());
    g.setColour(Ink());
    const float x0 = kPadSide, x1 = kW - kPadSide, w = x1 - x0;

    // rules between the bands and between modules (the modules themselves draw none)
    const float yStatus = kPadTop + kStatusH, yHeader = yStatus + 1 + kHeaderH, yChain = yHeader + 1 + kChainH;
    for(float y : {yStatus, yHeader, yChain, (float)row1_.getBottom()})
        Rule(g, x0, y, w, 1.f);
    for(auto* row : {&row1_, &row2_})
        for(auto* m : {filter_.get(), drive_.get(), tape_.get(), crush_.get(), delay_.get(), reverb_.get(), glitch_.get(), looper_.get()})
            if(m->getY() == row->getY() && m->getRight() < row->getRight() - 2)
                Rule(g, (float)m->getRight(), (float)row->getY(), 1.f, (float)row->getHeight());

    // wordmark, with a four-point star over the m, next to the FX
    const auto  word  = Fonts::Serif(54.f).withExtraKerningFactor(-.028f);
    const float midY  = yStatus + 1 + kHeaderH * .5f;
    g.setFont(word);
    g.drawText("Prism FX", juce::Rectangle<float>(x0, midY - 27.f, 240.f, 60.f), juce::Justification::centredLeft);
    {
        const float pr = juce::GlyphArrangement::getStringWidth(word, "Pris");
        const float mw = juce::GlyphArrangement::getStringWidth(word, "m");
        const float sx = x0 + pr + mw * .5f, sy = midY - 22.f;
        juce::Path  star;
        const float r = 7.5f, k = 1.2f;
        star.startNewSubPath(sx, sy - r);
        star.quadraticTo(sx + k, sy - k, sx + r, sy);
        star.quadraticTo(sx + k, sy + k, sx, sy + r);
        star.quadraticTo(sx - k, sy + k, sx - r, sy);
        star.quadraticTo(sx - k, sy - k, sx, sy - r);
        star.closeSubPath();
        g.fillPath(star);
    }
    const float wordW = juce::GlyphArrangement::getStringWidth(word, "Prism FX");
    g.setColour(Muted());
    g.setFont(Fonts::Caps(11.f));
    DrawTracked(g, "BY PRISMATIC", {x0 + wordW + 14.f, midY + 8.f, 120.f, 14.f}, .8f, juce::Justification::centredLeft);
}

void PrismEditor::LayoutCanvas()
{
    const int x0 = kPadSide, w = kW - 2 * kPadSide;
    int       y  = kPadTop;
    status_.setBounds(x0, y, w, kStatusH);
    y += kStatusH + 1;

    // header: macros after the wordmark, the four header knobs on the right
    {
        const float wordW = juce::GlyphArrangement::getStringWidth(Fonts::Serif(54.f).withExtraKerningFactor(-.028f), "Prism FX");
        const float byW   = TrackedWidth(Fonts::Caps(11.f), "BY PRISMATIC", .8f);
        macros_->setBounds(x0 + juce::roundToInt(wordW + 14 + byW + 34), y + 12, 120, 42);
        int x = kW - kPadSide;
        for(auto* k : {squash_.get(), output_.get(), dryWet_.get(), input_.get()})
        {
            const int kw = k == squash_.get() ? 136 : 108;
            k->setBounds(x - kw, y + 10, kw, 44);
            x -= kw + 14;
        }
    }
    y += kHeaderH + 1;

    // chain bar, with View and Reset on the right
    {
        auto      bar = juce::Rectangle<int>(x0, y, w, kChainH);
        const int rw = reset_.NaturalWidth(), vw = view_.NaturalWidth();
        reset_.setBounds(bar.removeFromRight(rw));
        bar.removeFromRight(18);
        view_.setBounds(bar.removeFromRight(vw));
        chain_->setBounds(bar.withTrimmedRight(18));
    }
    y += kChainH + 1;

    row1_ = {x0, y, w, kRow1H};
    row2_ = {x0, y + kRow1H + 1, w, kH - kPadBottom - (y + kRow1H + 1)};

    // each module: a fixed width (from the spec) or 0 for flexible, sized by its contents.
    // Folded modules take their strip width; if it all doesn't fit, everyone squeezes in proportion.
    auto lay = [](juce::Rectangle<int> row, std::vector<std::pair<Module*, int>> mods) {
        std::vector<int> want;
        int              total = 0, flexTotal = 0;
        for(auto& [m, fw] : mods)
        {
            // a fixed width is a minimum: a module never gets less than its contents need
            const int wv = m->IsCollapsed() ? kCollapsedW : juce::jmax(fw, m->PreferredWidth());
            want.push_back(wv);
            total += wv;
            if(!m->IsCollapsed() && fw == 0)
                flexTotal += wv;
        }
        const int room  = row.getWidth() - (int)(mods.size() - 1);
        const int spare = room - total;
        int       x     = row.getX();
        for(size_t i = 0; i < mods.size(); ++i)
        {
            auto [m, fw] = mods[i];
            int mw       = want[i];
            if(spare >= 0 && !m->IsCollapsed() && fw == 0 && flexTotal > 0)
                mw += spare * want[i] / flexTotal; // flexible modules share the spare room
            else if(spare < 0 && !m->IsCollapsed())
                mw += spare * want[i] / juce::jmax(1, total - kCollapsedW * 0); // squeeze
            if(i == mods.size() - 1)
                mw = row.getRight() - x;
            m->setBounds(x, row.getY(), mw, row.getHeight());
            x += mw + 1;
        }
    };
    lay(row1_, {{filter_.get(), 168}, {drive_.get(), 96}, {tape_.get(), 168}, {crush_.get(), 0}, {delay_.get(), 0}, {reverb_.get(), 0}});
    lay(row2_, {{glitch_.get(), 520}, {looper_.get(), 0}});
}
