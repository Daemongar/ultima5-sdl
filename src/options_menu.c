#include "common/common.h"
#include "common/options.h"
#include "funcs.h"
#include "macros.h"
#include "vars.h"

#include "options_menu.h"
#include "automap.h"
#include "journal.h"
#include "map_name.h"
#include "save_slots.h"
#include "graphics/grap_buf.h"
#include "key/key.h"
#include "time/time.h"

#if defined(TARGET_SDL)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Options screen reached from the title menu. Drawn with the game's font and
// frame like the rest of the title screens: three pages of settings, changed
// with the arrow keys and saved to ultima5.ini on leaving.

void GRAP_SDL_ApplyDisplayOptions(void);
void AUDIO_SDL_ApplySoundOptions(void);

enum
{
    ITEM_TOGGLE,
    ITEM_VOLUME,
    ITEM_WINDOW_MODE,
};

enum
{
    PAGE_DISPLAY,
    PAGE_SOUND,
    PAGE_GAMEPLAY,
    PAGE_COMBAT,
    PAGE_COUNT,
};

typedef struct OptionItem
{
    int page;
    int kind;
    char* label;
    bool* flag;  // ITEM_TOGGLE
    int* value;  // ITEM_VOLUME, ITEM_WINDOW_MODE
} OptionItem;

// Settings shown on the screen. Others (such as bard_mana_roundup) can only be
// changed in ultima5.ini.
static OptionItem s_items[] =
{
    {PAGE_DISPLAY, ITEM_WINDOW_MODE, "Window mode", NULL, &g_options.windowMode},
    {PAGE_DISPLAY, ITEM_TOGGLE, "Keep aspect ratio", &g_options.keepAspect, NULL},
    {PAGE_DISPLAY, ITEM_TOGGLE, "Full-color tiles", &g_options.colorTiles, NULL},
    {PAGE_DISPLAY, ITEM_TOGGLE, "Softer palette", &g_options.enhancedPalette, NULL},
    {PAGE_DISPLAY, ITEM_TOGGLE, "Wide view (16:9)", &g_options.wideView, NULL},
#if defined(ENABLE_HOVER_NAMES)
    {PAGE_DISPLAY, ITEM_TOGGLE, "Names under mouse", &g_options.hoverNames, NULL},
#endif
#if defined(ENABLE_KARMA_HINT)
    {PAGE_DISPLAY, ITEM_TOGGLE, "Karma on Z-stats", &g_options.karmaHint, NULL},
#endif
#if defined(ENABLE_SMOOTH_SCROLL)
    {PAGE_DISPLAY, ITEM_TOGGLE, "Smooth scrolling", &g_options.smoothScroll, NULL},
#endif
#if defined(ENABLE_WALK_ANIM)
    {PAGE_DISPLAY, ITEM_TOGGLE, "Walking Avatar", &g_options.walkAnimation, NULL},
#endif

    {PAGE_SOUND, ITEM_TOGGLE, "Music", &g_options.music, NULL},
    {PAGE_SOUND, ITEM_VOLUME, "Music volume", NULL, &g_options.musicVolume},
    {PAGE_SOUND, ITEM_TOGGLE, "Sound effects", &g_options.soundEffects, NULL},
    {PAGE_SOUND, ITEM_VOLUME, "Effects volume", NULL, &g_options.soundVolume},
    {PAGE_SOUND, ITEM_TOGGLE, "Enhanced effects", &g_options.enhancedSounds, NULL},
    {PAGE_SOUND, ITEM_TOGGLE, "Terrain footsteps", &g_options.terrainFootsteps, NULL},
#if defined(ENABLE_AMBIENCE)
    {PAGE_SOUND, ITEM_TOGGLE, "Ambient sounds", &g_options.ambientSounds, NULL},
#endif

    {PAGE_COMBAT, ITEM_TOGGLE, "Mouse aiming", &g_options.mouseAim, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Dialog improvements", &g_options.dialogImprovements, NULL},
    {PAGE_COMBAT, ITEM_TOGGLE, "Smarter monsters", &g_options.smarterAi, NULL},
    {PAGE_COMBAT, ITEM_TOGGLE, "Party shares exp.", &g_options.partyExp, NULL},
    {PAGE_COMBAT, ITEM_TOGGLE, "Group exp. bonus", &g_options.groupExpBonus, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Brighter nights", &g_options.brighterNight, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Esc asks to quit", &g_options.quickExit, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Climbing costs time", &g_options.terrainElevation, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Class stat growth", &g_options.classStatGrowth, NULL},
    {PAGE_COMBAT, ITEM_TOGGLE, "Varied battlefields", &g_options.variedCombat, NULL},
    {PAGE_COMBAT, ITEM_TOGGLE, "Vas Mani in combat", &g_options.vasManiInCombat, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "No wasted casts", &g_options.noWastedCasts, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Inn poison not fatal", &g_options.noInnPoisonDeath, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Stat point per level", &g_options.statPerLevel, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Vanishing rings", &g_options.vanishingRings, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Ultima IV class mana", &g_options.u4ClassMana, NULL},
#if defined(ENABLE_MIX_RECIPES)
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Recipe helper", &g_options.mixRecipes, NULL},
#endif
#if defined(ENABLE_STAT_DAMAGE)
    {PAGE_COMBAT, ITEM_TOGGLE, "Stat-based damage", &g_options.statDamage, NULL},
#endif
#if defined(ENABLE_CLASS_PERKS)
    {PAGE_COMBAT, ITEM_TOGGLE, "Class perks", &g_options.classPerks, NULL},
#endif
};

static char* s_pageNames[PAGE_COUNT] = {"Display", "Sound", "Gameplay", "Combat"};
static char* s_windowModeNames[] = {"Fullscreen", "Bordered", "Windowed"};

#define SCREEN_RIGHT 0x27
#define SCREEN_BOTTOM 0x18
#define FIRST_ITEM_ROW 5
#define LABEL_COLUMN 3
#define VALUE_COLUMN 26

static void PrintAt(int x, int y, char* text)
{
    ULTIMA_1bf2_SetTextPosition(x, y);
    ULTIMA_1850_PrintString(text);
}

static void PrintCentered(int y, char* text)
{
    PrintAt((SCREEN_RIGHT + 1 - (int)strlen(text)) / 2, y, text);
}

// A rectangle around the whole screen, drawn with lines like the title menu's
// box (INTRO_04e0_DrawMenuBorders). The frame character 0x7f is not used: in
// the game's font it is a thin bar, so only the sides showed.
static void DrawFrame(void)
{
    ULTIMA_0a70_GRAP_2d_SetPenColor(D_13b0_white_color);
    ULTIMA_0b10_GRAP_Line(4, 4, 315, 4);
    ULTIMA_0f90_GRAP_Pen(315, 195);
    ULTIMA_0f90_GRAP_Pen(4, 195);
    ULTIMA_0f90_GRAP_Pen(4, 4);
    ULTIMA_1cca_SetTextForegroundColor(D_13b0_white_color);
}

static void ValueText(OptionItem* item, char* out)
{
    switch (item->kind)
    {
    case ITEM_TOGGLE:
        strcpy(out, *item->flag ? "On" : "Off");
        break;

    case ITEM_VOLUME:
        // A bar of 10 cells, filled up to the volume.
        for (int i = 0; i < 10; i++)
            out[i] = i < *item->value ? '*' : '.';
        out[10] = '\0';
        break;

    case ITEM_WINDOW_MODE:
        strcpy(out, s_windowModeNames[*item->value]);
        break;
    }
}

static int PageItems(int page, OptionItem** items)
{
    int count = 0;
    for (int i = 0; i < ARRAYSIZE(s_items); i++)
    {
        if (s_items[i].page == page)
            items[count++] = &s_items[i];
    }
    return count;
}

// Most items on any page; rows below a short page's items are blanked.
#define MAX_PAGE_ITEMS 18
#define LABEL_WIDTH 20
#define VALUE_WIDTH 11

// Print text padded with spaces to width, so it overwrites what was there.
static void PrintPadded(char* text, int width)
{
    int n = (int)strlen(text);
    ULTIMA_1850_PrintString(text);
    for (; n < width; n++)
        ULTIMA_16ba_PrintChar(0x20);
}

// Frame, title and help text: drawn once when the screen opens.
static void DrawStatic(void)
{
    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    DrawFrame();
    PrintCentered(1, "Options");
    PrintCentered(SCREEN_BOTTOM - 1, "Arrows: change  Tab: page  Esc: done");
}

// The page names in a row, centered, one space between them.
static void DrawTabs(int page)
{
    int width = PAGE_COUNT - 1;
    for (int p = 0; p < PAGE_COUNT; p++)
        width += (int)strlen(s_pageNames[p]) + 2;

    int x = (SCREEN_RIGHT + 1 - width) / 2;
    for (int p = 0; p < PAGE_COUNT; p++)
    {
        ULTIMA_1bf2_SetTextPosition(x, 3);
        if (p == page)
            ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);
        ULTIMA_16ba_PrintChar(0x20);
        ULTIMA_1850_PrintString(s_pageNames[p]);
        ULTIMA_16ba_PrintChar(0x20);
        if (p == page)
            ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);
        x += (int)strlen(s_pageNames[p]) + 3;
    }
}

