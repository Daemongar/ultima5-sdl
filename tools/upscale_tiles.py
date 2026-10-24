#!/usr/bin/env python3
"""Build a full-color tile atlas (TILES.BMP) from the original TILES.16.

The original tiles are 512 16x16 images in the 16-color EGA palette. This
script decodes TILES.16 (the game's LZW format), doubles each tile with
Scale2x, which rounds off diagonal staircases instead of plain pixel
doubling, and recolors it with a softened palette and light top/bottom
shading. Black backgrounds are kept, as in the original.

The output is a 24-bit BMP, 32 tiles across, which the SDL builds load from
the game data directory.

Usage: tools/upscale_tiles.py <path/to/TILES.16> <output TILES.BMP>
                              [--scale 2|4] [--transparent]
                              [--algorithm scale2x|xbr] [--keep TILES.BMP]
                              [--header src/graphics/tile_colors.h]

--scale 4 applies the upscaler twice for 64x64 tiles (2048x1024); the default
2 gives 32x32 tiles.

--algorithm picks the upscaler: scale2x (the default) or xbr, which weighs the
colors around each edge before rounding it, for smoother diagonals and fewer
stray pixels. Both keep to the 16-color palette.

--keep copies every tile of an existing sheet that isn't this script's own
output from either upscaler (with the same --scale and --transparent), so
hand-drawn tiles survive regenerating the rest, and any rows past the
original 512 tiles (the Avatar's walking frames).

--transparent makes the black background of creature and object tiles
(256-511) see-through (magenta), so the game draws them over the terrain:
black connected to the tile's edge, not black inside the figure. Tiles whose
black is part of the picture (TRANSPARENT_EXCLUDE) are left as they are.

With --header it also writes the palette the game uses to render animated
tiles, so they match the atlas. Requires Pillow and NumPy.
"""

import struct
import sys

import numpy as np
from PIL import Image

TILE = 16
SCALE = 2
COLUMNS = 32

# The 16 EGA colors, softened two thirds of the way toward warmer tones.
PALETTE = np.array([
    (12, 10, 15),     # 0  black
    (18, 36, 150),    # 1  blue
    (31, 149, 39),    # 2  green
    (27, 156, 162),   # 3  cyan
    (162, 26, 22),    # 4  red
    (149, 34, 149),   # 5  magenta
    (157, 93, 31),    # 6  brown
    (171, 169, 164),  # 7  light gray
    (87, 86, 91),     # 8  dark gray
    (88, 109, 237),   # 9  light blue
    (103, 223, 90),   # 10 light green
    (109, 231, 237),  # 11 light cyan
    (240, 93, 83),    # 12 light red
    (231, 103, 231),  # 13 light magenta
    (248, 231, 96),   # 14 yellow
    (248, 245, 239),  # 15 white
], dtype=np.float32)


def lzw_decompress(data):
    """Port of src/common/lzw.c: 32-bit length, then 9-12 bit LSB-first codes."""
    out_len = struct.unpack_from('<I', data, 0)[0]
    pos, bit_buf, bits = 4, 0, 0
    code_size, next_code, prev = 9, 258, -1
    prefix = [0] * 4096
    suffix = [0] * 4096
    out = bytearray()

    def expand(code):
        stack = []
        while code >= 256:
            stack.append(suffix[code])
            code = prefix[code]
        stack.append(code)
        return stack[::-1]

    while len(out) < out_len:
        while bits < code_size:
            if pos >= len(data):
                break
            bit_buf |= data[pos] << bits
            pos += 1
            bits += 8
        if bits < code_size:
            break
        code = bit_buf & ((1 << code_size) - 1)
        bit_buf >>= code_size
        bits -= code_size

        if code == 256:
            code_size, next_code, prev = 9, 258, -1
            continue
        if code == 257:
            break
        if code < next_code:
            seq = expand(code)
        elif code == next_code and prev >= 0:
            seq = expand(prev)
            seq.append(seq[0])
        else:
            raise ValueError(f"invalid LZW code {code}")
        out.extend(seq)

        if prev >= 0 and next_code < 4096:
            prefix[next_code] = prev
            suffix[next_code] = seq[0]
            next_code += 1
            if code_size < 12 and next_code == (1 << code_size):
                code_size += 1
        prev = code

    return bytes(out[:out_len])


