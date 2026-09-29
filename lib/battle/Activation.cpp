/*
 * Activation.cpp, part of VCMI engine
 */
#include "StdInc.h"
#include "Activation.h"

#include "../CStack.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusSelector.h"
#include "../combatScripts/ICombatEventScript.h"
#include "../GameLibrary.h"
#include "../scripting/ScriptService.h"
#include "CBattleInfoCallback.h"
#include "Focus.h"
#include "IBattleState.h"
#include "Unit.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../spells/AbilityCaster.h"
#include "../spells/CSpell.h"
#include "../spells/ISpellMechanics.h"
#include <vcmi/ServerCallback.h>
#include <vcmi/spells/Service.h>

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

static std::string nativeActivationIdentity(const Bonus * source, EActionType action, const SpellID & spell, const std::string & ability)
{
	if(!ability.empty())
		return ability;
	if(source && source->activation && !source->activation->id.empty())
		return source->activation->id;
	const auto spellKey = [&]()
	{
		return spell.hasValue() ? spell.toSpell()->getJsonKey() : std::string();
	};
	switch(action)
	{
		case EActionType::MONSTER_SPELL:
			return source && source->type == BonusType::RANDOM_SPELLCASTER ? "randomSpellcaster" : "monsterSpell:" + spellKey();
		case EActionType::WALK_AND_CAST: return "walkAndCast:" + spellKey();
		case EActionType::STACK_HEAL:
			return source && source->subtype.as<SpellID>().hasValue() ? "heal:" + source->subtype.as<SpellID>().toSpell()->getJsonKey() : "heal";
		case EActionType::CATAPULT: return "catapult";
		default: return {};
	}
}

static ActivationStateKey activationStateKey(const std::string & key, EActivationLimitScope scope, EActivationLimitPeriod period, const CStack * stack)
{
	return {key, scope, scope == EActivationLimitScope::STACK && stack ? static_cast<int32_t>(stack->unitId()) : -1, period};
}

battle::ActivationResolution battle::resolveActivation(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability)
{
	ActivationResolution result;
	result.source = getActivationSource(stack, action, spell, ability);
	if(!result.source || !result.source->activation)
	{
		result.identity = nativeActivationIdentity(result.source.get(), action, spell, ability);
		result.returnsControl = action == EActionType::MONSTER_SPELL && spell.hasValue() && spell.toSpell()->canCastWithoutSkip();
		return result;
	}
	const auto & activation = *result.source->activation;
	result.identity = nativeActivationIdentity(result.source.get(), action, spell, ability);
	result.focusCost = activation.costs.focus;
	result.returnsControl = activation.turn ? activation.turn->returnsControl : result.source->actionReturnsControl;
	const auto side = focusSide(battle, stack);
	result.focusAvailable = result.focusCost == 0 || (battle.getBattle()->getFocusRules().enabled && battle.getFocusCharges(side) >= result.focusCost);
	for(const auto & limit : activation.limits)
	{
		const auto key = limit.key.empty() ? result.identity : limit.key;
		const auto stateKey = activationStateKey(key, limit.scope, limit.period, stack);
		result.limits.push_back(stateKey);
		if(battle.getBattle()->getActivationUsage(side, stateKey) >= limit.max)
			result.limitsAvailable = false;
	}
	if(activation.cooldown)
	{
		const auto & cooldown = *activation.cooldown;
		EActivationLimitScope scope = EActivationLimitScope::STACK;
		if(cooldown.scope)
			scope = *cooldown.scope;
		else if(!activation.limits.empty())
		{
			scope = activation.limits.front().scope;
			for(const auto & limit : activation.limits)
				if(limit.scope != scope)
					return result; // malformed mixed scope is rejected by loader
		}
		const auto key = cooldown.key.empty() ? result.identity : cooldown.key;
		result.cooldown = activationStateKey(key, scope, EActivationLimitPeriod::BATTLE, stack);
		const int32_t readyRound = std::max(battle.getBattle()->getActivationCooldownReadyRound(side, *result.cooldown), cooldown.initial + 1);
		result.cooldownRoundsRemaining = std::max(0, readyRound - battle.getBattle()->getRound());
		result.cooldownAvailable = result.cooldownRoundsRemaining == 0;
	}
	return result;
}

bool battle::areActivationLimitsAvailable(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability)
{
	return resolveActivation(battle, stack, action, spell, ability).limitsAvailable;
}