// One setting line: the label (inverted when selected) and its value.
static void DrawItemRow(OptionItem* item, int row, bool selected)
{
    char text[32];
    int y = FIRST_ITEM_ROW + row;

    ULTIMA_1bf2_SetTextPosition(LABEL_COLUMN - 1, y);
    if (selected)
        ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);
    ULTIMA_16ba_PrintChar(0x20);
    PrintPadded(item->label, LABEL_WIDTH);
    ULTIMA_16ba_PrintChar(0x20);
    if (selected)
        ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);

    ValueText(item, text);
    ULTIMA_1bf2_SetTextPosition(VALUE_COLUMN, y);
    PrintPadded(text, VALUE_WIDTH);
}

static void DrawItems(OptionItem** items, int count, int selected)
{
    for (int row = 0; row < MAX_PAGE_ITEMS; row++)
    {
        if (row < count)
        {
            DrawItemRow(items[row], row, row == selected);
        }
        else
        {
            ULTIMA_1bf2_SetTextPosition(LABEL_COLUMN - 1, FIRST_ITEM_ROW + row);
            PrintPadded("", VALUE_COLUMN + VALUE_WIDTH - (LABEL_COLUMN - 1));
        }
    }
}

static void ChangeItem(OptionItem* item, int direction)
{
    switch (item->kind)
    {
    case ITEM_TOGGLE:
        *item->flag = !*item->flag;
        break;

    case ITEM_VOLUME:
        *item->value += direction;
        if (*item->value < 0)
            *item->value = 0;
        if (*item->value > 10)
            *item->value = 10;
        break;

    case ITEM_WINDOW_MODE:
        *item->value = (*item->value + direction + ARRAYSIZE(s_windowModeNames)) % ARRAYSIZE(s_windowModeNames);
        break;
    }

    if (item->page == PAGE_DISPLAY)
        GRAP_SDL_ApplyDisplayOptions();
    else if (item->page == PAGE_SOUND)
        AUDIO_SDL_ApplySoundOptions();
}

static bool s_menuOpen;

extern bool g_presentFullScreenPage;

// Wait for a key without the game's own key wait (ULTIMA_266c_GetChar), which
// updates the game frame while idle and would redraw the map over the menu.
static int ReadKey(void)
{
    int key;
    while ((key = KEY_PollKey()) == 0)
    {
        TIME_SleepMs(10);
    }
    return key;
}

void OPTIONS_MENU_Run(void)
{
    OptionItem* items[ARRAYSIZE(s_items)];
    int page = PAGE_DISPLAY;
    int selected = 0;

    s_menuOpen = true;
    g_presentFullScreenPage = true; // full-screen page: centered in the wide view

    // Text must advance as it's printed; the game may have this off when F1
    // is pressed (OPTIONS_MENU_RunInGame restores it afterwards).
    D_538e = 1;

    ULTIMA_1c22_SetTextWindowSize(0, 0, 0, SCREEN_RIGHT, SCREEN_BOTTOM);
    ULTIMA_1b94_SelectTextWindow(0);
    ULTIMA_0c22_GRAP_0f_SelectPage(0);

    DrawStatic();
    int count = PageItems(page, items);
    DrawTabs(page);
    DrawItems(items, count, selected);

    for (;;)
    {
        int previous = selected;
        int key = ReadKey();

        switch (key)
        {
        case U5_KEY_UP:
            selected = (selected + count - 1) % count;
            DrawItemRow(items[previous], previous, false);
            DrawItemRow(items[selected], selected, true);
            break;

        case U5_KEY_DOWN:
            selected = (selected + 1) % count;
            DrawItemRow(items[previous], previous, false);
            DrawItemRow(items[selected], selected, true);
            break;

        case U5_KEY_LEFT:
            ChangeItem(items[selected], -1);
            DrawItemRow(items[selected], selected, true);
            break;

        case U5_KEY_RIGHT:
        case U5_KEY_ENTER:
        case U5_KEY_SPACE:
            ChangeItem(items[selected], 1);
            DrawItemRow(items[selected], selected, true);
            break;

        case '\t':
        case U5_KEY_PGDN:
        case U5_KEY_PGUP:
            page = (page + (key == U5_KEY_PGUP ? PAGE_COUNT - 1 : 1)) % PAGE_COUNT;
            selected = 0;
            count = PageItems(page, items);
            DrawTabs(page);
            DrawItems(items, count, selected);
            break;

        case U5_KEY_ESC:
            OPTIONS_Save();
            ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
            s_menuOpen = false;
            g_presentFullScreenPage = false;
            return;
        }
    }
}

// Text output state the Options screen changes; saved and restored around it.
typedef struct TextState
{
    TextWindow windows[4];
    u16 windowIdx;
    u16 charsetIdx;
    u16 advance;
    u8* charset;
    TextWindow* window;
    u16 underline;
    u16 flag53a6;
    u16 inverse;
    u8 fg;
    u8 bg;
    VideoDriverParams video;
} TextState;

// Keys listed on the shortcuts screen, in two columns.
static char* s_commandKeys[] =
{
    "A Attack",          "N New order",
    "B Board",           "O Open",
    "C Cast",            "P Push",
    "E Enter",           "Q Quit & save",
    "F Fire",            "R Ready gear",
    "G Get",             "S Search",
    "H Hole up & camp",  "T Talk",
    "I Ignite torch",    "U Use item",
    "J Jimmy lock",      "V View gem",
    "K Klimb",           "X X-it",
    "L Look",            "Y Yell",
    "M Mix reagents",    "Z Z-stats",
};

