// Grains' screen checks, compiled for the host and run by
// python sdk.py test --machine <folder>: the sample's waveform on the strip, the
// swap hint Shift shows, what Shift shows now that it edits the stock sampler's
// markers (resources.stockShift), the markers on the strip, and that every
// caption Shift's row can show fits its column on the device.
#include "../display.cpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace {
using opxy::i32;
using opxy::u32;
opxy::draw::SampleOverview overview;
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}
i32 at(double value) { return i32(value * 2147483647.0); }

// Univers Next Pro Light's advances in thousandths of an em, for the
// characters these captions use (the device lays text out by advance). A
// value at 20 px must fit 53 px, a title at 10 px 56 px.
struct Advance { char c; int width; };
const Advance advances[] = {
    {'+', 676}, {'-', 329}, {'.', 280}, {'0', 560}, {'1', 560}, {'2', 560}, {'3', 560}, {'4', 560}, {'5', 560}, {'6', 560},
    {'7', 560}, {'8', 560}, {'9', 560}, {'a', 536}, {'d', 581}, {'e', 532}, {'g', 589}, {'i', 235}, {'m', 894}, {'n', 574},
    {'o', 571}, {'r', 345}, {'s', 480}, {'t', 325}, {'w', 820}};
double caption_width(const char* text, int px) {
    int total = 0;
    for (; *text; ++text) {
        int w = -100000;
        for (const Advance& a : advances) if (a.c == *text) w = a.width;
        total += w;
    }
    return total * px / 1000.0;
}

struct Screen {
    grains::DisplayState state;
    grains::Frame frame;
    opxy::draw::Input input;
    Screen() : state(), frame(), input() {
        const double defaults[8] = {0, 0.853553, 0.392472, 0.157895, 0, 0.071429, 0.5, 0.083333};
        for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
        for (u32 i = 0; i < 4; ++i) input.shown[i] = opxy::u8(i);
        input.track = 1;
        input.elapsed = 17;
        grains::display_reset(state);
    }
    // Frames enough for the header to settle.
    void run(u32 frames = 20) {
        for (u32 f = 0; f < frames; ++f) {
            grains::display(state, input, frame);
            if (!opxy::draw::valid(frame)) { std::printf("FAIL an invalid frame\n"); ++failures; return; }
        }
    }
    bool shown(u32 node) const { return frame.nodes[node].x != opxy::draw::hidden_x * opxy::draw::unit; }
    i32 x(u32 node) const { return frame.nodes[node].x / opxy::draw::unit; }
    const char* text(u32 slot) const { return frame.texts[slot].chars; }
};
}
const opxy::draw::SampleOverview* opxy_sample_overview() { return &overview; }
namespace {
// At rest the screen shows one still grain per chord note at pos (one, chrd off).
u32 lane_count_at_rest(const Screen&) { return 1; }
}

