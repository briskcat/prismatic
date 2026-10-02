// TEMPO's tempo-synced granular ("glitch") delay. Ported from TEMPO firmware
// granularDelay.h + SimpleCrossfade.h.
//
// A stereo delay locked to a tempo division. On each clock edge, with a
// probability set by Chaos, the next repeat is replaced by a variation:
//   Glitch mode:  retrigger, reverse, octave up or octave down
//   Shimmer mode: octave up, panned randomly
// Freeze captures the delay output and loops it in time with the clock.
//
// Added for the plug-in: each event type can be switched off, Spread sets the
// random panning, and Pattern makes the dice rolls repeat. With a pattern
// length set, each step's roll comes from (seed, step within the pattern), and
// steps are counted from the host's song position, so the same glitches land
// in the same places every time the song plays.
//
// On the hardware one knob chose both the mode (left/right of noon) and the
// division (distance from noon). Here they are separate controls. The host
// drives the clock with SetTempo / ClockEdge / ClockPulse (12 PPQN), instead
// of TEMPO's internal or MIDI clock.

#pragma once
#include <vector>
#include "DspUtil.h"

namespace prism
{
namespace glitch
{
constexpr int   kNumDivs              = 9;
constexpr float kDivs[kNumDivs]       = {1.f / 8.f, 1.f / 6.f, 1.f / 4.f, 1.f / 3.f, 3.f / 8.f, 1.f / 2.f, 3.f / 4.f, 1.f, 2.f};
constexpr int   kFrozenPulses[kNumDivs] = {6, 8, 12, 16, 18, 24, 36, 48, 96}; // at 12 PPQN

/** Linear fade helper, from TEMPO SimpleCrossfade.h */
class Crossfade
{
  public:
    enum Type
    {
        THROUGH_ZERO,
        TO_ZERO,
        TO_ONE,
    };

    void Init(size_t samples) { samples_ = samples > 0 ? samples : 1; }

    /** env: current gain. crossed: set true at the fade's midpoint/end. */
    void Process(float* env, bool* crossed)
    {
        if(!running_)
        {
            *env = 1.f;
            return;
        }
        if(type_ == THROUGH_ZERO)
        {
            if(fading_down_)
            {
                if(--counter_ == 0)
                {
                    fading_down_ = false;
                    *crossed     = true;
                }
            }
            else if(++counter_ == samples_)
            {
                running_ = false;
            }
        }
        else if(type_ == TO_ZERO)
        {
            if(--counter_ == 0)
            {
                running_ = false;
                *crossed = true;
            }
        }
        else if(++counter_ == samples_)
        {
            running_ = false;
            *crossed = true;
        }
        *env = static_cast<float>(counter_) / static_cast<float>(samples_);
    }

    void Start(Type type)
    {
        running_     = true;
        type_        = type;
        counter_     = type == TO_ONE ? 0 : samples_;
        fading_down_ = type == THROUGH_ZERO;
    }

    bool IsRunning() const { return running_; }

  private:
    bool   running_     = false;
    bool   fading_down_ = false;
    size_t samples_     = 1;
    size_t counter_     = 0;
    Type   type_        = TO_ONE;
};

struct Buffer
{
    std::vector<float> data; // interleaved stereo
    size_t             frames = 1;
    float              offset_l = 960.f, offset_r = 480.f;

    void Read(float read_head, float* out_l, float* out_r) const
    {
        const float n = static_cast<float>(frames);
        float rh_r = read_head - offset_r;
        if(rh_r < 0.f)
            rh_r += n;
        float rh_l = read_head - offset_l;
        if(rh_l < 0.f)
            rh_l += n;

        size_t i0_l = static_cast<size_t>(rh_l) % frames;
        size_t i1_l = (i0_l + 1) % frames;
        size_t i0_r = static_cast<size_t>(rh_r) % frames;
        size_t i1_r = (i0_r + 1) % frames;

        const float frac = rh_l - std::floor(rh_l);
        const float a_l  = data[i0_l * 2], b_l = data[i1_l * 2];
        const float a_r  = data[i0_r * 2 + 1], b_r = data[i1_r * 2 + 1];
        *out_l += a_l + frac * (b_l - a_l);
        *out_r += a_r + frac * (b_r - a_r);
    }

