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
    out.append(f'<path id="{ID}_node_{2 + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
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

# Static: the octave lanes and their labels.
for semis, label, x in ((0, "root", 437), (12, "+1 oct", 425), (24, "+2 oct", 425)):
    y = ROOT_Y - semis * 4
    out.append(f'<path d="{rect(LEFT, y - 0.3, LEFT + WIDTH, y + 0.3)}" fill="none" stroke="#3E3E3E" stroke-width="0.5"/>')
    out.append(f'<text transform="matrix(1 0 0 1 {x} {y - 4})" fill="#6A6A6A" {FONT} font-size="10px">{label}</text>')

# With no sample: node 45 covers the stage (shown when empty) beneath the hint,
# and node 46 covers the hint itself (shown when a sample is loaded).
out.append(node(45, rect(0, 40, 480, 220), "#000000", 0.5, "#000000"))
# The hint sits between the +2 and +1 octave lanes (y 54 and 102), so its cover
# never cuts a lane.
out.append(f'<text transform="matrix(1 0 0 1 182 80)" fill="#CBCBCB" {FONT} font-size="20px">load a sample</text>')
out.append(f'<text transform="matrix(1 0 0 1 196 97)" fill="#7D7D7D" {FONT} font-size="13px">shift +</text>')
# The sample key's icon (a stand-in: a waveform in a rounded key), after "shift +".
out.append('<path d="M241,86 L263,86 L265,88 L265,98 L263,100 L241,100 L239,98 L239,88 Z" fill="none" stroke="#7D7D7D" stroke-width="1"/>')
out.append('<path d="M243,93 L246,89 L249,98 L252,90 L255,96 L258,92 L261,93 L258,93.6 L255,97.4 L252,91.4 L249,99.4 L246,90.4 L243,94 Z" fill="none" stroke="#7D7D7D" stroke-width="0.8"/>')
out.append(node(46, rect(170, 60, 310, 101), "#000000", 0.5, "#000000"))

# Nodes 0 and 1: the strip's halves, 64 distinct points each (display.cpp reshapes them).
for h in range(2):
    top = [(-54 + i * 108 / 31, -1 - (i % 2) * 0.5) for i in range(32)]
    back = [(x, -y) for x, y in reversed(top)]
    out.append(node(h, outline(top + back), "#666666", 0.5))
out.append(node(10, rect(-0.7, -19, 0.7, 19), "#e6e6e6", 0.5, "#e6e6e6"))                     # pos
out.append(node(11, outline([(0, -0.5), (8, -0.5), (8, -3), (11, 0), (8, 3), (8, 0.5), (0, 0.5)])))   # scan forward
out.append(node(12, outline([(0, -0.5), (-8, -0.5), (-8, -3), (-11, 0), (-8, 3), (-8, 0.5), (0, 0.5)])))  # scan back
out.append(node(13, rect(-3, -0.5, 3, 0.5)))                                                   # scan frozen
for i in (14, 15):
    out.append(node(i, rect(-0.4, -19, 0.4, 19), "#666666", 0.5, "#666666"))                   # spry
for i in range(16, 21):
    out.append(node(i, rect(0, -0.3, WIDTH, 0.3), "#444444", 0.5))                              # chord lanes
for s, length in enumerate((6, 16, 40)):
    for g in range(8):
        out.append(node(21 + 8 * s + g, rect(-length / 2, -1.3, length / 2, 1.3)))           # grains
for i in range(16):
    grey = f"#{i * 17:02x}{i * 17:02x}{i * 17:02x}"
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="1"/>')
out.append("</g>")
target = ROOT / "panel.svg.fragment" if ID == "grains" else pathlib.Path(sys.argv[2])
target.write_text("\n".join(out) + "\n")
print("wrote", target)
