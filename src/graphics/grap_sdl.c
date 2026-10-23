#include "common/common.h"
#include "common/config.h"
#include "common/file.h"
#include "common/options.h"

#include "app_icon.h"
#include "grap_buf.h"
#include "grap_ops.h"
#include "hover.h"
#include "origin.h"
#include "wd.h"

#include <SDL3/SDL.h>

static SDL_Window* s_sdlWindow;
static SDL_Renderer* s_sdlRenderer;
static SDL_Surface* s_sdlSurface;
static SDL_Texture* s_sdlTexture;
static Uint32* s_colorTileAtlas;
static int s_colorTileColumns;
static int s_colorTileCount;
static int s_colorTileSize;

int windowWidth = 1280;
int windowHeight = 960;

// Where the picture is drawn in the window, and how it is laid out (for
// mouse mapping in key_sdl.c).
SDL_FRect g_presentRect = {0, 0, 1280, 960};
int g_presentWidth = hiresWidth; // width of the composed picture
int g_presentLayout = PRESENT_LAYOUT_NORMAL;

// The game's own 640x400 picture (times g_colorScale), before it is laid out
// for the window.
static Uint32* s_frame;

extern u8 D_5893_map_id;

static Uint32 s_egaPaletteOriginal[16] =
{
    0xff000000, 0xff0000aa, 0xff00aa00, 0xff00aaaa, 0xffaa0000, 0xffaa00aa, 0xffaa5500, 0xffaaaaaa,
    0xff555555, 0xff5555ff, 0xff55ff55, 0xff55ffff, 0xffff5555, 0xffff55ff, 0xffffff55, 0xffffffff,
};

#if defined(ENABLE_ENHANCED_PALETTE)
// The same 16 EGA hues/roles (index-for-index), softened: less oversaturated,
// slightly warmer grays/white, so tile art (authored against these exact
// slots) reads the same way but without the harsh raw 0x00/0x55/0xaa/0xff
// stepping of the original EGA DAC. Used when the enhanced_palette setting is on.
static Uint32 s_egaPaletteEnhanced[16] =
{
    0xff0a0a0a, 0xff1e3a8a, 0xff15803d, 0xff0e7490, 0xffb91c1c, 0xff86198f, 0xff92400e, 0xffa8a29e,
    0xff57534e, 0xff3b82f6, 0xff4ade80, 0xff22d3ee, 0xfff87171, 0xffe879f9, 0xfffacc15, 0xfffafaf9,
};
#endif

static const Uint32* CurrentPalette(void)
{
#if defined(ENABLE_ENHANCED_PALETTE)
    if (g_options.enhancedPalette)
        return s_egaPaletteEnhanced;
#endif
    return s_egaPaletteOriginal;
}

void GRAP_SDL_FlushFrame(void);

void AUDIO_SDL_Init(void);
void AUDIO_SDL_Cleanup(void);

// TILES.BMP is looked for in the game data folder, then next to the program,
// then three folders up from it: the repository root when running a fresh build
// from build/linux/<config>/ or u5win/x64/<config>/.
static bool FindColorTileAtlas(char* path, int size)
{
    FILE* fp = FILE_Open("TILES.BMP", "rb");
    if (fp != NULL)
    {
        fclose(fp);
        FILE_ResolvePath("TILES.BMP", path, size);
        return true;
    }

    const char* base = SDL_GetBasePath();
    if (base == NULL)
        return false;

    static const char* s_candidates[] = {"TILES.BMP", "../../../TILES.BMP"};
    for (int i = 0; i < ARRAYSIZE(s_candidates); i++)
    {
        SDL_snprintf(path, size, "%s%s", base, s_candidates[i]);
        SDL_IOStream* io = SDL_IOFromFile(path, "rb");
        if (io != NULL)
        {
            SDL_CloseIO(io);
            return true;
        }
    }
    return false;
}

