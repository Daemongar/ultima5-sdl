#ifndef _GRAPHICS_GRAP_BUF_H
#define _GRAPHICS_GRAP_BUF_H

#include "image.h"

#define hiresWidth 640
#define hiresHeight 400

#define loresWidth 320
#define loresHeight 200

#define hiresPackedStride 80

// Keep legacy game coordinates while rendering 32px tiles at 640x400.
#define loresToHiresRatio 2
#define renderWidth (loresWidth * loresToHiresRatio)
#define renderHeight (loresHeight * loresToHiresRatio)
#define tilePixelSize 32

// Wide view layout (see ComposeFrame in grap_sdl.c), in 640x400 render pixels.
// The original map occupies x 16..367; 3 extra tile columns go on each side.
#define MAP_RENDER_LEFT 16
#define MAP_RENDER_RIGHT 368
#define MAP_RENDER_TOP 16
#define MAP_RENDER_BOTTOM 368
#define WIDE_EXTRA_COLUMNS 3
#define WIDE_EXTRA_WIDTH (WIDE_EXTRA_COLUMNS * tilePixelSize)
#define WIDE_RENDER_WIDTH (hiresWidth + 2 * WIDE_EXTRA_WIDTH)
#define WIDE_EXTRA_ROWS 11

// Tile shown in each extra square (left strip columns 0-2, right 3-5), or -1
// for dark; filled by WIDE_UpdateExtraTiles in wide_view.c.
extern int g_wideExtraTiles[WIDE_EXTRA_ROWS][2 * WIDE_EXTRA_COLUMNS];
// Terrain under a creature in an extra square, drawn first so the creature's
// see-through pixels show it; -1 if none.
extern int g_wideExtraUnder[WIDE_EXTRA_ROWS][2 * WIDE_EXTRA_COLUMNS];
extern bool g_wideExtraTilesReady;

enum
{
    PRESENT_LAYOUT_NORMAL,   // the game's 640x400 screen as is
    PRESENT_LAYOUT_CENTERED, // wide view, non-map screen: centered
    PRESENT_LAYOUT_WIDE_MAP, // wide view, map screen: map widened, panel on the right
};

// Game x for a wide-map-layout x, or -1 inside the extra strips beside the map.
static inline int WideToGameX(int x)
{
    if (x < MAP_RENDER_LEFT)
        return x;
    if (x < MAP_RENDER_LEFT + WIDE_EXTRA_WIDTH)
        return -1;
    if (x < MAP_RENDER_RIGHT + WIDE_EXTRA_WIDTH)
        return x - WIDE_EXTRA_WIDTH;
    if (x < MAP_RENDER_RIGHT + 2 * WIDE_EXTRA_WIDTH)
        return -1;
    return x - 2 * WIDE_EXTRA_WIDTH;
}
#define tileSourcePixelSize (tilePixelSize / loresToHiresRatio)

// The full-color tile layer is drawn at g_colorScale times the render size:
// 1 for a TILES.BMP of 32x32 tiles, 2 (1280x800, 64x64 tiles) for one of
// 64x64 tiles. Everything else is drawn at 640x400 and doubled under it.
#define maxColorScale 2
extern int g_colorScale;
#define colorTileSize (tilePixelSize * g_colorScale)
#define colorWidth (renderWidth * g_colorScale)
#define colorHeight (renderHeight * g_colorScale)

extern u8* g_linearEgaBuffer0;
extern u8* g_linearEgaBuffer1;
extern u8* g_linearOverlayBuffer;
extern u32* g_linearColorPixels0;
extern u8* g_linearColorMask0;

typedef void pfGrapFlushFrame(void);

extern void GRAP_BUF_LoadColorTileAtlas(u32* pixels, int columns, int tileCount, int tileSize);
extern void GRAP_BUF_RenderTileRGB(int tileIdx, const u32* palette, u32* out);
extern void GRAP_BUF_RenderTileRGBOver(int tileIdx, const u32* palette, u32* out);
extern bool GRAP_BUF_TileHasClearPixels(int tileIdx);
extern bool GRAP_BUF_HasColorTile(int tileIdx);
extern void GRAP_BUF_SaveScreen(void);
extern void GRAP_BUF_RestoreScreen(void);
extern void GRAP_BUF_ClearColorLayer(void);
extern void GRAP_BUF_Initialize(pfGrapFlushFrame* pfFlushFrame);
extern void GRAP_BUF_Cleanup(void);
extern void GRAP_BUF_Present(void);
extern void GRAP_BUF_FlushPendingPresent(void);
extern void GRAP_BUF_MarkDirty(void);

extern void GRAP_BUF_SetPenColor(byte color);
extern void GRAP_BUF_SetPage(int page);
extern void GRAP_BUF_PrintChar(byte* ptr, int offset, byte fgColor, byte bgColor, int penX, int penY);
extern void GRAP_BUF_ScrollWindow(int left, int top, int right, int bottom, int amount);
extern void GRAP_BUF_Line(int x1, int y1, int x2, int y2);
extern void GRAP_BUF_Pset(int x, int y);
extern void GRAP_BUF_FillWindow(int x1, int y1, int x2, int y2, int xorMode);
extern void GRAP_BUF_LoadTileset(byte* tileset);
extern void GRAP_BUF_UnloadTileset(void);
extern void GRAP_BUF_AnimateTileset(void);
extern void GRAP_BUF_UpdateTimeTileset(int mode, byte hour, byte minute);
extern void GRAP_BUF_PutAnimatedMoongateTile(int tileX, int tileY, int visibleRows, byte floorType, int xOffset, int yOffset);
extern void GRAP_BUF_PutTileRevealStep(int tileX, int tileY, int tileIdx, int progress, int xOffset, int yOffset);
extern void GRAP_BUF_PutTile(int tileX, int tileY, int tileIdx, int xOffset, int yOffset);
extern void GRAP_BUF_PutImage(ImageView* view, int x, int y, int flags);
extern void GRAP_BUF_PutBitImage(BitImageView* view, int x, int y);
extern void GRAP_BUF_TransferPage(int srcPage, int dstPage, int x1, int y1, int x2, int y2, int dstX, int dstY);
extern void GRAP_BUF_TransferPage_Reveal(int srcPage, int dstPage, int x1, int y1, int x2, int y2, int dstX, int dstY);

#endif
