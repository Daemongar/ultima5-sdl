#include "common/common.h"
#include "common/config.h"
#include "common/options.h"
#include "graphics/grap_buf.h"
#include "tiles.h"
#include "vars.h"

#include "walk_anim.h"

#if defined(ENABLE_WALK_ANIM)

#include <SDL3/SDL.h>
#include <stdlib.h>

// The Avatar walks: the original has one picture of him on foot, facing the
// viewer, so the full-color tile sheet carries walking frames in its 17th row
// (tools/make_walk_frames.py): going up or down he lifts one foot and then
// the other; going left or right he leans that way and alternates a stride
// and a passing step. Each step on the map shows the next frame for a moment
// before he stands again; holding a direction keeps him stepping. Outdoors
// and in towns, on foot, when the sheet has the frames.

#define WALK_FRAME_FIRST 0x200 // after the 512 tiles of the original
#define WALK_FRAME_MS 180 // walk_frame_ms in ultima5.ini

// The frames, from WALK_FRAME_FIRST, for each way he goes: two each, shown
// on alternate steps.
enum
{
    WALK_UP_DOWN = 0, // left foot, right foot
    WALK_RIGHT = 2,   // stride, passing
    WALK_LEFT = 4,    // stride, passing
};

typedef struct Position
{
    int id, level, x, y;
} Position;

static Position s_last = {-1, 0, 0, 0};
static Uint64 s_stepTime;
static int s_foot;
static int s_way = WALK_UP_DOWN;
static int s_frameMs = -1;

// Squares from one coordinate to another; the overworld wraps at 256.
static int Step(int from, int to, bool wraps)
{
    int step = to - from;
    if (wraps)
    {
        step &= 0xff;
        if (step >= 0x80)
            step -= 0x100;
    }
    return step;
}

int WALK_AvatarTile(int actorTile)
{
    Position now = {D_5893_map_id, D_5895_map_level, D_5896_map_x, D_5897_map_y};

    if (!g_options.walkAnimation || actorTile != 0x100 + TILE_ACTOR_AVATAR || now.id > 0x20)
    {
        s_last = now;
        return actorTile;
    }

    // A step: one square from where he was last drawn, on the same map.
    int dx = Step(s_last.x, now.x, now.id == 0);
    int dy = Step(s_last.y, now.y, now.id == 0);
    if (now.id == s_last.id && now.level == s_last.level && abs(dx) + abs(dy) == 1)
    {
        int way = dx > 0 ? WALK_RIGHT : dx < 0 ? WALK_LEFT : WALK_UP_DOWN;
        s_stepTime = SDL_GetTicks();
        // A turn starts with the first frame (the stride, going sideways);
        // carrying on, the frames alternate.
        s_foot = way == s_way ? s_foot ^ 1 : 0;
        s_way = way;
    }
    s_last = now;

    if (s_frameMs < 0)
        s_frameMs = CONFIG_GetInt("display", "walk_frame_ms", WALK_FRAME_MS);

    int frame = WALK_FRAME_FIRST + s_way + s_foot;
    if (SDL_GetTicks() - s_stepTime < (Uint64)s_frameMs && GRAP_BUF_HasColorTile(frame))
        return frame;
    return actorTile;
}

#endif
