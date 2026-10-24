#!/usr/bin/env python3
"""Add the Avatar's walking frames to a full-color tile sheet.

The game has one picture of the Avatar on foot (tile 0x11c). This makes two
walking frames from it for going up and down, one per foot: that leg bends at
the knee and its boot lifts, while the other stays planted. Going left or
right he leans that way, and steps with a stride (legs apart, the leading foot
off the ground) and a passing frame (legs together), two of each direction.
They go in row 17 of the sheet (tiles 512-517: up/down left foot, right
foot; right stride, passing; left stride, passing), past the original 512
tiles, and the game shows them for a moment after each step on the map (see
src/walk_anim.c).

Usage: tools/make_walk_frames.py TILES.BMP [output.bmp]

The output defaults to the input. Run it again after redrawing the Avatar.
Only 64x64 sheets are supported. Requires Pillow and NumPy.
"""

import sys

import numpy as np
from PIL import Image

COLUMNS = 32
TILE = 64
AVATAR = 0x11c
FIRST_FRAME = 512
CLEAR = np.array([255, 0, 255], dtype=np.uint8)

# Where the Avatar's legs are in his 64x64 tile: each leg's columns, from the
# hips down. The rows from LIFT_FROM are taken out (the bent knee) and the
# rest of the leg and the boot move up to meet the leg above.
LEGS = {'left': (16, 30), 'right': (30, 45)}
LEG_TOP = 48
LIFT_FROM = 50
LIFT_ROWS = 5


def lift_leg(tile, columns):
    """The tile with one leg's lower part raised by LIFT_ROWS."""
    out = tile.copy()
    a, b = columns
    below = tile[LIFT_FROM + LIFT_ROWS:, a:b]
    out[LIFT_FROM:LIFT_FROM + len(below), a:b] = below
    out[TILE - LIFT_ROWS:, a:b] = CLEAR
    return out


# Walking sideways: the body leans toward the way he goes and the legs swing
# apart (stride) or come together (passing). The legs start at LEG_TOP.
LEAN = 3
STRIDE = 5
FRONT_LIFT = 3 # the leading foot is off the ground as it reaches ahead
PASS = 2


def move(region, dx, dy=0):
    """region shifted by dx, dy, with clear pixels where it moved from."""
    h, w = region.shape[:2]
    out = np.empty_like(region)
    out[:] = CLEAR
    xs, xd = (0, dx) if dx >= 0 else (-dx, 0)
    ys, yd = (0, dy) if dy >= 0 else (-dy, 0)
    out[yd:yd + h - abs(dy), xd:xd + w - abs(dx)] = region[ys:ys + h - abs(dy), xs:xs + w - abs(dx)]
    return out


def over(base, layer):
    """layer drawn over base, its clear pixels showing base."""
    out = base.copy()
    solid = ~(layer == CLEAR).all(2)
    out[solid] = layer[solid]
    return out


def side_step(tile, direction, stride):
    """The tile walking right (direction 1) or left (-1): the body leaning that
    way, the legs apart (stride) or together (passing)."""
    clear = np.empty_like(tile)
    clear[:] = CLEAR
    body = clear.copy()
    body[:LEG_TOP] = tile[:LEG_TOP]
    out = move(body, LEAN * direction)

    front, back = (LEGS['right'], LEGS['left']) if direction > 0 else (LEGS['left'], LEGS['right'])
    for columns, sign in ((front, 1), (back, -1)):
        leg = clear.copy()
        a, b = columns
        leg[LEG_TOP:, a:b] = tile[LEG_TOP:, a:b]
        if stride:
            # The front leg reaches ahead, the back one pushes off behind.
            shifted = move(leg, STRIDE * direction * sign, -FRONT_LIFT if sign > 0 else 0)
        else:
            # Passing: both legs under the body, toward each other.
            shifted = move(leg, -PASS * direction * sign + LEAN * direction)
        out = over(out, shifted)
    return out


def main():
    args = sys.argv[1:]
    if len(args) not in (1, 2):
        sys.exit(__doc__)
    source, output = args[0], args[-1]

    sheet = np.asarray(Image.open(source).convert('RGB'))
    if sheet.shape[1] != COLUMNS * TILE:
        sys.exit(f"{source} isn't 32 tiles of {TILE}x{TILE} across")

    rows = max(sheet.shape[0] // TILE, FIRST_FRAME // COLUMNS + 1)
    out = np.zeros((rows * TILE, COLUMNS * TILE, 3), dtype=np.uint8)
    out[:] = CLEAR
    out[:sheet.shape[0]] = sheet

    r, c = divmod(AVATAR, COLUMNS)
    avatar = sheet[r * TILE:(r + 1) * TILE, c * TILE:(c + 1) * TILE]
    frames = [lift_leg(avatar, LEGS['left']), lift_leg(avatar, LEGS['right']),
              side_step(avatar, 1, True), side_step(avatar, 1, False),
              side_step(avatar, -1, True), side_step(avatar, -1, False)]
    for i, frame in enumerate(frames):
        r, c = divmod(FIRST_FRAME + i, COLUMNS)
        out[r * TILE:(r + 1) * TILE, c * TILE:(c + 1) * TILE] = frame

    Image.fromarray(out, 'RGB').save(output, format='BMP')
    print(f"Wrote the Avatar's walking frames (tiles {FIRST_FRAME}-{FIRST_FRAME + len(frames) - 1}) to {output}")


if __name__ == '__main__':
    main()
