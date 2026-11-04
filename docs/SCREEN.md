# The screen

Grains shows its grains as a cloud: each grain where in the sample it reads
(across) and at what pitch (up), over the sample's strip with the read position
marked. [screen/cloud.png](screen/cloud.png) shows four states at 2×;
[screen/cloud.html](screen/cloud.html) draws them.

It covers the stock sampler's page (SDK 1.8's own screen) and draws the sample
from its overview.

It follows the SDK's [machine UX](https://github.com/etcetc000/open-xy/blob/staging/docs/ux.md)
and [display API](https://github.com/etcetc000/open-xy/blob/staging/docs/display.md).

## Layout (480 × 220)

| Area | y | What |
| --- | --- | --- |
| Header | 0 to 38 | The SDK's two-page header (`opxy/page_header.hpp`): page 1 **pos scan size dens**, page 2 **spry chrd shpe mode** |
| Cloud | 54 to 150 | Lanes from the root (150) to +2 octaves (54), one octave per 48 px |
| Strip | 167 to 205 | The sample's overview, centred at 186 |

The cloud and the strip share the sample's span, x 24 to 456, so a grain sits
straight above the stretch of sample it reads.

## Parts

- **Strip** (path 0): the sample's amplitude as one closed 64-point ribbon,
  32 bins out along the top and back along the bottom. Dim at rest, brighter
  while a note plays.
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
  note is let go, as before. Stock's times for the envelope page's positions
  aren't documented; the screen assumes them exponential, 2 ms at 0 to about
  10 s at full (1.9 s at 0.8), for attack, decay and release alike, and the
  sustain a level, until they are timed on the device.
- A control turned snaps its mark at once (pos, spry, the lanes for chrd); the
  header follows the SDK helper's 0.15 s page switch. Reduced motion shows the
  end state.
- Grain positions are drawn from the controls and time, as the MNMD screens draw
  hits: the screen doesn't read the audio's grains, so the cloud is faithful in
  where and how many, not grain for grain.

## Budget

Within the SDK's frame (4 paths of 64 points, 256 nodes, 16 shades, 16 text
slots):

| Part | Uses |
| --- | --- |
| Header | 8 bar nodes and all 16 text slots |
| Strip | path 0 |
| pos, scan, spry | 5 nodes (line, arrow, cap, a pair of dotted lines) |
| shpe | 2 nodes (the window's edges, sharing the peak as their pivot) |
| Lanes | 7 nodes (3 octaves, up to 4 chord notes) and 3 static labels |
| Grains | 24 nodes, the most grains shown at once; dashes in 3 authored lengths for short, medium and long sizes, scaled within each (a node scales uniformly, at most 2×) |
| Start and end | 2 nodes |

52 nodes and 2 paths, leaving room for later parts. Lane labels are static
`<text>` in the device font; the header takes every text slot.
