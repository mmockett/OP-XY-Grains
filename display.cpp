// Grains' screen (docs/SCREEN.md): a cloud of grains by the position they read
// (across) and pitch (up), over the sample's strip with pos, scan and spry.
#include "display.h"
#include "core.h"
#include "tables.h"
#include <opxy/sample_overview.hpp>

// --- sample view (to be shared) ---
// A sample engine's waveform and its Shift-time swap hint, written to be lifted
// into a header shared by the sample engines (and later the SDK): they use only
// SampleOverview and the frame API, take everything machine-specific as
// arguments, and do no 64-bit maths.
namespace grains { namespace sample_view {
using opxy::i32;
using opxy::u32;

// The square root of a 32-bit value, rounded down.
inline u32 root(u32 value) {
    u32 result = 0;
    for (u32 bit = 1u << 30; bit; bit >>= 2) {
        if (value >= result + bit) { value -= result + bit; result = (result >> 1) + bit; }
        else result >>= 1;
    }
    return result;
}

// Stock's gain steps (-30..+20, SampleOverview.gain) as a Q10 height factor:
// twice as tall each 10 steps up, 1024 at +0, 128 at -30, 4096 at +20. An
// unknown gain (gain_unavailable) reads as +0.
inline u32 gain_scale(i32 gain) {
    const i32 g = gain == opxy::draw::gain_unavailable ? 0 : gain < -30 ? -30 : gain > 20 ? 20 : gain;
    const u32 s = u32(g + 30);
    return (128u << (s / 10)) * (10 + s % 10) / 10;
}

// Columns each waveform path carries, at 64 points: its outline runs out along
// the zigzag (a point where it starts, each column's high and low, a point where
// it ends) and back along the same zigzag a few tenths of a pixel lower.
const u32 wave_columns_per_path = 15;
// How far below the zigzag its outline comes back, Q10 px: 3/8 px, so the
// outline has real area, never crosses itself, and its ends (the closing edge)
// stay over the 1/4 px the device needs.
const i32 wave_ribbon = 384;

// A column's high and low over the bins it covers within begin..end (Q16 of the
// whole), of columns across; 0 and 0 without an overview.
inline void wave_column(const opxy::draw::SampleOverview* o, u32 c, u32 columns, u32 begin, u32 span, i32& high, i32& low) {
    const u32 bins = opxy::draw::overview_bins, step = span * bins / columns;   // Q16 bins a column
    u32 b0 = (begin * bins + step * c) >> 16, b1 = (begin * bins + step * (c + 1) + 65535) >> 16;
    if (b0 >= bins) b0 = bins - 1;
    if (b1 > bins) b1 = bins;
    if (b1 <= b0) b1 = b0 + 1;
    high = 0;
    low = 0;
    for (u32 b = b0; o && b < b1; ++b) {
        if (o->high[b] > high) high = o->high[b];
        if (o->low[b] < low) low = o->low[b];
    }
}

// The sample's waveform as a sampler draws it: one column per stretch of the
// sample, a line zigzagging from each column's high (above the centre line) to
// its low (below), so a hit reads as a spike and a pad as a dense band. Each
// path is a thin closed ribbon (out along the zigzag, back 3/8 px below it):
// it never crosses itself and always has area, as the device needs.
//
// paths reshaping paths from first_path (1..4, 64 points each) share
// 15 * paths columns over w px from x, centred on y: 60 columns with 4 paths,
// each from 1.6 of the overview's 96 bins, its highest high and lowest low.
// Each path spans w / paths px and must stay within the SDK's 64 px either
// side (w <= 128 * paths). full: the half height in px of a full-scale peak at
// +0 gain; gain: stock's steps (gain_scale); half_h: the most either side, in
// px. Heights follow the level to the power 0.75, so quiet material still
// shows; a silent column keeps 0.75 px either side. begin..end: the part of the
// sample shown, Q16 of the whole (0..65536). o null (or not loaded) draws the
// flat stand-in. The points are rebuilt only when key changes (give each
// sample, gain and span its own non-zero key); otherwise only the shade can
// change, so a still waveform costs nothing.
template <class F>
void place_waveform(F& frame, u32 first_path, u32 paths, const opxy::draw::SampleOverview* o, i32 x, i32 y, i32 w,
                    i32 half_h, i32 full, i32 gain, u32 begin, u32 end, u32 shade, u32 key) {
    static_assert(F::point_count >= 4 * wave_columns_per_path + 4, "a waveform path needs 64 points");
    using opxy::draw::unit;
    using opxy::draw::Point;
    if (o && (!o->loaded || !o->frames)) o = nullptr;
    if (end > 65536) end = 65536;
    if (begin >= end) { begin = 0; end = 65536; }
    const u32 columns = wave_columns_per_path * paths, span = end - begin;
    u32 tallest = (u32(full) * unit * gain_scale(gain)) >> 10;     // Q10 px
    if (tallest > 65535) tallest = 65535;
    const i32 most = half_h * unit, least = unit * 3 / 4;
    // A level (0..32768) as its drawn height, Q10 px: tallest * (level / 32768)^0.75,
    // a gentle compression, so quiet material shows and loud still stands out.
    auto height = [&](i32 level) {
        const u32 v = u32(level < 0 ? 0 : level > 32768 ? 32768 : level);
        const u32 r = root((v * root(v << 15)) >> 15 << 15);   // 32768 (v / 32768)^0.75
        const i32 h = i32((tallest * r) >> 15);
        return h < least ? least : h > most ? most : h;
    };
    // Column c's top (its high, above the centre: negative) and bottom, Q10 px.
    auto top = [&](u32 c) { i32 high, low; wave_column(o, c, columns, begin, span, high, low); return -height(high); };
    auto bottom = [&](u32 c) { i32 high, low; wave_column(o, c, columns, begin, span, high, low); return height(-low); };
    // Q10 px from the waveform's left edge, in quarters of a column.
    auto at = [&](u32 quarter) { return i32(u32(w) * unit * quarter / (4 * columns)); };
    for (u32 p = 0; p < paths; ++p) {
        const u32 first = p * wave_columns_per_path, last = first + wave_columns_per_path;
        const i32 origin = at(4 * first + 2 * wave_columns_per_path);   // the path's middle
        typename F::path_type& path = frame.paths[first_path + p];
        if (path.key != key) {
            // Out along the zigzag. Inner ends sit halfway along the line from
            // one path's last low to the next one's first high, so the paths
            // join seamlessly; the waveform's own ends are on the centre line.
            Point out[F::point_count];
            u32 n = 0;
            out[n++] = Point{at(4 * first) - origin, first ? (bottom(first - 1) + top(first)) / 2 : 0};
            for (u32 c = first; c < last; ++c) {
                out[n++] = Point{at(4 * c + 1) - origin, top(c)};
                out[n++] = Point{at(4 * c + 3) - origin, bottom(c)};
            }
            out[n++] = Point{at(4 * last) - origin, last < columns ? (bottom(last - 1) + top(last)) / 2 : 0};
            // Back along it, 3/8 px lower; any points left over repeat the
            // last, as padding.
            for (u32 i = 0; i < n; ++i) path.points[i] = out[i];
            for (u32 i = 0; i < n; ++i) path.points[n + i] = Point{out[n - 1 - i].x, out[n - 1 - i].y + wave_ribbon};
            for (u32 i = 2 * n; i < F::point_count; ++i) path.points[i] = path.points[2 * n - 1];
            path.key = key;
        }
        opxy::draw::place(frame, first_path + p, x * unit + origin, y * unit, unit, shade, opxy::draw::stroke);
    }
}

// The hint to swap the loaded sample (Shift + the Sample key), shown only
// while Shift is held. Its words and key icon are static artwork at (x, y);
// cover_node, a filled box in the background's shade drawn just after them,
// hides them the rest of the time. Only the cover moves, on Shift's edges.
template <class F>
void place_swap_hint(F& frame, u32 cover_node, i32 x, i32 y, bool shown, u32 background = 0) {
    using opxy::draw::unit;
    if (shown) opxy::draw::hide(frame, cover_node);
    else opxy::draw::place(frame, cover_node, x * unit, y * unit, unit, background, opxy::draw::fill_and_stroke);
}
}}
// --- end of sample view ---

