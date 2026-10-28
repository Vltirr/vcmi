/*
 * AttackPossibility.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "AttackPossibility.h"
#include "../../lib/CStack.h" // TODO: remove
                              // Eventually only IBattleInfoCallback and battle::Unit should be used, 
                              // CUnitState should be private and CStack should be removed completely
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/ObstacleCasterProxy.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CombatValue.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/bonuses/BonusCustomTypes.h"

#include "../../lib/GameLibrary.h"

#include <vcmi/spells/Service.h>
#include <vcmi/spells/Spell.h>


uint64_t averageDmg(const DamageRange & range)
{
	return (range.min + range.max) / 2;
}

void DamageCache::cacheDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb)
{
	auto damage = averageDmg(hb->battleEstimateDamage(attacker, defender, 0).damage);

	damageCache[attacker->unitId()][defender->unitId()] = static_cast<float>(damage) / attacker->getCount();
}

void DamageCache::buildObstacleDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side)
{
	for(const auto & obst : hb->battleGetAllObstacles(side))
	{
		auto spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obst.get());

		if(!spellObstacle || !obst->triggersEffects())
			continue;

		auto triggerAbility = LIBRARY->spells()->getById(obst->getTrigger());
		auto triggerIsNegative = triggerAbility->isNegative() || triggerAbility->isDamage();

		if(!triggerIsNegative)
			continue;

		std::unique_ptr<spells::BattleCast> cast = nullptr;
		std::unique_ptr<spells::ObstacleCasterProxy> caster = nullptr;
		if(spellObstacle->obstacleType == SpellCreatedObstacle::EObstacleType::SPELL_CREATED)
		{
			const auto perspective = hb->battleGetMySide();
			const auto * hero = (perspective == BattleSide::ALL_KNOWING || perspective == spellObstacle->casterSide)
				? hb->battleGetFightingHero(spellObstacle->casterSide) : nullptr;
			caster = std::make_unique<spells::ObstacleCasterProxy>(hb->getSidePlayer(spellObstacle->casterSide), hero, *spellObstacle);
			cast = std::make_unique<spells::BattleCast>(spells::BattleCast(hb.get(), caster.get(), spells::Mode::PASSIVE, obst->getTrigger().toSpell()));
		}

		auto affectedHexes = obst->getAffectedTiles();
		auto stacks = hb->battleGetUnitsIf([](const battle::Unit * u) -> bool {
			return u->alive() && !u->isTurret() && u->getPosition().isValid();
		});

		auto inner = std::make_shared<HypotheticBattle>(hb->env, hb);

		for(auto stack : stacks)
		{
			auto updated = inner->getForUpdate(stack->unitId());

			spells::Target target;
			target.push_back(spells::Destination(updated.get()));

			if(cast)
				cast->castEval(inner->getServerCallback(), target);

			auto damageDealt = stack->getAvailableHealth() - updated->getAvailableHealth();

			for(const auto & hex : affectedHexes)
			{
				obstacleDamage[hex][stack->unitId()] = damageDealt;
			}
		}
	}
}

void DamageCache::buildDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side)
{
	if(parent == nullptr)
	{
		buildObstacleDamageCache(hb, side);
	}

	// a nested cache describes the same battle, so it keeps the data its parent was built against
	if(parent)
		facing = parent->facing;
	else
		for(auto known : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			facing.at(known) = CombatValueContext::against(*hb, known);

	auto stacks = hb->battleGetUnitsIf([=](const battle::Unit * u) -> bool
		{
			return u->isValidTarget();
		});

	// only the root cache holds them, so that a nested one scores its changes against the original state
	if(parent == nullptr)
		for(auto stack : stacks)
			unitValues[stack->unitId()] = LIBRARY->creh->getCombatValue().getAIValue(stack, facing.at(stack->unitSide()));

	battle::Units ourUnits;
	battle::Units enemyUnits;

	for(auto stack : stacks)
	{
		if(stack->unitSide() == side)
			ourUnits.push_back(stack);
		else
			enemyUnits.push_back(stack);
	}

	for(auto ourUnit : ourUnits)
	{
		if(!ourUnit->alive())
			continue;

		for(auto enemyUnit : enemyUnits)
		{
			if(enemyUnit->alive())
			{
				cacheDamage(ourUnit, enemyUnit, hb);
				cacheDamage(enemyUnit, ourUnit, hb);
			}
		}
	}
}

int64_t DamageCache::getDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb)
{
	bool wasComputedBefore = damageCache[attacker->unitId()].count(defender->unitId());

	if (!wasComputedBefore)
		cacheDamage(attacker, defender, hb);

	return damageCache[attacker->unitId()][defender->unitId()] * attacker->getCount();
}

int64_t DamageCache::getObstacleDamage(const BattleHex & hex, const battle::Unit * defender)
{
	if(parent)
		return parent->getObstacleDamage(hex, defender);

	auto damages = obstacleDamage.find(hex);

	if(damages == obstacleDamage.end())
		return 0;

	auto damage = damages->second.find(defender->unitId());

	return damage == damages->second.end()
		? 0
		: damage->second;
}

int64_t DamageCache::getOriginalDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb)
{
	if(parent)
	{
		auto attackerDamageMap = parent->damageCache.find(attacker->unitId());

		if(attackerDamageMap != parent->damageCache.end())
		{
			auto targetDamage = attackerDamageMap->second.find(defender->unitId());

			if(targetDamage != attackerDamageMap->second.end())
			{
				return static_cast<int64_t>(targetDamage->second * attacker->getCount());
			}
		}
	}

	return getDamage(attacker, defender, hb);
}

float DamageCache::getOriginalValue(const battle::Unit * unit) const
{
	if(parent)
		return parent->getOriginalValue(unit);

	auto value = unitValues.find(unit->unitId());

	if(value != unitValues.end())
		return value->second;

	// unit that did not exist when the cache was built, such as one summoned by a spell
	return LIBRARY->creh->getCombatValue().getAIValue(unit, facing.at(unit->unitSide()));
}

AttackPossibility::AttackPossibility(const BattleHex & from, const BattleHex & dest, const BattleAttackInfo & attack)
	: from(from), dest(dest), attack(attack)
{
	this->attack.attackerPos = from;
	this->attack.defenderPos = dest;
}

float AttackPossibility::damageDiff() const
{
	return defenderDamageReduce - attackerDamageReduce - collateralDamageReduce + shootersBlockedDmg;
}

float AttackPossibility::damageDiff(float positiveEffectMultiplier, float negativeEffectMultiplier) const
{
	return positiveEffectMultiplier * (defenderDamageReduce + shootersBlockedDmg)
		- negativeEffectMultiplier * (attackerDamageReduce + collateralDamageReduce);
}

float AttackPossibility::attackValue() const
{
	return damageDiff();
}

float hpFunction(uint64_t unitHealthStart, uint64_t unitHealthEnd, uint64_t maxHealth)
{
	float ratioStart = static_cast<float>(unitHealthStart) / maxHealth;
	float ratioEnd = static_cast<float>(unitHealthEnd) / maxHealth;
	float base = 0.666666f;

	// reduce from max to 0 must be 1. 
	// 10 hp from end costs bit more than 10 hp from start because our goal is to kill unit, not just hurt it
	// ********** 2 * base - ratioStart *********
	// *                                                              *
	// *        height = ratioStart - ratioEnd         *
	// *                                                                  *
	// ******************** 2 * base - ratioEnd ******
	// S = (a + b) * h / 2
	return (base * (4 - ratioStart - ratioEnd)) * (ratioStart - ratioEnd) / 2 ;
}

/// <summary>
/// How enemy damage will be reduced by this attack
/// Half bounty for kill, half for making damage equal to enemy health
/// Bounty - the killed creature average damage calculated against attacker
/// </summary>
float AttackPossibility::calculateDamageReduce(
	const battle::Unit * defender,
	uint64_t damageDealt,
	const DamageCache & damageCache)
{
	const float HEALTH_BOUNTY = 0.5;
	const float KILL_BOUNTY = 0.5;

	auto maxHealth = defender->getMaxHealth();
	auto availableHealth = defender->getFirstHPleft() + ((defender->getCount() - 1) * maxHealth);

	vstd::amin(damageDealt, availableHealth);

	auto enemiesKilled = damageDealt / maxHealth + (damageDealt % maxHealth >= defender->getFirstHPleft() ? 1 : 0);
	auto damagePerEnemy = damageCache.getOriginalValue(defender);
	auto exceedingDamage = (damageDealt % maxHealth);
	float hpValue = (damageDealt / maxHealth);
	
	if(defender->getFirstHPleft() >= exceedingDamage)
	{
		hpValue += hpFunction(defender->getFirstHPleft(), defender->getFirstHPleft() - exceedingDamage, maxHealth);
	}
	else
	{
		hpValue += hpFunction(defender->getFirstHPleft(), 0, maxHealth);
		hpValue += hpFunction(maxHealth, maxHealth + defender->getFirstHPleft() - exceedingDamage, maxHealth);
	}

	return damagePerEnemy * (enemiesKilled * KILL_BOUNTY + hpValue * HEALTH_BOUNTY);
}

int64_t AttackPossibility::evaluateBlockedShootersDmg(
	const BattleAttackInfo & attackInfo,
	BattleHex hex,
	DamageCache & damageCache,
	std::shared_ptr<CBattleInfoCallback> state)
{
	int64_t res = 0;

	if(attackInfo.shooting)
		return 0;

	std::set<uint32_t> checkedUnits;

	auto attacker = attackInfo.attacker;
	const auto & hexes = attacker->getSurroundingHexes(hex);
	for(const BattleHex & tile : hexes)
	{
		auto st = state->battleGetUnitByPos(tile, true);
		if(!st || !state->battleMatchOwner(st, attacker))
			continue;
		if(vstd::contains(checkedUnits, st->unitId()))
			continue;
		if(!state->battleCanShoot(st))
			continue;

		checkedUnits.insert(st->unitId());

		// FIXME: provide distance info for Jousting bonus
		BattleAttackInfo rangeAttackInfo(st, attacker, 0, true);
		rangeAttackInfo.defenderPos = hex;

		BattleAttackInfo meleeAttackInfo(st, attacker, 0, false);
		meleeAttackInfo.defenderPos = hex;

		auto rangeDmg = state->battleEstimateDamage(rangeAttackInfo);
		auto meleeDmg = state->battleEstimateDamage(meleeAttackInfo);
		// blocking a shooter denies a fraction of its combat value, scored on the same scale as a kill
		const int64_t shooterValue = damageCache.getOriginalValue(st) * CombatValue::stackScale(*st);
		const int64_t rangeDamage = averageDmg(rangeDmg.damage);
		const int64_t gain = rangeDamage - static_cast<int64_t>(averageDmg(meleeDmg.damage)) + 1;

		res += gain * shooterValue / std::max<int64_t>(1, rangeDamage);
	}

	return res;
}

AttackPossibility AttackPossibility::evaluate(
	const BattleAttackInfo & attackInfo,
	BattleHex hex,
	DamageCache & damageCache,
	std::shared_ptr<CBattleInfoCallback> state)
{
	auto attacker = attackInfo.attacker;
	auto defender = attackInfo.defender;
	const std::string cachingStringBlocksRetaliation = "type_BLOCKS_RETALIATION";
	static const auto selectorBlocksRetaliation = Selector::type()(BonusType::BLOCKS_RETALIATION);
	const auto attackerSide = state->playerToSide(state->battleGetOwner(attacker));
	const bool counterAttacksBlocked = attacker->hasBonus(selectorBlocksRetaliation, cachingStringBlocksRetaliation);

	AttackPossibility bestAp(hex, BattleHex::INVALID, attackInfo);

	BattleHexArray defenderHex;
	if(attackInfo.shooting)
		defenderHex.insert(defender->getPosition());
	else
		defenderHex = state->meleeAttackHexes(attacker, defender, hex);

	for(const BattleHex & defHex : defenderHex)
	{
		if(defHex == hex) // should be impossible but check anyway
			continue;

		AttackPossibility ap(hex, defHex, attackInfo);
		ap.attackerState = attacker->acquireState();
		ap.shootersBlockedDmg = bestAp.shootersBlockedDmg;

		const int totalAttacks = ap.attackerState->getTotalAttacks(attackInfo.shooting);

		if(hex.isValid())
			ap.attackerState->setPosition(hex);

		battle::Units defenderUnits;
		battle::Units retaliatedUnits = {attacker};
		battle::Units affectedUnits;

		if (attackInfo.shooting)
			defenderUnits = state->getAttackedBattleUnits(attacker, defender, defHex, true, hex, defender->getPosition());
		else
		{
			defenderUnits = state->getAttackedBattleUnits(attacker, defender, defHex, false, hex, defender->getPosition());
			retaliatedUnits = state->getAttackedBattleUnits(defender, attacker, hex, false, defender->getPosition(), hex);

			if(!vstd::contains_if(retaliatedUnits, [attacker](const battle::Unit * u) -> bool { return u->unitId() == attacker->unitId(); }))
			{
				retaliatedUnits.push_back(attacker);
			}

		}

		if(!attackInfo.shooting || (attackInfo.mobileShooting && hex.isValid() && hex != attacker->getPosition()))
		{
			auto obstacleDamage = damageCache.getObstacleDamage(hex, attacker);
			if(obstacleDamage > 0)
			{
				ap.attackerDamageReduce += calculateDamageReduce(attacker, obstacleDamage, damageCache);

				ap.attackerState->damage(obstacleDamage);
			}
		}

		// The moving attacker no longer occupies the old hex included by an area attack.
		if(!attackInfo.shooting || attackInfo.mobileShooting)
		{
			vstd::erase_if(defenderUnits, [attacker](const battle::Unit * unit)
			{
				return unit->unitId() == attacker->unitId();
			});
		}

		// ensure the defender is also affected
		if(!vstd::contains_if(defenderUnits, [defender](const battle::Unit * u) -> bool { return u->unitId() == defender->unitId(); }))
		{
			defenderUnits.push_back(defender);
		}

		affectedUnits = defenderUnits;
		vstd::concatenate(affectedUnits, retaliatedUnits);

#if BATTLE_TRACE_LEVEL>=1
		logAi->trace("Attacked battle units count %d, %d->%d", affectedUnits.size(), hex, defHex);
#endif

		std::map<uint32_t, std::shared_ptr<battle::CUnitState>> defenderStates;

		for(auto u : affectedUnits)
		{
			if(u->unitId() == attacker->unitId())
				continue;

			auto defenderState = u->acquireState();

			ap.affectedUnits.push_back(defenderState);
			defenderStates[u->unitId()] = defenderState;
		}

		bool repeatRetaliation = false;
		const auto trivialRules = state->getBattle()->getTrivialRetaliationRules();
		const auto firstStrikeSelector = Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
			.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, attackInfo.shooting ? BonusCustomSubtype::damageTypeRanged : BonusCustomSubtype::damageTypeMelee));
		const bool strikesFirst = trivialRules.enabled && defender->hasBonus(firstStrikeSelector) && !defender->hasBonusOfType(BonusType::NOT_ACTIVE);
		const auto respondingDefender = defenderStates.at(defender->unitId());
		const auto canRespond = [&]()
		{
			if(!ap.attackerState->alive() || !respondingDefender->ableToRetaliate())
				return false;
			if(attackInfo.shooting)
				return trivialRules.enabled && respondingDefender->hasBonusOfType(BonusType::RANGED_RETALIATION)
					&& !ap.attackerState->hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
					&& state->battleCanShoot(respondingDefender.get(), ap.attackerState->getPosition());
			return !counterAttacksBlocked && !ap.attackerState->isInvincible()
				&& !state->isLongWeaponAttack(ap.attackerState.get(), respondingDefender.get());
		};
		const auto respond = [&](const TrivialRetaliation & trivial)
		{
			for(const auto * target : retaliatedUnits)
			{
				const auto targetState = target->unitId() == attacker->unitId()
					? ap.attackerState : defenderStates.at(target->unitId());
				if(!targetState->alive())
					continue;
				BattleAttackInfo response(respondingDefender.get(), targetState.get(), 0, attackInfo.shooting);
				auto estimate = state->calculateDmgRange(response);
				trivial.apply(estimate);
				auto damage = std::min<int64_t>(averageDmg(estimate.damage), targetState->getAvailableHealth());
				const auto reduction = calculateDamageReduce(target, damage, damageCache);
				if(target->unitId() == attacker->unitId())
					ap.attackerDamageReduce += reduction;
				else if(target->unitSide() == defender->unitSide())
					ap.defenderDamageReduce += reduction;
				else
					ap.collateralDamageReduce += reduction;
				targetState->damage(damage);
			}
			respondingDefender->afterAttack(attackInfo.shooting, !trivial.active());
			repeatRetaliation = trivial.active();
		};

		for(int i = 0; i < totalAttacks; i++)
		{
			if(!ap.attackerState->alive() || !respondingDefender->alive())
				break;
			const auto trivial = trivialRules.classify(*ap.attackerState, *respondingDefender);
			if(strikesFirst && (i == 0 || repeatRetaliation) && canRespond())
				respond(trivial);
			if(!ap.attackerState->alive())
				break;

			for(const auto * target : defenderUnits)
			{
				const auto targetState = defenderStates.at(target->unitId());
				if(!targetState->alive())
					continue;
				auto simulatedAttack = ap.attack;
				simulatedAttack.attacker = ap.attackerState.get();
				simulatedAttack.defender = targetState.get();
				const auto estimate = state->calculateDmgRange(simulatedAttack);
				auto damage = std::min<int64_t>(averageDmg(estimate.damage), targetState->getAvailableHealth());
				const auto reduction = calculateDamageReduce(target, damage, damageCache);
				if(state->battleMatchOwner(attacker, target))
					ap.defenderDamageReduce += reduction;
				if(attackerSide == target->unitSide())
					ap.collateralDamageReduce += reduction;
				targetState->damage(damage);
			}
			ap.attackerState->afterAttack(attackInfo.shooting, false);
			if(!strikesFirst && (i == 0 || repeatRetaliation) && canRespond())
				respond(trivial);
			ap.defenderDead = !respondingDefender->alive();
		}
#if BATTLE_TRACE_LEVEL>=2
		logAi->trace("BattleAI AP: %s -> %s at %d from %d, affects %d units: d:%lld a:%lld c:%lld s:%lld",
			attackInfo.attacker->unitType()->getJsonKey(),
			attackInfo.defender->unitType()->getJsonKey(),
			ap.dest.toInt(), ap.from.toInt(), (int)ap.affectedUnits.size(),
			ap.defenderDamageReduce, ap.attackerDamageReduce, ap.collateralDamageReduce, ap.shootersBlockedDmg);
#endif

		if(!bestAp.dest.isValid() || ap.attackValue() > bestAp.attackValue())
			bestAp = ap;
	}

	// check how much damage we gain from blocking enemy shooters on this hex
	bestAp.shootersBlockedDmg = evaluateBlockedShootersDmg(attackInfo, hex, damageCache, state);

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("BattleAI best AP: %s -> %s at %d from %d, affects %d units: d:%lld a:%lld c:%lld s:%lld",
		attackInfo.attacker->unitType()->getJsonKey(),
		attackInfo.defender->unitType()->getJsonKey(),
		bestAp.dest.toInt(), bestAp.from.toInt(), (int)bestAp.affectedUnits.size(),
		bestAp.defenderDamageReduce, bestAp.attackerDamageReduce, bestAp.collateralDamageReduce, bestAp.shootersBlockedDmg);
#endif

	//TODO other damage related to attack (eg. fire shield and other abilities)
	return bestAp;
}
