// The full effects chain. The order of the first eight blocks can be changed
// (SetOrder); the default is
//   input -> filter -> drive -> tape (warble) -> crush -> delay -> glitch
//   -> reverb -> looper -> squash -> dry/wet -> output
// Squash always comes last, as the output stage.
//
// Filter, drive, warble, delay, reverb and the looper follow the TAPE
// firmware. Glitch (granular delay) and reverb freeze come from TEMPO. Crush is
// new. Putting the looper last matches TAPE's default (FX recorded into the
// loop); moving it to the front matches its "FX after looper" setting.
//
// Each section has an on/off switch that crossfades, so toggling never clicks.
// Reordering briefly fades to dry while the blocks swap.

#pragma once
#include <array>
#include "Crusher.h"
#include "DjFilter.h"
#include "GlitchDelay.h"
#include "Reverb.h"
#include "Squash.h"
#include "TapeDelay.h"
#include "TapeLooper.h"
#include "Warble.h"

namespace prism
{
struct FxParams
{
    bool classic = false; // original one-knob couplings

    float input_gain = 1.f, output_gain = 1.f, dry_wet = 1.f;

    bool  filter_on = true;
    float cutoff = .5f, resonance = 0.f;

    bool  drive_on = true;
    float drive    = 0.f;

    bool  tape_on = true;
    float warble_depth = 0.f, warble_rate = .3f;

    bool  crush_on = false;
    float crush_rate_hz = 12000.f, crush_bits = 12.f, crush_mix = 1.f;

    bool  delay_on = true;
    float delay_seconds = .375f, delay_time_norm = .5f, delay_feedback = 0.f, delay_mix = .35f;
    bool  delay_varispeed = true;

    bool  glitch_on = false;
    int   glitch_mode = 0, glitch_division = 7;
    float glitch_chaos = .3f, glitch_feedback = .3f, glitch_mix = .5f;
    bool  glitch_freeze = false;

    bool  reverb_on = true;
    float reverb_mix = 0.f, reverb_decay = .5f, reverb_tone = .7f, reverb_diffusion = .5f;
    bool  reverb_freeze = false;

    bool  squash_on = false;
    float squash = 0.f;

    float looper_speed = 1.f, looper_glide = .2f, looper_dub = 1.f, looper_level = 1.f, looper_scrub = 0.f;
    bool  looper_reverse = false;
};

/** The blocks whose order can change */
enum class Block : uint8_t
{
    Filter,
    Drive,
    Tape,
    Crush,
    Delay,
    Glitch,
    Reverb,
    Looper,
};
constexpr int kNumBlocks = 8;
using BlockOrder = std::array<Block, kNumBlocks>;

inline BlockOrder DefaultOrder()
{
    return {Block::Filter, Block::Drive, Block::Tape, Block::Crush, Block::Delay, Block::Glitch, Block::Reverb, Block::Looper};
}

class FxChain
{
  public:
    /** init_looper = false keeps the loop (e.g. the host re-preparing at the same rate) */
    void Init(float sample_rate, bool init_looper = true)
    {
        rate_.Init(sample_rate);
        coef_ = rate_.Coef(.001f);

        dc_l_.Init();
        dc_r_.Init();
        filter_.Init(rate_);
        warble_.Init(rate_);
        crusher_.Init(rate_);
        delay_.Init(rate_);
        glitch_.Init(rate_);
        reverb_.Init(rate_);
        squash_.Init(rate_);
        if(init_looper)
            looper_.Init(rate_);
        route_step_ = 1.f / rate_.Samples(144.f); // ~3 ms
    }

    /** Jump all smoothed values to their targets (after Init + SetParams). */
    void Snap()
    {
        for(auto& s : sections_)
            s.env = s.target ? 1.f : 0.f;
        in_gain_       = p_.input_gain;
        out_gain_      = p_.output_gain;
        dry_wet_       = p_.dry_wet;
        cutoff_        = cutoff_target_;
        res_           = res_target_;
        sat_           = sat_target_;
        rev_amount_    = rev_amount_target_;
        rev_time_      = rev_time_target_;
        rev_tone_      = rev_tone_target_;
        rev_diffusion_ = rev_diffusion_target_;
    }

    GlitchDelay& Glitch() { return glitch_; }
    TapeLooper&  Looper() { return looper_; }

