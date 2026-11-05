#pragma once
#include <opxy/page_header.hpp>

namespace grains {
// Matches resources.display: paths, points, nodes, texts, palette (docs/SCREEN.md).
// No reshaping paths: the strip's waveform is columns (sample_view.h).
using Frame = opxy::draw::Frame<0, 0, 151, 18, 16>;

// Nodes: the header's bar, the marks, the grains, the covers for the empty
// state, the octave lanes, the trim's markers, then the sample view's
// (sample_view.h): the strip's centre line and its 96 columns, and the Shift
// overlay's icon and cover (with text slots 16 and 17).
const opxy::u32 header_first = 0, pos_node = 8, scan_arrow = 9, window_rise = 10, scan_frozen = 11,
                spray_first = 12, lane_first = 14, lane_count = 4, window_fall = 18, grain_first = 19, grain_shown = 8,
                cover_stage = 43, cover_hint = 44,   // with no sample: the stage hidden, the hint shown
                octave_first = 45,                   // the root, +1 and +2 octave lanes
                trim_first = 48,                     // the stock sampler's start and end markers (Shift)
                sample_line = 50,                    // the strip's centre line, then its 96 columns
                overlay_icon = 147, overlay_cover = 150,   // while Shift is held: how to swap the sample, over a dimming cover
                overlay_text = 16,
                node_total = 151;
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
    opxy::u32 wave_key;      // the sample, trim and gain the strip last drew (0: none)
    opxy::u32 drawn;         // a frame drawn since reset (until then the strip draws at once)
};
}