    float Wrap(float head) const
    {
        const float n = static_cast<float>(frames);
        while(head < 0.f)
            head += n;
        while(head >= n)
            head -= n;
        return head;
    }
};

enum Event
{
    RETRIG,
    REVERSE,
    PITCH_UP,
    PITCH_DOWN,
    NONE
};

class Voice
{
  public:
    void Init(const Buffer* buffer, size_t event_xfade)
    {
        buffer_      = buffer;
        event_xfade_ = event_xfade;
        crossfade_env_ = 1.f;
        cur_event_ = next_event_ = NONE;
        cur_pan_ = next_pan_ = 0.f;
        div_pos_   = 8;
        active_    = false;
        fading_in_ = fading_out_ = false;
        div_crossfade_ = false;
    }

    void UpdateTempo(float delay_samples, uint32_t write_head)
    {
        if(!div_crossfade_)
            delay_samples_ = delay_samples;
        write_head_ = write_head;
    }

    void StartFadeIn()
    {
        if(fading_in_ || fading_out_ || active_)
            return;
        active_          = true;
        fading_in_       = true;
        event_counter_   = 0;
        cur_event_       = next_event_;
        next_event_      = NONE;
        cur_pan_         = next_pan_;
        next_pan_        = 0.f;
        if(cur_event_ == PITCH_UP && div_pos_ < 5)
            read_head_ = write_head_ - delay_samples_ * (0.5f / kDivs[div_pos_]);
        else
            read_head_ = write_head_ - delay_samples_;
        read_head_ = buffer_->Wrap(read_head_);
    }

    void StartFadeOut()
    {
        if(fading_in_ || fading_out_ || !active_)
            return;
        fading_out_    = true;
        event_counter_ = event_xfade_;
    }

    void SetNextEvent(Event event, float pan)
    {
        next_event_ = event;
        next_pan_   = pan;
    }

    bool SetDivCrossfade(float new_read_head, float new_delay_samples, int new_div, size_t xfade)
    {
        if(active_)
        {
            if(div_crossfade_)
                return false;
            div_read_head_     = new_read_head;
            div_delay_samples_ = new_delay_samples;
            div_counter_       = xfade;
            div_xfade_         = xfade;
            div_crossfade_     = true;
        }
        else
        {
            read_head_ = new_read_head;
        }
        div_pos_ = new_div;
        return true;
    }

    void Read(float* out_l, float* out_r)
    {
        if(!active_)
            return;

        crossfade_env_ = 1.f;
        float div_env  = 1.f;

        const float pan_l = std::sqrt(0.5f * (1.f - cur_pan_));
        const float pan_r = std::sqrt(0.5f * (1.f + cur_pan_));

        if(fading_in_)
        {
            if(++event_counter_ > event_xfade_)
            {
                event_counter_ = event_xfade_;
                fading_in_     = false;
            }
            crossfade_env_ = std::sin(Phase() * kPi * 0.5f);
        }
        else if(fading_out_)
        {
            if(event_counter_ > 0)
                --event_counter_;
            if(event_counter_ < 1)
            {
                fading_out_ = false;
                active_     = false;
            }
            crossfade_env_ = std::sin(Phase() * kPi * 0.5f);
        }

        switch(cur_event_)
        {
            case NONE: read_head_ = write_head_ - delay_samples_; break;
            case RETRIG: read_head_ = static_cast<float>(write_head_) - delay_samples_ * 1.125f; break;
            case REVERSE: read_head_ -= 1.f; break;
            case PITCH_UP: read_head_ += 2.f; break;
            case PITCH_DOWN: read_head_ += .5f; break;
        }
        read_head_ = buffer_->Wrap(read_head_);

        if(div_crossfade_)
        {
            --div_counter_;
            div_env = static_cast<float>(div_counter_) / static_cast<float>(div_xfade_);
            if(div_counter_ == 0)
            {
                read_head_     = div_read_head_;
                delay_samples_ = div_delay_samples_;
                div_crossfade_ = false;
                div_env        = 1.f;
            }
        }

        float sig_l = 0.f, sig_r = 0.f;
        buffer_->Read(read_head_, &sig_l, &sig_r);
        *out_l += sig_l * crossfade_env_ * div_env * pan_l;
        *out_r += sig_r * crossfade_env_ * div_env * pan_r;

        if(div_crossfade_)
        {
            switch(cur_event_)
            {
                case NONE:
                case RETRIG: div_read_head_ += 1.f; break;
                case REVERSE: div_read_head_ -= 1.f; break;
                case PITCH_UP: div_read_head_ += 2.f; break;
                case PITCH_DOWN: div_read_head_ += .5f; break;
            }
            div_read_head_ = buffer_->Wrap(div_read_head_);

            float c_l = 0.f, c_r = 0.f;
            buffer_->Read(div_read_head_, &c_l, &c_r);
            *out_l += c_l * crossfade_env_ * (1.f - div_env) * pan_l;
            *out_r += c_r * crossfade_env_ * (1.f - div_env) * pan_r;
        }
    }

