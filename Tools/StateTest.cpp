// Dev test: records a loop through the real processor, saves the plug-in state,
// restores it into a fresh instance (as a DAW does when opening a project) and
// checks the loop and routing came back.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"

static void Press(PrismProcessor& p, const char* id, juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi)
{
    auto* param = p.apvts.getParameter(id);
    param->setValueNotifyingHost(1.f);
    p.processBlock(buf, midi);
    param->setValueNotifyingHost(0.f);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    constexpr double sr = 48000.0;
    constexpr int    block = 512;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, block);
    double phase = 0.0;
    auto fill = [&] {
        for(int i = 0; i < block; ++i, phase += 2.0 * juce::MathConstants<double>::pi * 330.0 / sr)
            buf.setSample(0, i, 0.4f * (float)std::sin(phase)), buf.setSample(1, i, 0.4f * (float)std::sin(phase));
    };

    PrismProcessor a;
    a.setPlayConfigDetails(2, 2, sr, block);
    a.prepareToPlay(sr, block);
    fill(); Press(a, prism::ids::loopRec, buf, midi);
    for(int b = 0; b < int(sr * 1.5 / block); ++b) { fill(); a.processBlock(buf, midi); }
    fill(); Press(a, prism::ids::loopPlay, buf, midi);
    auto order = prism::DefaultOrder();
    std::swap(order[0], order[7]);
    a.SetOrder(order);
    const size_t len = a.Looper().GetLength();

    juce::MemoryBlock state;
    a.getStateInformation(state);

    PrismProcessor b;
    b.setStateInformation(state.getData(), (int)state.getSize()); // before prepare, like most hosts
    b.setPlayConfigDetails(2, 2, sr, block);
    b.prepareToPlay(sr, block);
    b.prepareToPlay(sr, block * 2); // hosts re-prepare; the loop must survive

    const bool ok = b.Looper().GetLength() == len && len > 0 && b.GetOrder() == order;
    std::printf("loop %zu -> %zu samples, state %.1f KB, routing %s: %s\n", len, b.Looper().GetLength(),
                state.getSize() / 1024.0, b.GetOrder() == order ? "kept" : "lost", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