    /** Takes effect after a short fade. */
    void SetOrder(const BlockOrder& order) { pending_order_ = order; }
    const BlockOrder& Order() const { return order_; }

    /** Called once per block with the latest parameter values. */
    void SetParams(const FxParams& p)
    {
        p_ = p;

        sections_[kFilter].target = p.filter_on;
        sections_[kDrive].target  = p.drive_on;
        sections_[kTape].target   = p.tape_on;
        sections_[kCrush].target  = p.crush_on;
        sections_[kDelay].target  = p.delay_on;
        sections_[kReverb].target = p.reverb_on;
        sections_[kSquash].target = p.squash_on;

        cutoff_target_ = p.cutoff;
        res_target_    = p.resonance;

        const float sat     = std::log(1.7f * p.drive + 1.f);
        sat_target_         = sat * 13.f + 1.f;

        if(p.classic)
            warble_.SetClassic(p.warble_depth);
        else
        {
            warble_.SetDepth(p.warble_depth);
            warble_.SetRate(p.warble_rate);
        }

        crusher_.SetRateHz(p.crush_rate_hz);
        crusher_.SetBits(p.crush_bits);
        crusher_.SetMix(p.crush_mix);

        delay_.SetClassic(p.classic);
        delay_.SetVarispeed(p.delay_varispeed);
        delay_.SetTimeSeconds(p.delay_seconds);
        delay_.SetFeedback(p.delay_feedback);
        delay_.SetMix(p.delay_mix);

        glitch_.SetActive(p.glitch_on);
        glitch_.SetMode(p.glitch_mode == 0 ? GlitchDelay::Mode::Glitch : GlitchDelay::Mode::Shimmer);
        glitch_.SetDivision(p.glitch_division);
        glitch_.SetChaos(p.glitch_chaos);
        glitch_.SetFeedback(p.glitch_feedback);
        glitch_.SetMix(p.glitch_mix);
        glitch_.SetFreeze(p.glitch_freeze);

        if(p.classic)
        {
            // TAPE: one reverb knob sets amount, tone and diffusion;
            // the delay time knob also sets the reverb decay.
            const float amt         = 1.3f * std::log(p.reverb_mix + 1.f);
            rev_amount_target_      = amt * amt * .8f;
            rev_tone_target_        = amt * .6f + .4f;
            rev_diffusion_target_   = amt * .6f;
            rev_time_target_        = fclamp(p.delay_time_norm, .05f, .97f);
        }
        else
        {
            rev_amount_target_    = p.reverb_mix;
            rev_tone_target_      = .4f + .6f * p.reverb_tone;
            rev_diffusion_target_ = .75f * p.reverb_diffusion;
            rev_time_target_      = .05f + .92f * p.reverb_decay;
        }
        reverb_.SetFreeze(p.reverb_freeze);

        squash_.SetAmount(p.squash);

        looper_.SetSpeed(p.looper_speed);
        looper_.SetReverse(p.looper_reverse);
        looper_.SetGlide(p.looper_glide);
        looper_.SetDub(p.looper_dub);
        looper_.SetLevel(p.looper_level);
        looper_.SetScrub(p.looper_scrub);
    }

    void Process(float* l, float* r, int n)
    {
        for(int i = 0; i < n; ++i)
        {
            for(auto& s : sections_)
                fonepole(s.env, s.target ? 1.f : 0.f, coef_);
            fonepole(in_gain_, p_.input_gain, coef_);
            fonepole(out_gain_, p_.output_gain, coef_);
            fonepole(dry_wet_, p_.dry_wet, coef_);

            const float dry_l = l[i] * in_gain_, dry_r = r[i] * in_gain_;
            float       x_l = dc_l_.Process(dry_l), x_r = dc_r_.Process(dry_r);

            // smoothed controls
            fonepole(cutoff_, cutoff_target_, coef_);
            fonepole(res_, res_target_, coef_);
            fonepole(sat_, sat_target_, coef_);
            fonepole(rev_amount_, rev_amount_target_, coef_);
            fonepole(rev_time_, rev_time_target_, coef_);
            fonepole(rev_tone_, rev_tone_target_, coef_);
            fonepole(rev_diffusion_, rev_diffusion_target_, coef_);

            // reordering: fade to dry, swap, fade back
            if(pending_order_ != order_)
            {
                route_env_ -= route_step_;
                if(route_env_ <= 0.f)
                {
                    route_env_ = 0.f;
                    order_     = pending_order_;
                }
            }
            else
                route_env_ = std::fmin(1.f, route_env_ + route_step_);

            for(Block b : order_)
                ProcessBlock(b, x_l, x_r);

            Section(kSquash, x_l, x_r, [&](float& a, float& b) { squash_.Process(&a, &b); });

            const float wet = dry_wet_ * route_env_;
            l[i] = (dry_l + (x_l - dry_l) * wet) * out_gain_;
            r[i] = (dry_r + (x_r - dry_r) * wet) * out_gain_;
        }
    }

