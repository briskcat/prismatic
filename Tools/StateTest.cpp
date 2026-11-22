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
    // Reverb (Stray's Space plate), through the chain with every other block off.
    {
        auto fx = std::make_unique<prism::FxChain>();
        fx->Init(48000.f);
        prism::FxParams p;
        p.filter_on = p.drive_on = p.tape_on = p.delay_on = false;
        p.reverb_on = true;
        p.reverb_mix = .5f;
        p.reverb_decay = .5f;
        fx->SetParams(p);
        fx->Snap();
        juce::Random rng(7);
        auto noise = [&] { return .3f * (rng.nextFloat() * 2.f - 1.f); };
        auto rms = [&](long len, bool feed, float* peak = nullptr) {
            double e = 0;
            for(long k = 0; k < len; ++k)
            {
                float l = feed ? noise() : 0.f, r = feed ? noise() : 0.f;
                fx->Process(&l, &r, 1);
                if(!std::isfinite(l)) return 1e9;
                e += .5 * (l * l + r * r);
                if(peak) *peak = std::max(*peak, std::max(std::fabs(l), std::fabs(r)));
            }
            return std::sqrt(e / len);
        };
        const double inRms = .3 / std::sqrt(3.0);
        rms(48000, true);
        const double half = rms(96000, true) / inRms;
        p.reverb_mix = 1.f; fx->SetParams(p);
        rms(48000, true);
        const double full = rms(96000, true) / inRms;
        // Full decay for 10 s of noise: must stay bounded.
        p.reverb_decay = 1.f; fx->SetParams(p);
        float peak = 0.f;
        rms(480000, true, &peak);
        // Freeze holds the tail; without it a medium decay dies away.
        p.reverb_decay = .5f; p.reverb_freeze = true; fx->SetParams(p);
        rms(4800, true);
        const double frozen0 = rms(24000, false), frozen4 = (rms(168000, false), rms(24000, false));
        p.reverb_freeze = false; fx->SetParams(p);
        rms(48000, true);
        const double free0 = rms(24000, false), free4 = (rms(168000, false), rms(24000, false));
        // Mix and Decay jumped end to end with a 220 Hz tone in: biggest one-sample
        // step, four tries at different points of the wave. The tone itself moves
        // at most .009 a sample.
        float jump = 0.f;
        long  n = 0;
        auto tone = [&] { return .3f * (float)std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * (n++) / 48000.0); };
        for(long offset : {0L, 27L, 54L, 81L})
            for(int which = 0; which < 2; ++which)
            {
                p.reverb_mix = which == 0 ? 0.f : 1.f; p.reverb_decay = 0.f; fx->SetParams(p);
                float prev = 0.f;
                for(long k = 0; k < 24000 + offset; ++k) { float l = tone(), r = l; fx->Process(&l, &r, 1); prev = l; }
                if(which == 0) p.reverb_mix = 1.f; else p.reverb_decay = 1.f;
                fx->SetParams(p);
                float last = 0.f;
                for(long k = 0; k < 4800; ++k)
                {
                    float l = tone(), r = l;
                    fx->Process(&l, &r, 1);
                    // compare against the tone's own step so only the knob's jump counts
                    jump = std::max(jump, std::fabs((l - prev) - last));
                    last = l - prev;
                    prev = l;
                }
            }
        std::printf("reverb: level vs dry at mix .5 %.2f, mix 1 %.2f; peak at full decay %.2f; frozen tail %.3f -> %.3f; free tail %.3f -> %.4f; biggest knob step %.4f\n",
                    half, full, peak, frozen0, frozen4, free0, free4, jump);
        const bool pass = half > .6 && half < 1.4 && full > .4 && full < 1.4 && peak < 1.2f
                       && frozen4 > .7 * frozen0 && frozen0 > .02 && free4 < .02 * free0 && jump < .02f;
        std::printf("Reverb level, freeze and knob glide: %s\n", pass ? "PASS" : "FAIL");
        ok = ok && pass;
    }
    return ok ? 0 : 1;
}
