// Tape wow & flutter: a short modulated delay whose time drifts to random
// targets. Ported from TAPE firmware Warble.h.
//
// On the hardware one knob set both how often the tape drifts and how much of
// the warbled signal is heard. Here they are separate (SetRate / SetDepth);
// SetClassic() reproduces the original single-knob behaviour.

#pragma once
#include <vector>
#include "DspUtil.h"

namespace prism
{
class Warble
{
  public:
    void Init(const RateScale& rate)
    {
        rate_ = rate;
        line_l_.assign(static_cast<size_t>(rate.Samples(1024.f)) + 4, 0.f);
        line_r_.assign(line_l_.size(), 0.f);
        write_ptr_ = 0;
        mix_coef_  = rate.Coef(.001f);
        l_ = lend_ = rate.Samples(100.f);
        coeff_     = 0.f;
        mix_ = mix_target_ = 0.f;
    }

    void Process(float in_l, float in_r, float* out_l, float* out_r)
    {
        // events per second ~= freq_ctrl_
        if(rng_.Uniform() < freq_ctrl_ / rate_.sr)
        {
            lend_  = rate_.Samples(100.f + rng_.Uniform() * 880.f);
            coeff_ = rate_.Coef(rng_.Uniform() * .0001f);
        }
        fonepole(l_, lend_, coeff_);
        fonepole(mix_, mix_target_, mix_coef_);

        const size_t size = line_l_.size();
        line_l_[write_ptr_] = in_l;
        line_r_[write_ptr_] = in_r;

        const size_t idx  = static_cast<size_t>(l_);
        const float  frac = l_ - static_cast<float>(idx);
        const size_t a    = (write_ptr_ + idx) % size;
        const size_t b    = (a + 1) % size;
        const float  dl   = line_l_[a] + (line_l_[b] - line_l_[a]) * frac;
        const float  dr   = line_r_[a] + (line_r_[b] - line_r_[a]) * frac;
        write_ptr_        = (write_ptr_ + size - 1) % size;

        *out_l = mix_ * (dl - in_l) + in_l;
        *out_r = mix_ * (dr - in_r) + in_r;
    }

    /** 0-1: how much of the warbled signal is heard */
    void SetDepth(float depth) { mix_target_ = depth; }

    /** 0-1: how often the tape speed drifts (~0.1 to 30 times per second) */
    void SetRate(float rate) { freq_ctrl_ = rate * 30.f + .1f; }

    /** The original TAPE single knob */
    void SetClassic(float val)
    {
        SetDepth(val);
        SetRate(val);
    }

  private:
    RateScale          rate_;
    Rng                rng_{0x5eed1u};
    std::vector<float> line_l_, line_r_;
    size_t             write_ptr_ = 0;
    float              l_ = 0.f, lend_ = 0.f, coeff_ = 0.f;
    float              freq_ctrl_ = .1f;
    float              mix_ = 0.f, mix_target_ = 0.f, mix_coef_ = .001f;
};

} // namespace prism