bool battle::actionReturnsControl(const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability)
{
	const auto source = getActivationSource(stack, action, spell, ability);
	if(source && source->activation && source->activation->turn)
		return source->activation->turn->returnsControl;
	if(source && source->actionReturnsControl)
		return true;
	return action == EActionType::MONSTER_SPELL && spell.hasValue() && spell.toSpell()->canCastWithoutSkip();
}

bool battle::isActivationTargetAvailable(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability, const Unit * target)
{
	const auto source = getActivationSource(stack, action, spell, ability);
	if(!source || !source->activation || action != EActionType::WALK_AND_ATTACK)
		return true;
	if(!target || !target->isValidTarget())
		return false;

	if(source->type == BonusType::SPELL_AFTER_ATTACK)
	{
		const SpellID effectSpell = source->subtype.as<SpellID>();
		if(!effectSpell.hasValue())
			return false;
		int32_t spellLevel = 0;
		if(source->parameters)
		{
			const auto values = source->parameters->toVector();
			if(!values.empty())
				spellLevel = values[0];
		}
		spells::AbilityCaster caster(stack, spellLevel);
		spells::Target spellTarget;
		spellTarget.emplace_back(target);
		spells::BattleCast cast(&battle, &caster, spells::Mode::PASSIVE, effectSpell.toSpell());
		return effectSpell.toSpell()->battleMechanics(&cast)->canBeCastAt(spellTarget);
	}

	if(source->type == BonusType::COMBAT_EVENT_TRIGGER)
	{
		const ScriptID scriptID = source->subtype.as<ScriptID>();
		if(!scriptID.hasValue())
			return false;
		const auto & script = LIBRARY->scriptTypes()->getById(scriptID);
		if(!script.combatEventScript)
			return false;

		// A combat script with a spell of the same identity uses that spell's target condition.
		// This covers immunities declared in data without duplicating them in the script.
		if(const CSpell * effectSpell = dynamic_cast<const CSpell *>(LIBRARY->spells()->getByName(script.scriptId)))
		{
			spells::AbilityCaster caster(stack, 0);
			spells::Target spellTarget;
			spellTarget.emplace_back(target);
			spells::BattleCast cast(&battle, &caster, spells::Mode::PASSIVE, effectSpell);
			if(!effectSpell->battleMechanics(&cast)->canBeCastAt(spellTarget))
				return false;
		}

		static const JsonNode emptyParameters;
		const JsonNode & parameters = source->parameters ? source->parameters->toCustom<JsonNode>() : emptyParameters;
		return script.combatEventScript->isApplicable(battle, stack, target, parameters);
	}

	return true;
}

bool battle::isActivationAvailable(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability)
{
	return resolveActivation(battle, stack, action, spell, ability).available();
}

void ServerCallback::changeActivationUsage(const IBattleInfoCallback & battle, BattleSide side, const std::string & key, EActivationLimitPeriod period, int32_t delta)
{
	const ActivationStateKey stateKey{key, EActivationLimitScope::SIDE, -1, period};
	const auto current = battle.getBattle() ? battle.getBattle()->getActivationUsage(side, stateKey) : 0;
	setActivationUsage(battle, side, stateKey, static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(current) + delta, 0, std::numeric_limits<int32_t>::max())));
}

void ServerCallback::setActivationUsage(const IBattleInfoCallback & battle, BattleSide side, const ActivationStateKey & key, int32_t uses)
{
	if(!battle.getBattle() || key.key.empty() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return;
	BattleActivationUsageChanged pack;
	pack.battleID = battle.getBattle()->getBattleID();
	pack.side = side;
	pack.key = key.key;
	pack.scope = key.scope;
	pack.stack = key.stack;
	pack.period = key.period;
	pack.uses = std::max(0, uses);
	if(pack.uses != battle.getBattle()->getActivationUsage(side, key))
		apply(pack);
}

void ServerCallback::setActivationCooldownReadyRound(const IBattleInfoCallback & battle, BattleSide side, const ActivationStateKey & key, int32_t readyRound)
{
	if(!battle.getBattle() || key.key.empty() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return;
	BattleActivationCooldownChanged pack;
	pack.battleID = battle.getBattle()->getBattleID();
	pack.side = side;
	pack.key = key.key;
	pack.scope = key.scope;
	pack.stack = key.stack;
	pack.readyRound = std::max(0, readyRound);
	if(pack.readyRound != battle.getBattle()->getActivationCooldownReadyRound(side, key))
		apply(pack);
}

void ServerCallback::apply(BattleActivationUsageChanged & pack)
{
	apply(static_cast<CPackForClient &>(pack));
}
