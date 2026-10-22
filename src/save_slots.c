#include "common/common.h"
#include "funcs.h"
#include "macros.h"
#include "vars.h"

#include "save_slots.h"

#if defined(ENABLE_SAVE_SLOTS)

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "audio/audio.h"
#include "cast2.h"
#include "common/file.h"
#include "map_name.h"
#include "options_menu.h"
#include "savegame.h"

#if defined(OS_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

// Quicksave (F5), quickload (F9) and nine save slots (F6 save, F7 load).
//
// A slot is a folder in SAVEGAME holding copies of SAVED.GAM and SAVED.OOL,
// the two files the Quit command writes; Journey Onward rebuilds BRIT.OOL and
// UNDER.OOL from them. Saving to a slot saves as Quit does and copies the
// result into the slot. Loading copies the slot back and restarts the game
// from it as Journey Onward does, so either way the slot also becomes the
// game Journey Onward continues.
//
// Both only happen at a command prompt, where Quit can save, so the game is
// never caught halfway through a move or a fight.

jmp_buf g_saveSlotsLoadJump;

static char* s_saveFiles[] = {"SAVED.GAM", "SAVED.OOL"};

// Offsets into SAVED.GAM, which is the block of variables from D_55a6.
#define GAM_LEADER_NAME 0x002
#define GAM_LEADER_LEVEL (0x002 + 0x16)
#define GAM_YEAR 0x2ce
#define GAM_MONTH 0x2d7
#define GAM_DAY 0x2d8
#define GAM_MAP_ID 0x2ed
#define GAM_MAP_LEVEL 0x2ef
#define GAM_HEADER_SIZE 0x2f0

static void SlotDir(int slot, char* out)
{
    if (slot == SAVE_SLOT_QUICK)
        strcpy(out, "SAVEGAME" PATH_SEP "QUICK");
    else
        sprintf(out, "SAVEGAME" PATH_SEP "SLOT%d", slot);
}

static bool CopySaveFile(const char* from, const char* to)
{
    char buf[0x400];
    size_t n;
    bool ok = true;

    FILE* in = FILE_Open(from, "rb");
    if (!in)
        return false;

    FILE* out = FILE_Open(to, "wb");
    if (!out)
    {
        fclose(in);
        return false;
    }

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
        {
            ok = false;
            break;
        }
    }

    fclose(in);
    if (fclose(out) != 0)
        ok = false;
    return ok;
}

// Copies the save files between SAVEGAME and a slot's folder.
static bool CopySlot(int slot, bool toSlot)
{
    char dir[64];
    char current[64];
    char inSlot[96];

    SlotDir(slot, dir);
    if (toSlot)
    {
        u5_mkdir("SAVEGAME");
        u5_mkdir(dir);
    }

    for (int i = 0; i < ARRAYSIZE(s_saveFiles); i++)
    {
        sprintf(current, "SAVEGAME" PATH_SEP "%s", s_saveFiles[i]);
        sprintf(inSlot, "%s" PATH_SEP "%s", dir, s_saveFiles[i]);
        if (!(toSlot ? CopySaveFile(current, inSlot) : CopySaveFile(inSlot, current)))
            return false;
    }
    return true;
}

time_t SAVESLOTS_Describe(int slot, char* line1, char* line2)
{
    char dir[64];
    char path[96];
    u8 gam[GAM_HEADER_SIZE];
    char name[9];
    struct stat info;
    char when[16];

    line1[0] = '\0';
    line2[0] = '\0';

    // A slot needs both files to be loaded.
    SlotDir(slot, dir);
    sprintf(path, "%s" PATH_SEP "%s", dir, s_saveFiles[1]);
    if (stat(path, &info) != 0)
        return 0;

    sprintf(path, "%s" PATH_SEP "%s", dir, s_saveFiles[0]);
    FILE* stream = FILE_Open(path, "rb");
    if (!stream)
        return 0;
    size_t size = fread(gam, 1, sizeof(gam), stream);
    fclose(stream);
    if (size != sizeof(gam) || stat(path, &info) != 0)
        return 0;

    memcpy(name, gam + GAM_LEADER_NAME, 8);
    name[8] = '\0';
    sprintf(line1, "%-8s Lv%-2d %s", name, gam[GAM_LEADER_LEVEL],
        MAP_GetName(gam[GAM_MAP_ID], gam[GAM_MAP_LEVEL]));

    strftime(when, sizeof(when), "%b %d %H:%M", localtime(&info.st_mtime));
    sprintf(line2, "Day %d-%d-%d  %s", gam[GAM_MONTH], gam[GAM_DAY],
        gam[GAM_YEAR] | (gam[GAM_YEAR + 1] << 8), when);

    return info.st_mtime;
}

