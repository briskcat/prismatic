// Space: the plate reverb from Stray, in the reverb slot.
//
// A stereo plate after Jon Dattorro's "Effect Design, Part 1" (JAES 1997):
// a short pre-delay, four input diffusers that smear the sound into a wash,
// then a figure-of-eight "tank" of two halves that feed each other, each with
// a slowly wobbling allpass, a damping filter and long delays. The output taps
// are the paper's. Lengths are the paper's (at 29761 Hz) scaled to the running
// sample rate, so it sounds the same at any rate. This is an original
// implementation of the paper, written for Stray and ported here with:
//   - Tone (the tank's damping) and Diffusion (the input diffusers' strength)
//     as knobs; in Stray both are fixed (Tone .6 and Diffusion .7 here are
//     Stray's values).
//   - Freeze: the input fades out and the tank goes lossless, so the tail holds.
//   - Stray's Space blend for Mix: dry and plate trade places at constant
//     loudness, and the plate is fed a little under the dry level because its
//     long tail stacks up louder than what goes in.
//
// Every control glides per sample (Mix, Decay, Tone and Diffusion arrive
// already smoothed from FxChain; decay and freeze also glide here, ~20 ms),
// because the tank's feedback gain clicks if it jumps.

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "DspUtil.h"

namespace prism
{
class PlateLine
{
  public:
    void Init(int length)
    {
        len = std::max(length, 1);
        buf.assign(static_cast<size_t>(len + 4), 0.f);
        w = 0;
    }
    void  Clear() { std::fill(buf.begin(), buf.end(), 0.f); }
    float Tap(int d) const { return buf[Index(d)]; }
    float TapFrac(float d) const
    {
        const int   i = static_cast<int>(d);
        const float f = d - static_cast<float>(i);
        return Tap(i) + (Tap(i + 1) - Tap(i)) * f;
    }
    float Out() const { return Tap(len); }
    void  Push(float x)
    {
        buf[w] = x;
        if(++w == buf.size())
            w = 0;
    }

  private:
    size_t Index(int d) const
    {
        const size_t n = buf.size();
        return (w + n - static_cast<size_t>(std::clamp(d, 1, static_cast<int>(n) - 1))) % n;
    }
    std::vector<float> buf;
    int                len = 1;
    size_t             w   = 0;
};

class Plate
{
  public:
    static constexpr float kSend = .6f; // Stray's kPlateSend

    void Init(const RateScale& rate)
    {
        const float sr = rate.sr;
        k              = sr / 29761.f;
        auto n         = [&](float x) { return std::max(1, static_cast<int>(std::lround(x * k))); };
        pre.Init(n(600.f));
        const float diff[4]{142.f, 107.f, 379.f, 277.f};
        for(int i = 0; i < 4; ++i)
            inAp[static_cast<size_t>(i)].Init(n(diff[i]));
        excursion = 16.f * k;
        apL1.Init(n(672.f) + static_cast<int>(excursion) + 2);
        apR1.Init(n(908.f) + static_cast<int>(excursion) + 2);
        apL1Len = static_cast<float>(n(672.f));
        apR1Len = static_cast<float>(n(908.f));
        dL1.Init(n(4453.f));
        apL2.Init(n(1800.f));
        dL2.Init(n(3720.f));
        dR1.Init(n(4217.f));
        apR2.Init(n(2656.f));
        dR2.Init(n(3163.f));
        lfoInc = 6.2831853f / sr; // 1 Hz
        glideK = 1.f - std::exp(-1.f / (.02f * sr));
        fresh  = true;
        Clear();
    }

    void Clear()
    {
        for(PlateLine* l : {&pre, &inAp[0], &inAp[1], &inAp[2], &inAp[3], &apL1, &dL1, &apL2, &dL2, &apR1, &dR1, &apR2, &dR2})
            l->Clear();
        bw = dampL = dampR = 0.f;
    }

    /** 0..1: dry only at 0, plate only at 1, equal loudness on the way. */
    void SetMix(float m)
    {
        const float th = fclamp(m, 0.f, 1.f) * 1.5707963f;
        keep           = std::cos(th);
        send           = std::sin(th) * kSend;
    }
    /** 0..1, Stray's Decay knob. */
    void SetDecay(float d) { decayTo = .2f + fclamp(d, 0.f, 1.f) * .76f; }
    /** 0..1: dark (heavy damping) to bright (none). */
    void SetTone(float t) { dampTo = .75f * (1.f - fclamp(t, 0.f, 1.f)); }
    /** 0..1: how hard the input diffusers smear; .7 is Stray's. */
    void SetDiffusion(float d) { diffusion = std::min(.3f + fclamp(d, 0.f, 1.f), 1.2f); }
    void SetFreeze(bool f) { freezeTo = f ? 1.f : 0.f; }