static void LoadColorTileAtlas(void)
{
    char path[1024];
    if (!FindColorTileAtlas(path, sizeof(path)))
        return;

    SDL_Surface* source = SDL_LoadBMP(path);
    if (source == NULL)
    {
        fprintf(stderr, "Couldn't load optional %s: %s\n", path, SDL_GetError());
        return;
    }

    // 32 tiles across, each 32x32 or (drawn at 1280x800) 64x64 pixels.
    int tileSize = source->w / 32;
    if (source->w % 32 != 0 || (tileSize != tilePixelSize && tileSize != tilePixelSize * 2) ||
        source->h <= 0 || source->h % tileSize != 0)
    {
        fprintf(stderr, "%s must be 32 tiles across, each %dx%d or %dx%d pixels\n", path,
                tilePixelSize, tilePixelSize, tilePixelSize * 2, tilePixelSize * 2);
        SDL_DestroySurface(source);
        return;
    }

    SDL_Surface* converted = SDL_ConvertSurface(source, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(source);
    if (converted == NULL)
    {
        fprintf(stderr, "Couldn't convert %s to 32-bit color: %s\n", path, SDL_GetError());
        return;
    }

    s_colorTileSize = tileSize;
    s_colorTileColumns = converted->w / tileSize;
    int rows = converted->h / tileSize;
    s_colorTileCount = s_colorTileColumns * rows;
    size_t pixelCount = (size_t)converted->w * (size_t)converted->h;
    s_colorTileAtlas = malloc(pixelCount * sizeof(*s_colorTileAtlas));
    if (s_colorTileAtlas == NULL)
    {
        fprintf(stderr, "Couldn't allocate color tiles for %s\n", path);
        SDL_DestroySurface(converted);
        s_colorTileCount = 0;
        s_colorTileColumns = 0;
        return;
    }

    // See-through pixels (magenta, or mostly clear in a BMP with alpha) are
    // stored as 0; all others as opaque colors.
    for (int y = 0; y < converted->h; y++)
    {
        const Uint32* src = (const Uint32*)((const Uint8*)converted->pixels + y * converted->pitch);
        for (int x = 0; x < converted->w; x++)
        {
            Uint32 rgb = src[x] & 0x00ffffff;
            bool clear = (src[x] >> 24) < 0x80 || rgb == 0xff00ff;
            s_colorTileAtlas[y * converted->w + x] = clear ? 0 : 0xff000000 | rgb;
        }
    }

    SDL_DestroySurface(converted);
    if (g_options.colorTiles)
    {
        GRAP_BUF_LoadColorTileAtlas(s_colorTileAtlas, s_colorTileColumns, s_colorTileCount, s_colorTileSize);
    }
    fprintf(stderr, "Loaded %d full-color %dx%d tiles from %s\n", s_colorTileCount, tileSize, tileSize, path);
}

// F8: read TILES.BMP again, so changes to the tile sheet show without
// restarting. The map picks up the new tiles the next time it is drawn.
void GRAP_SDL_ReloadColorTiles(void)
{
    GRAP_BUF_LoadColorTileAtlas(NULL, 0, 0, 0);
    free(s_colorTileAtlas);
    s_colorTileAtlas = NULL;
    s_colorTileColumns = 0;
    s_colorTileCount = 0;
    s_colorTileSize = 0;

    LoadColorTileAtlas();
}

// Shrink a windowed size (keeping its shape) so it fits on the primary display.
static void FitWindowToDisplay(int* width, int* height)
{
    SDL_Rect usable;
    if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable))
        return;

    int maxWidth = usable.w * 9 / 10;
    int maxHeight = usable.h * 9 / 10;
    if (*width > maxWidth || *height > maxHeight)
    {
        float scale = SDL_min((float)maxWidth / *width, (float)maxHeight / *height);
        *width = (int)(*width * scale);
        *height = (int)(*height * scale);
    }
}

