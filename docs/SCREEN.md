# The screen

Grains shows its grains as a cloud: each grain where in the sample it reads
(across) and at what pitch (up), over the sample's waveform with the read
position marked. [screen/cloud.png](screen/cloud.png) shows four states at 2×,
rendered from `display.cpp` like [screen/preview.png](screen/preview.png);
[screen/cloud.html](screen/cloud.html) is the first design sketch they came
from, with its strip still the old envelope.

It covers the stock sampler's page (SDK 1.8's own screen) and draws the sample
from its overview.

It follows the SDK's [machine UX](https://github.com/etcetc000/open-xy/blob/staging/docs/ux.md)
and [display API](https://github.com/etcetc000/open-xy/blob/staging/docs/display.md).

## Layout (480 × 220)

| Area | y | What |
| --- | --- | --- |
| Header | 0 to 38 | The SDK's two-page header (`opxy/page_header.hpp`): page 1 **pos scan size dens**, page 2 **spry chrd shpe mode** |
| Cloud | 54 to 150 | Lanes from the root (150) to +2 octaves (54), one octave per 48 px |
| Strip | 166 to 206 | The sample's waveform, centred at 186, at most 20 px a side; the swap hint above its right end (y 151 to 165) while Shift is held |

The cloud and the strip share the sample's span, x 24 to 456, so a grain sits
straight above the stretch of sample it reads.

## Parts

- **Strip** (`sample_view.h`, shared with Ghost and Recur): the sample's
  waveform as a sampler draws it, a column for each of the overview's 96
  bins across the strip (4.5 px apart), from the bin's lowest value, below a
  faint centre line, to its highest, above it, so a hit stands out as a tall
  lopsided column and a held sound as a dense band. A column reaches as far
  as the root of its value against the loudest bin's, the loudest 17 px (85%
  of the 20 px) at +0, so quiet material still shows; a column shorter than
  5 px (an eighth of the strip) is left out and the line shows there.
  Columns between the start and end markers are lit, those outside dim; a
  note doesn't change them. They are redrawn only for a new sample, its trim
  or its gain, at most 12 a frame. Before a sample's overview is read the
  strip is flat: the centre line alone.
- **pos**: a bright vertical line through the strip.
- **scan**: an arrow off the top of the pos line, pointing the way it moves,
  longer when faster. At 0 (frozen) a short cap instead.
- **spry**: two dotted lines either side of pos, at the spread grains start;
  past halfway, grains also drift a little off their note's lane
  within, kept inside the sample.
- **Lanes**: root, +1 oct and +2 oct solid and labelled at the right; **chrd**'s
  other notes dotted (min: +3, +7, +15, +19; harm: ×3 at +19; 5th: +7, +19).
  dtun draws no extra lanes.
- **Grains**: a dash per live grain, on its note's lane at the position it
  reads, as long as **size** spans in the sample. Its shade follows its window:
  dim as it starts, brightest at its window's peak, dim as it ends. dtun
  scatters them a few pixels around the root.
- **shpe**: the grain's window, drawn under the pos line: two lines from a
  peak down to feet 48 px apart on the screen's bottom edge. The peak slides
  from the right (0, a swell) to the left (100, a pluck), where the engine
  puts it.
- **Swap hint** (`sample_view.h`, as Ghost and Recur show it): while Shift
  is held on a loaded sample, "swap", "shift +" and the Sample key's icon on
  one line above the strip's right end (x 362 to 447, y 151 to 165), at
  10 px in the empty state's shades (#CBCBCB, #7D7D7D), the key 14 px. The
  icon is 3 nodes and the words 2 text slots, off screen the rest of the
  time, so Shift's edges move them and nothing else. It stops short of the
  end marker's flag, and the strip and marks draw over it.
- **Start and end** (Shift + encoders 1, 2 and 4, the stock sampler's
  markers): a bar each on the strip, a flag at its top pointing into the part
  grains read. Shown bright while Shift is held, and a step dimmer whenever
  they trim the sample; hidden on an untrimmed sample. pos, its scan and spry
  range between them.
- **Shift's row**: while Shift is held, the header's bright row reads
  **start**, **window**, **gain** and **end**, with the start's and end's
  times in the sample, the window's length (`0.62s`, `12.4s`, `123s`, `17m`)
  and the sample's gain in stock's steps (`+0`, `-12`, `+20`; a blank cell
  while no gain is known). The gain also scales the strip's waveform, twice as
  tall each 10 steps up, at most 20 px a side.

## Motion

- **At rest the screen is still**: one dim grain per chord note at pos, a preview
  of where a note will read.
- **While a note is held** grains appear, brighten and fade with their windows,
  and pos's line moves with scan. Only what audio does moves.
- **The amp envelope** (SDK 1.10): grains go on coming while the note sounds,
  held or in its release, as bright as the track's stock amp envelope lets
  them: they swell in over the attack, settle at the sustain and fade over the
  release, and stop once it's over. With the envelope unknown they go as the
  note is let go, as before. The times and shapes are stock's: a larger
  attack or decay is a longer stage, but a larger release is a shorter one
  (about 6 ms at 100, near a minute to silence at 0, 3 s at 50). The attack
  rises fast and slows toward its top, the decay falls exponentially to the
  sustain, a plain level, and the release fades exponentially from wherever it
  was (`amp_curve.h`).
- A control turned snaps its mark at once (pos, spry, the lanes for chrd); the
  header follows the SDK helper's 0.15 s page switch. Reduced motion shows the
  end state.
- Grain positions are drawn from the controls and time, as the MNMD screens draw
  hits: the screen doesn't read the audio's grains, so the cloud is faithful in
  where and how many, not grain for grain.

## Budget

Within the SDK's frame (no paths, 150 nodes, 16 shades, 18 text slots):

| Part | Uses |
| --- | --- |
| Header | 8 bar nodes and text slots 0 to 15 |
| Strip | 97 nodes: the centre line and 96 columns (`sample_view.h`) |
| Swap hint | 3 nodes (the key's icon) and text slots 16 and 17 |
| pos, scan, spry | 5 nodes (line, arrow, cap, a pair of dotted lines) |
| shpe | 2 nodes (the window's edges, sharing the peak as their pivot) |
| Lanes | 7 nodes (3 octaves, up to 4 chord notes) and 3 static labels |
| Grains | 24 nodes, the most grains shown at once; dashes in 3 authored lengths for short, medium and long sizes, scaled within each (a node scales uniformly, at most 2×) |
| Start and end | 2 nodes |
| No sample | 2 nodes (the covers) |

150 nodes and no reshaping paths. The strip, the markers and the swap hint
come from `sample_view.h` (with its artwork in `tools/sample_view.py`),
shared byte for byte with the Nonagon Suite's Ghost and Recur: it takes the
slot's place, size, shades and nodes as arguments and uses only
`SampleOverview` and the frame API. Lane labels are static `<text>` in the
device font.

Churn over a scripted run (at rest, notes, a minor chord with spry, Shift
held with the trim slid and the gain stepped, on the wane sample) averages
4.0 nodes a frame, 8.4 while a note is held, and peaks at 27 while Shift
slides the trim (pos, its marks and grains following, and 12 columns
redrawn); at rest nothing changes.
