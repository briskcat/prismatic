#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"

namespace prism::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

/** Indie editorial look: hairline rules, serif + mono caps, outlined pills */
class Look : public juce::LookAndFeel_V4
{
  public:
    Look();
    void Recolour();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh,
                      juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackgroundWithOptions(juce::Graphics&, int w, int h, const juce::PopupMenu::Options&) override;
    void drawPopupMenuSectionHeaderWithOptions(juce::Graphics&, const juce::Rectangle<int>&, const juce::String&,
                                               const juce::PopupMenu::Options&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;
};

/** A knob cell: label above, the dial, value below. Header knobs are smaller, with label and value beside. */
class PrismKnob : public juce::Component
{
  public:
    PrismKnob(APVTS&, const char* paramId, const juce::String& name, bool header = false);
    void resized() override;
    void paint(juce::Graphics&) override;
    void SetHeaderLabel(const juce::String& s);
    void lookAndFeelChanged() override { Recolour(); }

  private:
    void                                     Recolour();
    bool                                     dragging_ = false;
    juce::Slider                             slider_;
    juce::Label                              name_;
    juce::String                             headerLabel_;
    bool                                     header_ = false;
    std::unique_ptr<APVTS::SliderAttachment> attach_;
};

/** One knob slot that shows a different parameter depending on a switch (time in ms or divisions) */
class SwapKnob : public juce::Component
{
  public:
    SwapKnob(APVTS&, const char* switchId, const char* offId, const char* onId, const juce::String& name);
    void resized() override;
    void Update();

  private:
    std::atomic<float>* switch_;
    PrismKnob           off_, on_;
};

/** Pill button. Toggle: accent fill when on. Action: red outline, red fill while pressed or lit. */
class Pill : public juce::TextButton
{
  public:
    enum class Kind
    {
        Toggle,
        Action,
        Text // plain serif word (reset, view)
    };
    Pill(const juce::String& text, Kind kind, bool dot = false);
    /** A toggle bound to a bool parameter */
    static std::unique_ptr<Pill> Toggle(APVTS&, const char* id, const juce::String& text);
    /** An action that holds its bool parameter at 1 while pressed */
    static std::unique_ptr<Pill> Action(APVTS&, const char* id, const juce::String& text, bool dot = false);
    int NaturalWidth() const;

  private:
    void buttonStateChanged() override;
    std::unique_ptr<APVTS::ButtonAttachment> attach_;
    juce::RangedAudioParameter*              momentary_ = nullptr;
    bool                                     down_      = false;
};

/** Dropdown: mono caps label, serif value, a hairline underline and a chevron */
class UnderlineCombo : public juce::Component
{
  public:
    /** inline: the label sits to the left of the value on one line, instead of above it */
    UnderlineCombo(APVTS*, const char* paramId, const juce::String& label, bool inlineLabel = false);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::ComboBox box;

  private:
    juce::String                               label_;
    bool                                       inline_ = false;
    std::unique_ptr<APVTS::ComboBoxAttachment> attach_;
};

/** A hand-drawn diagram; redraws only when its inputs change */
class Sketch : public juce::Component
{
  public:
    using Draw  = std::function<void(juce::Graphics&, juce::Rectangle<float>)>;
    using State = std::function<uint32_t()>;
    Sketch(Draw draw, State state) : draw_(std::move(draw)), state_(std::move(state)) { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override { draw_(g, getLocalBounds().toFloat().reduced(1.f)); }
    void Poll()
    {
        if(const auto s = state_(); s != last_ || force_)
        {
            last_  = s;
            force_ = false;
            repaint();
        }
    }
    void Invalidate() { force_ = true; }

  private:
    Draw     draw_;
    State    state_;
    uint32_t last_  = 0;
    bool     force_ = true;
};

/** One effect: number, title and header pills on the first line, then lines of controls. Folds to a strip when off. */
class Module : public juce::Component
{
  public:
    Module(APVTS&, const juce::String& title, const char* onParam);

    void SetNumber(int n);
    void AddHeader(juce::Component&, int width);
    void SetHeaderNote(std::function<juce::String()> note) { note_ = std::move(note); }
    void SetSketch(Sketch::Draw, Sketch::State);
    /** Redraw the sketch if its inputs changed, and re-lay the header if the note changed length */
    void PollSketch();
    void InvalidateSketch()
    {
        if(sketch_)
            sketch_->Invalidate();
    }

    /** Start a line of controls. height 0 = share what's left. */
    void NewLine(int height);
    void AddKnob(juce::Component&);
    void AddFixed(juce::Component&, int width);
    void AddFlexible(juce::Component&, int minWidth);

