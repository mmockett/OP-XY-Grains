// Grains' own checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
#include "../sample.cpp"
#include <cmath>
#include <cstdio>

namespace {
using opxy::i32;
using opxy::u32;
const u32 c4 = 25480119u, frames = 44100;
opxy::i16 sine[frames], square[frames], soft[frames], level16[frames];
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}
// A control at 0..1.
i32 at(double value) { return i32(value * 2147483647.0); }
// The middle of choice n of an enum with count choices.
i32 choice(u32 n, u32 count) { return at((n + 0.5) / count); }

struct Rig {
    i32 out[2 * opxy::block_frames];
    i32 parameters[8];
    u32 pitch[opxy::block_frames];
    opxy::SampleBlock block;
    grains::Voice voice;
    Rig(const opxy::i16* data, u32 count) {
        // definition.json's defaults.
        const double defaults[8] = {0, 0.853553, 0.392468, 0.157895, 0, 0.071429, 0.5, 0.083333};
        for (u32 i = 0; i < 8; ++i) parameters[i] = at(defaults[i]);
        for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = c4;
        block = opxy::SampleBlock();
        block.output = out; block.parameters = parameters; block.pitch = pitch;
        block.region.begin = 0; block.region.end = count; block.region.level = opxy::sample_unity_level;
        block.transport.bpm_x10 = 1200;
        block.output_channels = 2;
        const opxy::SampleView view = {data, count, 44100, 1};
        grains::initialize(voice, view);
        grains::start(voice);
    }
    // Renders blocks; returns the peak magnitude.
    i32 run(u32 blocks) {
        i32 peak = 0;
        for (u32 b = 0; b < blocks; ++b) {
            grains::render_sample(voice, block);
            for (u32 i = 0; i < 2 * opxy::block_frames; ++i) peak = out[i] < 0 ? (-out[i] > peak ? -out[i] : peak) : (out[i] > peak ? out[i] : peak);
        }
        return peak;
    }
};
}

