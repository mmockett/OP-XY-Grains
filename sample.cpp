#include "core.h"
#include "tables.h"

namespace grains {
namespace {
using opxy::i32;
using opxy::u32;

// Phase increment of middle C.
const u32 c4_increment = 25480119u;
// Output may peak at half scale.
const i32 half_scale = 0x40000000;

// A control as 0..65535.
u32 level(i32 parameter) { return parameter < 0 ? 0u : u32(parameter) >> 15; }

// a / b in 16-bit fixed point, by long division, without a 64-bit divide.
u32 ratio(u32 a, u32 b) {
    if (!b) return 0;
    u32 whole = a / b, remainder = a % b, fraction = 0;
    for (u32 bit = 0; bit < 16; ++bit) {
        const bool carry = remainder >= 0x80000000u;
        remainder <<= 1;
        fraction <<= 1;
        if (carry || remainder >= b) { remainder -= b; fraction |= 1; }
    }
    return whole << 16 | fraction;
}

// (a * b) >> 16 in 32-bit arithmetic: the device has no 64-bit multiply.
// Exact while the result fits 32 bits.
u32 times(u32 a, u32 b) {
    const u32 ah = a >> 16, al = a & 0xffff, bh = b >> 16, bl = b & 0xffff;
    return (ah * bh << 16) + ah * bl + al * bh + (al * bl >> 16);
}

// 2^(cents/1200) in Q16: whole semitones from the table, the rest linearly.
u32 cents_ratio(i32 cents) {
    i32 octave = 0;
    while (cents < 0) { cents += 1200; --octave; }
    while (cents >= 1200) { cents -= 1200; ++octave; }
    const u32 semitones = u32(cents) / 100, rest = u32(cents) % 100;
    const u32 low = semitone_table[semitones], high = semitones < 11 ? semitone_table[semitones + 1] : 131072u;
    const u32 r = low + (high - low) * rest / 100;
    return octave >= 0 ? r << octave : r >> -octave;
}

u32 next_random(Voice& voice) {
    u32 x = voice.random ? voice.random : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return voice.random = x;
}

// A table read with linear interpolation between its 2^steps + 1 entries.
template <class T> u32 curve(const T* table, u32 steps, u32 value16) {
    const u32 position = value16 << steps, index = position >> 16, fraction = position & 0xffff;
    if (index >= (1u << steps)) return u32(table[1u << steps]);
    return u32(table[index]) + ((u32(table[index + 1]) - u32(table[index])) * fraction >> 16);
}

// The sample at a frame and 16-bit fraction, mono, linearly interpolated.
i32 read(const opxy::SampleView& s, u32 frame, u32 fraction, u32 end) {
    const u32 next = frame + 1 < end ? frame + 1 : frame;
    i32 a, b;
    if (s.channels == 2) {
        a = (i32(s.data[frame * 2]) + s.data[frame * 2 + 1]) >> 1;
        b = (i32(s.data[next * 2]) + s.data[next * 2 + 1]) >> 1;
    } else {
        a = s.data[frame];
        b = s.data[next];
    }
    return a + (((b - a) * i32(fraction >> 1)) >> 15);   // 17 by 15 bits: no overflow
}

// The controls, decoded once a block.
struct Settings {
    u32 begin, end, span;   // the region
    u32 pos;                // frames into the region
    i32 speed;              // scan, frames per output sample, Q16 (negative: backward)
    u32 length;             // a grain, samples
    u32 overlap8;           // grains sounding at once, Q8, 1..8
    u32 spray;              // frames either side of the read point
    u32 chord;              // chord_table row
    u32 peak16;             // where the window peaks, 0..65535 through the grain
    u32 mode;
    u32 rate;               // the played note's playback rate, Q16
};

Settings decode(const Voice& voice, const opxy::SampleBlock& block) {
    const i32* p = block.parameters;
    Settings s;
    s.begin = block.region.begin;
    s.end = block.region.end < voice.sample.frames ? block.region.end : voice.sample.frames;
    s.span = s.end > s.begin ? s.end - s.begin : 0;
    s.pos = times(level(p[kPos]), s.span);
    if (s.pos >= s.span && s.span) s.pos = s.span - 1;
    // scan: -2x .. 2x of real time, curved so the middle is fine; 0 freezes.
    // A small dead zone around the middle freezes it exactly.
    i32 centred = i32(level(p[kScan])) - 32768;                 // -32768..32767
    centred = centred > 128 ? centred - 128 : centred < -128 ? centred + 128 : 0;
    const u32 magnitude = u32(centred < 0 ? -centred : centred);
    const u32 curved = magnitude * magnitude / 16256;            // 0..65536: 1.0x is 32768
    const i32 speed = i32(times(curved * 2, ratio(voice.sample.rate, 44100)));
    s.speed = centred < 0 ? -speed : speed;
    s.length = curve(size_table, 5, level(p[kSize]));
    s.overlap8 = 256 + ((level(p[kDens]) * 7 * 256) >> 16);
    s.spray = times(level(p[kSpry]), s.span >> 1);
    s.chord = (level(p[kChrd]) * 7) >> 16;
    // shpe 0 peaks late (a swell), 0.5 in the middle, 1 early (a pluck); the
    // peak keeps a little of the grain on each side.
    s.peak16 = 2048 + (((65535 - level(p[kShpe])) * 61440u) >> 16);
    s.mode = (level(p[kMode]) * 3) >> 16;
    // The note's rate from middle C, the sample's own rate and the pad's tuning.
    s.rate = times(times(ratio(block.pitch[0], c4_increment), ratio(voice.sample.rate, 44100)),
                   cents_ratio(block.region.pitch_cents));
    return s;
}

// Samples until the next grain.
u32 interval(Voice& voice, const Settings& s, const opxy::SampleBlock& block) {
    if (s.mode == kSync) {
        // dens picks the division: 1/2, 1/4, 1/8, 1/16 or 1/32 notes.
        const u32 bpm_x10 = block.transport.bpm_x10 ? block.transport.bpm_x10 : 1200;
        const u32 quarter = 44100u * 600u / bpm_x10;
        const u32 division = ((s.overlap8 - 256) * 5) / (7 * 256 + 1);   // 0..4
        return (quarter * 2) >> division;
    }
    const u32 even = (s.length * 256) / s.overlap8;
    if (s.mode == kOsc) return even ? even : 1;
    // cloud: half to one and a half times the even spacing.
    const u32 jitter = (next_random(voice) >> 16) + 32768;          // 0.5..1.5, Q16
    const u32 spaced = times(even, jitter);
    return spaced ? spaced : 1;
}

void spawn(Voice& voice, const Settings& s) {
    Grain* g = 0;
    for (u32 i = 0; i < grain_count && !g; ++i) if (!voice.grains[i].active) g = &voice.grains[i];
    if (!g || !s.span) return;
    // Where it reads: pos, plus the scan, plus spray, wrapped into the region.
    i32 offset = i32(s.pos) + voice.scan;
    if (s.spray) offset += i32(next_random(voice) % (2 * s.spray + 1)) - i32(s.spray);
    offset %= i32(s.span);
    if (offset < 0) offset += i32(s.span);
    g->frame = s.begin + u32(offset);
    g->fraction = 0;
    // Its pitch: the note, times chrd's note for this grain.
    const u32 n = voice.spawned++;
    const u32 count = chord_count[s.chord];
    u32 r = chord_table[s.chord * 8 + n % count];
    if (s.chord == 1) r = detune_table[n % 8];
    g->step = times(s.rate, r);
    if (!g->step) g->step = 1;
    // Its window: rising to the peak, then falling, over its length.
    g->length = s.length;
    g->peak = times(s.length, s.peak16);
    if (!g->peak) g->peak = 1;
    if (g->peak >= g->length) g->peak = g->length - 1;
    g->rise = (64u << 16) / g->peak;
    g->fall = (64u << 16) / (g->length - g->peak);
    g->window = 0;
    g->age = 0;
    // Spray also spreads grains across the stereo field and, past a quarter
    // of the region, turns every other one around.
    const u32 r16 = next_random(voice) >> 16;
    const u32 width = s.span ? s.spray * 16 / s.span : 0;   // 0..8
    g->pan = opxy::u8(8 + i32(r16 % (2 * width + 1)) - i32(width));
    g->reverse = s.spray > s.span / 8 && (n & 1);
    g->active = true;
}
}

void initialize(Voice& voice, const opxy::SampleView& sample) {
    voice.sample = sample;
    for (u32 i = 0; i < grain_count; ++i) voice.grains[i].active = false;
    voice.scan = 0;
    voice.scan_fraction = 0;
    voice.countdown = 0;
    voice.random = 0x2545f491u;
    voice.spawned = 0;
}

// A note: no grains yet, the scan back at pos, and the first grain at once.
void start(Voice& voice) {
    for (u32 i = 0; i < grain_count; ++i) voice.grains[i].active = false;
    voice.scan = 0;
    voice.scan_fraction = 0;
    voice.countdown = 0;
    voice.spawned = 0;
}

void render_sample(Voice& voice, const opxy::SampleBlock& block) {
    const bool stereo = block.output_channels == 2;
    const u32 words = stereo ? 2 * opxy::block_frames : opxy::block_frames;
    for (u32 i = 0; i < words; ++i) block.output[i] = 0;
    if (!voice.sample.frames || !voice.sample.data) return;
    const Settings s = decode(voice, block);
    if (!s.span) return;
    const u32 sounding = s.overlap8 >> 8;
    const i32 gain = gain_table[(sounding < 1 ? 1 : sounding > 8 ? 8 : sounding) - 1];

    for (u32 i = 0; i < opxy::block_frames; ++i) {
        if (!voice.countdown) {
            spawn(voice, s);
            voice.countdown = interval(voice, s, block);
        }
        --voice.countdown;
        i32 left = 0, right = 0;
        for (u32 k = 0; k < grain_count; ++k) {
            Grain& g = voice.grains[k];
            if (!g.active) continue;
            // Window: up the table to the peak, back down after it.
            const u32 index = g.window >> 16, part = g.window & 0xffff;
            const i32 w = index >= 64 ? window_table[64] :
                window_table[index] + ((i32(window_table[index + 1]) - window_table[index]) * i32(part) >> 16);
            const i32 v = ((read(voice.sample, g.frame, g.fraction, s.end) * w) >> 15) * gain;   // Q30 at full scale
            left += (v >> 15) * pan_table[g.pan];
            right += (v >> 15) * pan_table[16 - g.pan];
            if (g.age < g.peak) g.window = g.window + g.rise > (64u << 16) ? 64u << 16 : g.window + g.rise;
            else g.window = g.window > g.fall ? g.window - g.fall : 0;
            // Read on, wrapping within the region.
            const u32 moved = g.fraction + (g.step & 0xffff);
            const u32 frames = (g.step >> 16) + (moved >> 16);
            g.fraction = moved & 0xffff;
            if (g.reverse) {
                const u32 back = g.frame - s.begin;
                g.frame = s.begin + (back >= frames ? back - frames : s.span - (frames - back) % s.span);
                if (g.frame >= s.end) g.frame = s.begin;
            } else {
                g.frame += frames;
                if (g.frame >= s.end) g.frame = s.begin + (g.frame - s.end) % s.span;
            }
            if (++g.age >= g.length) g.active = false;
        }
        // The sample's own volume (unsigned Q27, unity 1 << 27), as the sampler applies it.
        if (block.region.level != opxy::sample_unity_level) {
            const i32 level = i32(block.region.level >> 16);          // Q11
            left = (left >> 11) * level;
            right = (right >> 11) * level;
        }
        if (left > half_scale) left = half_scale;
        if (left < -half_scale) left = -half_scale;
        if (right > half_scale) right = half_scale;
        if (right < -half_scale) right = -half_scale;
        if (stereo) { block.output[2 * i] = left; block.output[2 * i + 1] = right; }
        else block.output[i] = (left >> 1) + (right >> 1);
    }

    // The scan moves pos's read point on, in real time, wrapping in the region.
    const i32 moved = i32(voice.scan_fraction) + s.speed * i32(opxy::block_frames);   // well inside 32 bits
    voice.scan += moved >> 16;
    voice.scan_fraction = u32(moved) & 0xffff;
    voice.scan %= i32(s.span);
}
}
