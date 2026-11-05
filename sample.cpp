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
// Samples a grain shed over a voice's share takes to fade out.
const u32 fade = 256;

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
    if (!cents) return 65536;
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

// The shared grain budget. Sample engines have no Shared state and no call
// when a voice ends, so voices are counted, not tracked: each render adds the
// grains its voice wants to the cycle's total, and a voice rendering a second
// time in a cycle starts the next one. A count that starts afresh every block
// cannot drift as notes come and go.
struct Census {
    u32 cycle;    // the cycle being counted
    u32 wanted;   // grains wanted in it so far
    u32 total;    // grains wanted in the last whole cycle
};
Census census;

// The grains a voice may sound this block, given those it wants: as many as it
// has slots while every voice together wants no more than the budget, and
// otherwise its part of the budget, at least one.
u32 share(Voice& voice, u32 want) {
    if (voice.cycle == census.cycle) {
        census.total = census.wanted;
        census.wanted = 0;
        ++census.cycle;
    }
    voice.cycle = census.cycle;
    census.wanted += want;
    if (census.total <= grain_budget) return grain_count;
    const u32 part = grain_budget * want / census.total;
    return part ? part : 1;
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

// Half the output, held within half of half scale, then doubled.
inline i32 full(i32 x) {
    const i32 limit = half_scale >> 1;
    x = x < limit ? x : limit;
    return (x > -limit ? x : -limit) * 2;
}

// The controls, decoded once a block.
struct Settings {
    u32 begin, end, span;   // the region
    u32 pos;                // frames into the region
    i32 speed;              // scan, frames per output sample, Q16 (negative: backward)
    u32 length;             // a grain, samples
    u32 overlap8;           // grains sounding at once, Q8, 1..grain_count
    i32 gain;               // a new grain's share of the output, for overlap8
    u32 chaos;              // spry, 0..65535: how random each grain is
    u32 spray;              // frames either side of the read point
    u32 chord;              // chord_table row
    u32 peak16;             // where the window peaks, 0..65535 through the grain
    u32 mode;               // kCloud, kSync or kOsc
    bool backward;          // the mode's reversed variant: grains play backwards
    u32 rate;               // the played note's playback rate, Q16
};

// A grain's share of the output when overlap8 >> 8 sound at once.
i32 grain_gain(u32 overlap8) {
    const u32 sounding = overlap8 >> 8;
    return gain_table[(sounding < 1 ? 1 : sounding > grain_count ? grain_count : sounding) - 1];
}

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
    const u32 own_rate = voice.sample.rate == 44100 ? 65536u : ratio(voice.sample.rate, 44100);   // Q16 of 44.1 kHz
    const i32 speed = i32(times(curved * 2, own_rate));
    s.speed = centred < 0 ? -speed : speed;
    s.length = curve(size_table, 5, level(p[kSize]));
    s.overlap8 = 256 + ((level(p[kDens]) * (grain_count - 1) * 256) >> 16);
    s.gain = grain_gain(s.overlap8);
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

// sync's samples between grains: dens picks the division, 1/2, 1/4, 1/8, 1/16
// or 1/32 notes.
u32 division(const Settings& s, const opxy::SampleBlock& block) {
    const u32 bpm_x10 = block.transport.bpm_x10 ? block.transport.bpm_x10 : 1200;
    const u32 quarter = 44100u * 600u / bpm_x10;
    return (quarter * 2) >> (((s.overlap8 - 256) * 5) / ((grain_count - 1) * 256 + 1));
}

// Samples until the next grain.
u32 interval(Voice& voice, const Settings& s, const opxy::SampleBlock& block) {
    if (s.mode == kSync) return division(s, block);
    const u32 even = (s.length * 256) / s.overlap8;
    if (s.mode == kOsc) return even ? even : 1;
    // cloud: half to one and a half times the even spacing.
    const u32 jitter = (next_random(voice) >> 16) + 32768;          // 0.5..1.5, Q16
    const u32 spaced = times(even, jitter);
    return spaced ? spaced : 1;
}

// The grains a voice sounds at once: dens's overlap, or in sync as many as its
// division fits into a grain's length.
u32 wanted(const Settings& s, const opxy::SampleBlock& block) {
    u32 n = (s.overlap8 + 255) >> 8;
    if (s.mode == kSync) {
        const u32 spacing = division(s, block);
        n = (s.length + spacing - 1) / spacing;
    }
    return n < 1 ? 1 : n > grain_count ? grain_count : n;
}

// Starts a grain in the first free slot; returns it, or null when none is free
// or the voice already sounds cap grains.
// free_at, while a block renders: the sample from which each slot is free.
Grain* spawn(Voice& voice, const Settings& s, const u32* free_at = 0, u32 at = 0, u32 cap = grain_count) {
    Grain* g = 0;
    u32 busy = 0;
    for (u32 i = 0; i < grain_count; ++i) {
        if (free_at ? free_at[i] <= at : !voice.grains[i].active) { if (!g) g = &voice.grains[i]; }
        else ++busy;
    }
    if (!g || busy >= cap || !s.span) return 0;
    // Where it reads: pos, plus the scan, plus spray, wrapped into the region.
    i32 offset = i32(s.pos) + voice.scan;
    if (s.spray) offset += i32(next_random(voice) % (2 * s.spray + 1)) - i32(s.spray);
    if (offset < 0 || offset >= i32(s.span)) {   // most grains start inside: no division
        offset %= i32(s.span);
        if (offset < 0) offset += i32(s.span);
    }
    g->frame = s.begin + u32(offset);
    g->fraction = 0;
    // Its pitch: the note, times chrd's note for this grain.
    const u32 n = voice.spawned++;
    const u32 count = chord_count[s.chord];
    u32 r = chord_table[s.chord * 8 + (count > 1 ? n % count : 0)];
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
    g->gain = s.gain;
    g->age = 0;
    const u32 r16 = next_random(voice) >> 16;
    const u32 wide = (s.chaos * 17) >> 16, width = wide > 8 ? 8 : wide;   // 0..8, full from the middle
    g->pan = opxy::u8(8 + (width ? i32(r16 % (2 * width + 1)) - i32(width) : 0));
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

// Ends a grain early: its window falls from where it is now to nothing over
// fade samples. The closed form above then reads it as a grain peaking now.
void retire(Grain& g) {
    const u32 w = window_position(g, g.age);
    if (!g.age || !w) { g.active = false; return; }
    g.rise = w / g.age;
    g.peak = g.age;
    g.fall = (w + fade - 1) / fade;
    g.length = g.age + fade;
}

// Fades out the grains a voice sounds beyond cap, those with longest left
// first (the first slot of equals), so a voice whose share shrinks keeps to it
// within fade samples. A retired grain has fade left, so retiring one at a time
// would pick the same ones: one pass and a sort, not a pass for each.
void shed(Voice& voice, u32 cap) {
    opxy::u8 order[grain_count];
    u32 staying = 0;
    for (u32 k = 0; k < grain_count; ++k) {
        const Grain& g = voice.grains[k];
        if (!g.active) continue;
        const u32 left = g.length - g.age;
        if (left <= fade) continue;
        u32 at = staying++;   // insertion, longest first, keeping slot order among equals
        for (; at && voice.grains[order[at - 1]].length - voice.grains[order[at - 1]].age < left; --at) order[at] = order[at - 1];
        order[at] = opxy::u8(k);
    }
    for (u32 k = cap; k < staying; ++k) retire(voice.grains[order[k - cap]]);
}

// A grain's window and gain at age t, before its pan: at most 53508.
i32 shaped(const Grain& g, u32 t) {
    const u32 position = window_position(g, t), index = position >> 16, part = position & 0xffff;
    const i32 w = index >= 64 ? window_table[64] :
        window_table[index] + ((i32(window_table[index + 1]) - window_table[index]) * i32(part) >> 16);
    return (w * g.gain) >> 15;
}

// The block's mix, half of each sample's output, for headroom. A grain panned
// off centre adds to pairs (left/right); a centred one, whose left and right
// are equal, adds once to centre, which goes to both sides at the end. Each is
// cleared when a grain first adds to it.
struct Mix {
    i32 pairs[2 * opxy::block_frames];
    i32 centre[opxy::block_frames];
    bool has_pairs, has_centre;
};

// One segment of a grain's run: n samples, its amplitudes from l and r, ramped
// by dl and dr a sample. An amplitude is the window, gain and pan in one Q9
// multiplier: a sample times (amplitude >> 9), at most 26753 so that both fit
// 16 bits, is half its share of the output.
struct Ramp {
    u32 n;
    i32 l, dl, r, dr;
};

// A grain's segments into out (left/right pairs, or one word a sample for a
// centred grain), reading from at with fraction, for a run clear of the
// region's ends: no checks. Reverse, the sample's channels and centring are
// fixed per function, so that each loop keeps to registers. Each sample's
// frames are read while the one before is mixed. Returns where the next
// sample reads.
template <bool Stereo, bool Reverse, bool Centre>
__attribute__((noinline)) const opxy::i16* run(const opxy::i16* at, u32* fraction_io, u32 step, i32* out,
                                               const Ramp* ramp, u32 count) {
    const u32 stride = Centre ? 1 : 2, width = Stereo ? 2 : 1;
    u32 fraction = *fraction_io;
    // The run ends at least a frame clear of the region's end (play), so the
    // frames read ahead after its last sample are still in it.
    i32 a = Stereo ? (i32(at[0]) + at[1]) >> 1 : at[0];
    i32 b = Stereo ? (i32(at[2]) + at[3]) >> 1 : at[1];
    for (; count; --count, ++ramp) {
        i32 l = ramp->l, r = Centre ? 0 : ramp->r;
        const i32 dl = ramp->dl, dr = Centre ? 0 : ramp->dr;
        for (u32 n = ramp->n; n; --n, out += stride) {
            // The whole step at once: fraction < 2^16, so the frames are its top half.
            const u32 f = fraction;
            fraction += step;
            const u32 frames = fraction >> 16;
            fraction &= 0xffff;
            if (Reverse) at -= frames * width;
            else at += frames * width;
            // mul16, on operands the compiler can't widen: the Blackfin's 16 by
            // 16 multiply. (b - a) >> 1 fits 16 bits, so its low half is the same.
            const i32 v = a + ((i32(opxy::i16(u32(b - a) >> 1)) * opxy::i16(f >> 1)) >> 14);
            a = Stereo ? (i32(at[0]) + at[1]) >> 1 : at[0];
            b = Stereo ? (i32(at[2]) + at[3]) >> 1 : at[1];
            out[0] += i32(opxy::i16(v)) * opxy::i16(l >> 9);
            l += dl;
            if (!Centre) {
                out[1] += i32(opxy::i16(v)) * opxy::i16(r >> 9);
                r += dr;
            }
        }
    }
    *fraction_io = fraction;
    return at;
}

// One segment of a grain, as run, for a run that may reach a region's end:
// each read checks it and the read point wraps within the region.
template <bool Stereo, bool Reverse, bool Centre>
void wrap(Grain& g, const opxy::i16* data, const Settings& s, i32* out, const Ramp& ramp) {
    const u32 stride = Centre ? 1 : 2, whole = g.step >> 16, part = g.step & 0xffff;
    u32 frame = g.frame, fraction = g.fraction;
    i32 l = ramp.l, r = ramp.r;
    for (u32 n = ramp.n; n; --n, out += stride) {
        const i32 v = read<Stereo>(data, frame, fraction, s.end);
        out[0] += i32(opxy::i16(v)) * opxy::i16(l >> 9);
        l += ramp.dl;
        if (!Centre) {
            out[1] += i32(opxy::i16(v)) * opxy::i16(r >> 9);
            r += ramp.dr;
        }
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
    g.frame = frame;
    g.fraction = fraction;
}

// Whether n samples from g's read point stay clear of the region's ends, with
// a frame to spare. n <= 64 and part < 2^16, so no overflow.
template <bool Reverse>
bool clear(const Grain& g, const Settings& s, u32 n) {
    const u32 travel = n * (g.step >> 16) + ((g.fraction + n * (g.step & 0xffff)) >> 16);   // frames it moves
    return Reverse ? g.frame + 1 < s.end && g.frame >= s.begin + travel + 1 : g.frame + travel + 1 < s.end;
}

// count segments, n samples in all, of one grain into out. Most runs stay
// clear of the region's ends, and then read without checking; a run that
// isn't goes a segment at a time, each read without checking where it can.
template <bool Stereo, bool Reverse, bool Centre>
void play(Grain& g, const opxy::SampleView& sample, const Settings& s, i32* out, const Ramp* ramp, u32 count, u32 n) {
    const u32 width = Stereo ? 2 : 1, stride = Centre ? 1 : 2;
    if (clear<Reverse>(g, s, n)) {
        const opxy::i16* at = run<Stereo, Reverse, Centre>(sample.data + g.frame * width, &g.fraction, g.step, out, ramp, count);
        g.frame = u32(at - sample.data) / width;
        return;
    }
    for (; count; --count, ++ramp) {
        if (clear<Reverse>(g, s, ramp->n)) {
            const opxy::i16* at = run<Stereo, Reverse, Centre>(sample.data + g.frame * width, &g.fraction, g.step, out, ramp, 1);
            g.frame = u32(at - sample.data) / width;
        } else {
            wrap<Stereo, Reverse, Centre>(g, sample.data, s, out, *ramp);
        }
        out += stride * ramp->n;
    }
}

// One grain over block samples [from, to), in segments of at most 16 samples
// (to the block's 16-sample marks, the window's peak and the grain's end),
// each a straight ramp between its amplitudes at either end; a segment's end
// is the next one's start. Returns the sample from which its slot is free: to
// while it sounds on.
template <bool Stereo, bool Reverse, bool Centre>
u32 sound(Grain& g, const opxy::SampleView& sample, const Settings& s, i32* mix, u32 from, u32 to) {
    const i32 pan_left = pan_table[g.pan], pan_right = pan_table[16 - g.pan];
    Ramp ramps[6];   // at most five: to each 16-sample mark in a block, and one split at the peak
    u32 count = 0, age = g.age, i = from;
    const i32 w0 = shaped(g, age);
    i32 l0 = (w0 * pan_left) >> 7, r0 = Centre ? 0 : (w0 * pan_right) >> 7;
    while (i < to && age < g.length) {
        u32 n = ((i | 15) + 1 < to ? (i | 15) + 1 : to) - i;
        if (n > g.length - age) n = g.length - age;
        if (age < g.peak && n > g.peak - age) n = g.peak - age;
        const i32 w = shaped(g, age + n);
        const i32 l1 = (w * pan_left) >> 7, r1 = Centre ? 0 : (w * pan_right) >> 7;
        Ramp& ramp = ramps[count++];
        ramp.n = n;
        ramp.l = l0;
        ramp.r = r0;
        // A full segment divides by a shift; the rare shorter ones by n.
        ramp.dl = n == 16 ? (l1 - l0) >> 4 : (l1 - l0) / i32(n);
        ramp.dr = Centre ? 0 : n == 16 ? (r1 - r0) >> 4 : (r1 - r0) / i32(n);
        l0 = l1;
        r0 = r1;
        i += n;
        age += n;
    }
    play<Stereo, Reverse, Centre>(g, sample, s, mix + (Centre ? 1 : 2) * from, ramps, count, i - from);
    g.age = age;
    if (age >= g.length) { g.active = false; return i; }
    return to;
}

// One grain over block samples [from, to), into the mix for its pan. Returns
// the sample from which its slot is free.
u32 render_grain(Grain& g, const opxy::SampleView& sample, const Settings& s, Mix& mix, u32 from, u32 to) {
    if (from >= to || !g.active) return g.active ? to : from;
    // pan_table[8] both ways: a centred grain's left and right are equal.
    const bool stereo = sample.channels == 2;
    if (g.pan == 8) {
        if (!mix.has_centre) { for (u32 k = 0; k < opxy::block_frames; ++k) mix.centre[k] = 0; mix.has_centre = true; }
        if (g.reverse) return stereo ? sound<true, true, true>(g, sample, s, mix.centre, from, to)
                                     : sound<false, true, true>(g, sample, s, mix.centre, from, to);
        return stereo ? sound<true, false, true>(g, sample, s, mix.centre, from, to)
                      : sound<false, false, true>(g, sample, s, mix.centre, from, to);
    }
    if (!mix.has_pairs) { for (u32 k = 0; k < 2 * opxy::block_frames; ++k) mix.pairs[k] = 0; mix.has_pairs = true; }
    if (g.reverse) return stereo ? sound<true, true, false>(g, sample, s, mix.pairs, from, to)
                                 : sound<false, true, false>(g, sample, s, mix.pairs, from, to);
    return stereo ? sound<true, false, false>(g, sample, s, mix.pairs, from, to)
                  : sound<false, false, false>(g, sample, s, mix.pairs, from, to);
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
    voice.cycle = census.cycle - 1;   // not yet counted in this cycle
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
    Settings s;
    if (!voice.sample.frames || !voice.sample.data || !(s = decode(voice, block)).span) {
        for (u32 i = 0; i < words; ++i) block.output[i] = 0;
        return;
    }
    // The voice's share of the grain budget: past it, fewer grains at once in
    // cloud and osc, and shorter ones on the same beat in sync.
    const u32 want = wanted(s, block), cap = share(voice, want);
    // In sync dens is a division, not an overlap: a grain's level follows how many sound.
    if (s.mode == kSync) s.gain = grain_gain(want << 8);
    if (cap < want) {
        if (s.mode == kSync) s.length = division(s, block) * cap;
        else s.overlap8 = cap << 8;
        s.gain = grain_gain(cap << 8);
    }
    shed(voice, cap);

    // A grain at a time through the block: first those already sounding, then
    // each one due to start, from its own sample. A slot is free for a new
    // grain only from the sample its old one ends on.
    Mix mix;
    mix.has_pairs = mix.has_centre = false;
    u32 free_at[grain_count];
    for (u32 k = 0; k < grain_count; ++k)
        free_at[k] = voice.grains[k].active ? render_grain(voice.grains[k], voice.sample, s, mix, 0, opxy::block_frames) : 0;
    for (u32 i = 0;;) {
        if (!voice.countdown) {
            if (Grain* g = spawn(voice, s, free_at, i, cap))
                free_at[g - voice.grains] = render_grain(*g, voice.sample, s, mix, i, opxy::block_frames);
            voice.countdown = interval(voice, s, block);
        }
        const u32 skip = voice.countdown < opxy::block_frames - i ? voice.countdown : opxy::block_frames - i;
        i += skip;
        voice.countdown -= skip;
        if (i >= opxy::block_frames) break;
    }

    // Half the output, a word a frame while every grain is centred, else as pairs.
    i32* half = mix.pairs;
    u32 count = 2 * opxy::block_frames;
    if (!mix.has_pairs) {
        if (mix.has_centre) { half = mix.centre; count = opxy::block_frames; }
        else for (u32 k = 0; k < count; ++k) half[k] = 0;
    } else if (mix.has_centre) {
        for (u32 i = 0; i < opxy::block_frames; ++i) {
            half[2 * i] = i32(u32(half[2 * i]) + u32(mix.centre[i]));
            half[2 * i + 1] = i32(u32(half[2 * i + 1]) + u32(mix.centre[i]));
        }
    }
    // The sample's own volume (unsigned Q27, unity 1 << 27), as the sampler applies it.
    if (block.region.level != opxy::sample_unity_level) {
        const i32 level = i32(block.region.level >> 16);          // Q11
        for (u32 k = 0; k < count; ++k) half[k] = (half[k] >> 11) * level;
    }
    if (count == opxy::block_frames) {   // centred: left and right are the same
        if (stereo) {
            for (u32 i = 0; i < opxy::block_frames; ++i) {
                const i32 y = full(half[i]);
                block.output[2 * i] = y;
                block.output[2 * i + 1] = y;
            }
        } else {
            for (u32 i = 0; i < opxy::block_frames; ++i) block.output[i] = full(half[i]);   // (y >> 1) + (y >> 1), y even
        }
    } else if (stereo) {
        for (u32 k = 0; k < count; ++k) block.output[k] = full(half[k]);
    } else {
        for (u32 i = 0; i < opxy::block_frames; ++i)
            block.output[i] = (full(half[2 * i]) >> 1) + (full(half[2 * i + 1]) >> 1);
    }

    // The scan moves pos's read point on, in real time, wrapping in the region.
    const i32 moved = i32(voice.scan_fraction) + s.speed * i32(opxy::block_frames);   // well inside 32 bits
    voice.scan += moved >> 16;
    voice.scan_fraction = u32(moved) & 0xffff;
    voice.scan %= i32(s.span);
}
}
