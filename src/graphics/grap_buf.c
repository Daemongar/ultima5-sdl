#include "common/common.h"
#include "key/key.h"
#include "time/time.h"

#include "vars.h"

#include "grap_buf.h"
#include "animate.h"
#include "reveal.h"
#if defined(ENABLE_ANIMATED_COLOR_TILES)
#include "tile_colors.h"
#endif

#if defined(ENABLE_GRAP_OVERLAY_DEBUG)
#include "common/dbg_font_data.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_enableDebugOverlay = 0;

// 640x400 EGA pixels
u8* g_linearEgaBuffer0;

// 640x400 EGA pixels
u8* g_linearEgaBuffer1;
u32* g_linearColorPixels0;
u8* g_linearColorMask0;

static u32* s_colorPixels[2];
static u8* s_colorMask[2];
static u32* s_colorTileAtlas;
static int s_colorTileColumns;
static int s_colorTileCount;
static bool* s_colorTileClear; // per atlas tile: has see-through pixels
int g_colorScale = 1;

// The color layer buffers are allocated for the largest scale.
#define COLOR_LAYER_PIXELS ((size_t)renderWidth * renderHeight * maxColorScale * maxColorScale)

#if defined(ENABLE_GRAP_OVERLAY)
// 256k
u8* g_linearOverlayBuffer;
#endif

byte g_grapPenColor = 0;

extern VideoDriverParams D_52ba_vdp;

static u8 s_bitMask[8] = {0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1};

static u8 s_colorTable[16] = {0, 1, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15}; // ?

static byte* s_tileset;

#if defined(ENABLE_ANIMATED_COLOR_TILES)
static byte* s_tilesetPristine; // tileset as loaded, to spot animated tiles
static u32* s_dynCache;         // full-color renders of animated tiles
#endif

static pfGrapFlushFrame* s_pfFlushFrame;

static u32 s_lastPresentTick = 0;
static bool s_dirty = false;

// T1K:19d9
static u16 s_tileRevealSeed = 0;

void GRAP_BUF_Initialize(pfGrapFlushFrame* pfFlushFrame)
{
    g_linearEgaBuffer0 = malloc(renderWidth * renderHeight);
    memset(g_linearEgaBuffer0, 0, renderWidth * renderHeight);

    g_linearEgaBuffer1 = malloc(renderWidth * renderHeight);
    memset(g_linearEgaBuffer1, 0, renderWidth * renderHeight);

    for (int page = 0; page < 2; page++)
    {
        s_colorPixels[page] = calloc(COLOR_LAYER_PIXELS, sizeof(*s_colorPixels[page]));
        s_colorMask[page] = calloc(COLOR_LAYER_PIXELS, sizeof(*s_colorMask[page]));
    }
    g_linearColorPixels0 = s_colorPixels[0];
    g_linearColorMask0 = s_colorMask[0];

#if defined(ENABLE_GRAP_OVERLAY)
    g_linearOverlayBuffer = malloc(hiresWidth * hiresHeight);
    memset(g_linearOverlayBuffer, 0, hiresWidth * hiresHeight);
#endif

    s_pfFlushFrame = pfFlushFrame;
}

void GRAP_BUF_Cleanup(void)
{
    if (s_tileset != NULL)
    {
        free(s_tileset);
        s_tileset = NULL;
    }

#if defined(ENABLE_ANIMATED_COLOR_TILES)
    free(s_tilesetPristine);
    s_tilesetPristine = NULL;
    free(s_dynCache);
    s_dynCache = NULL;
#endif

    free(g_linearEgaBuffer0);
    free(g_linearEgaBuffer1);

    g_linearEgaBuffer0 = NULL;
    g_linearEgaBuffer1 = NULL;

    for (int page = 0; page < 2; page++)
    {
        free(s_colorPixels[page]);
        free(s_colorMask[page]);
        s_colorPixels[page] = NULL;
        s_colorMask[page] = NULL;
    }
    g_linearColorPixels0 = NULL;
    g_linearColorMask0 = NULL;

#if defined(ENABLE_GRAP_OVERLAY)
    free(g_linearOverlayBuffer);
    g_linearOverlayBuffer = NULL;
#endif
}

void GRAP_BUF_SetPenColor(byte color)
{
    g_grapPenColor = color;
}

void GRAP_BUF_SetPage(int page)
{
    D_52ba_vdp._52d8_page = page;
}

static inline byte* GetPage(int page)
{
    if (page == 0)
        return g_linearEgaBuffer0;
    else if (page == 1)
        return g_linearEgaBuffer1;
    return 0;
}

static inline u32* GetColorPixels(int page)
{
    return page == 0 || page == 1 ? s_colorPixels[page] : NULL;
}

static inline u8* GetColorMask(int page)
{
    return page == 0 || page == 1 ? s_colorMask[page] : NULL;
}

// x, y, width and height in render pixels.
static inline void ClearColorMaskRect(int page, int x, int y, int width, int height)
{
    u8* mask = GetColorMask(page);
    if (mask == NULL)
        return;

    int scale = g_colorScale;
    for (int row = 0; row < height * scale; row++)
    {
        memset(&mask[(y * scale + row) * colorWidth + x * scale], 0, width * scale);
    }
}

#if defined(ENABLE_GRAP_OVERLAY)
static inline void GrPutOverlayPixel(int x, int y, int egaColor) { g_linearOverlayBuffer[y * hiresWidth + x] = egaColor; }
#endif

