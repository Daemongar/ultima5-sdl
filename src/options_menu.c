#include "common/common.h"
#include "common/options.h"
#include "funcs.h"
#include "macros.h"
#include "vars.h"

#include "options_menu.h"
#include "save_slots.h"
#include "graphics/grap_buf.h"
#include "key/key.h"
#include "time/time.h"

#if defined(TARGET_SDL)

#include <stdio.h>
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

    {PAGE_SOUND, ITEM_TOGGLE, "Music", &g_options.music, NULL},
    {PAGE_SOUND, ITEM_VOLUME, "Music volume", NULL, &g_options.musicVolume},
    {PAGE_SOUND, ITEM_TOGGLE, "Sound effects", &g_options.soundEffects, NULL},
    {PAGE_SOUND, ITEM_VOLUME, "Effects volume", NULL, &g_options.soundVolume},
    {PAGE_SOUND, ITEM_TOGGLE, "Enhanced effects", &g_options.enhancedSounds, NULL},
    {PAGE_SOUND, ITEM_TOGGLE, "Terrain footsteps", &g_options.terrainFootsteps, NULL},

    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Mouse aiming", &g_options.mouseAim, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Dialog improvements", &g_options.dialogImprovements, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Smarter monsters", &g_options.smarterAi, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Party shares exp.", &g_options.partyExp, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Group exp. bonus", &g_options.groupExpBonus, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Brighter nights", &g_options.brighterNight, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Esc asks to quit", &g_options.quickExit, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Climbing costs time", &g_options.terrainElevation, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Class stat growth", &g_options.classStatGrowth, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Varied battlefields", &g_options.variedCombat, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Vas Mani in combat", &g_options.vasManiInCombat, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "No wasted casts", &g_options.noWastedCasts, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Inn poison not fatal", &g_options.noInnPoisonDeath, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Stat point per level", &g_options.statPerLevel, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Vanishing rings", &g_options.vanishingRings, NULL},
    {PAGE_GAMEPLAY, ITEM_TOGGLE, "Ultima IV class mana", &g_options.u4ClassMana, NULL},
};

static char* s_pageNames[PAGE_COUNT] = {"Display", "Sound", "Gameplay"};
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
#define MAX_PAGE_ITEMS 16
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
    PrintCentered(SCREEN_BOTTOM - 2, "Arrows: choose and change");
    PrintCentered(SCREEN_BOTTOM - 1, "Tab: next page   Esc: done");
}

static void DrawTabs(int page)
{
    int x = 6;
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
        x += (int)strlen(s_pageNames[p]) + 4;
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
    "F5     Quicksave",  "F9     Quickload",
    "F8     Reload BMP", "F6/F7  Save/load",
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
    PrintTwoColumns(s_commandKeys, ARRAYSIZE(s_commandKeys), 3);
    PrintTwoColumns(s_otherKeys, ARRAYSIZE(s_otherKeys), 15);
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
