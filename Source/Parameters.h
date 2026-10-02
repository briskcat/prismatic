#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace prism::ids
{
// global
inline constexpr const char* classic = "classic";
inline constexpr const char* input   = "input";
inline constexpr const char* output  = "output";
inline constexpr const char* dryWet  = "dry_wet";
// filter
inline constexpr const char* filterOn  = "filter_on";
inline constexpr const char* cutoff    = "filter_cutoff";
inline constexpr const char* resonance = "filter_res";
// drive
inline constexpr const char* driveOn = "drive_on";
inline constexpr const char* drive   = "drive";
// tape
inline constexpr const char* tapeOn      = "tape_on";
inline constexpr const char* warbleDepth = "warble_depth";
inline constexpr const char* warbleRate  = "warble_rate";
// crush
inline constexpr const char* crushOn   = "crush_on";
inline constexpr const char* crushRate = "crush_rate";
inline constexpr const char* crushBits = "crush_bits";
inline constexpr const char* crushMix  = "crush_mix";
// delay
inline constexpr const char* delayOn       = "delay_on";
inline constexpr const char* delaySync     = "delay_sync";
inline constexpr const char* delayTime     = "delay_time";
inline constexpr const char* delayDiv      = "delay_div";
inline constexpr const char* delayFeedback = "delay_feedback";
inline constexpr const char* delayMix      = "delay_mix";
inline constexpr const char* delayVari     = "delay_varispeed";
// glitch
inline constexpr const char* glitchOn       = "glitch_on";
inline constexpr const char* glitchMode     = "glitch_mode";
inline constexpr const char* glitchDiv      = "glitch_div";
inline constexpr const char* glitchRate     = "glitch_rate";
inline constexpr const char* glitchChaos    = "glitch_chaos";
inline constexpr const char* glitchFeedback = "glitch_feedback";
inline constexpr const char* glitchMix      = "glitch_mix";
inline constexpr const char* glitchFreeze   = "glitch_freeze";
inline constexpr const char* glitchRetrig   = "glitch_retrig";
inline constexpr const char* glitchReverse  = "glitch_reverse";
inline constexpr const char* glitchOctUp    = "glitch_oct_up";
inline constexpr const char* glitchOctDown  = "glitch_oct_down";
inline constexpr const char* glitchSpread   = "glitch_spread";
inline constexpr const char* glitchPattern  = "glitch_pattern";
inline constexpr const char* glitchSeed     = "glitch_seed";
// reverb
inline constexpr const char* reverbOn        = "reverb_on";
inline constexpr const char* reverbMix       = "reverb_mix";
inline constexpr const char* reverbDecay     = "reverb_decay";
inline constexpr const char* reverbTone      = "reverb_tone";
inline constexpr const char* reverbDiffusion = "reverb_diffusion";
inline constexpr const char* reverbFreeze    = "reverb_freeze";
// squash
inline constexpr const char* squashOn = "squash_on";
inline constexpr const char* squash   = "squash";
// looper
inline constexpr const char* loopRec        = "loop_rec";
inline constexpr const char* loopPlay       = "loop_play";
inline constexpr const char* loopClear      = "loop_clear";
inline constexpr const char* loopSpeed      = "loop_speed";
inline constexpr const char* loopSpeedSteps = "loop_speed_steps";
inline constexpr const char* loopReverse    = "loop_reverse";
inline constexpr const char* loopGlide      = "loop_glide";
inline constexpr const char* loopDub        = "loop_dub";
inline constexpr const char* loopLevel      = "loop_level";
inline constexpr const char* loopScrub      = "loop_scrub";
inline constexpr const char* loopQuantize   = "loop_quantize";
inline constexpr const char* loopLength     = "loop_length";
inline constexpr const char* loopSave       = "loop_save";
inline constexpr const char* loopSpeedStep  = "loop_speed_step";
inline constexpr const char* loopPos        = "loop_pos";    // where the loop window starts on the tape (0-1)
inline constexpr const char* loopLen        = "loop_len";    // how long it is (0-1 of the tape)
inline constexpr const char* loopSnap       = "loop_snap";
inline constexpr const char* loopWander     = "loop_wander"; // how far the window moves on its own
inline constexpr const char* loopMoves      = "loop_moves";  // drift, random, scan
inline constexpr const char* loopEvery      = "loop_every";  // each pass, every 1/2/4 bars
inline constexpr const char* loopSeed       = "loop_seed";
inline constexpr const char* loopLand       = "loop_land";   // changes land at the end of a pass, or right away
} // namespace prism::ids

namespace prism
{
/** Tape delay sync divisions, in beats (quarter notes) */
struct NoteDiv
{
    const char* name;
    float       beats;
};

inline constexpr NoteDiv kDelayDivs[] = {
    {"1/32", .125f},   {"1/16T", 1.f / 6.f}, {"1/16", .25f}, {"1/16.", .375f}, {"1/8T", 1.f / 3.f},
    {"1/8", .5f},      {"1/8.", .75f},       {"1/4T", 2.f / 3.f}, {"1/4", 1.f}, {"1/4.", 1.5f},
    {"1/2T", 4.f / 3.f}, {"1/2", 2.f},       {"1/2.", 3.f},  {"1 bar", 4.f},
};

/** How often the glitch delay rolls the dice, in beats */
inline constexpr NoteDiv kGlitchRates[] = {{"1/4", 1.f}, {"1/8", .5f}, {"1/16", .25f}, {"1/32", .125f}};

/** Glitch pattern lengths, in bars (0 = free random) */
inline constexpr int   kPatternBars[]   = {0, 1, 2, 4};
/** Looper fixed recording lengths, in bars (0 = free) */
inline constexpr int   kLoopBars[]      = {0, 1, 2, 4, 8};
/** Looper speed steps: octaves and fifths, like TAPE's quantised varispeed */
inline constexpr float kSpeedSteps[]    = {.25f, 1.f / 3.f, .5f, 2.f / 3.f, 1.f, 1.5f, 2.f};
inline constexpr const char* kSpeedStepNames[] = {"1/4x  -2 oct", "1/3x  -oct-5th", "1/2x  -1 oct", "2/3x  -5th",
                                                  "1x", "3/2x  +5th", "2x  +1 oct"};
/** How the loop window wanders, and how often (in bars; 0 = each pass) */
inline constexpr const char* kLoopMoves[]      = {"Drift", "Random", "Scan"};
inline constexpr const char* kLoopMoveHints[]  = {"small steps", "jumps", "creeps forward"};
inline constexpr int         kLoopEveryBars[]  = {0, 1, 2, 4};
inline constexpr const char* kLoopEveryNames[] = {"each pass", "every bar", "every 2 bars", "every 4 bars"};
/** Loop position/length snapping, in beats (0 = free) */
inline constexpr NoteDiv kLoopSnaps[] = {{"Free", 0.f}, {"1/16", .25f}, {"1/8", .5f}, {"1/4", 1.f}, {"1 bar", 4.f}};

juce::AudioProcessorValueTreeState::ParameterLayout CreateParameterLayout();
} // namespace prism