static char* s_otherKeys[] =
{
    "Arrows Move",       "Space  Pass",
    "1-6    Active plr", "0      No active",
    "Ctrl+K Karma",      "Ctrl+S Sound",
    "Ctrl+B Key buffer", "Ctrl+V Version",
    "Ctrl+E Exit game",  "F1     Options",
    "F2     This page",  "F3     Spells",
    "F4     Journal",    "F10    Map",
    "F5     Quicksave",  "F9     Quickload",
    "F6/F7  Save/load",  "F8     Reload BMP",
};

#define SHORTCUT_LEFT_COLUMN 3
#define SHORTCUT_RIGHT_COLUMN 21

static void PrintTwoColumns(char** entries, int count, int firstRow)
{
    for (int i = 0; i < count; i++)
    {
        PrintAt(i % 2 == 0 ? SHORTCUT_LEFT_COLUMN : SHORTCUT_RIGHT_COLUMN, firstRow + i / 2, entries[i]);
    }
}

static void ShowShortcuts(void)
{
    s_menuOpen = true;
    g_presentFullScreenPage = true;
    D_538e = 1;

    ULTIMA_1c22_SetTextWindowSize(0, 0, 0, SCREEN_RIGHT, SCREEN_BOTTOM);
    ULTIMA_1b94_SelectTextWindow(0);
    ULTIMA_0c22_GRAP_0f_SelectPage(0);

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    DrawFrame();
    PrintCentered(1, "Keyboard Shortcuts");
    PrintTwoColumns(s_commandKeys, ARRAYSIZE(s_commandKeys), 2);
    PrintTwoColumns(s_otherKeys, ARRAYSIZE(s_otherKeys), 14);
    PrintCentered(SCREEN_BOTTOM - 1, "Any key: done");

    ReadKey();

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    s_menuOpen = false;
    g_presentFullScreenPage = false;
}

// Full spell names; the game's own list (D_19e2) cuts them to ten letters.
static char* s_spellNames[48] =
{
    "In Lor", "Grav Por", "An Zu", "An Nox", "Mani", "An Ylem",
    "An Sanct", "An Xen Corp", "Rel Hur", "In Wis", "Kal Xen", "In Xen Mani",
    "Vas Lor", "Vas Flam", "In Flam Grav", "In Nox Grav", "In Zu Grav", "In Por",
    "An Grav", "In Sanct", "In Sanct Grav", "Uus Por", "Des Por", "Wis Quas",
    "In Bet Xen", "An Ex Por", "In Ex Por", "Vas Mani", "In Zu", "Rel Tym",
    "In Vas Por Ylem", "Quas An Wis", "In An", "Wis An Ylem", "An Xen Ex", "Rel Xen Bet",
    "Sanct Lor", "Xen Corp", "In Quas Xen", "In Quas Wis", "In Nox Hur", "In Quas Corp",
    "In Mani Corp", "Kal Xen Corp", "In Vas Grav Corp", "In Flam Hur", "Vas Rel Por", "An Tym",
};

static char* s_circleNames[8] =
{
    "First", "Second", "Third", "Fourth", "Fifth", "Sixth", "Seventh", "Eighth",
};

// Recipes the player learns by talking to people in the game, not from the
// spell book; the page shows ??? for these.
#define SPELL_IN_XEN_MANI 11
#define SPELL_REL_XEN_BET 35

#define SPELLS_PER_CIRCLE 6
#define SPELL_NAME_COLUMN 3
#define REAGENT_COLUMN 5
#define REAGENT_WIDTH (SCREEN_RIGHT - 1 - REAGENT_COLUMN)

// The spell's reagents from the mixing table (D_1cc0), wrapped onto as many
// rows as they need. Returns the next free row.
static int PrintReagents(int spell, int y)
{
    int x = REAGENT_COLUMN;
    bool first = true;

    if (spell == SPELL_IN_XEN_MANI || spell == SPELL_REL_XEN_BET)
    {
        PrintAt(x, y, "???");
        return y + 1;
    }

    for (int i = 0; i < 8; i++)
    {
        if (!(D_1cc0[spell] & (0x80 >> i)))
            continue;

        int width = (int)strlen(D_3c20[i]) + (first ? 0 : 2);
        if (!first && x + width + 1 > REAGENT_COLUMN + REAGENT_WIDTH)
        {
            ULTIMA_1850_PrintString(",");
            x = REAGENT_COLUMN;
            y++;
            first = true;
            width = (int)strlen(D_3c20[i]);
        }

        ULTIMA_1bf2_SetTextPosition(x, y);
        if (!first)
            ULTIMA_1850_PrintString(", ");
        ULTIMA_1850_PrintString(D_3c20[i]);
        x += width;
        first = false;
    }
    return y + 1;
}

static void DrawSpellPage(int circle)
{
    char title[32];
    int y = 5;

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    DrawFrame();
    PrintCentered(1, "Spells and Reagents");
    strcpy(title, s_circleNames[circle]);
    strcat(title, " Circle");
    PrintCentered(3, title);

    for (int i = 0; i < SPELLS_PER_CIRCLE; i++)
    {
        int spell = circle * SPELLS_PER_CIRCLE + i;
        PrintAt(SPELL_NAME_COLUMN, y++, s_spellNames[spell]);
        y = PrintReagents(spell, y);
    }

    PrintCentered(SCREEN_BOTTOM - 2, "Arrows: circle   Esc: done");
}

static void ShowSpells(void)
{
    int circle = 0;

    s_menuOpen = true;
    g_presentFullScreenPage = true;
    D_538e = 1;

    ULTIMA_1c22_SetTextWindowSize(0, 0, 0, SCREEN_RIGHT, SCREEN_BOTTOM);
    ULTIMA_1b94_SelectTextWindow(0);
    ULTIMA_0c22_GRAP_0f_SelectPage(0);

    DrawSpellPage(circle);

    for (;;)
    {
        int key = ReadKey();

        if (key == U5_KEY_ESC)
            break;

        switch (key)
        {
        case U5_KEY_LEFT:
        case U5_KEY_UP:
        case U5_KEY_PGUP:
            circle = (circle + 7) % 8;
            DrawSpellPage(circle);
            break;

        case U5_KEY_RIGHT:
        case U5_KEY_DOWN:
        case U5_KEY_PGDN:
        case U5_KEY_SPACE:
        case U5_KEY_ENTER:
        case '\t':
            circle = (circle + 1) % 8;
            DrawSpellPage(circle);
            break;
        }
    }

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    s_menuOpen = false;
    g_presentFullScreenPage = false;
}

#if defined(ENABLE_SAVE_SLOTS)

// Save slot screen (F6 and F7): the quicksave and slots 1-9, two rows each.
#define SLOT_FIRST_ROW 3
#define SLOT_LABEL_COLUMN 2
#define SLOT_TEXT_COLUMN 6
#define SLOT_TEXT_WIDTH (SCREEN_RIGHT - 1 - SLOT_TEXT_COLUMN)

