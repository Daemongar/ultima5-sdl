#ifndef _COMMON_PLATFORM_H
#define _COMMON_PLATFORM_H

//
// Compiler
//
#if defined(_MSC_VER)
#define COMPILER_MSVC 1
#elif defined(__GNUC__)
#define COMPILER_GCC 1
#else
#define COMPILER_UNKNOWN 1
#endif

//
// OS / Environment
//
#if defined(_WIN32) || defined(_WIN64)
// Windows
#define OS_WINDOWS 1
#elif defined(__DJGPP__)
// Protected mode DOS
#define OS_DOS32 1
#elif defined(MSDOS) && defined(M_I86) // MSC 5.x
// Real mode DOS
#define OS_DOS16 1
#elif defined(__linux__)
// Linux
#define OS_LINUX 1
#elif defined(__APPLE__) && defined(__MACH__)
// Mac
#define OS_MACOS 1
#else
#define OS_UNKNOWN 1
#endif

//
// Target profiles
//
#if defined(OS_WINDOWS) || defined(OS_MACOS) || defined(OS_LINUX)
#define TARGET_SDL 1
#elif defined(OS_DOS32)
#define TARGET_DOS32 1
#elif defined(OS_DOS16)
#define TARGET_DOS16 1
#endif

// TODO: move out of this file
//
// Build Configurations
//
// Matching build configuration (IBM 1.16 720 KB release)
//#define MATCHING_BUILD 1

// Approximates the behavior of the binary-patched GOG release
//#define GOG_BUILD 1

// Enable BGM
//#define ENABLE_BGM 1

// Enable speed fixes for modern build
//#define ENABLE_SPEED_FIX 1

// Enable bug fixes
//#define ENABLE_BUG_FIX 1

// Enable mouse-aimed combat targeting
//#define ENABLE_MOUSE_AIM 1

// Enable dialog improvements (case-insensitive keyword matching, auto-paging,
// larger input buffer, repeat detection, keyword hints)
//#define ENABLE_DIALOG_IMPROVEMENTS 1

// Enable HP-aware monster target selection (still nearest-enemy first, but
// prefers a wounded target over a healthier one at similar range)
//#define ENABLE_SMARTER_AI 1

// Award kill experience to the whole living party instead of only whoever
// landed the killing blow
//#define ENABLE_PARTY_EXP 1

// Award bonus kill experience for fighting multiple enemies at once (see
// COMBAT_AwardExp in combat.c)
//#define ENABLE_GROUP_EXP_BONUS 1

// Wider ambient light radius outdoors at night without a torch (~3 tiles
// instead of ~1). Dungeons and other always-dark maps are unaffected -
// torches remain necessary underground.
//#define ENABLE_BRIGHTER_NIGHT 1

// Let Esc also trigger the "Exit to DOS?" prompt (previously only Ctrl+E)
// at the idle overworld/town/dungeon command prompt, since Ctrl+E is an
// undiscoverable legacy DOS convention.
//#define ENABLE_QUICK_EXIT 1

// Charge extra travel time for climbing straight from low ground into much
// higher terrain (see MAINOUT_03e0 in mainout.c), on top of the destination
// tile's own difficulty cost. Descending is free.
//#define ENABLE_TERRAIN_ELEVATION 1

// Replace the raw 16-color EGA palette with a softer, less oversaturated
// set of the same 16 hues (see s_egaPalette in graphics/grap_sdl.c). Purely
// cosmetic - tile art is unchanged, just recolored at display time.
//#define ENABLE_ENHANCED_PALETTE 1

// Weight the random STR/DEX/INTEL level-up bonus (OUTSUBS_0658) by class
// instead of a flat uniform 1-in-3 roll - see s_classStatWeights.
//#define ENABLE_CLASS_STAT_GROWTH 1

// Round a Bard's mana up instead of down when derived from an odd INTEL
// (intel/2 -> (intel+1)/2), everywhere mag is set for CLASS_BARD.
//#define ENABLE_BARD_MANA_ROUNDUP 1

// Let Vas Mani (Great Heal) work during a fight. The original refused it
// until "VICTORY!" even though the spell table lists it as usable in combat,
// so casting it mid-battle just wasted the mixture and MP.
//#define ENABLE_VAS_MANI_IN_COMBAT 1

// Check MP and caster level before casting. The original used up the mixed
// spell before the MP check and spent the MP before the level check, so a
// cast that could never succeed still cost the reagents (and often the MP).
//#define ENABLE_NO_WASTED_CASTS 1

// Poisoned guests wake healed but still poisoned after resting at an inn.
// The original restored them to full health and then killed them overnight.
//#define ENABLE_NO_INN_POISON_DEATH 1

// Grant one random stat point per level gained at a level-up apparition.
// The original gave one point per visit, so skipping levels between visits
// (e.g. 2 -> 4) lost the points for the skipped levels.
//#define ENABLE_STAT_PER_LEVEL 1

