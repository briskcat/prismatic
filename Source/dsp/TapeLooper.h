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
// - Position / Length set a window within the recorded tape. Playback and
//   overdubs loop inside it; moving it never touches the audio outside. The
//   window wraps: past the end of the tape it carries on from the start.
// - Changes to the window can wait for the end of the current pass, so every
//   pass is whole and in time. Wander moves the window on its own at pass ends
//   (drift, random or scan), seeded so the same seed takes the same path.
//
// Buttons follow the hardware:
//   REC   empty -> record; recording -> close the loop and keep overdubbing;
//         playing -> overdub on/off
//   PLAY  recording -> close the loop and play; playing -> stop; stopped -> play
//   CLEAR erase the loop

#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
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
    /** The part of the tape to loop, in samples: where it starts and how long it is. It may wrap. */
    void SetWindow(size_t start, size_t length)
    {
        home_target_ = start;
        wlen_target_ = length;
    }

    enum class Moves
    {
        Drift,
        Random,
        Scan
    };
    /** Wander: amount 0-1, how it moves, the least time between moves (samples; 0 = each pass),
        a seed, the snap unit in samples (0 = free), and whether changes wait for the end of a pass. */
    void SetMoves(float amount, Moves moves, double every, uint32_t seed, double unit, bool latch)
    {
        wander_ = fclamp(amount, 0.f, 1.f);
        moves_  = moves;
        every_  = every;
        unit_   = unit;
        latch_  = latch;
        if(seed != seed_)
        {
            seed_ = seed;
            RestartMoves();
        }
    }
    /** Start the wander from the top: same seed, same path (on transport start) */
    void RestartMoves()
    {
        move_idx_    = 0;
        wander_off_  = 0.0;
        since_move_  = 0.0;
    }

    /** 0-1 position in the loop. Moves the tape only while stopped. */
    void SetScrub(float pos)
    {
        if(std::fabs(pos - last_scrub_) > 1e-6f)
        {
            last_scrub_ = pos;
            if(state_ == State::Stopped && len_ > 0 && wlen_ > 0)
            {
                // where that is inside the window (which may wrap), held to the window
                const double d = Wrap(pos * static_cast<double>(len_) - static_cast<double>(win_start_), len_);
                const double w = static_cast<double>(wlen_);
                scrubbing_     = true;
                scrub_target_  = d < w ? d : (d - w < static_cast<double>(len_) - d ? w - 1.0 : 0.0);
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
        ui_pos_.store(len_ > 0 ? static_cast<float>(Wrap(static_cast<double>(win_start_) + off_, len_)) : 0.f,
                      std::memory_order_relaxed);
        ui_win_start_.store(win_start_, std::memory_order_relaxed);
        ui_win_len_.store(wlen_, std::memory_order_relaxed);
        ui_wander_off_.store(static_cast<int64_t>(wander_off_), std::memory_order_relaxed);
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

        UpdateWindow(false);

        // tape speed
        float target = 0.f;
        if(state_ == State::Playing)
            target = reverse_ ? -varispeed_ : varispeed_;
        else if(scrubbing_)
        {
            const float dist = static_cast<float>(scrub_target_ - off_);
            target           = fclamp(dist / rate_.Samples(2400.f), -3.f, 3.f);
            if(std::fabs(dist) < 1.f)
                scrubbing_ = false;
        }
        fonepole(speed_, target, scrubbing_ ? rate_.Coef(.01f) : glide_coef_);

        // read before writing, so overdubs aren't heard twice
        const double wlen = static_cast<double>(wlen_);
        const float  seam = static_cast<float>(std::fmin(off_, wlen - off_));
        jump_env_         = std::fmin(1.f, jump_env_ + 1.f / seam_fade_);
        last_seam_env_    = fclamp(seam / seam_fade_, 0.f, 1.f) * jump_env_;
        const float env   = last_seam_env_
                          * fclamp(std::fabs(speed_) * 8.f, 0.f, 1.f) // no DC when stopped
                          * clear_env_ * level_;
        float out_l, out_r;
        Read(Wrap(static_cast<double>(win_start_) + off_, len_), &out_l, &out_r);
        out_l *= env;
        out_r *= env;
        // after a jump, the old head fades out while the new one fades in
        if(tail_env_ > 0.f)
        {
            float t_l, t_r;
            Read(tail_pos_, &t_l, &t_r);
            const float t = tail_env_ * clear_env_ * level_ * fclamp(std::fabs(speed_) * 8.f, 0.f, 1.f);
            out_l += t_l * t;
            out_r += t_r * t;
            tail_pos_ = Wrap(tail_pos_ + speed_, len_);
            tail_env_ -= 1.f / seam_fade_;
        }

        // advance and write every sample the head passes
        const float input_target = dubbing_ && state_ == State::Playing ? 1.f : 0.f;
        input_env_ = fclamp(input_env_ + (input_target > input_env_ ? input_step_ : -input_step_), 0.f, 1.f);

        const double old = off_;
        off_ += speed_;
        const float moved = static_cast<float>(off_ - old);
        if(input_env_ > .001f && std::fabs(moved) > 1e-6f)
        {
            const int64_t a = static_cast<int64_t>(std::floor(old));
            const int64_t b = static_cast<int64_t>(std::floor(off_));
            // The record head trails the playhead by a couple of samples (as on
            // the hardware), so the playhead never reads what was just written.
            constexpr int64_t kLag = 2;
            if(b > a)
                for(int64_t i = a + 1; i <= b; ++i)
                    WriteDub(i - kLag, static_cast<float>((static_cast<double>(i) - old) / moved), in_l, in_r);
            else
                for(int64_t i = a; i > b; --i)
                    WriteDub(i + kLag, static_cast<float>((static_cast<double>(i) - old) / moved), in_l, in_r);
        }
        if(state_ == State::Playing)
            since_move_ += 1.0;
        // the end of a pass: the time for moves, and for changes that were waiting
        if(off_ >= wlen || off_ < 0.0)
        {
            off_ = off_ >= wlen ? off_ - wlen : off_ + wlen;
            if(state_ == State::Playing)
                Move();
            UpdateWindow(true);
        }

        prev_in_l_ = in_l;
        prev_in_r_ = in_r;

        *l = in_l + out_l;
        *r = in_r + out_r;
    }

  public:
    // ---- for the UI (read from any thread) ----
    State  GetState() const { return ui_state_.load(std::memory_order_relaxed); }
    /** The window that's playing now (it may wrap past the end of the tape) */
    size_t GetWindowStart() const { return ui_win_start_.load(std::memory_order_relaxed); }
    size_t GetWindowLength() const { return ui_win_len_.load(std::memory_order_relaxed); }
    /** How far wander has moved the window from its set position, in samples */
    int64_t GetWanderOffset() const { return ui_wander_off_.load(std::memory_order_relaxed); }
    /** Where the window started on earlier passes, most recent first (-1 = none yet) */
    int64_t GetTrail(int i) const { return trail_[static_cast<size_t>(i)].load(std::memory_order_relaxed); }
    static constexpr int kTrail = 3;
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
        win_start_ = 0;
        wlen_      = 0;
        off_       = 0.0;
    }

  private:
    void ResetTape()
    {
        state_   = State::Empty;
        dubbing_ = false;
        len_     = 0;
        off_     = 0.0;
        win_start_ = 0;
        wlen_      = 0;
        RestartMoves();
        for(auto& t : trail_)
            t.store(-1, std::memory_order_relaxed);
        speed_   = 1.f;
        input_env_ = 0.f;
        clearing_  = false;
        clear_env_ = 1.f;
        scrubbing_ = false;
        lim_peak_l_ = lim_peak_r_ = .5f;
        for(auto& p : peaks_)
            p.store(0.f, std::memory_order_relaxed);
    }

    static double Wrap(double x, size_t len)
    {
        const double l = static_cast<double>(len);
        x              = std::fmod(x, l);
        return x < 0.0 ? x + l : x;
    }

    /** Applies the requested window: right away, or (when latched and playing) only at the end of a pass.
        at_seam: the playhead has just wrapped. */
    void UpdateWindow(bool at_seam)
    {
        constexpr size_t kMin  = 64;
        const size_t     wlen  = std::clamp(wlen_target_, std::min(kMin, len_), len_);
        const size_t     start = static_cast<size_t>(Wrap(static_cast<double>(home_target_) + wander_off_, len_));
        if(start == win_start_ && wlen == wlen_)
            return;
        if(wlen_ == 0) // first time: just take it
        {
            win_start_ = start;
            wlen_      = wlen;
            off_       = 0.0;
            return;
        }
        if(latch_ && state_ == State::Playing && !at_seam)
            return; // waits for the end of this pass

        if(at_seam)
        {
            // a fresh pass in the new window; the seam fade already covers the jump
            if(start != win_start_)
            {
                for(int i = kTrail - 1; i > 0; --i)
                    trail_[static_cast<size_t>(i)].store(trail_[static_cast<size_t>(i - 1)].load(std::memory_order_relaxed),
                                                         std::memory_order_relaxed);
                trail_[0].store(static_cast<int64_t>(win_start_), std::memory_order_relaxed);
            }
            if(speed_ < 0.f)
                off_ = static_cast<double>(wlen) - (static_cast<double>(wlen_) - off_);
            off_ = std::clamp(off_, 0.0, static_cast<double>(wlen) - 1e-3);
        }
        else
        {
            // keep playing from the same spot on the tape if it's still inside; otherwise jump in with a fade
            const double abs_pos = Wrap(static_cast<double>(win_start_) + off_, len_);
            const double d       = Wrap(abs_pos - static_cast<double>(start), len_);
            jump_env_            = std::fmin(jump_env_, last_seam_env_); // ramp from the old seam fade, don't jump
            if(d < static_cast<double>(wlen))
                off_ = d;
            else
            {
                tail_pos_ = abs_pos; // crossfade from where the head was
                tail_env_ = last_seam_env_;
                off_      = speed_ < 0.f ? static_cast<double>(wlen) - 1.0 : 0.0;
                jump_env_ = 0.f;
            }
        }
        win_start_ = start;
        wlen_      = wlen;
    }

    /** At the end of a pass: maybe move the window, as the wander settings say */
    void Move()
    {
        if(wander_ <= 0.f)
        {
            wander_off_ = 0.0; // back home
            since_move_ = 0.0;
            return;
        }
        if(since_move_ + .5 < every_)
            return;
        since_move_ = 0.0;
        // the same dice for the same seed and move number, so a path repeats
        uint32_t h = seed_ * 0x9E3779B9u ^ static_cast<uint32_t>(move_idx_++) * 0x85EBCA6Bu;
        h ^= h >> 16;
        Rng         dice(h | 1u);
        const float u    = dice.Uniform();
        const double tape = static_cast<double>(len_), wlen = static_cast<double>(std::min(wlen_target_, len_));
        switch(moves_)
        {
            case Moves::Drift: // small random steps, never further than the wander range from home
                wander_off_ = std::clamp(wander_off_ + (u * 2.0 - 1.0) * wander_ * wlen, -wander_ * tape, wander_ * tape);
                break;
            case Moves::Random: // anywhere within the range ahead of home (100% = anywhere)
                wander_off_ = u * wander_ * tape;
                break;
            case Moves::Scan: // creep forward a step each time (100% = a whole window)
                wander_off_ = std::fmod(wander_off_ + wander_ * wlen, tape);
                break;
        }
        if(unit_ > 0.0)
            wander_off_ = std::round(wander_off_ / unit_) * unit_;
    }

    void CloseLoop()
    {
        off_       = 0.0;
        win_start_ = 0;
        wlen_      = 0; // the next window update takes the whole new loop (or the set window)
        RestartMoves();
        speed_ = reverse_ ? -varispeed_ : varispeed_;
    }

    void Read(double pos, float* l, float* r) const
    {
        const size_t len = len_;
        const size_t i0  = static_cast<size_t>(pos) % len;
        const size_t i1  = (i0 + 1) % len;
        const float  f   = static_cast<float>(pos - std::floor(pos));
        *l = tape_l_[i0] + (tape_l_[i1] - tape_l_[i0]) * f;
        *r = tape_r_[i0] + (tape_r_[i1] - tape_r_[i0]) * f;
    }

    /** Overdub at integer index i. t: where i falls between the last input and this one. */
    void WriteDub(int64_t i, float t, float in_l, float in_r)
    {
        // i is an offset in the window; the window may wrap past the end of the tape
        const int64_t wlen = static_cast<int64_t>(wlen_);
        const int64_t in_w = ((i % wlen) + wlen) % wlen;
        const size_t  idx  = (win_start_ + static_cast<size_t>(in_w)) % len_;
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
    double off_     = 0.0; // playhead, as an offset into the window

    // copies for the UI thread
    std::atomic<State>  ui_state_{State::Empty};
    std::atomic<bool>   ui_dubbing_{false};
    std::atomic<size_t> ui_len_{0};
    std::atomic<float>  ui_pos_{0.f};
    std::atomic<size_t>  ui_win_start_{0}, ui_win_len_{0};
    std::atomic<int64_t> ui_wander_off_{0};
    std::array<std::atomic<int64_t>, kTrail> trail_{};

    size_t win_start_ = 0, wlen_ = 0;
    size_t home_target_ = 0, wlen_target_ = SIZE_MAX;

    // wander
    float    wander_ = 0.f;
    Moves    moves_  = Moves::Drift;
    double   every_ = 0.0, unit_ = 0.0, wander_off_ = 0.0, since_move_ = 0.0;
    uint32_t seed_ = 1, move_idx_ = 0;
    bool     latch_ = true;
    float  jump_env_ = 1.f, last_seam_env_ = 1.f;
    double tail_pos_ = 0.0;
    float  tail_env_ = 0.f;

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
    double scrub_target_ = 0.0;
    float  last_scrub_   = -1.f;
    float lim_up_ = .05f, lim_down_ = .0004f, lim_peak_l_ = .5f, lim_peak_r_ = .5f;
};

} // namespace prism
