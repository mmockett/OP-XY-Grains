#pragma once
#include <opxy/sample_engine.hpp>

namespace grains {
// The controls, by saved parameter index (definition.json).
enum Control : unsigned { kPos, kScan, kSize, kDens, kSpry, kChrd, kShpe, kMode };
enum Mode : unsigned { kCloud, kSync, kOsc };
const unsigned grain_count = 8;

// One grain: where it reads, how fast, and how far through its window it is.
struct Grain {
    opxy::u32 frame, fraction;   // read position: whole frames and a 16-bit fraction
    opxy::u32 step;              // frames per output sample, Q16
    opxy::u32 age, length;       // samples played and in all
    opxy::u32 window;            // window table position, Q16 (0..64)
    opxy::u32 rise, fall;        // its change per sample before and after the peak, Q16
    opxy::u32 peak;              // the sample at which the window peaks
    opxy::u8 pan;                // 0 (left) .. 16 (right)
    bool reverse, active;
};

// A voice: its sample, its grains, where the scan has reached and when the
// next grain starts. Stock creates 256, so keep it small.
struct Voice {
    opxy::SampleView sample;
    Grain grains[grain_count];
    opxy::i32 scan;              // the scan's offset from pos, frames
    opxy::u32 scan_fraction;     // and its 16-bit fraction
    opxy::u32 countdown;         // samples until the next grain
    opxy::u32 random;            // xorshift state
    opxy::u32 spawned;           // grains started since the note, for chrd's notes
};

void initialize(Voice& voice, const opxy::SampleView& sample);
void start(Voice& voice);
void render_sample(Voice& voice, const opxy::SampleBlock& block);
}
