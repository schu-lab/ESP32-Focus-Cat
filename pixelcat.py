"""Render the firmware Nyan Cat sprite to cat_preview.png (pure PNG, no deps)."""
import struct
import zlib

DISPLAY_W, DISPLAY_H, SCALE = 240, 135, 3
CELL, OX, BASE_OY = 3, 126, 44

BG = (0, 85, 170)
C = {
    '.': BG,
    'X': (0, 0, 0),          # black outline
    'g': (170, 170, 170),    # cat grey
    't': (255, 170, 170),    # pop-tart border and cheeks
    'f': (255, 170, 255),    # frosting
    's': (255, 85, 170),     # sprinkles
    'w': (255, 255, 255),    # white
    'R': (255, 0, 0),
    'O': (255, 170, 0),
    'Y': (255, 255, 0),
    'G': (85, 255, 0),
    'B': (0, 170, 255),
    'P': (85, 85, 255),
    'S': (255, 255, 255),
    'K': (0, 0, 0),
    'V': (0, 45, 96),
    'L': (30, 230, 80),
}

CAT = [
    "........XXXXXXXXXXXXXXXXX........",
    ".......XtttttttttttttttttX.......",
    "......XtttffffffffffffftttX......",
    "......XttffffffsffsfffffttX......",
    "......XtffsfffffffffffffftX......",
    "......XtfffffffffffXXfsfftX..XX..",
    "......XtffffffffffXggXffftX.XggX.",
    "......XtffffffsfffXgggXfftXXgggX.",
    "......XtffffffffffXggggXXXXggggX.",
    "......XtfffsffffffXggggggggggggX.",
    "....XXXtfffffffsfXggggggggggggggX",
    "..XXggXtfsfffffffXgggwXgggggwXggX",
    ".XggggXtfffffffffXgggXXgggXgXXggX",
    "XggXXXXtfffffsfffXgttgggggggggttX",
    "XggX..XttfsffffffXgttgXggXggXgttX",
    ".XX...XtttffffffffXgggXXXXXXXggX.",
    "......XXtttttttttttXggggggggggX..",
    ".....XggXXXXXXXXXXXXXXXXXXXXXX...",
    ".....XggX.XggX......XggX.XggX....",
    ".....XXX...XXX.......XXX..XXX....",
]


grid = [['.'] * DISPLAY_W for _ in range(DISPLAY_H)]


def sp(x, y, c):
    if 0 <= x < DISPLAY_W and 0 <= y < DISPLAY_H:
        grid[y][x] = c


def rect(x0, y0, w, h, c):
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            sp(x, y, c)


def draw_rainbow():
    band_h = 2 * CELL
    top = BASE_OY + 3 * CELL
    seg = 8 * CELL
    right_edge = OX + 6 * CELL
    colors = "ROYGBP"
    x = i = 0
    while x < right_edge:
        off = 0 if i % 2 else band_h // 2
        w = min(seg, right_edge - x)
        for b, color in enumerate(colors):
            rect(x, top + off + b * band_h, w, band_h, color)
        x += w
        i += 1


def draw_cat():
    for row, line in enumerate(CAT):
        for col, ch in enumerate(line):
            if ch != '.':
                rect(OX + col * CELL, BASE_OY + row * CELL, CELL, CELL, ch)


def draw_star(x, y, r=1):
    for d in range(-r, r + 1):
        sp(x + d, y, 'S')
        sp(x, y + d, 'S')


def draw_status_bar(fill_ratio=0.75):
    x, y, w, h = 6, 126, 228, 8
    rect(x, y, w, h, 'K')
    rect(x + 1, y + 1, w - 2, h - 2, 'V')
    rect(x + 1, y + 1, int((w - 2) * fill_ratio), h - 2, 'L')


draw_rainbow()
draw_cat()
draw_status_bar()
for star in [(28, 16, 1), (62, 10, 1), (180, 20, 1), (214, 28, 1),
             (92, 112, 1), (134, 118, 1), (198, 84, 1), (222, 104, 1)]:
    draw_star(*star)


def write_png(path, w, h, pixels):
    def chunk(kind, data):
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
        )

    rows = []
    for line in pixels:
        raw = b''.join(struct.pack('BBB', *C[ch]) for ch in line for _ in range(SCALE))
        rows.extend([raw] * SCALE)
    data = b''.join(b'\x00' + row for row in rows)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack(">IIBBBBB", w * SCALE, h * SCALE, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(data, 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


write_png('cat_preview.png', DISPLAY_W, DISPLAY_H, grid)

print(f"// CAT {len(CAT[0])} x {len(CAT)}")
for row in CAT:
    print(f'"{row}",')
