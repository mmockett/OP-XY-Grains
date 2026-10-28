#pragma once
#include <opxy/sample_engine.hpp>

namespace grains {
// The controls, by saved parameter index (definition.json).
enum Control : unsigned { kPos, kScan, kSize, kDens, kSpry, kChrd, kShpe, kMode };
enum Mode : unsigned { kCloud, kSync, kOsc };
const unsigned mode_count = 6;              // the three, then each with grains played backwards
const opxy::i32 pitch_jitter_cents = 50;    // spry's widest random pitch, either way
const unsigned grain_count = 20;            // a voice's grains, and dens's most at once
const unsigned grain_budget = 64;           // grains every Grains voice may sound at once, on every track

// One grain: where it reads, how fast, and how far through its window it is.
struct Grain {
    opxy::u32 frame, fraction;   // read position: whole frames and a 16-bit fraction
    opxy::u32 step;              // frames per output sample, Q16
    opxy::u32 age, length;       // samples played and in all
    opxy::i32 gain;              // its share of the output (gain_table), fixed as it starts
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
    opxy::u32 cycle;             // the grain census's cycle this voice last rendered in
};

void initialize(Voice& voice, const opxy::SampleView& sample);
void start(Voice& voice);
void render_sample(Voice& voice, const opxy::SampleBlock& block);
}
