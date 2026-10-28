// The spike's own checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
#include "../sample.cpp"
#include <cstdio>

namespace {
const opxy::u32 c4 = 25480119u;
opxy::i16 ramp[10000];

int fail(const char* why) { std::printf("FAIL %s\n", why); return 1; }
}

int main() {
    for (unsigned i = 0; i < 10000; ++i) ramp[i] = opxy::i16(i);
    const opxy::SampleView view = {ramp, 10000, 44100, 1};
    static grains::Voice voice;
    grains::initialize(voice, view);
    grains::start(voice);

    opxy::i32 out[opxy::block_frames], parameters[8];
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < 8; ++i) parameters[i] = 0x40000000;
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = c4;
    opxy::SampleBlock block = {};
    block.output = out; block.parameters = parameters; block.pitch = pitch;
    block.region.begin = 1000; block.region.end = 3000; block.region.level = opxy::sample_unity_level;
    block.output_channels = 1;

    // At middle C the region plays frame by frame and loops inside it.
    unsigned expected = 1000;
    for (unsigned b = 0; b < 100; ++b) {
        grains::render_sample(voice, block);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            if (out[i] != opxy::i32(expected) << 14) return fail("the region doesn't play frame by frame in a loop");
            expected = expected + 1 == 3000 ? 1000 : expected + 1;
        }
    }
    // Moving control 3 beeps three pulses, then stops; the others stay still.
    parameters[2] = 0x60000000;
    unsigned pulses = 0, quiet = 100000;   // quiet for long before the change
    bool on = false;
    for (unsigned b = 0; b < 400; ++b) {
        grains::render_sample(voice, block);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            const opxy::i32 beep = out[i] - (opxy::i32(expected) << 14);
            expected = expected + 1 == 3000 ? 1000 : expected + 1;
            const bool sounding = beep != 0;
            if (sounding && !on && quiet > 1000) ++pulses;
            quiet = sounding ? 0 : quiet + 1;
            on = sounding || (on && quiet < 1000);
        }
    }
    if (pulses != 3) { std::printf("FAIL control 3 beeped %u pulses\n", pulses); return 1; }
    std::puts("PASS region loop and control beeps");
    return 0;
}