    /** In place: dry in, dry/plate blend out. */
    void Process(float* l, float* r)
    {
        if(fresh)
            decay = decayTo, damping = dampTo, fresh = false;
        fonepole(freeze, freezeTo, glideK);
        fonepole(decay, decayTo + freeze * (1.f - decayTo), glideK);
        fonepole(damping, dampTo * (1.f - freeze), glideK);

        const float dryL = *l, dryR = *r;
        const float in   = (dryL + dryR) * .5f * send * (1.f - freeze);

        // Pre-delay and input bandwidth.
        const float x = pre.Out();
        pre.Push(in);
        bw += .9995f * (x - bw);
        float       s = bw;
        const float g[4]{.75f, .75f, .625f, .625f};
        for(int i = 0; i < 4; ++i)
            s = Allpass(inAp[static_cast<size_t>(i)], s, std::min(g[i] * diffusion, .9f));

        lfo += lfoInc;
        if(lfo > 6.2831853f)
            lfo -= 6.2831853f;
        const float m = std::sin(lfo) * excursion;

        const float feedL = dR2.Out() * decay, feedR = dL2.Out() * decay;
        // Left half of the tank.
        float tl = ModAllpass(apL1, s + feedL, -.7f, apL1Len + m);
        dL1.Push(tl);
        dampL += (1.f - damping) * (dL1.Out() - dampL);
        tl = Allpass(apL2, dampL * decay, Decay2());
        dL2.Push(tl);
        // Right half.
        float tr = ModAllpass(apR1, s + feedR, -.7f, apR1Len - m);
        dR1.Push(tr);
        dampR += (1.f - damping) * (dR1.Out() - dampR);
        tr = Allpass(apR2, dampR * decay, Decay2());
        dR2.Push(tr);

        auto        t  = [&](const PlateLine& line, float at) { return line.Tap(std::max(1, static_cast<int>(std::lround(at * k)))); };
        const float wl = .6f * (t(dR1, 266) + t(dR1, 2974) - t(apR2, 1913) + t(dR2, 1996) - t(dL1, 1990) - t(apL2, 187) - t(dL2, 1066));
        const float wr = .6f * (t(dL1, 353) + t(dL1, 3627) - t(apL2, 1228) + t(dL2, 2673) - t(dR1, 2111) - t(apR2, 335) - t(dR2, 121));

        // Stray bends the sum above .7 so a pile-up can't clip; here only the
        // plate is bent, so the dry passes untouched when Mix is low.
        *l = dryL * keep + Soft(wl);
        *r = dryR * keep + Soft(wr);
    }

  private:
    float        Decay2() const { return fclamp(decay + .15f, .25f, .5f); }
    static float Soft(float x)
    {
        const float a = std::fabs(x);
        return a < .7f ? x : std::copysign(.7f + .3f * std::tanh((a - .7f) / .3f), x);
    }
    static float Allpass(PlateLine& line, float x, float g)
    {
        const float d = line.Out();
        const float v = x - g * d;
        line.Push(v);
        return d + g * v;
    }
    static float ModAllpass(PlateLine& line, float x, float g, float delay)
    {
        const float d = line.TapFrac(delay);
        const float v = x - g * d;
        line.Push(v);
        return d + g * v;
    }

    PlateLine                pre;
    std::array<PlateLine, 4> inAp;
    PlateLine                apL1, dL1, apL2, dL2, apR1, dR1, apR2, dR2;
    float apL1Len = 1.f, apR1Len = 1.f;
    float k = 1.f, excursion = 16.f, lfo = 0.f, lfoInc = 0.f;
    float bw = 0.f, dampL = 0.f, dampR = 0.f;
    float keep = 1.f, send = 0.f, diffusion = 1.f;
    float decay = .58f, decayTo = .58f, damping = .3f, dampTo = .3f, freeze = 0.f, freezeTo = 0.f, glideK = .001f;
    bool  fresh = true;
};
} // namespace prism
