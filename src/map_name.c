#include "common/common.h"
#include "vars.h"

#include "map_name.h"

// Maps with no name in the game's own table (D_1e3a), by map ID.
static char* s_unnamedMaps[] =
{
    "SUTEK'S HUT",         // 0x0e
    "SIN VRAAL'S HUT",     // 0x0f
    "GRENDEL'S HUT",       // 0x10
    "CASTLE BRITISH",      // 0x11
    "BLACKTHORN'S PALACE", // 0x12
};

char* MAP_GetName(int mapId, int mapLevel)
{
    if (mapId == 0)
        return mapLevel != 0 ? "UNDERWORLD" : "BRITANNIA";
    if (mapId >= 1 && mapId <= ARRAYSIZE(D_1e3a) && D_1e3a[mapId - 1])
        return D_1e3a[mapId - 1];
    if (mapId >= 0x0e && mapId < 0x0e + ARRAYSIZE(s_unnamedMaps))
        return s_unnamedMaps[mapId - 0x0e];
    return "";
}
