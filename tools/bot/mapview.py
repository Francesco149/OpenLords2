"""mapview.py — where a map cell is on screen, asked of retail itself.

The strategic view is a staggered-row grid. Retail's cursor→cell resolver (FUN_00429ba4, ported as
engine_camera_cursor_to_cell) reads the viewport geometry below and a tile grid of int32 per view cell
(row stride 0x41) whose value is the LOGICAL map cell's byte offset (cell*8, 64x64 cells). So:
  visible(s)        → {logical cell: (x, y) screen point}, from the geometry + grid (one read)
  locate(s, col, row) → a screen point over that cell, confirmed by hovering it (DAT_00568974)
  show(s, county)   → recentre the view on a county via its minimap click point (l2state)
"""
import struct

X_ORIGIN, X_STEP, HALF_STEP = 0x52AFDC, 0x568220, 0x567950
Y_ORIGIN, Y_STEP = 0x553244, 0x53F65C
COL_COUNT, ROW_COUNT = 0x53E8AC, 0x56D67C
SCROLL_COL, SCROLL_ROW = 0x5651B4, 0x5651B8
TILE_GRID = 0x55CEA0              # int32 [row][0x41]: logical cell*8, >= 0xfff0000 = invalid
HOVER_TILE = 0x568974            # the tile index under the cursor (written by the resolver every frame)
VIEW_W = 476                     # the map viewport ends where the HUD begins (x ~ 480)


def geometry(s):
    g = struct.unpack("<10i", b"".join(s.read(va, 4) for va in
                      (X_ORIGIN, X_STEP, HALF_STEP, Y_ORIGIN, Y_STEP, COL_COUNT, ROW_COUNT, SCROLL_COL, SCROLL_ROW, TILE_GRID)))
    keys = ("x_origin", "x_step", "half_step", "y_origin", "y_step", "cols", "rows", "scroll_col", "scroll_row", "_")
    return dict(zip(keys, g))


def visible(s):
    """logical cell index → screen point at the centre of its view cell (unverified)"""
    g = geometry(s)
    rows, cols = g["rows"] + 1, g["cols"] + 1
    raw = s.read(TILE_GRID + 4 * (g["scroll_row"] * 0x41), 4 * 0x41 * (rows + 1))
    grid = struct.unpack(f"<{len(raw) // 4}i", raw)
    out = {}
    for vr in range(1, rows):
        for vc in range(0, cols):
            t = grid[vr * 0x41 + vc + g["scroll_col"]] if vc + g["scroll_col"] < 0x41 else 0x0fff0000
            if t < 0 or t >= 0x0fff0000:
                continue
            x = vc * g["x_step"] + g["x_origin"] - (g["half_step"] if vr & 1 else 0) + g["x_step"] // 2
            y = g["y_step"] * (vr + 1) + g["y_origin"] + g["y_step"] // 2
            if 8 <= x < VIEW_W and 40 <= y < 470:
                out.setdefault(t // 8, (x, y))
    return out


def hovered(s, x, y):
    s.move(x, y, frames=2)
    t = s.i32(HOVER_TILE)
    return t // 8 if 0 <= t < 0x0fff0000 else None


def locate(s, col, row, tries=6):
    """a screen point over logical cell (col,row), confirmed by a hover; None if not on screen"""
    want = row * 64 + col
    pt = visible(s).get(want)
    if pt is None:
        return None
    x0, y0 = pt
    for dx, dy in [(0, 0), (0, -4), (0, 4), (-6, 0), (6, 0), (0, 8)][:tries]:
        if hovered(s, x0 + dx, y0 + dy) == want:
            return (x0 + dx, y0 + dy)
    return None


CELLS = 0x522F90                 # 64x64 cells x 8 B; +7 = county id (l2state)


def county_cells(s, county):
    """every logical cell (col, row) of a county"""
    cells = s.read(CELLS, 64 * 64 * 8)
    return [(i % 64, i // 64) for i in range(64 * 64) if cells[i * 8 + 7] == county]


def toward(s, county, near=None):
    """a visible screen point inside `county` (the cell nearest `near`=(col,row) if given), hover-confirmed"""
    cells = s.read(CELLS, 64 * 64 * 8)
    vis = visible(s)
    cand = [(cell, pt) for cell, pt in vis.items() if cells[cell * 8 + 7] == county]
    if near:
        cand.sort(key=lambda cp: (cp[0] % 64 - near[0]) ** 2 + (cp[0] // 64 - near[1]) ** 2)
    for cell, (x, y) in cand[:8]:
        if hovered(s, x, y) == cell:
            return (x, y), (cell % 64, cell // 64)
    return None, None


GRID_ROWS = 0x84                 # absolute staggered rows covered by TILE_GRID (scroll_row + rows <= 0x80)


def absolute(s, col, row):
    """the absolute staggered (C, R) of logical cell (col,row) in the tile grid, or None"""
    raw = s.read(TILE_GRID, 4 * 0x41 * GRID_ROWS)
    grid = struct.unpack(f"<{len(raw) // 4}i", raw)
    want = (row * 64 + col) * 8
    for i, t in enumerate(grid):
        if t == want:
            return (i % 0x41, i // 0x41)
    return None


# edge-park points (window-relative 1-px border; FUN_00432221): direction → cursor position
EDGE = {"N": (320, 0), "S": (320, 479), "E": (639, 240), "W": (0, 240),
        "NE": (639, 0), "NW": (0, 0), "SE": (639, 479), "SW": (0, 479)}


def scroll_to(s, col, row, max_frames=400):
    """edge-scroll until logical cell (col,row) sits well inside the view; works in army-move mode (0x10)"""
    target = absolute(s, col, row)
    if target is None:
        return False
    for _ in range(max_frames // 4):
        g = geometry(s)
        dc = target[0] - (g["scroll_col"] + g["cols"] // 2)
        dr = target[1] - (g["scroll_row"] + g["rows"] // 2)
        if abs(dc) <= 2 and abs(dr) <= 6:
            break
        d = ("N" if dr < -6 else "S" if dr > 6 else "") + ("W" if dc < -2 else "E" if dc > 2 else "")
        s.post(0x200, 0, (EDGE[d][1] << 16) | EDGE[d][0])
        s.run(4)
    s.post(0x200, 0, (240 << 16) | 240)        # park the cursor mid-map: scrolling stops
    s.run(4)
    return locate(s, col, row) is not None


def show(s, county_point):
    """recentre the main view on a minimap point (l2state county 'minimap_click')"""
    s.click(*county_point)
    s.run(10)