    bool active_ = false;

  private:
    float Phase() const { return static_cast<float>(event_counter_) / static_cast<float>(event_xfade_); }

    const Buffer* buffer_      = nullptr;
    size_t        event_xfade_ = 1024;
    uint32_t      write_head_  = 0;
    float         read_head_   = 0.f;
    float         crossfade_env_ = 1.f;
    float         delay_samples_ = 0.f;
    Event         cur_event_ = NONE, next_event_ = NONE;
    float         cur_pan_ = 0.f, next_pan_ = 0.f;
    size_t        event_counter_ = 0;
    bool          fading_in_ = false, fading_out_ = false;
    int           div_pos_ = 8;

    float  div_read_head_ = 0.f, div_delay_samples_ = 0.f;
    bool   div_crossfade_ = false;
    size_t div_counter_ = 0, div_xfade_ = 1;
};
} // namespace glitch

class GlitchDelay
{
  public:
    enum class Mode
    {
        Glitch,
        Shimmer
    };

    void Init(const RateScale& rate)
    {
        rate_ = rate;
        coef_ = rate.Coef(.001f);

        // 10.5 s holds 2 bars down to ~46 BPM
        const size_t frames = static_cast<size_t>(rate.sr * 10.5f);
        for(auto* b : {&buffer_, &frozen_})
        {
            b->data.assign(frames * 2, 0.f);
            b->frames   = frames;
            b->offset_l = rate.Samples(960.f);
            b->offset_r = rate.Samples(480.f);
        }

        xfade_       = static_cast<size_t>(rate.Samples(256.f));
        event_xfade_ = static_cast<size_t>(rate.Samples(1024.f));
        wrap_xfade_.Init(xfade_);
        toggle_xfade_.Init(xfade_);

        write_head_ = frozen_write_head_ = 0;
        frozen_read_head_ = 0.f;
        frozen_sample_counter_ = 0;
        for(auto& c : clock_pulse_counter_)
            c = 0;
        div_crossfade_ = false;
        lock_buffer_   = false;
        lock_target_   = false;
        cur_l_ = cur_r_ = 0.f;
        wet_ = wet_target_ = 0.f;
        delay_on_ = false;
        div_pos_  = 8;
        delay_samples_ = delay_samples_target_ = SamplesFor(div_pos_);
        event_[0] = event_[1] = glitch::NONE;
        clock_edge_ = false;

        cur_idx_  = 0;
        next_idx_ = 1;
        for(auto& v : voices_)
            v.Init(&buffer_, event_xfade_);
        voices_[cur_idx_].active_ = true;

        duck_env_ = 0.f;
        duck_gain_ = 1.f;
        duck_attack_  = 1.0f - std::exp(-1.0f / (.01f * rate.sr));
        duck_release_ = 1.0f - std::exp(-1.0f / (.25f * rate.sr));
        dry_ = dry_target_ = 1.f;
        send_ = send_target_ = 0.f;
    }

    /** In place: adds the delay to the dry signal. */
    void Process(float* l, float* r)
    {
        fonepole(dry_, dry_target_, coef_);
        fonepole(send_, send_target_, coef_);

        const float in_l = *l, in_r = *r;
        Write(in_l * send_, in_r * send_);

        float out_l = 0.f, out_r = 0.f;
        if(delay_on_)
            ProcessDelay(&out_l, &out_r);

        // TEMPO ducks the repeats under the dry signal at high feedback
        const float dry_l = in_l * dry_, dry_r = in_r * dry_;
        if(duck_amount_ > 0.f)
            Duck(&out_l, &out_r, dry_l, dry_r);

        *l = dry_l + out_l;
        *r = dry_r + out_r;
    }

