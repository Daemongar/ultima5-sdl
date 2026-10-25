#include "common/common.h"
#include "common/file.h"
#include "common/options.h"
#include "graphics/grap_buf.h"
#include "macros.h"
#include "vars.h"

#include "hover.h"
#include "topics.h"

#if defined(ENABLE_HOVER_NAMES)

#include <stdio.h>
#include <string.h>

// Names under the mouse: on the overworld, in towns and in combat, a label
// near the pointer names the creature or terrain in the square below it, as
// the Look command would ("a minstrel", "grass"). The names come from
// LOOK2.DAT: an offset table for the 256 map tiles and then the 256 creature
// tiles, each pointing to a string. The label is drawn in the game's font
// over the finished frame, so it never touches the game's own screen.

extern SDL_FRect g_presentRect;
extern int g_presentWidth;
extern int g_presentLayout;
extern bool g_presentFullScreenPage;
extern bool g_presentTitleScreens;
extern bool GRAP_SDL_WindowToRender(float windowX, float windowY, int* renderX, int* renderY);

// Filled by WIDE_UpdateExtraTiles: what each extra square of the wide view
// shows, as a LOOK2.DAT entry, or -1.
extern int g_wideExtraLook[WIDE_EXTRA_ROWS][2 * WIDE_EXTRA_COLUMNS];

#define LOOK_ENTRIES 0x200
#define LOOK_CREATURE 0x100
#define TILE_DARKNESS 0xff
#define ACTOR_NONE 0x16 // GetActorMap value for a square with no one drawn

static char* s_names[LOOK_ENTRIES];
static bool s_namesLoaded;

static bool s_mouseInside;
static bool s_hiddenByKey;
static float s_mouseX;
static float s_mouseY;

static void LoadNames(void)
{
    static char data[0x1000];
    FILE* stream;
    size_t size;

    s_namesLoaded = true;
    stream = FILE_Open("LOOK2.DAT", "rb");
    if (!stream)
        return;
    size = fread(data, 1, sizeof(data) - 1, stream);
    fclose(stream);
    if (size < LOOK_ENTRIES * 2)
        return;
    data[size] = '\0';

    for (int i = 0; i < LOOK_ENTRIES; i++)
    {
        size_t offset = (u8)data[i * 2] | ((u8)data[i * 2 + 1] << 8);
        // "x" and "*" mark tiles with no name.
        if (offset < size && strcmp(&data[offset], "x") != 0 && strcmp(&data[offset], "*") != 0)
            s_names[i] = &data[offset];
    }
}

static void Redraw(void)
{
    GRAP_BUF_MarkDirty();
}

void HOVER_MouseMoved(float x, float y)
{
    s_mouseX = x;
    s_mouseY = y;
    s_mouseInside = true;
    s_hiddenByKey = false;
    Redraw();
}

void HOVER_MouseLeft(void)
{
    s_mouseInside = false;
    Redraw();
}

void HOVER_KeyPressed(void)
{
    if (!s_hiddenByKey)
    {
        s_hiddenByKey = true;
        Redraw();
    }
}

// The LOOK2.DAT entry for an 11x11 map square: the creature drawn there, or
// else the terrain (the same choice ULTIMA_56ac_DrawMap makes).
static int MapSquareEntry(int x, int y)
{
    int tile = GetMapViewport(x, y);

    if (tile == 0)
    {
        int actor = GetActorMap(x, y);
        return actor == ACTOR_NONE ? -1 : LOOK_CREATURE + actor;
    }
    return tile == TILE_DARKNESS ? -1 : tile;
}