static bool s_slotLoading;
static int s_chosenSlot;

static void DrawSlotRow(int slot, bool selected)
{
    char line1[48];
    char line2[48];
    int y = SLOT_FIRST_ROW + slot * 2;

    if (SAVESLOTS_Describe(slot, line1, line2) == 0)
        strcpy(line1, "(empty)");

    ULTIMA_1bf2_SetTextPosition(SLOT_LABEL_COLUMN, y);
    if (selected)
        ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);
    ULTIMA_16ba_PrintChar(0x20);
    ULTIMA_16ba_PrintChar(slot == SAVE_SLOT_QUICK ? 'Q' : '0' + slot);
    ULTIMA_16ba_PrintChar(0x20);
    if (selected)
        ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);

    ULTIMA_1bf2_SetTextPosition(SLOT_TEXT_COLUMN, y);
    PrintPadded(line1, SLOT_TEXT_WIDTH);
    ULTIMA_1bf2_SetTextPosition(SLOT_TEXT_COLUMN, y + 1);
    PrintPadded(line2, SLOT_TEXT_WIDTH);
}

static void PrintSlotHelp(char* text)
{
    ULTIMA_1bf2_SetTextPosition(1, SCREEN_BOTTOM - 1);
    PrintPadded("", SCREEN_RIGHT - 1);
    PrintCentered(SCREEN_BOTTOM - 1, text);
}

static void ShowSaveSlots(void)
{
    char dummy[48];
    char question[32];
    time_t saved[SAVE_SLOT_COUNT];
    int selected = s_slotLoading ? SAVE_SLOT_QUICK : 1;
    char* help = s_slotLoading ? "Arrows: choose  Enter: load  Esc: back"
                               : "Arrows: choose  Enter: save  Esc: back";

    s_menuOpen = true;
    g_presentFullScreenPage = true;
    D_538e = 1;
    s_chosenSlot = -1;

    ULTIMA_1c22_SetTextWindowSize(0, 0, 0, SCREEN_RIGHT, SCREEN_BOTTOM);
    ULTIMA_1b94_SelectTextWindow(0);
    ULTIMA_0c22_GRAP_0f_SelectPage(0);

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    DrawFrame();
    PrintCentered(1, s_slotLoading ? "Load Game" : "Save Game");

    // Start on the most recent save: the one to load, or to save over again.
    for (int slot = 0; slot < SAVE_SLOT_COUNT; slot++)
    {
        saved[slot] = SAVESLOTS_Describe(slot, dummy, dummy);
        if (saved[slot] > saved[selected] && (s_slotLoading || slot != SAVE_SLOT_QUICK))
            selected = slot;
    }

    for (int slot = 0; slot < SAVE_SLOT_COUNT; slot++)
        DrawSlotRow(slot, slot == selected);
    PrintSlotHelp(help);

    for (;;)
    {
        int previous = selected;
        int key = ULTIMA_2032_ToUpper(ReadKey());

        if (key == U5_KEY_ESC)
            break;

        if (key == 'Q')
            selected = SAVE_SLOT_QUICK;
        else if (key >= '1' && key <= '9')
            selected = key - '0';
        else if (key == U5_KEY_UP || key == U5_KEY_LEFT)
            selected = (selected + SAVE_SLOT_COUNT - 1) % SAVE_SLOT_COUNT;
        else if (key == U5_KEY_DOWN || key == U5_KEY_RIGHT)
            selected = (selected + 1) % SAVE_SLOT_COUNT;
        else if (key != U5_KEY_ENTER && key != U5_KEY_SPACE)
            continue;

        if (selected != previous)
        {
            DrawSlotRow(previous, false);
            DrawSlotRow(selected, true);
        }

        if (key != U5_KEY_ENTER && key != U5_KEY_SPACE)
            continue;

        if (s_slotLoading)
        {
            if (saved[selected] == 0)
                continue;
        }
        else if (saved[selected] != 0)
        {
            if (selected == SAVE_SLOT_QUICK)
                strcpy(question, "Replace the quicksave? (Y/N)");
            else
                sprintf(question, "Replace slot %d? (Y/N)", selected);
            PrintSlotHelp(question);

            while ((key = ULTIMA_2032_ToUpper(ReadKey())) != 'Y' && key != 'N' && key != U5_KEY_ESC)
            {
            }
            if (key != 'Y')
            {
                PrintSlotHelp(help);
                continue;
            }
        }

        s_chosenSlot = selected;
        break;
    }

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    s_menuOpen = false;
    g_presentFullScreenPage = false;
}

#endif

#if defined(ENABLE_JOURNAL)

// Journal screen (F4): a list of the people who have answered the player's
// questions, under the places they live; typing filters it. Enter shows what
// one person said, keyword by keyword.
#define JOURNAL_FIND_ROW 3
#define JOURNAL_FIRST_ROW 5
#define JOURNAL_ROWS 17
#define JOURNAL_TEXT_COLUMN 2
#define JOURNAL_TEXT_WIDTH (SCREEN_RIGHT - 1 - JOURNAL_TEXT_COLUMN)
#define JOURNAL_FIND_MAX 20
#define JOURNAL_MAX_LINES 2000

typedef struct JournalRow
{
    int person; // -1 for a place's heading
    int mapId;
} JournalRow;

typedef struct JournalLine
{
    char text[40];
    bool keyword;
} JournalLine;

static JournalRow* s_journalRows;
static int s_journalRowCount;
static JournalLine s_journalLines[JOURNAL_MAX_LINES];
static int s_journalLineCount;

// Rows for the people matching filter, each place's heading before them.
static void BuildJournalRows(const char* filter)
{
    int count = JOURNAL_PersonCount();
    int lastMap = -1;

    free(s_journalRows);
    s_journalRows = malloc((count * 2 + 1) * sizeof(JournalRow));
    s_journalRowCount = 0;

    for (int i = 0; i < count; i++)
    {
        JournalPerson* person = JOURNAL_GetPerson(i);

        if (!JOURNAL_PersonMatches(person, filter))
            continue;

        if (person->mapId != lastMap)
        {
            s_journalRows[s_journalRowCount].person = -1;
            s_journalRows[s_journalRowCount++].mapId = person->mapId;
            lastMap = person->mapId;
        }
        s_journalRows[s_journalRowCount].person = i;
        s_journalRows[s_journalRowCount++].mapId = person->mapId;
    }
}

// The next row after row in direction (+1 or -1) that is a person, or row if
// there is none.
static int NextPersonRow(int row, int direction)
{
    for (int i = row + direction; i >= 0 && i < s_journalRowCount; i += direction)
    {
        if (s_journalRows[i].person >= 0)
            return i;
    }
    return row;
}

