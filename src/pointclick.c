#include "common/common.h"
#include "common/options.h"
#include "graphics/grap_buf.h"
#include "funcs.h"
#include "macros.h"
#include "tiles.h"
#include "vars.h"

#include "pointclick.h"

#if defined(ENABLE_POINT_CLICK)

#include <string.h>

// Point and click, on the overworld and in towns. A click on the map:
//
//   on a square beside the party  does what the square calls for: talk to a
//                                 person, attack a monster, open a door or a
//                                 chest, get an item, board a horse or ship
//   anywhere else                 walks there, a step a turn, by the shortest
//                                 way through the squares in sight (opening
//                                 doors on the way); toward a square out of
//                                 reach or out of the 11x11 view, as near as
//                                 it can get
//
// A walk stops on arrival, on any key, on a right click, when the party is
// blocked twice, or when the map changes. The game is given the same keys a
// player would press, through the gamepad's key queue, and only while it
// waits for a command.

extern SDL_FRect g_presentRect;
extern int g_presentWidth;
extern int g_presentLayout;
extern bool GRAP_SDL_WindowToRender(float windowX, float windowY, int* renderX, int* renderY);
extern void KEY_SDL_QueueKeys(const SDL_Keycode* keys, int count);

#define VIEW 11
#define STEP_MS 130 // between steps, so the map scroll and glides can show
#define ACTOR_NONE 0x16

static bool s_atPrompt;

static bool s_walking;
static int s_walkMap, s_walkLevel;
static int s_targetX, s_targetY; // world square
static Uint64 s_lastStep;
static int s_stepFromX, s_stepFromY; // where the last step started
static bool s_stepPending;
static int s_blocked;

void POINT_SetAtPrompt(bool atPrompt)
{
    s_atPrompt = atPrompt;
}

void POINT_Cancel(void)
{
    if (s_walking)
    {
        s_walking = false;
        GRAP_BUF_MarkDirty();
    }
}

static bool Here(void)
{
    // The overworld (and underworld) and towns; not dungeons or battles.
    return g_options.pointClick && D_5893_map_id <= 0x20;
}

// Squares from one coordinate to another; the overworld wraps at 256.
static int Step(int from, int to)
{
    int step = to - from;
    if (D_5893_map_id == 0)
    {
        step &= 0xff;
        if (step >= 0x80)
            step -= 0x100;
    }
    return step;
}

static int ActorAt(int x, int y)
{
    if (x < 0 || x >= VIEW || y < 0 || y >= VIEW || GetMapViewport(x, y) != 0)
        return -1;
    int actor = GetActorMap(x, y);
    return actor == ACTOR_NONE ? -1 : actor;
}

static bool ClosedDoor(int tile)
{
    return tile == TILE_MAP_DOOR_B8 || tile == TILE_MAP_DOOR_BA;
}

// A square the party can step onto (or open its way onto).
static bool Walkable(int x, int y)
{
    int tile = GetMapViewport(x, y);
    if (tile == 0xff || tile == 0)
        return false; // dark, or someone there
    return ClosedDoor(tile) || ULTIMA_2c4c(D_587c_partyTile, tile) != 0;
}

static const SDL_Keycode s_arrows[4] = {SDLK_UP, SDLK_RIGHT, SDLK_DOWN, SDLK_LEFT};
static const int s_dx[4] = {0, 1, 0, -1};
static const int s_dy[4] = {-1, 0, 1, 0};

// The first step toward (goalX, goalY) in view squares, which may be outside
// the view: a direction 0-3, or -1 if there is no nearer square to go to.
// Squares beside an occupied or blocked goal count as reaching it.
static int FirstStep(int goalX, int goalY)
{
    int distance[VIEW][VIEW];
    int first[VIEW][VIEW];
    int queue[VIEW * VIEW][2];
    int head = 0, tail = 0;

    memset(distance, -1, sizeof(distance));
    distance[5][5] = 0;
    first[5][5] = -1;
    queue[tail][0] = 5;
    queue[tail][1] = 5;
    tail++;

    while (head < tail)
    {
        int x = queue[head][0], y = queue[head][1];
        head++;
        for (int d = 0; d < 4; d++)
        {
            int nx = x + s_dx[d], ny = y + s_dy[d];
            if (nx < 0 || nx >= VIEW || ny < 0 || ny >= VIEW || distance[ny][nx] >= 0 || !Walkable(nx, ny))
                continue;
            distance[ny][nx] = distance[y][x] + 1;
            first[ny][nx] = x == 5 && y == 5 ? d : first[y][x];
            queue[tail][0] = nx;
            queue[tail][1] = ny;
            tail++;
        }
    }

    // The reachable square nearest the goal, then the closest to walk to.
    int best = -1, bestGap = abs(goalX - 5) + abs(goalY - 5), bestLength = 0;
    for (int y = 0; y < VIEW; y++)
    {
        for (int x = 0; x < VIEW; x++)
        {
            if (distance[y][x] <= 0)
                continue;
            int gap = abs(goalX - x) + abs(goalY - y);
            if (gap < bestGap || (gap == bestGap && best >= 0 && distance[y][x] < bestLength))
            {
                best = first[y][x];
                bestGap = gap;
                bestLength = distance[y][x];
            }
        }
    }
    return best;
}

