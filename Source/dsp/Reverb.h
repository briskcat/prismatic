// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Griesinger/Dattorro plate reverb from Mutable Instruments, as used in the
// TAPE and TEMPO firmware (reverb.h + fx_engine.h, via Electrosmith DaisySP).
// This port keeps the 16-bit delay memory, which is part of its sound, and
// adds TEMPO's freeze.
//
// Its delay lengths are fixed in samples and tuned for ~48 kHz. Hosts running
// at 88.2 kHz or above run the reverb at 1/2 or 1/4 rate so it sounds the same.

#pragma once
#include <algorithm>
#include <cstdint>
#include "DspUtil.h"

namespace prism
{
namespace fx
{
enum LFOIndex
{
    LFO_1,
    LFO_2
};

inline int32_t Clip16(int32_t x) { return x < -32768 ? -32768 : (x > 32767 ? 32767 : x); }

struct Format16
{
    typedef uint16_t T;
    static float Decompress(T value) { return static_cast<float>(static_cast<int16_t>(value)) * 0.0000305175f; }
    static T Compress(float value) { return static_cast<uint16_t>(Clip16(static_cast<int32_t>(value * 32768.0f))); }
};

/** Ported from pichenettes/eurorack plaits/dsp/fx/fx_engine.h */
template <size_t size>
class FxEngine
{
  public:
    typedef Format16::T T;

    void Init(T* buffer)
    {
        buffer_ = buffer;
        lfo_phase_[0] = lfo_phase_[1] = 0.f;
        lfo_freq_[0] = lfo_freq_[1] = 0.f;
        Clear();
    }

    void Clear()
    {
        std::fill(&buffer_[0], &buffer_[size], T(0));
        write_ptr_ = 0;
    }

    struct Empty
    {
    };

    template <int32_t l, typename TT = Empty>
    struct Reserve
    {
        typedef TT Tail;
        enum
        {
            length = l
        };
    };

    template <typename Memory, int32_t index>
    struct DelayLine
    {
        enum
        {
            length = DelayLine<typename Memory::Tail, index - 1>::length,
            base   = DelayLine<Memory, index - 1>::base + DelayLine<Memory, index - 1>::length + 1
        };
    };

    template <typename Memory>
    struct DelayLine<Memory, 0>
    {
        enum
        {
            length = Memory::length,
            base   = 0
        };
    };

    class Context
    {
        friend class FxEngine;

      public:
        void Load(float value) { accumulator_ = value; }
        void Read(float value, float scale) { accumulator_ += value * scale; }
        void Read(float value) { accumulator_ += value; }
        void Write(float& value) { value = accumulator_; }
        void Write(float& value, float scale)
        {
            value = accumulator_;
            accumulator_ *= scale;
        }

        template <typename D>
        void Write(D&, int32_t offset, float scale)
        {
            static_assert(D::base + D::length <= size, "delay memory full");
            T w = Format16::Compress(accumulator_);
            if(offset == -1)
                buffer_[(write_ptr_ + D::base + D::length - 1) & MASK] = w;
            else
                buffer_[(write_ptr_ + D::base + offset) & MASK] = w;
            accumulator_ *= scale;
        }

        template <typename D>
        void Write(D& d, float scale)
        {
            Write(d, 0, scale);
        }

        template <typename D>
        void WriteAllPass(D& d, int32_t offset, float scale)
        {
            Write(d, offset, scale);
            accumulator_ += previous_read_;
        }

        template <typename D>
        void WriteAllPass(D& d, float scale)
        {
            WriteAllPass(d, 0, scale);
        }

        template <typename D>
        void Read(D&, int32_t offset, float scale)
        {
            static_assert(D::base + D::length <= size, "delay memory full");
            T r;
            if(offset == -1)
                r = buffer_[(write_ptr_ + D::base + D::length - 1) & MASK];
            else
                r = buffer_[(write_ptr_ + D::base + offset) & MASK];
            float r_f      = Format16::Decompress(r);
            previous_read_ = r_f;
            accumulator_ += r_f * scale;
        }

        template <typename D>
        void Read(D& d, float scale)
        {
            Read(d, 0, scale);
        }

        void Lp(float& state, float coefficient)
        {
            state += coefficient * (accumulator_ - state);
            accumulator_ = state;
        }