int main() {
    overview.assigned = overview.loaded = 1;
    overview.generation = 1;
    overview.frames = 4 * 44100; overview.rate = 44100; overview.begin = 0; overview.end = overview.frames;
    for (u32 b = 0; b < 96; ++b) { const opxy::i16 peak = opxy::i16((40 + (b * 37) % 200) << 7); overview.high[b] = peak; overview.low[b] = opxy::i16(-peak); }
    {   // The whole sample, Shift up: no markers, and the machine's own titles.
        Screen s;
        s.run();
        check(!s.shown(grains::trim_first) && !s.shown(grains::trim_first + 1), "an untrimmed sample shows no markers");
        check(!std::strcmp(s.text(0), "pos") && !std::strcmp(s.text(3), "dens"), "the header names pos..dens without Shift");
        // Shift held: the markers at the sample's ends, and the row names them.
        s.input.shift = 1;
        s.run();
        check(s.shown(grains::trim_first) && s.x(grains::trim_first) == 24, "Shift shows the start marker at the strip's left");
        check(s.shown(grains::trim_first + 1) && s.x(grains::trim_first + 1) == 456, "Shift shows the end marker at its right");
        check(!std::strcmp(s.text(0), "start") && !std::strcmp(s.text(1), "window") && !std::strcmp(s.text(2), "gain") &&
              !std::strcmp(s.text(3), "end"), "Shift's row names start, window, gain and end");
        check(!std::strcmp(s.text(4), "0.00s") && !std::strcmp(s.text(5), "4.00s") && !std::strcmp(s.text(6), "+0") &&
              !std::strcmp(s.text(7), "4.00s"), "Shift's row gives the start's and end's times, the window's length and the gain");
        check(!std::strcmp(s.frame.header[0], "0.00s") && !std::strcmp(s.frame.header[3], "4.00s"), "the stock header gets them too");
    }
    {   // Trimmed to 1 s .. 3 s: the markers stay, a step dimmer, and pos at 0 reads from the start marker.
        overview.begin = 44100; overview.end = 3 * 44100;
        Screen s;
        s.run();
        check(s.shown(grains::trim_first) && std::abs(s.x(grains::trim_first) - 132) <= 1, "the start marker at 1 s of 4");
        check(s.shown(grains::trim_first + 1) && std::abs(s.x(grains::trim_first + 1) - 348) <= 1, "the end marker at 3 s of 4");
        check(s.frame.nodes[grains::trim_first].shade < 14, "the markers are dimmer without Shift");
        check(std::abs(s.x(grains::pos_node) - s.x(grains::trim_first)) <= 1, "pos at 0 sits on the start marker");
        // On the second page Shift's row is that page's, its third the gain.
        for (u32 i = 0; i < 4; ++i) s.input.shown[i] = opxy::u8(4 + i);
        s.input.shift = 1;
        s.run();
        check(!std::strcmp(s.text(0), "start") && !std::strcmp(s.text(2), "gain") && !std::strcmp(s.text(3), "end"),
              "page 2's Shift row names start, window, gain and end");
        check(!std::strcmp(s.text(4), "1.00s") && !std::strcmp(s.text(5), "2.00s") && !std::strcmp(s.text(7), "3.00s"),
              "page 2's Shift row gives 1.00s, 2.00s and 3.00s");
        check(s.frame.nodes[grains::trim_first].shade == 14, "Shift brightens the markers");
    }
    {   // The gain (SampleOverview.gain, #400) in stock's steps, every one within
        // the caption's width, and the strip's waveform taller as it rises.
        double last = 0, last_ten = 0;
        for (i32 g = -30; g <= 20; ++g) {
            overview.gain = g;
            Screen s;
            s.input.shift = 1;
            s.run(2);
            char want[8];
            std::snprintf(want, sizeof want, "%c%d", g < 0 ? '-' : '+', g < 0 ? -g : g);
            check(!std::strcmp(s.text(6), want), "the gain reads in stock's signed steps");
            const double w = caption_width(s.text(6), 20);
            check(w >= 0 && w <= 53, "the gain's caption fits 53 px");
            double tallest = 0;   // drawn px from the centre line
            for (u32 h = 0; h < grains::Frame::path_count; ++h)
                for (const opxy::draw::Point& q : s.frame.paths[h].points)
                    tallest = std::fmax(tallest, std::fabs(q.y / 1024.0) * s.frame.nodes[h].scale / 1024.0);
            if (g % 10 == 0 && g <= 0) { check(tallest > last_ten + 0.2, "the waveform grows with the gain"); last_ten = tallest; }
            check(tallest >= last && tallest <= 20.375, "the waveform never shrinks as the gain rises, and stays within 20 px a side (and its 3/8 px ribbon)");
            last = tallest;
        }
        // No gain known (gain_unavailable): its cell shows nothing, and the waveform is as at +0.
        overview.gain = opxy::draw::gain_unavailable;
        Screen u;
        u.input.shift = 1;
        u.run(2);
        check(!u.text(2)[0] && !u.text(6)[0] && !u.frame.header[2][0], "an unknown gain shows nothing");
        overview.gain = 0;
        Screen z;
        z.input.shift = 1;
        z.run(2);
        bool same = true;
        for (u32 h = 0; h < grains::Frame::path_count; ++h)
            for (u32 i = 0; i < 64; ++i) same = same && u.frame.paths[h].points[i].y == z.frame.paths[h].points[i].y;
        check(same, "an unknown gain leaves the waveform as at +0");
    }
    {   // The waveform: 60 columns across the strip, each from its stretch of
        // the overview's 96 bins, its high above the centre line and its low
        // below, each drawn to its own height, as a sampler draws a sample; every
        // path a thin closed ribbon the device draws cleanly (distinct
        // neighbours, within 64 px, no hairpin turns, area, no crossing).
        overview.gain = 0;
        for (u32 b = 0; b < 96; ++b) { overview.high[b] = 2000; overview.low[b] = -2000; }
        overview.high[50] = 32767; overview.low[50] = -8192;   // a hit, lopsided
        overview.generation = 7;
        Screen s;
        s.run();
        const double pitch = 432.0 / 60;   // bin 50 is in column 31 (bins 49.6 .. 51.2)
        double top_x = 0, top = 0, bottom50 = 0, quiet = 1e9;
        for (u32 h = 0; h < grains::Frame::path_count; ++h) {
            const opxy::draw::Node& n = s.frame.nodes[h];
            check(n.scale == 1024, "the waveform is drawn 1:1");
            const auto& pts = s.frame.paths[h].points;
            double sharpest = 180, area = 0;
            for (u32 i = 0; i < 64; ++i) {
                const opxy::draw::Point &a = pts[i], &b = pts[(i + 1) % 64], &c = pts[(i + 2) % 64];
                const double x = (n.x + a.x) / 1024.0, y = a.y / 1024.0;
                if (i < 32) {   // out along the zigzag
                    if (-y > top) { top = -y; top_x = x; }
                    if (std::fabs(x - (24 + pitch * 31.75)) < 0.01) bottom50 = y;
                    if (i % 2 == 1 && i < 31) quiet = std::fmin(quiet, std::fabs(y));   // each column's high
                    // Back 3/8 px below, point for point: a ribbon that can't cross itself.
                    check(pts[63 - i].x == a.x && pts[63 - i].y == a.y + 384, "the outline comes back 3/8 px below the zigzag");
                }
                check(std::abs(a.x) <= 64 * 1024 && std::abs(a.y) <= 64 * 1024, "waveform points stay within 64 px");
                check(std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)) >= 256, "waveform neighbours and ends stay 1/4 px apart");
                const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y, dot = ux * vx + uy * vy;
                if (dot < 0) sharpest = std::fmin(sharpest, std::atan2(std::fabs(ux * vy - uy * vx), -dot) * 180 / M_PI);
                area += (double(a.x) * b.y - double(b.x) * a.y) / (1024.0 * 1024.0) / 2;
            }
            check(sharpest > 2.5, "no waveform turn is sharper than the device's 2.5 degrees");
            check(std::fabs(area) > 30, "each waveform path has real area");
        }
        check(std::fabs(top_x - (24 + pitch * 31.25)) < 0.01, "the loudest bin peaks in its own column");
        check(std::fabs(top - 13) < 0.1, "a full-scale peak stands 13 px at +0");
        check(bottom50 > 4.2 && bottom50 < 5.0, "the column's low (-12 dB) is drawn to its own, smaller depth, 4.6 px");
        check(quiet > 1.4 && quiet < 1.8, "a quiet bin (-24 dB) still stands 1.6 px, twice what a straight scale draws");
        // Still while nothing changes; rebuilt only for a new sample.
        grains::Frame before = s.frame;
        s.run(30);
        check(!std::memcmp(&before, &s.frame, sizeof before), "at rest no node, path or text changes");
        s.input.note_serial = 1; s.input.held_notes = 1;
        s.run(1);
        before = s.frame;
        s.run(60);
        bool same_paths = true;
        for (u32 h = 0; h < grains::Frame::path_count; ++h)
            same_paths = same_paths && s.frame.paths[h].key == before.paths[h].key &&
                         !std::memcmp(&s.frame.paths[h].points, &before.paths[h].points, sizeof before.paths[h].points);
        check(same_paths, "the waveform isn't rebuilt while a note plays");
        overview.generation = 8; overview.high[50] = 2000;
        s.run(1);
        check(s.frame.paths[2].key != before.paths[2].key, "a new sample rebuilds the waveform");
        // Before the overview is read: the flat stand-in.
        overview.loaded = 0;
        Screen f;
        f.run(2);
        double flat = 0;
        for (u32 h = 0; h < grains::Frame::path_count; ++h)
            for (const opxy::draw::Point& q : f.frame.paths[h].points) flat = std::fmax(flat, std::fabs(q.y / 1024.0));
        check(flat == 0.75 + 0.375, "a sample still loading shows the flat stand-in, 0.75 px either side and its ribbon");
        overview.loaded = 1;
        overview.generation = 1;
        for (u32 b = 0; b < 96; ++b) { const opxy::i16 peak = opxy::i16((40 + (b * 37) % 200) << 7); overview.high[b] = peak; overview.low[b] = opxy::i16(-peak); }
    }
    {   // The swap hint: hidden (its cover placed) until Shift is held on a
        // loaded sample, then shown (its cover hidden), and nothing else of the
        // stage moves for it.
        Screen s;
        s.run();
        check(s.shown(grains::swap_cover), "no swap hint without Shift");
        s.input.shift = 1;
        s.run(1);
        check(!s.shown(grains::swap_cover), "Shift shows the swap hint on a loaded sample");
        s.run();
        grains::Frame held = s.frame;
        s.run(30);
        check(!std::memcmp(&held.nodes, &s.frame.nodes, sizeof held.nodes), "the hint holds still while Shift stays down");
        s.input.shift = 0;
        s.run(1);
        check(s.shown(grains::swap_cover), "letting Shift go hides the swap hint");
        s.input.reduced_motion = 1;
        s.input.shift = 1;
        s.run(1);
        check(!s.shown(grains::swap_cover), "reduced motion shows the hint just the same");
        overview.loaded = 0;
        Screen l;
        l.input.shift = 1;
        l.run();
        check(l.shown(grains::swap_cover), "no swap hint while the sample is still loading");
        overview.assigned = 0;
        Screen e;
        e.input.shift = 1;
        e.run();
        check(e.shown(grains::swap_cover), "no swap hint over the empty state's own load hint");
        overview.assigned = overview.loaded = 1;
    }
    {   // No sample: Shift's row shows no times, and nothing is drawn on the stage.
        overview.assigned = overview.loaded = 0;
        Screen s;
        s.input.shift = 1;
        s.run();
        check(!s.shown(grains::trim_first) && !s.shown(grains::trim_first + 1), "no sample, no markers");
        check(!std::strcmp(s.text(4), "-"), "no sample, no start time");
        overview.assigned = overview.loaded = 1;
    }
    {   // Every caption Shift's row can show, for samples from a tenth of a second to twenty minutes.
        double widest = 0;
        const u32 lengths[] = {4410, 44100, 441000, 4410000, 52920000};
        for (u32 length : lengths)
            for (u32 a = 0; a <= 40; ++a)
                for (u32 b = a; b <= 40; ++b) {
                    overview.frames = length;
                    overview.begin = u32((unsigned long long)(length) * a / 40);
                    overview.end = u32((unsigned long long)(length) * b / 40);
                    Screen s;
                    s.input.shift = 1;
                    s.run(1);
                    for (u32 slot = 4; slot < 8; ++slot) {
                        const double w = caption_width(s.text(slot), 20);
                        if (slot != 6 && (w < 0 || w > 53)) {   // the gain: measured above
                            std::printf("FAIL the caption '%s' is %.1f px at 20 px, over 53\n", s.text(slot), w);
                            ++failures;
                        }
                        if (slot != 6 && w > widest) widest = w;
                    }
                }
        double widest_title = 0;
        for (const char* t : grains::shift_titles)
            if (t) widest_title = std::fmax(widest_title, caption_width(t, 10));
        check(widest_title <= 56, "Shift's titles fit 56 px at 10 px");
        std::printf("Shift's captions: the widest value %.1f of 53 px, the widest title %.1f of 56 px\n", widest, widest_title);
    }
    {   // The amp envelope (input.amp, SDK 1.10): grains last as long as the sound.
        // Unknown, they go as the note is let go, as before. Stock reads a larger
        // release word as a shorter release: at its top (0x7f000000) the grains
        // go at once; at 0, the longest, they keep coming, dimming, and stop
        // once it's over. A larger attack word swells them in more slowly.
        overview.frames = 4 * 44100; overview.begin = 0; overview.end = overview.frames;
        auto grains_shown = [](const Screen& s) {
            u32 n = 0;
            for (u32 i = 0; i < grains::dash_lengths * grains::grain_shown; ++i) n += s.shown(grains::grain_first + i);
            return n;
        };
        auto brightest = [](const Screen& s) {
            u32 b = 0;
            for (u32 i = 0; i < grains::dash_lengths * grains::grain_shown; ++i)
                if (s.shown(grains::grain_first + i)) b = std::max<u32>(b, s.frame.nodes[grains::grain_first + i].shade);
            return b;
        };
        const i32 unknown = opxy::draw::envelope_unavailable, full = 0x7fffffff, shortest = 0x7f000000;
        const opxy::draw::Envelope envelopes[3] = {{unknown, unknown, unknown, unknown}, {0, 0, full, shortest}, {0, 0, full, 0}};
        u32 after[3];
        for (u32 e = 0; e < 3; ++e) {
            Screen s;
            s.input.amp = envelopes[e];
            s.run(2);
            s.input.note_serial = 1; s.input.held_notes = 1;
            s.run(60);
            check(grains_shown(s) > 0, "grains play while a note is held");
            s.input.held_notes = 0; s.input.note_off_serial = 1;
            s.run(30);   // half a second after the release
            after[e] = grains_shown(s);
            if (e == 2) {
                s.run(60 * 60);   // past the longest release (about 55 s to -66 dB)
                check(grains_shown(s) == lane_count_at_rest(s), "once the release is over, the screen is at rest");
            }
        }
        check(after[0] == after[1], "an unknown envelope lets the grains go as the shortest release does");
        check(after[2] > after[1], "a long release keeps grains coming after the note is let go");
        // A larger release word ends the motion sooner: frames from the note's
        // release until the screen is at rest, at two release words.
        auto rest_after = [&](i32 release) {
            Screen s;
            s.input.amp = {0, 0, full, release};
            s.run(2);
            s.input.note_serial = 1; s.input.held_notes = 1;
            s.run(60);
            s.input.held_notes = 0; s.input.note_off_serial = 1;
            u32 frames = 0;
            while (frames < 60 * 60 && grains_shown(s) != lane_count_at_rest(s)) { s.run(1); ++frames; }
            return frames;
        };
        const u32 longer = rest_after(at(0.3)), shorter = rest_after(at(0.7));
        check(shorter < longer, "a larger release word ends the grains sooner");
        Screen fast, slow;
        fast.input.amp = {0, 0, full, 0};
        slow.input.amp = {at(0.8), 0, full, 0};
        for (Screen* s : {&fast, &slow}) { s->run(2); s->input.note_serial = 1; s->input.held_notes = 1; s->run(20); }
        check(brightest(slow) < brightest(fast), "a larger attack word swells the grains in more slowly");
        std::printf("envelope: grains half a second after release: %u unknown, %u shortest, %u longest; at rest %u frames after release word 0.3, %u after 0.7; "
                    "brightest after 1/3 s: %u fast, %u slow attack\n",
                    after[0], after[1], after[2], longer, shorter, brightest(fast), brightest(slow));
    }
    if (failures) return 1;
    std::printf("screen: the waveform, the swap hint, Shift's markers and row checked\n");
    return 0;
}
