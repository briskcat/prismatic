// Tape looper. Follows TAPE firmware Sampler.h (FileSampler) and
// LooperEngine.h, rewritten on a plain float buffer instead of SD streaming.
//
// The loop behaves like a tape loop:
// - The playhead moves at a smoothed, signed speed. Stop slows the tape to a
//   halt, play spins it back up, and reverse runs it down through zero and back
//   up the other way. Glide sets how long that takes.
// - The record head follows the playhead. When overdubbing at a different
//   speed, input is written at the tape's rate, so a part overdubbed at half
//   speed plays back an octave up at normal speed, just like real tape.
// - While overdubbing, the existing loop is scaled by Dub (1 = keep it all).
// - While stopped, Scrub drags the tape to a position, so you hear it move.
// - Start / End set a window within the recorded tape. Playback and overdubs
//   loop inside it; moving it never touches the audio outside.
//
// Buttons follow the hardware:
//   REC   empty -> record; recording -> close the loop and keep overdubbing;
//         playing -> overdub on/off
//   PLAY  recording -> close the loop and play; playing -> stop; stopped -> play
//   CLEAR erase the loop

#pragma once
#include <atomic>
#include <vector>
#include "DspUtil.h"

namespace prism
{
class TapeLooper
{
  public:
    static constexpr float kMaxSeconds = 120.f;
    static constexpr int   kPeakBin    = 512; // samples per waveform-display bin

    enum class State
    {
        Empty,
        Recording, // first take
        Playing,
        Stopped,
    };

    void Init(const RateScale& rate)
    {
        rate_     = rate;
        capacity_ = static_cast<size_t>(rate.sr * kMaxSeconds);
        tape_l_.assign(capacity_, 0.f);
        tape_r_.assign(capacity_, 0.f);
        peaks_ = std::vector<std::atomic<float>>((capacity_ + kPeakBin - 1) / kPeakBin);
        for(auto& p : peaks_)
            p.store(0.f, std::memory_order_relaxed);

        input_step_ = rate.Coef(.001f);      // ~20 ms record fade
        dub_coef_   = rate.Coef(.0001f);
        level_coef_ = rate.Coef(.001f);
        seam_fade_  = rate.Samples(96.f);    // ~2 ms each side of the loop point
        clear_step_ = 1.f / rate.Samples(480.f);
        lim_up_     = rate.Coef(.05f);
        lim_down_   = rate.Coef(.0004f);
        ResetTape();
    }

    // ---- buttons (call from the audio thread) ----
    void PressRecord()
    {
        switch(state_)
        {
            case State::Empty:
                ResetTape();
                state_ = State::Recording;
                break;
            case State::Recording:
                CloseLoop();
                state_  = State::Playing;
                dubbing_ = true;
                break;
            case State::Playing: dubbing_ = !dubbing_; break;
            case State::Stopped:
                state_   = State::Playing;
                dubbing_ = true;
                break;
        }
    }

    void PressPlay()
    {
        switch(state_)
        {
            case State::Empty: break;
            case State::Recording:
                CloseLoop();
                state_ = State::Playing;
                break;
            case State::Playing:
                state_   = State::Stopped;
                dubbing_ = false;
                break;
            case State::Stopped:
                state_     = State::Playing;
                scrubbing_ = false;
                break;
        }
    }

    void PressClear()
    {
        if(state_ != State::Empty)
            clearing_ = true;
    }

    // ---- controls ----
    void SetSpeed(float ratio) { varispeed_ = fclamp(ratio, .05f, 4.f); }
    void SetReverse(bool r) { reverse_ = r; }
    /** 0 = snappy, 1 = very slow tape */
    void SetGlide(float g) { glide_coef_ = rate_.Coef(.01f * std::pow(.005f, fclamp(g, 0.f, 1.f))); }
    void SetDub(float d) { dub_target_ = fclamp(d, 0.f, 1.f); }
    void SetLevel(float l) { level_target_ = l; }
    /** The part of the tape to loop, in samples. Clamped to the recorded length. */
    void SetWindow(size_t start, size_t end)
    {
        win_start_target_ = start;
        win_end_target_   = end;
    }