static inline void GrPutPixel(int page, int x, int y, int egaColor)
{
    byte* target = GetPage(page);
    int dstX = x * loresToHiresRatio;
    int dstY = y * loresToHiresRatio;
    int offset = dstY * renderWidth + dstX;

    target[offset] = egaColor;
    target[offset + 1] = egaColor;
    target[offset + renderWidth] = egaColor;
    target[offset + renderWidth + 1] = egaColor;
    ClearColorMaskRect(page, dstX, dstY, loresToHiresRatio, loresToHiresRatio);
}

static inline void GrPutByte(int page, int x, int y, byte egaByte)
{
    byte* target = GetPage(page);
    int dstX = x * loresToHiresRatio;
    int dstY = y * loresToHiresRatio;
    byte* row = &target[dstY * renderWidth + dstX];
    byte leftColor = egaByte >> 4;
    byte rightColor = egaByte & 0xf;

    row[0] = row[1] = leftColor;
    row[2] = row[3] = rightColor;
    memcpy(&row[renderWidth], row, 4);
    ClearColorMaskRect(page, dstX, dstY, loresToHiresRatio * 2, loresToHiresRatio);
}

// safe but slow
static inline void GrPutClippedPixel(int page, int x, int y, int egaColor)
{
    if (x >= 0 && x < loresWidth && y >= 0 && y < loresHeight)
    {
        GrPutPixel(page, x, y, egaColor);
    }
}

#if defined(ENABLE_GRAP_OVERLAY)
static inline void GrPutOverlayMonoByte(int x, int y, byte b, int egaColor)
{
    g_linearOverlayBuffer[y * hiresWidth + x + 0] = ((b >> 7) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 1] = ((b >> 6) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 2] = ((b >> 5) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 3] = ((b >> 4) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 4] = ((b >> 3) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 5] = ((b >> 2) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 6] = ((b >> 1) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 7] = ((b >> 0) & 1) ? egaColor : 0;
}
#endif

#if defined(ENABLE_GRAP_OVERLAY_DEBUG)
static int _debugPenX;
static int _debugPenY;

static void PrintDebugOverlayChar(int penX, int penY, uint ch)
{
    static u8 mask[8] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80};

    char* p = font8x8_basic[ch];
    for (int y = 0; y < 8; y++)
    {
        byte b = p[y];
        for (int x = 0; x < 8; x++)
        {
            byte col = b & mask[x] ? 15 : 1;

            int dstX = penX * 16 + x * 2;
            int dstY = penY * 16 + y * 2;
            GrPutOverlayPixel(dstX, dstY, col);
            GrPutOverlayPixel(dstX + 1, dstY, col);
            GrPutOverlayPixel(dstX, dstY + 1, col);
            GrPutOverlayPixel(dstX + 1, dstY + 1, col);
        }
    }
}

static void PrintDebugOverlayString(char* str)
{
    if (_debugPenY >= 25)
        return;

    int len = strlen(str);
    for (int i = 0; i < len; i++)
    {
        PrintDebugOverlayChar(_debugPenX, _debugPenY, str[i]);
        _debugPenX++;
        if (_debugPenX >= 40)
        {
            _debugPenX = 0;
            _debugPenY++;
        }
    }

    _debugPenX = 0;
    _debugPenY++;
}

static void PrintDebugOverlayStringFmt(char* str, ...)
{
    char debugBuffer[256];

    va_list args;
    va_start(args, str);

    if (vsprintf(debugBuffer, str, args) < 0)
    {
        debugBuffer[0] = 0;
    }

    va_end(args);

    PrintDebugOverlayString(debugBuffer);
}

static void DisplayDebugMessages(void)
{
#if _DEBUG
    if (!g_enableDebugOverlay)
        return;

    memset(g_linearOverlayBuffer, 0, hiresWidth * hiresHeight);

    _debugPenX = 0;
    _debugPenY = 0;

    PrintDebugOverlayStringFmt("P: X%d Y%d", D_5c5a[0]._2_x, D_5c5a[0]._3_y);
    PrintDebugOverlayStringFmt("Map: %d", D_5893_map_id);
    PrintDebugOverlayStringFmt("Time: %02d-%02d %02d:%02d", D_587d, D_587e, D_587f, D_5881);
#endif
    return;

    if (D_5893_map_id > 0)
    {
        PrintDebugOverlayString("NPC:");

        for (int i = 0; i < 32; i++)
        {
            if (i >= 8)
            {
                PrintDebugOverlayString("...");
                break;
            }

            PrintDebugOverlayStringFmt("%2x: %d X%d Y%d %d %d %d %d %d", i, D_5f5e[i]._0, D_5f5e[i]._2_x, D_5f5e[i]._4_y,
                                       D_5f5e[i]._6_z, D_5f5e[i]._8, D_5f5e[i]._a, D_5f5e[i]._c, D_5f5e[i]._e);
        }
    }
    else
    {
        PrintDebugOverlayString("Actor:");

        for (int i = 0; i < 32; i++)
        {
            if (i >= 8)
            {
                PrintDebugOverlayString("...");
                break;
            }
            PrintDebugOverlayStringFmt("%2x: T%02x A%02x X%d Y%d Z%d %d %d %d", i, D_5c5a[i]._0_tile, D_5c5a[i]._1_animTile,
                                       D_5c5a[i]._2_x, D_5c5a[i]._3_y, D_5c5a[i]._4_z, D_5c5a[i]._5, D_5c5a[i]._6,
                                       D_5c5a[i]._7);
        }
    }
}
#endif

void GRAP_BUF_Present(void)
{
#if defined(ENABLE_GRAP_OVERLAY_DEBUG)
    DisplayDebugMessages();
#endif

    (*s_pfFlushFrame)();

    s_dirty = false;
}