static void DrawJournalList(const char* filter, int selected, int top)
{
    char text[48];

    ULTIMA_1bf2_SetTextPosition(JOURNAL_TEXT_COLUMN, JOURNAL_FIND_ROW);
    if (filter[0])
    {
        strcpy(text, "Find: ");
        strcat(text, filter);
        strcat(text, "_");
    }
    else
    {
        strcpy(text, "Type to find a word");
    }
    PrintPadded(text, JOURNAL_TEXT_WIDTH);

    for (int i = 0; i < JOURNAL_ROWS; i++)
    {
        int row = top + i;
        int y = JOURNAL_FIRST_ROW + i;

        ULTIMA_1bf2_SetTextPosition(JOURNAL_TEXT_COLUMN, y);
        if (row >= s_journalRowCount)
        {
            if (row == 0)
            {
                PrintPadded(filter[0] ? "No one has mentioned that." : "No one has told you anything yet.",
                    JOURNAL_TEXT_WIDTH);
            }
            else
            {
                PrintPadded("", JOURNAL_TEXT_WIDTH);
            }
            continue;
        }

        if (s_journalRows[row].person < 0)
        {
            PrintPadded(MAP_GetName(s_journalRows[row].mapId, 0), JOURNAL_TEXT_WIDTH);
            continue;
        }

        PrintPadded("", 1);
        if (row == selected)
            ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);
        ULTIMA_16ba_PrintChar(0x20);
        strncpy(text, JOURNAL_PersonTitle(JOURNAL_GetPerson(s_journalRows[row].person)), JOURNAL_TEXT_WIDTH - 4);
        text[JOURNAL_TEXT_WIDTH - 4] = '\0';
        ULTIMA_1850_PrintString(text);
        ULTIMA_16ba_PrintChar(0x20);
        if (row == selected)
            ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);
        PrintPadded("", JOURNAL_TEXT_WIDTH - 3 - (int)strlen(text));
    }
}

static void AddJournalLine(const char* text, int length, bool keyword)
{
    if (s_journalLineCount >= JOURNAL_MAX_LINES)
        return;

    JournalLine* line = &s_journalLines[s_journalLineCount++];
    if (length > (int)sizeof(line->text) - 1)
        length = sizeof(line->text) - 1;
    memcpy(line->text, text, length);
    line->text[length] = '\0';
    line->keyword = keyword;
}

// Word-wraps text into lines of width, starting a new line at each '\n'.
static void AddWrappedLines(const char* text, int width)
{
    char line[40];

    while (*text)
    {
        int length = 0;
        int lastSpace = -1;

        while (text[length] && text[length] != '\n' && length < width)
        {
            if (text[length] == ' ')
                lastSpace = length;
            length++;
        }

        if (text[length] && text[length] != '\n' && text[length] != ' ' && lastSpace > 0)
            length = lastSpace;

        strcpy(line, "  ");
        memcpy(line + 2, text, length);
        AddJournalLine(line, length + 2, false);

        text += length;
        while (*text == ' ' || *text == '\n')
        {
            if (*text++ == '\n')
                break;
        }
    }
}

static void BuildJournalLines(JournalPerson* person)
{
    s_journalLineCount = 0;

    for (int i = 0; i < person->entryCount; i++)
    {
        if (i > 0)
            AddJournalLine("", 0, false);
        AddJournalLine(person->entries[i].keyword, (int)strlen(person->entries[i].keyword), true);
        AddWrappedLines(person->entries[i].text, JOURNAL_TEXT_WIDTH - 2);
    }
}

static void DrawJournalLines(int top)
{
    for (int i = 0; i < JOURNAL_ROWS + 1; i++)
    {
        int index = top + i;

        ULTIMA_1bf2_SetTextPosition(JOURNAL_TEXT_COLUMN, JOURNAL_FIRST_ROW - 1 + i);
        if (index >= s_journalLineCount)
        {
            PrintPadded("", JOURNAL_TEXT_WIDTH);
        }
        else if (s_journalLines[index].keyword)
        {
            ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);
            ULTIMA_16ba_PrintChar(0x20);
            ULTIMA_1850_PrintString(s_journalLines[index].text);
            ULTIMA_16ba_PrintChar(0x20);
            ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);
            PrintPadded("", JOURNAL_TEXT_WIDTH - 2 - (int)strlen(s_journalLines[index].text));
        }
        else
        {
            PrintPadded(s_journalLines[index].text, JOURNAL_TEXT_WIDTH);
        }
    }
}

// One person's page; returns when the player presses Esc.
static void ShowJournalPerson(JournalPerson* person)
{
    char title[40];
    int top = 0;
    int pageRows = JOURNAL_ROWS + 1;

    BuildJournalLines(person);

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    DrawFrame();
    strncpy(title, JOURNAL_PersonTitle(person), JOURNAL_TEXT_WIDTH);
    title[JOURNAL_TEXT_WIDTH] = '\0';
    PrintCentered(1, title);
    PrintCentered(2, MAP_GetName(person->mapId, 0));
    PrintCentered(SCREEN_BOTTOM - 1, "Arrows: scroll   Esc: back");
    DrawJournalLines(top);

    for (;;)
    {
        int key = ReadKey();
        int maxTop = s_journalLineCount > pageRows ? s_journalLineCount - pageRows : 0;
        int previous = top;

        if (key == U5_KEY_ESC)
            return;

        switch (key)
        {
        case U5_KEY_UP:
            top--;
            break;
        case U5_KEY_DOWN:
            top++;
            break;
        case U5_KEY_PGUP:
        case U5_KEY_LEFT:
            top -= pageRows - 1;
            break;
        case U5_KEY_PGDN:
        case U5_KEY_RIGHT:
        case U5_KEY_SPACE:
            top += pageRows - 1;
            break;
        }

        if (top > maxTop)
            top = maxTop;
        if (top < 0)
            top = 0;
        if (top != previous)
            DrawJournalLines(top);
    }
}

static void DrawJournalStatic(void)
{
    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    DrawFrame();
    PrintCentered(1, "Journal");
    PrintCentered(SCREEN_BOTTOM - 1, "Enter: read   Esc: done");
}

