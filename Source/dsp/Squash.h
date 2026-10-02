// TAPE's output compressor/limiter ("final comp"). Ported from TAPE firmware
// limiter.h (extracted from pichenettes/stmlib) and the output stage of
// DSPEngine.h.

#pragma once
#include "DspUtil.h"

namespace prism
{
class Squash
{
  public:
    void Init(const RateScale& rate)
    {
        peak_up_   = rate.Coef(.05f);
        peak_down_ = rate.Coef(.0002f);
        gain_up_   = rate.Coef(.001f);
        gain_down_ = rate.Coef(.005f);
        smooth_    = rate.Coef(.001f);
        peak_l_ = peak_r_ = .5f;
        gain_l_ = gain_r_ = 1.f;
    }

    void Process(float* l, float* r)
    {
        fonepole(amt_, amt_target_, smooth_);
        const float thresh  = 1.f / (10.f * amt_ + 4.f);
        const float ratio   = 1.f + amt_ * amt_ * 7.f;
        const float makeup  = .9f + amt_ * .6f;
        const float pregain = 7.f * amt_ + 1.f;
        *l = Comp(*l, pregain, thresh, ratio, makeup, peak_l_, gain_l_);
        *r = Comp(*r, pregain, thresh, ratio, makeup, peak_r_, gain_r_);
    }

    void SetAmount(float amt) { amt_target_ = amt; }

  private:
    static void Slope(float& out, float in, float up, float down)
    {
        const float err = in - out;
        out += (err > 0.f ? up : down) * err;
    }

    float Comp(float in, float pregain, float thresh, float ratio, float makeup, float& peak, float& gain) const
    {
        const float pre = in * pregain;
        Slope(peak, std::fabs(pre), peak_up_, peak_down_);
        const float target = peak <= thresh ? 1.f : 1.f / (ratio * (1.f + (peak - thresh)));
        Slope(gain, target, gain_up_, gain_down_);
        return SoftLimit(pre * gain * makeup);
    }

    float peak_up_ = .05f, peak_down_ = .0002f, gain_up_ = .001f, gain_down_ = .005f, smooth_ = .001f;
    float peak_l_ = .5f, peak_r_ = .5f, gain_l_ = 1.f, gain_r_ = 1.f;
    float amt_ = 0.f, amt_target_ = 0.f;
};

} // namespace prism
