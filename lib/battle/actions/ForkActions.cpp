/*
 * ForkActions.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "ForkActions.h"

#include "IBattleActionEnvironment.h"
#include "../Activation.h"
#include "../BattleAction.h"
#include "../CBattleInfoCallback.h"
#include "../../CStack.h"
#include "../../bonuses/BonusParameters.h"
#include "../../gameState/InfoAboutArmy.h"
#include "../../spells/ISpellMechanics.h"
#include "../../spells/CSpell.h"
#include "../../spells/Problem.h"

bool CreatureAbilityAction::isUnitAction() const
{
	return true;
}

void CreatureAbilityAction::collectOptions(const CBattleInfoCallback & battle, const battle::Unit & actor, std::vector<ActionOption> & out) const
{
	if(battle.battleTacticDist())
		return;
	const auto * stack = battle.battleGetStackByID(actor.unitId());
	for(const auto & bonus : battle::getDeliberateActivationSources(stack, EActionType::CREATURE_ABILITY))
	{
		if(bonus->type == BonusType::ENCHANTER && !bonus->activation->cooldown && battle.battleGetEnchanterCounter(actor.controllingSide()) != 0)
			continue;
		if(bonus->type == BonusType::MANA_DRAIN)
		{
			const auto hero = battle.battleGetHeroInfo(battle.otherSide(actor.controllingSide()));
			if(!hero.details || hero.details->mana == 0)
				continue;
		}
		std::string name = bonus->activation->id;
		std::ranges::replace(name, ':', '.');
		out.push_back({this, UnitActionButton{6, SpellID::NONE, ImagePath::builtin("battle/actionAttack"), name + ".name"}, SpellID::NONE, bonus->activation->id});
	}
}

int CreatureAbilityAction::getPriority(const CBattleInfoCallback & battle, const ActionOption & option, const battle::Unit & actor, const battle::Unit * target) const
{
	return 100;
}

std::optional<BattleAction> CreatureAbilityAction::build(const CBattleInfoCallback & battle, const ActionOption & option, const ActionContext & context) const
{
	if(context.hoveredHex.isValid() || !context.actor)
		return std::nullopt;
	return BattleAction::makeCreatureAbility(context.actor, option.activatedAbility);
}

bool CreatureAbilityAction::validate(const IGameInfoCallback & game, const CBattleInfoCallback & battle, const BattleAction & action, spells::Problem & problem) const
{
	const auto * stack = battle.battleGetStackByID(action.stackNumber);
	if(!checkUnitCanAct(battle, stack, problem) || action.activatedAbility.empty())
		return false;
	std::vector<ActionOption> options;
	collectOptions(battle, *stack, options);
	if(std::ranges::none_of(options, [&](const ActionOption & option){ return option.activatedAbility == action.activatedAbility; }))
		return false;
	const auto source = battle::getActivationSource(stack, action.actionType, action.spell, action.activatedAbility);
	if(source->type == BonusType::ENCHANTER)
	{
		const auto bonuses = stack->getBonusesOfType(BonusType::ENCHANTER);
		for(const auto & bonus : *bonuses)
		{
			if(!bonus->activation || bonus->activation->id != action.activatedAbility || !bonus->subtype.as<SpellID>().hasValue() || enchanterParameters(*bonus).weight <= 0)
				continue;
			spells::ActivationCaster caster(stack, false);
			spells::BattleCast cast(&battle, &caster, spells::Mode::ENCHANTER, bonus->subtype.as<SpellID>().toSpell());
			cast.setSpellLevel(bonus->val);
			if(cast.getSpell()->battleMechanics(&cast)->canBeCastAt({cast.forceMassive ? spells::Destination() : spells::Destination(stack)}))
				return true;
		}
		return false;
	}
	return true;
}

ActionPreview CreatureAbilityAction::preview(const IGameInfoCallback & game, const CBattleInfoCallback & battle, const ActionOption & option, const ActionContext & context) const
{
	return {"combatBlocked", {}, {}, {}};
}

void CreatureAbilityAction::apply(IBattleActionEnvironment & env, const CBattleInfoCallback & battle, const BattleAction & action) const
{
	env.activateCreatureAbility(getActor(battle, action), action.activatedAbility);
}

bool WalkAndShootAction::isUnitAction() const
{
	return true;
}

static bool validateMobileAreaShot(const CBattleInfoCallback & battle, const battle::Unit & actor, const BattleHex & hex, spells::Problem & problem)
{
	const auto * target = battle.battleGetUnitByPos(hex, true);
	if(!battle.battleCanTargetEmptyHex(&actor) || (target && !target->isInvincible()))
		return true;
	const auto ability = actor.getBonus(Selector::type()(BonusType::SPELL_LIKE_ATTACK));
	const auto * spell = ability->subtype.as<SpellID>().toSpell();
	spells::BattleCast cast(&battle, &actor, spells::Mode::SPELL_LIKE_ATTACK, spell);
	return spell->battleMechanics(&cast)->canBeCastAt({battle::Destination(hex)}, problem);
}

bool WalkAndShootAction::validate(const IGameInfoCallback & game, const CBattleInfoCallback & battle, const BattleAction & action, spells::Problem & problem) const
{
	const auto * actor = battle.battleGetUnitByID(action.stackNumber);
	if(!checkUnitCanAct(battle, actor, problem))
		return false;
	const auto target = action.getTarget(&battle);
	if(target.size() != 2 || !battle.battleCanMoveAndShoot(actor, target[0].hexValue, target[1].hexValue))
	{
		problem.add(MetaString::createFromRawString("Invalid move and shoot action."));
		return false;
	}
	return validateMobileAreaShot(battle, *actor, target[1].hexValue, problem);
}

void WalkAndShootAction::apply(IBattleActionEnvironment & env, const CBattleInfoCallback & battle, const BattleAction & action) const
{
	const auto & actor = getActor(battle, action);
	const auto target = action.getTarget(&battle);
	const auto destination = battle.toWhichHexMove(&actor, target[0].hexValue);
	const auto move = env.moveUnit(actor, destination);
	if(!move.completed || !actor.alive() || actor.getPosition() != destination || actor.hasBonusOfType(BonusType::NOT_ACTIVE))
		return;
	if(!battle.battleCanShootAfterMoving(&actor, target[1].hexValue))
		return;
	spells::detail::ProblemImpl problem;
	if(!validateMobileAreaShot(battle, actor, target[1].hexValue, problem))
		return;
	env.rangedAttack(actor, target[1].hexValue, true);
}