    /** 0-1 position in the loop. Moves the tape only while stopped. */
    void SetScrub(float pos)
    {
        if(std::fabs(pos - last_scrub_) > 1e-6f)
        {
            last_scrub_ = pos;
            if(state_ == State::Stopped && len_ > 0)
            {
                scrubbing_    = true;
                scrub_target_ = fclamp(pos * static_cast<float>(len_), static_cast<float>(win_start_),
                                       static_cast<float>(win_end_) - 1.f);
            }
        }
    }

    /** In place: adds the loop to the signal passing through. */
    void Process(float* l, float* r)
    {
        ProcessSample(l, r);
        ui_state_.store(state_, std::memory_order_relaxed);
        ui_dubbing_.store(dubbing_, std::memory_order_relaxed);
        ui_len_.store(len_, std::memory_order_relaxed);
        ui_pos_.store(pos_, std::memory_order_relaxed);
        ui_win_start_.store(win_start_, std::memory_order_relaxed);
        ui_win_end_.store(win_end_, std::memory_order_relaxed);
    }

    /** Internal state, for the audio thread (e.g. fixed-length recording). */
    State  AudioState() const { return state_; }
    size_t AudioLength() const { return len_; }

  private:
    void ProcessSample(float* l, float* r)
    {
        const float in_l = *l, in_r = *r;
        fonepole(dub_, dub_target_, dub_coef_);
        fonepole(level_, level_target_, level_coef_);

        if(state_ == State::Recording)
        {
            Write(len_, in_l, in_r, false);
            ++len_;
            if(len_ >= capacity_)
            {
                CloseLoop();
                state_   = State::Playing;
                dubbing_ = true;
            }
            prev_in_l_ = in_l;
            prev_in_r_ = in_r;
            return;
        }
        if(len_ == 0)
            return;

        // clearing fades the loop out, then empties it
        if(clearing_)
        {
            clear_env_ -= clear_step_;
            if(clear_env_ <= 0.f)
            {
                ResetTape();
                return;
            }
        }

        UpdateWindow();

        // tape speed
        float target = 0.f;
        if(state_ == State::Playing)
            target = reverse_ ? -varispeed_ : varispeed_;
        else if(scrubbing_)
        {
            float dist = scrub_target_ - pos_;
            target     = fclamp(dist / rate_.Samples(2400.f), -3.f, 3.f);
            if(std::fabs(dist) < 1.f)
                scrubbing_ = false;
        }
        fonepole(speed_, target, scrubbing_ ? rate_.Coef(.01f) : glide_coef_);

        // read before writing, so overdubs aren't heard twice
        const float ws    = static_cast<float>(win_start_);
        const float we    = static_cast<float>(win_end_);
        const float seam  = std::fmin(pos_ - ws, we - pos_);
        jump_env_         = std::fmin(1.f, jump_env_ + 1.f / seam_fade_);
        last_seam_env_    = fclamp(seam / seam_fade_, 0.f, 1.f) * jump_env_;
        const float env   = last_seam_env_
                          * fclamp(std::fabs(speed_) * 8.f, 0.f, 1.f) // no DC when stopped
                          * clear_env_ * level_;
        float out_l, out_r;
        Read(pos_, &out_l, &out_r);

        // advance and write every sample the head passes
        const float input_target = dubbing_ && state_ == State::Playing ? 1.f : 0.f;
        input_env_ = fclamp(input_env_ + (input_target > input_env_ ? input_step_ : -input_step_), 0.f, 1.f);

        const float old = pos_;
        pos_ += speed_;
        const float moved = pos_ - old;
        if(input_env_ > .001f && std::fabs(moved) > 1e-6f)
        {
            const int64_t a = static_cast<int64_t>(std::floor(old));
            const int64_t b = static_cast<int64_t>(std::floor(pos_));
            // The record head trails the playhead by a couple of samples (as on
            // the hardware), so the playhead never reads what was just written.
            constexpr int64_t kLag = 2;
            if(b > a)
                for(int64_t i = a + 1; i <= b; ++i)
                    WriteDub(i - kLag, (static_cast<float>(i) - old) / moved, in_l, in_r);
            else
                for(int64_t i = a; i > b; --i)
                    WriteDub(i + kLag, (static_cast<float>(i) - old) / moved, in_l, in_r);
        }
        const float wlen = we - ws;
        while(pos_ >= we)
            pos_ -= wlen;
        while(pos_ < ws)
            pos_ += wlen;

        prev_in_l_ = in_l;
        prev_in_r_ = in_r;

        *l = in_l + out_l * env;
        *r = in_r + out_r * env;
    }

