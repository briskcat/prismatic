#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace prism;

namespace
{
constexpr int kStateMagic   = 0x4D535250; // "PRSM"
constexpr int kStateVersion = 1;
const juce::Identifier kRoutingProp{"routing"};

} // namespace

PrismProcessor::PrismProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", CreateParameterLayout()),
      chain_(std::make_unique<FxChain>()),
      order_(Pack(DefaultOrder()))
{
}

bool PrismProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if(out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void PrismProcessor::prepareToPlay(double sampleRate, int)
{
    const juce::SpinLock::ScopedLockType lock(loopLock_);

    // Hosts re-prepare for many reasons (buffer size, transport...). Keep the
    // loop unless the sample rate changed.
    const bool newRate = std::abs(sampleRate - preparedRate_) > .5;
    sampleRate_        = sampleRate;
    chain_->Init(static_cast<float>(sampleRate), newRate);
    chain_->SetParams(ReadParams(bpm_));
    chain_->SetOrder(GetOrder());
    chain_->Snap();
    preparedRate_ = sampleRate;

    if(hasPendingLoop_)
    {
        chain_->Looper().Load(pendingLoop_.getReadPointer(0), pendingLoop_.getReadPointer(1),
                              static_cast<size_t>(pendingLoop_.getNumSamples()));
        pendingLoop_.setSize(0, 0);
        hasPendingLoop_ = false;
    }

    beat_     = 0.0;
    lastEdge_ = lastPulse_ = -1;
    pendingCmd_ = kNone;
}

FxParams PrismProcessor::ReadParams(float bpm) const
{
    FxParams p;
    p.classic     = On(ids::classic);
    p.input_gain  = juce::Decibels::decibelsToGain(Raw(ids::input));
    p.output_gain = juce::Decibels::decibelsToGain(Raw(ids::output));
    p.dry_wet     = Raw(ids::dryWet);

    p.filter_on = On(ids::filterOn);
    p.cutoff    = Raw(ids::cutoff);
    p.resonance = Raw(ids::resonance);

    p.drive_on = On(ids::driveOn);
    p.drive    = Raw(ids::drive);

    p.tape_on      = On(ids::tapeOn);
    p.warble_depth = Raw(ids::warbleDepth);
    p.warble_rate  = Raw(ids::warbleRate);

    p.crush_on      = On(ids::crushOn);
    p.crush_rate_hz = Raw(ids::crushRate);
    p.crush_bits    = Raw(ids::crushBits);
    p.crush_mix     = Raw(ids::crushMix);

    p.delay_on = On(ids::delayOn);
    if(On(ids::delaySync))
    {
        const int div   = juce::jlimit(0, (int)std::size(kDelayDivs) - 1, (int)Raw(ids::delayDiv));
        p.delay_seconds = kDelayDivs[div].beats * 60.f / bpm;
    }
    else
    {
        p.delay_seconds = Raw(ids::delayTime) * .001f;
    }
    p.delay_seconds = juce::jlimit(.001f, TapeDelay::kMaxSeconds - .01f, p.delay_seconds);
    // Where the TAPE time knob would sit for this delay time; Classic mode uses
    // it to set the reverb decay. (Firmware curve: .99 * v^3 * 96256 + 450 samples.)
    p.delay_time_norm = std::cbrt(juce::jmax(0.f, (p.delay_seconds * 48000.f - 450.f) / (.99f * 96256.f)));
    p.delay_feedback  = Raw(ids::delayFeedback);
    p.delay_mix       = Raw(ids::delayMix);
    p.delay_varispeed = On(ids::delayVari);

    p.glitch_on       = On(ids::glitchOn);
    p.glitch_mode     = (int)Raw(ids::glitchMode);
    p.glitch_division = (int)Raw(ids::glitchDiv);
    p.glitch_chaos    = Raw(ids::glitchChaos);
    p.glitch_feedback = Raw(ids::glitchFeedback);
    p.glitch_mix      = Raw(ids::glitchMix);
    p.glitch_freeze   = On(ids::glitchFreeze);

    p.reverb_on        = On(ids::reverbOn);
    p.reverb_mix       = Raw(ids::reverbMix);
    p.reverb_decay     = Raw(ids::reverbDecay);
    p.reverb_tone      = Raw(ids::reverbTone);
    p.reverb_diffusion = Raw(ids::reverbDiffusion);
    p.reverb_freeze    = On(ids::reverbFreeze);

    p.squash_on = On(ids::squashOn);
    p.squash    = Raw(ids::squash);

    const int step   = juce::jlimit(0, (int)std::size(kSpeedSteps) - 1, (int)Raw(ids::loopSpeedStep));
    p.looper_speed   = On(ids::loopSpeedSteps) ? kSpeedSteps[step] : Raw(ids::loopSpeed);
    p.looper_reverse = On(ids::loopReverse);
    p.looper_glide   = Raw(ids::loopGlide);
    p.looper_dub     = Raw(ids::loopDub);
    p.looper_level   = Raw(ids::loopLevel);
    p.looper_scrub   = Raw(ids::loopScrub);
    return p;
}

void PrismProcessor::ReadLooperButtons()
{
    auto pressed = [this](const char* id, bool& last) {
        const bool now  = On(id);
        const bool edge = now && !last;
        last            = now;
        return edge;
    };
    int cmd = kNone;
    if(pressed(ids::loopRec, lastRec_))
        cmd = kRec;
    if(pressed(ids::loopPlay, lastPlay_))
        cmd = kPlay;
    if(pressed(ids::loopClear, lastClear_))
        cmd = kClear;
    if(cmd == kNone)
        return;

    const int quantize = (int)Raw(ids::loopQuantize); // 0 off, 1 beat, 2 bar
    if(quantize == 0 || cmd == kClear)
    {
        RunLooperCommand(cmd);
        return;
    }
    // pressing again while waiting cancels
    if(pendingCmd_.load() == cmd)
    {
        pendingCmd_ = kNone;
        return;
    }
    const double unit = quantize == 1 ? 1.0 : static_cast<double>(beatsPerBar_);
    pendingBoundary_  = static_cast<int64_t>(std::floor(beat_ / unit)) + 1;
    pendingCmd_       = cmd;
}

void PrismProcessor::RunLooperCommand(int cmd)
{
    auto& looper = chain_->Looper();
    if(cmd == kRec)
        looper.PressRecord();
    else if(cmd == kPlay)
        looper.PressPlay();
    else if(cmd == kClear)
        looper.PressClear();
}

void PrismProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    if(getTotalNumInputChannels() == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, n);

    // a saved loop is being loaded: pass audio through for this block
    const juce::SpinLock::ScopedTryLockType lock(loopLock_);
    if(!lock.isLocked())
        return;

    // tempo and position from the host; free-run at the last tempo when stopped
    if(auto* ph = getPlayHead())
    {
        if(auto pos = ph->getPosition())
        {
            if(auto bpm = pos->getBpm())
                bpm_ = static_cast<float>(*bpm);
            if(auto sig = pos->getTimeSignature())
                beatsPerBar_ = juce::jmax(1, sig->numerator);
            if(pos->getIsPlaying())
                if(auto ppq = pos->getPpqPosition())
                    beat_ = *ppq;
        }
    }

    chain_->SetParams(ReadParams(bpm_));
    chain_->SetOrder(GetOrder());
    ReadLooperButtons();

    auto& glitch = chain_->Glitch();
    glitch.SetTempo(bpm_);
    glitch.SetClassic(On(ids::classic));
    glitch.SetEvents((On(ids::glitchRetrig) ? 1u : 0u) | (On(ids::glitchReverse) ? 2u : 0u)
                     | (On(ids::glitchOctUp) ? 4u : 0u) | (On(ids::glitchOctDown) ? 8u : 0u));
    glitch.SetSpread(Raw(ids::glitchSpread));

    const int    rateIdx   = juce::jlimit(0, (int)std::size(kGlitchRates) - 1, (int)Raw(ids::glitchRate));
    const double edgeBeats = kGlitchRates[rateIdx].beats;
    const int    patIdx    = juce::jlimit(0, (int)std::size(kPatternBars) - 1, (int)Raw(ids::glitchPattern));
    const int    patSteps  = juce::roundToInt(kPatternBars[patIdx] * beatsPerBar_ / edgeBeats);
    glitch.SetPattern(patSteps, static_cast<uint32_t>(Raw(ids::glitchSeed)));

    // looper: quantised presses and fixed-length recording
    auto&        looper    = chain_->Looper();
    const int    quantize  = (int)Raw(ids::loopQuantize);
    const double unit      = quantize == 1 ? 1.0 : static_cast<double>(beatsPerBar_);
    const int    lenIdx    = juce::jlimit(0, (int)std::size(kLoopBars) - 1, (int)Raw(ids::loopLength));
    const size_t fixedLen  = kLoopBars[lenIdx] == 0
                                 ? 0
                                 : static_cast<size_t>(kLoopBars[lenIdx] * beatsPerBar_ * 60.0 / bpm_ * sampleRate_);

    uiBpm.store(bpm_, std::memory_order_relaxed);

    // loop window, optionally snapped to note values counted from the loop's start
    if(const size_t loopLen = looper.AudioLength(); loopLen > 0 && looper.AudioState() != TapeLooper::State::Recording)
    {
        const auto [start, end] = LoopWindow(loopLen, LoopSnapUnit(), Raw(ids::loopStart), Raw(ids::loopEnd));
        looper.SetWindow(start, end);
    }

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getWritePointer(1);

    const double beatsPerSample = bpm_ / 60.0 / sampleRate_;

    for(int i = 0; i < n; ++i)
    {
        const auto edge  = static_cast<int64_t>(std::floor(beat_ / edgeBeats));
        const auto pulse = static_cast<int64_t>(std::floor(beat_ * 12.0));
        if(edge != lastEdge_)
        {
            lastEdge_ = edge;
            glitch.ClockEdge(edge);
            const int64_t steps = patSteps > 0 ? patSteps : 8;
            uiGlitchStep.store(static_cast<int>(((edge % steps) + steps) % steps), std::memory_order_relaxed);
        }
        if(pulse != lastPulse_)
        {
            lastPulse_ = pulse;
            glitch.ClockPulse();
        }

        const int pending = pendingCmd_.load(std::memory_order_relaxed);
        if(pending != kNone && static_cast<int64_t>(std::floor(beat_ / unit)) >= pendingBoundary_)
        {
            pendingCmd_ = kNone;
            RunLooperCommand(pending);
        }

        chain_->Process(l + i, r + i, 1);

        if(fixedLen > 0 && looper.AudioState() == TapeLooper::State::Recording && looper.AudioLength() >= fixedLen)
            looper.PressPlay();

        beat_ += beatsPerSample;
    }
}