        template <typename D>
        void Interpolate(D&, float offset, LFOIndex index, float amplitude, float scale)
        {
            static_assert(D::base + D::length <= size, "delay memory full");
            lfo_phase_[index] += lfo_freq_[index];
            lfo_phase_[index] = lfo_phase_[index] >= 1.f ? lfo_phase_[index] - 1.f : lfo_phase_[index];

            offset += amplitude * std::cos(lfo_phase_[index] * kTwoPi);

            int32_t offset_integral   = static_cast<int32_t>(offset);
            float   offset_fractional = offset - static_cast<float>(offset_integral);

            float a = Format16::Decompress(buffer_[(write_ptr_ + offset_integral + D::base) & MASK]);
            float b = Format16::Decompress(buffer_[(write_ptr_ + offset_integral + D::base + 1) & MASK]);
            float x        = a + (b - a) * offset_fractional;
            previous_read_ = x;
            accumulator_ += x * scale;
        }

      private:
        float   accumulator_   = 0.f;
        float   previous_read_ = 0.f;
        T*      buffer_        = nullptr;
        int32_t write_ptr_     = 0;
        float*  lfo_phase_     = nullptr;
        float*  lfo_freq_      = nullptr;
    };

    void SetLFOFrequency(LFOIndex index, float frequency) { lfo_freq_[index] = frequency; }

    void Start(Context* c)
    {
        --write_ptr_;
        if(write_ptr_ < 0)
            write_ptr_ += size;
        c->accumulator_   = 0.0f;
        c->previous_read_ = 0.0f;
        c->buffer_        = buffer_;
        c->write_ptr_     = write_ptr_;
        c->lfo_phase_     = lfo_phase_;
        c->lfo_freq_      = lfo_freq_;

        if((write_ptr_ & 31) == 0)
        {
            for(int i = 0; i < 2; ++i)
            {
                lfo_phase_[i] += lfo_freq_[i];
                lfo_phase_[i] = lfo_phase_[i] >= 1.f ? lfo_phase_[i] - 1.f : lfo_phase_[i];
            }
        }
    }

  private:
    enum
    {
        MASK = size - 1
    };

    int32_t write_ptr_ = 0;
    T*      buffer_    = nullptr;
    float   lfo_phase_[2]{};
    float   lfo_freq_[2]{};
};
} // namespace fx

class Reverb
{
  public:
    void Init(const RateScale& rate)
    {
        // run at ~48 kHz: 1x up to 64 kHz, 2x up to 128 kHz, 4x above
        decim_ = rate.sr > 128000.f ? 4 : (rate.sr > 64000.f ? 2 : 1);
        const float internal_sr = rate.sr / static_cast<float>(decim_);

        engine_.Init(buffer_);
        engine_.SetLFOFrequency(fx::LFO_1, 0.5f / internal_sr);
        engine_.SetLFOFrequency(fx::LFO_2, 0.3f / internal_sr);
        lp_          = 0.7f;
        diffusion_   = 0.625f;
        input_gain_  = .3f;
        reverb_time_ = .5f;
        amount_      = 0.f;
        freeze_      = 0.f;
        lp_decay_1_ = lp_decay_2_ = 0.f;
        acc_l_ = acc_r_ = 0.f;
        prev_l_ = prev_r_ = cur_l_ = cur_r_ = 0.f;
        count_ = 0;
    }

    void Clear() { engine_.Clear(); }

    /** In place. Mixes the wet signal over the dry by `amount`. */
    void Process(float* left, float* right)
    {
        float wet_l, wet_r;
        if(decim_ == 1)
        {
            ProcessCore(*left, *right, &wet_l, &wet_r);
        }
        else
        {
            // average the input over decim_ samples, linearly interpolate the output
            acc_l_ += *left;
            acc_r_ += *right;
            if(++count_ == decim_)
            {
                const float inv = 1.f / static_cast<float>(decim_);
                prev_l_         = cur_l_;
                prev_r_         = cur_r_;
                ProcessCore(acc_l_ * inv, acc_r_ * inv, &cur_l_, &cur_r_);
                acc_l_ = acc_r_ = 0.f;
                count_          = 0;
            }
            const float t = static_cast<float>(count_ + 1) / static_cast<float>(decim_);
            wet_l         = prev_l_ + (cur_l_ - prev_l_) * t;
            wet_r         = prev_r_ + (cur_r_ - prev_r_) * t;
        }
        *left += (wet_l - *left) * amount_;
        *right += (wet_r - *right) * amount_;
    }

