#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/FxChain.h"

class PrismProcessor : public juce::AudioProcessor
{
  public:
    PrismProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- routing (message thread) ----
    prism::BlockOrder GetOrder() const { return Unpack(order_.load()); }
    void              SetOrder(const prism::BlockOrder& order);

    // ---- looper, for the UI ----
    const prism::TapeLooper& Looper() const { return chain_->Looper(); }
    /** A button press is waiting for the next beat or bar */
    bool   LooperWaiting() const { return pendingCmd_.load() != kNone; }
    double SampleRate() const { return sampleRate_; }
    std::atomic<float> uiBpm{120.f};

    /** The loop window in samples for start/end (0-1), snapped to unitLen samples when it's above 0.
        Shared by the audio thread and the waveform, so the handles show exactly where the loop will be. */
    static std::pair<size_t, size_t> LoopWindow(size_t loopLen, double unitLen, float startFrac, float endFrac);
    /** Snap length in samples for the current snap setting, tempo and sample rate (0 = free) */
    double LoopSnapUnit() const;
    /** Which glitch step (dice roll) is playing, within its pattern; for the UI's step lane */
    std::atomic<int>   uiGlitchStep{0};

  private:
    enum Cmd
    {
        kNone,
        kRec,
        kPlay,
        kClear
    };

    prism::FxParams ReadParams(float bpm) const;
    void            ReadLooperButtons();
    void            RunLooperCommand(int cmd);

    float Raw(const char* id) const { return apvts.getRawParameterValue(id)->load(); }
    bool  On(const char* id) const { return Raw(id) > .5f; }

    static uint32_t          Pack(const prism::BlockOrder&);
    static prism::BlockOrder Unpack(uint32_t);
    static bool              Valid(const prism::BlockOrder&);

    std::unique_ptr<prism::FxChain> chain_;
    double sampleRate_   = 48000.0;
    double preparedRate_ = 0.0;

    // host-synced clock
    double  beat_        = 0.0;
    float   bpm_         = 120.f;
    int     beatsPerBar_ = 4;
    int64_t lastEdge_    = -1;
    int64_t lastPulse_   = -1;

    // looper buttons are momentary parameters: act on the press
    bool             lastRec_ = false, lastPlay_ = false, lastClear_ = false;
    std::atomic<int> pendingCmd_{kNone};
    int64_t          pendingBoundary_ = 0;

    std::atomic<uint32_t> order_;

    // Loop audio from a saved project. Loaded on the message thread and copied
    // into the looper under the lock, which the audio thread only try-locks.
    juce::SpinLock           loopLock_;
    juce::AudioBuffer<float> pendingLoop_;
    bool                     hasPendingLoop_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PrismProcessor)
};
