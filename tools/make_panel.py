#!/usr/bin/env python3
"""Writes panel.svg.fragment, Grains' screen artwork (docs/SCREEN.md), with the
node numbers display.h names: run it after changing a shape here, and commit
both. Every node is one closed subpath of distinct points, so every outline
encloses area."""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 else "grains"   # another prefix for a preview copy
FONT = 'font-family="UniversNextPro-Light"'
LEFT, WIDTH, ROOT_Y, STRIP_Y = 24, 432, 150, 186


def num(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


def outline(points):
    return "M" + " L".join(f"{num(x)},{num(y)}" for x, y in points) + " Z"


def rect(x0, y0, x1, y1):
    return outline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


def node(i, d, stroke="#ffffff", width=1, fill="none"):
    return f'<path id="{ID}_node_{i}" d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>'


out = [f'<g id="{ID}_panel">',
       # Opaque: the screen is drawn over the stock sample page, which it hides.
       '<path d="M0,0 L480,0 L480,220 L0,220 Z" fill="#000000"/>',
       "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
       '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i, colour in enumerate(bar):
    out.append(f'<path id="{ID}_node_{4 + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
               f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{colour}" stroke="{colour}" stroke-width="0.5"/>')
texts = []
for c in range(4):
    ink = "#CBCBCB" if c < 2 else "#000000"
    texts.append((5 + 120 * c, 10, ink, 10))
for c in range(4):
    ink = "#CBCBCB" if c < 2 else "#000000"
    texts.append((65 + 120 * c, 17.4, ink, 20))
texts += [(5 + 120 * c, 33, "#5A5A5A", 10) for c in range(4)] + [(65 + 120 * c, 33, "#707070", 13) for c in range(4)]
for i, (x, y, ink, size) in enumerate(texts):
    out.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{ink}" {FONT} '
               f'font-size="{size}px">-</text>')

# Static: the octave lanes' labels, at the right, beneath the stage's cover.
for semis, label, x in ((0, "root", 437), (12, "+1 oct", 425), (24, "+2 oct", 425)):
    out.append(f'<text transform="matrix(1 0 0 1 {x} {ROOT_Y - semis * 4 - 4})" fill="#6A6A6A" {FONT} font-size="10px">{label}</text>')

# With no sample: node 47 covers the stage (shown when empty) beneath the hint,
# and node 48 covers the hint (shown when a sample is loaded). The octave lanes
# are nodes 49..51, drawn after both, so the hint's cover never cuts them; the
# labels lie right of the hint, which its cover never reaches.
out.append(node(47, rect(0, 40, 480, 220), "#000000", 0.5, "#000000"))
out.append(f'<text transform="matrix(1 0 0 1 166 108)" fill="#CBCBCB" {FONT} font-size="26px">load a sample</text>')
out.append(f'<text transform="matrix(1 0 0 1 190 150)" fill="#7D7D7D" {FONT} font-size="18px">shift +</text>')
# The Sample key's icon after "shift +": its rounded key, circle and seven
# bars, traced from the key (472 px square) and drawn 28 px square.
def key_icon(x0, y0, size, ink, bars=((181, 236, 258), (205, 236, 258), (230, 185, 308), (254, 212, 283),
                                      (279, 236, 258), (304, 236, 258), (328, 236, 258))):
    k = size / 472
    def key_xy(x, y):
        return x0 + (x - 12) * k, y0 + (y - 12) * k
    corner, x1, y1 = 24 * k, x0 + size, y0 + size
    out.append(f'<path d="{outline([(x0 + corner, y0), (x1 - corner, y0), (x1, y0 + corner), (x1, y1 - corner), (x1 - corner, y1), (x0 + corner, y1), (x0, y1 - corner), (x0, y0 + corner)])}" fill="none" stroke="{ink}" stroke-width="1"/>')
    cx, cy = key_xy(248, 246)
    out.append(f'<circle cx="{num(cx)}" cy="{num(cy)}" r="{num(153 * k)}" fill="none" stroke="{ink}" stroke-width="0.8"/>')
    for x, top, bottom in bars:
        bx, by0 = key_xy(x, top)
        _, by1 = key_xy(x, bottom)
        out.append(f'<path d="{rect(bx - 0.4, by0, bx + 0.4, by1)}" fill="{ink}" stroke="{ink}" stroke-width="0.3"/>')
key_icon(256, 129, 28, "#7D7D7D")
out.append(node(48, rect(150, 82, 330, 162), "#000000", 0.5, "#000000"))
for n, semis in ((49, 0), (50, 12), (51, 24)):
    y = ROOT_Y - semis * 4
    out.append(node(n, rect(LEFT, y - 0.3, LEFT + WIDTH, y + 0.3), "#3E3E3E", 0.5))

# The swap hint, while Shift is held on a loaded sample: "swap" and "shift +"
# with the Sample key's icon, as the empty state words it, small above the
# strip's right end, clear of the end marker's flag (x 451..456) and the
# strip's tallest peaks (y 166). Node 54, a box in the background drawn just
# after it, hides it the rest of the time (display.cpp sample_view, swap_x and
# swap_y). It lies over the root lane's gap and under the strip and marks, which
# draw over it. At 13 px the key's seven bars would run together, so it
# draws three, spaced apart.
SWAP_X, SWAP_Y = 355, 151
out.append(f'<text transform="matrix(1 0 0 1 357 162)" fill="#CBCBCB" {FONT} font-size="11px">swap</text>')
out.append(f'<text transform="matrix(1 0 0 1 391 162)" fill="#7D7D7D" {FONT} font-size="11px">shift +</text>')
key_icon(427, 151.5, 13, "#7D7D7D", bars=((170, 212, 283), (230, 185, 308), (290, 212, 283)))
out.append(node(54, rect(0, 0, 87, 14), "#000000", 0.5, "#000000"))   # placed at (SWAP_X, SWAP_Y)

# Node 55: the strip's faint centre line, under the waveform.
out.append(node(55, rect(LEFT, STRIP_Y - 0.3, LEFT + WIDTH, STRIP_Y + 0.3), "#3E3E3E", 0.5))
# Nodes 0..3: the strip's waveform, 15 columns each, as display.cpp's
# place_waveform draws its flat stand-in: a zigzag 0.75 px either side of the
# centre line, out and back 3/8 px lower, a thin closed ribbon of 64 points.
for h in range(4):
    out_pts = [(-54, 0)]
    for c in range(15):
        out_pts += [(-54 + 7.2 * c + 1.8, -0.75), (-54 + 7.2 * c + 5.4, 0.75)]
    out_pts.append((54, 0))
    pts = out_pts + [(x, y + 0.375) for x, y in reversed(out_pts)]
    out.append(node(h, outline(pts), "#666666", 0.5))
out.append(node(12, rect(-0.7, -19, 0.7, 19), "#e6e6e6", 0.5, "#e6e6e6"))                     # pos
out.append(node(13, outline([(0, -0.5), (8, -0.5), (8, -3), (11, 0), (8, 3), (8, 0.5), (0, 0.5)])))   # scan, turned to point back
out.append(node(14, rect(0, -0.15, 64, 0.15)))                                                 # shpe's window, rising
out.append(node(15, rect(-3, -0.5, 3, 0.5)))                                                   # scan frozen
for i in (16, 17):
    out.append(node(i, rect(-0.4, -19, 0.4, 19), "#666666", 0.5, "#666666"))                   # spry
for i in range(18, 22):
    out.append(node(i, rect(0, -0.3, WIDTH, 0.3), "#444444", 0.5))                              # chord lanes
out.append(node(22, rect(0, -0.15, 64, 0.15)))                                                 # shpe's window, falling
for s, length in enumerate((6, 16, 40)):
    for g in range(8):
        out.append(node(23 + 8 * s + g, rect(-length / 2, -1.3, length / 2, 1.3)))           # grains
# Nodes 52 and 53: the stock sampler's start and end markers on the strip,
# Shift's start and end, each a bar with a flag at its top pointing into the
# part grains read. Each starts at its flag's corner, where both edges are long.
out.append(node(52, outline([(-0.6, -23), (5, -23), (0.6, -18.6), (0.6, 21), (-0.6, 21)]), "#e6e6e6", 0.5, "#e6e6e6"))
out.append(node(53, outline([(0.6, -23), (0.6, 21), (-0.6, 21), (-0.6, -18.6), (-5, -23)]), "#e6e6e6", 0.5, "#e6e6e6"))
for i in range(16):
    grey = f"#{i * 17:02x}{i * 17:02x}{i * 17:02x}"
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="1"/>')
out.append("</g>")
target = ROOT / "panel.svg.fragment" if ID == "grains" else pathlib.Path(sys.argv[2])
target.write_text("\n".join(out) + "\n")
print("wrote", target)