  private:
    void ProcessBlock(Block b, float& x_l, float& x_r)
    {
        switch(b)
        {
            case Block::Filter:
                filter_.SetControl(cutoff_);
                filter_.SetRes(res_);
                Section(kFilter, x_l, x_r, [&](float& a, float& c) { filter_.Process(a, c, &a, &c); });
                break;
            case Block::Drive:
                // soft clip with loudness compensation
                Section(kDrive, x_l, x_r, [&](float& a, float& c) {
                    a = SoftClip(sat_ * a);
                    c = SoftClip(sat_ * c);
                    const float gain = 1.f - SoftClip(.4f * (sat_ - 1.f)) * .7f;
                    a *= gain;
                    c *= gain;
                });
                break;
            case Block::Tape:
                Section(kTape, x_l, x_r, [&](float& a, float& c) { warble_.Process(a, c, &a, &c); });
                break;
            case Block::Crush:
                Section(kCrush, x_l, x_r, [&](float& a, float& c) { crusher_.Process(&a, &c); });
                break;
            case Block::Delay:
                Section(kDelay, x_l, x_r, [&](float& a, float& c) { delay_.Process(&a, &c); });
                break;
            case Block::Glitch:
                // fades itself in and out (SetActive) so its tail is kept tidy
                glitch_.Process(&x_l, &x_r);
                break;
            case Block::Reverb:
                reverb_.SetAmount(rev_amount_);
                reverb_.SetTime(rev_time_);
                reverb_.SetLowpass(rev_tone_);
                reverb_.SetDiffusion(rev_diffusion_);
                Section(kReverb, x_l, x_r, [&](float& a, float& c) { reverb_.Process(&a, &c); });
                break;
            case Block::Looper: looper_.Process(&x_l, &x_r); break;
        }
    }

    enum SectionId
    {
        kFilter,
        kDrive,
        kTape,
        kCrush,
        kDelay,
        kReverb,
        kSquash,
        kNumSections
    };

    struct SectionState
    {
        bool  target = true;
        float env    = 1.f;
    };

    /** Runs fn while the section is on or fading, and crossfades to bypass. */
    template <typename Fn>
    void Section(SectionId id, float& l, float& r, Fn&& fn)
    {
        const float env = sections_[id].env;
        if(env < 1e-4f && !sections_[id].target)
            return;
        float a = l, b = r;
        fn(a, b);
        l += (a - l) * env;
        r += (b - r) * env;
    }

    RateScale    rate_;
    FxParams     p_;
    SectionState sections_[kNumSections];
    float        coef_ = .001f;
    float        in_gain_ = 1.f, out_gain_ = 1.f, dry_wet_ = 1.f;

    DcBlock     dc_l_, dc_r_;
    DjFilter    filter_;
    Warble      warble_;
    Crusher     crusher_;
    TapeDelay   delay_;
    GlitchDelay glitch_;
    Reverb      reverb_;
    Squash      squash_;
    TapeLooper  looper_;

    BlockOrder order_ = DefaultOrder(), pending_order_ = DefaultOrder();
    float      route_env_ = 1.f, route_step_ = .01f;

    float cutoff_ = .5f, cutoff_target_ = .5f, res_ = 0.f, res_target_ = 0.f;
    float sat_ = 1.f, sat_target_ = 1.f;
    float rev_amount_ = 0.f, rev_amount_target_ = 0.f;
    float rev_time_ = .5f, rev_time_target_ = .5f;
    float rev_tone_ = .7f, rev_tone_target_ = .7f;
    float rev_diffusion_ = .5f, rev_diffusion_target_ = .5f;
};

} // namespace prism
