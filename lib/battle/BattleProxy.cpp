/*
 * BattleProxy.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleProxy.h"
#include "Activation.h"
#include "Unit.h"

///BattleProxy

FocusRules BattleProxy::getFocusRules() const
{
	return subject->getBattle()->getFocusRules();
}

int32_t BattleProxy::getFocusPoints(BattleSide side) const
{
	const auto it = focusPoints.find(side);
	return it == focusPoints.end() ? subject->getFocusPoints(side) : it->second;
}

void BattleProxy::setFocusPoints(BattleSide side, int32_t points)
{
	if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		focusPoints[side] = getFocusRules().clamp(points);
}

int32_t BattleProxy::getActivationUsage(BattleSide side, const std::string & key, EActivationLimitPeriod period) const
{
	const auto index = std::make_tuple(side, key, period);
	const auto it = activationUsage.find(index);
	if(it != activationUsage.end())
		return it->second;
	if(period == EActivationLimitPeriod::ROUND && activationUsageRoundReset)
		return 0;
	return subject->getBattle()->getActivationUsage(side, key, period);
}

void BattleProxy::setActivationUsage(BattleSide side, const std::string & key, EActivationLimitPeriod period, int32_t uses)
{
	activationUsage[std::make_tuple(side, key, period)] = std::max(0, uses);
}

void BattleProxy::resetActivationUsageRound()
{
	std::erase_if(activationUsage, [](const auto & entry)
	{
		return std::get<2>(entry.first) == EActivationLimitPeriod::ROUND;
	});
	activationUsageRoundReset = true;
}

BattleProxy::BattleProxy(Subject subject_): 
	subject(std::move(subject_))
{}

BattleProxy::~BattleProxy() = default;

const IBattleInfo * BattleProxy::getBattle() const
{
	return this;
}

std::optional<PlayerColor> BattleProxy::getPlayerID() const
{
	return subject->getPlayerID();
}

int32_t BattleProxy::getActiveStackID() const
{
	const auto * ret = subject->battleActiveUnit();
	if(ret)
		return ret->unitId();
	else
		return -1;
}

TStacks BattleProxy::getStacksIf(const TStackFilter & predicate) const
{
	return subject->battleGetStacksIf(predicate);
}

battle::Units BattleProxy::getUnitsIf(const battle::UnitFilter & predicate) const
{
	return subject->battleGetUnitsIf(predicate);
}

BattleField BattleProxy::getBattlefieldType() const
{
	return subject->battleGetBattlefieldType();
}

TerrainId BattleProxy::getTerrainType() const
{
	return subject->battleTerrainType();
}

IBattleInfo::ObstacleCList BattleProxy::getAllObstacles() const
{
	return subject->battleGetAllObstacles();
}

PlayerColor BattleProxy::getSidePlayer(BattleSide side) const
{
	return subject->sideToPlayer(side);
}

const CArmedInstance * BattleProxy::getSideArmy(BattleSide side) const
{
	return subject->battleGetArmyObject(side);
}

const CGHeroInstance * BattleProxy::getSideHero(BattleSide side) const
{
	return subject->battleGetFightingHero(side);
}

ui8 BattleProxy::getTacticDist() const
{
	return subject->battleTacticDist();
}

BattleSide BattleProxy::getTacticsSide() const
{
	return subject->battleGetTacticsSide();
}

int32_t BattleProxy::getRound() const
{
	return subject->battleGetRound();
}

const CGTownInstance * BattleProxy::getDefendedTown() const
{
	return subject->battleGetDefendedTown();
}

EWallState BattleProxy::getWallState(EWallPart partOfWall) const
{
	return subject->battleGetWallState(partOfWall);
}

EGateState BattleProxy::getGateState() const
{
	return subject->battleGetGateState();
}

int32_t BattleProxy::getCastSpells(BattleSide side) const
{
	return subject->battleCastSpells(side);
}

int32_t BattleProxy::getEnchanterCounter(BattleSide side) const
{
	return subject->battleGetEnchanterCounter(side);
}

const IBonusBearer * BattleProxy::getBonusBearer() const
{
	return subject->getBonusBearer();
}