// What a click beside the party does: the command letter (0 for a plain
// step), or -1 for nothing.
static int ActionFor(int x, int y, bool* thenBoard)
{
    int actor = ActorAt(x, y);
    int tile = GetMapViewport(x, y);

    *thenBoard = false;
    if (actor >= 0)
    {
        if (actor == TILE_ACTOR_CHEST)
            return SDLK_O;
        if ((actor >= TILE_ACTOR_GOLD && actor <= TILE_ACTOR_FOOD) || actor == TILE_ACTOR_MOONSTONE ||
            (actor >= TILE_ACTOR_SHARD && actor <= TILE_ACTOR_AMULET_LB))
            return SDLK_G;
        if (actor >= TILE_ACTOR_WIZARD && actor < 0x80)
            return SDLK_T;
        if (actor >= 0x80 && (actor & 0xfc) != TILE_ACTOR_E8 && (actor & 0xfc) != TILE_ACTOR_WHIRLPOOL)
            return SDLK_A;
        // A horse, ship, skiff or carpet: step on and board it.
        if (D_587c_partyTile == TILE_ACTOR_AVATAR &&
            ((actor & 0xfc) == TILE_ACTOR_HORSE || (actor >= TILE_ACTOR_FRIGATE_20 && actor <= TILE_ACTOR_SKIFF_29 + 2) ||
             actor == TILE_ACTOR_CARPET))
        {
            *thenBoard = true;
            return 0;
        }
        return -1;
    }
    if (ClosedDoor(tile))
        return SDLK_O;
    return Walkable(x, y) ? 0 : -1;
}

static void Queue(SDL_Keycode command, int direction, bool thenBoard)
{
    SDL_Keycode keys[3];
    int count = 0;

    if (command != 0)
        keys[count++] = command;
    keys[count++] = s_arrows[direction];
    if (thenBoard)
        keys[count++] = SDLK_B;
    KEY_SDL_QueueKeys(keys, count);
}

// The square under a window point, in view squares (outside 0-10 in the
// wide view's extra columns); false if it isn't on the map.
static bool SquareAt(float windowX, float windowY, int* x, int* y)
{
    int renderX, renderY;
    int extra = g_presentLayout == PRESENT_LAYOUT_WIDE_MAP ? WIDE_EXTRA_COLUMNS : 0;

    if (!GRAP_SDL_WindowToRender(windowX, windowY, &renderX, &renderY))
        return false;
    if (g_presentLayout == PRESENT_LAYOUT_CENTERED)
        renderX -= WIDE_EXTRA_WIDTH;
    if (renderX < MAP_RENDER_LEFT || renderY < MAP_RENDER_TOP || renderY >= MAP_RENDER_BOTTOM ||
        renderX >= MAP_RENDER_RIGHT + 2 * extra * tilePixelSize)
        return false;
    *x = (renderX - MAP_RENDER_LEFT) / tilePixelSize - extra;
    *y = (renderY - MAP_RENDER_TOP) / tilePixelSize;
    return true;
}

