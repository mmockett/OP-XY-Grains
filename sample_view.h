// sample_view.h: a sample engine's own view of its sample, drawn as a sampler
// draws one, for the OP-XY's machine screens (SDK 1.10). Shared, byte for
// byte, by Grains, Ghost and Recur, with its artwork in tools/sample_view.py;
// written to be offered to the SDK. Only SampleOverview and the frame API, in
// 32-bit integers: no 64-bit maths, no floating point, no divides by zero.
//
// Use: copy into the machine's folder, beside tools/sample_view.py, and include
// it inside your own namespace, once per translation unit, after
// <opxy/draw.hpp> and <opxy/sample_overview.hpp>:
//     namespace mymachine { namespace sample_view {
//     #include "sample_view.h"
//     } }
// Everything is static inline or a template and it includes nothing (it uses
// the SDK's i32 and u32), so several machines can link together without
// clashing. Never edit one copy:
// change them all together (the suites' tools/check_shared.py checks).
//
// What it draws, and the contract with the artwork:
// - place_waveform: the waveform in a slot, SampleSlot's x0..x0 + width,
//   centred on y. A column for each of the overview's 96 bins (sample_columns),
//   from the bin's lowest value to its highest, both signs, so a lopsided wave
//   sits off the line and a stretch of negative values hangs below it, on a
//   faint centre line. A column's reach is the root of its value against the
//   loudest bin's (so quiet stretches still show): the loudest reaches 85% of
//   half at a gain of 0, twice as far each 10 of stock's gain steps up (-30 to
//   +20; unknown reads as 0), never past half. Columns whose middle lies
//   within the trim (begin..end) take shade lit, the rest dim; the line takes
//   shade line.
//   Nodes: line, the centre line, then the 96 columns, line + 1 .. line + 96.
//   The artwork (sample_view.py's artwork()) authors the line as a ribbon the
//   slot's width, and each column as a rectangle with a fill, 4.8 px wide
//   (sample_column_width_tenths) and four times the slot's height (8 half),
//   both about their middle; the columns are placed fill_and_stroke and
//   scaled to their span, at most a quarter, so a column the slot's height is
//   1.2 px wide and its stroke a quarter of the palette's.
//   The hide rule: a column shorter than an eighth of the slot's height (2 px
//   in a 16 px slot) is hidden and leaves the line, so a column is never drawn
//   narrower than 0.15 px, which the OP-XY would merge into a streak.
//   Rebuild and churn: the columns are worked out again only when the sample
//   (its generation, or its loading), its trim or its gain changes (key holds
//   what was last drawn; 0 draws afresh). Then at most budget columns change a
//   frame, from the left, the rest following on the next frames, or all at
//   once with snap (after a reset, or reduced motion). At rest it changes
//   nothing. With no sample assigned every node hides.
// - place_trim_marks: the trim's start and end markers, two nodes with a fill
//   authored about the slot's centre line (each machine draws its own flags),
//   at the trim's start and end, in shade.
// - place_swap_hint: while shown (Shift held, with a sample), the hint that
//   Shift and the Sample key swap the sample: the words "swap" and "shift +"
//   (two text slots, text_slot and text_slot + 1, 10 px, #CBCBCB and #7D7D7D,
//   the empty "load a sample" hint's greys) and the Sample key's icon, Grains'
//   (sample_hint_nodes nodes, first .. first + 2: its outline, its circle and
//   its bars, 14 px, authored with their own paint, which is kept). Stacked,
//   "swap" over "shift +" with the icon beside both, 52 x 18 px; in line,
//   all three on one line, 85 x 14 px; (x, y) is its top left corner.
//   Otherwise all of it waits off screen: it changes only on Shift's edges,
//   3 nodes and 2 text slots.
#pragma once

// The SDK's own sizes (on the Blackfin a u32 is an unsigned long).
using ::opxy::i32;
using ::opxy::u32;

