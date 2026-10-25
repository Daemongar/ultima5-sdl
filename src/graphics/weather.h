#ifndef _GRAPHICS_WEATHER_H
#define _GRAPHICS_WEATHER_H

#if defined(ENABLE_WEATHER)

#include "common/common.h"

// How hard it is raining where the party is, 0-1: 0 indoors-like places
// (dungeons, the underworld, battles), in clear weather or with the weather
// turned off. For the ambience.
float WEATHER_RainLevel(void);

// Draws the weather over the map in a composed frame (ARGB pixels, pitch in
// pixels): clouds, rain, lightning and fog. slideX/slideY is how far smooth
// scrolling has the map shifted, in frame pixels. Returns true while there
// is weather to animate, so the frame should be presented again soon.
bool WEATHER_Apply(u32* frame, int pitch, int slideX, int slideY);

#endif

#endif