    int  PreferredWidth() const;
    bool IsCollapsed() const { return collapsed_; }
    /** Big modules don't fold when switched off: they stay in place, dimmed, with the title struck through */
    void SetFolds(bool folds) { folds_ = folds; }
    void Refresh();
    std::function<void()> onCollapse;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

  private:
    struct Item
    {
        enum Kind
        {
            Knob,
            Fixed,
            Flexible
        } kind;
        juce::Component* c;
        int              width;
        int              line;
    };
    int   LineWidth(int line) const;
    float TitleRight() const;
    bool  OnTitle(juce::Point<float>) const;

    APVTS&                                        apvts_;
    juce::String                                  title_;
    int                                           number_    = 0;
    std::atomic<float>*                           on_        = nullptr;
    const char*                                   onId_      = nullptr;
    bool                                          collapsed_ = false;
    bool                                          folds_     = true;
    bool                                          off_       = false;
    std::vector<std::pair<juce::Component*, int>> headers_;
    std::vector<Item>                             items_;
    std::vector<int>                              lineH_;
    std::function<juce::String()>                 note_;
    juce::String                                  lastNote_;
    std::unique_ptr<Sketch>                       sketch_;
};

/** The chain as a sentence: in → Filter, Drive, … → Squash → out. Click a name to switch it, drag to reorder. */
class ChainBar : public juce::Component, public juce::SettableTooltipClient
{
  public:
    explicit ChainBar(PrismProcessor&);
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void Sync();
    void ResetOrder();

  private:
    struct Word
    {
        juce::Rectangle<float> r;
        juce::String           text;
        int                    slot; // -1 for decoration, kNumBlocks for squash
    };
    std::vector<Word> Words() const;
    int               SlotAt(juce::Point<float>) const;
    int               InsertAt(float x) const;
    bool              IsOn(int slot) const;
    void              Toggle(int slot);
    uint32_t          Mask() const;

    PrismProcessor&    proc_;
    prism::BlockOrder  order_;
    uint32_t           mask_     = 0;
    int                dragSlot_ = -1, caret_ = -1;
    juce::Point<float> downAt_;
    bool               moved_ = false;
};

/** A quiet status line: the last control touched (name, a small position bar, value) and some context on the right.
    It only repaints when something it shows changes. */
class StatusLine : public juce::Component
{
  public:
    void SetTouched(const juce::String& name, const juce::String& value, float norm);
    void SetContext(const juce::String& context);
    void paint(juce::Graphics&) override;

  private:
    juce::String name_ = "nothing yet", value_, context_;
    float        norm_ = -1.f;
};

/** The glitch pattern as step pills: glitched steps filled, the playing step in red */
class StepLane : public juce::Component
{
  public:
    explicit StepLane(PrismProcessor&);
    void paint(juce::Graphics&) override;
    void Poll();

  private:
    uint32_t        Key() const;
    PrismProcessor& proc_;
    uint32_t        last_ = 0;
};

/** The loop inside a pill: waveform, loop points and playhead. Click to scrub, drag the points. */
class LoopStrip : public juce::Component
{
  public:
    explicit LoopStrip(PrismProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    /** A loop window on the tape, in samples. It may wrap past the end. */
    struct Win
    {
        size_t start = 0, length = 0;
        bool   operator==(const Win& o) const { return start == o.start && length == o.length; }
        bool   operator!=(const Win& o) const { return !(*this == o); }
    };

  private:
    enum class Grab
    {
        None,
        Start, // the left edge: end stays put
        End,   // the right edge: length changes
        Body   // inside: slide the whole window
    };
    juce::Rectangle<float> Box() const;
    juce::Rectangle<float> Wave() const;
    void                   DrawRuler(juce::Graphics&, juce::Rectangle<float> ruler, size_t len) const;
    /** The window as set (position and length, plus how far wander has moved it): where the handles go */
    Win                    Target(size_t len) const;
    /** The window playing now */
    Win                    Current(size_t len) const;
    /** One or two spans (when it wraps) covering a window, in x */
    std::vector<juce::Range<float>> Spans(Win, size_t len) const;
    juce::String           PosText(Win, size_t len) const;
    juce::String           LenText(Win, size_t len) const;
    float                  XFor(float frac) const;
    float                  FracAt(float x) const;
    Grab                   GrabAt(juce::Point<float>) const;
    void                   Set(const char* id, float value);

    PrismProcessor&        proc_;
    Grab                   grab_ = Grab::None;
    bool                   scrubbing_ = false;
    float                  downFrac_ = 0.f, pos0_ = 0.f, len0_ = 1.f; // at mouse down
    juce::Rectangle<float> startTag_, endTag_;
};

/** A pill you drag out of the plug-in: drops the looping part of the tape into the DAW as a WAV file */
class LoopDrag : public juce::Component, public juce::SettableTooltipClient
{
  public:
    explicit LoopDrag(PrismProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void Poll();
    int  NaturalWidth() const;
    /** Writes the looping part of the tape to a temporary WAV and returns it (empty if there's no loop) */
    juce::File WriteLoop() const;

  private:
    bool       HasLoop() const;
    PrismProcessor& proc_;
    bool            dragging_ = false, had_ = false;
};

/** The looper's movement: one dropdown for how the window wanders, how often, when changes land, and a new seed.
    Looks like an UnderlineCombo; the menu ticks one item in each section. */
class MovesMenu : public juce::Component
{
  public:
    explicit MovesMenu(APVTS&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void Poll();

  private:
    juce::String Text() const;
    void         Set(const char* id, float plain);
    APVTS&       apvts_;
    juce::String last_;
};

/** Controls in a grid, filled row by row; each cell the same size. Optionally a hairline on the left. */
class Grid : public juce::Component
{
  public:
    Grid(std::vector<juce::Component*> items, int cols, int rowH, int gapX, int gapY, bool rule = false);
    void resized() override;
    void paint(juce::Graphics&) override;

