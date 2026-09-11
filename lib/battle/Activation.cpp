/*
 * Activation.cpp, part of VCMI engine
 */
#include "StdInc.h"
#include "Activation.h"

#include "../CStack.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusSelector.h"
#include "CBattleInfoCallback.h"
#include "Focus.h"
#include "IBattleState.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../spells/CSpell.h"
#include <vcmi/ServerCallback.h>

static bool isDeliberateAbilitySource(EActionType action, BonusType type)
{
	switch(action)
	{
		case EActionType::WALK_AND_ATTACK:
			return type == BonusType::SPELL_AFTER_ATTACK || type == BonusType::COMBAT_EVENT_TRIGGER;
		case EActionType::CREATURE_ABILITY:
			return type == BonusType::ENCHANTER || type == BonusType::MANA_DRAIN;
		default:
			return false;
	}
}

std::vector<std::shared_ptr<const Bonus>> battle::getDeliberateActivationSources(const CStack * stack, EActionType action)
{
	std::vector<std::shared_ptr<const Bonus>> result;
	if(!stack)
		return result;

	std::set<std::string> listed;
	for(const auto & bonus : *stack->getAllBonuses(Selector::all))
	{
		if(bonus->activation && !bonus->activation->id.empty() &&
			isDeliberateAbilitySource(action, bonus->type) && listed.insert(bonus->activation->id).second)
			result.push_back(bonus);
	}
	return result;
}

std::shared_ptr<const Bonus> battle::getActivationSource(const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability)
{
	if(!stack)
		return nullptr;

	if(!ability.empty())
	{
		for(const auto & bonus : getDeliberateActivationSources(stack, action))
			if(bonus->activation->id == ability)
				return bonus;
		return nullptr;
	}

	switch(action)
	{
		case EActionType::MONSTER_SPELL:
		{
			auto source = stack->getBonus(Selector::typeSubtype(BonusType::SPELLCASTER, BonusSubtypeID(spell)));
			return source ? source : stack->getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER));
		}
		case EActionType::STACK_HEAL:
			return stack->getFirstBonus(Selector::type()(BonusType::HEALER));
		case EActionType::CATAPULT:
			return stack->getFirstBonus(Selector::type()(BonusType::CATAPULT));
		case EActionType::WALK_AND_CAST:
			return stack->getBonus(Selector::typeSubtype(BonusType::ADJACENT_SPELLCASTER, BonusSubtypeID(spell)));
		default:
			return nullptr;
	}
}

int32_t battle::getActivationFocusCost(const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability)
{
	const auto source = getActivationSource(stack, action, spell, ability);
	return source && source->activation ? source->activation->costs.focus : 0;
}

bool battle::areActivationLimitsAvailable(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability)
{
	const auto source = getActivationSource(stack, action, spell, ability);
	if(!source || !source->activation)
		return true;

	const auto side = focusSide(battle, stack);
	for(const auto & limit : source->activation->limits)
	{
		if(limit.scope == EActivationLimitScope::SIDE &&
			battle.getBattle()->getActivationUsage(side, limit.key, limit.period) >= limit.max)
			return false;
	}
	return true;
}

bool battle::actionReturnsControl(const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability)
{
	const auto source = getActivationSource(stack, action, spell, ability);
	if(source && source->actionReturnsControl)
		return true;
	return action == EActionType::MONSTER_SPELL && spell.hasValue() && spell.toSpell()->canCastWithoutSkip();
}

bool battle::isActivationAvailable(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability)
{
	if(stack && stack->castSpellThisTurn && actionReturnsControl(stack, action, spell, ability))
		return false;
	const int32_t focusCost = getActivationFocusCost(stack, action, spell, ability);
	const bool focusAvailable = focusCost == 0 || !battle.getBattle()->getFocusRules().enabled ||
		battle.getFocusCharges(focusSide(battle, stack)) >= focusCost;
	return focusAvailable && areActivationLimitsAvailable(battle, stack, action, spell, ability);
}

void ServerCallback::changeActivationUsage(const IBattleInfoCallback & battle, BattleSide side, const std::string & key, EActivationLimitPeriod period, int32_t delta)
{
	if(!battle.getBattle() || key.empty() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return;
	BattleActivationUsageChanged pack;
	pack.battleID = battle.getBattle()->getBattleID();
	pack.side = side;
	pack.key = key;
	pack.period = period;
	const auto current = battle.getBattle()->getActivationUsage(side, key, period);
	pack.uses = static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(current) + delta, 0,
		std::numeric_limits<int32_t>::max()));
	if(pack.uses != battle.getBattle()->getActivationUsage(side, key, period))
		apply(pack);
}

void ServerCallback::apply(BattleActivationUsageChanged & pack)
{
	apply(static_cast<CPackForClient &>(pack));
}