extern u32 TIME_CurrentFrame(void);

void GRAP_BUF_FlushPendingPresent(void)
{
    if (!s_dirty)
        return;

    u32 tick = TIME_CurrentFrame();
    if (s_lastPresentTick != tick)
    {
        s_lastPresentTick = tick;

        GRAP_BUF_Present();
    }
}

void GRAP_BUF_MarkDirty(void)
{
    s_dirty = true;
}

void GRAP_BUF_PrintChar(byte* ptr, int offset, byte fgColor, byte bgColor, int penX, int penY)
{
    byte* p = &ptr[offset];
    for (int y = 0; y < 8; y++)
    {
        byte b = p[y];
        for (int x = 0; x < 8; x++)
        {
            byte col = b & s_bitMask[x] ? s_colorTable[fgColor] : s_colorTable[bgColor];

            GrPutPixel(D_52ba_vdp._52d8_page, penX * 8 + x, penY * 8 + y, col);
        }
    }

    s_dirty = true;
}

// The same scroll on the color layer, in render pixels.
static void ScrollColorLayer(int x, int width, int y1, int y2, int dy, bool up)
{
    int scale = g_colorScale;
    x *= scale;
    width *= scale;
    y1 *= scale;
    y2 = (y2 + 1) * scale - 1;
    dy *= scale;

    for (int i = 0; i <= y2 - y1; i++)
    {
        int y = up ? y1 + i : y2 - i;
        int from = up ? y + dy : y - dy;
        size_t dst = (size_t)y * colorWidth + x;
        if (from >= y1 && from <= y2)
        {
            size_t src = (size_t)from * colorWidth + x;
            memmove(&s_colorPixels[0][dst], &s_colorPixels[0][src], width * sizeof(u32));
            memmove(&s_colorMask[0][dst], &s_colorMask[0][src], width);
        }
        else
        {
            memset(&s_colorPixels[0][dst], 0, width * sizeof(u32));
            memset(&s_colorMask[0][dst], 0, width);
        }
    }
}

// 0x27
void GRAP_BUF_ScrollWindow(int left, int top, int right, int bottom, int amount)
{
    int x = left * loresToHiresRatio;
    int width = (right - left + 1) * loresToHiresRatio;
    int y1 = top * loresToHiresRatio;
    int y2 = (bottom + 1) * loresToHiresRatio - 1;
    int dy = abs(amount) * loresToHiresRatio;

    ScrollColorLayer(x, width, y1, y2, dy, amount < 0);

    if (amount < 0)
    {
        for (int y = y1; y <= y2 - dy; y++)
        {
            memcpy(&g_linearEgaBuffer0[y * renderWidth + x], &g_linearEgaBuffer0[(y + dy) * renderWidth + x], width);
        }

        for (int y = y2 - dy + 1; y <= y2; y++)
        {
            memset(&g_linearEgaBuffer0[y * renderWidth + x], 0, width);
        }
    }
    else
    {
        for (int y = y2; y >= y1 + dy; y--)
        {
            memcpy(&g_linearEgaBuffer0[y * renderWidth + x], &g_linearEgaBuffer0[(y - dy) * renderWidth + x], width);
        }

        for (int y = y1; y < y1 + dy; y++)
        {
            memset(&g_linearEgaBuffer0[y * renderWidth + x], 0, width);
        }
    }

    s_dirty = true;
}

// 0x3f
void GRAP_BUF_FillWindow(int x1, int y1, int x2, int y2, int xorMode)
{
    if (y1 > y2)
        return;
    if (x1 > x2)
        return;

    byte* target = GetPage(D_52ba_vdp._52d8_page);
    x1 *= loresToHiresRatio;
    y1 *= loresToHiresRatio;
    x2 = (x2 + 1) * loresToHiresRatio - 1;
    y2 = (y2 + 1) * loresToHiresRatio - 1;
    ClearColorMaskRect(D_52ba_vdp._52d8_page, x1, y1, x2 - x1 + 1, y2 - y1 + 1);

    for (int y = y1; y <= y2; y++)
    {
        if (!xorMode)
        {
            memset(&target[y * renderWidth + x1], g_grapPenColor, x2 - x1 + 1);
        }
        else
        {
            for (int x = x1; x <= x2; x++)
            {
                target[y * renderWidth + x] ^= g_grapPenColor;
                target[y * renderWidth + x] &= 0xf;
            }
        }
    }

    if (D_52ba_vdp._52d8_page == 0)
    {
        s_dirty = true;
    }
}

#if defined(ENABLE_ANIMATED_COLOR_TILES)
//
// Animated full-color tiles. The game animates tiles (water, lava, flags,
// fireplaces, shorelines, the clock, ladders, the ending) by rewriting the
// 16-color tile data, which the static TILES.BMP atlas bypasses. Water and lava
// only scroll down a row per frame, so their atlas tiles are drawn with the
// same scroll. Any other tile whose data no longer matches the tileset as
// loaded is rendered in full color from its current data, using the same steps
// as tools/upscale_tiles.py and its generated palette (tile_colors.h), and
// cached until it changes again.
//

#define COLOR_TILE_COUNT 512
#define TILE_BYTES 0x80

static u32 s_tileAnimFrame;
static byte s_dynSource[COLOR_TILE_COUNT][TILE_BYTES];
static bool s_dynValid[COLOR_TILE_COUNT];

#define DYN_MAX_SIZE (tilePixelSize * maxColorScale)

