// TAPE's stereo ping-pong delay. Ported from the delay block of
// Engine::ApplyFx in TAPE firmware DSPEngine.h.
//
// The input (summed to mono) is delayed into the left tap; the left tap is
// delayed again into the right tap, and the right tap feeds back.
//
// Two ways of changing the delay time:
// - Varispeed (default in the plug-in): the delay is a fixed loop of tape and
//   Time sets how fast it runs. Changing the time re-pitches everything already
//   on the tape, so going from 1/4 to 1/2 drops the repeats an octave, like
//   slowing down a tape echo. At full feedback the loop holds indefinitely.
// - Classic (TAPE's behaviour): the read point slides along a normal delay
//   line, which gives the quick "whip" when the time changes.
//
// On the hardware the feedback knob also set the dry/wet balance. Here Mix is
// its own control; Classic mode brings back the original coupling.

#pragma once
#include <vector>
#include "DspUtil.h"

namespace prism
{
class TapeDelay
{
  public:
    static constexpr float kMaxSeconds = 2.0f;
    /** Length of the varispeed tape loop. Longer than the longest delay, so every normal setting runs
        the tape at or above real time and stays clean; it only runs slow (and gets darker) when an
        existing loop is stretched past 2 s worth of pitch drop. */
    static constexpr float kTapeSeconds = 2.05f;

    void Init(const RateScale& rate)
    {
        rate_ = rate;
        size_ = static_cast<size_t>(rate.sr * kMaxSeconds) + 8;
        line_l_.assign(size_, 0.f);
        line_r_.assign(size_, 0.f);
        write_ptr_ = 0;

        tape_len_ = static_cast<size_t>(rate.sr * kTapeSeconds);
        tape_l_.assign(tape_len_, 0.f);
        tape_r_.assign(tape_len_, 0.f);
        tape_pos_ = 0.0;
        for(auto& h : hist_l_)
            h = 0.f;
        for(auto& h : hist_r_)
            h = 0.f;
        aa_l_[0] = aa_l_[1] = aa_r_[0] = aa_r_[1] = 0.f;

        coef_ = rate.Coef(.001f);
        time_ = time_target_ = rate.sr * .25f;
        fb_ = fb_target_ = 0.f;
        mix_ = mix_target_ = 0.f;
    }

    void Clear()
    {
        std::fill(line_l_.begin(), line_l_.end(), 0.f);
        std::fill(line_r_.begin(), line_r_.end(), 0.f);
        std::fill(tape_l_.begin(), tape_l_.end(), 0.f);
        std::fill(tape_r_.begin(), tape_r_.end(), 0.f);
    }

    void Process(float* l, float* r)
    {
        fonepole(fb_, fb_target_, coef_);
        fonepole(time_, time_target_, coef_);
        fonepole(mix_, mix_target_, coef_);

        float del_l, del_r;
        if(varispeed_)
            ReadTape(&del_l, &del_r);
        else
            ReadLine(&del_l, &del_r);

        float dry_mix, wet_mix;
        if(classic_)
        {
            const float del_vol = fb_ < .2f ? fb_ * 5.f : 1.f;
            del_l *= del_vol;
            del_r *= del_vol;
            wet_mix = fb_ > .25f ? .5f : 2.f * fb_;          // quickly to 50%
            dry_mix = fb_ > .83f ? .5f : (1.f - .6f * fb_);  // slowly to 50%
        }
        else
        {
            dry_mix = std::cos(mix_ * kPi * .5f);
            wet_mix = std::sin(mix_ * kPi * .5f);
        }

        const float mono_sum = (*l + *r) * .5f;
        const float del_in   = mono_sum + del_r * std::pow(fb_, .7f);
        if(varispeed_)
            WriteTape(del_in, del_l);
        else
        {
            // the firmware stored 16-bit samples, which also clamped at +-1
            line_l_[write_ptr_] = fclamp(del_in, -1.f, 1.f);
            line_r_[write_ptr_] = fclamp(del_l, -1.f, 1.f);
            write_ptr_          = (write_ptr_ + size_ - 1) % size_;
        }

        *l = *l * dry_mix + del_l * wet_mix;
        *r = *r * dry_mix + del_r * wet_mix;
    }

    void SetTimeSeconds(float seconds)
    {
        time_target_ = fclamp(seconds * rate_.sr, 1.f, static_cast<float>(size_ - 2));
    }
    /** Varispeed lets feedback reach 100% (the loop holds); the classic line stops at 90%, like TAPE. */
    void SetFeedback(float fb) { fb_target_ = fb * (varispeed_ ? 1.f : .9f); }
    void SetMix(float mix) { mix_target_ = mix; }
    void SetClassic(bool classic) { classic_ = classic; }
    void SetVarispeed(bool v)
    {
        if(v != varispeed_)
            Clear(); // the other mode's buffer holds stale audio
        varispeed_ = v;
    }