// The overview's bins: one column each.
static const u32 sample_columns = 96;
// A column as authored (sample_view.py): this wide, in tenths of a pixel.
static const i32 sample_column_width_tenths = 48;
static const u32 sample_hint_nodes = 3;
// The hint's size, px, from its top left corner.
static const i32 sample_hint_width = 52, sample_hint_height = 18;
static const i32 sample_hint_line_width = 85, sample_hint_line_height = 14;
enum SampleHintLayout { sample_hint_stacked = 0, sample_hint_in_line = 1 };

// Where a machine puts its sample: Q10 px and palette shades.
struct SampleSlot {
    i32 x0, width;           // the slot's left end and width (the whole sample)
    i32 y, half;             // its centre line, and the furthest a column reaches from it
    u32 lit, dim, line; // columns within the trim, outside it; the centre line
};

static inline u32 sample_mix(u32 h, u32 v) {
    return h ^ (v + 0x9e3779b9u + (h << 6) + (h >> 2));
}
static inline u32 sample_root(u32 n) {
    u32 r = 0;
    for (u32 bit = 1u << 15; bit; bit >>= 1)
        if ((r + bit) * (r + bit) <= n) r += bit;
    return r;
}
// at / frames of span (Q10 px, under 2^21), without a 64-bit product.
static inline i32 sample_along(i32 span, u32 at, u32 frames) {
    while (frames >= 65536) { at >>= 1; frames >>= 1; }
    if (!frames) return 0;
    if (at > frames) at = frames;
    return i32((u32(span >> 4) * at / frames) << 4);
}
// How far a bin's value v (-32768..32767) reaches from the centre line, Q10
// px, up for positive values: the root of |v| against the loudest, 85% of half
// for the loudest at a gain of 0, scaled by louder (Q10), never past half.
static inline i32 sample_reach(i32 v, i32 loudest, i32 half, i32 louder) {
    const u32 size = u32(v < 0 ? -v : v);
    const u32 r = sample_root(size * 65536u / u32(loudest));   // 0..256
    i32 h = i32(r) * (half * 87 / 102) >> 8;
    h = (h * louder) >> 10;
    if (h > half) h = half;
    return v < 0 ? -h : h;
}
// The sample's trim, frames: begin..end within 1..frames.
static inline void sample_trim(const ::opxy::draw::SampleOverview& o, u32& frames, u32& begin, u32& end) {
    frames = o.frames ? o.frames : 1;
    end = o.end && o.end <= frames ? o.end : frames;
    begin = o.begin < end ? o.begin : 0;
}

