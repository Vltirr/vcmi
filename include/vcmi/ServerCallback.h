/*
 * ServerCallback.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include "scripting/ApiTags.h"

namespace vstd
{
	class RNG;
}

namespace battle
{
	class Unit;
	class CUnitState;
}

namespace spells
{
	class Spell;
}

class IBattleInfoCallback;
enum class BattleSide : int8_t;
enum class EActivationLimitPeriod : int8_t;
struct ActivationStateKey;
struct BattleFocusChanged;
struct BattleActivationUsageChanged;
class CBattleInfoCallback;

struct CPackForClient;
struct BattleLogMessage;
struct BattleStackMoved;
struct BattleUnitsChanged;
struct SetStackEffect;
struct StacksInjured;
struct BattleObstaclesChanged;
struct CatapultAttack;

class DLL_LINKAGE ServerCallback : public scripting::ApiRawPointer<ServerCallback>
{
public:
	virtual ~ServerCallback() = default;

	virtual void complain(const std::string & problem) = 0;
	virtual bool describeChanges() const = 0;

	virtual vstd::RNG * getRNG() = 0;

	/// Rolls a chance-based combat ability of the given unit
	virtual bool rollCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor, int percentageChance) = 0;

	void changeFocusPoints(const IBattleInfoCallback & battle, BattleSide side, int32_t delta);
	void changeFocusCharges(const IBattleInfoCallback & battle, BattleSide side, int32_t delta);
	void changeActivationUsage(const IBattleInfoCallback & battle, BattleSide side, const std::string & key, EActivationLimitPeriod period, int32_t delta);
	void setActivationUsage(const IBattleInfoCallback & battle, BattleSide side, const ActivationStateKey & key, int32_t uses);
	void setActivationCooldownReadyRound(const IBattleInfoCallback & battle, BattleSide side, const ActivationStateKey & key, int32_t readyRound);
	virtual void apply(BattleFocusChanged & pack);
	virtual void apply(BattleActivationUsageChanged & pack);
	virtual void apply(CPackForClient & pack) = 0;

	virtual void apply(BattleLogMessage & pack) = 0;
	virtual void apply(BattleStackMoved & pack) = 0;
	virtual void apply(BattleUnitsChanged & pack) = 0;
	virtual void apply(SetStackEffect & pack) = 0;
	virtual void apply(StacksInjured & pack) = 0;
	virtual void apply(BattleObstaclesChanged & pack) = 0;
	virtual void apply(CatapultAttack & pack) = 0;

	/// Reports a completed deliberate spell cast and the pre-cast state of affected units
	virtual void spellHasHit(const CBattleInfoCallback & battle, const spells::Spell & spell, const battle::Unit * casterUnit, const std::vector<std::shared_ptr<const battle::CUnitState>> & unitsBefore) {}
};