// Scale2x on an n x n index image, clamped at the tile edges.
static void DynScale2x(const byte* src, int n, byte* dst)
{
    for (int y = 0; y < n; y++)
    {
        for (int x = 0; x < n; x++)
        {
            byte c = src[y * n + x];
            byte a = src[(y > 0 ? y - 1 : y) * n + x];
            byte d = src[(y < n - 1 ? y + 1 : y) * n + x];
            byte l = src[y * n + (x > 0 ? x - 1 : x)];
            byte r = src[y * n + (x < n - 1 ? x + 1 : x)];
            byte* out = &dst[y * 2 * n * 2 + x * 2];

            out[0] = (l == a && l != d && a != r) ? a : c;
            out[1] = (a == r && a != l && r != d) ? r : c;
            out[n * 2] = (d == l && d != r && l != a) ? l : c;
            out[n * 2 + 1] = (r == d && r != a && d != l) ? d : c;
        }
    }
}

// Renders a tile at colorTileSize, as tools/upscale_tiles.py does with
// --scale 2 (32x32) or --scale 4 (64x64).
static void DynRenderTile(int tileIdx, const byte* data, u32* out)
{
    static byte bufA[DYN_MAX_SIZE * DYN_MAX_SIZE];
    static byte bufB[DYN_MAX_SIZE * DYN_MAX_SIZE];
    byte* idx = bufA;
    int n = 16;

    (void)tileIdx;
    for (int y = 0; y < 16; y++)
    {
        for (int x = 0; x < 16; x += 2)
        {
            idx[y * 16 + x] = data[y * 8 + x / 2] >> 4;
            idx[y * 16 + x + 1] = data[y * 8 + x / 2] & 0x0f;
        }
    }
    while (n < colorTileSize)
    {
        byte* next = idx == bufA ? bufB : bufA;
        DynScale2x(idx, n, next);
        idx = next;
        n *= 2;
    }

    // Lighten the top edge of each shape and darken the bottom edge, in a band
    // half an original pixel thick.
    int band = g_colorScale;
    for (int y = 0; y < n; y++)
    {
        for (int x = 0; x < n; x++)
        {
            byte c = idx[y * n + x];
            u32 base = g_tilePalette[c];
            bool top = false;
            bool bottom = false;
            for (int k = 1; k <= band; k++)
            {
                top |= idx[(y >= k ? y - k : 0) * n + x] != c;
                bottom |= idx[(y + k < n ? y + k : n - 1) * n + x] != c;
            }
            float shade = 1.0f;
            if (top) shade *= 1.10f;
            if (bottom) shade *= 0.86f;

            u32 px = 0;
            for (int k = 0; k < 3; k++)
            {
                float v = (float)((base >> (16 - 8 * k)) & 0xff) * shade;
                int iv = v < 0 ? 0 : (v > 255 ? 255 : (int)v);
                px |= (u32)iv << (16 - 8 * k);
            }
            out[y * n + x] = 0xff000000 | px;
        }
    }
}

// Water 1-3 and lava only scroll down one row per animation frame.
static bool IsScrollingTile(int tileIdx)
{
    return tileIdx == 0x01 || tileIdx == 0x02 || tileIdx == 0x03 || tileIdx == 0x8f;
}

// Returns the full-color source for a tile: its colorTileSize pixels square,
// row stride and a vertical scroll (in rows) to apply when reading.
static const u32* GetColorTileSource(int tileIdx, int* stride, int* scroll)
{
    int size = colorTileSize;
    *scroll = 0;
    *stride = s_colorTileColumns * size;
    const u32* atlasTile = &s_colorTileAtlas[(tileIdx / s_colorTileColumns) * size * *stride +
                                             (tileIdx % s_colorTileColumns) * size];

    if (s_tileset == NULL || s_tilesetPristine == NULL || tileIdx >= COLOR_TILE_COUNT)
        return atlasTile;

    if (IsScrollingTile(tileIdx))
    {
        *scroll = (int)((s_tileAnimFrame * loresToHiresRatio * g_colorScale) % size);
        return atlasTile;
    }

    const byte* current = &s_tileset[TILE_BYTES * tileIdx];
    if (memcmp(current, &s_tilesetPristine[TILE_BYTES * tileIdx], TILE_BYTES) == 0)
        return atlasTile;

    if (s_dynCache == NULL)
    {
        s_dynCache = malloc(COLOR_TILE_COUNT * DYN_MAX_SIZE * DYN_MAX_SIZE * sizeof(u32));
        if (s_dynCache == NULL)
            return atlasTile;
    }

    u32* cached = &s_dynCache[tileIdx * DYN_MAX_SIZE * DYN_MAX_SIZE];
    if (!s_dynValid[tileIdx] || memcmp(current, s_dynSource[tileIdx], TILE_BYTES) != 0)
    {
        DynRenderTile(tileIdx, current, cached);
        memcpy(s_dynSource[tileIdx], current, TILE_BYTES);
        s_dynValid[tileIdx] = true;
    }

    *stride = size;
    return cached;
}
#endif

void GRAP_BUF_LoadTileset(byte* tileset)
{
    s_tileset = tileset;

#if defined(ENABLE_ANIMATED_COLOR_TILES)
    free(s_tilesetPristine);
    s_tilesetPristine = NULL;
    if (tileset != NULL)
    {
        s_tilesetPristine = malloc(COLOR_TILE_COUNT * TILE_BYTES);
        if (s_tilesetPristine != NULL)
            memcpy(s_tilesetPristine, tileset, COLOR_TILE_COUNT * TILE_BYTES);
    }
    s_tileAnimFrame = 0;
    memset(s_dynValid, 0, sizeof(s_dynValid));
#endif
}