// ---------------------------------------------------------------- routing

uint32_t PrismProcessor::Pack(const BlockOrder& order)
{
    uint32_t v = 0;
    for(int i = 0; i < kNumBlocks; ++i)
        v |= static_cast<uint32_t>(order[(size_t)i]) << (4 * i);
    return v;
}

BlockOrder PrismProcessor::Unpack(uint32_t v)
{
    BlockOrder order;
    for(int i = 0; i < kNumBlocks; ++i)
        order[(size_t)i] = static_cast<Block>((v >> (4 * i)) & 0xF);
    return order;
}

bool PrismProcessor::Valid(const BlockOrder& order)
{
    bool seen[kNumBlocks] = {};
    for(Block b : order)
    {
        const int i = static_cast<int>(b);
        if(i < 0 || i >= kNumBlocks || seen[i])
            return false;
        seen[i] = true;
    }
    return true;
}

void PrismProcessor::SetOrder(const BlockOrder& order)
{
    if(!Valid(order))
        return;
    order_.store(Pack(order));
    juce::StringArray names;
    for(Block b : order)
        names.add(juce::String(static_cast<int>(b)));
    apvts.state.setProperty(kRoutingProp, names.joinIntoString(","), nullptr);
}

// ---------------------------------------------------------------- state

void PrismProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream out(destData, false);
    out.writeInt(kStateMagic);
    out.writeInt(kStateVersion);

    auto xml = apvts.copyState().createXml();
    out.writeString(xml ? xml->toString() : juce::String());

    // The loop is saved as 24-bit FLAC. The audio thread may still be writing
    // to it; at worst a few samples from an overdub in progress are mixed.
    const auto&  looper = chain_->Looper();
    const size_t len    = looper.GetLength();
    const bool   recording = looper.GetState() == TapeLooper::State::Recording;
    juce::MemoryBlock flac;
    if(On(ids::loopSave) && len > 0 && !recording)
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(flac, false);
        juce::FlacAudioFormat format;
        auto writer = format.createWriterFor(
            stream, juce::AudioFormatWriterOptions().withSampleRate(sampleRate_).withNumChannels(2).withBitsPerSample(24));
        if(writer)
        {
            const float* chans[] = {looper.DataL(), looper.DataR()};
            writer->writeFromFloatArrays(chans, 2, static_cast<int>(len));
        }
    }
    out.writeInt64(static_cast<juce::int64>(flac.getSize()));
    out.write(flac.getData(), flac.getSize());
}

void PrismProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream in(data, static_cast<size_t>(sizeInBytes), false);
    if(in.readInt() != kStateMagic)
        return;
    in.readInt(); // version

    if(auto xml = juce::parseXML(in.readString()))
    {
        if(xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            BlockOrder order = DefaultOrder();
            const auto parts = juce::StringArray::fromTokens(apvts.state.getProperty(kRoutingProp).toString(), ",", "");
            if(parts.size() == kNumBlocks)
                for(int i = 0; i < kNumBlocks; ++i)
                    order[(size_t)i] = static_cast<Block>(parts[i].getIntValue());
            order_.store(Pack(Valid(order) ? order : DefaultOrder()));
        }
    }

    const auto flacSize = in.readInt64();
    if(flacSize <= 0 || flacSize > in.getNumBytesRemaining())
        return;
    juce::MemoryBlock flac;
    in.readIntoMemoryBlock(flac, static_cast<ssize_t>(flacSize));

    juce::FlacAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(
        format.createReaderFor(new juce::MemoryInputStream(flac, false), true));
    if(!reader)
        return;

    juce::AudioBuffer<float> audio(2, static_cast<int>(reader->lengthInSamples));
    reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);

    const juce::SpinLock::ScopedLockType lock(loopLock_);
    if(preparedRate_ > 0.0)
    {
        chain_->Looper().Load(audio.getReadPointer(0), audio.getReadPointer(1),
                              static_cast<size_t>(audio.getNumSamples()));
    }
    else
    {
        pendingLoop_    = std::move(audio);
        hasPendingLoop_ = true;
    }
}