bool POINT_MouseDown(float windowX, float windowY, int button)
{
    int x, y;

    if (!Here() || !SquareAt(windowX, windowY, &x, &y))
        return false;

    if (button == SDL_BUTTON_RIGHT)
    {
        bool was = s_walking;
        POINT_Cancel();
        return was;
    }
    if (button != SDL_BUTTON_LEFT || !s_atPrompt)
        return false;

    POINT_Cancel();
    if (x == 5 && y == 5)
        return true;

    // Beside the party: act on the square.
    if (abs(x - 5) + abs(y - 5) == 1)
    {
        bool thenBoard;
        int command = ActionFor(x, y, &thenBoard);
        int direction = x > 5 ? 1 : x < 5 ? 3 : y > 5 ? 2 : 0;
        if (command >= 0)
            Queue(command, direction, thenBoard);
        return true;
    }

    // Farther: walk there.
    s_walking = true;
    s_walkMap = D_5893_map_id;
    s_walkLevel = D_5895_map_level;
    s_targetX = (D_5896_map_x + x - 5) & (D_5893_map_id == 0 ? 0xff : 0xffff);
    s_targetY = (D_5897_map_y + y - 5) & (D_5893_map_id == 0 ? 0xff : 0xffff);
    s_stepPending = false;
    s_blocked = 0;
    s_lastStep = 0;
    GRAP_BUF_MarkDirty();
    return true;
}

void POINT_Tick(void)
{
    if (!s_walking)
        return;
    if (!Here() || D_5893_map_id != s_walkMap || D_5895_map_level != s_walkLevel)
    {
        POINT_Cancel();
        return;
    }
    if (!s_atPrompt || SDL_GetTicks() - s_lastStep < STEP_MS)
        return;

    // The last step didn't move the party: blocked (or a ship turning).
    if (s_stepPending && D_5896_map_x == s_stepFromX && D_5897_map_y == s_stepFromY && ++s_blocked >= 2)
    {
        POINT_Cancel();
        return;
    }
    s_stepPending = false;

    int goalX = Step(D_5896_map_x, s_targetX) + 5;
    int goalY = Step(D_5897_map_y, s_targetY) + 5;
    if (goalX == 5 && goalY == 5)
    {
        POINT_Cancel();
        return;
    }

    int direction = FirstStep(goalX, goalY);
    if (direction < 0)
    {
        POINT_Cancel();
        return;
    }

    // A closed door in the way is opened first; the step comes next turn.
    int nx = 5 + s_dx[direction], ny = 5 + s_dy[direction];
    if (ClosedDoor(GetMapViewport(nx, ny)))
    {
        Queue(SDLK_O, direction, false);
    }
    else
    {
        Queue(0, direction, false);
        s_stepPending = true;
        s_stepFromX = D_5896_map_x;
        s_stepFromY = D_5897_map_y;
    }
    s_lastStep = SDL_GetTicks();
}

void POINT_Draw(SDL_Renderer* renderer)
{
    if (!s_walking || !Here())
        return;

    // The target square, in render pixels, then window pixels.
    int extra = g_presentLayout == PRESENT_LAYOUT_WIDE_MAP ? WIDE_EXTRA_COLUMNS : 0;
    int shift = g_presentLayout == PRESENT_LAYOUT_CENTERED ? WIDE_EXTRA_WIDTH : 0;
    int x = Step(D_5896_map_x, s_targetX) + 5 + extra;
    int y = Step(D_5897_map_y, s_targetY) + 5;
    if (x < 0 || x >= VIEW + 2 * extra || y < 0 || y >= VIEW)
        return;

    float scaleX = g_presentRect.w / g_presentWidth;
    float scaleY = g_presentRect.h / hiresHeight;
    float left = g_presentRect.x + (MAP_RENDER_LEFT + shift + x * tilePixelSize) * scaleX;
    float top = g_presentRect.y + (MAP_RENDER_TOP + y * tilePixelSize) * scaleY;
    float w = tilePixelSize * scaleX, h = tilePixelSize * scaleY;

    // Corner brackets, pulsing gently.
    float pulse = 0.5f + 0.5f * SDL_sinf(SDL_GetTicks() / 160.0f);
    float inset = (2.0f + 2.0f * pulse) * scaleX;
    float arm = w * 0.3f, thick = SDL_max(1.0f, 2.0f * scaleX);
    SDL_SetRenderDrawColor(renderer, 255, 230, 120, 255);
    SDL_FRect bars[8] = {
        {left + inset, top + inset, arm, thick},          {left + inset, top + inset, thick, arm},
        {left + w - inset - arm, top + inset, arm, thick}, {left + w - inset - thick, top + inset, thick, arm},
        {left + inset, top + h - inset - thick, arm, thick}, {left + inset, top + h - inset - arm, thick, arm},
        {left + w - inset - arm, top + h - inset - thick, arm, thick},
        {left + w - inset - thick, top + h - inset - arm, thick, arm},
    };
    SDL_RenderFillRects(renderer, bars, 8);
    GRAP_BUF_MarkDirty(); // keep pulsing
}

#endif
