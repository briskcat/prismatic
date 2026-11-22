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

    bool ok = b.Looper().GetLength() == len && len > 0 && b.GetOrder() == order;
    std::printf("loop %zu -> %zu samples, state %.1f KB, routing %s: %s\n", len, b.Looper().GetLength(),
                state.getSize() / 1024.0, b.GetOrder() == order ? "kept" : "lost", ok ? "PASS" : "FAIL");

    // REC, then REC again, closes the loop into plain playback (no overdub);
    // a third REC starts the overdub.
    {
        PrismProcessor c;
        c.setPlayConfigDetails(2, 2, sr, block);
        c.prepareToPlay(sr, block);
        fill(); Press(c, prism::ids::loopRec, buf, midi);
        for(int i = 0; i < int(sr / block); ++i) { fill(); c.processBlock(buf, midi); }
        fill(); Press(c, prism::ids::loopRec, buf, midi);
        const bool playing = c.Looper().GetState() == prism::TapeLooper::State::Playing && !c.Looper().IsDubbing();
        fill(); c.processBlock(buf, midi); // let go of the button between presses
        fill(); Press(c, prism::ids::loopRec, buf, midi);
        const bool dubbing = c.Looper().IsDubbing();
        const bool pass = playing && dubbing;
        std::printf("REC closes the loop into playback, next REC overdubs: %s\n", pass ? "PASS" : "FAIL");
        ok = ok && pass;
    }

    // Mix, straight on the looper: noon = input + loop, 0 = input only, 1 = loop only.
    // The loop holds a 330 Hz tone; the input is a 550 Hz tone.
    {
        prism::RateScale rate;
        rate.Init(48000.f);
        prism::TapeLooper lp;
        lp.Init(rate);
        auto tone = [](double hz, long n) { return .3f * (float)std::sin(2.0 * juce::MathConstants<double>::pi * hz * n / 48000.0); };
        long n = 0;
        lp.PressRecord();
        for(; n < 48000; ++n) { float l = tone(330, n), r = l; lp.Process(&l, &r); }
        lp.PressRecord();
        // energy at a frequency over the last half second of a 1 s run
        auto run = [&](float mix) {
            lp.SetMix(mix);
            double re3 = 0, im3 = 0, re5 = 0, im5 = 0;
            for(long k = 0; k < 48000; ++k, ++n)
            {
                float l = tone(550, n), r = l;
                lp.Process(&l, &r);
                if(k >= 24000)
                {
                    const double w3 = 2.0 * juce::MathConstants<double>::pi * 330.0 * n / 48000.0, w5 = w3 * 550.0 / 330.0;
                    re3 += l * std::cos(w3); im3 += l * std::sin(w3); re5 += l * std::cos(w5); im5 += l * std::sin(w5);
                }
            }
            const double loop = std::sqrt(re3 * re3 + im3 * im3) / 12000.0, input = std::sqrt(re5 * re5 + im5 * im5) / 12000.0;
            return std::pair{loop, input};
        };
        const auto mid = run(.5f), dry = run(0.f), loopOnly = run(1.f);
        // The knob from full loop to full input in one go, with the input silent so
        // only the loop moves. Four tries a quarter cycle apart, so one jump lands
        // where the loop is loud. The loop's own wave moves at most .013 a sample.
        float jump = 0.f;
        for(long offset : {0L, 37L, 74L, 111L})
        {
            float prev = 0.f;
            lp.SetMix(1.f);
            for(long k = 0; k < 9600 + offset; ++k, ++n) { float l = 0.f, r = 0.f; lp.Process(&l, &r); prev = l; }
            lp.SetMix(0.f);
            for(long k = 0; k < 4800; ++k, ++n)
            {
                float l = 0.f, r = 0.f;
                lp.Process(&l, &r);
                jump = std::max(jump, std::fabs(l - prev));
                prev = l;
            }
        }
        std::printf("mix: noon loop %.3f input %.3f; 0 loop %.3f input %.3f; 1 loop %.3f input %.3f; biggest step on a jump %.3f\n",
                    mid.first, mid.second, dry.first, dry.second, loopOnly.first, loopOnly.second, jump);
        const bool pass = mid.first > .25 && mid.second > .25 && dry.first < .01 && dry.second > .25
                       && loopOnly.first > .25 && loopOnly.second < .01 && jump < .03f;
        std::printf("Loop Mix blends input and loop, and glides: %s\n", pass ? "PASS" : "FAIL");
        ok = ok && pass;
    }
    return ok ? 0 : 1;
}
