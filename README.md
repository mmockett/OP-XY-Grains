# Grains

A granular sample synth for the OP-XY. Load a sample and play it across the
keyboard: grains read around a position that can scan through it, stack into
chords and spread across the stereo field, and the screen shows each grain
where it reads and at what pitch.

![Grains' screen: no sample loaded, at rest, while a note plays, and with a minor chord](docs/screen/preview.png)

**Status: experimental.** Grains plays on an OP-XY running OS 1.2.1 and targets
Open-XY SDK 1.10. Its render callback measures 3.067% of the stock block
budget on the device (v1.1.0, one capture of `opxy-sample-callback-v1`,
graphics excluded). That measures its defaults only, so a preset or setting with
more grains, `dens` up to 20 for example, costs more, and it isn't total device
CPU or a worst-case sweep.

Grains is live in the [open-xy.com builder](https://open-xy.com/#preview/machines/grains),
which installs `v1.1.0`, the version the Open-XY catalog pins.

## Playing it

Choose **Grains** on an instrument track, then load a sample (Shift + the
Sample key). One sample plays across the keyboard, as on the stock sampler:
the sample's root key plays it as recorded. Grains reads between the
sampler's start and end markers, the whole sample when it is newly loaded:
pos, scan and spry choose where its grains read within them.

| Page 1 | | Page 2 | |
| --- | --- | --- | --- |
| **pos** | where grains read | **spry** | how random each grain is: where it starts around pos, then its stereo spread, reversing some, and past halfway its pitch and length |
| **scan** | how fast pos moves on: either way, up to 2x; the middle freezes it | **chrd** | off, a detune, or grains stacked on octaves, fifths, a major or minor chord, or harmonics |
| **size** | each grain's length, 10 ms to 2 s | **shpe** | each grain's window, from a swell through smooth to a pluck; the screen draws it under pos |
| **dens** | how many grains overlap, 1 to 20 (in sync mode, the division) | **mode** | cloud (random), sync (on the tempo's divisions) or osc (evenly overlapped); rcld, rsync and rosc play every grain backwards |

Press any encoder to switch pages. The OP-XY's own envelopes, LFOs, filter and
effects apply as for any instrument.

Holding Shift, the encoders edit the stock sampler's markers and gain
instead, on either page, and the screen shows them:

| Shift + | Edits | On the screen |
| --- | --- | --- |
| encoder 1 | **start**: where grains may begin reading | the start marker on the strip; its time in the sample |
| encoder 2 | **window**: start and end together, keeping the length between them | both markers slide; the length |
| encoder 3 | **gain**: the sample's gain, in stock's steps from -30 to +20 | the strip's waveform grows and shrinks, twice as tall each 10 steps up; `+0`, `-12`, `+20` (blank while no gain is known) |
| encoder 4 | **end**: where grains stop reading | the end marker; its time in the sample |

Trim to the stretch that matters and pos, scan and spry range over it alone:
pos at 0 sits on the start marker, and a scan wraps from end back to start.
The markers stay on the strip, a step dimmer, while they trim the sample. The
track saves them, as the sampler does, so a sample trimmed on the stock
sampler plays the same stretch in Grains. Gain and tuning set on the stock
sampler still apply.

Every note of Grains, on every track, shares 64 grains at once, to keep its
processing in bounds. Notes take what dens asks for while there's room: three
notes at 20, or eight at 8. Past that, each note gets its part of the 64, in
proportion to what it asks for: fewer grains at once in cloud and osc, shorter
ones on the same beat in sync. A note that has to give grains up fades them
out over about 6 ms. Each grain's level is set as it starts, so fewer grains
play louder without the ones already sounding jumping. The screen still shows
dens's own count.

## The sample

`samples/wane.wav` is Wane rendered on the host through its own engine: a held
C4 (261.6 Hz) with the octave above it at 0.4, on Worn-like tape (shape swept
0.28 to 0.72 and back once, detune 0.35, drift 0.3, warmth 0.35, wow 0.25,
flutter 0.15, sag 0.3, age 0.15). It is six seconds, 16-bit mono at 44.1 kHz
(517 KiB), peaking at -6 dBFS so Grains' busiest init sound stays under its
output ceiling, and its end crossfades into its start, so a scan wraps without
a seam. Its region roots it at middle C, which plays it as recorded.

The build installs it once as `/content/samples/grains/wane.wav`, in the
OP-XY's own storage rather than the module memory; the OP-XY loads it into its
sample memory when a preset naming it is loaded, as it loads any sample. The
init preset (`presets/default.json`) and every preset below name it, each
keeping its own controls. `tools/make_presets.py` writes those regions and the
init preset; run it after changing the sample, the defaults or a preset.

## Presets

Six presets, each opening on the wane sample and made to play on whatever sample is
loaded after it. With no sample on the track Grains stays silent and the screen says
how to load one:

| Preset | What it is | How it's set |
| --- | --- | --- |
| **still** | a frozen cloud, a sound held in place | cloud, no scan, grains of 420 ms, detuned |
| **drift** | a pad that wanders slowly through the sample | cloud, slow scan, long grains, octaves |
| **choir** | each note a major chord | cloud, grains of 240 ms, stacked and spread |
| **stutter** | sixteenths on the tempo | sync, short plucked grains |
| **reed** | a pitched, steady tone from one spot | osc, grains of 28 ms, octaves |
| **undertow** | the sample backwards, swelling | rcloud, slow backward scan, fifths |

Where the sample's own character sits matters most, so try `pos` first.

## The SDK it needs

Grains is a sample engine on the stock sample page (page 2) that plays like the
sampler and draws its own screen, using [Open-XY](https://github.com/etcetc000/open-xy)
SDK 1.8's sample engines with their own screen and the sample's overview, and
SDK 1.10's `stockShift`, which hands Shift and the encoders back to the stock
sampler's markers and gain. Showing the gain needs the sample overview's
`gain`, from [etcetc000/open-xy#400](https://github.com/etcetc000/open-xy/pull/400),
as well as `stockShift`:

```sh
git clone -b sdk/machine-samples https://github.com/mmockett/open-xy ../open-xy
python ../open-xy/sdk.py test --machine . --cxx clang++ --host-cxx g++
python ../open-xy/sdk.py build --machine . --stock-os <your stock-1.2.1>/os.ldr --out <folder outside this checkout>
```

Then install the build's `candidate/` files as the SDK's
[build guide](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md#5-install)
describes, including `content/presets/grains/` (the init preset and the six)
and `content/samples/grains/wane.wav`. Without the sample on the device, a
preset opens with no sample loaded, as before. The browser builder doesn't
install a machine's own samples yet, so use `sdk.py build`.

## Develop

- `sample.cpp`: the grain engine, fixed point, with its tables in `tables.h`
  (`tools/gen_tables.py` writes them).
- `display.cpp`: the screen ([docs/SCREEN.md](docs/SCREEN.md)), its artwork in
  `panel.svg.fragment` (`tools/make_panel.py` writes it).
- `tests/sound.cpp`: the engine's sound tests, and `tests/screen.cpp`, Shift's
  markers and captions, the gain's every step and its width, a blank cell for
  an unknown gain, and the waveform growing with the gain, and that grains
  follow the amp envelope (gone at release when it's unknown or short, still
  coming in a long release, dimmer early in a slow attack); `sdk.py test` runs
  both.

Sample engines have no browser preview of their own. `docs/screen/preview.png`
was rendered by running `display.cpp` through a stand-in synth with the same
screen in the SDK's preview, at the OP-XY's 480 × 220 and in its screen font,
Univers Next Pro Light
([mmockett/OP-XY's renderer](https://github.com/mmockett/OP-XY/tree/main/tools/render)). Agent instructions are in [AGENTS.md](AGENTS.md).

## Licence

MIT ([LICENSE](LICENSE)). Grains takes ideas, not code, from
[Oi, Grandad!](https://github.com/publicsamples/Oi-Grandad) and Elektron's
Tonverk Grainer.