    void SetAmount(float amount) { amount_ = amount; }
    void SetInputGain(float input_gain) { input_gain_ = input_gain; }
    void SetTime(float reverb_time) { reverb_time_ = reverb_time; }
    void SetDiffusion(float diffusion) { diffusion_ = diffusion; }
    void SetLowpass(float lp) { lp_ = lp; }
    void SetFreeze(bool freeze) { freeze_ = freeze ? 1.f : 0.f; }

  private:
    /** Pure wet output (the firmware's Process with amount = 1). */
    void ProcessCore(float in_l, float in_r, float* out_l, float* out_r)
    {
        // This is the Griesinger topology described in the Dattorro paper
        // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
        // Modulation is applied to the two long delays for a slow shimmer/chorus effect.
        typedef E::Reserve<150,
                E::Reserve<214,
                E::Reserve<319,
                E::Reserve<527,
                E::Reserve<2182,
                E::Reserve<2690,
                E::Reserve<4501,
                E::Reserve<2525,
                E::Reserve<2197,
                E::Reserve<6312>>>>>>>>>> Memory;
        E::DelayLine<Memory, 0> ap1;
        E::DelayLine<Memory, 1> ap2;
        E::DelayLine<Memory, 2> ap3;
        E::DelayLine<Memory, 3> ap4;
        E::DelayLine<Memory, 4> dap1a;
        E::DelayLine<Memory, 5> dap1b;
        E::DelayLine<Memory, 6> del1;
        E::DelayLine<Memory, 7> dap2a;
        E::DelayLine<Memory, 8> dap2b;
        E::DelayLine<Memory, 9> del2;
        E::Context c;

        const float kap  = diffusion_;
        const float krt  = fclamp(reverb_time_ + freeze_, 0.f, 1.f);
        const float lock = 1.0f - freeze_;
        const float klp  = fclamp(lp_ + freeze_ * (1.0f - lp_), 0.f, 1.f);
        const float gain = input_gain_ * lock;

        float lp_1 = lp_decay_1_;
        float lp_2 = lp_decay_2_;

        float wet;
        float apout = 0.0f;
        engine_.Start(&c);

        c.Read(in_l + in_r, gain);

        // Diffuse through 4 allpasses.
        c.Read(ap1, -1, kap);
        c.WriteAllPass(ap1, -kap);
        c.Read(ap2, -1, kap);
        c.WriteAllPass(ap2, -kap);
        c.Read(ap3, -1, kap);
        c.WriteAllPass(ap3, -kap);
        c.Read(ap4, -1, kap);
        c.WriteAllPass(ap4, -kap);
        c.Write(apout);

        // Main reverb loop.
        c.Load(apout);
        c.Interpolate(del2, 6261.0f, fx::LFO_2, 50.0f, krt);
        c.Lp(lp_1, klp);
        c.Read(dap1a, -1, -kap);
        c.WriteAllPass(dap1a, kap);
        c.Read(dap1b, -1, kap);
        c.WriteAllPass(dap1b, -kap);
        c.Write(del1, 2.0f);
        c.Write(wet, 0.0f);
        *out_l = wet;

        c.Load(apout);
        c.Interpolate(del1, 4460.0f, fx::LFO_1, 40.0f, krt);
        c.Lp(lp_2, klp);
        c.Read(dap2a, -1, kap);
        c.WriteAllPass(dap2a, -kap);
        c.Read(dap2b, -1, -kap);
        c.WriteAllPass(dap2b, kap);
        c.Write(del2, 2.0f);
        c.Write(wet, 0.0f);
        *out_r = wet;

        lp_decay_1_ = lp_1;
        lp_decay_2_ = lp_2;
    }

    typedef fx::FxEngine<32768> E;
    E engine_;

    float amount_      = 0.f;
    float input_gain_  = .3f;
    float reverb_time_ = .5f;
    float diffusion_   = .625f;
    float lp_          = .7f;
    float freeze_      = 0.f;
    float lp_decay_1_  = 0.f;
    float lp_decay_2_  = 0.f;

    int   decim_ = 1, count_ = 0;
    float acc_l_ = 0.f, acc_r_ = 0.f;
    float prev_l_ = 0.f, prev_r_ = 0.f, cur_l_ = 0.f, cur_r_ = 0.f;

    uint16_t buffer_[32768]{};
};

} // namespace prism
