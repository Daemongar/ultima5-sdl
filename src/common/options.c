#include "common.h"
#include "config.h"
#include "options.h"

#include <string.h>

Options g_options;

typedef struct OptionFlag
{
    char* section;
    char* key;
    bool* value;
    bool defaultValue;
} OptionFlag;

static OptionFlag s_flags[] =
{
    {"display", "keep_aspect", &g_options.keepAspect, false},
    {"display", "color_tiles", &g_options.colorTiles, true},
    {"display", "enhanced_palette", &g_options.enhancedPalette, true},
    {"display", "wide_view", &g_options.wideView, false},
#if defined(ENABLE_HOVER_NAMES)
    {"display", "hover_names", &g_options.hoverNames, true},
#endif
#if defined(ENABLE_SMOOTH_SCROLL)
    {"display", "smooth_scroll", &g_options.smoothScroll, false},
#endif
#if defined(ENABLE_WALK_ANIM)
    {"display", "walk_animation", &g_options.walkAnimation, true},
#endif
#if defined(ENABLE_LIGHTING)
    {"display", "lighting", &g_options.lighting, true},
#endif
#if defined(ENABLE_SPELL_FX)
    {"display", "spell_effects", &g_options.spellEffects, true},
#endif
#if defined(ENABLE_WEATHER)
    {"display", "weather", &g_options.weather, true},
#endif
#if defined(ENABLE_GLIDE)
    {"display", "glide", &g_options.glide, true},
#endif
    {"sound", "music", &g_options.music, true},
    {"sound", "sound_effects", &g_options.soundEffects, true},
    {"sound", "enhanced_sound_effects", &g_options.enhancedSounds, true},
    {"sound", "terrain_footsteps", &g_options.terrainFootsteps, true},
#if defined(ENABLE_AMBIENCE)
    {"sound", "ambient_sounds", &g_options.ambientSounds, true},
#endif
    {"gameplay", "mouse_aim", &g_options.mouseAim, true},
    {"gameplay", "dialog_improvements", &g_options.dialogImprovements, true},
#if defined(ENABLE_TALK_TOPICS)
    {"gameplay", "talk_topics", &g_options.talkTopics, true},
#endif
    {"gameplay", "smarter_ai", &g_options.smarterAi, true},
    {"gameplay", "party_exp", &g_options.partyExp, true},
    {"gameplay", "group_exp_bonus", &g_options.groupExpBonus, true},
    {"gameplay", "brighter_night", &g_options.brighterNight, true},
    {"gameplay", "quick_exit", &g_options.quickExit, true},
    {"gameplay", "terrain_elevation", &g_options.terrainElevation, false},
    {"gameplay", "class_stat_growth", &g_options.classStatGrowth, true},
    {"gameplay", "bard_mana_roundup", &g_options.bardManaRoundup, true},
    {"gameplay", "varied_combat", &g_options.variedCombat, false},
    {"gameplay", "vas_mani_in_combat", &g_options.vasManiInCombat, true},
    {"gameplay", "no_wasted_casts", &g_options.noWastedCasts, true},
    {"gameplay", "no_inn_poison_death", &g_options.noInnPoisonDeath, true},
    {"gameplay", "stat_per_level", &g_options.statPerLevel, true},
    {"gameplay", "vanishing_rings", &g_options.vanishingRings, true},
    {"gameplay", "u4_class_mana", &g_options.u4ClassMana, true},
#if defined(ENABLE_MIX_RECIPES)
    {"gameplay", "mix_recipes", &g_options.mixRecipes, true},
#endif
#if defined(ENABLE_STAT_DAMAGE)
    {"gameplay", "stat_damage", &g_options.statDamage, true},
#endif
#if defined(ENABLE_KARMA_HINT)
    {"display", "karma_hint", &g_options.karmaHint, true},
#endif
#if defined(ENABLE_CLASS_PERKS)
    {"gameplay", "class_perks", &g_options.classPerks, true},
#endif
};

static const char* s_windowModeNames[] = {"fullscreen", "bordered", "windowed"};

static int ClampVolume(int volume)
{
    return volume < 0 ? 0 : (volume > 10 ? 10 : volume);
}

void OPTIONS_Load(void)
{
    char mode[32];

    for (int i = 0; i < ARRAYSIZE(s_flags); i++)
    {
        *s_flags[i].value = CONFIG_GetInt(s_flags[i].section, s_flags[i].key, s_flags[i].defaultValue) != 0;
    }

    // [window] mode; without it the older fullscreen=1/0 picks fullscreen or bordered.
    CONFIG_GetString("window", "mode", "", mode, sizeof(mode));
    g_options.windowMode = CONFIG_GetInt("window", "fullscreen", 1) != 0 ? OPTION_WINDOW_FULLSCREEN : OPTION_WINDOW_BORDERED;
    for (int i = 0; i < ARRAYSIZE(s_windowModeNames); i++)
    {
        char lower[32];
        int n;
        for (n = 0; mode[n] != '\0' && n < (int)sizeof(lower) - 1; n++)
            lower[n] = (char)((mode[n] >= 'A' && mode[n] <= 'Z') ? mode[n] + 32 : mode[n]);
        lower[n] = '\0';
        if (strcmp(lower, s_windowModeNames[i]) == 0)
            g_options.windowMode = i;
    }

    g_options.musicVolume = ClampVolume(CONFIG_GetInt("sound", "music_volume", 8));
    g_options.soundVolume = ClampVolume(CONFIG_GetInt("sound", "sound_volume", 8));
}

void OPTIONS_Save(void)
{
    CONFIG_SetString("window", "mode", (char*)s_windowModeNames[g_options.windowMode]);
    CONFIG_SetInt("sound", "music_volume", g_options.musicVolume);
    CONFIG_SetInt("sound", "sound_volume", g_options.soundVolume);

    for (int i = 0; i < ARRAYSIZE(s_flags); i++)
    {
        CONFIG_SetInt(s_flags[i].section, s_flags[i].key, *s_flags[i].value ? 1 : 0);
    }
}
