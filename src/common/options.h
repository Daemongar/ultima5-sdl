#ifndef _COMMON_OPTIONS_H
#define _COMMON_OPTIONS_H

#include "common.h"

// Player settings, changeable from the title menu's Options screen and stored
// in ultima5.ini. Each gameplay feature also needs its ENABLE_* switch in
// platform.h to be compiled in; the setting only chooses whether it is used.

enum
{
    OPTION_WINDOW_FULLSCREEN, // borderless, covering the whole screen
    OPTION_WINDOW_BORDERED,   // normal window with title bar, maximized
    OPTION_WINDOW_WINDOWED,   // normal resizable window
};

typedef struct Options
{
    // Display
    int windowMode;
    bool keepAspect; // keep the picture's shape (4:3, or the wide view's) with black bars instead of stretching
    bool colorTiles; // use TILES.BMP when present
    bool enhancedPalette;
    bool wideView; // 17-column map for 16:9 screens instead of the original 11
    bool hoverNames; // name what's under the mouse on the map
    bool smoothScroll; // slide the map a square at a time instead of jumping
    bool walkAnimation; // the Avatar steps as he walks
    bool lighting; // dusk, night and the glow of fires and lamps on the map
    bool spellEffects; // spells sparkle, burst and glow instead of inverting the map

    // Sound
    bool music;
    int musicVolume; // 0-10
    bool soundEffects;
    int soundVolume; // 0-10
    bool enhancedSounds; // smooth tones and reverb instead of the PC speaker's beeps
    bool terrainFootsteps;
    bool ambientSounds;

    // Gameplay
    bool mouseAim;
    bool dialogImprovements;
    bool talkTopics; // list the topics found so far while talking
    bool smarterAi;
    bool partyExp;
    bool groupExpBonus;
    bool brighterNight;
    bool quickExit;
    bool terrainElevation;
    bool classStatGrowth;
    bool bardManaRoundup;
    bool variedCombat;
    bool vasManiInCombat;
    bool noWastedCasts;
    bool noInnPoisonDeath;
    bool statPerLevel;
    bool vanishingRings;
    bool u4ClassMana;
    bool mixRecipes;
    bool statDamage;
    bool karmaHint;
    bool classPerks;
} Options;

extern Options g_options;

void OPTIONS_Load(void);
void OPTIONS_Save(void);

#endif
