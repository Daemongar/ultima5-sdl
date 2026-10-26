#include "common/common.h"
#include "common/options.h"
#include "funcs.h"
#include "tiles.h"
#include "vars.h"

#include "ambience.h"
#include "graphics/weather.h"

#if defined(ENABLE_AMBIENCE)

#include <math.h>

#include "audio/audio.h"

// The background of a place, under the game's own sound effects:
//
//   outdoors by the sea      surf, louder the more open water is near
//   outdoors or in a town    birdsong by day, crickets at night
//   by a fireplace in a town the crackle of the hearth
//   underworld, dungeons     dripping water
//   in the rain              rain, in place of the sea and the birds
//
// Battles have none. The audio driver fades it out when the calls stop (in a
// conversation, on a full-screen page) and cross-fades between kinds.

// The squares the game's own ambient sounds listen to (ULTIMA_4102): within
// this squared distance of the party.
#define HEARING 0x33
#define DAY_START_HOUR 5
#define NIGHT_START_HOUR 20
#define SURF_MIN_WATER 6 // open water squares (deep counting double) for surf

void AMBIENCE_Update(void)
{
    int kind = AMBIENCE_NONE;
    int level = 100;
    int map = D_5893_map_id;

    if (!g_options.ambientSounds || map >= 0x80 || map == 0x40 || map == 0x41)
    {
        AUDIO_SetAmbience(AMBIENCE_NONE, 0);
        return;
    }

    if ((map > 0x20 && map < 0x29) || (map == 0 && D_5895_map_level > 0x7f))
    {
        AUDIO_SetAmbience(AMBIENCE_DRIPS, 100);
        return;
    }

    // Open water and the nearest fireplace within hearing.
    int water = 0;
    int nearestFire = HEARING;
    for (int dy = -5; dy <= 5; dy++)
    {
        for (int dx = -5; dx <= 5; dx++)
        {
            int distance = dx * dx + dy * dy;
            if (distance >= HEARING)
                continue;

            int tile = *ULTIMA_4402_GetTileAddr(D_5896_map_x + dx, D_5897_map_y + dy);
            if (tile == TILE_MAP_WATER_1)
                water += 2;
            else if (tile == TILE_MAP_WATER_2)
                water++;
            else if (tile == TILE_MAP_FIREPLACE && distance < nearestFire)
                nearestFire = distance;
        }
    }

    bool day = D_587f >= DAY_START_HOUR && D_587f < NIGHT_START_HOUR;
#if defined(ENABLE_WEATHER)
    float rain = WEATHER_RainLevel();
#else
    float rain = 0.0f;
#endif
    if (map != 0 && nearestFire < HEARING)
    {
        kind = AMBIENCE_HEARTH;
        level = 100 - (int)(sqrt((double)nearestFire) * 100 / 7.15);
    }
    else if (rain > 0.1f)
    {
        kind = AMBIENCE_RAIN;
        // Quieter indoors (WEATHER_RainLevel turns down the rain on a roof).
        level = (int)(110 * rain) * (map == 0 ? 100 : 85) / 100;
    }
    else if (map == 0 && water >= SURF_MIN_WATER)
    {
        kind = AMBIENCE_SURF;
        level = water * 3 > 100 ? 100 : water * 3;
    }
    else
    {
        kind = day ? AMBIENCE_BIRDS : AMBIENCE_CRICKETS;
        // Quieter among a town's walls than in the open.
        level = map == 0 ? 100 : 60;
    }

    AUDIO_SetAmbience(kind, level < 20 ? 20 : level);
}

#endif
