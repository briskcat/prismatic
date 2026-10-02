// Dev tool: renders the plug-in editor to a PNG, for checking the UI without a DAW.
//   PrismSnapshot out.png [paramId=value ...] [--cream]
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/ui/Theme.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    PrismProcessor proc;
    for(int i = 2; i < argc; ++i)
    {
        const auto arg = juce::String(argv[i]);
        if(auto* p = proc.apvts.getParameter(arg.upToFirstOccurrenceOf("=", false, false)))
            p->setValueNotifyingHost(p->convertTo0to1(arg.fromFirstOccurrenceOf("=", false, false).getFloatValue()));
    }
    // night theme unless --cream (the theme lives in the plug-in's state)
    for(int i = 2; i < argc; ++i)
        if(juce::String(argv[i]) == "--cream")
            proc.apvts.state.setProperty("uiTheme", "cream", nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    juce::ignoreUnused(editor);
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.f);
    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "snapshot.png"));
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat().writeImageToStream(image, stream);
    std::printf("%s (%dx%d)\n", out.getFullPathName().toRawUTF8(), editor->getWidth(), editor->getHeight());
    return 0;
}