// Snapshot of everything drawn, so an overlay screen (the in-game Options
// screen) can be shown and the game screen put back exactly afterwards.
typedef struct ScreenSnapshot
{
    u8* ega[2];
    u32* colorPixels[2];
    u8* colorMask[2];
    u8* overlay;
    byte penColor;
} ScreenSnapshot;

static ScreenSnapshot s_snapshot;

static void* SnapshotCopy(void** dst, const void* src, size_t size)
{
    if (src == NULL)
        return NULL;
    if (*dst == NULL)
        *dst = malloc(size);
    if (*dst != NULL)
        memcpy(*dst, src, size);
    return *dst;
}

void GRAP_BUF_SaveScreen(void)
{
    size_t pixels = (size_t)renderWidth * renderHeight;

    SnapshotCopy((void**)&s_snapshot.ega[0], g_linearEgaBuffer0, pixels);
    SnapshotCopy((void**)&s_snapshot.ega[1], g_linearEgaBuffer1, pixels);
    for (int page = 0; page < 2; page++)
    {
        SnapshotCopy((void**)&s_snapshot.colorPixels[page], s_colorPixels[page], COLOR_LAYER_PIXELS * sizeof(u32));
        SnapshotCopy((void**)&s_snapshot.colorMask[page], s_colorMask[page], COLOR_LAYER_PIXELS);
    }
    SnapshotCopy((void**)&s_snapshot.overlay, g_linearOverlayBuffer, (size_t)hiresWidth * hiresHeight);
    s_snapshot.penColor = g_grapPenColor;
}

// Hide the full-color tile layer (and the debug overlay), which otherwise sit
// on top of anything drawn in 16 colors. Put back by GRAP_BUF_RestoreScreen.
void GRAP_BUF_ClearColorLayer(void)
{
    for (int page = 0; page < 2; page++)
    {
        if (s_colorMask[page] != NULL)
            memset(s_colorMask[page], 0, COLOR_LAYER_PIXELS);
    }
    if (g_linearOverlayBuffer != NULL)
        memset(g_linearOverlayBuffer, 0, (size_t)hiresWidth * hiresHeight);

    s_dirty = true;
}

void GRAP_BUF_RestoreScreen(void)
{
    size_t pixels = (size_t)renderWidth * renderHeight;

    if (s_snapshot.ega[0] != NULL && g_linearEgaBuffer0 != NULL)
        memcpy(g_linearEgaBuffer0, s_snapshot.ega[0], pixels);
    if (s_snapshot.ega[1] != NULL && g_linearEgaBuffer1 != NULL)
        memcpy(g_linearEgaBuffer1, s_snapshot.ega[1], pixels);
    for (int page = 0; page < 2; page++)
    {
        if (s_snapshot.colorPixels[page] != NULL && s_colorPixels[page] != NULL)
            memcpy(s_colorPixels[page], s_snapshot.colorPixels[page], COLOR_LAYER_PIXELS * sizeof(u32));
        if (s_snapshot.colorMask[page] != NULL && s_colorMask[page] != NULL)
            memcpy(s_colorMask[page], s_snapshot.colorMask[page], COLOR_LAYER_PIXELS);
    }
    if (s_snapshot.overlay != NULL && g_linearOverlayBuffer != NULL)
        memcpy(g_linearOverlayBuffer, s_snapshot.overlay, (size_t)hiresWidth * hiresHeight);
    g_grapPenColor = s_snapshot.penColor;

    s_dirty = true;
}

void GRAP_BUF_LoadColorTileAtlas(u32* pixels, int columns, int tileCount, int tileSize)
{
    int scale = pixels != NULL && tileSize == tilePixelSize * 2 ? 2 : 1;

    if (scale != g_colorScale)
    {
        // Whatever the color layer holds is laid out for the old scale.
        g_colorScale = scale;
        for (int page = 0; page < 2; page++)
        {
            if (s_colorMask[page] != NULL)
                memset(s_colorMask[page], 0, COLOR_LAYER_PIXELS);
        }
#if defined(ENABLE_ANIMATED_COLOR_TILES)
        memset(s_dynValid, 0, sizeof(s_dynValid));
#endif
    }

    s_colorTileAtlas = pixels;
    s_colorTileColumns = columns;
    s_colorTileCount = tileCount;
    s_dirty = true;

    free(s_colorTileClear);
    s_colorTileClear = pixels != NULL ? calloc(tileCount, sizeof(*s_colorTileClear)) : NULL;
    if (s_colorTileClear != NULL)
    {
        int stride = columns * tileSize;
        for (int i = 0; i < tileCount; i++)
        {
            const u32* tile = &pixels[(i / columns) * tileSize * stride + (i % columns) * tileSize];
            for (int y = 0; y < tileSize && !s_colorTileClear[i]; y++)
                for (int x = 0; x < tileSize; x++)
                    if ((tile[y * stride + x] >> 24) == 0)
                    {
                        s_colorTileClear[i] = true;
                        break;
                    }
        }
    }
}

// True if the full-color tile sheet is in use and has this tile, including
// tiles past the original 512 that only the sheet has (walk_anim.c).
bool GRAP_BUF_HasColorTile(int tileIdx)
{
    return s_colorTileAtlas != NULL && tileIdx >= 0 && tileIdx < s_colorTileCount;
}

