// Grains' own checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
#include "../sample.cpp"
#include <cmath>
#include <cstdio>

namespace {
using opxy::i32;
using opxy::u32;
const u32 c4 = 25480119u, frames = 44100;
opxy::i16 sine[frames], square[frames];
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
        const double defaults[8] = {0, 0.853553, 0.392468, 0.428571, 0, 0.071429, 0.5, 0.166667};
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
            for (u32 mode = 0; mode < 3; ++mode) {
                Rig r(square, frames);
                r.parameters[grains::kChrd] = choice(chord, 7);
                r.parameters[grains::kMode] = choice(mode, 3);
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
    {   // sync at 120 BPM: dens picks a half note (44100 samples) down to a 1/32 (2756).
        Rig r(sine, frames);
        r.parameters[grains::kMode] = choice(1, 3);
        r.parameters[grains::kDens] = at(0);
        check(grains::interval(r.voice, grains::decode(r.voice, r.block), r.block) == 44100, "sync's longest division is a half note");
        r.parameters[grains::kDens] = at(1);
        check(grains::interval(r.voice, grains::decode(r.voice, r.block), r.block) == 2756, "sync's shortest is a 1/32 note");
    }
    {   // osc: grains evenly spaced, size over the overlap.
        Rig r(sine, frames);
        r.parameters[grains::kMode] = choice(2, 3);
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
    if (!failures) std::puts("PASS grains: silence, levels, freeze, scan, chords, pitch, sync, osc, shape, size");
    return failures ? 1 : 0;
}
