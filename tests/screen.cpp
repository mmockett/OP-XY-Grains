// Grains' screen checks, compiled for the host and run by
// python sdk.py test --machine <folder>: what Shift shows now that it edits the
// stock sampler's markers (resources.stockShift), the markers on the strip, and
// that every caption Shift's row can show fits its column on the device.
#include "../display.cpp"
#include <cmath>
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
    {'-', 329}, {'.', 280}, {'0', 560}, {'1', 560}, {'2', 560}, {'3', 560}, {'4', 560}, {'5', 560}, {'6', 560},
    {'7', 560}, {'8', 560}, {'9', 560}, {'a', 536}, {'d', 581}, {'e', 532}, {'i', 235}, {'m', 894}, {'n', 574},
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
        check(!std::strcmp(s.text(0), "start") && !std::strcmp(s.text(1), "window") && !std::strcmp(s.text(2), "size") &&
              !std::strcmp(s.text(3), "end"), "Shift's row names start, window, size (its own fine turn) and end");
        check(!std::strcmp(s.text(4), "0.00s") && !std::strcmp(s.text(5), "4.00s") && !std::strcmp(s.text(7), "4.00s"),
              "Shift's row gives the start's and end's times and the window's length");
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
        // On the second page Shift's row is that page's, its third still its own.
        for (u32 i = 0; i < 4; ++i) s.input.shown[i] = opxy::u8(4 + i);
        s.input.shift = 1;
        s.run();
        check(!std::strcmp(s.text(0), "start") && !std::strcmp(s.text(2), "shpe") && !std::strcmp(s.text(3), "end"),
              "page 2's Shift row names start, window, shpe and end");
        check(!std::strcmp(s.text(4), "1.00s") && !std::strcmp(s.text(5), "2.00s") && !std::strcmp(s.text(7), "3.00s"),
              "page 2's Shift row gives 1.00s, 2.00s and 3.00s");
        check(s.frame.nodes[grains::trim_first].shade == 14, "Shift brightens the markers");
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
                        if (slot != 6 && (w < 0 || w > 53)) {
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
    if (failures) return 1;
    std::printf("screen: Shift's markers and row checked\n");
    return 0;
}
