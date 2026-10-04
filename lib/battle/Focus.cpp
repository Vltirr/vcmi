/*
 * Focus.cpp, part of VCMI engine
 */
#include "StdInc.h"
#include "Focus.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "Unit.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <vcmi/ServerCallback.h>

BattleSide battle::focusSide(const CBattleInfoCallback & battle, const Unit * unit)
{
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	return side == BattleSide::NONE ? unit->unitSide() : side;
}

void ServerCallback::changeFocusPoints(const IBattleInfoCallback & battle, BattleSide side, int32_t delta)
{
	if(!battle.getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return;
	const auto rules = battle.getBattle()->getFocusRules();
	if(!rules.enabled)
		return;
	BattleFocusChanged pack;
	pack.battleID = battle.getBattle()->getBattleID();
	pack.side = side;
	pack.points = rules.clamp(static_cast<int64_t>(battle.getFocusPoints(side)) + delta);
	if(pack.points != battle.getFocusPoints(side))
		apply(pack);
}

void ServerCallback::changeFocusCharges(const IBattleInfoCallback & battle, BattleSide side, int32_t delta)
{
	if(!battle.getBattle())
		return;
	const auto points = static_cast<int64_t>(delta) * battle.getBattle()->getFocusRules().pointsPerCharge;
	changeFocusPoints(battle, side, static_cast<int32_t>(std::clamp<int64_t>(points,
		std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max())));
}

void ServerCallback::apply(BattleFocusChanged & pack)
{
	apply(static_cast<CPackForClient &>(pack));
}