    // ---- clock (driven by the host) ----
    void SetTempo(float bpm) { bpm_ = fclamp(bpm, 20.f, 400.f); }
    /** step: index of this clock edge since the start of the song */
    void ClockEdge(int64_t step)
    {
        clock_edge_ = true;
        step_       = step;
    }
    void ClockPulse()
    {
        if(lock_buffer_)
            for(auto& c : clock_pulse_counter_)
                c++;
    }

    // ---- controls ----
    void SetActive(bool on)
    {
        wet_target_ = on ? 1.f : 0.f;
        if(on)
            delay_on_ = true;
    }
    void SetMode(Mode m) { mode_ = m; }
    void SetDivision(int idx) { div_target_ = idx < 0 ? 0 : (idx >= glitch::kNumDivs ? glitch::kNumDivs - 1 : idx); }
    void SetChaos(float v) { chaos_ = v; }
    void SetFeedback(float v)
    {
        feedback_    = v * .975f;
        duck_amount_ = v > .6f ? (v - .6f) / .4f : 0.f;
    }
    /** TEMPO's mix: below noon fades the repeats in, above noon fades the dry out */
    void SetMix(float v)
    {
        if(v >= .5f)
        {
            dry_target_  = 2.f - v * 2.f;
            send_target_ = 1.f;
        }
        else
        {
            dry_target_  = 1.f;
            send_target_ = v * 2.f;
        }
    }
    void SetFreeze(bool f) { lock_target_ = f; }

    /** bit per event type: 1 retrigger, 2 reverse, 4 octave up, 8 octave down */
    void SetEvents(unsigned mask) { event_mask_ = mask; }
    void SetSpread(float v) { spread_ = v; }
    /** steps before the dice rolls repeat; 0 = never (free random) */
    void SetPattern(int steps, uint32_t seed)
    {
        pattern_steps_ = steps;
        seed_          = seed;
    }
    /** original TEMPO panning: none in Glitch mode, chaos-scaled in Shimmer */
    void SetClassic(bool c) { classic_ = c; }

    static const char* DivisionName(int idx)
    {
        static const char* names[glitch::kNumDivs] = {"1/8", "1/4T", "1/4", "1/2T", "1/4.", "1/2", "1/2.", "1 bar", "2 bars"};
        return names[idx];
    }

  private:
    float SamplesFor(int div) const
    {
        const float samples = rate_.sr * 60.f / bpm_ * 4.f * glitch::kDivs[div];
        return fclamp(samples, 1.f, static_cast<float>(buffer_.frames - 4));
    }

    void Write(float in_l, float in_r)
    {
        if(lock_buffer_)
            return;
        const size_t idx      = write_head_ * 2;
        buffer_.data[idx]     = (in_l + cur_l_) * lock_env_;
        buffer_.data[idx + 1] = (in_r + cur_r_) * lock_env_;
        write_head_           = (write_head_ + 1) % buffer_.frames;
    }