// True if the full-color tile has see-through pixels, so whatever is drawn
// in its square first (the terrain under a creature) shows through them.
bool GRAP_BUF_TileHasClearPixels(int tileIdx)
{
    return s_colorTileClear != NULL && s_colorTileAtlas != NULL && tileIdx >= 0 && tileIdx < s_colorTileCount &&
           s_colorTileClear[tileIdx];
}

void GRAP_BUF_UnloadTileset(void)
{
    if (s_tileset != NULL)
    {
        free(s_tileset);
        s_tileset = NULL;
    }
}

void GRAP_BUF_AnimateTileset(void)
{
#if defined(ENABLE_ANIMATED_COLOR_TILES)
    s_tileAnimFrame++;
#endif
    AnimateTileset(s_tileset);
}

void GRAP_BUF_UpdateTimeTileset(int mode, byte hour, byte minute)
{
    AnimateTimeTileset(s_tileset, mode, hour, minute);
}

// A tile as colorTileSize square RGB pixels, for drawing outside the game's
// own screen (the wide view's extra columns): the full-color atlas when loaded
// (animated like GRAP_BUF_PutTile), otherwise the 16-color tile doubled with
// the given palette (the color scale is then 1).
static void RenderTileRGB(int tileIdx, const u32* palette, u32* out, bool over)
{
    if (s_colorTileAtlas != NULL && tileIdx >= 0 && tileIdx < s_colorTileCount)
    {
        int size = colorTileSize;
#if defined(ENABLE_ANIMATED_COLOR_TILES)
        int stride;
        int scroll;
        const u32* source = GetColorTileSource(tileIdx, &stride, &scroll);
#else
        int stride = s_colorTileColumns * size;
        int scroll = 0;
        const u32* source = &s_colorTileAtlas[(tileIdx / s_colorTileColumns) * size * stride +
                                              (tileIdx % s_colorTileColumns) * size];
#endif
        for (int y = 0; y < size; y++)
        {
            const u32* row = &source[((y - scroll + size) % size) * stride];
            for (int x = 0; x < size; x++)
            {
                if (!over || (row[x] >> 24) != 0)
                    out[y * size + x] = 0xff000000 | row[x];
            }
        }
        return;
    }

    if (s_tileset == NULL || tileIdx < 0 || tileIdx >= 512)
    {
        for (int i = 0; i < tilePixelSize * tilePixelSize; i++)
            out[i] = palette[0];
        return;
    }

    const byte* tile = &s_tileset[0x80 * tileIdx];
    for (int y = 0; y < tilePixelSize; y++)
    {
        for (int x = 0; x < tilePixelSize; x++)
        {
            int sx = x / loresToHiresRatio;
            byte packed = tile[(y / loresToHiresRatio) * (tileSourcePixelSize / 2) + sx / 2];
            out[y * tilePixelSize + x] = palette[(sx & 1) ? (packed & 0x0f) : (packed >> 4)];
        }
    }
}

void GRAP_BUF_RenderTileRGB(int tileIdx, const u32* palette, u32* out)
{
    RenderTileRGB(tileIdx, palette, out, false);
}

// The same, drawn over what out already holds: see-through pixels are skipped.
void GRAP_BUF_RenderTileRGBOver(int tileIdx, const u32* palette, u32* out)
{
    RenderTileRGB(tileIdx, palette, out, true);
}

void GRAP_BUF_PutTile(int tileX, int tileY, int tileIdx, int xOffset, int yOffset)
{
    int page = D_52ba_vdp._52d8_page;
    if (s_colorTileAtlas != NULL && tileIdx >= 0 && tileIdx < s_colorTileCount)
    {
        u32* colorPixels = GetColorPixels(page);
        u8* colorMask = GetColorMask(page);
        int size = colorTileSize;
        int dstX = (tileX * tilePixelSize + xOffset * loresToHiresRatio) * g_colorScale;
        int dstY = (tileY * tilePixelSize + yOffset * loresToHiresRatio) * g_colorScale;
#if defined(ENABLE_ANIMATED_COLOR_TILES)
        int stride;
        int scroll;
        const u32* source = GetColorTileSource(tileIdx, &stride, &scroll);
#else
        int srcX = (tileIdx % s_colorTileColumns) * size;
        int srcY = (tileIdx / s_colorTileColumns) * size;
        int atlasWidth = s_colorTileColumns * size;
#endif

        for (int y = 0; y < size; y++)
        {
            int outY = dstY + y;
            if (outY < 0 || outY >= colorHeight)
                continue;

            for (int x = 0; x < size; x++)
            {
                int outX = dstX + x;
                if (outX < 0 || outX >= colorWidth)
                    continue;

                int dst = outY * colorWidth + outX;
#if defined(ENABLE_ANIMATED_COLOR_TILES)
                int srcRow = (y - scroll + size) % size;
                u32 pixel = source[srcRow * stride + x];
#else
                u32 pixel = s_colorTileAtlas[(srcY + y) * atlasWidth + srcX + x];
#endif
                if ((pixel >> 24) == 0)
                    continue; // see-through: keep what is already there
                colorPixels[dst] = pixel;
                colorMask[dst] = 1;
            }
        }

        s_dirty = true;
        return;
    }

    byte* tile = &s_tileset[0x80 * tileIdx];

    int width = tileSourcePixelSize;
    int height = tileSourcePixelSize;
    int dstX = tileX * width + xOffset;
    int dstY = tileY * height + yOffset;

    for (int y = dstY; y < dstY + height; y++)
    {
        for (int x = dstX; x < dstX + width; x += 2)
        {
            GrPutByte(D_52ba_vdp._52d8_page, x, y, *tile++);
        }
    }

    s_dirty = true;
}


