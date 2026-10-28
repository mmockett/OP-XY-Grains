#include "core.h"

namespace grains {
namespace {
// Phase increment of middle C, and of 220 Hz, the lowest beep.
const opxy::u32 c4_increment = 25480119u, beep_low = 21426141u;
// A control moved when it changed by more than about 0.1% in a block.
const opxy::i32 moved = 1 << 21;

opxy::u32 level(opxy::i32 parameter) { return parameter < 0 ? 0u : opxy::u32(parameter); }

// a / b in 16-bit fixed point, by long division, without a 64-bit divide.
opxy::u32 ratio(opxy::u32 a, opxy::u32 b) {
    opxy::u32 whole = a / b, remainder = a % b, fraction = 0;
    for (opxy::u32 bit = 0; bit < 16; ++bit) {
        const bool carry = remainder >= 0x80000000u;
        remainder <<= 1;
        fraction <<= 1;
        if (carry || remainder >= b) { remainder -= b; fraction |= 1; }
    }
    return whole << 16 | fraction;
}
}

void initialize(Voice& voice, const opxy::SampleView& sample) {
    voice.sample = sample;
    voice.frame = voice.fraction = 0;
    voice.primed = false;
    voice.beep_slot = 0;
    voice.beep_elapsed = beep_idle;
    voice.beep_phase = 0;
}

void start(Voice& voice) {
    voice.frame = ~0u;   // the first block starts at the region's beginning
    voice.fraction = 0;
}

void render_sample(Voice& voice, const opxy::SampleBlock& block) {
    // Which control moved: start a pattern of n pulses for control n, unless
    // one is already sounding; its pitch follows the control's value.
    for (opxy::u32 k = 0; k < 8; ++k) {
        const opxy::i32 change = block.parameters[k] - voice.last[k];
        if (voice.primed && (change > moved || change < -moved) && voice.beep_elapsed == beep_idle) {
            voice.beep_slot = k;
            voice.beep_elapsed = 0;
        }
    }
    for (opxy::u32 k = 0; k < 8; ++k) voice.last[k] = block.parameters[k];
    voice.primed = true;
    // 220..880 Hz across the control's range.
    const opxy::u32 beep_increment = beep_low + (beep_low * 3u >> 16) * (level(block.parameters[voice.beep_slot]) >> 15);

    const opxy::SampleRegion& region = block.region;
    const bool playing = voice.sample.frames && region.begin < region.end;
    // Playback rate relative to middle C, in 16-bit fixed point, once a block.
    const opxy::u32 rate = ratio(block.pitch[0], c4_increment);
    for (opxy::u32 i = 0; i < opxy::block_frames; ++i) {
        opxy::i32 out = 0;
        if (playing) {
            if (voice.frame < region.begin || voice.frame >= region.end) { voice.frame = region.begin; voice.fraction = 0; }
            out = opxy::i32(opxy::sample_at(voice.sample, voice.frame)) << 14;   // a quarter of full scale
            voice.fraction += rate & 0xffffu;
            voice.frame += (rate >> 16) + (voice.fraction >> 16);
            voice.fraction &= 0xffffu;
        }
        if (voice.beep_elapsed != beep_idle) {
            const opxy::u32 pulse = voice.beep_elapsed / beep_period;
            if (pulse > voice.beep_slot) voice.beep_elapsed = beep_idle;
            else {
                if (voice.beep_elapsed % beep_period < beep_on)
                    out += voice.beep_phase < 0x80000000u ? 0x08000000 : -0x08000000;   // a square, an eighth of full scale
                voice.beep_phase += beep_increment;
                ++voice.beep_elapsed;
            }
        }
        block.output[i] = out;
    }
}
}