juce::AudioProcessorEditor* PrismProcessor::createEditor() { return new PrismEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PrismProcessor(); }

std::pair<size_t, size_t> PrismProcessor::LoopWindow(size_t loopLen, double unitLen, float startFrac, float endFrac)
{
    auto point = [&](float frac) {
        double s = frac * (double)loopLen;
        if(unitLen > 0.0)
            s = std::round(s / unitLen) * unitLen;
        return static_cast<size_t>(juce::jlimit(0.0, (double)loopLen, s));
    };
    const size_t start = point(startFrac);
    size_t       end   = point(endFrac);
    if(end <= start) // keep at least one snap unit (or a little audio) between them
        end = std::min(loopLen, start + (unitLen > 0.0 ? static_cast<size_t>(unitLen) : 64));
    return {start, end};
}

double PrismProcessor::LoopSnapUnit() const
{
    const int    snapIdx = juce::jlimit(0, (int)std::size(kLoopSnaps) - 1, (int)apvts.getRawParameterValue(ids::loopSnap)->load());
    const double sr      = getSampleRate();
    const double bpm     = uiBpm.load(std::memory_order_relaxed);
    return sr > 0.0 && bpm > 0.0 ? kLoopSnaps[snapIdx].beats * 60.0 / bpm * sr : 0.0;
}
