/*
 * LibActionEntry.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "LibActionEntry.h"
#include "BattleActionsController.h"

#include "BattleFieldController.h"
#include "BattleInterface.h"

#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../../lib/CBonusTypeHandler.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/Activation.h"
#include "../../lib/bonuses/Bonus.h"

#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/actions/BattleActionType.h"
#include "../../lib/callback/CCallback.h"

LibActionEntry::LibActionEntry(BattleInterface & owner, const CStack * actor, const ActionOption & option)
	: owner(owner)
	, actor(actor)
	, option(option)
{
}

ActionContext LibActionEntry::getContext(const BattleHex & hex) const
{
	ActionContext context{actor, hex, std::nullopt, owner.actionsController->getPartialTarget(), owner.getBattle()->battleGetMySide()};

	const auto approaches = getApproaches(hex);
	if(!approaches.empty())
		context.approach = owner.fieldController->selectApproach(hex, approaches);

	return context;
}

std::vector<ActionApproach> LibActionEntry::getApproaches(const BattleHex & hex) const
{
	if(!actor)
		return {};
	return option.type->getApproaches(*owner.getBattle(), option, *actor, hex);
}

int LibActionEntry::getPriority(const battle::Unit * target) const
{
	// an option of the hero is the only entry while it is offered
	if(!actor)
		return 0;

	return option.activatedAbility.empty() ? option.type->getPriority(*owner.getBattle(), option, *actor, target) : 100;
}

bool LibActionEntry::isLegal(const BattleHex & hex) const
{
	const ActionContext context = getContext(hex);
	if(actor && !battle::isActivationAvailable(*owner.getBattle(), actor, option.type->getID(), option.spell, option.activatedAbility))
		return false;
	return option.type->buildTargetStep(*owner.getBattle(), option, context).has_value()
		|| option.type->isLegal(*owner.curInt->cb, *owner.getBattle(), option, context);
}

ActionPreview LibActionEntry::preview(const BattleHex & hex) const
{
	if(actor && !battle::isActivationAvailable(*owner.getBattle(), actor, option.type->getID(), option.spell, option.activatedAbility))
		return {"combatBlocked", {}, {}, {}};
	return option.type->preview(*owner.curInt->cb, *owner.getBattle(), option, getContext(hex));
}

void LibActionEntry::realize(const BattleHex & hex) const
{
	const ActionContext context = getContext(hex);

	if(const auto step = option.type->buildTargetStep(*owner.getBattle(), option, context))
	{
		owner.actionsController->addTargetStep(*this, *step);
		return;
	}

	owner.actionsController->sendAction(option.type->build(*owner.getBattle(), option, context).value(), actor);
}

SpellID LibActionEntry::getSpell() const
{
	return option.spell;
}

bool LibActionEntry::needsNoTarget() const
{
	return (option.spell.hasValue() || !option.activatedAbility.empty()) && option.type->build(*owner.getBattle(), option, getContext(BattleHex::INVALID)).has_value();
}

std::optional<UnitActionButton> LibActionEntry::getPanelButton() const
{
	auto button = option.button;
	if(!actor || !button)
		return button;
	const auto resolution = battle::resolveActivation(*owner.getBattle(), actor, option.type->getID(), option.spell, option.activatedAbility);
	button->activatedAbility = option.activatedAbility;
	button->source = resolution.source;
	button->focusCost = resolution.focusCost;
	button->available = resolution.available();
	button->limitReached = !resolution.limitsAvailable;
	button->cooldownRemaining = resolution.cooldownRoundsRemaining;
	if(resolution.source && !option.activatedAbility.empty())
	{
		const auto source = std::const_pointer_cast<Bonus>(resolution.source);
		button->icon = LIBRARY->bth->bonusToGraphics(source);
		if(source->type == BonusType::SPELL_AFTER_ATTACK && source->subtype.as<SpellID>().hasValue())
			button->spell = source->subtype.as<SpellID>();
		if(source->type == BonusType::ENCHANTER && actor->unitType())
		{
			const auto & bonuses = actor->unitType()->getExportedBonusList();
			const auto iconBonus = std::ranges::find_if(bonuses, [](const auto & bonus)
			{
				return bonus->type == BonusType::NONE && bonus->description.toString(&GAME->translator()).starts_with("{.Enchanter.}");
			});
			if(iconBonus != bonuses.end())
				button->icon = LIBRARY->bth->bonusToGraphics(*iconBonus);
		}
		if(button->icon.empty())
			button->icon = ImagePath::builtin("battle/actionAttack");
	}
	return button;
}

const BattleActionType * LibActionEntry::getType() const
{
	return option.type;
}