void GRAP_BUF_PutAnimatedMoongateTile(int tileX, int tileY, int visibleRows, byte floorType, int xOffset, int yOffset)
{
    byte backup[0x80];

    AnimateTile_BuildMoongateTile(s_tileset, visibleRows, floorType, backup);
    GRAP_BUF_PutTile(tileX & 0xff, tileY & 0xff, 0x116, xOffset, yOffset); // magic circle tile is used as scratch tile
    AnimateTile_RestoreMoongateTile(s_tileset, backup);
}

static u16 StepLfsrSeed(u16 seed, u16 mask)
{
    int carry = seed & 1;

    seed >>= 1;
    if (carry)
    {
        seed ^= mask;
    }

    return seed;
}

static void PutTileRevealPixel(int tileX, int tileY, int tileIdx, int relX, int relY, int xOffset, int yOffset)
{
    byte packed;
    int dstX;
    int dstY;

    packed = s_tileset[0x80 * tileIdx + relY * (tileSourcePixelSize / 2) + (relX >> 1)];
    dstX = tileX * tileSourcePixelSize + xOffset + relX;
    dstY = tileY * tileSourcePixelSize + yOffset + relY;

    if ((relX & 1) == 0)
    {
        GrPutPixel(D_52ba_vdp._52d8_page, dstX, dstY, packed >> 4);
    }
    else
    {
        GrPutPixel(D_52ba_vdp._52d8_page, dstX, dstY, packed & 0x0f);
    }

    s_dirty = true;
}

// T1K:1b27
void GRAP_BUF_PutTileRevealStep(int tileX, int tileY, int tileIdx, int progress, int xOffset, int yOffset)
{
    u16 seed;

    if (progress == 0)
    {
        PutTileRevealPixel(tileX, tileY, tileIdx, 0, 0, xOffset, yOffset);
        return;
    }

    seed = s_tileRevealSeed;
    if (progress == 1 || seed == 0)
    {
        seed = 1;
    }

    PutTileRevealPixel(tileX, tileY, tileIdx, seed >> 4, seed & 0x0f, xOffset, yOffset);

    // 1b7f
    s_tileRevealSeed = StepLfsrSeed(seed, 0xb8);
}

// https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm#All_cases
void PlotLine(int x1, int y1, int x2, int y2)
{
    int dx = abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int error = dx + dy;
    int e2;

    while (1)
    {
        GrPutPixel(D_52ba_vdp._52d8_page, x1, y1, g_grapPenColor);
        e2 = 2 * error;

        if (e2 >= dy)
        {
            if (x1 == x2)
                break;

            error += dy;
            x1 += sx;
        }
        if (e2 <= dx)
        {
            if (y1 == y2)
                break;

            error += dx;
            y1 += sy;
        }
    }
}

//int ULTIMA_08e6_ClipRectCoord(int* x1, int* y1, int* x2, int* y2);

// 0x27
void GRAP_BUF_Line(int x1, int y1, int x2, int y2)
{
    // ULTIMA_08e6_ClipRectCoord(&x1, &y1, &x2, &y2);

    PlotLine(x1, y1, x2, y2);

    if (D_52ba_vdp._52d8_page == 0)
    {
        s_dirty = true;
    }
}

void GRAP_BUF_LineRectangle(int x1, int y1, int x2, int y2, byte color)
{
    byte x = g_grapPenColor;
    g_grapPenColor = color;

    PlotLine(x1, y1, x2, y1);
    PlotLine(x1, y2, x2, y2);
    PlotLine(x1, y1, x1, y2);
    PlotLine(x2, y1, x2, y2);

    PlotLine(x1, y1, x2, y2);
    PlotLine(x2, y1, x1, y2);

    g_grapPenColor = x;

    s_dirty = true;
}

void GRAP_BUF_Pset(int x, int y)
{
    // ULTIMA_08e6_ClipRectCoord(&x1, &y1, &x2, &y2);

    GrPutClippedPixel(D_52ba_vdp._52d8_page, x, y, g_grapPenColor);

    if (D_52ba_vdp._52d8_page == 0)
    {
        s_dirty = true;
    }
}

static inline byte GetImagePixel(byte packed, int relX)
{
    if ((relX & 1) == 0)
        return packed >> 4;

    return packed & 0x0f;
}

static inline bool MaskHasPixel(byte* maskPtr, int relX)
{
    if (maskPtr == NULL)
        return true;

    return (maskPtr[relX / 8] & (0x80 >> (relX & 7))) == 0;
}

void GRAP_BUF_PutImage(ImageView* view, int x, int y, int flags)
{
    int width = view->width;
    int height = view->height;
    int byteWidth = view->byteWidth;
    int page = D_52ba_vdp._52d8_page;

    if (width <= 0 || height <= 0)
        return;

    bool vflip = (flags & 1) != 0;
    bool hflip = (flags & 2) != 0;

    for (int yy = 0; yy < height; yy++)
    {
        int srcY;
        int dstY;

        if (vflip)
        {
            srcY = height - yy - 1;
            dstY = y + yy + 1;
        }
        else
        {
            srcY = yy;
            dstY = y + yy;
        }

        byte* linePtr = &view->pixels[srcY * view->stride];
        byte* maskPtr = view->hasMask ? &view->maskBits[srcY * view->maskStride] : NULL;
        for (int byteX = 0; byteX < byteWidth; byteX++)
        {
            byte packed = linePtr[byteX];
            int leftRelX = byteX * 2;

            if (hflip)
            {
                int dstX = x + width - 2 - leftRelX;

                if (MaskHasPixel(maskPtr, leftRelX))
                {
                    GrPutClippedPixel(page, dstX, dstY, GetImagePixel(packed, leftRelX));
                }

                leftRelX++;
                if (leftRelX < width && MaskHasPixel(maskPtr, leftRelX))
                {
                    GrPutClippedPixel(page, dstX - 1, dstY, GetImagePixel(packed, leftRelX));
                }
            }
            else
            {
                int dstX = x + leftRelX;

                if (MaskHasPixel(maskPtr, leftRelX))
                {
                    GrPutClippedPixel(page, dstX, dstY, GetImagePixel(packed, leftRelX));
                }

                leftRelX++;
                if (leftRelX < width && MaskHasPixel(maskPtr, leftRelX))
                {
                    GrPutClippedPixel(page, dstX + 1, dstY, GetImagePixel(packed, leftRelX));
                }
            }
        }
    }

    if (page == 0)
    {
        s_dirty = true;
    }
}

