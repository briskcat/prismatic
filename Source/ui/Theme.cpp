#include "Theme.h"
#include "BinaryData.h"

namespace prism::ui
{
const Theme Theme::night{juce::Colour(0xff161616), juce::Colour(0xffece8de), juce::Colour(0xff8f8a80),
                         juce::Colour(0xffd6f35c), juce::Colour(0xffff7354), juce::Colour(0xffece8de).withAlpha(.28f)};
const Theme Theme::cream{juce::Colour(0xfff2f0e8), juce::Colour(0xff1b1a18), juce::Colour(0xff6f6b62),
                         juce::Colour(0xff3a62ea), juce::Colour(0xffd23a28), juce::Colour(0xff1b1a18).withAlpha(.25f)};

namespace
{
bool cream = false;
juce::Typeface::Ptr Load(const void* d, size_t n) { return juce::Typeface::createSystemTypefaceFor(d, n); }
} // namespace

const Theme& T() { return cream ? Theme::cream : Theme::night; }
void         SetCream(bool c) { cream = c; }
bool         IsCream() { return cream; }

namespace Fonts
{
juce::Font Serif(float px, bool italic)
{
    static auto regular = Load(BinaryData::InstrumentSerifRegular_ttf, BinaryData::InstrumentSerifRegular_ttfSize);
    static auto ital    = Load(BinaryData::InstrumentSerifItalic_ttf, BinaryData::InstrumentSerifItalic_ttfSize);
    return juce::Font(juce::FontOptions(italic ? ital : regular).withPointHeight(px));
}

juce::Font Mono(float px, bool medium)
{
    static auto regular = Load(BinaryData::DMMonoRegular_ttf, BinaryData::DMMonoRegular_ttfSize);
    static auto med     = Load(BinaryData::DMMonoMedium_ttf, BinaryData::DMMonoMedium_ttfSize);
    return juce::Font(juce::FontOptions(medium ? med : regular).withPointHeight(px));
}

juce::Font Caps(float px, bool medium) { return Mono(px, medium); } // tracking is added when drawn
} // namespace Fonts
} // namespace prism::ui
