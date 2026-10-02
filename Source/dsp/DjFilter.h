// One-knob DJ filter: low-pass below noon, high-pass above.
// Ported from TAPE firmware DJFilter.h / BasicMMF.h (derived from Electrosmith DSP source).

#pragma once
#include "DspUtil.h"

namespace prism
{
/** Basic two-pole multimode filter. freq is a 0-1 coefficient. */
class BasicMMF
{
  public:
    enum class Mode
    {
        Lowpass,
        Highpass,
    };

    float Process(float in)
    {
        buf0_ += freq_ * (in - buf0_ + fb_amt_ * (buf0_ - buf1_));
        buf1_ += freq_ * (buf0_ - buf1_);
        return mode_ == Mode::Lowpass ? buf1_ : in - buf0_;
    }

    void SetFreq(float f)
    {
        freq_ = f;
        CalculateFeedback();
    }
    void SetRes(float r)
    {
        res_ = r;
        CalculateFeedback();
    }
    void SetMode(Mode m) { mode_ = m; }
    void Reset() { buf0_ = buf1_ = 0.f; }

  private:
    void CalculateFeedback() { fb_amt_ = res_ + (res_ / (1.f - freq_)); }

    Mode  mode_   = Mode::Lowpass;
    float freq_   = .5f;
    float res_    = .5f;
    float buf0_   = 0.f;
    float buf1_   = 0.f;
    float fb_amt_ = 0.f;
};

class DjFilter
{
  public:
    void Init(const RateScale& rate)
    {
        rate_     = rate;
        smooth_   = rate.Coef(.0002f);
        for(auto* f : {&llp_, &rlp_})
            f->SetMode(BasicMMF::Mode::Lowpass);
        for(auto* f : {&lhp_, &rhp_})
            f->SetMode(BasicMMF::Mode::Highpass);
        for(auto* f : {&llp_, &rlp_, &lhp_, &rhp_})
        {
            f->Reset();
            f->SetRes(.6f);
        }
        SetControl(.5f);
        lp_ = lp_target_;
        hp_ = hp_target_;
    }

    void Process(float in_l, float in_r, float* out_l, float* out_r)
    {
        fonepole(lp_, lp_target_, smooth_);
        fonepole(hp_, hp_target_, smooth_);

        // The firmware's 0-1 coefficients were tuned at 48 kHz. Scale them so
        // the cutoff lands at roughly the same frequency at other rates.
        const float lp = fclamp(lp_ / rate_.ratio, 0.f, .99f);
        const float hp = fclamp(hp_ / rate_.ratio, 0.f, .99f);
        llp_.SetFreq(lp);
        rlp_.SetFreq(lp);
        lhp_.SetFreq(hp);
        rhp_.SetFreq(hp);

        if(hp_ > .8f)
        {
            const float param = 5.f * (1.f - cutoff_);
            lhp_.SetRes(res_ * param);
            rhp_.SetRes(res_ * param);
        }

        *out_l = lhp_.Process(llp_.Process(in_l));
        *out_r = rhp_.Process(rlp_.Process(in_r));
    }

    /** 0 = dark (LP closed), .5 = open, 1 = thin (HP open) */
    void SetControl(float cutoff)
    {
        cutoff_    = cutoff;
        lp_target_ = fclamp(.01f + cutoff_ * 2.f, 0.f, .99f);
        lp_target_ = lp_target_ * lp_target_ * lp_target_;

        hp_target_ = fclamp((cutoff_ * 1.9f) - 1.f, 0.f, 1.f);
        hp_target_ = hp_target_ * hp_target_ * hp_target_;
    }

    void SetRes(float res)
    {
        res *= .95f;
        res_ = res;
        for(auto* f : {&llp_, &rlp_, &lhp_, &rhp_})
            f->SetRes(res);
    }

  private:
    RateScale rate_;
    BasicMMF  llp_, rlp_, lhp_, rhp_;
    float     smooth_    = .0002f;
    float     cutoff_    = .5f;
    float     res_       = 0.f;
    float     lp_        = 0.f, lp_target_ = 0.f;
    float     hp_        = 0.f, hp_target_ = 0.f;
};

} // namespace prism