void GRAP_SDL_Initialize(void)
{
    windowWidth = CONFIG_GetInt("window", "width", windowWidth);
    windowHeight = CONFIG_GetInt("window", "height", windowHeight);

    SDL_WindowFlags windowFlags = 0;
    int windowMode = g_options.windowMode;

    switch (windowMode)
    {
    case OPTION_WINDOW_FULLSCREEN:
        windowFlags |= SDL_WINDOW_FULLSCREEN;
        break;
    case OPTION_WINDOW_BORDERED:
        windowFlags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;
        break;
    default:
        windowFlags |= SDL_WINDOW_RESIZABLE;
        FitWindowToDisplay(&windowWidth, &windowHeight);
        break;
    }

    SDL_CreateWindowAndRenderer("Ultima V: Warriors of Destiny", windowWidth, windowHeight, windowFlags, &s_sdlWindow, &s_sdlRenderer);

    // Window and taskbar icon: the ankh from tools/make_icon.py.
    SDL_Surface* icon = SDL_CreateSurfaceFrom(APP_ICON_SIZE, APP_ICON_SIZE, SDL_PIXELFORMAT_RGBA32,
                                              (void*)g_appIcon, APP_ICON_SIZE * 4);
    if (icon != NULL)
    {
        SDL_SetWindowIcon(s_sdlWindow, icon);
        SDL_DestroySurface(icon);
    }

    if (windowMode == OPTION_WINDOW_WINDOWED)
    {
        SDL_SetWindowPosition(s_sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    // The picture is scaled to the live window size, which in fullscreen and
    // bordered modes differs from the requested width/height.
    SDL_GetWindowSizeInPixels(s_sdlWindow, &windowWidth, &windowHeight);

    // Big enough for the wide view at the largest color scale; smaller frames
    // use the top left corner.
    s_sdlSurface = SDL_CreateSurface(WIDE_RENDER_WIDTH * maxColorScale, hiresHeight * maxColorScale,
                                     SDL_GetPixelFormatForMasks(32, 0xff0000, 0xff00, 0xff, 0xff000000));
    s_frame = malloc(sizeof(Uint32) * hiresWidth * hiresHeight * maxColorScale * maxColorScale);

    s_sdlTexture = SDL_CreateTextureFromSurface(s_sdlRenderer, s_sdlSurface);
    SDL_SetTextureScaleMode(s_sdlTexture, SDL_SCALEMODE_LINEAR);

    if (SDL_MUSTLOCK(s_sdlSurface))
    {
        exit(0);
    }

    if (s_sdlSurface->pitch != WIDE_RENDER_WIDTH * maxColorScale * 4 || s_frame == NULL)
    {
        exit(0);
    }

    GRAP_BUF_Initialize(GRAP_SDL_FlushFrame);
    LoadColorTileAtlas();
}

void GRAP_SDL_Cleanup(void)
{
    GRAP_BUF_Cleanup();
    GRAP_BUF_LoadColorTileAtlas(NULL, 0, 0, 0);
    free(s_colorTileAtlas);
    s_colorTileAtlas = NULL;
    s_colorTileColumns = 0;
    s_colorTileCount = 0;

    free(s_frame);
    s_frame = NULL;
    SDL_DestroyTexture(s_sdlTexture);
    SDL_DestroySurface(s_sdlSurface);
    SDL_DestroyRenderer(s_sdlRenderer);
    SDL_DestroyWindow(s_sdlWindow);

    s_sdlTexture = NULL;
    s_sdlSurface = NULL;
    s_sdlRenderer = NULL;
    s_sdlWindow = NULL;
}

// The 16-color screen (doubled when the color scale is 2) with the full-color
// tile layer over it.
static void LinearToRGB(void)
{
    Uint32* pixels = s_frame;
    int scale = g_colorScale;
    int pitch = colorWidth;
    const Uint32* s_egaPalette = CurrentPalette();

    for (int y = 0; y < colorHeight; y++)
    {
        const u8* ega = &g_linearEgaBuffer0[(y / scale) * renderWidth];
        for (int x = 0; x < colorWidth; x++)
        {
            Uint32 color = s_egaPalette[ega[x / scale] & 0xf];
            int offset = y * pitch + x;
            if (g_linearColorMask0 != NULL && g_linearColorMask0[offset])
            {
                color = 0xff000000 | g_linearColorPixels0[offset];
            }
            pixels[offset] = color;
        }
    }

#if defined(ENABLE_GRAP_OVERLAY)
    for (int y = 0; y < colorHeight; y++)
    {
        for (int x = 0; x < colorWidth; x++)
        {
            byte pixel = g_linearOverlayBuffer[(y / scale) * hiresWidth + x / scale] & 0xf;
            if (pixel != 0)
            {
                pixels[y * pitch + x] = s_egaPalette[pixel];
            }
        }
    }
#endif
}

// Wide view: the game still draws its 640x400 screen. Map screens are laid out
// wider, with the map moved right by WIDE_EXTRA_WIDTH and the stats panel moved
// to the right edge; the strips beside the map are WIDE_EXTRA_WIDTH each
// (black for now, with the frame's top and bottom borders carried across).
// Other screens (title, intro) are centered.
// Set while a full-screen page is shown over the game (the Options screen),
// so it is centered like the title screens rather than laid out as a map.
bool g_presentFullScreenPage;

// True until the title sequence ends (Journey Onward): the logo screens, the
// menu and its "Return to the View" demo are all centered, even when the demo
// shows the overworld (it draws the map itself, so there are no extra columns).
bool g_presentTitleScreens = true;

static bool IsMapScreen(void)
{
    return !g_presentTitleScreens && !g_presentFullScreenPage && D_5893_map_id != 0x40 && D_5893_map_id != 0x41;
}

// Sizes below are in 640x400 render pixels (g_presentWidth too, for mouse
// mapping); the frame itself is g_colorScale times larger.
static void ComposeFrame(void)
{
    Uint32* out = s_sdlSurface->pixels;
    int pitch = s_sdlSurface->pitch / 4;
    Uint32 black = CurrentPalette()[0];
    int scale = g_colorScale;
    int frameWidth = hiresWidth * scale;
    int frameHeight = hiresHeight * scale;

    if (!g_options.wideView)
    {
        g_presentWidth = hiresWidth;
        g_presentLayout = PRESENT_LAYOUT_NORMAL;
        for (int y = 0; y < frameHeight; y++)
            memcpy(&out[y * pitch], &s_frame[y * frameWidth], sizeof(Uint32) * frameWidth);
        return;
    }

    g_presentWidth = WIDE_RENDER_WIDTH;
    int wideWidth = WIDE_RENDER_WIDTH * scale;

    if (!IsMapScreen())
    {
        g_presentLayout = PRESENT_LAYOUT_CENTERED;
        int margin = (wideWidth - frameWidth) / 2;
        for (int y = 0; y < frameHeight; y++)
        {
            Uint32* row = &out[y * pitch];
            for (int x = 0; x < margin; x++)
                row[x] = row[wideWidth - 1 - x] = black;
            memcpy(&row[margin], &s_frame[y * frameWidth], sizeof(Uint32) * frameWidth);
        }
        return;
    }

    g_presentLayout = PRESENT_LAYOUT_WIDE_MAP;

    // Draw the extra squares' tiles (from WIDE_UpdateExtraTiles) once per frame.
    static Uint32 tiles[WIDE_EXTRA_ROWS][2 * WIDE_EXTRA_COLUMNS][tilePixelSize * tilePixelSize * maxColorScale * maxColorScale];
    int size = colorTileSize;
    // Black until the game has drawn its map once (the table starts zeroed,
    // which would otherwise show tile 0 everywhere).
    bool extrasReady = g_wideExtraTilesReady;
    for (int row = 0; extrasReady && row < WIDE_EXTRA_ROWS; row++)
        for (int col = 0; col < 2 * WIDE_EXTRA_COLUMNS; col++)
            if (g_wideExtraTiles[row][col] >= 0)
            {
                if (g_wideExtraUnder[row][col] >= 0 && GRAP_BUF_TileHasClearPixels(g_wideExtraTiles[row][col]))
                {
                    GRAP_BUF_RenderTileRGB(g_wideExtraUnder[row][col], CurrentPalette(), tiles[row][col]);
                    GRAP_BUF_RenderTileRGBOver(g_wideExtraTiles[row][col], CurrentPalette(), tiles[row][col]);
                }
                else
                {
                    GRAP_BUF_RenderTileRGB(g_wideExtraTiles[row][col], CurrentPalette(), tiles[row][col]);
                }
            }

    for (int y = 0; y < frameHeight; y++)
    {
        Uint32* row = &out[y * pitch];
        const Uint32* src = &s_frame[y * frameWidth];
        int renderY = y / scale;
        bool borderRow = renderY < MAP_RENDER_TOP || renderY >= MAP_RENDER_BOTTOM;
        int tileY = y - MAP_RENDER_TOP * scale; // within the extra squares

        for (int x = 0; x < wideWidth; x++)
        {
            int renderX = x / scale;
            int sub = x % scale;
            int srcX = WideToGameX(renderX);
            if (srcX >= 0)
            {
                row[x] = src[srcX * scale + sub];
            }
            else if (borderRow)
            {
                row[x] = src[(MAP_RENDER_LEFT + (renderX % 16)) * scale + sub]; // carry the frame border across
            }
            else
            {
                // An extra square: left strip starts at the map's old left edge,
                // the right strip right after the (moved) map.
                int stripX = renderX < MAP_RENDER_RIGHT ? renderX - MAP_RENDER_LEFT
                                                        : renderX - (MAP_RENDER_RIGHT + WIDE_EXTRA_WIDTH) + WIDE_EXTRA_WIDTH;
                stripX = stripX * scale + sub;
                int col = stripX / size;
                int tileRow = tileY / size;
                int tile = extrasReady ? g_wideExtraTiles[tileRow][col] : -1;
                row[x] = tile >= 0 ? tiles[tileRow][col][(tileY % size) * size + stripX % size] : black;
            }
        }
    }
}

extern void DisplayDebugMessages(void);

// A point in window coordinates (as mouse events give them) to the game's
// 640x400 render coordinates, with the wide view's extra width; false if it's
// outside the picture.
bool GRAP_SDL_WindowToRender(float windowX, float windowY, int* renderX, int* renderY)
{
    float x, y;

    if (g_presentRect.w <= 0 || g_presentRect.h <= 0 ||
        !SDL_RenderCoordinatesFromWindow(s_sdlRenderer, windowX, windowY, &x, &y))
    {
        return false;
    }

    *renderX = (int)((x - g_presentRect.x) * g_presentWidth / g_presentRect.w);
    *renderY = (int)((y - g_presentRect.y) * hiresHeight / g_presentRect.h);
    return x >= g_presentRect.x && y >= g_presentRect.y &&
           *renderX < g_presentWidth && *renderY < hiresHeight;
}

void GRAP_SDL_FlushFrame(void)
{
    LinearToRGB();
    ComposeFrame();

    SDL_UpdateTexture(s_sdlTexture, NULL, s_sdlSurface->pixels, s_sdlSurface->pitch);

    // Window may be resized or (un)maximized at any time; re-read the live size
    // each frame so the picture always fills the window instead of clipping.
    SDL_GetWindowSizeInPixels(s_sdlWindow, &windowWidth, &windowHeight);

    SDL_FRect srcRect = {0, 0, (float)(g_presentWidth * g_colorScale), (float)(hiresHeight * g_colorScale)};
    SDL_FRect dstRect = {0, 0, windowWidth, windowHeight};

    if (g_options.keepAspect)
    {
        // Keep the picture's shape, centered with black bars: 4:3 for the
        // original screen (shown on 4:3 monitors, so 200 lines display as
        // 240), and the same pixel shape for the wide view (about 16:9).
        float aspect = (g_presentWidth / 2.0f) / 240.0f;
        float w = (float)windowWidth;
        float h = (float)windowHeight;
        if (w > h * aspect)
            w = h * aspect;
        else
            h = w / aspect;
        dstRect = (SDL_FRect){(windowWidth - w) / 2.0f, (windowHeight - h) / 2.0f, w, h};
    }

    g_presentRect = dstRect;

    SDL_SetRenderDrawColor(s_sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(s_sdlRenderer);
    SDL_RenderTexture(s_sdlRenderer, s_sdlTexture, &srcRect, &dstRect);
#if defined(ENABLE_HOVER_NAMES)
    HOVER_Draw(s_sdlRenderer);
#endif
    SDL_RenderPresent(s_sdlRenderer);
}

// Apply display settings changed in the Options screen while the game runs.
void GRAP_SDL_ApplyDisplayOptions(void)
{
    switch (g_options.windowMode)
    {
    case OPTION_WINDOW_FULLSCREEN:
        SDL_SetWindowFullscreen(s_sdlWindow, true);
        break;

    case OPTION_WINDOW_BORDERED:
        SDL_SetWindowFullscreen(s_sdlWindow, false);
        SDL_SetWindowBordered(s_sdlWindow, true);
        SDL_SetWindowResizable(s_sdlWindow, true);
        SDL_MaximizeWindow(s_sdlWindow);
        break;

    default:
    {
        int width = CONFIG_GetInt("window", "width", 1280);
        int height = CONFIG_GetInt("window", "height", 960);
        FitWindowToDisplay(&width, &height);
        SDL_SetWindowFullscreen(s_sdlWindow, false);
        SDL_SetWindowBordered(s_sdlWindow, true);
        SDL_SetWindowResizable(s_sdlWindow, true);
        SDL_RestoreWindow(s_sdlWindow);
        SDL_SetWindowSize(s_sdlWindow, width, height);
        SDL_SetWindowPosition(s_sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        break;
    }
    }
    SDL_SyncWindow(s_sdlWindow);

    GRAP_BUF_LoadColorTileAtlas(g_options.colorTiles ? s_colorTileAtlas : NULL, s_colorTileColumns, s_colorTileCount,
                                s_colorTileSize);
}

static GraphicsDriverOps s_winOps =
{
    .Initialize = GRAP_SDL_Initialize,
    .Cleanup = GRAP_SDL_Cleanup,
    .Present = GRAP_BUF_Present,
    .FlushPendingPresent = GRAP_BUF_FlushPendingPresent,
    .SetPenColor = GRAP_BUF_SetPenColor,
    .SetPage = GRAP_BUF_SetPage,
    .PrintChar = GRAP_BUF_PrintChar,
    .ScrollWindow = GRAP_BUF_ScrollWindow,
    .Line = GRAP_BUF_Line,
    .Pset = GRAP_BUF_Pset,
    .FillWindow = GRAP_BUF_FillWindow,
    .LoadTileset = GRAP_BUF_LoadTileset,
    .UnloadTileset = GRAP_BUF_UnloadTileset,
    .AnimateTileset = GRAP_BUF_AnimateTileset,
    .UpdateTimeTileset = GRAP_BUF_UpdateTimeTileset,
    .ShowNextWDFrame = GRAP_BUF_ShowNextWDFrame,
    .AnimateWD = GRAP_BUF_AnimateWD,
    .AnimateOriginLogo = GRAP_BUF_AnimateOriginLogo,
    .PutAnimatedMoongateTile = GRAP_BUF_PutAnimatedMoongateTile,
    .PutTileRevealStep = GRAP_BUF_PutTileRevealStep,
    .PutTile = GRAP_BUF_PutTile,
    .PutImage = GRAP_BUF_PutImage,
    .PutBitImage = GRAP_BUF_PutBitImage,
    .TransferPage = GRAP_BUF_TransferPage,
    .TransferPage_Reveal = GRAP_BUF_TransferPage_Reveal
};

GraphicsDriverOps* GRAP_SDL_GetOps(void)
{
    return &s_winOps;
}
