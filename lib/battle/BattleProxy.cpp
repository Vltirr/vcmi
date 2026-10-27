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

TrivialRetaliationRules BattleProxy::getTrivialRetaliationRules() const
{
	return subject->getBattle()->getTrivialRetaliationRules();
}

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
	return getActivationUsage(side, ActivationStateKey{key, EActivationLimitScope::SIDE, -1, period});
}

int32_t BattleProxy::getActivationUsage(BattleSide side, const ActivationStateKey & key) const
{
	const auto index = std::make_pair(side, key);
	const auto it = activationUsage.find(index);
	if(it != activationUsage.end())
		return it->second;
	if(key.period == EActivationLimitPeriod::ROUND && activationUsageRoundReset)
		return 0;
	if(key.period == EActivationLimitPeriod::TURN && activationUsageTurnReset.contains(key.stack))
		return 0;
	return subject->getBattle()->getActivationUsage(side, key);
}

void BattleProxy::setActivationUsage(BattleSide side, const std::string & key, EActivationLimitPeriod period, int32_t uses)
{
	setActivationUsage(side, ActivationStateKey{key, EActivationLimitScope::SIDE, -1, period}, uses);
}

void BattleProxy::setActivationUsage(BattleSide side, const ActivationStateKey & key, int32_t uses)
{
	activationUsage[std::make_pair(side, key)] = std::max(0, uses);
}

int32_t BattleProxy::getActivationCooldownReadyRound(BattleSide side, const ActivationStateKey & key) const
{
	const auto index = std::make_pair(side, key);
	const auto it = activationCooldownReadyRound.find(index);
	return it == activationCooldownReadyRound.end() ? subject->getBattle()->getActivationCooldownReadyRound(side, key) : it->second;
}

void BattleProxy::setActivationCooldownReadyRound(BattleSide side, const ActivationStateKey & key, int32_t readyRound)
{
	activationCooldownReadyRound[std::make_pair(side, key)] = std::max(0, readyRound);
}

void BattleProxy::resetActivationUsageRound()
{
	std::erase_if(activationUsage, [](const auto & entry)
	{
		return entry.first.second.period == EActivationLimitPeriod::ROUND;
	});
	activationUsageRoundReset = true;
}

void BattleProxy::resetActivationUsageTurn(int32_t stack)
{
	std::erase_if(activationUsage, [stack](const auto & entry)
	{
		return entry.first.second.period == EActivationLimitPeriod::TURN && entry.first.second.stack == stack;
	});
	activationUsageTurnReset.insert(stack);
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
	//state getters are not subject to access checks, those are performed by callback methods
	return subject->getBattle()->getSideArmy(side);
}

const CGHeroInstance * BattleProxy::getSideHero(BattleSide side) const
{
	return subject->getBattle()->getSideHero(side);
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