namespace grains {
namespace {
// The track's amp envelope as the screen follows it (SDK 1.10, input.amp),
// so the note's motion lasts as long as its sound. The times and shapes are
// stock OS 1.2.1's, from amp_curve.h (a byte-identical copy of the helper the
// other suites use): a larger attack or decay is a longer stage, but a larger
// release is a shorter one (a release of 100 is stock's shortest, about 6 ms).
// The level rises concavely over the attack, from wherever it is, falls
// exponentially over the decay and stops at the sustain level plus 1%, holds
// the sustain, a linear level, while a note is held, and fades exponentially
// over the release from wherever it was, to -66 dB. With the envelope
// unknown, or reduced motion, the level is 1 while a note is held and 0
// otherwise, as before.
namespace amp_env {
#include "amp_curve.h"
}
// A Q16 level (0 .. 2.0) times a Q15 factor, in 32 bits.
inline opxy::u32 scaled(opxy::u32 level, opxy::u32 factor) {
    return (level >> 15) * factor + (((level & 0x7fffu) * factor) >> 15);
}
// us: the time since the last frame. Stages: 0 idle, 1 attack, 2 decay, 3 release, 4 sustain.
inline void follow(Swell& s, const opxy::draw::Envelope& e, bool held, bool started, bool still, opxy::u32 us) {
    if (still || !opxy::draw::envelope_known(e)) { s.level = held ? 65536 : 0; s.stage = held ? 2 : 0; s.from = 0; return; }
    const opxy::u32 top = opxy::u32(amp_env::sustain_q15(e.sustain)) << 1;   // the sustain, Q16
    // A new note, or keys held while the envelope has let go, starts the attack from where the level is.
    if (started || (held && (s.stage == 0 || s.stage == 3))) s.stage = 1;
    if (s.stage == 1) {
        // L(u) = 2 (1 - 0.505^u): what is left below 2 shrinks by the attack's factor.
        s.level = 131072 - scaled(131072 - s.level, amp_env::attack_frame_q15(e.attack, us));
        if (s.level >= opxy::u32(amp_env::attack_peak_q15) << 1) { s.level = 65536; s.stage = 2; }
    } else if (s.stage == 2) {
        s.level = scaled(s.level, amp_env::decay_frame_q15(e.decay, us));
        if (s.level <= top + 655) { s.level = top; s.stage = 4; }
    } else if (s.stage == 4) {
        s.level = top;
    } else if (s.stage == 3) {
        s.level = scaled(s.level, amp_env::release_frame_q15(e.release, us));
        if (s.level < 33) { s.level = 0; s.stage = 0; }   // 0.0005, -66 dB
    }
    // The last key up (or a note too short to see held) lets go: the release runs from this level.
    if (!held && (s.stage == 1 || s.stage == 2 || s.stage == 4)) s.stage = s.level ? 3 : 0;
    s.from = 0;
}

using namespace opxy::draw;
using opxy::i32;
using opxy::u32;

// The sample's span across the screen, the cloud's root and the strip's centre.
const i32 left = 24, width = 432, root_y = 150, strip_y = 186;
const i32 semitone_px = 4;                  // 24 semitones over 96 px
// Shades (palette entries, black to white in 16 steps).
const u32 lane = 2, octave = 4, dim = 6, mid = 9, bright = 14;

// Until the sample's overview has been read: a flat strip, 2.4 s long.
const u32 flat_ms = 2400;

// The sample as the screen shows it: its overview, or none for the flat strip;
// the outer markers the engine plays between as fractions of the whole (Q16);
// and the lengths in ms of the whole sample and of the part between the markers.
// and its gain in stock's steps (-30..+20, #400; 0 when unknown).
struct Sample { const opxy::draw::SampleOverview* overview; u32 key; u32 begin, end; u32 whole_ms, span_ms; i32 gain; };

// part / whole in Q16, by long division, for any sample length: 65536 at the end.
u32 share(u32 part, u32 whole) {
    if (!whole || part >= whole) return whole ? 65536 : 0;
    u32 rest = part, q = 0;   // rest < whole < 2^31, so rest << 1 stays in 32 bits
    for (u32 bit = 0; bit < 16; ++bit) {
        rest <<= 1;
        q <<= 1;
        if (rest >= whole) { rest -= whole; q |= 1; }
    }
    return q;
}

Sample read_sample(const opxy::draw::SampleOverview& o) {
    if (!o.loaded || !o.frames || !o.rate) return Sample{nullptr, 1, 0, 65536, flat_ms, flat_ms, 0};
    Sample s;
    s.overview = &o;
    s.gain = o.gain == opxy::draw::gain_unavailable ? 0 : o.gain < -30 ? -30 : o.gain > 20 ? 20 : o.gain;
    s.key = (o.generation << 8) | (u32(s.gain + 30) << 2) | 2;   // a new sample or gain; never the stand-in's 1
    // Fractions of the whole, exact at either end however long the sample.
    s.begin = o.begin < o.frames ? share(o.begin, o.frames) : 0;
    s.end = o.end && o.end <= o.frames ? share(o.end, o.frames) : 65536;
    if (s.end <= s.begin) { s.begin = 0; s.end = 65536; }
    s.whole_ms = o.frames / (o.rate / 1000 ? o.rate / 1000 : 1);
    s.span_ms = s.whole_ms * ((s.end - s.begin) >> 6) >> 10;
    if (!s.whole_ms) s.whole_ms = 1;
    if (!s.span_ms) s.span_ms = 1;
    return s;
}

// A place within the markers (Q16 of the part played) as an x on the strip.
i32 strip_x(const Sample& s, u32 within) {
    const u32 at = s.begin + ((s.end - s.begin) >> 4) * (within & 0xffff) / 4096;
    return left + i32(at * u32(width) >> 16);
}

// Each chord's notes in semitones as the lanes show them (harm rounds its
// partials to the nearest semitone and shows those within two octaves).
const opxy::u8 lane_notes[7][8] = {{0}, {0}, {0, 12, 24}, {0, 7, 12, 19, 24},
                                   {0, 4, 7, 12, 16, 19, 24}, {0, 3, 7, 12, 15, 19, 24}, {0, 12, 19, 24}};
const opxy::u8 lane_counts[7] = {1, 1, 3, 5, 7, 7, 4};
const char* const chord_names[7] = {"off", "dtun", "oct", "5th", "maj", "min", "harm"};
const char* const mode_names[mode_count] = {"cloud", "sync", "osc", "rcld", "rsync", "rosc"};
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
u32 overlap8(i32 v) { return 256 + ((level(v) * (grain_count - 1) * 256) >> 16); }
// Where shpe puts a grain's window peak, Q16 through the grain, as the engine does:
// late at 0 (a swell), the middle at 0.5, early at 1 (a pluck).
u32 window_peak(i32 v) { return 2048 + (((65535 - level(v)) * 61440u) >> 16); }

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

// Shift (resources.stockShift): encoders 1, 2 and 4 turn the stock sampler's
// start, both markers together (the window, keeping its length) and end;
// encoder 3 its gain.
const char* const shift_titles[4] = {"start", "window", "gain", "end"};

// Frames as seconds, within the 53 px a value has: 0.62s, 12.4s, 123s, and
// from 1000 s whole minutes, 17m.
void seconds(char* out, u32 frames, u32 rate) {
    const u32 per = rate / 100 ? rate / 100 : 1, hundredths = frames / per;
    if (hundredths < 1000) { out = put(out, hundredths / 100); *out++ = '.'; out = put(out, hundredths / 10 % 10); out = put(out, hundredths % 10); }
    else if (hundredths < 10000) { out = put(out, hundredths / 100); *out++ = '.'; out = put(out, hundredths / 10 % 10); }
    else if (hundredths < 100000) out = put(out, hundredths / 100);
    else { out = put(out, hundredths / 6000); *out++ = 'm'; *out = '\0'; return; }
    *out++ = 's';
    *out = '\0';
}

// While Shift is held, the row the encoders turn names what Shift edits there,
// as the stock header does: the sampler's start and end, at their times in the
// sample, the window, at its length, and the gain in stock's steps, +0, -12 or
// +20 (with no gain known, gain_unavailable, its cell is blank).
void shift_row(const Input& input, const opxy::u8 (&pages)[2][4], const opxy::draw::SampleOverview& o,
               const char* (&titles)[8], char (&values)[8][header_capacity]) {
    if (!input.shift) return;
    u32 page = 0;   // as page_header finds it
    for (u32 col = 0; col < 4; ++col)
        if (pages[1][col] != pages[0][col] && input.shown[col] == pages[1][col]) page = 1;
    const bool known = o.loaded && o.frames && o.rate;
    const u32 end = known && o.end && o.end <= o.frames ? o.end : o.frames, begin = o.begin < end ? o.begin : 0;
    const u32 marks[4] = {begin, end - begin, 0, end};
    for (u32 col = 0; col < 4; ++col) {
        if (!shift_titles[col]) continue;
        const u32 slot = pages[page][col];
        titles[slot] = shift_titles[col];
        if (col == 2) {
            values[slot][0] = '\0';
            if (o.gain == opxy::draw::gain_unavailable) { titles[slot] = ""; continue; }
            values[slot][0] = o.gain < 0 ? '-' : '+';
            put(values[slot] + 1, u32(o.gain < 0 ? -o.gain : o.gain));
        } else if (known) seconds(values[slot], marks[col], o.rate);
        else copy(values[slot], header_capacity, "-");
    }
}

// The stock sampler's start and end markers on the strip: shown while Shift
// edits them, and whenever they trim the sample, so pos's span reads.
void place_trim(Frame& frame, const Sample& sample, bool shift) {
    const opxy::draw::SampleOverview* o = sample.overview;
    const bool trimmed = o && ((o->begin && o->begin < o->frames) || (o->end && o->end < o->frames));
    if (!shift && !trimmed) { hide(frame, trim_first); hide(frame, trim_first + 1); return; }
    const u32 marks[2] = {sample.begin, sample.end};
    for (u32 i = 0; i < 2; ++i)
        place(frame, trim_first + i, (left * unit) + i32((u32(width) * unit * (marks[i] >> 4)) >> 12), strip_y * unit, unit,
              shift ? bright : mid, fill_and_stroke);
}

// The strip: the whole sample's waveform (sample_view::place_waveform), its
// height scaled by the gain, twice as tall each 10 of stock's steps up: a
// full-scale peak 13 px a side at +0, at most 20 px a side.
void place_strip(Frame& frame, const Sample& sample, bool held) {
    sample_view::place_waveform(frame, strip_first, strip_paths, sample.overview, left, strip_y, width, 20, 13,
                                sample.gain, 0, 65536, held ? mid : dim, sample.key);
}

// The swap hint (Shift + the Sample key) above the strip's right end, while
// Shift is held on a loaded sample.
const i32 swap_x = 355, swap_y = 151;

// One edge of the window: an authored 64 px ribbon from the peak, turned to
// pass through its foot, (run, down) in 1/64 px away. It runs on off the bottom
// of the screen, so it's never scaled and keeps its width.
void place_edge(Frame& frame, u32 node, i32 x, i32 y, i32 run, i32 down, u32 shade) {
    const i32 length = i32(sample_view::root(u32(run * run + down * down)));
    place(frame, node, x, y, unit, shade, stroke);
    const Turn edge = {run * unit / length, down * unit / length};
    turn(frame, node, edge);
}

// shpe: the grain's window, under pos. Its feet are fixed either side of pos,
// on the screen's bottom edge, and its peak slides between them.
void place_window(Frame& frame, i32 px, i32 shpe, bool held) {
    const i32 half = 24, height = 13;
    const i32 across = i32((u32(2 * half) * 64 * window_peak(shpe)) >> 16);   // 1/64 px from the left foot
    const i32 x = (px - half) * unit + across * (unit / 64), y = (220 - height) * unit;
    const u32 shade = held ? mid : dim;
    place_edge(frame, window_rise, x, y, -across, height * 64, shade);
    place_edge(frame, window_fall, x, y, 2 * half * 64 - across, height * 64, shade);
}

// scan is the read point's offset from pos while a note is held (Q16 of the
// sample), so the marks follow it.
void place_marks(Frame& frame, const Input& input, const Sample& sample, u32 scan, bool held) {
    const i32 px = strip_x(sample, level(input.slots[kPos]) + scan);
    place(frame, pos_node, px * unit, strip_y * unit, unit, bright, fill_and_stroke);
    const i32 rate = scan_rate(input.slots[kScan]);
    // The arrow grows with speed: 0.6x its length near the middle to 1.4x at 2x.
    const i32 grow = unit * 6 / 10 + i32(u32(rate < 0 ? -rate : rate) * 4 / 10 * u32(unit) >> 17);
    if (rate) {
        place(frame, scan_arrow, px * unit, (strip_y - 21) * unit, grow, bright, stroke);
        if (rate < 0) { const Turn back = {-unit, 0}; turn(frame, scan_arrow, back); }
    } else {
        hide(frame, scan_arrow);
    }
    if (!rate) place(frame, scan_frozen, px * unit, (strip_y - 21) * unit, unit, bright, stroke); else hide(frame, scan_frozen);
    // spry: the spread grains start within, either side of pos.
    const u32 span_px = u32(width) * ((sample.end - sample.begin) >> 4) >> 12;
    const i32 spread = i32(level(input.slots[kSpry]) * (span_px / 2) >> 16);
    if (spread) {
        const i32 first = strip_x(sample, 0), last = strip_x(sample, 0xffff);
        const i32 lo = px - spread < first ? first : px - spread, hi = px + spread > last ? last : px + spread;
        place(frame, spray_first, lo * unit, strip_y * unit, unit, dim, fill_and_stroke);
        place(frame, spray_first + 1, hi * unit, strip_y * unit, unit, dim, fill_and_stroke);
    } else {
        hide(frame, spray_first);
        hide(frame, spray_first + 1);
    }
    place_window(frame, px, input.slots[kShpe], held);
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
    const u32 mode = choice(input.slots[kMode], mode_count) % 3, size = size_ms(input.slots[kSize]);
    const u32 overlap = overlap8(input.slots[kDens]);
    u32 ms;
    if (mode == kSync) ms = 1000u >> ((overlap - 256) * 5 / ((grain_count - 1) * 256 + 1));
    else {
        ms = size * 256 / overlap;
        if (mode == kCloud) ms = ms * ((next_random(state) >> 16) + 32768) >> 16;
    }
    return ms < 40 ? 40 : ms;   // the screen shows at most eight at a time
}

void spawn(DisplayState& state, const Input& input) {
    ShownGrain* g = 0;
    for (u32 i = 0; i < grain_shown && !g; ++i) if (!state.grains[i].life) g = &state.grains[i];
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
    // Past half spry, grains drift off their note, up to 50 cents (2 px) either way.
    const u32 chaos = level(input.slots[kSpry]), jitter = chaos > 32768 ? (chaos - 32768) * 2 : 0;
    g->drift = static_cast<signed char>(jitter ? i32(next_random(state) % 5) - 2 : 0);
    if (jitter < 32768 && (next_random(state) & 1)) g->drift = 0;   // below three quarters, about half do
}

void place_grains(DisplayState& state, const Input& input, Frame& frame, const Sample& sample, bool moving) {
    const u32 chord = choice(input.slots[kChrd], 7);
    const u32 size = size_ms(input.slots[kSize]);
    const u32 length_px = u32(width) * size / sample.whole_ms;
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
            if (!g.life) { hide(frame, node); continue; }
            // Brightest at its window's peak: dim to bright to dim, as shpe shapes it.
            const u32 peak = u32(g.life) * window_peak(input.slots[kShpe]) >> 16;
            const u32 window = g.age < peak ? u32(g.age) * 11 / peak : u32(g.life - g.age) * 11 / (g.life - peak);
            shade = 3 + ((window * state.swell.level + 32768) >> 16);   // as loud as the envelope lets it
        }
        const u32 note = lane_notes[chord][g.note % lane_counts[chord]];
        i32 y = root_y - i32(note) * semitone_px;
        if (chord == 1) y += i32(g.note % 5) - 2;               // dtun scatters around the root
        if (moving) y -= g.drift;
        const i32 x = strip_x(sample, g.position);
        place(frame, node, x * unit, y * unit, i32(scale), shade > 15 ? 15 : shade, stroke);
    }
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    for (u32 i = 0; i < grain_shown; ++i) state.grains[i].life = 0;
    state.scan = 0;
    state.next = 0;
    state.random = 0x2545f491u;
    state.spawned = 0;
    state.track = ~0u;
    state.swell = Swell{0, 0, 0};
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    const opxy::draw::SampleOverview& overview = *opxy_sample_overview();
    const Sample sample = read_sample(overview);
    // Header: titles and real-unit captions for the two pages.
    const char* own[8] = {"pos", "scan", "size", "dens", "spry", "chrd", "shpe", "mode"};
    char values[8][header_capacity] = {};
    const u32 mode = choice(input.slots[kMode], mode_count), chord = choice(input.slots[kChrd], 7);
    header_amount(values[kPos], header_capacity, input.slots[kPos]);
    signed_tenths(values[kScan], scan_rate(input.slots[kScan]) * 10 / 65536);
    const u32 size = size_ms(input.slots[kSize]);
    if (size < 1000) put(values[kSize], size)[0] = '\0';   // ms
    else { tenths(values[kSize], size / 100); copy(values[kSize] + 3, 2, "s"); }
    const u32 overlap = overlap8(input.slots[kDens]);
    if (mode % 3 == kSync) copy(values[kDens], header_capacity, divisions[(overlap - 256) * 5 / ((grain_count - 1) * 256 + 1)]);
    else tenths(values[kDens], overlap * 10 >> 8);
    header_amount(values[kSpry], header_capacity, input.slots[kSpry]);
    copy(values[kChrd], header_capacity, chord_names[chord]);
    header_amount(values[kShpe], header_capacity, input.slots[kShpe]);
    copy(values[kMode], header_capacity, mode_names[mode]);
    const opxy::u8 pages[2][4] = {{kPos, kScan, kSize, kDens}, {kSpry, kChrd, kShpe, kMode}};
    shift_row(input, pages, overview, own, values);
    const char* const titles[8] = {own[0], own[1], own[2], own[3], own[4], own[5], own[6], own[7]};
    page_header(state.header, input, frame, titles, values, header_first, pages);

