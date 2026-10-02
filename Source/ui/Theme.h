// Indie editorial look: one ink on a flat ground, hairline rules, a condensed
// serif with small mono caps, outlined pills. Two themes, Night and Cream.

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace prism::ui
{
struct Theme
{
    juce::Colour bg, ink, muted, accent, red, track;

    static const Theme night;
    static const Theme cream;
};

/** The theme every paint call reads from */
const Theme& T();
void         SetCream(bool cream);
bool         IsCream();

inline juce::Colour Bg() { return T().bg; }
inline juce::Colour Ink() { return T().ink; }
inline juce::Colour Muted() { return T().muted; }
inline juce::Colour Accent() { return T().accent; }
inline juce::Colour Red() { return T().red; }

namespace Fonts
{
/** Instrument Serif, sized like CSS font-size */
juce::Font Serif(float px, bool italic = false);
/** DM Mono, regular or medium */
juce::Font Mono(float px, bool medium = false);
/** Small mono caps label: tracking added */
juce::Font Caps(float px = 11.f, bool medium = false);
} // namespace Fonts
} // namespace prism::ui