  private:
    std::vector<juce::Component*> items_;
    int                           cols_, rowH_, gapX_, gapY_;
    bool                          rule_;
};

/** Controls stacked top to bottom, each a fixed height, centred as a group. Optionally a hairline on the left. */
class Column : public juce::Component
{
  public:
    Column(std::vector<juce::Component*> items, int itemH, int gap, bool rule = false);
    void resized() override;
    void paint(juce::Graphics&) override;

  private:
    std::vector<juce::Component*> items_;
    int                           itemH_, gap_;
    bool                          rule_;
};

/** Holds the UI at its logical size; the editor scales it to the window */
class Canvas : public juce::Component
{
  public:
    std::function<void(juce::Graphics&)> onPaint;
    std::function<void()>                onResize;
    void paint(juce::Graphics& g) override
    {
        if(onPaint)
            onPaint(g);
    }
    void resized() override
    {
        if(onResize)
            onResize();
    }
};
} // namespace prism::ui

class PrismEditor : public juce::AudioProcessorEditor,
                    private juce::Timer,
                    private juce::AudioProcessorValueTreeState::Listener
{
  public:
    explicit PrismEditor(PrismProcessor&);
    ~PrismEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    void         timerCallback() override;
    void         parameterChanged(const juce::String& id, float) override;
    void         PaintCanvas(juce::Graphics&);
    void         LayoutCanvas();
    void         ApplyTheme(bool cream);
    void         ApplyScale(float scale);
    void         ShowViewMenu();
    void         Renumber();

    PrismProcessor&     proc_;
    prism::ui::Look     look_;
    prism::ui::Canvas   canvas_;
    juce::TooltipWindow tooltips_{this, 600};
    float               scale_ = 1.f;

    prism::ui::StatusLine                      status_;
    std::unique_ptr<prism::ui::UnderlineCombo> macros_;
    std::unique_ptr<juce::ParameterAttachment> macrosAttach_;
    std::unique_ptr<prism::ui::PrismKnob>      input_, dryWet_, output_, squash_;
    std::unique_ptr<prism::ui::ChainBar>       chain_;
    prism::ui::Pill                            reset_{"Reset", prism::ui::Pill::Kind::Text};
    prism::ui::Pill                            view_{"View", prism::ui::Pill::Kind::Text};

    std::unique_ptr<prism::ui::Module>            filter_, drive_, tape_, crush_, delay_, reverb_, glitch_, looper_;
    std::vector<std::unique_ptr<juce::Component>> owned_;

    std::vector<juce::Component*>     classicOff_, glitchEvents_;
    std::vector<prism::ui::SwapKnob*> swaps_;
    prism::ui::Pill *                 rec_ = nullptr, *play_ = nullptr;
    prism::ui::StepLane*              stepLane_  = nullptr;
    prism::ui::LoopStrip*             loopStrip_ = nullptr;
    prism::ui::LoopDrag*              loopDrag_  = nullptr;
    prism::ui::MovesMenu*             moves_     = nullptr;
    juce::Rectangle<int>              row1_, row2_;

    std::unique_ptr<juce::ComponentBoundsConstrainer> snap_;
    std::atomic<int>                        touched_{-1};
    juce::StringArray                       paramIds_;
    int                                     blink_ = 0;
    prism::TapeLooper::State                lastLoopState_ = prism::TapeLooper::State::Empty;
    bool                                    lastWaiting_   = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PrismEditor)
};
