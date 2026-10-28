// Grains' screen (docs/SCREEN.md): a cloud of grains by the position they read
// (across) and pitch (up), over the sample's strip with pos, scan and spry.
#include "display.h"
#include "core.h"
#include "tables.h"

namespace grains {
namespace {
using namespace opxy::draw;
using opxy::i32;
using opxy::u32;

// The sample's span across the screen, the cloud's root and the strip's centre.
const i32 left = 24, width = 432, root_y = 150, strip_y = 186;
const i32 semitone_px = 4;                  // 24 semitones over 96 px
// Shades (palette entries, black to white in 16 steps).
const u32 lane = 2, dim = 6, mid = 9, bright = 14;

// Until the SDK gives the screen the loaded sample's overview and length, a
// stand-in: a spoken phrase's amplitude in 64 bins, 2.4 s long.
const opxy::u8 placeholder[64] = {
    20, 25, 36, 56, 85, 124, 168, 209, 237, 245, 230, 198, 157, 119, 91, 78,
    79, 93, 115, 142, 169, 191, 204, 206, 196, 177, 151, 123, 97, 75, 61, 56,
    62, 79, 107, 144, 184, 221, 247, 255, 249, 224, 188, 147, 108, 77, 56, 47,
    49, 64, 88, 115, 136, 143, 133, 110, 81, 55, 36, 25, 19, 17, 16, 15};
const u32 sample_ms = 2400;

// Each chord's notes in semitones as the lanes show them (harm rounds its
// partials to the nearest semitone and shows those within two octaves).
const opxy::u8 lane_notes[7][8] = {{0}, {0}, {0, 12, 24}, {0, 7, 12, 19, 24},
                                   {0, 4, 7, 12, 16, 19, 24}, {0, 3, 7, 12, 15, 19, 24}, {0, 12, 19, 24}};
const opxy::u8 lane_counts[7] = {1, 1, 3, 5, 7, 7, 4};
const char* const chord_names[7] = {"off", "dtun", "oct", "5th", "maj", "min", "harm"};
const char* const mode_names[3] = {"cloud", "sync", "osc"};
const char* const divisions[5] = {"1/2", "1/4", "1/8", "1/16", "1/32"};

u32 level(i32 v) { return v < 0 ? 0u : u32(v) >> 15; }          // 0..65535
u32 choice(i32 v, u32 count) { return (level(v) * count) >> 16; }

// scan as a signed rate in Q16 of real time, as the engine reads it.
i32 scan_rate(i32 v) {
    i32 centred = i32(level(v)) - 32768;
    centred = centred > 128 ? centred - 128 : centred < -128 ? centred + 128 : 0;
    const u32 magnitude = u32(centred < 0 ? -centred : centred);
    const i32 rate = i32(magnitude * magnitude / 16256 * 2);     // 65536 is 1.0x
    return centred < 0 ? -rate : rate;
}
u32 size_ms(i32 v) {
    const u32 position = level(v) << 5, index = position >> 16, fraction = position & 0xffff;
    const u32 samples = index >= 32 ? size_table[32] :
        size_table[index] + ((size_table[index + 1] - size_table[index]) * fraction >> 16);
    return samples * 10 / 441;
}
u32 overlap8(i32 v) { return 256 + ((level(v) * 7 * 256) >> 16); }

// Captions.
char* put(char* out, u32 value) {
    char digits[10];
    u32 n = 0;
    do { digits[n++] = char('0' + value % 10); value /= 10; } while (value);
    while (n) *out++ = digits[--n];
    return out;
}
// Tenths as "1.5"; signed as "+1.5", "-2.0" or "0".
void tenths(char* out, u32 value) {
    out = put(out, value / 10);
    *out++ = '.';
    *out++ = char('0' + value % 10);
    *out = '\0';
}
void signed_tenths(char* out, i32 value) {
    if (!value) { copy(out, header_capacity, "0"); return; }
    *out = value < 0 ? '-' : '+';
    tenths(out + 1, u32(value < 0 ? -value : value));
}

void place_strip(Frame& frame, bool held) {
    for (u32 h = 0; h < 2; ++h) {
        Frame::path_type& path = frame.paths[h];
        if (path.key != 1) {
            for (u32 i = 0; i < 32; ++i) {
                // 32 bins out along the top, back along the bottom: a closed ribbon at scale 2.
                const i32 x = i32(i) * 108 * unit / 31 - 54 * unit;
                const i32 half = i32(placeholder[32 * h + i]) * 13 * unit / 510 + 307;
                path.points[i].x = x;
                path.points[i].y = -half;
                path.points[63 - i].x = x;
                path.points[63 - i].y = half;
            }
            path.key = 1;
        }
        place(frame, h, (left + 108 + 216 * i32(h)) * unit, strip_y * unit, 2 * unit, held ? mid : dim, stroke);
    }
}

// scan is the read point's offset from pos while a note is held (Q16 of the
// sample), so the marks follow it.
void place_marks(Frame& frame, const Input& input, u32 scan) {
    const i32 px = left + i32(((level(input.slots[kPos]) + scan) & 0xffff) * u32(width) >> 16);
    place(frame, pos_node, px * unit, strip_y * unit, unit, bright, fill_and_stroke);
    const i32 rate = scan_rate(input.slots[kScan]);
    // The arrow grows with speed: 0.6x its length near the middle to 1.4x at 2x.
    const i32 grow = unit * 6 / 10 + i32(u32(rate < 0 ? -rate : rate) * 4 / 10 * u32(unit) >> 17);
    if (rate > 0) place(frame, scan_forward, px * unit, (strip_y - 21) * unit, grow, bright, stroke); else hide(frame, scan_forward);
    if (rate < 0) place(frame, scan_back, px * unit, (strip_y - 21) * unit, grow, bright, stroke); else hide(frame, scan_back);
    if (!rate) place(frame, scan_frozen, px * unit, (strip_y - 21) * unit, unit, bright, stroke); else hide(frame, scan_frozen);
    // spry: the spread grains start within, either side of pos.
    const i32 spread = i32(level(input.slots[kSpry]) * u32(width / 2) >> 16);
    if (spread) {
        const i32 lo = px - spread < left ? left : px - spread, hi = px + spread > left + width ? left + width : px + spread;
        place(frame, spray_first, lo * unit, strip_y * unit, unit, dim, fill_and_stroke);
        place(frame, spray_first + 1, hi * unit, strip_y * unit, unit, dim, fill_and_stroke);
    } else {
        hide(frame, spray_first);
        hide(frame, spray_first + 1);
    }
}

void place_lanes(Frame& frame, u32 chord) {
    u32 used = 0;
    for (u32 i = 0; i < lane_counts[chord]; ++i) {
        const u32 note = lane_notes[chord][i];
        if (note % 12 == 0 || used >= lane_count) continue;     // octaves are drawn in the artwork
        place(frame, lane_first + used++, left * unit, (root_y - i32(note) * semitone_px) * unit, unit, lane, stroke);
    }
    while (used < lane_count) hide(frame, lane_first + used++);
}

u32 next_random(DisplayState& state) {
    u32 x = state.random ? state.random : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return state.random = x;
}

// ms until the next grain, as the engine spaces them (sync at 120 BPM: the
// screen has no tempo).
u32 interval_ms(DisplayState& state, const Input& input) {
    const u32 mode = choice(input.slots[kMode], 3), size = size_ms(input.slots[kSize]);
    const u32 overlap = overlap8(input.slots[kDens]);
    u32 ms;
    if (mode == kSync) ms = 1000u >> ((overlap - 256) * 5 / (7 * 256 + 1));
    else {
        ms = size * 256 / overlap;
        if (mode == kCloud) ms = ms * ((next_random(state) >> 16) + 32768) >> 16;
    }
    return ms < 40 ? 40 : ms;   // the screen shows at most eight at a time
}

void spawn(DisplayState& state, const Input& input) {
    ShownGrain* g = 0;
    for (u32 i = 0; i < grain_shown && !g; ++i) if (!state.grains[i].live) g = &state.grains[i];
    if (!g) return;
    const u32 pos = level(input.slots[kPos]), spray = level(input.slots[kSpry]) >> 1;
    i32 at = i32(pos) + i32(state.scan >> 0);
    if (spray) at += i32(next_random(state) % (2 * spray + 1)) - i32(spray);
    at &= 0xffff;   // the scan wraps around the sample, as audio's does
    g->position = opxy::u16(at);
    g->note = opxy::u8(state.spawned++);
    const u32 size = size_ms(input.slots[kSize]);
    g->life = opxy::u16(size < 150 ? 150 : size > 1500 ? 1500 : size);   // long enough to see
    g->age = 0;
    g->live = 1;
}

void place_grains(DisplayState& state, const Input& input, Frame& frame, bool moving) {
    const u32 chord = choice(input.slots[kChrd], 7);
    const u32 size = size_ms(input.slots[kSize]);
    const u32 length_px = u32(width) * size / sample_ms;
    const u32 set = length_px < 10 ? 0 : length_px < 28 ? 1 : 2;
    const u32 authored[3] = {6, 16, 40};
    u32 scale = length_px * u32(unit) / authored[set];
    scale = scale < u32(unit) * 6 / 10 ? u32(unit) * 6 / 10 : scale > u32(unit) * 16 / 10 ? u32(unit) * 16 / 10 : scale;
    for (u32 i = 0; i < dash_lengths * grain_shown; ++i) if (i / grain_shown != set) hide(frame, grain_first + i);
    for (u32 i = 0; i < grain_shown; ++i) {
        const u32 node = grain_first + set * grain_shown + i;
        ShownGrain g = state.grains[i];
        u32 shade = 0;
        if (!moving) {
            // At rest: one still grain per chord note at pos.
            if (i >= lane_counts[chord]) { hide(frame, node); continue; }
            g.position = opxy::u16(level(input.slots[kPos]));
            g.note = opxy::u8(i);
            shade = 5;
        } else {
            if (!g.live) { hide(frame, node); continue; }
            // Brightest at the middle of its window: a triangle, dim to bright to dim.
            const u32 half = g.life / 2, t = g.age < half ? g.age : g.life - g.age;
            shade = 3 + (half ? t * 11 / half : 0);
        }
        const u32 note = lane_notes[chord][g.note % lane_counts[chord]];
        i32 y = root_y - i32(note) * semitone_px;
        if (chord == 1) y += i32(g.note % 5) - 2;               // dtun scatters around the root
        const i32 x = left + i32(u32(g.position) * u32(width) >> 16);
        place(frame, node, x * unit, y * unit, i32(scale), shade > 15 ? 15 : shade, stroke);
    }
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    for (u32 i = 0; i < grain_shown; ++i) state.grains[i].live = 0;
    state.scan = 0;
    state.next = 0;
    state.random = 0x2545f491u;
    state.spawned = 0;
    state.track = ~0u;
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    // Header: titles and real-unit captions for the two pages.
    const char* const titles[8] = {"pos", "scan", "size", "dens", "spry", "chrd", "shpe", "mode"};
    char values[8][header_capacity] = {};
    const u32 mode = choice(input.slots[kMode], 3), chord = choice(input.slots[kChrd], 7);
    header_amount(values[kPos], header_capacity, input.slots[kPos]);
    signed_tenths(values[kScan], scan_rate(input.slots[kScan]) * 10 / 65536);
    const u32 size = size_ms(input.slots[kSize]);
    if (size < 1000) put(values[kSize], size)[0] = '\0';   // ms
    else { tenths(values[kSize], size / 100); copy(values[kSize] + 3, 2, "s"); }
    const u32 overlap = overlap8(input.slots[kDens]);
    if (mode == kSync) copy(values[kDens], header_capacity, divisions[(overlap - 256) * 5 / (7 * 256 + 1)]);
    else tenths(values[kDens], overlap * 10 >> 8);
    header_amount(values[kSpry], header_capacity, input.slots[kSpry]);
    copy(values[kChrd], header_capacity, chord_names[chord]);
    header_amount(values[kShpe], header_capacity, input.slots[kShpe]);
    copy(values[kMode], header_capacity, mode_names[mode]);
    const opxy::u8 pages[2][4] = {{kPos, kScan, kSize, kDens}, {kSpry, kChrd, kShpe, kMode}};
    page_header(state.header, input, frame, titles, values, header_first, pages);

    // A track change starts afresh; grains move only while a note is held.
    if (state.track != input.track) {
        for (u32 i = 0; i < grain_shown; ++i) state.grains[i].live = 0;
        state.scan = 0;
        state.track = input.track;
    }
    const NoteEvents events = state.notes.update(input);
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    if (events.hold_started) { state.scan = 0; state.next = 0; state.spawned = 0; }
    const bool moving = held && !input.reduced_motion;
    if (moving) {
        const u32 ms = u32(clamp(input.elapsed, 0, 102)) * 1000 >> 10;
        // The scan moves the read point on at its rate, through a 2.4 s sample.
        const i32 rate = scan_rate(input.slots[kScan]);
        state.scan += u32(rate / i32(sample_ms) * i32(ms));
        state.scan &= 0xffff;
        for (u32 i = 0; i < grain_shown; ++i) {
            ShownGrain& g = state.grains[i];
            if (!g.live) continue;
            g.age = opxy::u16(g.age + ms);
            if (g.age >= g.life) g.live = 0;
        }
        state.next = state.next > ms ? state.next - ms : 0;
        while (!state.next) {
            spawn(state, input);
            state.next = interval_ms(state, input);
        }
    } else if (!held) {
        for (u32 i = 0; i < grain_shown; ++i) state.grains[i].live = 0;
    }

    place_strip(frame, held);
    place_marks(frame, input, moving ? state.scan : 0);
    place_lanes(frame, chord);
    place_grains(state, input, frame, moving);
}
}