    // A track change starts afresh; grains move only while a note sounds:
    // held, or in its amp envelope's release.
    if (state.track != input.track) {
        for (u32 i = 0; i < grain_shown; ++i) state.grains[i].life = 0;
        state.scan = 0;
        state.track = input.track;
    }
    const NoteEvents events = state.notes.update(input);
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    if (events.hold_started) { state.scan = 0; state.next = 0; state.spawned = 0; }
    const u32 ms = u32(clamp(input.elapsed, 0, 102)) * 1000 >> 10;
    follow(state.swell, input.amp, held, events.hold_started, input.reduced_motion != 0, u32(clamp(input.elapsed, 0, 256)) * 15625u >> 4);
    const bool sounding = held || state.swell.level;
    const bool moving = sounding && !input.reduced_motion;
    if (moving) {
        // The scan moves the read point on at its rate, through the part played.
        const i32 rate = scan_rate(input.slots[kScan]);
        state.scan += u32(rate / i32(sample.span_ms) * i32(ms));
        state.scan &= 0xffff;
        for (u32 i = 0; i < grain_shown; ++i) {
            ShownGrain& g = state.grains[i];
            if (!g.life) continue;
            g.age = opxy::u16(g.age + ms);
            if (g.age >= g.life) g.life = 0;
        }
        state.next = state.next > ms ? state.next - ms : 0;
        while (!state.next) {
            spawn(state, input);
            state.next = interval_ms(state, input);
        }
    } else if (!sounding) {
        for (u32 i = 0; i < grain_shown; ++i) state.grains[i].life = 0;
    }