  public:
    // ---- for the UI (read from any thread) ----
    State  GetState() const { return ui_state_.load(std::memory_order_relaxed); }
    size_t GetWindowStart() const { return ui_win_start_.load(std::memory_order_relaxed); }
    size_t GetWindowEnd() const { return ui_win_end_.load(std::memory_order_relaxed); }
    bool   IsDubbing() const { return ui_dubbing_.load(std::memory_order_relaxed); }
    size_t GetLength() const { return ui_len_.load(std::memory_order_relaxed); }
    float  GetPosition() const { return ui_pos_.load(std::memory_order_relaxed); }
    size_t Capacity() const { return capacity_; }
    float Peak(size_t bin) const { return bin < peaks_.size() ? peaks_[bin].load(std::memory_order_relaxed) : 0.f; }

    // ---- saving / loading (caller makes sure audio isn't running Process) ----
    const float* DataL() const { return tape_l_.data(); }
    const float* DataR() const { return tape_r_.data(); }
    void Load(const float* l, const float* r, size_t frames)
    {
        ResetTape();
        frames = std::min(frames, capacity_);
        for(size_t i = 0; i < frames; ++i)
            Write(i, l[i], r[i], false);
        len_   = frames;
        state_ = frames > 0 ? State::Stopped : State::Empty;
        ui_state_.store(state_);
        ui_len_.store(len_);
        ui_pos_.store(0.f);
    }

  private:
    void ResetTape()
    {
        state_   = State::Empty;
        dubbing_ = false;
        len_     = 0;
        pos_     = 0.f;
        speed_   = 1.f;
        input_env_ = 0.f;
        clearing_  = false;
        clear_env_ = 1.f;
        scrubbing_ = false;
        lim_peak_l_ = lim_peak_r_ = .5f;
        for(auto& p : peaks_)
            p.store(0.f, std::memory_order_relaxed);
    }

    /** Applies the requested window. If the playhead ends up outside, it jumps in with a short fade. */
    void UpdateWindow()
    {
        constexpr size_t kMin = 64;
        size_t start = std::min(win_start_target_, len_ > kMin ? len_ - kMin : 0);
        size_t end   = std::min(win_end_target_, len_);
        if(end < start + kMin)
            end = std::min(start + kMin, len_);
        if(start != win_start_ || end != win_end_)
            jump_env_ = std::fmin(jump_env_, last_seam_env_); // ramp up from the old seam fade, don't jump
        win_start_ = start;
        win_end_   = end;
        if(pos_ < static_cast<float>(start) || pos_ >= static_cast<float>(end))
        {
            pos_      = speed_ < 0.f ? static_cast<float>(end) - 1.f : static_cast<float>(start);
            jump_env_ = 0.f;
        }
    }

    void CloseLoop()
    {
        pos_   = 0.f;
        speed_ = reverse_ ? -varispeed_ : varispeed_;
    }

