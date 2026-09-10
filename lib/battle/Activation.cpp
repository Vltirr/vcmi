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

std::shared_ptr<const Bonus> battle::getActivationSource(const CStack * stack, EActionType action, const SpellID & spell)
{
	if(!stack)
		return nullptr;

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

int32_t battle::getActivationFocusCost(const CStack * stack, EActionType action, const SpellID & spell)
{
	const auto source = getActivationSource(stack, action, spell);
	return source && source->activation ? source->activation->costs.focus : 0;
}

bool battle::isActivationAvailable(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell)
{
	const int32_t focusCost = getActivationFocusCost(stack, action, spell);
	if(focusCost == 0 || !battle.getBattle()->getFocusRules().enabled)
		return true;
	return battle.getFocusCharges(focusSide(battle, stack)) >= focusCost;
}