static void ShowJournal(void)
{
    char filter[JOURNAL_FIND_MAX + 1] = "";
    int selected;
    int top = 0;

    s_menuOpen = true;
    g_presentFullScreenPage = true;
    D_538e = 1;

    ULTIMA_1c22_SetTextWindowSize(0, 0, 0, SCREEN_RIGHT, SCREEN_BOTTOM);
    ULTIMA_1b94_SelectTextWindow(0);
    ULTIMA_0c22_GRAP_0f_SelectPage(0);

    BuildJournalRows(filter);
    selected = NextPersonRow(-1, 1);
    DrawJournalStatic();
    DrawJournalList(filter, selected, top);

    for (;;)
    {
        int key = ReadKey();
        int length = (int)strlen(filter);
        bool filterChanged = false;

        if (key == U5_KEY_ESC)
            break;

        switch (key)
        {
        case U5_KEY_UP:
            selected = NextPersonRow(selected, -1);
            break;
        case U5_KEY_DOWN:
            selected = NextPersonRow(selected, 1);
            break;
        case U5_KEY_PGUP:
            for (int i = 0; i < JOURNAL_ROWS - 1; i++)
                selected = NextPersonRow(selected, -1);
            break;
        case U5_KEY_PGDN:
            for (int i = 0; i < JOURNAL_ROWS - 1; i++)
                selected = NextPersonRow(selected, 1);
            break;
        case U5_KEY_BACKSPACE:
            if (length > 0)
            {
                filter[length - 1] = '\0';
                filterChanged = true;
            }
            break;
        case U5_KEY_ENTER:
            if (selected >= 0 && selected < s_journalRowCount && s_journalRows[selected].person >= 0)
            {
                ShowJournalPerson(JOURNAL_GetPerson(s_journalRows[selected].person));
                DrawJournalStatic();
            }
            break;
        default:
            key = ULTIMA_2032_ToUpper((u8)key);
            if (((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || key == ' ' || key == '\'') &&
                length < JOURNAL_FIND_MAX && (length > 0 || key != ' '))
            {
                filter[length] = (char)key;
                filter[length + 1] = '\0';
                filterChanged = true;
            }
            break;
        }

        if (filterChanged)
        {
            BuildJournalRows(filter);
            selected = NextPersonRow(-1, 1);
            top = 0;
        }

        // Keep the selection, and its place's heading, on screen.
        if (selected >= 0 && selected < top + 1)
            top = selected > 0 && s_journalRows[selected - 1].person < 0 ? selected - 1 : selected;
        if (selected >= top + JOURNAL_ROWS)
            top = selected - JOURNAL_ROWS + 1;

        DrawJournalList(filter, selected, top);
    }

    free(s_journalRows);
    s_journalRows = NULL;

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    s_menuOpen = false;
    g_presentFullScreenPage = false;
}

#endif

#if defined(ENABLE_AUTOMAP)

// Dungeon map screen (F10): one level of the current dungeon as an 8x8 grid
// of the cells the party has seen, drawn from the dungeon as it is now.
#define AUTOMAP_CELL 16
#define AUTOMAP_X 24
#define AUTOMAP_Y 24
#define AUTOMAP_LEGEND_X 168
#define AUTOMAP_LEGEND_TEXT_COLUMN 23
#define AUTOMAP_LEGEND_FIRST_ROW 3

// EGA colors.
#define EGA_BLACK 0
#define EGA_BROWN 6
#define EGA_LIGHT_GRAY 7
#define EGA_DARK_GRAY 8
#define EGA_LIGHT_BLUE 9
#define EGA_LIGHT_GREEN 10
#define EGA_LIGHT_RED 12
#define EGA_LIGHT_MAGENTA 13
#define EGA_YELLOW 14
#define EGA_WHITE 15

enum
{
    SYMBOL_NONE,
    SYMBOL_UP_LADDER,
    SYMBOL_DOWN_LADDER,
    SYMBOL_LADDER,
    SYMBOL_CHEST,
    SYMBOL_FOUNTAIN,
    SYMBOL_PIT,
    SYMBOL_FIELD,
    SYMBOL_DOOR,
};

static char* s_symbolNames[] =
{
    "", "Up ladder", "Down ladder", "Up & down", "Chest", "Fountain", "Pit", "Field", "Door",
};

// Cell types are the high nibble (see DNGLOOK's Look descriptions). Special
// walls (0xc0) hide secret doors, so they are drawn as walls.
static bool CellIsWall(int cell)
{
    cell &= 0xf0;
    return cell == 0xb0 || cell == 0xc0 || cell == 0xd0;
}

static int CellSymbol(int cell)
{
    switch (cell & 0xf0)
    {
    case 0x10: return SYMBOL_UP_LADDER;
    case 0x20: return SYMBOL_DOWN_LADDER;
    case 0x30: return SYMBOL_LADDER;
    case 0x40: return SYMBOL_CHEST;
    case 0x50: return SYMBOL_FOUNTAIN;
    case 0x60: return SYMBOL_PIT;
    case 0x80: return SYMBOL_FIELD;
    case 0xa0:
    case 0xe0:
    case 0xf0: return SYMBOL_DOOR;
    default: return SYMBOL_NONE; // passages, opened chests, "nothing of note"
    }
}

static void FillBox(int color, int x1, int y1, int x2, int y2)
{
    ULTIMA_0a70_GRAP_2d_SetPenColor(color);
    ULTIMA_0aa6_GRAP_3f_FillRect(x1, y1, x2, y2);
}

static void Line(int color, int x1, int y1, int x2, int y2)
{
    ULTIMA_0a70_GRAP_2d_SetPenColor(color);
    ULTIMA_0b10_GRAP_Line(x1, y1, x2, y2);
}

// An arrow in the 8x8 box at x, y, pointing up (0), right, down or left (3).
static void DrawArrow(int color, int x, int y, int direction)
{
    switch (direction)
    {
    case 0:
        Line(color, x + 3, y, x + 3, y + 7);
        Line(color, x + 4, y, x + 4, y + 7);
        Line(color, x, y + 3, x + 3, y);
        Line(color, x + 7, y + 3, x + 4, y);
        break;
    case 1:
        Line(color, x, y + 3, x + 7, y + 3);
        Line(color, x, y + 4, x + 7, y + 4);
        Line(color, x + 4, y, x + 7, y + 3);
        Line(color, x + 4, y + 7, x + 7, y + 4);
        break;
    case 2:
        Line(color, x + 3, y, x + 3, y + 7);
        Line(color, x + 4, y, x + 4, y + 7);
        Line(color, x, y + 4, x + 3, y + 7);
        Line(color, x + 7, y + 4, x + 4, y + 7);
        break;
    default:
        Line(color, x, y + 3, x + 7, y + 3);
        Line(color, x, y + 4, x + 7, y + 4);
        Line(color, x + 3, y, x, y + 3);
        Line(color, x + 3, y + 7, x, y + 4);
        break;
    }
}

// A symbol in the 8x8 box at x, y.
static void DrawSymbol(int symbol, int x, int y)
{
    switch (symbol)
    {
    case SYMBOL_UP_LADDER:
        DrawArrow(EGA_LIGHT_GREEN, x, y, 0);
        break;
    case SYMBOL_DOWN_LADDER:
        DrawArrow(EGA_LIGHT_RED, x, y, 2);
        break;
    case SYMBOL_LADDER:
        Line(EGA_YELLOW, x + 3, y, x + 3, y + 7);
        Line(EGA_YELLOW, x + 4, y, x + 4, y + 7);
        Line(EGA_YELLOW, x + 1, y + 2, x + 3, y);
        Line(EGA_YELLOW, x + 6, y + 2, x + 4, y);
        Line(EGA_YELLOW, x + 1, y + 5, x + 3, y + 7);
        Line(EGA_YELLOW, x + 6, y + 5, x + 4, y + 7);
        break;
    case SYMBOL_CHEST:
        FillBox(EGA_YELLOW, x + 1, y + 2, x + 6, y + 6);
        Line(EGA_BROWN, x + 1, y + 4, x + 6, y + 4);
        break;
    case SYMBOL_FOUNTAIN:
        FillBox(EGA_LIGHT_BLUE, x + 1, y + 1, x + 6, y + 6);
        break;
    case SYMBOL_PIT:
        // A black hole, outlined so it shows on black too.
        FillBox(EGA_LIGHT_GRAY, x, y, x + 7, y + 7);
        FillBox(EGA_BLACK, x + 1, y + 1, x + 6, y + 6);
        break;
    case SYMBOL_FIELD:
        FillBox(EGA_LIGHT_MAGENTA, x + 1, y + 1, x + 6, y + 6);
        break;
    case SYMBOL_DOOR:
        FillBox(EGA_BROWN, x, y + 1, x + 7, y + 6);
        break;
    }
}

static void DrawAutomapLevel(int dungeon, int level)
{
    char text[40];

    // Title and the grid area, cleared.
    ULTIMA_1bf2_SetTextPosition(1, 1);
    PrintPadded("", SCREEN_RIGHT - 1);
    sprintf(text, "%s - Level %d", MAP_GetName(dungeon + 0x21, 0), level + 1);
    PrintCentered(1, text);
    FillBox(EGA_BLACK, AUTOMAP_X - 1, AUTOMAP_Y - 1,
        AUTOMAP_X + AUTOMAP_SIZE * AUTOMAP_CELL, AUTOMAP_Y + AUTOMAP_SIZE * AUTOMAP_CELL);

    for (int y = 0; y < AUTOMAP_SIZE; y++)
    {
        for (int x = 0; x < AUTOMAP_SIZE; x++)
        {
            int left = AUTOMAP_X + x * AUTOMAP_CELL;
            int top = AUTOMAP_Y + y * AUTOMAP_CELL;
            int cell = GetDungeonMap(x, y, level);

            if (!AUTOMAP_IsSeen(dungeon, level, x, y))
                continue;

            if (CellIsWall(cell))
            {
                FillBox(EGA_LIGHT_GRAY, left, top, left + AUTOMAP_CELL - 1, top + AUTOMAP_CELL - 1);
                continue;
            }

            FillBox(EGA_DARK_GRAY, left + 1, top + 1, left + AUTOMAP_CELL - 2, top + AUTOMAP_CELL - 2);
            DrawSymbol(CellSymbol(cell), left + 4, top + 4);
        }
    }

    if (level == D_5895_map_level)
    {
        int left = AUTOMAP_X + D_5896_map_x * AUTOMAP_CELL;
        int top = AUTOMAP_Y + D_5897_map_y * AUTOMAP_CELL;

        FillBox(EGA_DARK_GRAY, left + 1, top + 1, left + AUTOMAP_CELL - 2, top + AUTOMAP_CELL - 2);
        DrawArrow(EGA_WHITE, left + 4, top + 4, D_6603 & 3);
    }

    // The level the party is on, if another is shown.
    ULTIMA_1bf2_SetTextPosition(1, SCREEN_BOTTOM - 3);
    PrintPadded("", SCREEN_RIGHT - 1);
    if (level != D_5895_map_level)
    {
        sprintf(text, "You are on level %d", D_5895_map_level + 1);
        PrintCentered(SCREEN_BOTTOM - 3, text);
    }
}

static void DrawAutomapLegend(void)
{
    int row = AUTOMAP_LEGEND_FIRST_ROW;

    DrawArrow(EGA_WHITE, AUTOMAP_LEGEND_X, row * 8, 0);
    PrintAt(AUTOMAP_LEGEND_TEXT_COLUMN, row, "You");
    row += 2;

    for (int symbol = SYMBOL_UP_LADDER; symbol <= SYMBOL_DOOR; symbol++, row += 2)
    {
        DrawSymbol(symbol, AUTOMAP_LEGEND_X, row * 8);
        PrintAt(AUTOMAP_LEGEND_TEXT_COLUMN, row, s_symbolNames[symbol]);
    }
}

// The next level in direction (+1 or -1) with something seen, or level.
static int NextSeenLevel(int dungeon, int level, int direction)
{
    for (int i = level + direction; i >= 0 && i < AUTOMAP_LEVELS; i += direction)
    {
        if (AUTOMAP_LevelSeen(dungeon, i))
            return i;
    }
    return level;
}

static void ShowAutomap(void)
{
    int dungeon = AUTOMAP_CurrentDungeon();
    int level = D_5895_map_level;

    s_menuOpen = true;
    g_presentFullScreenPage = true;
    D_538e = 1;

    ULTIMA_1c22_SetTextWindowSize(0, 0, 0, SCREEN_RIGHT, SCREEN_BOTTOM);
    ULTIMA_1b94_SelectTextWindow(0);
    ULTIMA_0c22_GRAP_0f_SelectPage(0);

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    DrawFrame();

    if (dungeon < 0)
    {
        PrintCentered(1, "Dungeon Map");
        PrintCentered(10, "The map shows the dungeons");
        PrintCentered(11, "you explore. You aren't in one.");
        PrintCentered(SCREEN_BOTTOM - 1, "Any key: done");
        ReadKey();
    }
    else
    {
        AUTOMAP_Flush();
        DrawAutomapLegend();
        DrawAutomapLevel(dungeon, level);
        PrintCentered(SCREEN_BOTTOM - 1, "PgUp/PgDn: level   Esc: done");

        for (;;)
        {
            int key = ReadKey();
            int previous = level;

            if (key == U5_KEY_ESC || key == U5_KEY_ENTER)
                break;

            if (key == U5_KEY_PGUP || key == U5_KEY_UP || key == U5_KEY_LEFT)
                level = NextSeenLevel(dungeon, level, -1);
            else if (key == U5_KEY_PGDN || key == U5_KEY_DOWN || key == U5_KEY_RIGHT)
                level = NextSeenLevel(dungeon, level, 1);

            if (level != previous)
                DrawAutomapLevel(dungeon, level);
        }
    }

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    s_menuOpen = false;
    g_presentFullScreenPage = false;
}

#endif

#if defined(ENABLE_GAMEPAD)

// The gamepad's key menu (Y): every key the game asks for, in a grid, so a
// command, an answer or a word can be entered without a keyboard.
#define PAD_KEYS_COLUMNS 3
#define PAD_KEYS_ROWS 13
#define PAD_KEYS_FIRST_ROW 3
#define PAD_KEYS_CELL_WIDTH 12
#define PAD_KEYS_TYPED_ROW (PAD_KEYS_FIRST_ROW + PAD_KEYS_ROWS + 1)
#define PAD_KEY_DELETE 0x7f // in the grid only; given to the game as Backspace

typedef struct PadKey
{
    u16 key;
    char* label;
} PadKey;

static PadKey s_padKeys[PAD_KEYS_COLUMNS * PAD_KEYS_ROWS] =
{
    {'a', "A Attack"},  {'b', "B Board"},   {'c', "C Cast"},
    {'d', "D"},         {'e', "E Enter"},   {'f', "F Fire"},
    {'g', "G Get"},     {'h', "H Hole up"}, {'i', "I Ignite"},
    {'j', "J Jimmy"},   {'k', "K Klimb"},   {'l', "L Look"},
    {'m', "M Mix"},     {'n', "N New order"}, {'o', "O Open"},
    {'p', "P Push"},    {'q', "Q Quit"},    {'r', "R Ready"},
    {'s', "S Search"},  {'t', "T Talk"},    {'u', "U Use"},
    {'v', "V View"},    {'w', "W"},         {'x', "X X-it"},
    {'y', "Y Yell/Yes"}, {'z', "Z Z-stats"}, {'0', "0"},
    {'1', "1"},         {'2', "2"},         {'3', "3"},
    {'4', "4"},         {'5', "5"},         {'6', "6"},
    {'7', "7"},         {'8', "8"},         {'9', "9"},
    {U5_KEY_SPACE, "Space"}, {PAD_KEY_DELETE, "Delete"}, {U5_KEY_ENTER, "Enter"},
};

static int s_padKeyCount;
static int s_padKeyMax;
static u16* s_padKeyOut;

static void DrawPadKey(int index, bool selected)
{
    int column = index % PAD_KEYS_COLUMNS;
    int row = index / PAD_KEYS_COLUMNS;

    ULTIMA_1bf2_SetTextPosition(2 + column * PAD_KEYS_CELL_WIDTH, PAD_KEYS_FIRST_ROW + row);
    if (selected)
        ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);
    ULTIMA_16ba_PrintChar(0x20);
    PrintPadded(s_padKeys[index].label, PAD_KEYS_CELL_WIDTH - 2);
    if (selected)
        ULTIMA_16ba_PrintChar(CTRL_CHAR_TOGGLE_INVERSION);
}

