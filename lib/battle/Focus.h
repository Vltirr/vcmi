/*
 * Focus.h, part of VCMI engine
 */
#pragma once

#include "BattleSide.h"

class CBattleInfoCallback;

namespace battle
{
class Unit;
DLL_LINKAGE BattleSide focusSide(const CBattleInfoCallback & battle, const Unit * unit);
}
