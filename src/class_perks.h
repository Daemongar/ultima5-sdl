#ifndef _CLASS_PERKS_H
#define _CLASS_PERKS_H

#include "common/common.h"
#include "common/options.h"
#include "macros.h"
#include "vars.h"

#if defined(ENABLE_CLASS_PERKS)
// Class perks (class_perks setting): fighters hit more often with close
// weapons, bards with ranged ones and pick locks better, and mages pay 1 MP
// less for spells above the first circle. The Ultima IV classes follow the
// nearest Ultima V class: Paladins and Tinkers fight, Rangers are bards and
// Druids mages. The Avatar and Shepherds have none.

enum
{
    CLASS_ROLE_NONE,
    CLASS_ROLE_FIGHTER,
    CLASS_ROLE_BARD,
    CLASS_ROLE_MAGE,
};

// How much a perk adds to a Dexterity roll (hits, Jimmy).
#define CLASS_PERK_DEX_BONUS 6

static inline int CLASS_Role(u8 cls)
{
    if (!g_options.classPerks)
        return CLASS_ROLE_NONE;

    switch (cls)
    {
    case CLASS_FIGHTER:
    case CLASS_PALADIN:
    case CLASS_TINKER:
        return CLASS_ROLE_FIGHTER;
    case CLASS_BARD:
    case CLASS_RANGER:
        return CLASS_ROLE_BARD;
    case CLASS_MAGE:
    case CLASS_DRUID:
        return CLASS_ROLE_MAGE;
    default:
        return CLASS_ROLE_NONE;
    }
}

// MP a spell of the given circle costs this class.
static inline int CLASS_SpellCost(u8 cls, int circle)
{
    return CLASS_Role(cls) == CLASS_ROLE_MAGE && circle > 1 ? circle - 1 : circle;
}

#define CLASS_LOCKPICK_DEX(member) \
    (D_55a8_party[member].dex + (CLASS_Role(D_55a8_party[member].cls) == CLASS_ROLE_BARD ? CLASS_PERK_DEX_BONUS : 0))
#define CLASS_SPELL_MP_COST(member, circle) CLASS_SpellCost(D_55a8_party[member].cls, (circle))
#else
#define CLASS_LOCKPICK_DEX(member) (D_55a8_party[member].dex)
#define CLASS_SPELL_MP_COST(member, circle) (circle)
#endif

#endif