static void DrawPadTyped(void)
{
    char text[48] = "Typed: ";
    int length = (int)strlen(text);

    for (int i = 0; i < s_padKeyCount && length < (int)sizeof(text) - 2; i++)
    {
        u16 key = s_padKeyOut[i];
        text[length++] = key == U5_KEY_BACKSPACE ? '<' : (char)ULTIMA_2032_ToUpper((u8)key);
    }
    text[length++] = '_';
    text[length] = '\0';

    ULTIMA_1bf2_SetTextPosition(2, PAD_KEYS_TYPED_ROW);
    PrintPadded(text, SCREEN_RIGHT - 3);
}

// Adds the key in the grid to the keys typed: Delete takes back the last one,
// or, with none, is passed on to the game as Backspace.
static void AddPadKey(u16 key)
{
    if (key == PAD_KEY_DELETE && s_padKeyCount > 0)
    {
        s_padKeyCount--;
        return;
    }
    if (s_padKeyCount < s_padKeyMax)
        s_padKeyOut[s_padKeyCount++] = key == PAD_KEY_DELETE ? U5_KEY_BACKSPACE : key;
}

static void ShowPadKeys(void)
{
    static int s_selected; // remembered between openings
    int count = ARRAYSIZE(s_padKeys);

    s_menuOpen = true;
    g_presentFullScreenPage = true;
    D_538e = 1;

    ULTIMA_1c22_SetTextWindowSize(0, 0, 0, SCREEN_RIGHT, SCREEN_BOTTOM);
    ULTIMA_1b94_SelectTextWindow(0);
    ULTIMA_0c22_GRAP_0f_SelectPage(0);

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    DrawFrame();
    PrintCentered(1, "Keys");
    for (int i = 0; i < count; i++)
        DrawPadKey(i, i == s_selected);
    DrawPadTyped();
    PrintCentered(SCREEN_BOTTOM - 3, "A: pick   X: pick and type more");
    PrintCentered(SCREEN_BOTTOM - 2, "B: cancel");

    for (;;)
    {
        int previous = s_selected;
        int key = ReadKey();

        if (key == U5_KEY_ESC)
        {
            s_padKeyCount = 0;
            break;
        }

        if (key == U5_KEY_ENTER)
        {
            AddPadKey(s_padKeys[s_selected].key);
            break;
        }

        switch (key)
        {
        case U5_KEY_SPACE:
            AddPadKey(s_padKeys[s_selected].key);
            DrawPadTyped();
            break;
        case U5_KEY_LEFT:
            s_selected = (s_selected + count - 1) % count;
            break;
        case U5_KEY_RIGHT:
            s_selected = (s_selected + 1) % count;
            break;
        case U5_KEY_UP:
            s_selected = (s_selected + count - PAD_KEYS_COLUMNS) % count;
            break;
        case U5_KEY_DOWN:
            s_selected = (s_selected + PAD_KEYS_COLUMNS) % count;
            break;
        }

        if (s_selected != previous)
        {
            DrawPadKey(previous, false);
            DrawPadKey(s_selected, true);
        }
    }

    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    s_menuOpen = false;
    g_presentFullScreenPage = false;
}

