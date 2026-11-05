// Grains' screen checks, compiled for the host and run by
// python sdk.py test --machine <folder>: the sample's waveform on the strip, the
// overlay Shift shows, what Shift shows now that it edits the stock sampler's
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
    // Bin b's column on the strip: shown, and its top and bottom, px.
    bool column(u32 b, double& top, double& bottom) const {
        const opxy::draw::Node& n = frame.nodes[grains::sample_line + 1 + b];
        const double h = n.scale / 1024.0 * 4 * 20;   // authored 8 x 20 px tall, scaled to its span
        top = n.y / 1024.0 - h; bottom = n.y / 1024.0 + h;
        return shown(grains::sample_line + 1 + b);
    }
    // How far the strip's columns reach from its centre line, px.
    double reach() const {
        double most = 0, top, bottom;
        for (u32 b = 0; b < grains::sample_view::sample_columns; ++b)
            if (column(b, top, bottom)) most = std::fmax(most, std::fmax(186 - top, bottom - 186));
        return most;
    }
    bool hint_shown() const {
        bool all = true, none = true;
        for (u32 k = 0; k < grains::sample_view::sample_overlay_icon_nodes; ++k) { all = all && shown(grains::overlay_icon + k); none = none && !shown(grains::overlay_icon + k); }
        all = all && shown(grains::overlay_cover) && frame.nodes[grains::overlay_cover].x == 0;
        none = none && !shown(grains::overlay_cover);
        const bool words = !std::strcmp(text(grains::overlay_text), "swap sample") && !std::strcmp(text(grains::overlay_text + 1), "with");
        if (all && words) return true;
        if (!none || text(grains::overlay_text)[0] || text(grains::overlay_text + 1)[0]) { std::printf("FAIL the Shift overlay half shown\n"); ++failures; }
        return false;
    }
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
            const double tallest = s.reach();   // drawn px from the centre line
            // At -30 a quiet sample may be the centre line alone.
            if (g % 10 == 0 && g <= 0) { check(g == -30 || tallest > last_ten + 0.2, "the waveform grows with the gain"); last_ten = tallest; }
            check(tallest >= last && tallest <= 20.01, "the waveform never shrinks as the gain rises, and stays within 20 px a side");
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
        check(!std::memcmp(&u.frame.nodes[grains::sample_line], &z.frame.nodes[grains::sample_line], 97 * sizeof(opxy::draw::Node)),
              "an unknown gain leaves the waveform as at +0");
    }
    {   // The waveform (sample_view.h): a column for each of the overview's 96
        // bins across the strip, from the bin's lowest value (below the centre
        // line) to its highest (above), as a sampler draws a sample, its reach
        // the root of the value against the loudest's; still at rest and while
        // a note plays; redrawn for a new sample, 12 columns a frame at most.
        overview.gain = 0;
        for (u32 b = 0; b < 96; ++b) { overview.high[b] = 2000; overview.low[b] = -2000; }
        overview.high[50] = 32767; overview.low[50] = -8192;   // a hit, lopsided
        overview.high[60] = 0; overview.low[60] = 0;           // silence
        overview.generation = 7;
        Screen s;
        s.run();
        double top, bottom;
        check(s.column(50, top, bottom), "the hit has a column");
        check(std::fabs(s.frame.nodes[grains::sample_line + 51].x / 1024.0 - (24 + 432.0 * 101 / 192)) < 0.01, "the hit's column stands over its own bin");
        check(std::fabs(186 - top - 17.06) < 0.1, "the loudest bin reaches 85% of the 20 px at +0");
        check(std::fabs(bottom - 186 - 8.53) < 0.1, "its low (a quarter of the loudest) reaches half as far, below the line");
        check(s.column(10, top, bottom) && std::fabs(186 - top - 4.21) < 0.1 && std::fabs(bottom - 186 - 4.21) < 0.1,
              "a quiet bin (-24 dB) still stands 4.2 px either side, the root of its level");
        check(!s.column(60, top, bottom), "a silent bin leaves only the centre line");
        check(s.shown(grains::sample_line), "the centre line");
        // Still while nothing changes, and while a note plays; rebuilt only for a new sample.
        grains::Frame before = s.frame;
        s.run(30);
        check(!std::memcmp(&before, &s.frame, sizeof before), "at rest no node or text changes");
        s.input.note_serial = 1; s.input.held_notes = 1;
        s.run(1);
        before = s.frame;
        s.run(60);
        check(!std::memcmp(&before.nodes[grains::sample_line], &s.frame.nodes[grains::sample_line], 97 * sizeof(opxy::draw::Node)),
              "the waveform doesn't change while a note plays");
        overview.generation = 8; overview.high[50] = 2000; overview.low[50] = -2000;
        u32 most = 0, frames = 0;
        for (u32 f = 0; f < 20; ++f) {
            const grains::Frame was = s.frame;
            s.run(1);
            u32 changed = 0;
            for (u32 n = 0; n <= 96; ++n)
                changed += std::memcmp(&was.nodes[grains::sample_line + n], &s.frame.nodes[grains::sample_line + n], sizeof(opxy::draw::Node)) != 0;
            most = std::max(most, changed);
            frames += changed != 0;
        }
        check(frames > 0 && most <= 12, "a new sample redraws the waveform, 12 columns a frame at most");
        // Before the overview is read: the flat stand-in, the centre line alone.
        overview.loaded = 0;
        Screen f;
        f.run(2);
        check(f.shown(grains::sample_line) && f.reach() == 0, "a sample still loading shows the flat stand-in: the centre line alone");
        overview.loaded = 1;
        overview.generation = 1;
        for (u32 b = 0; b < 96; ++b) { const opxy::i16 peak = opxy::i16((40 + (b * 37) % 200) << 7); overview.high[b] = peak; overview.low[b] = opxy::i16(-peak); }
    }
    {   // The Shift overlay (sample_view.h): hidden until Shift is held on a
        // loaded sample, then the cloud dimmed under its cover and "swap
        // sample", "with" and the Sample key's icon over it; its edges move
        // its 4 nodes, the markers and the header, nothing else of the stage.
        Screen s;
        s.run();
        check(!s.hint_shown(), "no Shift overlay without Shift");
        s.input.shift = 1;
        s.run(1);
        check(s.hint_shown() && s.x(grains::overlay_icon) == grains::sample_view::sample_overlay_icon_x &&
              s.x(grains::overlay_icon + 1) == grains::sample_view::sample_overlay_icon_x, "Shift shows the overlay on a loaded sample, in its place");
        s.run();
        grains::Frame held = s.frame;
        s.run(30);
        check(!std::memcmp(&held.nodes, &s.frame.nodes, sizeof held.nodes), "the hint holds still while Shift stays down");
        {
            const grains::Frame was = s.frame;
            s.input.shift = 0;
            s.run(1);
            u32 edge = 0;
            for (u32 n = 0; n < grains::Frame::node_count; ++n) edge += std::memcmp(&was.nodes[n], &s.frame.nodes[n], sizeof(opxy::draw::Node)) != 0;
            check(edge <= 14, "Shift's edge changes at most 14 nodes (the overlay's 4, the markers' 2, the header's 8)");
            std::printf("Shift's edge: %u nodes change\n", edge);
        }
        check(!s.hint_shown(), "letting Shift go hides the overlay");
        s.input.reduced_motion = 1;
        s.input.shift = 1;
        s.run(1);
        check(s.hint_shown(), "reduced motion shows the hint just the same");
        overview.loaded = 0;
        Screen l;
        l.input.shift = 1;
        l.run();
        check(!l.hint_shown(), "no Shift overlay while the sample is still loading");
        overview.assigned = 0;
        Screen e;
        e.input.shift = 1;
        e.run();
        check(!e.hint_shown(), "no Shift overlay over the empty state's own load hint");
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
    std::printf("screen: the waveform, the Shift overlay, Shift's markers and row checked\n");
    return 0;
}
