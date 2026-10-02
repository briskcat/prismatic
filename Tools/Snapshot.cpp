// Dev tool: renders the plug-in editor to a PNG, for checking the UI without a DAW.
//   PrismSnapshot out.png [paramId=value ...] [--cream] [--loop]
// --loop records four bars of drum-like hits into the looper first, so the waveform has something to show.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/ui/Theme.h"
#include "../Source/PluginEditor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    PrismProcessor proc;
    for(int i = 2; i < argc; ++i)
        if(juce::String(argv[i]) == "--loop")
        {
            constexpr double sr = 48000.0;
            constexpr int    block = 512;
            proc.setPlayConfigDetails(2, 2, sr, block);
            proc.prepareToPlay(sr, block);
            juce::AudioBuffer<float> buf(2, block);
            juce::MidiBuffer         midi;
            juce::Random             rng(7);
            int64_t                  n = 0;
            auto fill = [&] {
                for(int i = 0; i < block; ++i, ++n)
                {
                    const double t   = (double)(n % 12000) / sr; // a hit every 1/8 note at 120 bpm
                    const float  acc = (n / 12000) % 4 == 0 ? 1.f : .45f + .3f * (float)((n / 12000) % 3) / 2.f;
                    const float  x   = acc * .8f * (float)std::exp(-t * 18.0) * (rng.nextFloat() * 2.f - 1.f);
                    buf.setSample(0, i, x);
                    buf.setSample(1, i, x);
                }
            };
            auto press = [&](const char* id) {
                auto* param = proc.apvts.getParameter(id);
                param->setValueNotifyingHost(1.f);
                fill();
                proc.processBlock(buf, midi);
                param->setValueNotifyingHost(0.f);
            };
            press(prism::ids::loopRec);
            for(int b = 0; b < int(sr * 8.0 / block); ++b) { fill(); proc.processBlock(buf, midi); }
            press(prism::ids::loopPlay);
            for(int b = 0; b < int(sr * 1.3 / block); ++b) { fill(); proc.processBlock(buf, midi); }
        }
    for(int i = 2; i < argc; ++i)
    {
        const auto arg = juce::String(argv[i]);
        if(auto* p = proc.apvts.getParameter(arg.upToFirstOccurrenceOf("=", false, false)))
            p->setValueNotifyingHost(p->convertTo0to1(arg.fromFirstOccurrenceOf("=", false, false).getFloatValue()));
    }
    // let the looper pick up any loop settings from the command line
    if(proc.Looper().GetLength() > 0)
    {
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer         midi;
        for(int b = 0; b < 40; ++b)
        {
            buf.clear();
            proc.processBlock(buf, midi);
        }
    }
    // night theme unless --cream (the theme lives in the plug-in's state)
    for(int i = 2; i < argc; ++i)
        if(juce::String(argv[i]) == "--cream")
            proc.apvts.state.setProperty("uiTheme", "cream", nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    juce::ignoreUnused(editor);
    // --drag=x0,x1: drag on the loop waveform from x0 to x1 (strip coordinates), to test the handles
    for(int i = 2; i < argc; ++i)
        if(juce::String(argv[i]).startsWith("--drag="))
        {
            auto xs = juce::StringArray::fromTokens(juce::String(argv[i]).fromFirstOccurrenceOf("=", false, false), ",", "");
            std::function<juce::Component*(juce::Component*)> find = [&](juce::Component* c) -> juce::Component* {
                if(dynamic_cast<prism::ui::LoopStrip*>(c) != nullptr)
                    return c;
                for(auto* ch : c->getChildren())
                    if(auto* f = find(ch))
                        return f;
                return nullptr;
            };
            if(auto* strip = find(editor.get()))
            {
                auto src = juce::Desktop::getInstance().getMainMouseSource();
                auto ev  = [&](float x, bool drag) {
                    const juce::Point<float> pt(x, strip->getHeight() * .5f), down(xs[0].getFloatValue(), pt.y);
                    return juce::MouseEvent(src, pt, juce::ModifierKeys::leftButtonModifier, 0.f, 0.f, 0.f, 0.f, 0.f, strip, strip,
                                            juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, drag);
                };
                std::printf("strip %d wide; start %.3f end %.3f\n", strip->getWidth(),
                            proc.apvts.getRawParameterValue("loop_start")->load(), proc.apvts.getRawParameterValue("loop_end")->load());
                strip->mouseDown(ev(xs[0].getFloatValue(), false));
                strip->mouseDrag(ev(xs[1].getFloatValue(), true));
                strip->mouseUp(ev(xs[1].getFloatValue(), true));
                std::printf("after drag: start %.3f end %.3f\n", proc.apvts.getRawParameterValue("loop_start")->load(),
                            proc.apvts.getRawParameterValue("loop_end")->load());
            }
        }
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.f);
    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "snapshot.png"));
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat().writeImageToStream(image, stream);
    std::printf("%s (%dx%d)\n", out.getFullPathName().toRawUTF8(), editor->getWidth(), editor->getHeight());
    return 0;
}
