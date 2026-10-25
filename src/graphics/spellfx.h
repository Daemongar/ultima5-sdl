#ifndef _GRAPHICS_SPELLFX_H
#define _GRAPHICS_SPELLFX_H

#if defined(ENABLE_SPELL_FX)

#include "common/common.h"

// Spell effects drawn over the map in full color: sparks around the caster,
// a burst in place of the original's inverted map, glowing bolts and storms.
// Positions are the game's 320x200 coordinates; the effects run on their own
// clock, so the game needn't wait for them (only bolts do).

// A spell (0-47, as cast.c numbers them) is cast: sparks around the caster.
void SPELLFX_Cast(int spell);

// The original's flash (CAST2_0000_CastEffect): a burst and a wash of light
// in the last spell's color. False if spell effects are off, so the caller
// flashes the map as the original did.
bool SPELLFX_Flash(int kind);

// A bolt (D_589d: 0x30 magic missile, 0x31 fireball, 0x32 death bolt,
// 0x33-0x36 fields) flies between combat squares, and the call returns when
// it lands.
void SPELLFX_Bolt(int fromX, int fromY, int toX, int toY, int bolt);

// A missile the game animates (COMSUBS_12de) passes a point, and lands:
// glows for spells' bolts (D_589d) and monsters' magic (styles 3-6).
void SPELLFX_MissilePoint(int x, int y, int style, int bolt);
void SPELLFX_MissileLanded(int x, int y, int style, int bolt);

// A storm (CAST_1f60: 1 sleep, 2 poison, 3 fire, 4 energy) starts, and one
// of its rays reaches a point.
void SPELLFX_BeginStorm(int storm);
void SPELLFX_StormPoint(int x, int y);

// Draws the effects into a composed frame (ARGB pixels, pitch in pixels).
// Returns true while any is showing, so the frame should be presented again.
bool SPELLFX_Apply(u32* frame, int pitch);

#endif

#endif