// The LOOK2.DAT entry for the map square at a render position, or -1.
static int EntryAt(int renderX, int renderY)
{
    int row;

    // Map screens only: not the title, a full-screen page or a dungeon's
    // first-person view.
    if (g_presentTitleScreens || g_presentFullScreenPage ||
        (D_5893_map_id > 0x20 && D_5893_map_id < 0x80))
    {
        return -1;
    }

    if (renderY < MAP_RENDER_TOP || renderY >= MAP_RENDER_BOTTOM)
        return -1;
    row = (renderY - MAP_RENDER_TOP) / tilePixelSize;

    if (g_presentLayout == PRESENT_LAYOUT_WIDE_MAP)
    {
        int gameX = WideToGameX(renderX);
        if (gameX < 0)
        {
            // An extra square: left strip, or the right one after the map.
            int col;
            if (renderX < MAP_RENDER_LEFT + WIDE_EXTRA_WIDTH)
                col = (renderX - MAP_RENDER_LEFT) / tilePixelSize;
            else
                col = WIDE_EXTRA_COLUMNS + (renderX - MAP_RENDER_RIGHT - WIDE_EXTRA_WIDTH) / tilePixelSize;
            return g_wideExtraLook[row][col];
        }
        renderX = gameX;
    }
    else if (g_presentLayout != PRESENT_LAYOUT_NORMAL)
    {
        return -1;
    }

    if (renderX < MAP_RENDER_LEFT || renderX >= MAP_RENDER_RIGHT)
        return -1;
    return MapSquareEntry((renderX - MAP_RENDER_LEFT) / tilePixelSize, row);
}

// Text in the game's font (charset 0, 8x8 with the top bit on the left),
// each font pixel a pixelW x pixelH rectangle.
static void DrawText(SDL_Renderer* renderer, const char* text, float x, float y, float pixelW, float pixelH)
{
    const u8* font = D_539c[0];
    SDL_FRect pixels[64];

    if (!font)
        return;

    for (; *text; text++, x += 8 * pixelW)
    {
        const u8* glyph = &font[((u8)*text & 0x7f) * 8];
        int count = 0;

        for (int row = 0; row < 8; row++)
        {
            for (int col = 0; col < 8; col++)
            {
                if (glyph[row] & (0x80 >> col))
                    pixels[count++] = (SDL_FRect){x + col * pixelW, y + row * pixelH, pixelW, pixelH};
            }
        }
        SDL_RenderFillRects(renderer, pixels, count);
    }
}

void HOVER_Draw(SDL_Renderer* renderer)
{
    int renderX, renderY;
    int entry;
    char text[40];

    if (!g_options.hoverNames || !s_mouseInside || s_hiddenByKey)
        return;
#if defined(ENABLE_TALK_TOPICS)
    // The topic list is over the map.
    if (TOPICS_IsShowing())
        return;
#endif
    if (!GRAP_SDL_WindowToRender(s_mouseX, s_mouseY, &renderX, &renderY))
        return;
    if ((entry = EntryAt(renderX, renderY)) < 0)
        return;

    if (!s_namesLoaded)
        LoadNames();
    if (!s_names[entry])
        return;

    snprintf(text, sizeof(text), "%s", s_names[entry]);
    text[0] = (char)SDL_toupper((unsigned char)text[0]);
    // "darkness!" and the like end in a mark the label doesn't need.
    size_t length = strlen(text);
    if (length > 0 && text[length - 1] == '!')
        text[--length] = '\0';

    // In window pixels: one font pixel is two render pixels, as the game's
    // own text is drawn.
    float scaleX = g_presentRect.w / g_presentWidth;
    float scaleY = g_presentRect.h / hiresHeight;
    float pixelW = 2 * scaleX;
    float pixelH = 2 * scaleY;
    float padX = 4 * scaleX;
    float padY = 3 * scaleY;
    float boxW = length * 8 * pixelW + 2 * padX;
    float boxH = 8 * pixelH + 2 * padY;

    // Below and to the right of the pointer, kept inside the picture.
    float pointerX = g_presentRect.x + renderX * scaleX;
    float pointerY = g_presentRect.y + renderY * scaleY;
    float boxX = pointerX + 12 * scaleX;
    float boxY = pointerY + 18 * scaleY;
    if (boxX + boxW > g_presentRect.x + g_presentRect.w)
        boxX = pointerX - 4 * scaleX - boxW;
    if (boxY + boxH > g_presentRect.y + g_presentRect.h)
        boxY = pointerY - 4 * scaleY - boxH;

    SDL_FRect box = {boxX, boxY, boxW, boxH};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderRect(renderer, &box);
    DrawText(renderer, text, boxX + padX, boxY + padY, pixelW, pixelH);
}

#endif