int main() {
    for (u32 i = 0; i < frames; ++i) {
        sine[i] = opxy::i16(32767 * std::sin(2 * M_PI * 220.0 * i / 44100));
        square[i] = (i / 50) & 1 ? 32767 : -32768;
        soft[i] = opxy::i16(sine[i] / 8);   // well below the output ceiling
        level16[i] = 16384;                  // a constant: the output is the window
    }

    {   // No sample: silence.
        Rig r(sine, 0);
        check(r.run(20) == 0, "silence without a sample");
    }
    {   // The defaults sound, within half scale.
        Rig r(sine, frames);
        const i32 peak = r.run(200);
        check(peak > 0x02000000, "the defaults sound");
        check(peak <= 0x40000000, "the defaults stay within half scale");
    }
    {   // Every chord and mode, dense and wide, on a full-scale square: within half scale.
        for (u32 chord = 0; chord < 7; ++chord)
            for (u32 mode = 0; mode < grains::mode_count; ++mode) {
                Rig r(square, frames);
                r.parameters[grains::kChrd] = choice(chord, 7);
                r.parameters[grains::kMode] = choice(mode, grains::mode_count);
                r.parameters[grains::kDens] = at(1);
                r.parameters[grains::kSpry] = at(1);
                const i32 peak = r.run(100);
                check(peak > 0 && peak <= 0x40000000, "every chord and mode stays within half scale");
            }
    }
    {   // Frozen with no spray: every grain starts at pos.
        Rig r(sine, frames);
        r.parameters[grains::kScan] = at(0.5);
        r.parameters[grains::kPos] = at(0.25);
        r.run(50);
        const grains::Settings s = grains::decode(r.voice, r.block);
        check(r.voice.scan == 0, "scan 0 freezes the read point");
        for (u32 n = 0; n < 4; ++n) {
            for (u32 k = 0; k < grains::grain_count; ++k) r.voice.grains[k].active = false;
            grains::spawn(r.voice, s);
            check(r.voice.grains[0].frame == s.pos, "a frozen grain starts at pos");
        }
    }
    {   // scan at its default plays through the sample at real time.
        Rig r(sine, frames);
        r.run(100);
        const i32 expected = 100 * opxy::block_frames;
        check(r.voice.scan > expected - 200 && r.voice.scan < expected + 200, "scan's default moves at real time");
    }
    {   // chrd min: grain n plays the chord's note n.
        Rig r(sine, frames);
        r.parameters[grains::kChrd] = choice(5, 7);
        const grains::Settings s = grains::decode(r.voice, r.block);
        const int steps[7] = {0, 3, 7, 12, 15, 19, 24};
        for (u32 n = 0; n < 7; ++n) {
            for (u32 k = 0; k < grains::grain_count; ++k) r.voice.grains[k].active = false;
            grains::spawn(r.voice, s);
            const double expected = 65536.0 * std::pow(2.0, steps[n] / 12.0);
            check(std::fabs(r.voice.grains[0].step - expected) < expected * 0.001, "chrd min plays its notes in turn");
        }
    }
    {   // The played note's pitch: an octave up doubles each grain's step.
        Rig r(sine, frames);
        for (u32 i = 0; i < opxy::block_frames; ++i) r.pitch[i] = 2 * c4;
        const grains::Settings s = grains::decode(r.voice, r.block);
        grains::spawn(r.voice, s);
        check(r.voice.grains[0].step > 131000 && r.voice.grains[0].step < 131200, "an octave up doubles the step");
    }
    {   // The sample's root: a sample rooted on A3 (pitch_cents +300) plays as recorded
        // on A3, a semitone faster on B flat 3, and at the sample's own rate.
        Rig r(sine, frames);
        r.block.region.pitch_cents = 300;
        for (u32 i = 0; i < opxy::block_frames; ++i) r.pitch[i] = u32(c4 * std::pow(2.0, -3 / 12.0) + 0.5);
        grains::spawn(r.voice, grains::decode(r.voice, r.block));
        check(r.voice.grains[0].step > 65400 && r.voice.grains[0].step < 65670, "a sample plays as recorded on its root");
        for (u32 i = 0; i < opxy::block_frames; ++i) r.pitch[i] = u32(c4 * std::pow(2.0, -2 / 12.0) + 0.5);
        for (u32 k = 0; k < grains::grain_count; ++k) r.voice.grains[k].active = false;
        grains::spawn(r.voice, grains::decode(r.voice, r.block));
        const double semitone = 65536 * std::pow(2.0, 1 / 12.0);
        check(std::fabs(r.voice.grains[0].step - semitone) < semitone * 0.002, "a semitone up plays a semitone faster");
        r.voice.sample.rate = 22050;   // a sample at half the rate plays at half the step
        for (u32 k = 0; k < grains::grain_count; ++k) r.voice.grains[k].active = false;
        grains::spawn(r.voice, grains::decode(r.voice, r.block));
        check(std::fabs(r.voice.grains[0].step - semitone / 2) < semitone * 0.002, "a sample's own rate sets its speed");
    }
    {   // The sample's volume: half its level halves the output.
        Rig loud(soft, frames), quiet(soft, frames);
        quiet.block.region.level = opxy::sample_unity_level / 2;
        const i32 a = loud.run(100), b = quiet.run(100);
        check(b > a / 2 - a / 50 && b < a / 2 + a / 50, "the sample's volume scales the output");
    }
    {   // sync at 120 BPM: dens picks a half note (44100 samples) down to a 1/32 (2756).
        Rig r(sine, frames);
        r.parameters[grains::kMode] = choice(1, grains::mode_count);
        r.parameters[grains::kDens] = at(0);
        check(grains::interval(r.voice, grains::decode(r.voice, r.block), r.block) == 44100, "sync's longest division is a half note");
        r.parameters[grains::kDens] = at(1);
        check(grains::interval(r.voice, grains::decode(r.voice, r.block), r.block) == 2756, "sync's shortest is a 1/32 note");
    }
    {   // osc: grains evenly spaced, size over the overlap.
        Rig r(sine, frames);
        r.parameters[grains::kMode] = choice(2, grains::mode_count);
        r.parameters[grains::kDens] = at(1);   // 8 at once
        const grains::Settings s = grains::decode(r.voice, r.block);
        check(grains::interval(r.voice, s, r.block) == s.length * 256 / s.overlap8, "osc spaces grains evenly");
    }
    {   // shpe: a pluck peaks early, a swell late.
        Rig r(sine, frames);
        r.parameters[grains::kShpe] = at(1);
        grains::spawn(r.voice, grains::decode(r.voice, r.block));
        check(r.voice.grains[0].peak * 10 < r.voice.grains[0].length, "shpe 1 peaks early");
        r.parameters[grains::kShpe] = at(0);
        for (u32 k = 0; k < grains::grain_count; ++k) r.voice.grains[k].active = false;
        grains::spawn(r.voice, grains::decode(r.voice, r.block));
        check(r.voice.grains[0].peak * 10 > 9 * r.voice.grains[0].length, "shpe 0 peaks late");
    }
    {   // size: 10 ms at 0, 2 s at the top.
        Rig r(sine, frames);
        r.parameters[grains::kSize] = at(0);
        check(grains::decode(r.voice, r.block).length == 441, "size starts at 10 ms");
        r.parameters[grains::kSize] = at(1);
        const u32 longest = grains::decode(r.voice, r.block).length;
        check(longest > 88000 && longest <= 88200, "size ends at 2 s");
    }
    {   // spry in stages, over 400 grains at each setting.
        struct Spread { u32 reversed, min_pan, max_pan; double low_step, high_step; u32 short_len, long_len; };
        auto spread = [&](double spry, u32 mode) {
            Rig r(sine, frames);
            r.parameters[grains::kSpry] = at(spry);
            r.parameters[grains::kMode] = choice(mode, grains::mode_count);
            const grains::Settings s = grains::decode(r.voice, r.block);
            Spread out = {0, 16, 0, 1e9, 0, ~0u, 0};
            for (u32 n = 0; n < 400; ++n) {
                for (u32 k = 0; k < grains::grain_count; ++k) r.voice.grains[k].active = false;
                r.voice.spawned = 0;   // chrd off: every grain on the root
                grains::spawn(r.voice, s);
                const grains::Grain& g = r.voice.grains[0];
                out.reversed += g.reverse;
                out.min_pan = g.pan < out.min_pan ? g.pan : out.min_pan;
                out.max_pan = g.pan > out.max_pan ? g.pan : out.max_pan;
                out.low_step = g.step < out.low_step ? g.step : out.low_step;
                out.high_step = g.step > out.high_step ? g.step : out.high_step;
                out.short_len = g.length < out.short_len ? g.length : out.short_len;
                out.long_len = g.length > out.long_len ? g.length : out.long_len;
            }
            return out;
        };
        const double root = 65536, cents50 = std::pow(2.0, 50 / 1200.0);
        const u32 length = grains::decode(Rig(sine, frames).voice, Rig(sine, frames).block).length;
        Spread a = spread(0, 0);   // none: forward, centred, on pitch, one length
        check(a.reversed == 0 && a.min_pan == 8 && a.max_pan == 8, "spry 0 keeps grains forward and centred");
        check(a.low_step == a.high_step && std::fabs(a.low_step - root) < 2 && a.short_len == length && a.long_len == length,
              "spry 0 keeps every grain's pitch and length");
        a = spread(0, 3);          // rcloud: every grain backwards
        check(a.reversed == 400, "a reversed mode plays every grain backwards");
        a = spread(0.2, 0);        // under a quarter: some spread, nothing reversed, on pitch
        check(a.reversed == 0 && a.min_pan < 8 && a.max_pan > 8 && a.max_pan < 16, "low spry spreads grains but keeps them forward");
        check(a.low_step == a.high_step, "low spry keeps the pitch");
        a = spread(0.5, 0);        // the middle: full width, a quarter reversed, still on pitch
        check(a.min_pan == 0 && a.max_pan == 16, "spry reaches full width by the middle");
        check(a.reversed > 60 && a.reversed < 140, "spry reverses some grains past a quarter");
        check(a.low_step == a.high_step && a.short_len == length && a.long_len == length, "pitch and length jitter start past the middle");
        a = spread(1, 0);          // the top: half reversed, up to 50 cents and half again in length
        check(a.reversed > 160 && a.reversed < 240, "full spry reverses about half the grains");
        check(a.low_step < root / std::pow(2.0, 30 / 1200.0) && a.low_step > root / cents50 * 0.999 &&
              a.high_step > root * std::pow(2.0, 30 / 1200.0) && a.high_step < root * cents50 * 1.001,
              "full spry detunes grains up to 50 cents either way");
        check(a.short_len < length * 0.6 && a.short_len >= length / 2 - 1 &&
              a.long_len > length * 1.4 && a.long_len <= length * 3 / 2 + 1, "full spry varies length by half either way");
        a = spread(1, 4);          // rsync at full spry: about half turned forward again
        check(a.reversed > 160 && a.reversed < 240, "full spry turns about half of a reversed mode's grains forward");
    }
    {   // One grain at a time (osc, dens 1, 10 ms) on a constant sample: each output
        // sample is its window, gain and pan, ramped a block segment at a time,
        // within 1% of the window worked out sample by sample.
        Rig r(level16, frames);
        r.parameters[grains::kMode] = choice(2, grains::mode_count);
        r.parameters[grains::kDens] = at(0);
        r.parameters[grains::kSize] = at(0);
        const u32 length = 441, peak = u32((2048 + ((65535 - 32767) * 61440u >> 16)) * 441ull >> 16);
        const u32 rise = (64u << 16) / peak, fall = (64u << 16) / (length - peak);
        double worst = 0;
        for (u32 b = 0; b < 40; ++b) {
            grains::render_sample(r.voice, r.block);
            for (u32 i = 0; i < opxy::block_frames; ++i) {
                const u32 t = (b * opxy::block_frames + i) % length;
                const u32 top = 64u << 16;
                const u32 w = t <= peak ? (t * rise > top ? top : t * rise)
                                        : ((t - peak) * fall >= (peak * rise > top ? top : peak * rise) ? 0
                                           : (peak * rise > top ? top : peak * rise) - (t - peak) * fall);
                const u32 index = w >> 16, part = w & 0xffff;
                const double window = index >= 64 ? grains::window_table[64] :
                    grains::window_table[index] + (grains::window_table[index + 1] - grains::window_table[index]) * (part / 65536.0);
                const double expected = 16384.0 * window * grains::gain_table[0] * 32767 / 1073741824.0;
                const double off = std::fabs(r.out[2 * i] - expected) / (16384.0 * 32767 * grains::gain_table[0] * 32767 / 1073741824.0);
                if (off > worst) worst = off;
            }
        }
        check(worst < 0.01, "a grain's output follows its window");
    }
    if (!failures) std::puts("PASS grains: silence, levels, freeze, scan, chords, pitch, sync, osc, shape, size, spry, reverse, window");
    return failures ? 1 : 0;
}
