// Sample-rate and bit-depth reduction. New for the plugin (not in the firmware).
//
// Sample-and-hold decimation with a fractional phase accumulator, so any
// target rate works at any host rate, then quantisation to `bits`.

#pragma once
#include "DspUtil.h"

namespace prism
{
class Crusher
{
  public:
    void Init(const RateScale& rate)
    {
        rate_  = rate;
        coef_  = rate.Coef(.001f);
        phase_ = 1.f;
        hold_l_ = hold_r_ = 0.f;
        mix_ = mix_target_;
        step_ = step_target_;
        levels_ = levels_target_;
    }

    void Process(float* l, float* r)
    {
        fonepole(mix_, mix_target_, coef_);
        fonepole(step_, step_target_, coef_);
        fonepole(levels_, levels_target_, coef_);

        phase_ += step_;
        if(phase_ >= 1.f)
        {
            phase_ -= static_cast<float>(static_cast<int>(phase_));
            hold_l_ = Quantise(*l);
            hold_r_ = Quantise(*r);
        }
        *l += (hold_l_ - *l) * mix_;
        *r += (hold_r_ - *r) * mix_;
    }

    void SetRateHz(float hz) { step_target_ = fclamp(hz / rate_.sr, 0.f, 1.f); }
    void SetBits(float bits) { levels_target_ = std::pow(2.f, fclamp(bits, 1.f, 24.f) - 1.f); }
    void SetMix(float mix) { mix_target_ = mix; }

  private:
    float Quantise(float x) const { return std::round(x * levels_) / levels_; }

    RateScale rate_;
    float     coef_  = .001f;
    float     phase_ = 1.f;
    float     hold_l_ = 0.f, hold_r_ = 0.f;
    float     mix_ = 0.f, mix_target_ = 0.f;
    float     step_ = 1.f, step_target_ = 1.f;
    float     levels_ = 32768.f, levels_target_ = 32768.f;
};

} // namespace prism