#endif

bool OPTIONS_MENU_IsOpen(void)
{
    return s_menuOpen;
}

// Saves the screen and text state, runs a full-screen page over the game,
// then puts everything back.
static void RunOverGame(void (*page)(void))
{
    TextState saved;

    if (s_menuOpen)
        return;

    memcpy(saved.windows, D_535e_textWindows, sizeof(saved.windows));
    saved.windowIdx = D_5386_current_text_window_idx;
    saved.charsetIdx = D_5388_current_charset_idx;
    saved.advance = D_538e;
    saved.charset = D_5398_currentCharset;
    saved.window = D_539a_currentTextWindow;
    saved.underline = D_53a4_underline;
    saved.flag53a6 = D_53a6;
    saved.inverse = D_53a8_inverse;
    saved.fg = D_53aa_text_fg_color;
    saved.bg = D_53ab_text_bg_color;
    saved.video = D_52ba_vdp;
    GRAP_BUF_SaveScreen();
    GRAP_BUF_ClearColorLayer();

    // Start from plain white text in window 0, whatever the game was doing.
    D_53a4_underline = 0;
    D_53a6 = 0;
    D_53a8_inverse = 0;
    ULTIMA_1c9e_SelectCharset(0);

    page();

    GRAP_BUF_RestoreScreen();
    memcpy(D_535e_textWindows, saved.windows, sizeof(saved.windows));
    D_5386_current_text_window_idx = saved.windowIdx;
    D_5388_current_charset_idx = saved.charsetIdx;
    D_538e = saved.advance;
    D_5398_currentCharset = saved.charset;
    D_539a_currentTextWindow = saved.window;
    D_53a4_underline = saved.underline;
    D_53a6 = saved.flag53a6;
    D_53a8_inverse = saved.inverse;
    D_53aa_text_fg_color = saved.fg;
    D_53ab_text_bg_color = saved.bg;
    D_52ba_vdp = saved.video;
}

// Opened with F1 while the game waits for a key.
void OPTIONS_MENU_RunInGame(void)
{
    RunOverGame(OPTIONS_MENU_Run);
}

// Opened with F2 while the game waits for a key.
void OPTIONS_MENU_ShowShortcutsInGame(void)
{
    RunOverGame(ShowShortcuts);
}

// Opened with F3 while the game waits for a key.
void OPTIONS_MENU_ShowSpellsInGame(void)
{
    RunOverGame(ShowSpells);
}

#if defined(ENABLE_JOURNAL)
// Opened with F4 while the game waits for a key.
void OPTIONS_MENU_ShowJournalInGame(void)
{
    RunOverGame(ShowJournal);
}
#endif

#if defined(ENABLE_AUTOMAP)
// Opened with F10 while the game waits for a key.
void OPTIONS_MENU_ShowAutomapInGame(void)
{
    RunOverGame(ShowAutomap);
}
#endif

#if defined(ENABLE_GAMEPAD)
// Opened with the gamepad's Y button while the game waits for a key.
int OPTIONS_MENU_ChooseKeys(u16* keys, int max)
{
    s_padKeyOut = keys;
    s_padKeyMax = max;
    s_padKeyCount = 0;
    RunOverGame(ShowPadKeys);
    return s_padKeyCount;
}
#endif

#if defined(ENABLE_SAVE_SLOTS)
// Opened with F6 (saving) or F7 (loading) at a command prompt.
int OPTIONS_MENU_ChooseSaveSlot(bool loading)
{
    s_slotLoading = loading;
    s_chosenSlot = -1;
    RunOverGame(ShowSaveSlots);
    return s_chosenSlot;
}
#endif

#endif
