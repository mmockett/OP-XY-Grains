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

// a * b >> 14 for 16-bit a and b: the Blackfin's single-cycle 16 by 16 multiply.
inline i32 mul16(i32 a, i32 b) { return (i32(opxy::i16(a)) * opxy::i16(b)) >> 14; }

// The sample at a frame and 16-bit fraction, mono, linearly interpolated; a
// stereo sample's channels are averaged.
template <bool Stereo> i32 read(const opxy::i16* data, u32 frame, u32 fraction, u32 end) {
    const u32 next = frame + 1 < end ? frame + 1 : frame;
    i32 a, b;
    if (Stereo) {
        a = (i32(data[frame * 2]) + data[frame * 2 + 1]) >> 1;
        b = (i32(data[next * 2]) + data[next * 2 + 1]) >> 1;
    } else {
        a = data[frame];
        b = data[next];
    }
    return a + mul16((b - a) >> 1, fraction >> 1);   // half the step by half the fraction: one multiply
}

// The controls, decoded once a block.
struct Settings {
    u32 begin, end, span;   // the region
    u32 pos;                // frames into the region
    i32 speed;              // scan, frames per output sample, Q16 (negative: backward)
    u32 length;             // a grain, samples
    u32 overlap8;           // grains sounding at once, Q8, 1..grain_count
    u32 chaos;              // spry, 0..65535: how random each grain is
    u32 spray;              // frames either side of the read point
    u32 chord;              // chord_table row
    u32 peak16;             // where the window peaks, 0..65535 through the grain
    u32 mode;               // kCloud, kSync or kOsc
    bool backward;          // the mode's reversed variant: grains play backwards
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
    const u32 own_rate = ratio(voice.sample.rate, 44100);       // the sample's rate, Q16 of 44.1 kHz
    const i32 speed = i32(times(curved * 2, own_rate));
    s.speed = centred < 0 ? -speed : speed;
    s.length = curve(size_table, 5, level(p[kSize]));
    s.overlap8 = 256 + ((level(p[kDens]) * (grain_count - 1) * 256) >> 16);
    s.chaos = level(p[kSpry]);
    s.spray = times(s.chaos, s.span >> 1);
    s.chord = (level(p[kChrd]) * 7) >> 16;
    // shpe 0 peaks late (a swell), 0.5 in the middle, 1 early (a pluck); the
    // peak keeps a little of the grain on each side.
    s.peak16 = 2048 + (((65535 - level(p[kShpe])) * 61440u) >> 16);
    const u32 mode = (level(p[kMode]) * mode_count) >> 16;   // cloud, sync, osc, then each reversed
    s.mode = mode % 3;
    s.backward = mode >= 3;
    // The note's rate from middle C, the sample's own rate and the pad's tuning.
    s.rate = times(times(ratio(block.pitch[0], c4_increment), own_rate),
                   cents_ratio(block.region.pitch_cents));
    return s;
}

// Samples until the next grain.
u32 interval(Voice& voice, const Settings& s, const opxy::SampleBlock& block) {
    if (s.mode == kSync) {
        // dens picks the division: 1/2, 1/4, 1/8, 1/16 or 1/32 notes.
        const u32 bpm_x10 = block.transport.bpm_x10 ? block.transport.bpm_x10 : 1200;
        const u32 quarter = 44100u * 600u / bpm_x10;
        const u32 division = ((s.overlap8 - 256) * 5) / ((grain_count - 1) * 256 + 1);   // 0..4
        return (quarter * 2) >> division;
    }
    const u32 even = (s.length * 256) / s.overlap8;
    if (s.mode == kOsc) return even ? even : 1;
    // cloud: half to one and a half times the even spacing.
    const u32 jitter = (next_random(voice) >> 16) + 32768;          // 0.5..1.5, Q16
    const u32 spaced = times(even, jitter);
    return spaced ? spaced : 1;
}

