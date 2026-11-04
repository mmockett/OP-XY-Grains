#pragma once
#include <opxy/page_header.hpp>

namespace grains {
// Matches resources.display: paths, points, nodes, texts, palette (docs/SCREEN.md).
using Frame = opxy::draw::Frame<4, 64, 56, 16, 16>;

// Nodes: the strip's waveform (the four paths, 15 columns each), the header's
// bar, then the marks.
const opxy::u32 strip_first = 0, strip_paths = 4,
                header_first = 4, pos_node = 12, scan_arrow = 13, window_rise = 14, scan_frozen = 15,
                spray_first = 16, lane_first = 18, lane_count = 4, window_fall = 22, grain_first = 23, grain_shown = 8,
                cover_stage = 47, cover_hint = 48,   // with no sample: the stage hidden, the hint shown
                octave_first = 49,                   // the root, +1 and +2 octave lanes
                trim_first = 52,                     // the stock sampler's start and end markers (Shift)
                swap_cover = 54,                     // hides the swap hint unless Shift is held on a loaded sample
                centre_line = 55;                    // the strip's faint centre line
// Grains are dashes of three authored lengths (6, 16 and 40 px), eight of each.
const opxy::u32 dash_lengths = 3;

// A grain on the screen: drawn from the controls and time, not read from audio.
// A grain on screen; life 0 when none. drift: its pitch off its lane, in pixels (spry's jitter).
struct ShownGrain { opxy::u16 age, life; opxy::u16 position; opxy::u8 note; signed char drift; };

// The amp envelope's level as the screen follows it (display.cpp, follow()).
struct Swell { opxy::u32 level, from, stage; };   // level Q16; from spare (0); stage 0 idle, 1 attack, 2 decay, 3 release, 4 sustain

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    ShownGrain grains[grain_shown];
    opxy::u32 scan;          // the scan's offset from pos while a note is held, Q16 of the sample
    opxy::u32 next;          // ms until the next grain
    opxy::u32 random;
    opxy::u32 spawned;
    opxy::u32 track;
    Swell swell;             // the amp envelope's level: grains swell, hold and fade with it
};
}
