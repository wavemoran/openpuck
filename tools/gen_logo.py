"""Generate the OpenPuck logo files in docs/brand/ (Split Duel mark).

Usage: python tools/gen_logo.py docs/brand fonts/AtkinsonHyperlegibleNext-Bold.ttf
Needs fontTools. PNGs are written too when CairoSVG is installed
(pip install fonttools cairosvg); otherwise render them from the SVGs, e.g.
rsvg-convert -w 512 openpuck-avatar.svg -o openpuck-avatar.png.

The mark is defined on a 64x64 grid, center (32, 32), with the same numbers
as main/logo.c: a ring of center-line radius 24, one chevron facing each
player and an amber seam between them, held inside the ring. Strokes thicken
as the mark shrinks (ROWS below).
"""
import os, sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

OUT = sys.argv[1]
TTF = sys.argv[2]
os.makedirs(OUT, exist_ok=True)

# --- Palette (docs/brand/README.md) ---
FIELD = "#0F0F17"        # dark field behind the mark
BONE = "#ECEBF5"
VIOLET = "#A08CFF"
AMBER = "#FFB547"
MUTED = "#9A98B3"
TILE_STROKE = "#3A345D"  # violet at 30% on FIELD
DARK = dict(ring=BONE, chevron=VIOLET, seam=AMBER, word=BONE, accent=VIOLET, tag=MUTED)
LIGHT = dict(ring="#0F0F17", chevron="#6247E0", seam="#D98A00", word="#0F0F17", accent="#6247E0",
             tag="#4D4B66")
MONO = dict(ring="currentColor", chevron="currentColor", seam="currentColor")

# --- Mark geometry (64 grid) ---
RING_R = 24
# Stroke rows by rendered size: ring, chevron, seam (0 = none), seam x0,
# chevron wing tip x0, wing tip and apex distance from the center line.
ROWS = {
    "large": (3, 5, 2.5, 12, 21, 7, 15),   # >= 64 px (splash 184-192)
    "48": (4, 6, 3.5, 13, 21, 7, 15),      # menu center, lockup, avatar
    "32": (5, 7, 4.5, 14, 21, 7, 15),
    "16": (7, 9, 0, 0, 20, 8, 16),         # favicon: no seam
}


def mark_group(colors=DARK, diameter=192.0, cx=120.0, cy=120.0, row="large"):
    """The mark with its ring's outer diameter `diameter`, centered at (cx, cy)."""
    ring, chev, seam, seam_x0, wing_x0, wing_dy, apex_dy = ROWS[row]
    s = diameter / (2 * RING_R + ring)
    parts = [f'<circle cx="32" cy="32" r="{RING_R}" fill="none" stroke="{colors["ring"]}" '
             f'stroke-width="{ring}"/>']
    for d in (-1, 1):
        wy, ay = 32 + d * wing_dy, 32 + d * apex_dy
        parts.append(f'<polyline points="{wing_x0},{wy} 32,{ay} {64 - wing_x0},{wy}" fill="none" '
                     f'stroke="{colors["chevron"]}" stroke-width="{chev}" '
                     f'stroke-linejoin="miter" stroke-miterlimit="4" stroke-linecap="butt"/>')
    if seam:
        parts.append(f'<line x1="{seam_x0}" y1="32" x2="{64 - seam_x0}" y2="32" '
                     f'stroke="{colors["seam"]}" stroke-width="{seam}" stroke-linecap="butt"/>')
    return (f'<g transform="translate({cx - 32 * s:.3f} {cy - 32 * s:.3f}) scale({s:.5f})">'
            + "".join(parts) + "</g>")


def svg(w, h, body, bg=None, title="OpenPuck"):
    bgr = f'<rect width="{w}" height="{h}" fill="{bg}"/>' if bg else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" '
            f'role="img" aria-label="{title}"><title>{title}</title>{bgr}{body}</svg>\n')


written = []


def write(name, s):
    with open(os.path.join(OUT, name), "w") as f:
        f.write(s)
    written.append(name)


# --- Wordmark from Atkinson Hyperlegible Next Bold, outlined ---
font = TTFont(TTF)
gs = font.getGlyphSet()
cmap = font.getBestCmap()
upm = font["head"].unitsPerEm
CAP = font["OS/2"].sCapHeight / upm   # cap height per font-size unit
hmtx = font["hmtx"]