    void ProcessDelay(float* out_l, float* out_r)
    {
        fonepole(wet_, wet_target_, coef_);
        if(wet_target_ == 0.f && wet_ < .001f)
        {
            delay_on_ = false;
            return;
        }

        lock_env_         = 1.f;
        float crossfade_env = 1.f;
        float div_env       = 1.f;

        // freeze on/off fades through zero
        if(lock_target_ != lock_buffer_ && !toggle_xfade_.IsRunning())
            toggle_xfade_.Start(glitch::Crossfade::THROUGH_ZERO);

        // division change
        if(div_target_ != div_pos_)
        {
            if(lock_buffer_)
            {
                if(!div_crossfade_)
                {
                    const float samples_per_tick = rate_.sr * 60.f / (bpm_ * 12.f);
                    const float phase_offset = frozen_read_head_ - static_cast<float>(clock_pulse_counter_[div_pos_]) * samples_per_tick;
                    crossfade_read_head_ = frozen_.Wrap(static_cast<float>(clock_pulse_counter_[div_target_]) * samples_per_tick + phase_offset);
                    div_crossfade_       = true;
                    div_counter_         = xfade_;
                    div_pos_             = div_target_;
                }
            }
            else
            {
                const float new_delay = SamplesFor(div_target_);
                const float new_head  = buffer_.Wrap(static_cast<float>(write_head_) - new_delay);
                bool ok = true;
                for(auto& v : voices_)
                    if(!v.SetDivCrossfade(new_head, new_delay, div_target_, xfade_))
                        ok = false;
                if(ok)
                {
                    delay_samples_ = new_delay;
                    div_pos_       = div_target_;
                }
            }
        }

        delay_samples_target_ = SamplesFor(div_pos_);
        fonepole(delay_samples_, delay_samples_target_, coef_);

        if(toggle_xfade_.IsRunning())
        {
            bool crossed = false;
            toggle_xfade_.Process(&lock_env_, &crossed);
            if(crossed)
            {
                lock_buffer_ = !lock_buffer_;
                if(lock_buffer_)
                {
                    frozen_read_head_      = frozen_.Wrap(static_cast<float>(frozen_write_head_) - delay_samples_);
                    frozen_sample_counter_ = 0;
                    for(auto& c : clock_pulse_counter_)
                        c = 0;
                }
            }
        }

        if(lock_buffer_)
        {
            const float read_end = frozen_.Wrap(static_cast<float>(frozen_write_head_) - delay_samples_);
            frozen_read_head_ += 1.f;

            for(int i = 0; i < glitch::kNumDivs; ++i)
            {
                if(clock_pulse_counter_[i] >= glitch::kFrozenPulses[i])
                {
                    clock_pulse_counter_[i] = 0;
                    if(i == div_pos_)
                        wrap_xfade_.Start(glitch::Crossfade::THROUGH_ZERO);
                }
            }
            frozen_sample_counter_++;

            if(div_crossfade_)
                div_env = static_cast<float>(div_counter_) / static_cast<float>(xfade_);
            if(wrap_xfade_.IsRunning())
            {
                bool crossed = false;
                wrap_xfade_.Process(&crossfade_env, &crossed);
                if(crossed)
                {
                    frozen_read_head_      = read_end;
                    frozen_sample_counter_ = 0;
                }
            }
            frozen_read_head_ = frozen_.Wrap(frozen_read_head_);

            float sig_l = 0.f, sig_r = 0.f;
            frozen_.Read(frozen_read_head_, &sig_l, &sig_r);
            const float g = crossfade_env * wet_ * lock_env_;
            *out_l        = sig_l * g * div_env;
            *out_r        = sig_r * g * div_env;

            if(div_crossfade_)
            {
                float c_l = 0.f, c_r = 0.f;
                frozen_.Read(crossfade_read_head_, &c_l, &c_r);
                *out_l += c_l * g * (1.f - div_env);
                *out_r += c_r * g * (1.f - div_env);

                if(--div_counter_ == 0)
                {
                    frozen_read_head_ = crossfade_read_head_;
                    div_crossfade_    = false;
                }
                crossfade_read_head_ = frozen_.Wrap(crossfade_read_head_ + 1.f);
            }
            cur_l_ = cur_r_ = 0.f;
        }
        else
        {
            for(auto& v : voices_)
                v.UpdateTempo(delay_samples_, write_head_);

            if(clock_edge_)
            {
                clock_edge_ = false;
                event_[0]   = event_[1];

                Rng  pattern_rng(StepSeed());
                Rng& dice     = pattern_steps_ > 0 ? pattern_rng : rng_;
                const float roll = dice.Uniform();
                const float pan  = RandomPan(dice);
                const glitch::Event event = PickEvent(dice);

                if(roll < 0.5f * chaos_ && event != glitch::NONE)
                {
                    cur_idx_  = next_idx_;
                    next_idx_ = (cur_idx_ + 1) % 2;
                    event_[1] = event;
                    voices_[cur_idx_].SetNextEvent(event_[1], pan);
                }
                else
                {
                    event_[1] = glitch::NONE;
                    if(event_[0] != glitch::NONE)
                    {
                        cur_idx_  = next_idx_;
                        next_idx_ = (cur_idx_ + 1) % 2;
                        voices_[cur_idx_].SetNextEvent(glitch::NONE, 0.f);
                    }
                }
            }

            if(event_[0] != glitch::NONE || event_[1] != glitch::NONE)
            {
                voices_[cur_idx_].StartFadeIn();
                voices_[next_idx_].StartFadeOut();
            }

            for(auto& v : voices_)
                v.Read(out_l, out_r);

            *out_l *= wet_ * lock_env_;
            *out_r *= wet_ * lock_env_;

            cur_l_ = *out_l * feedback_;
            cur_r_ = *out_r * feedback_;

            const size_t idx       = frozen_write_head_ * 2;
            frozen_.data[idx]      = *out_l;
            frozen_.data[idx + 1]  = *out_r;
            frozen_write_head_     = (frozen_write_head_ + 1) % frozen_.frames;
        }
    }