static void SaveToSlot(int slot)
{
    ULTIMA_1850_PrintString("Saving...\n");
    CAST2_WriteSaveFiles();
    if (CopySlot(slot, true))
        ULTIMA_1850_PrintString("Done.\n");
    else
        ULTIMA_1850_PrintString("Save failed!\n");
}

// Does not return unless the slot can't be read.
static void LoadFromSlot(int slot)
{
    ULTIMA_1850_PrintString("Loading...\n");
    if (!CopySlot(slot, false))
    {
        ULTIMA_1850_PrintString("Load failed!\n");
        return;
    }

#if defined(ENABLE_BGM)
    AUDIO_StopBgm();
#endif
    longjmp(g_saveSlotsLoadJump, 1);
}

// Y or N, as the Quit command asks; Esc also answers no.
static bool AskYesNo(void)
{
    int key;

    while ((key = ULTIMA_266c_GetChar()) != 'Y' && key != 'N' && key != U5_KEY_ESC)
    {
    }

    ULTIMA_1850_PrintString(key == 'Y' ? "Yes\n" : "No\n");
    return key == 'Y';
}

void SAVESLOTS_Command(int key)
{
    char dummy[48];
    char text[24];
    int slot;

    switch (key)
    {
    case U5_KEY_QUICKSAVE:
        ULTIMA_1850_PrintString("Quicksave\n");
        SaveToSlot(SAVE_SLOT_QUICK);
        break;

    case U5_KEY_QUICKLOAD:
        if (SAVESLOTS_Describe(SAVE_SLOT_QUICK, dummy, dummy) == 0)
        {
            ULTIMA_1850_PrintString("Quickload\nNo quicksave!\n");
            break;
        }
        ULTIMA_1850_PrintString("Quickload? ");
        if (AskYesNo())
            LoadFromSlot(SAVE_SLOT_QUICK);
        break;

    case U5_KEY_SAVE_SLOT:
        slot = OPTIONS_MENU_ChooseSaveSlot(false);
        if (slot < 0)
            break;
        if (slot == SAVE_SLOT_QUICK)
            strcpy(text, "Quicksave\n");
        else
            sprintf(text, "Save to slot %d\n", slot);
        ULTIMA_1850_PrintString(text);
        SaveToSlot(slot);
        break;

    case U5_KEY_LOAD_SLOT:
        slot = OPTIONS_MENU_ChooseSaveSlot(true);
        if (slot < 0)
            break;
        if (slot == SAVE_SLOT_QUICK)
            strcpy(text, "Quickload\n");
        else
            sprintf(text, "Load slot %d\n", slot);
        ULTIMA_1850_PrintString(text);
        LoadFromSlot(slot);
        break;
    }
}

// The rest of Journey Onward (INTRO_0986_Main, 'J'), after the save files are
// in place: read them and set up the world they describe. The main loop then
// enters the saved map as it does after the title screen.
void SAVESLOTS_ResumeLoadedGame(void)
{
    ULTIMA_637e_DrawFrame();
    ULTIMA_2e96_SetWindDirection(0);
    ULTIMA_1b94_SelectTextWindow(2);
    ULTIMA_16ba_PrintChar(CTRL_CHAR_CLEAR_WINDOW);
    ULTIMA_1bf2_SetTextPosition(0, 0xc);

    ULTIMA_251e_SwitchDisks(3);
    FILE_ReadSavegameFile("SAVED.GAM");
    ULTIMA_256e_ReadFileFromDisk("SAVED.OOL", D_b21e, 0x200, 0);
    ULTIMA_251e_SwitchDisks(1);
    ULTIMA_25d8_WriteFileToDisk("BRIT.OOL", D_b21e, 0x100);
    ULTIMA_25d8_WriteFileToDisk("UNDER.OOL", D_b31e, 0x100);
    if ((D_5893_map_id == 0) && (D_5895_map_level != 0))
    {
        ULTIMA_251e_SwitchDisks(5);
        while (ULTIMA_1674_TestOpenFile("UNDER.DAT") == 0)
        {
        }
        ULTIMA_25d8_WriteFileToDisk("UNDER.OOL", D_b31e, 0x100);
    }
    ULTIMA_2e96_SetWindDirection(-1);
    D_52ba_vdp._52be_tileYOffset = 8;

    ULTIMA_2900_UpdateVitalsDisplay();
}

#endif
