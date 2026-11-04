"""sample_view.py: the artwork sample_view.h places, for a machine's
tools/make_panel.py. Shared, byte for byte, by Grains, Ghost and Recur (the
suites' tools/check_shared.py checks); never edit one copy alone.

    from sample_view import artwork, hint, HINT_NODES

artwork(prefix, line, length, half, marks) gives the waveform's nodes: the
centre line (node line, a ribbon length px long) and the 96 columns after it
(rectangles with a fill, COLUMN_WIDTH px wide and 8 half tall, which
sample_view.h scales to a quarter at most), and, if given,
the trim's two markers (each machine draws its own flags). hint(prefix,
first, text_slot) gives the swap hint: the Sample key's icon in nodes
first .. first + 2 and the words "swap" and "shift +" in two text slots.

Every part is authored off screen, at (640, 320), so nothing shows before the
first frame places it, and every outline closes over a long edge: the OP-XY
draws a closing edge under a quarter of a pixel as a sliver."""
import math

COLUMNS, COLUMN_WIDTH, HINT_NODES = 96, 4.8, 3
PARKED = ' transform="matrix(1 0 0 1 640 320)"'


def number(v):
    text = f"{round(v, 4):.4f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def node(prefix, i, points, stroke, width, fill="none"):
    d = "M" + " L".join(f"{number(x)},{number(y)}" for x, y in points) + " Z"
    return (f'<path id="{prefix}_node_{i}"{PARKED} d="{d}" fill="{fill}" stroke="{stroke}" '
            f'stroke-width="{number(width)}"/>')


def artwork(prefix, line, length, half, marks=None):
    h = length / 2
    out = [node(prefix, line, [(h, 0.15), (-h, 0.15), (-h, -0.15), (h, -0.15)], "#333333", 0.5, "#333333")]
    w = COLUMN_WIDTH / 2
    column = [(-w, 4 * half), (-w, -4 * half), (w, -4 * half), (w, 4 * half)]
    out += [node(prefix, line + 1 + b, column, "#999999", 0.5, "#999999") for b in range(COLUMNS)]
    for i, points in marks or []:
        out.append(node(prefix, i, points, "#e6e6e6", 0.5, "#e6e6e6"))
    return out


def hint(prefix, first, text_slot):
    """The words at 10 px in the empty hint's greys (#CBCBCB, #7D7D7D), and
    Grains' Sample key icon at 14 px, at (38, 4) from the hint's corner, in its
    own paint (sample_view.h keeps it): an outline, a circle and the bars."""
    font = 'font-family="UniversNextPro-Light" font-size="10px"'
    out = [f'<text id="{prefix}_text_{text_slot}"{PARKED} fill="#CBCBCB" {font}>swap</text>',
           f'<text id="{prefix}_text_{text_slot + 1}"{PARKED} fill="#7D7D7D" {font}>shift +</text>']
    k = 14 / 28
    key = lambda x, y: (38 + (x - 256) * k, 4 + (y - 129) * k)
    # The key: a rounded square, begun along its top so it closes over a long edge.
    square = [(282.5763, 129), (284, 130.4237), (284, 155.5763), (282.5763, 157), (257.4237, 157), (256, 155.5763),
              (256, 130.4237), (257.4237, 129)]
    out.append(node(prefix, first, [key(x, y) for x, y in square], "#7D7D7D", 0.8))
    ring = [(270 + 9.0763 * math.cos(2 * math.pi * i / 20), 142.8814 + 9.0763 * math.sin(2 * math.pi * i / 20)) for i in range(20)]
    out.append(node(prefix, first + 1, [key(x, y) for x, y in ring], "#7D7D7D", 0.6))
    # Its seven bars, a little waveform, as one filled outline: along their
    # tops, then back along their bottoms, joined by a hairline at their middle.
    bars = ((265.6254, 142.2881, 143.5932), (267.0492, 142.2881, 143.5932), (268.5322, 139.2627, 146.5593), (269.9559, 140.8644, 145.0763),
            (271.439, 142.2881, 143.5932), (272.922, 142.2881, 143.5932), (274.3458, 142.2881, 143.5932))
    middle, joint = 142.9407, 0.3
    tops, bottoms = [], []
    for x, top, bottom in bars:
        tops += [(x, middle - joint), (x, top), (x + 0.8, top), (x + 0.8, middle - joint)]
        bottoms += [(x, middle + joint), (x, bottom), (x + 0.8, bottom), (x + 0.8, middle + joint)]
    wave = tops[1:-1] + bottoms[::-1][1:-1]
    out.append(node(prefix, first + 2, [key(x, y) for x, y in wave], "#7D7D7D", 0.5, "#7D7D7D"))
    return out
