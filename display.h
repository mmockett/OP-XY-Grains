#pragma once
#include <opxy/page_header.hpp>

namespace grains {
// Matches resources.display: paths, points, nodes, texts, palette (docs/SCREEN.md).
using Frame = opxy::draw::Frame<2, 64, 50, 16, 16>;

// Nodes: the strip's two halves (the paths), the header's bar, then the marks.
const opxy::u32 header_first = 2, pos_node = 10, scan_forward = 11, scan_back = 12, scan_frozen = 13,
                spray_first = 14, lane_first = 16, lane_count = 5, grain_first = 21, grain_shown = 8,
                cover_stage = 45, cover_hint = 46,   // with no sample: the stage hidden, the hint shown
                octave_first = 47;                   // the root, +1 and +2 octave lanes
// Grains are dashes of three authored lengths (6, 16 and 40 px), eight of each.
const opxy::u32 dash_lengths = 3;

// A grain on the screen: drawn from the controls and time, not read from audio.
struct ShownGrain { opxy::u16 age, life; opxy::u16 position; opxy::u8 note; opxy::u8 live; };

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    ShownGrain grains[grain_shown];
    opxy::u32 scan;          // the scan's offset from pos while a note is held, Q16 of the sample
    opxy::u32 next;          // ms until the next grain
    opxy::u32 random;
    opxy::u32 spawned;
    opxy::u32 track;
};
}