// Restore the 1-in-16 chance that a Ring of Invisibility or Regeneration
// crumbles when equipped ("Ring vanishes!"). A typo in the original compared
// the quantity owned with the ring's item ID, so it never happened.
//#define ENABLE_VANISHING_RINGS 1

// Give the Ultima IV classes (Druid, Paladin, Ranger, Tinker) a mana rule; see
// class_mana.h. An imported character of these classes never regained mana.
//#define ENABLE_U4_CLASS_MANA 1

// Vary the overworld footstep by terrain (grass, brush, forest, swamp, desert,
// hills, mountains, roads). The original played the same click everywhere.
//#define ENABLE_TERRAIN_FOOTSTEPS 1

// Let a skiff leave a river tile through its open ends, back the way it came.
// On an island's south shore (x 0x0f, y 0xaa) a river bend opens only onto
// shoreline that can't be entered southward or westward, so a skiff rowed in
// was stuck.
//#define ENABLE_SKIFF_RIVER_EXIT 1

// Keep animated tiles (water, lava, flags, fireplaces, shorelines, clock,
// ladders) moving when drawn from the full-color TILES.BMP atlas.
//#define ENABLE_ANIMATED_COLOR_TILES 1

// F5 quicksave, F9 quickload, and save slots: F6 saves to and F7 loads from
// one of nine slots (see save_slots.c). The original kept a single save.
//#define ENABLE_SAVE_SLOTS 1

// A journal (F4) of everything people have said in answer to the player's
// questions, by person and keyword (see journal.c).
//#define ENABLE_JOURNAL 1

// A map (F10) of the dungeon cells the party has seen (see automap.c).
//#define ENABLE_AUTOMAP 1

// When mixing, tick the chosen spell's reagents from the spell book (if all
// are owned) and say how many can be made. In Xen Mani and Rel Xen Bet,
// learned in the game, stay manual.
//#define ENABLE_MIX_RECIPES 1

// --script FILE plays keys from a file, with a fixed random seed, for the
// tests in tools/playtest.py (see playtest.c).
//#define ENABLE_PLAYTEST 1

// Name the creature or terrain under the mouse pointer on the map, as Look
// would (see hover.c).
//#define ENABLE_HOVER_NAMES 1

// Raise the lowest roll of a party member's weapon with Strength (close
// weapons) or Dexterity (ranged); the maximum is unchanged. The original
// rolled 1 to the maximum whatever the wielder's stats.
//#define ENABLE_STAT_DAMAGE 1

// Show karma on the Z-stats equipment page as a word, in the bands of 20
// that pick Lord British's messages. The original showed it only with Ctrl+K.
//#define ENABLE_KARMA_HINT 1

#if defined(TARGET_DOS16)
#define MATCHING_BUILD 1
#else
#define GOG_BUILD 1
#endif

#if !defined(TARGET_DOS16)
#define ENABLE_BGM 1
#define ENABLE_SPEED_FIX 1
#define ENABLE_BUG_FIX 1
#define ENABLE_MOUSE_AIM 1
#define ENABLE_VARIED_COMBAT 1 // off by default in the Options screen
#define ENABLE_DIALOG_IMPROVEMENTS 1
#define ENABLE_SMARTER_AI 1
#define ENABLE_PARTY_EXP 1
#define ENABLE_GROUP_EXP_BONUS 1
#define ENABLE_BRIGHTER_NIGHT 1
#define ENABLE_QUICK_EXIT 1
#define ENABLE_TERRAIN_ELEVATION 1 // off by default in the Options screen
#define ENABLE_ENHANCED_PALETTE 1
#define ENABLE_CLASS_STAT_GROWTH 1
#define ENABLE_BARD_MANA_ROUNDUP 1
#define ENABLE_VAS_MANI_IN_COMBAT 1
#define ENABLE_NO_WASTED_CASTS 1
#define ENABLE_NO_INN_POISON_DEATH 1
#define ENABLE_STAT_PER_LEVEL 1
#define ENABLE_VANISHING_RINGS 1
#define ENABLE_U4_CLASS_MANA 1
#define ENABLE_TERRAIN_FOOTSTEPS 1
#define ENABLE_ANIMATED_COLOR_TILES 1
#define ENABLE_SKIFF_RIVER_EXIT 1
#define ENABLE_SAVE_SLOTS 1
#define ENABLE_JOURNAL 1
#define ENABLE_AUTOMAP 1
#define ENABLE_MIX_RECIPES 1
#define ENABLE_PLAYTEST 1
#define ENABLE_HOVER_NAMES 1
#define ENABLE_STAT_DAMAGE 1
#define ENABLE_KARMA_HINT 1
//#define ENABLE_TRANSLATION 1
#endif

#if defined(TARGET_DOS16)
#define CDECL cdecl
#define FAR far
#else
#define CDECL
#define FAR
#endif

#if defined(COMPILER_MSVC)
#define _CRT_SECURE_NO_WARNINGS
#endif

#endif