# Pair kerning from GPOS is skipped; tracking is applied instead.
def text_paths(text, size, x, baseline, fill, tracking=0.0):
    s = size / upm
    out, cx = [], x
    for i, ch in enumerate(text):
        g = cmap[ord(ch)]
        if ch != " ":
            pen = SVGPathPen(gs)
            gs[g].draw(TransformPen(pen, (s, 0, 0, -s, cx, baseline)))
            out.append(f'<path d="{pen.getCommands()}" fill="{fill}"/>')
        cx += hmtx[g][0] * s
        if i < len(text) - 1:
            cx += tracking * size
    return "".join(out), cx - x


def lockup(colors, tagline=False, size=100.0):
    """Mark left of OPENPUCK (+ optional tagline). Returns (w, h, body)."""
    mark_d = 1.75 * size
    gap = 0.5 * size
    text_x = mark_d + gap
    open_paths, open_w = text_paths("OPEN", size, 0, 0, colors["word"], tracking=0.06)
    puck_paths, puck_w = text_paths("PUCK", size, open_w + 0.06 * size, 0, colors["accent"], tracking=0.06)
    word_w = open_w + 0.06 * size + puck_w
    cap = CAP * size
    block_top, block_bottom = -cap, 0.0          # relative to the wordmark baseline
    tag_paths = ""
    text_w = word_w
    if tagline:
        tsize = 0.3 * size
        tag_baseline = 0.62 * size
        tag_paths, tag_w = text_paths("LIFE COUNTER FIRMWARE", tsize, 0, tag_baseline, colors["tag"],
                                      tracking=0.22)
        block_bottom = tag_baseline
        text_w = max(word_w, tag_w)
    # Vertically center the text block on the mark.
    h = mark_d
    baseline = h / 2 - (block_top + block_bottom) / 2
    body = (mark_group(colors, mark_d, mark_d / 2, h / 2, row="48")
            + f'<g transform="translate({text_x:.2f} {baseline:.2f})">{open_paths}{puck_paths}{tag_paths}</g>')
    return text_x + text_w, h, body


# ---------- Exports ----------
# Mark alone on a 240 canvas at the splash size (outer diameter 192).
write("openpuck-mark.svg", svg(240, 240, mark_group(DARK)))
write("openpuck-mark-light.svg", svg(240, 240, mark_group(LIGHT)))
write("openpuck-mark-mono.svg", svg(240, 240, mark_group(MONO)))
# Favicon: the 16 px row (no seam), on the dark field.
write("openpuck-favicon.svg", svg(16, 16, f'<rect width="16" height="16" rx="3.4" fill="{FIELD}"/>'
                                  + mark_group(DARK, 15, 8, 8, row="16")))

# App icon / avatar: rounded tile, 2 px inner violet-30% stroke, mark at 70%.
side = 240
tile = (f'<rect x="1" y="1" width="{side - 2}" height="{side - 2}" rx="{0.21 * side - 1:.1f}" '
        f'fill="{FIELD}" stroke="{TILE_STROKE}" stroke-width="2"/>')
write("openpuck-avatar.svg", svg(side, side, tile + mark_group(DARK, 0.70 * side, 120, 120, row="48")))

# The device splash at 1:1 (240 round screen, mark at 192).
write("openpuck-device.svg", svg(240, 240, '<circle cx="120" cy="120" r="120" fill="#000"/>' + mark_group(DARK)))

# Lockups (the README header and the installer use the plain ones).
for name, colors, tag in [("openpuck-lockup", DARK, False), ("openpuck-lockup-light", LIGHT, False),
                          ("openpuck-lockup-tagline", DARK, True),
                          ("openpuck-lockup-tagline-light", LIGHT, True)]:
    w, h, body = lockup(colors, tagline=tag)
    write(f"{name}.svg", svg(round(w, 1), round(h, 1), body))

# Social preview 1280x640: dark field, lockup with tagline centered.
w, h, body = lockup(DARK, tagline=True)
sc = 880 / w
social = f'<g transform="translate({(1280 - w * sc) / 2:.1f} {(640 - h * sc) / 2:.1f}) scale({sc:.4f})">{body}</g>'
write("openpuck-social.svg", svg(1280, 640, social, bg=FIELD))

# PNGs (same file names as before, so links keep working).
PNGS = [("openpuck-avatar.svg", "openpuck-avatar.png", 512),
        ("openpuck-device.svg", "openpuck-device-240.png", 240),
        ("openpuck-social.svg", "openpuck-social.png", 1280)]
try:
    import cairosvg
except ImportError:
    cairosvg = None
for src, dst, width in PNGS:
    if cairosvg:
        cairosvg.svg2png(url=os.path.join(OUT, src), write_to=os.path.join(OUT, dst), output_width=width)
        written.append(dst)
    else:
        print(f"skipped {dst}: render with  rsvg-convert -w {width} {src} -o {dst}")
print("wrote", ", ".join(written))