def decode_tiles(raw):
    """512 tiles, 4 bits per pixel, high nibble is the left pixel."""
    data = np.frombuffer(raw, dtype=np.uint8).reshape(-1, TILE, TILE // 2)
    tiles = np.empty((data.shape[0], TILE, TILE), dtype=np.uint8)
    tiles[:, :, 0::2] = data >> 4
    tiles[:, :, 1::2] = data & 0x0f
    return tiles


def scale2x(img):
    """Scale2x/EPX on an index image, clamping at the tile edges."""
    p = np.pad(img, 1, mode='edge')
    c = p[1:-1, 1:-1]
    a, d = p[:-2, 1:-1], p[2:, 1:-1]   # up, down
    l, r = p[1:-1, :-2], p[1:-1, 2:]   # left, right

    e0 = np.where((l == a) & (l != d) & (a != r), a, c)
    e1 = np.where((a == r) & (a != l) & (r != d), r, c)
    e2 = np.where((d == l) & (d != r) & (l != a), l, c)
    e3 = np.where((r == d) & (r != a) & (d != l), d, c)

    out = np.empty((img.shape[0] * 2, img.shape[1] * 2), dtype=img.dtype)
    out[0::2, 0::2] = e0
    out[0::2, 1::2] = e1
    out[1::2, 0::2] = e2
    out[1::2, 1::2] = e3
    return out


def _palette_distances():
    """How different two palette colors look: xBR's YUV distance."""
    rgb = PALETTE.astype(np.float64)
    y = 0.299 * rgb[:, 0] + 0.587 * rgb[:, 1] + 0.114 * rgb[:, 2]
    u = -0.169 * rgb[:, 0] - 0.331 * rgb[:, 1] + 0.5 * rgb[:, 2]
    v = 0.5 * rgb[:, 0] - 0.419 * rgb[:, 1] - 0.081 * rgb[:, 2]
    return (48 * np.abs(y[:, None] - y[None, :]) + 7 * np.abs(u[:, None] - u[None, :]) +
            6 * np.abs(v[:, None] - v[None, :]))


DISTANCE = _palette_distances()


def xbr2x(img):
    """2xBR on an index image, clamping at the tile edges.

    Each pixel becomes four. For each corner, xBR compares how strongly the
    colors around the pixel run along the two diagonals there, from a 5x5
    neighbourhood; when an edge crosses the corner, the corner takes the
    color across the edge, which rounds staircases into smooth lines. Unlike
    the usual xBR it doesn't blend, so the result stays in the 16-color
    palette (the shading and see-through backgrounds work on the indices),
    and it keeps single pixels whole, as blending would.
    """
    p = np.pad(img, 2, mode='edge')
    h, w = img.shape

    def at(dy, dx):
        return p[2 + dy:2 + dy + h, 2 + dx:2 + dx + w]

    def d(a, b):
        return DISTANCE[a, b]

    e = at(0, 0)
    out = np.empty((h * 2, w * 2), dtype=img.dtype)
    # (sy, sx) is the corner: down/up and right/left. The rule is written for
    # the bottom-right corner and mirrored for the others.
    for sy, sx in ((-1, -1), (-1, 1), (1, -1), (1, 1)):
        def n(dy, dx):
            return at(dy * sy, dx * sx)
        b, dd, f, h_ = n(-1, 0), n(0, -1), n(0, 1), n(1, 0)
        c, g, i = n(-1, 1), n(1, -1), n(1, 1)
        f4, h5, i4, i5 = n(0, 2), n(2, 0), n(1, 2), n(2, 1)

        across = d(e, c) + d(e, g) + d(i, f4) + d(i, h5) + 4 * d(h_, f)
        along = d(h_, dd) + d(h_, i5) + d(f, i4) + d(f, b) + 4 * d(e, i)
        # Only round a corner of a shape that carries on away from it: without
        # blending, a lone pixel (an eye, a window, a speck of grass) would
        # otherwise lose all four corners and vanish.
        edge = (across < along) & (e != f) & (e != h_) & ((e == b) | (e == dd))
        new = np.where(d(e, f) <= d(e, h_), f, h_)
        corner = np.where(edge, new, e)

        out[(sy + 1) // 2::2, (sx + 1) // 2::2] = corner
    return out


ALGORITHMS = {'scale2x': scale2x, 'xbr': xbr2x}


# Creature/object tiles whose black background belongs to the picture: stars,
# the magic circle and ladders, people in chairs and beds, stocks and
# shackles, and tiles drawn over grass.
TRANSPARENT_EXCLUDE = {0x100, *range(0x116, 0x119), *range(0x130, 0x140), *range(0x160, 0x168),
                       *range(0x1e0, 0x1e4), *range(0x1f4, 0x1fc)}
CLEAR = np.array([255, 0, 255], dtype=np.uint8)


def edge_black(idx):
    """Pixels of color 0 connected (4-way) to the tile's edge."""
    n = idx.shape[0]
    black = idx == 0
    mask = np.zeros_like(black)
    stack = [(y, x) for y in range(n) for x in (0, n - 1)] + [(y, x) for x in range(n) for y in (0, n - 1)]
    while stack:
        y, x = stack.pop()
        if 0 <= y < n and 0 <= x < n and black[y, x] and not mask[y, x]:
            mask[y, x] = True
            stack += [(y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)]
    return mask


def render_tile(idx16, scale=SCALE, transparent=False, algorithm='scale2x'):
    idx = idx16
    while idx.shape[0] < TILE * scale:
        idx = ALGORITHMS[algorithm](idx)
    rgb = PALETTE[idx].copy()

    # Shading: lighten the top edge of each shape, darken the bottom edge
    # (a band one original pixel's half thick, so it looks the same at any scale).
    band = scale // 2
    top = np.zeros(idx.shape, dtype=bool)
    bottom = np.zeros(idx.shape, dtype=bool)
    for k in range(1, band + 1):
        top |= np.pad(idx, ((k, 0), (0, 0)), mode='edge')[:-k] != idx
        bottom |= np.pad(idx, ((0, k), (0, 0)), mode='edge')[k:] != idx
    shade = np.ones(idx.shape, dtype=np.float32)
    shade[top] *= 1.10
    shade[bottom] *= 0.86
    rgb *= shade[:, :, None]

    out = np.clip(rgb, 0, 255).astype(np.uint8)
    if transparent:
        out[edge_black(idx)] = CLEAR
    return out


def write_header(path):
    """Emit the palette the game uses to render animated tiles the same way."""
    with open(path, 'w') as f:
        f.write("/* Generated by tools/upscale_tiles.py - do not edit by hand. */\n")
        f.write("#ifndef _GRAPHICS_TILE_COLORS_H\n#define _GRAPHICS_TILE_COLORS_H\n\n")
        f.write("static const u32 g_tilePalette[16] =\n{\n" +
                "\n".join("    0x%02x%02x%02x," % tuple(c.astype(int)) for c in PALETTE) + "\n};\n\n#endif\n")
    print(f"Wrote {path}")


def option(args, name):
    """Removes --name VALUE from args and returns VALUE, or None."""
    if name not in args:
        return None
    i = args.index(name)
    value = args[i + 1]
    del args[i:i + 2]
    return value


def main():
    args = sys.argv[1:]
    header = None
    scale = SCALE
    transparent = '--transparent' in args
    if transparent:
        args.remove('--transparent')
    algorithm = option(args, '--algorithm') or 'scale2x'
    if algorithm not in ALGORITHMS:
        sys.exit("--algorithm must be one of: " + ", ".join(ALGORITHMS))
    keep = option(args, '--keep')
    if '--header' in args:
        i = args.index('--header')
        header = args[i + 1]
        del args[i:i + 2]
    if '--scale' in args:
        i = args.index('--scale')
        scale = int(args[i + 1])
        del args[i:i + 2]
        if scale not in (2, 4):
            sys.exit("--scale must be 2 or 4")
    if len(args) != 2:
        sys.exit(__doc__)

    with open(args[0], 'rb') as f:
        tiles = decode_tiles(lzw_decompress(f.read()))

    size = TILE * scale
    rows = (len(tiles) + COLUMNS - 1) // COLUMNS
    atlas = np.zeros((rows * size, COLUMNS * size, 3), dtype=np.uint8)

    existing = None
    if keep:
        existing = np.asarray(Image.open(keep).convert('RGB'))
        if existing.shape[1] != atlas.shape[1] or existing.shape[0] < atlas.shape[0]:
            sys.exit(f"--keep {keep} is {existing.shape[1]}x{existing.shape[0]}, not {atlas.shape[1]}x{atlas.shape[0]}")

    kept = 0
    for i, tile in enumerate(tiles):
        r, c = divmod(i, COLUMNS)
        cell = (slice(r * size, (r + 1) * size), slice(c * size, (c + 1) * size))
        clear = transparent and i >= 0x100 and i not in TRANSPARENT_EXCLUDE
        if existing is not None and not any(np.array_equal(existing[cell], render_tile(tile, scale, clear, a))
                                            for a in ALGORITHMS):
            # Not what this script makes from the original: drawn by hand, so keep it.
            atlas[cell] = existing[cell]
            kept += 1
        else:
            atlas[cell] = render_tile(tile, scale, clear, algorithm)

    if existing is not None and existing.shape[0] > atlas.shape[0]:
        # Tiles past the original 512, such as the Avatar's walking frames
        # (tools/make_walk_frames.py), come across as they are.
        atlas = np.concatenate([atlas, existing[atlas.shape[0]:]], 0)

    Image.fromarray(atlas, 'RGB').save(args[1], format='BMP')
    print(f"Wrote {len(tiles)} tiles ({atlas.shape[1]}x{atlas.shape[0]}) to {args[1]} with {algorithm}"
          + (f", keeping {kept} hand-drawn tiles from {keep}" if existing is not None else ""))
    if header:
        write_header(header)


if __name__ == '__main__':
    main()