    void Read(float pos, float* l, float* r) const
    {
        const size_t len = len_;
        const size_t i0  = static_cast<size_t>(pos) % len;
        const size_t i1  = (i0 + 1) % len;
        const float  f   = pos - std::floor(pos);
        *l = tape_l_[i0] + (tape_l_[i1] - tape_l_[i0]) * f;
        *r = tape_r_[i0] + (tape_r_[i1] - tape_r_[i0]) * f;
    }

    /** Overdub at integer index i. t: where i falls between the last input and this one. */
    void WriteDub(int64_t i, float t, float in_l, float in_r)
    {
        const int64_t ws  = static_cast<int64_t>(win_start_);
        const int64_t len = static_cast<int64_t>(win_end_) - ws;
        const size_t  idx = static_cast<size_t>(ws + (((i - ws) % len) + len) % len);
        const float   x_l = prev_in_l_ + (in_l - prev_in_l_) * t;
        const float   x_r = prev_in_r_ + (in_r - prev_in_r_) * t;
        const float   w_l = tape_l_[idx] * dub_ + x_l * input_env_;
        const float   w_r = tape_r_[idx] * dub_ + x_r * input_env_;
        Write(idx, Limit(w_l, lim_peak_l_), Limit(w_r, lim_peak_r_), true);
    }

    void Write(size_t idx, float l, float r, bool overdub)
    {
        tape_l_[idx] = l;
        tape_r_[idx] = r;
        auto&       peak = peaks_[idx / kPeakBin];
        const float m    = std::fmax(std::fabs(l), std::fabs(r));
        // the first sample of a bin restarts its peak, so overdubs that fade the loop show up
        const float p = (idx % kPeakBin == 0 && overdub) ? m : std::fmax(peak.load(std::memory_order_relaxed), m);
        peak.store(p, std::memory_order_relaxed);
    }

    /** TAPE's overdub limiter (Limiter::ProcessHard): keeps sound-on-sound from blowing up */
    float Limit(float x, float& peak) const
    {
        const float a   = std::fabs(x);
        const float err = a - peak;
        peak += (err > 0.f ? lim_up_ : lim_down_) * err;
        const float gain = peak <= 1.f ? 1.f : 1.f / peak;
        return fclamp(x * gain, -1.f, 1.f);
    }

    RateScale          rate_;
    std::vector<float> tape_l_, tape_r_;
    std::vector<std::atomic<float>> peaks_;
    size_t capacity_ = 0;

    State  state_   = State::Empty;
    bool   dubbing_ = false;
    size_t len_     = 0;
    float  pos_     = 0.f;

    // copies for the UI thread
    std::atomic<State>  ui_state_{State::Empty};
    std::atomic<bool>   ui_dubbing_{false};
    std::atomic<size_t> ui_len_{0};
    std::atomic<float>  ui_pos_{0.f};
    std::atomic<size_t> ui_win_start_{0}, ui_win_end_{0};

    size_t win_start_ = 0, win_end_ = 0;
    size_t win_start_target_ = 0, win_end_target_ = SIZE_MAX;
    float  jump_env_ = 1.f, last_seam_env_ = 1.f;

    float speed_ = 1.f, varispeed_ = 1.f, glide_coef_ = .01f;
    bool  reverse_ = false;
    float dub_ = 1.f, dub_target_ = 1.f, dub_coef_ = .0001f;
    float level_ = 1.f, level_target_ = 1.f, level_coef_ = .001f;
    float input_env_ = 0.f, input_step_ = .001f;
    float prev_in_l_ = 0.f, prev_in_r_ = 0.f;
    float seam_fade_ = 96.f;
    bool  clearing_ = false;
    float clear_env_ = 1.f, clear_step_ = .002f;
    bool  scrubbing_ = false;
    float scrub_target_ = 0.f, last_scrub_ = -1.f;
    float lim_up_ = .05f, lim_down_ = .0004f, lim_peak_l_ = .5f, lim_peak_r_ = .5f;
};

} // namespace prism
