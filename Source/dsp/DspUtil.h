// Small DSP helpers, ported from DaisySP (MIT, (c) Electrosmith) so the FX
// code can build on desktop without the Daisy libraries.
//
// The firmware runs at a fixed 48 kHz, so many of its constants (one-pole
// smoothing coefficients, delay lengths in samples) assume that rate.
// RateScale converts them for whatever rate the host is running.

#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace prism
{
constexpr float kFirmwareRate = 48000.f;
constexpr float kPi           = 3.14159265358979f;
constexpr float kTwoPi        = 2.f * kPi;

inline float fclamp(float in, float min, float max)
{
    return std::fmin(std::fmax(in, min), max);
}

inline void fonepole(float& out, float in, float coeff)
{
    out += coeff * (in - out);
}

inline float SoftLimit(float x)
{
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

inline float SoftClip(float x)
{
    if(x < -3.f)
        return -1.f;
    if(x > 3.f)
        return 1.f;
    return SoftLimit(x);
}

/** Converts firmware (48 kHz) constants to the current host rate. */
struct RateScale
{
    float sr    = kFirmwareRate;
    float ratio = 1.f; // sr / 48000

    void Init(float sampleRate)
    {
        sr    = sampleRate;
        ratio = sampleRate / kFirmwareRate;
    }

    /** One-pole coefficient tuned at 48 kHz -> same time constant at sr. */
    float Coef(float coef48) const
    {
        return 1.f - std::pow(1.f - coef48, 1.f / ratio);
    }

    /** A length in samples at 48 kHz -> the same duration at sr. */
    float Samples(float samples48) const { return samples48 * ratio; }
};

class DcBlock
{
  public:
    void Init() { input_ = output_ = 0.f; }

    float Process(float in)
    {
        const float out = in - input_ + gain_ * output_;
        output_         = out;
        input_          = in;
        return out;
    }

  private:
    float input_ = 0.f, output_ = 0.f, gain_ = 0.99f;
};

/** Cheap deterministic RNG, one per effect instance (the firmware used rand()). */
class Rng
{
  public:
    explicit Rng(uint32_t seed = 22222) : state_(seed) {}

    uint32_t Next()
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    /** Uniform in [0, 1) */
    float Uniform() { return (Next() >> 8) * (1.f / 16777216.f); }

  private:
    uint32_t state_;
};

} // namespace prism