void GRAP_BUF_PutBitImage(BitImageView* view, int x, int y)
{
    int w = view->width;
    int h = view->height;
    byte* buf = view->bits;
    int stride = view->stride;
    int page = D_52ba_vdp._52d8_page;

    for (int yy = 0; yy < h; yy++)
    {
        byte* linePtr = &buf[yy * stride];
        for (int xx = 0; xx < w; xx++)
        {
            if (linePtr[xx / 8] & s_bitMask[xx % 8])
            {
                GrPutClippedPixel(page, xx + x, yy + y, 15);
            }
        }
    }

    if (page == 0)
    {
        s_dirty = true;
    }
}

void GRAP_BUF_TransferPage(int srcPage, int dstPage, int x1, int y1, int x2, int y2, int dstX, int dstY)
{
    byte* srcPagePtr = GetPage(srcPage);
    byte* dstPagePtr = GetPage(dstPage);
    u32* srcColorPixels = GetColorPixels(srcPage);
    u32* dstColorPixels = GetColorPixels(dstPage);
    u8* srcColorMask = GetColorMask(srcPage);
    u8* dstColorMask = GetColorMask(dstPage);

    // TODO: check bounds
    int srcX = x1 * loresToHiresRatio;
    int srcY = y1 * loresToHiresRatio;
    int dstScaledX = dstX * loresToHiresRatio;
    int dstScaledY = dstY * loresToHiresRatio;
    int width = (x2 - x1 + 1) * loresToHiresRatio;
    int height = (y2 - y1 + 1) * loresToHiresRatio;

    for (int y = 0; y < height; y++)
    {
        memcpy(&dstPagePtr[(y + dstScaledY) * renderWidth + dstScaledX], &srcPagePtr[(y + srcY) * renderWidth + srcX], width);
    }

    int scale = g_colorScale;
    for (int y = 0; y < height * scale; y++)
    {
        size_t dst = (size_t)(y + dstScaledY * scale) * colorWidth + dstScaledX * scale;
        size_t src = (size_t)(y + srcY * scale) * colorWidth + srcX * scale;
        memcpy(&dstColorPixels[dst], &srcColorPixels[src], width * scale * sizeof(u32));
        memcpy(&dstColorMask[dst], &srcColorMask[src], width * scale);
    }

    if (dstPage == 0)
    {
        s_dirty = true;
    }
}

void GRAP_BUF_TransferPage_Reveal(int srcPage, int dstPage, int x1, int y1, int x2, int y2, int dstX, int dstY)
{
    GRAP_BUF_FlushPendingPresent();

    RevealState state;

    byte* srcPagePtr = GetPage(srcPage);
    byte* dstPagePtr = GetPage(dstPage);

    int w = x2 - x1 + 1;
    int h = y2 - y1 + 1;

    RevealInit(&state, w, h, 1);

    int nextIdx;
    while (RevealNextIndex(&state, &nextIdx))
    {
        int nx = nextIdx % w;
        int ny = nextIdx / w;
        int srcX = (nx + x1) * loresToHiresRatio;
        int srcY = (ny + y1) * loresToHiresRatio;
        int dstPixelX = (nx + dstX) * loresToHiresRatio;
        int dstPixelY = (ny + dstY) * loresToHiresRatio;
        for (int dy = 0; dy < loresToHiresRatio; dy++)
        {
            byte* src = &srcPagePtr[(srcY + dy) * renderWidth + srcX];
            byte* dst = &dstPagePtr[(dstPixelY + dy) * renderWidth + dstPixelX];
            dst[0] = dst[1] = src[0];
            dst[2] = dst[3] = src[1];
        }

        int scale = g_colorScale;
        int block = loresToHiresRatio * scale;
        for (int dy = 0; dy < block; dy++)
        {
            size_t srcOffset = (size_t)(srcY * scale + dy) * colorWidth + srcX * scale;
            size_t dstOffset = (size_t)(dstPixelY * scale + dy) * colorWidth + dstPixelX * scale;
            memcpy(&s_colorPixels[dstPage][dstOffset], &s_colorPixels[srcPage][srcOffset], block * sizeof(u32));
            memcpy(&s_colorMask[dstPage][dstOffset], &s_colorMask[srcPage][srcOffset], block);
        }

        if (state.produced % 512 == 511)
        {
            GRAP_BUF_Present();

            if (KEY_PollKey())
            {
                GRAP_BUF_TransferPage(srcPage, dstPage, x1, y1, x2, y2, dstX, dstY);
                return;
            }

            TIME_SleepMs(1 * 55);
        }
    }

    GRAP_BUF_Present();
}