// Starts a grain in the first free slot; returns it, or null when none is free.
// free_at, while a block renders: the sample from which each slot is free.
Grain* spawn(Voice& voice, const Settings& s, const u32* free_at = 0, u32 at = 0) {
    Grain* g = 0;
    for (u32 i = 0; i < grain_count && !g; ++i)
        if (free_at ? free_at[i] <= at : !voice.grains[i].active) g = &voice.grains[i];
    if (!g || !s.span) return 0;
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
    // spry, in stages: position scatter from the start (above), stereo spread
    // reaching full width by the middle, reversed grains from a quarter (up to
    // half of them by three quarters), and from the middle up a random pitch,
    // up to 50 cents either way, and length, up to half again either way.
    const u32 jitter = s.chaos > 32768 ? (s.chaos - 32768) * 2 : 0;   // 0..65534
    if (jitter) {
        const i32 cents = i32(next_random(voice) % (2 * pitch_jitter_cents + 1)) - i32(pitch_jitter_cents);
        g->step = times(g->step, cents_ratio(cents * i32(jitter >> 8) >> 8));
    }
    if (!g->step) g->step = 1;
    // Its window: rising to the peak, then falling, over its length.
    g->length = s.length;
    if (jitter) {
        const i32 spread = (i32(next_random(voice) >> 16) - 32768) * i32(jitter >> 1) >> 15;   // -32768..32767
        g->length = times(s.length, u32(65536 + spread));                                       // 0.5..1.5
    }
    if (g->length < 2) g->length = 2;
    g->peak = times(s.length, s.peak16);
    if (!g->peak) g->peak = 1;
    if (g->peak >= g->length) g->peak = g->length - 1;
    g->rise = (64u << 16) / g->peak;
    g->fall = (64u << 16) / (g->length - g->peak);
    g->window = 0;
    g->age = 0;
    const u32 r16 = next_random(voice) >> 16;
    const u32 wide = (s.chaos * 17) >> 16, width = wide > 8 ? 8 : wide;   // 0..8, full from the middle
    g->pan = opxy::u8(8 + i32(r16 % (2 * width + 1)) - i32(width));
    const u32 flips = s.chaos <= 16384 ? 0 : s.chaos - 16384 > 32768 ? 32768 : s.chaos - 16384;   // Q16 chance
    g->reverse = s.backward != ((next_random(voice) >> 16) < flips);
    g->active = true;
    return g;
}

// Where a grain's window is at age t, Q16 over its 64 steps: the ramp a
// sample at a time up to the peak and down after it, in closed form.
u32 window_position(const Grain& g, u32 t) {
    const u32 top = 64u << 16;
    if (t <= g.peak) return t * g.rise > top ? top : t * g.rise;   // t * rise stays near top
    const u32 peak = g.peak * g.rise > top ? top : g.peak * g.rise;
    const u32 down = (t - g.peak) * g.fall;                        // and so does this
    return down >= peak ? 0 : peak - down;
}

// A grain's left and right amplitude at age t, its window, gain and pan in one
// Q9 multiplier: a sample times (amplitude >> 9), at most 26753 so that both
// fit 16 bits, is half its share of the output.
void amplitude(const Grain& g, u32 t, i32 gain, i32& left, i32& right) {
    const u32 position = window_position(g, t), index = position >> 16, part = position & 0xffff;
    const i32 w = index >= 64 ? window_table[64] :
        window_table[index] + ((i32(window_table[index + 1]) - window_table[index]) * i32(part) >> 16);
    const i32 shaped = (w * gain) >> 15;                           // at most 53508
    left = (shaped * pan_table[g.pan]) >> 7;
    right = (shaped * pan_table[16 - g.pan]) >> 7;
}

// n samples of one grain into out (left/right pairs), its amplitudes ramped
// by dl and dr a sample. Reverse and the sample's channels are fixed per call.
// Most runs stay clear of the region's ends, and then read without checking.
template <bool Stereo, bool Reverse>
void play(Grain& g, const opxy::SampleView& sample, const Settings& s, i32* out, u32 n, i32 l, i32 r, i32 dl, i32 dr) {
    const opxy::i16* data = sample.data;
    const u32 whole = g.step >> 16, part = g.step & 0xffff;
    u32 frame = g.frame, fraction = g.fraction;
    const u32 travel = (fraction + n * g.step) >> 16;   // frames the run moves: n <= 16, so no overflow
    const bool clear = Reverse ? frame + 1 < s.end && frame >= s.begin + travel + 1
                               : frame + travel + 1 < s.end;
    if (clear) {
        for (; n; --n, out += 2) {
            const i32 a = Stereo ? (i32(data[frame * 2]) + data[frame * 2 + 1]) >> 1 : data[frame];
            const i32 b = Stereo ? (i32(data[frame * 2 + 2]) + data[frame * 2 + 3]) >> 1 : data[frame + 1];
            const i32 v = a + mul16((b - a) >> 1, fraction >> 1);
            out[0] += i32(opxy::i16(v)) * opxy::i16(l >> 9);
            out[1] += i32(opxy::i16(v)) * opxy::i16(r >> 9);
            l += dl;
            r += dr;
            fraction += part;
            if (Reverse) frame -= whole + (fraction >> 16);
            else frame += whole + (fraction >> 16);
            fraction &= 0xffff;
        }
    } else {
        for (; n; --n, out += 2) {
            const i32 v = read<Stereo>(data, frame, fraction, s.end);
            out[0] += i32(opxy::i16(v)) * opxy::i16(l >> 9);
            out[1] += i32(opxy::i16(v)) * opxy::i16(r >> 9);
            l += dl;
            r += dr;
            // Read on, wrapping within the region.
            fraction += part;
            const u32 frames = whole + (fraction >> 16);
            fraction &= 0xffff;
            if (Reverse) {
                const u32 back = frame - s.begin;
                frame = s.begin + (back >= frames ? back - frames : s.span - (frames - back) % s.span);
                if (frame >= s.end) frame = s.begin;
            } else {
                frame += frames;
                if (frame >= s.end) frame = s.begin + (frame - s.end) % s.span;
            }
        }
    }
    g.frame = frame;
    g.fraction = fraction;
}

