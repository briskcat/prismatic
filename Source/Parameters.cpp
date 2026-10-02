#include "Parameters.h"
#include "dsp/GlitchDelay.h"

namespace prism
{
namespace
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;

juce::ParameterID Id(const char* id) { return {id, 1}; }

juce::AudioParameterFloatAttributes Percent()
{
    return juce::AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return juce::String(juce::roundToInt(v * 100.f)) + "%"; });
}

std::unique_ptr<APF> Knob(const char* id, const char* name, float def)
{
    return std::make_unique<APF>(Id(id), name, juce::NormalisableRange<float>(0.f, 1.f), def, Percent());
}

std::unique_ptr<APF> Decibels(const char* id, const char* name, float lo, float hi, float def)
{
    return std::make_unique<APF>(
        Id(id), name, juce::NormalisableRange<float>(lo, hi, .1f), def,
        juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " dB"; }));
}

std::unique_ptr<APB> Switch(const char* id, const char* name, bool def)
{
    return std::make_unique<APB>(Id(id), name, def);
}

template <size_t N>
juce::StringArray Names(const NoteDiv (&divs)[N])
{
    juce::StringArray out;
    for(const auto& d : divs)
        out.add(d.name);
    return out;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout CreateParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // global
    layout.add(Switch(ids::classic, "Classic Macros", false));
    layout.add(Decibels(ids::input, "Input", -24.f, 24.f, 0.f));
    layout.add(Decibels(ids::output, "Output", -24.f, 24.f, 0.f));
    layout.add(Knob(ids::dryWet, "Dry/Wet", 1.f));

    // filter
    layout.add(Switch(ids::filterOn, "Filter On", true));
    layout.add(std::make_unique<APF>(
        Id(ids::cutoff), "Filter", juce::NormalisableRange<float>(0.f, 1.f), .5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
            if(std::abs(v - .5f) < .01f)
                return juce::String("Open");
            return (v < .5f ? juce::String("LP ") : juce::String("HP "))
                   + juce::String(juce::roundToInt(std::abs(v - .5f) * 200.f)) + "%";
        })));
    layout.add(Knob(ids::resonance, "Resonance", 0.f));

    // drive
    layout.add(Switch(ids::driveOn, "Drive On", true));
    layout.add(Knob(ids::drive, "Drive", 0.f));

    // tape
    layout.add(Switch(ids::tapeOn, "Tape On", true));
    layout.add(Knob(ids::warbleDepth, "Warble", 0.f));
    layout.add(Knob(ids::warbleRate, "Warble Rate", .3f));

    // crush
    layout.add(Switch(ids::crushOn, "Crush On", false));
    {
        juce::NormalisableRange<float> range(200.f, 48000.f);
        range.setSkewForCentre(4000.f);
        layout.add(std::make_unique<APF>(
            Id(ids::crushRate), "Crush Rate", range, 12000.f,
            juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction([](float v, int) {
                return v >= 1000.f ? juce::String(v / 1000.f, 1) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
            })));
    }
    layout.add(std::make_unique<APF>(
        Id(ids::crushBits), "Bits", juce::NormalisableRange<float>(2.f, 16.f), 12.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " bit"; })));
    layout.add(Knob(ids::crushMix, "Crush Mix", 1.f));

    // delay
    layout.add(Switch(ids::delayOn, "Delay On", true));
    layout.add(Switch(ids::delaySync, "Delay Sync", false));
    {
        juce::NormalisableRange<float> range(10.f, 2000.f);
        range.setSkewForCentre(300.f);
        layout.add(std::make_unique<APF>(
            Id(ids::delayTime), "Delay Time", range, 375.f,
            juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction([](float v, int) {
                return v >= 1000.f ? juce::String(v / 1000.f, 2) + " s" : juce::String(juce::roundToInt(v)) + " ms";
            })));
    }
    layout.add(std::make_unique<APC>(Id(ids::delayDiv), "Delay Division", Names(kDelayDivs), 6));
    layout.add(Knob(ids::delayFeedback, "Delay Feedback", .35f));
    layout.add(Knob(ids::delayMix, "Delay Mix", 0.f));
    layout.add(Switch(ids::delayVari, "Delay Varispeed", true));

    // glitch
    layout.add(Switch(ids::glitchOn, "Glitch On", false));
    layout.add(std::make_unique<APC>(Id(ids::glitchMode), "Glitch Mode", juce::StringArray{"Glitch", "Shimmer"}, 0));
    {
        juce::StringArray names;
        for(int i = 0; i < glitch::kNumDivs; ++i)
            names.add(GlitchDelay::DivisionName(i));
        layout.add(std::make_unique<APC>(Id(ids::glitchDiv), "Glitch Division", names, 2));
    }
    layout.add(std::make_unique<APC>(Id(ids::glitchRate), "Glitch Rate", Names(kGlitchRates), 1));
    layout.add(Knob(ids::glitchChaos, "Chaos", .3f));
    layout.add(Knob(ids::glitchFeedback, "Glitch Feedback", .3f));
    layout.add(Knob(ids::glitchMix, "Glitch Mix", .5f));
    layout.add(Switch(ids::glitchFreeze, "Glitch Freeze", false));
    layout.add(Switch(ids::glitchRetrig, "Glitch Retrigger", true));
    layout.add(Switch(ids::glitchReverse, "Glitch Reverse", true));
    layout.add(Switch(ids::glitchOctUp, "Glitch Octave Up", true));
    layout.add(Switch(ids::glitchOctDown, "Glitch Octave Down", true));
    layout.add(Knob(ids::glitchSpread, "Glitch Spread", .3f));
    layout.add(std::make_unique<APC>(Id(ids::glitchPattern), "Glitch Pattern",
                                     juce::StringArray{"Free", "1 bar", "2 bars", "4 bars"}, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(Id(ids::glitchSeed), "Glitch Seed", 1, 64, 1));

    // reverb
    layout.add(Switch(ids::reverbOn, "Reverb On", true));
    layout.add(Knob(ids::reverbMix, "Reverb Mix", 0.f));
    layout.add(Knob(ids::reverbDecay, "Decay", .5f));
    layout.add(Knob(ids::reverbTone, "Tone", .6f));
    layout.add(Knob(ids::reverbDiffusion, "Diffusion", .7f));
    layout.add(Switch(ids::reverbFreeze, "Reverb Freeze", false));

    // squash
    layout.add(Switch(ids::squashOn, "Squash On", false));
    layout.add(Knob(ids::squash, "Squash", .3f));

    // looper
    layout.add(Switch(ids::loopRec, "Loop Record", false));
    layout.add(Switch(ids::loopPlay, "Loop Play/Stop", false));
    layout.add(Switch(ids::loopClear, "Loop Clear", false));
    {
        juce::NormalisableRange<float> range(.25f, 2.f);
        range.setSkewForCentre(1.f);
        layout.add(std::make_unique<APF>(
            Id(ids::loopSpeed), "Loop Speed", range, 1.f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float v, int) { return juce::String(v, 2) + "x"; })));
    }
    layout.add(Switch(ids::loopSpeedSteps, "Loop Speed Steps", false));
    layout.add(Switch(ids::loopReverse, "Loop Reverse", false));
    layout.add(Knob(ids::loopGlide, "Loop Glide", .2f));
    layout.add(Knob(ids::loopDub, "Loop Dub Keep", 1.f));
    layout.add(Knob(ids::loopLevel, "Loop Level", 1.f));
    layout.add(Knob(ids::loopScrub, "Loop Scrub", 0.f));
    layout.add(std::make_unique<APC>(Id(ids::loopQuantize), "Loop Quantize", juce::StringArray{"Off", "Beat", "Bar"}, 0));
    layout.add(std::make_unique<APC>(Id(ids::loopLength), "Loop Length",
                                     juce::StringArray{"Free", "1 bar", "2 bars", "4 bars", "8 bars"}, 0));
    layout.add(Switch(ids::loopSave, "Save Loop With Project", true));
    {
        juce::StringArray names;
        for(auto* n : kSpeedStepNames)
            names.add(n);
        layout.add(std::make_unique<APC>(Id(ids::loopSpeedStep), "Loop Speed Step", names, 4));
    }
    layout.add(Knob(ids::loopPos, "Loop Position", 0.f));
    layout.add(Knob(ids::loopLen, "Loop Length", 1.f));
    layout.add(std::make_unique<APC>(Id(ids::loopSnap), "Loop Point Snap", Names(kLoopSnaps), 0));
    layout.add(Knob(ids::loopWander, "Loop Wander", 0.f));
    layout.add(std::make_unique<APC>(Id(ids::loopMoves), "Loop Movement", juce::StringArray(kLoopMoves, 3), 0));
    layout.add(std::make_unique<APC>(Id(ids::loopEvery), "Loop Movement Every", juce::StringArray(kLoopEveryNames, 4), 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(Id(ids::loopSeed), "Loop Movement Seed", 1, 999, 1));
    layout.add(std::make_unique<APC>(Id(ids::loopLand), "Loop Changes Land", juce::StringArray{"End of pass", "Right away"}, 0));

    return layout;
}
} // namespace prism
