// Hand-drawn diagrams of what each effect is doing, drawn from its controls.
// Thin ink lines in the current theme; each redraws only when its inputs change.

#pragma once
#include <functional>
#include "../Parameters.h"
#include "../dsp/DspUtil.h"
#include "Theme.h"

namespace prism::ui
{
// ---------------------------------------------------------------- sketches

namespace sketch
{
// drawing helpers: thin, round-capped ink lines and light fills, in the current theme
enum class Tone
{
    Light,
    Half,
    Dark
};
inline void Pen(juce::Graphics& g, const juce::Path& p, float thickness = 1.6f, juce::Colour c = Ink())
{
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(juce::jmax(1.f, thickness * .65f), juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded));
}
inline void Dither(juce::Graphics& g, const juce::Path& p, Tone t)
{
    g.setColour(Ink().withAlpha(t == Tone::Light ? .1f : t == Tone::Half ? .18f : .3f));
    g.fillPath(p);
}
inline juce::Rectangle<float> Snap(juce::Rectangle<float> r) { return r; }

using R = juce::Rectangle<float>;
inline juce::Colour Faint() { return Muted(); }

/** Pack a few values into a change-detection key (64 steps each is plenty for a drawing) */
inline uint32_t Key(std::initializer_list<float> vals)
{
    uint32_t h = 2166136261u;
    for(float v : vals)
        h = (h ^ (uint32_t)juce::roundToInt(v * 64.f)) * 16777619u;
    return h;
}

/** A curve y(x) for x in 0-1, with y = 0 at the bottom of r and 1 at the top */
inline juce::Path Curve(R r, int n, const std::function<float(float)>& y)
{
    juce::Path p;
    for(int i = 0; i <= n; ++i)
    {
        const float x  = (float)i / (float)n;
        const auto  pt = juce::Point<float>(r.getX() + x * r.getWidth(), r.getBottom() - juce::jlimit(0.f, 1.f, y(x)) * r.getHeight());
        if(i == 0)
            p.startNewSubPath(pt);
        else
            p.lineTo(pt);
    }
    return p;
}

inline void Dashed(juce::Graphics& g, const juce::Path& p, juce::Colour c, float thick = 1.2f)
{
    juce::Path   d;
    const float  dash[] = {3.f, 3.f};
    juce::PathStrokeType(thick).createDashedStroke(d, p, dash, 2);
    g.setColour(c);
    g.fillPath(d);
}

inline void Note(juce::Graphics&, R, const juce::String&, juce::Justification = juce::Justification::bottomRight)
{
    // captions are left out in this look: the header line is too short for them
}

/** Shading under a curve, as dither: denser for smaller gaps and darker alphas */
inline void Hatch(juce::Graphics& g, R r, float gap, const std::function<float(float)>& y, uint32_t, float alpha = .45f)
{
    juce::Path area;
    area.startNewSubPath(r.getX(), r.getBottom());
    for(int i = 0; i <= 60; ++i)
    {
        const float x = (float)i / 60.f;
        area.lineTo(r.getX() + x * r.getWidth(), r.getBottom() - juce::jlimit(0.f, 1.f, y(x)) * r.getHeight());
    }
    area.lineTo(r.getRight(), r.getBottom());
    area.closeSubPath();
    const int density = (gap < 4.5f ? 1 : 0) + (alpha > .5f ? 1 : 0);
    Dither(g, area, density == 0 ? Tone::Light : density == 1 ? Tone::Half : Tone::Dark);
}

// filter: the response curve, shaded underneath, with a marker at the cutoff
inline void Filter(juce::Graphics& g, R r, float c, float res)
{
    const bool  open = std::abs(c - .5f) < .01f, lp = c < .5f;
    const float fc   = lp ? .06f + c * 1.8f : (c - .5f) * 1.8f + .06f;
    const float bump = res * .38f;
    const auto  y    = [&](float x) {
        x = .02f + x * .98f;
        if(open)
            return .55f;
        const float base = lp ? 1.f / (1.f + std::pow(x / fc, 4.f)) : 1.f / (1.f + std::pow(fc / x, 4.f));
        return .55f * base + bump * std::exp(-std::pow((x - fc) / .06f, 2.f));
    };
    auto plot = r.withTrimmedBottom(1.f);
    Hatch(g, plot, 5.f, y, 30u, .35f);
    Pen(g, Curve(plot, 60, y), 1.8f);
    if(!open)
    {
        juce::Path m;
        const float mx = plot.getX() + (fc - .02f) / .98f * plot.getWidth();
        m.startNewSubPath(mx, plot.getY());
        m.lineTo(mx, plot.getBottom());
        Dashed(g, m, Faint());
    }
    Note(g, r, open ? "open" : lp ? "low-pass" : "high-pass");
}

// drive: the clean wave (dashed) and what comes out
inline void Drive(juce::Graphics& g, R r, float d)
{
    const float k    = 1.f + d * 12.f;
    auto        plot = r.withTrimmedBottom(1.f);
    Dashed(g, Curve(plot, 60, [](float x) { return .5f + .48f * std::sin(x * kTwoPi * 1.5f); }), Faint());
    Pen(g, Curve(plot, 80, [&](float x) { return .5f + .48f * std::tanh(k * std::sin(x * kTwoPi * 1.5f)) / std::tanh(k); }), 1.8f);
    Note(g, r, d < .05f ? "clean" : d < .5f ? "warm" : "fuzz");
}

// tape: two reels; the tape wobbles as deep and as often as the warble
inline void Tape(juce::Graphics& g, R r, float depth, float rate)
{
    const float rr = juce::jmin(11.f, r.getHeight() * .3f);
    const juce::Point<float> a(r.getX() + rr + 2.f, r.getY() + rr + 2.f), b(r.getRight() - rr - 2.f, r.getY() + rr + 2.f);
    for(auto c : {a, b})
    {
        juce::Path reel;
        reel.addEllipse(c.x - rr, c.y - rr, rr * 2.f, rr * 2.f);
        reel.addEllipse(c.x - 2.f, c.y - 2.f, 4.f, 4.f);
        for(int k = 0; k < 3; ++k)
        {
            const float ang = k * kTwoPi / 3.f;
            reel.startNewSubPath(c.x + 3.f * std::sin(ang), c.y - 3.f * std::cos(ang));
            reel.lineTo(c.x + (rr - 2.f) * std::sin(ang), c.y - (rr - 2.f) * std::cos(ang));
        }
        Pen(g, reel, 1.5f);
    }
    const float cycles = 1.f + rate * 7.f;
    R           tape(a.x, r.getBottom() - 12.f, b.x - a.x, 12.f);
    Pen(g, Curve(tape, 80, [&](float x) { return .5f + .5f * depth * std::sin(x * kTwoPi * cycles); }), 1.6f);
}

// crush: the original wave (dashed) and the stepped version, blended by mix
inline void Crush(juce::Graphics& g, R r, float rateHz, float bits, float mix)
{
    auto        plot   = r.withTrimmedBottom(1.f);
    const int   steps  = juce::jlimit(5, 60, (int)(rateHz / 48000.f * 70.f) + 5);
    const float levels = juce::jlimit(2.f, 16.f, std::pow(2.f, bits - 1.f) / 256.f + 2.f);
    const auto  wave   = [](float x) { return .5f + .45f * std::sin(x * kTwoPi); };
    Dashed(g, Curve(plot, 60, wave), Faint());
    juce::Path p;
    for(int i = 0; i < steps; ++i)
    {
        const float x0 = (float)i / steps, x1 = (float)(i + 1) / steps;
        const float q  = std::round(wave(x0) * levels) / levels;
        const float v  = q * mix + wave(x0) * (1.f - mix);
        const float px0 = plot.getX() + x0 * plot.getWidth(), px1 = plot.getX() + x1 * plot.getWidth();
        const float py  = plot.getBottom() - v * plot.getHeight();
        if(i == 0)
            p.startNewSubPath(px0, py);
        else
            p.lineTo(px0, py);
        p.lineTo(px1, py);
    }
    Pen(g, p, 1.8f);
    Note(g, r, juce::String(juce::roundToInt(bits)) + " bit");
}

// squash: loud peaks (dashed) pulled down to an even level
inline void Squash(juce::Graphics& g, R r, float amt)
{
    auto        plot = r.withTrimmedBottom(1.f);
    const float peaks[] = {.9f, .35f, .7f, .25f, .95f, .5f, .3f, .8f};
    juce::Path  in, out;
    const int   n = 8;
    for(int i = 0; i < n; ++i)
    {
        const float x  = plot.getX() + (i + .5f) * plot.getWidth() / n;
        const float pk = peaks[i];
        const float sq = pk + (std::pow(pk, .25f) * .6f - pk) * amt; // louder peaks come down more
        in.startNewSubPath(x, plot.getBottom());
        in.lineTo(x, plot.getBottom() - pk * plot.getHeight());
        out.startNewSubPath(x + 3.f, plot.getBottom());
        out.lineTo(x + 3.f, plot.getBottom() - sq * plot.getHeight());
    }
    Dashed(g, in, Faint());
    Pen(g, out, 2.4f);
    Note(g, r, amt < .05f ? "gentle" : amt < .6f ? "squashed" : "flattened");
}

// delay: the hit and its echoes, spaced by the time, bouncing left (up) and right (down)
inline void Delay(juce::Graphics& g, R r, float timeNorm, float fb, float mix, bool vari, bool fb100)
{
    auto        plot  = r.withTrimmedBottom(1.f);
    const float mid   = plot.getCentreY();
    const float space = juce::jlimit(6.f, plot.getWidth() / 3.f, (8.f + timeNorm * 60.f));
    juce::Path  dry, wet;
    dry.startNewSubPath(plot.getX() + 3.f, mid);
    dry.lineTo(plot.getX() + 3.f, mid - plot.getHeight() * .5f * (1.f - mix * .5f));
    float level = .55f + .45f * mix; // first echo height follows the mix
    int   tap   = 0;
    for(float x = plot.getX() + 3.f + space; x < plot.getRight() - 2.f; x += space, ++tap)
    {
        const float h = plot.getHeight() * .48f * juce::jmax(.1f, level);
        wet.startNewSubPath(x, mid);
        wet.lineTo(x, (tap % 2 == 0) ? mid - h : mid + h);
        if(tap % 2 == 1)
            level *= juce::jmax(.05f, std::pow(fb, .7f) * (fb100 ? 1.f : .9f));
    }
    juce::Path axis;
    axis.startNewSubPath(plot.getX(), mid);
    axis.lineTo(plot.getRight(), mid);
    Dashed(g, axis, Faint(), 1.f);
    Pen(g, dry, 2.6f, Ink().withAlpha(.45f));
    Pen(g, wet, 2.4f);
    Note(g, r, vari ? "tape loop" : "classic", juce::Justification::bottomRight);
}

// reverb: the first hit, then a tail as long as the decay, as dense as the diffusion,
// darker hatching for a darker tone; freeze holds it forever
inline void Reverb(juce::Graphics& g, R r, float mix, float decay, float tone, float diffusion, bool freeze)
{
    auto        plot = r.withTrimmedBottom(1.f);
    const float len  = .12f + decay * .85f;
    const auto  env  = [&](float x) {
        if(freeze)
            return .55f * (.3f + .7f * mix);
        return (.3f + .7f * mix) * std::exp(-x / (len * .45f));
    };
    juce::Path hit;
    hit.startNewSubPath(plot.getX() + 2.f, plot.getBottom());
    hit.lineTo(plot.getX() + 2.f, plot.getY());
    Pen(g, hit, 2.2f, Ink().withAlpha(.5f));
    Hatch(g, plot.withTrimmedLeft(6.f), 7.f - diffusion * 4.5f, env, 18u, .25f + .55f * (1.f - tone));
    Pen(g, Curve(plot.withTrimmedLeft(6.f), 50, env), 1.5f);
    Note(g, r, freeze ? "frozen" : juce::String(juce::roundToInt(decay * 100.f)) + "% decay");
}

/** The same dice the glitch delay rolls (GlitchDelay::StepSeed), so the preview matches the sound */
inline uint32_t StepSeed(uint32_t seed, int64_t step, int64_t steps)
{
    const int64_t pos = ((step % steps) + steps) % steps;
    uint32_t      h   = seed * 0x9E3779B9u ^ static_cast<uint32_t>(pos) * 0x85EBCA6Bu;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    return h | 1u;
}

// glitch: one cell per dice roll in the pattern; the glitches are drawn as what they'll do
inline void Glitch(juce::Graphics& g, R r, float chaos, bool shimmer, int seed, float spread, int patternBars, int rateIdx,
            unsigned mask, bool freeze)
{
    auto          plot  = r.withTrimmedBottom(1.f);
    const float   beats = kGlitchRates[juce::jlimit(0, (int)std::size(kGlitchRates) - 1, rateIdx)].beats;
    const int     steps = patternBars > 0 ? juce::roundToInt(patternBars * 4 / beats) : 8;
    const int     cells = juce::jlimit(4, juce::jmax(4, (int)(plot.getWidth() / 14.f)), steps);
    const float   cw    = juce::jmin(34.f, plot.getWidth() / (float)cells); // one dice roll per cell
    for(int i = 0; i < cells; ++i)
    {
        // free mode isn't repeatable, so show an example roll
        prism::Rng  dice(StepSeed(patternBars > 0 ? (uint32_t)seed : 977u, i, steps));
        const float roll = dice.Uniform();
        const float pan  = dice.Uniform() * 2.f - 1.f;
        const uint32_t pick = dice.Next();
        int  ev = -1;
        if(shimmer)
            ev = 2;
        else
        {
            int enabled[4], n = 0;
            for(int e = 0; e < 4; ++e)
                if(mask & (1u << e))
                    enabled[n++] = e;
            if(n > 0)
                ev = enabled[pick % (uint32_t)n];
        }
        const bool hit = roll < .5f * chaos && ev >= 0;
        R          cell(plot.getX() + i * cw + 1.f, plot.getY() + 4.f, cw - 3.f, plot.getHeight() - 8.f);
        if(!hit)
        {
            juce::Path box;
            box.addRectangle(Snap(cell.reduced(0.f, cell.getHeight() * .25f)));
            Pen(g, box, 1.5f);
            continue;
        }
        cell = Snap(cell.translated(0.f, pan * spread * cell.getHeight() * .25f));
        g.setColour(Ink());
        g.fillRect(cell);
        // what this glitch does: retrigger, reverse, octave up, octave down
        juce::Path sym;
        const auto c = cell.getCentre();
        const float s = juce::jmin(cell.getWidth(), cell.getHeight()) * .28f;
        if(ev == 0)
        {
            sym.startNewSubPath(c.x - s, c.y - s);
            sym.lineTo(c.x - s, c.y + s);
            sym.startNewSubPath(c.x, c.y - s);
            sym.lineTo(c.x, c.y + s);
            sym.startNewSubPath(c.x + s, c.y - s);
            sym.lineTo(c.x + s, c.y + s);
        }
        else if(ev == 1)
        {
            sym.startNewSubPath(c.x + s, c.y);
            sym.lineTo(c.x - s, c.y);
            sym.startNewSubPath(c.x - s * .2f, c.y - s * .7f);
            sym.lineTo(c.x - s, c.y);
            sym.lineTo(c.x - s * .2f, c.y + s * .7f);
        }
        else
        {
            const float dir = ev == 2 ? -1.f : 1.f;
            sym.startNewSubPath(c.x, c.y - dir * s);
            sym.lineTo(c.x, c.y + dir * s);
            sym.startNewSubPath(c.x - s * .7f, c.y + dir * s * .2f);
            sym.lineTo(c.x, c.y + dir * s);
            sym.lineTo(c.x + s * .7f, c.y + dir * s * .2f);
        }
        g.setColour(Bg());
        g.strokePath(sym, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if(freeze)
    {
        juce::Path loop;
        loop.addRectangle(plot.reduced(0.f, 1.f));
        Dashed(g, loop, Ink(), 1.4f);
    }
    juce::String what = freeze ? "frozen" : patternBars > 0 ? "repeats every " + juce::String(patternBars) + (patternBars == 1 ? " bar" : " bars")
                                                           : "free: new rolls every time";
    Note(g, r, what);
}

/** Everything the looper sketch shows */
struct DeckState
{
    float speed = 1.f, glide = .2f, dub = 1.f, level = 1.f, pos = 0.f; // pos: 0-1 through the loop
    bool  reverse = false, recording = false, dubbing = false, playing = false, hasLoop = false, steps = false;
    int   recBars = 0, quantize = 0, stepIdx = 4;
};

// looper: dub keep as stacked overdub passes, newest on top. Older passes fade unless dub keep is high,
// and the newest turns red while recording or overdubbing.
inline void Deck(juce::Graphics& g, R r, const DeckState& d)
{
    const auto plot = r.withTrimmedBottom(1.f).withWidth(juce::jmin(r.getWidth(), 40.f));
    for(int k = 0; k < 4; ++k)
    {
        juce::Path  layer;
        const float ly = plot.getY() + 3.f + k * (plot.getHeight() - 6.f) / 3.f;
        layer.startNewSubPath(plot.getX(), ly);
        layer.lineTo(plot.getRight(), ly);
        const auto ink = k == 0 && (d.recording || d.dubbing) ? Red() : Ink();
        Pen(g, layer, 2.2f, ink.withAlpha(juce::jmax(.08f, std::pow(d.dub, (float)k * 1.5f))));
    }
}
} // namespace sketch
} // namespace prism::ui
