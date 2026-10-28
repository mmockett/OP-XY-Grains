#pragma once
#include <opxy/sample_engine.hpp>

namespace grains {
// The page-2 spike: a voice loops its region at the played pitch and beeps
// when one of the eight live controls moves, n pulses for control n.
struct Voice {
    opxy::SampleView sample;
    opxy::u32 frame, fraction;      // playhead: whole frames and a 16-bit fraction
    opxy::i32 last[8];              // the controls as the previous block saw them
    bool primed;                    // last[] holds a block's controls
    opxy::u32 beep_slot;            // the control being beeped, 0..7
    opxy::u32 beep_elapsed;         // samples into the pulse pattern
    opxy::u32 beep_phase;
};
void initialize(Voice& voice, const opxy::SampleView& sample);
void start(Voice& voice);
void render_sample(Voice& voice, const opxy::SampleBlock& block);

// Pulse pattern: 40 ms on, 60 ms off, once per control number.
const opxy::u32 beep_period = 4410, beep_on = 1764, beep_idle = ~0u;
}
