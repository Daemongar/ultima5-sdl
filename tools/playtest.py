#!/usr/bin/env python3
"""Scripted playthrough tests.

Each test copies your Ultima V data to a temporary folder, writes a new game
there (the party from INIT.GAM, as character creation makes it, plus any
changes the test needs), plays a script of key presses with `--script` (see
src/playtest.c), and checks the files the game leaves behind. Your own game
folder, saves and settings are never touched.

    python3 tools/playtest.py                 # run every test
    python3 tools/playtest.py quicksave       # run tests whose name contains this
    python3 tools/playtest.py --list
    python3 tools/playtest.py --keep          # keep the temporary folders

The game data isn't part of this repository, so the tests use the Ultima V
folder the game remembers, or --data <folder>. --exe picks the program
(default: the Release build for this platform).
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Offsets into SAVED.GAM, which holds the variables from D_55a6 (src/vars.h).
LEADER_NAME = 0x002       # D_55a8_party[0].name
LEADER_STR = 0x002 + 0x0c
LEADER_INT = 0x002 + 0x0e
LEADER_MAG = 0x002 + 0x0f
MIXED_SPELLS = 0x24a      # D_57f0, 48 spells
TORCHES = 0x208           # D_57ae
REAGENTS = 0x2aa          # D_5850, 8 reagents
MAP_ID = 0x2ed            # D_5893_map_id
MAP_LEVEL = 0x2ef
MAP_X = 0x2f0
MAP_Y = 0x2f1
CHUNK_X = 0x2f5           # D_589b
CHUNK_Y = 0x2f6           # D_589c
TORCH_LIGHT = 0x300       # D_58a6
DUNGEON_MAP = 0x3b4       # D_595a, 8 levels of 8x8 cells
OBJECTS = 0x6b4           # D_5c5a, 32 objects of 8 bytes
FACING = 0x105d           # D_6603

PARTY_COUNT = 0x2b5       # D_585b
MARIAH = 0x002 + 3 * 0x20 # party record 3, a mage in INIT.GAM

GINSENG, SPIDER_SILK = 1, 3
SPELL_MANI = 4
SPELL_IN_XEN_MANI = 11
DESPISE = 0x22            # map ID; dungeon 1 in AUTOMAP.DAT

# Skip the title sequence and choose Journey Onward.
START = "wait 1500 space wait 1500 j wait 3000"


def remembered_data_folder():
    if sys.platform == "win32":
        base = Path(os.environ.get("APPDATA", "")) / "ultima5-sdl" / "UltimaV"
    else:
        base = Path.home() / ".local" / "share" / "ultima5-sdl" / "UltimaV"
    path_file = base / "data_path.txt"
    if path_file.is_file():
        return Path(path_file.read_text().strip())
    return None


def default_exe():
    if sys.platform == "win32":
        return ROOT / "u5win" / "x64" / "Release" / "u5win.exe"
    return ROOT / "build" / "linux" / "release" / "u5linux"


def find_file(folder, name):
    """A file by name in folder or its GOG __support/save, ignoring case."""
    for directory in (folder, folder / "__support" / "save"):
        if directory.is_dir():
            for entry in directory.iterdir():
                if entry.name.upper() == name.upper():
                    return entry
    return None


class Game:
    """A copy of the game data in a temporary folder, with a new game."""

    def __init__(self, data, folder):
        self.folder = folder
        shutil.copytree(
            data, folder, dirs_exist_ok=True,
            ignore=shutil.ignore_patterns(
                "SAVEGAME", "BGM*", "*.exe", "*.dll", "*.pdb", "*.ini", "LOG.TXT", "*.log"))
        (folder / "ultima5.ini").write_text(
            "[window]\nmode=windowed\nwidth=960\nheight=600\n"
            "[sound]\nmusic=0\nsound_effects=0\n")

        self.saves = folder / "SAVEGAME"
        self.saves.mkdir()

        # What character creation (FONT_0b0a) writes: INIT.GAM with the
        # Avatar named, and SAVED.OOL with no outdoor objects.
        self.gam = bytearray(find_file(folder, "INIT.GAM").read_bytes())
        name = b"Tester"
        self.gam[LEADER_NAME:LEADER_NAME + 9] = name + bytes(9 - len(name))
        self.gam[LEADER_STR] = 20
        self.gam[LEADER_MAG] = self.gam[LEADER_INT]
        self.ool = bytes(0x100) + find_file(folder, "INIT.OOL").read_bytes()[:0x100]

    def write_save(self):
        (self.saves / "SAVED.GAM").write_bytes(self.gam)
        (self.saves / "SAVED.OOL").write_bytes(self.ool)
        # Backdated, so a save the game writes is always newer.
        os.utime(self.saves / "SAVED.GAM", (1, 1))

    def saved_by_game(self):
        path = self.saves / "SAVED.GAM"
        return path.is_file() and path.stat().st_mtime > 1

    def play(self, exe, script, timeout):
        script_file = self.folder / "test.keys"
        script_file.write_text(script)
        return subprocess.run(
            [str(exe), "--data", str(self.folder), "--script", str(script_file)],
            cwd=self.folder, capture_output=True, text=True, timeout=timeout)

    def saved(self, slot=None):
        path = self.saves / slot / "SAVED.GAM" if slot else self.saves / "SAVED.GAM"
        return path.read_bytes() if path.is_file() else None


class TestFailed(Exception):
    pass


def check(condition, message):
    if not condition:
        raise TestFailed(message)


# Each test: (name, what it shows, set up the new game, script, check results).

def setup_none(game):
    pass


def check_saved(game):
    gam = game.saved()
    check(game.saved_by_game(), "Quit didn't write SAVED.GAM")
    check(gam[LEADER_NAME:LEADER_NAME + 6] == b"Tester", "the saved leader isn't Tester")
    check(gam[MAP_ID] == game.gam[MAP_ID], "the party isn't where the new game starts")


def setup_torches(game):
    game.gam[TORCHES] = 5


def check_quickload(game):
    gam = game.saved()
    check((game.saves / "QUICK" / "SAVED.GAM").is_file(), "F5 didn't write the QUICK slot")
    check(gam is not None, "Quit didn't write SAVED.GAM")
    # 5 torches, one lit after the quicksave and undone by F9, then one more.
    check(gam[TORCHES] == 4, f"expected 4 torches after the quickload, found {gam[TORCHES]}")


def check_slots(game):
    gam = game.saved()
    check(game.saved("SLOT3") is not None, "F6 didn't write slot 3")
    check(game.saved("SLOT3")[TORCHES] == 5, "slot 3 doesn't hold the game as saved")
    check(gam is not None, "Quit didn't write SAVED.GAM")
    check(gam[TORCHES] == 5, f"expected loading slot 3 to bring back 5 torches, found {gam[TORCHES]}")


def setup_reagents(game):
    game.gam[REAGENTS:REAGENTS + 8] = bytes(8)
    game.gam[REAGENTS + GINSENG] = 5
    game.gam[REAGENTS + SPIDER_SILK] = 5
    game.gam[MIXED_SPELLS + SPELL_MANI] = 0


def check_mixed(game):
    gam = game.saved()
    check(gam is not None, "Quit didn't write SAVED.GAM")
    check(gam[MIXED_SPELLS + SPELL_MANI] == 3, f"expected 3 Mani mixed, found {gam[MIXED_SPELLS + SPELL_MANI]}")
    check(gam[REAGENTS + GINSENG] == 2 and gam[REAGENTS + SPIDER_SILK] == 2,
          "mixing didn't use one ginseng and one spider silk for each")


def setup_dungeon(game):
    # The party on Despise's first level, at its ladder up, with a torch lit.
    dungeons = find_file(game.folder, "DUNGEON.DAT").read_bytes()
    levels = dungeons[(DESPISE - 0x21) * 0x200:(DESPISE - 0x20) * 0x200]
    game.gam[DUNGEON_MAP:DUNGEON_MAP + 0x200] = levels
    ladder = next(i for i in range(0x40) if levels[i] & 0xf0 == 0x10)
    game.gam[MAP_ID] = DESPISE
    game.gam[MAP_LEVEL] = 0
    game.gam[MAP_X] = ladder % 8
    game.gam[MAP_Y] = ladder // 8
    game.gam[FACING] = 0
    game.gam[TORCH_LIGHT] = 50
    # No monster on the level yet (objects 1 and 2 are the dungeon's monster).
    for obj in (1, 2):
        base = OBJECTS + obj * 8
        game.gam[base:base + 8] = bytes([0, 0, 0xff, 0xff, 0, 0xff, 0, 0])
    game.start_cell = (ladder % 8, ladder // 8)


def check_dungeon_map(game):
    path = game.saves / "AUTOMAP.DAT"
    check(path.is_file(), "the dungeon map wasn't written")
    data = path.read_bytes()
    level = data[(DESPISE - 0x21) * 64:(DESPISE - 0x21) * 64 + 8]
    x, y = game.start_cell
    check(level[y] & (1 << x), "the party's own cell isn't on the map")
    seen = sum(bin(row).count("1") for row in level)
    check(seen >= 3, f"with a torch lit only {seen} cells were seen")
    check(sum(bin(b).count("1") for b in data) == seen, "cells outside Despise level 1 were marked")


def setup_mage(game):
    # Mariah the mage (party record 3) joins, with an In Xen Mani mixed: a
    # 2nd-circle spell that needs no target and works anywhere.
    game.gam[PARTY_COUNT] = 4
    game.gam[MIXED_SPELLS + SPELL_IN_XEN_MANI] = 1
    game.mariah_mp = game.gam[MARIAH + 0x0f]


def check_mage_cost(game):
    gam = game.saved()
    check(game.saved_by_game(), "Quit didn't write SAVED.GAM")
    check(gam[MIXED_SPELLS + SPELL_IN_XEN_MANI] == 0, "In Xen Mani wasn't cast")
    spent = game.mariah_mp - gam[MARIAH + 0x0f]
    check(spent == 1, f"expected Mariah to spend 1 MP on a 2nd-circle spell, she spent {spent}")


AMBIENCE_SURF, AMBIENCE_DRIPS = 1, 4  # AMBIENCE_* in src/audio/audio.h


def setup_coast(game):
    # On the grass at the sea's edge east of Britain.
    x, y = 94, 106
    game.gam[MAP_ID] = 0
    game.gam[MAP_LEVEL] = 0
    game.gam[MAP_X] = x
    game.gam[MAP_Y] = y
    # The 32x32 block of map the game keeps around the party (D_589b/c).
    game.gam[CHUNK_X] = ((x >> 4) - 1) * 16 & 0xff
    game.gam[CHUNK_Y] = ((y >> 4) - 1) * 16 & 0xff


def ambience_played(game):
    log = game.folder / "LOG.TXT"
    text = log.read_text(errors="replace") if log.is_file() else ""
    return [int(line.split("ambience:")[1]) for line in text.splitlines() if "ambience:" in line]


def check_surf(game):
    played = ambience_played(game)
    check(AMBIENCE_SURF in played, f"expected surf by the sea, the ambience was {played}")


def check_drips(game):
    played = ambience_played(game)
    check(played[:1] == [AMBIENCE_DRIPS], f"expected dripping water in the dungeon, the ambience was {played}")


TESTS = [
    ("save_and_quit", "a new game starts and Q saves it",
     setup_none, START + " q y", check_saved),
    ("quicksave_quickload", "F9 brings back the F5 quicksave",
     setup_torches, START + " f5 i f9 y wait 3000 i q y", check_quickload),
    ("save_slots", "F6 saves to slot 3 and F7 loads it",
     setup_torches, START + " f6 3 enter i f7 enter wait 3000 q y", check_slots),
    ("recipe_helper", "mixing ticks Mani's recipe and makes 3",
     setup_reagents, START + " m m enter m 3 enter q y", check_mixed),
    ("dungeon_map", "the dungeon map records what the torch shows",
     setup_dungeon, START + " f10 wait 500 esc", check_dungeon_map),
    ("class_perks", "a mage pays 1 MP less for a 2nd-circle spell",
     setup_mage, START + " c down down down enter i x m enter wait 1500 q y", check_mage_cost),
    ("ambience_coast", "the sea's edge plays surf",
     setup_coast, START + " wait 2000", check_surf),
    ("ambience_dungeon", "a dungeon plays dripping water",
     setup_dungeon, START + " wait 2000", check_drips),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("filter", nargs="?", default="", help="run tests whose name contains this")
    parser.add_argument("--data", type=Path, help="your Ultima V folder")
    parser.add_argument("--exe", type=Path, default=default_exe(), help="the program to test")
    parser.add_argument("--timeout", type=int, default=120, help="seconds before a test is stopped")
    parser.add_argument("--keep", action="store_true", help="keep each test's temporary folder")
    parser.add_argument("--list", action="store_true", help="list the tests")
    args = parser.parse_args()

    tests = [t for t in TESTS if args.filter in t[0]]
    if args.list:
        for name, description, *_ in tests:
            print(f"{name:22} {description}")
        return 0

    data = args.data or remembered_data_folder()
    if not data or not find_file(data, "INIT.GAM"):
        print("Couldn't find your Ultima V folder; pass --data <folder>.", file=sys.stderr)
        return 2
    if not args.exe.is_file():
        print(f"No program at {args.exe}; build it first or pass --exe.", file=sys.stderr)
        return 2

    failures = 0
    for name, description, setup, script, verify in tests:
        folder = Path(tempfile.mkdtemp(prefix=f"u5test-{name}-"))
        result = None
        try:
            game = Game(data, folder)
            setup(game)
            game.write_save()
            result = game.play(args.exe, script, args.timeout)
            check(result.returncode == 0, f"the game exited with status {result.returncode}")
            verify(game)
            print(f"PASS  {name}: {description}")
        except subprocess.TimeoutExpired:
            failures += 1
            print(f"FAIL  {name}: stopped after {args.timeout}s without finishing the script")
        except TestFailed as error:
            failures += 1
            print(f"FAIL  {name}: {error}")
            if result is not None and result.stderr.strip():
                print("      " + result.stderr.strip().replace("\n", "\n      "))
        finally:
            if args.keep:
                print(f"      kept {folder}")
            else:
                shutil.rmtree(folder, ignore_errors=True)

    print(f"\n{len(tests) - failures} of {len(tests)} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