// The waveform: the centre line (node line) and a column for each bin (the 96
// nodes after it), as the contract above says.
template <class F>
void place_waveform(F& frame, u32 line, const ::opxy::draw::SampleOverview& o, const SampleSlot& s, u32& key,
                    bool snap, u32 budget) {
    namespace d = ::opxy::draw;
    if (!o.assigned) {
        for (u32 n = line; n <= line + sample_columns; ++n) d::hide(frame, n);
        key = 0;
        return;
    }
    const i32 gain = o.gain == d::gain_unavailable ? 0 : d::clamp(o.gain, -30, 20);
    u32 frames, begin, end;
    sample_trim(o, frames, begin, end);
    const u32 want = sample_mix(sample_mix(sample_mix(sample_mix(o.generation, o.loaded), u32(gain + 30)), begin), end) | 1;
    if (key == want) return;
    d::place(frame, line, s.x0 + s.width / 2, s.y, d::unit, s.line, d::fill_and_stroke);
    // 2^(gain / 10), Q10: whole doublings, and the rest of one linearly (near enough to see).
    const i32 g = gain + 30, louder = (128 << (g / 10)) * (10 + g % 10) / 10;   // 128 at -30 .. 4096 at +20
    i32 loudest = 1;
    if (o.loaded)
        for (u32 b = 0; b < sample_columns; ++b) {
            const i32 high = o.high[b], low = -i32(o.low[b]);
            loudest = high > loudest ? high : loudest;
            loudest = low > loudest ? low : loudest;
        }
    const i32 authored = 8 * s.half > 1 ? 8 * s.half : 1;   // a column's height as authored, Q10 px
    bool done = true;
    for (u32 b = 0; b < sample_columns; ++b) {
        const u32 node = line + 1 + b;
        const i32 top = s.y - (o.loaded ? sample_reach(o.high[b], loudest, s.half, louder) : 0);
        const i32 bottom = s.y - (o.loaded ? sample_reach(o.low[b], loudest, s.half, louder) : 0);
        const i32 scale = (bottom - top) * d::unit / authored;
        const d::Node& n = frame.nodes[node];
        i32 x = d::hidden_x * d::unit, y = d::hidden_y * d::unit, at = d::unit;
        u32 shade = 0;
        d::Paint paint = d::keep;
        if (scale >= d::unit / 32) {
            // Its middle within the trim: lit.
            const u32 middle = frames / sample_columns * b + frames / sample_columns / 2;
            x = s.x0 + s.width * i32(2 * b + 1) / i32(2 * sample_columns);
            y = (top + bottom) / 2;
            at = scale;
            shade = middle >= begin && middle < end ? s.lit : s.dim;
            paint = d::fill_and_stroke;
        }
        if (n.x == x && n.y == y && n.scale == at && n.cos == d::unit && n.sin == 0 && (paint == d::keep || (n.shade == shade && n.paint == paint)))
            continue;
        if (!snap) {
            if (!budget) { done = false; continue; }
            --budget;
        }
        d::place(frame, node, x, y, at, shade, paint);
    }
    if (done) key = want;
}

// The trim's start and end markers (nodes first and first + 1), on the
// sample's start and end within the slot, in shade.
template <class F>
void place_trim_marks(F& frame, u32 first, const ::opxy::draw::SampleOverview& o, const SampleSlot& s, u32 shade) {
    namespace d = ::opxy::draw;
    u32 frames, begin, end;
    sample_trim(o, frames, begin, end);
    d::place(frame, first, s.x0 + sample_along(s.width, begin, frames), s.y, d::unit, shade, d::fill_and_stroke);
    d::place(frame, first + 1, s.x0 + sample_along(s.width, end, frames), s.y, d::unit, shade, d::fill_and_stroke);
}

// The swap hint (nodes first .. first + 2, text slots text_slot and
// text_slot + 1), its top left corner at (x, y), Q10 px, while shown.
template <class F>
void place_swap_hint(F& frame, u32 first, u32 text_slot, i32 x, i32 y, bool shown, SampleHintLayout layout) {
    namespace d = ::opxy::draw;
    // The icon is authored at (38, 4) from the corner, as stacked; in line it
    // sits at (71, 0), after the words.
    const i32 in_line = layout == sample_hint_in_line;
    const i32 ix = x + (in_line ? 33 * d::unit : 0), iy = y - (in_line ? 4 * d::unit : 0);
    for (u32 k = 0; k < sample_hint_nodes; ++k) {
        if (shown) d::place(frame, first + k, ix, iy, d::unit, 0, d::keep);
        else d::hide(frame, first + k);
    }
    const i32 swap_x = x, swap_y = y + (in_line ? 11 * d::unit : 15 * d::unit / 2);
    const i32 shift_x = x + (in_line ? 34 * d::unit : 0), shift_y = y + (in_line ? 11 * d::unit : 18 * d::unit);
    d::text(frame, text_slot, shown ? swap_x : d::hidden_x * d::unit, shown ? swap_y : d::hidden_y * d::unit, d::unit, shown ? "swap" : "");
    d::text(frame, text_slot + 1, shown ? shift_x : d::hidden_x * d::unit, shown ? shift_y : d::hidden_y * d::unit, d::unit, shown ? "shift +" : "");
}