// One grain over block samples [from, to), in segments of at most 16 samples
// (to the block's 16-sample marks, the window's peak and the grain's end),
// each a straight ramp between its amplitudes at either end. Returns the
// sample from which its slot is free: to while it sounds on.
u32 render_grain(Grain& g, const opxy::SampleView& sample, const Settings& s, i32 gain, i32* out, u32 from, u32 to) {
    u32 i = from;
    while (i < to && g.active) {
        u32 n = ((i | 15) + 1 < to ? (i | 15) + 1 : to) - i;
        if (n > g.length - g.age) n = g.length - g.age;
        if (g.age < g.peak && n > g.peak - g.age) n = g.peak - g.age;
        i32 l0, r0, l1, r1;
        amplitude(g, g.age, gain, l0, r0);
        amplitude(g, g.age + n, gain, l1, r1);
        // A full segment divides by a shift; the rare shorter ones by n.
        const i32 dl = n == 16 ? (l1 - l0) >> 4 : (l1 - l0) / i32(n);
        const i32 dr = n == 16 ? (r1 - r0) >> 4 : (r1 - r0) / i32(n);
        i32* at = out + 2 * i;
        const bool stereo = sample.channels == 2;
        if (g.reverse) {
            if (stereo) play<true, true>(g, sample, s, at, n, l0, r0, dl, dr);
            else play<false, true>(g, sample, s, at, n, l0, r0, dl, dr);
        } else {
            if (stereo) play<true, false>(g, sample, s, at, n, l0, r0, dl, dr);
            else play<false, false>(g, sample, s, at, n, l0, r0, dl, dr);
        }
        i += n;
        g.age += n;
        if (g.age >= g.length) g.active = false;
    }
    return g.active ? to : i;
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
    const i32 gain = gain_table[(sounding < 1 ? 1 : sounding > grain_count ? grain_count : sounding) - 1];

    // A grain at a time through the block: first those already sounding, then
    // each one due to start, from its own sample. A slot is free for a new
    // grain only from the sample its old one ends on.
    i32 mix[2 * opxy::block_frames] = {};   // half of each sample's output, for headroom
    u32 free_at[grain_count];
    for (u32 k = 0; k < grain_count; ++k)
        free_at[k] = voice.grains[k].active ? render_grain(voice.grains[k], voice.sample, s, gain, mix, 0, opxy::block_frames) : 0;
    for (u32 i = 0;;) {
        if (!voice.countdown) {
            if (Grain* g = spawn(voice, s, free_at, i))
                free_at[g - voice.grains] = render_grain(*g, voice.sample, s, gain, mix, i, opxy::block_frames);
            voice.countdown = interval(voice, s, block);
        }
        const u32 skip = voice.countdown < opxy::block_frames - i ? voice.countdown : opxy::block_frames - i;
        i += skip;
        voice.countdown -= skip;
        if (i >= opxy::block_frames) break;
    }

    for (u32 i = 0; i < opxy::block_frames; ++i) {
        i32 left = mix[2 * i], right = mix[2 * i + 1];   // half the output
        // The sample's own volume (unsigned Q27, unity 1 << 27), as the sampler applies it.
        if (block.region.level != opxy::sample_unity_level) {
            const i32 level = i32(block.region.level >> 16);          // Q11
            left = (left >> 11) * level;
            right = (right >> 11) * level;
        }
        const i32 limit = half_scale >> 1;
        left = (left > limit ? limit : left < -limit ? -limit : left) << 1;
        right = (right > limit ? limit : right < -limit ? -limit : right) << 1;
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