  private:
    void ReadLine(float* l, float* r) const
    {
        const float  t    = fclamp(time_, 1.f, static_cast<float>(size_ - 2));
        const size_t ti   = static_cast<size_t>(t);
        const float  frac = t - static_cast<float>(ti);
        const size_t a    = (write_ptr_ + ti) % size_;
        const size_t b    = (a + 1) % size_;
        *l = line_l_[a] + (line_l_[b] - line_l_[a]) * frac;
        *r = line_r_[a] + (line_r_[b] - line_r_[a]) * frac;
    }

    /** 4-point cubic (Hermite) interpolation between y1 and y2 */
    static float Hermite(float y0, float y1, float y2, float y3, float t)
    {
        const float c1 = .5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.f * y2 - .5f * y3;
        const float c3 = .5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    /** Clean until near full scale, then a soft knee: lets 100% feedback hold without adding grit. */
    static float Knee(float x)
    {
        const float a = std::fabs(x);
        if(a <= .8f)
            return x;
        return std::copysign(.8f + .2f * std::tanh((a - .8f) * 5.f), x);
    }

    /** The tape is one loop long: what's under the head now was written one lap ago. */
    void ReadTape(float* l, float* r) const
    {
        const size_t n  = tape_len_;
        const size_t i1 = static_cast<size_t>(tape_pos_) % n;
        const size_t i0 = (i1 + n - 1) % n, i2 = (i1 + 1) % n, i3 = (i1 + 2) % n;
        const float  f  = static_cast<float>(tape_pos_ - std::floor(tape_pos_));
        *l = Hermite(tape_l_[i0], tape_l_[i1], tape_l_[i2], tape_l_[i3], f);
        *r = Hermite(tape_r_[i0], tape_r_[i1], tape_r_[i2], tape_r_[i3], f);
    }

    /** Advance the tape at speed = tape length / delay time, recording every sample the head passes. */
    void WriteTape(float in_l, float in_r)
    {
        const double speed = static_cast<double>(tape_len_) / fclamp(time_, 1.f, static_cast<float>(size_));

        // when the tape runs slower than real time, filter first so the slow write doesn't alias
        if(speed < 1.f)
        {
            const float a = 1.f - std::exp(-kTwoPi * .21f * static_cast<float>(speed)); // ~0.4 of the tape's Nyquist
            for(int s = 0; s < 2; ++s)
            {
                fonepole(aa_l_[s], s == 0 ? in_l : aa_l_[0], a);
                fonepole(aa_r_[s], s == 0 ? in_r : aa_r_[0], a);
            }
            in_l = aa_l_[1];
            in_r = aa_r_[1];
        }
        else
        {
            aa_l_[0] = aa_l_[1] = in_l;
            aa_r_[0] = aa_r_[1] = in_r;
        }

        // input history; the write interpolates between the two middle samples (one sample of latency)
        for(int k = 0; k < 3; ++k)
        {
            hist_l_[k] = hist_l_[k + 1];
            hist_r_[k] = hist_r_[k + 1];
        }
        hist_l_[3] = in_l;
        hist_r_[3] = in_r;

        const double  old = tape_pos_;
        const double  pos = old + speed;
        const int64_t a   = static_cast<int64_t>(std::floor(old));
        const int64_t b   = static_cast<int64_t>(std::floor(pos));
        for(int64_t i = a + 1; i <= b; ++i)
        {
            const float  t   = static_cast<float>((static_cast<double>(i) - old) / speed);
            // the record head trails the play head by a few samples, so the next read's
            // interpolation never touches what was just written
            constexpr int64_t kLag = 3;
            const int64_t     len  = static_cast<int64_t>(tape_len_);
            const size_t      idx  = static_cast<size_t>(((i - kLag) % len + len) % len);
            tape_l_[idx] = Knee(Hermite(hist_l_[0], hist_l_[1], hist_l_[2], hist_l_[3], t));
            tape_r_[idx] = Knee(Hermite(hist_r_[0], hist_r_[1], hist_r_[2], hist_r_[3], t));
        }
        tape_pos_ = pos;
        while(tape_pos_ >= static_cast<double>(tape_len_))
            tape_pos_ -= static_cast<double>(tape_len_);
    }

    RateScale          rate_;
    std::vector<float> line_l_, line_r_;
    size_t             size_      = 1;
    size_t             write_ptr_ = 0;

    std::vector<float> tape_l_, tape_r_;
    size_t             tape_len_ = 1;
    double             tape_pos_ = 0.0; // double: float drifts enough over a lap to smear the repeats
    float              hist_l_[4]{}, hist_r_[4]{};
    float              aa_l_[2]{}, aa_r_[2]{};

    float coef_ = .001f;
    float time_ = 0.f, time_target_ = 0.f;
    float fb_ = 0.f, fb_target_ = 0.f;
    float mix_ = 0.f, mix_target_ = 0.f;
    bool  classic_   = false;
    bool  varispeed_ = true;
};

} // namespace prism