    /** Always draws the same number of values, so patterns stay stable when settings change. */
    float RandomPan(Rng& dice) const
    {
        const float r = dice.Uniform() * 2.f - 1.f;
        if(classic_)
            return mode_ == Mode::Shimmer ? r * (0.5f + chaos_ * 0.5f) : 0.f;
        return r * spread_;
    }

    glitch::Event PickEvent(Rng& dice) const
    {
        const uint32_t pick = dice.Next();
        if(mode_ == Mode::Shimmer)
            return glitch::PITCH_UP;
        const unsigned mask = classic_ ? 0xFu : event_mask_;
        glitch::Event enabled[4];
        int n = 0;
        for(int e = 0; e < 4; ++e)
            if(mask & (1u << e))
                enabled[n++] = static_cast<glitch::Event>(e);
        return n == 0 ? glitch::NONE : enabled[pick % static_cast<uint32_t>(n)];
    }

    uint32_t StepSeed() const
    {
        const int64_t steps = pattern_steps_ > 0 ? pattern_steps_ : 1;
        const int64_t pos   = ((step_ % steps) + steps) % steps;
        uint32_t h = seed_ * 0x9E3779B9u ^ static_cast<uint32_t>(pos) * 0x85EBCA6Bu;
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        return h | 1u; // xorshift state must be non-zero
    }

    /** TEMPO SimpleCompressor: duck the repeats under the dry signal */
    void Duck(float* wet_l, float* wet_r, float dry_l, float dry_r)
    {
        const float dry_mix = ((dry_l + dry_r) * 0.5f) * 100.f;
        duck_env_           = std::fmin(duck_attack_ * std::fabs(dry_mix) + (1.0f - duck_attack_) * duck_env_, 1.f);
        constexpr float threshold = .1f;
        float target = 1.f;
        if(duck_env_ > threshold)
            target = 1.0f - std::fmin((duck_env_ - threshold) / (1.0f - threshold), 1.f) * duck_amount_;
        const float c = target < duck_gain_ ? duck_attack_ : duck_release_;
        duck_gain_    = c * target + (1.0f - c) * duck_gain_;
        *wet_l *= duck_gain_;
        *wet_r *= duck_gain_;
    }

    RateScale      rate_;
    Rng            rng_{0xC0FFEEu};
    float          coef_ = .001f;
    glitch::Buffer buffer_, frozen_;
    glitch::Voice  voices_[2];
    size_t         xfade_ = 256, event_xfade_ = 1024;

    uint32_t write_head_ = 0, frozen_write_head_ = 0;
    float    delay_samples_ = 0.f, delay_samples_target_ = 0.f;
    float    cur_l_ = 0.f, cur_r_ = 0.f;
    float    frozen_read_head_ = 0.f;
    size_t   frozen_sample_counter_ = 0;
    int      clock_pulse_counter_[glitch::kNumDivs]{};

    bool   div_crossfade_       = false;
    float  crossfade_read_head_ = 0.f;
    size_t div_counter_         = 0;

    glitch::Crossfade wrap_xfade_, toggle_xfade_;
    float             lock_env_ = 1.f;

    float wet_ = 0.f, wet_target_ = 0.f;
    bool  delay_on_    = false;
    bool  lock_buffer_ = false, lock_target_ = false;
    float feedback_    = .3f * .975f;
    float chaos_       = 0.f;
    Mode  mode_        = Mode::Glitch;
    int   div_pos_ = 8, div_target_ = 8;
    float bpm_ = 120.f;

    unsigned event_mask_    = 0xFu;
    float    spread_        = 0.f;
    int      pattern_steps_ = 0;
    uint32_t seed_          = 1;
    int64_t  step_          = 0;
    bool     classic_       = false;

    bool          clock_edge_ = false;
    glitch::Event event_[2]   = {glitch::NONE, glitch::NONE};
    uint8_t       cur_idx_ = 0, next_idx_ = 1;

    float dry_ = 1.f, dry_target_ = 1.f, send_ = 0.f, send_target_ = 0.f;
    float duck_amount_ = 0.f, duck_env_ = 0.f, duck_gain_ = 1.f, duck_attack_ = 0.f, duck_release_ = 0.f;
};

} // namespace prism