    // With no sample, the stage gives way to how to load one.
    if (!overview.assigned) {
        place_strip(frame, sample, false);   // every path keeps a shape, shown or not
        place(frame, cover_stage, 0, 0, unit, 0, fill_and_stroke);
        hide(frame, cover_hint);
        for (u32 n = 0; n < grain_first + dash_lengths * grain_shown; ++n)
            if (n < header_first || n >= pos_node) hide(frame, n);
        for (u32 i = 0; i < 3; ++i) hide(frame, octave_first + i);
        for (u32 i = 0; i < 2; ++i) hide(frame, trim_first + i);
        hide(frame, centre_line);
        sample_view::place_swap_hint(frame, swap_cover, swap_x, swap_y, false);
        return;
    }
    hide(frame, cover_stage);
    place(frame, cover_hint, 0, 0, unit, 0, fill_and_stroke);
    for (u32 i = 0; i < 3; ++i) place(frame, octave_first + i, 0, 0, unit, octave, stroke);
    place(frame, centre_line, 0, 0, unit, octave, stroke);
    place_strip(frame, sample, sounding);
    place_marks(frame, input, sample, moving ? state.scan : 0, sounding);
    place_lanes(frame, chord);
    place_grains(state, input, frame, sample, moving);
    place_trim(frame, sample, input.shift != 0);
    sample_view::place_swap_hint(frame, swap_cover, swap_x, swap_y, input.shift && sample.overview);
}
}
