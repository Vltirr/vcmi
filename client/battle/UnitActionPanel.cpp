/*
 * UnitActionPanel.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "UnitActionPanel.h"

#include "BattleInterface.h"
#include "BattleActionsController.h"
#include "BattleStacksController.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "events/InputHandler.h"
#include "../gui/WindowHandler.h"
#include "../render/CAnimation.h"
#include "../render/IRenderHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../windows/CSpellWindow.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/CBonusTypeHandler.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../GameInstance.h"

namespace
{

std::string focusCostText(int32_t focusCost)
{
	MetaString text;
	text.appendTextID(focusCost == 1 ? "vcmi.battle.focus.cost.one" : "vcmi.battle.focus.cost.many");
	text.replaceNumber(focusCost);
	return text.toString(&GAME->translator());
}

std::string cooldownText(int32_t rounds)
{
	MetaString text;
	text.appendTextID(rounds == 1 ? "vcmi.battle.activation.cooldown.one" : "vcmi.battle.activation.cooldown.many");
	text.replaceNumber(rounds);
	return text.toString(&GAME->translator());
}

std::string activationName(const PossiblePlayerBattleAction & action, const std::shared_ptr<const Bonus> & source)
{
	if(source && source->type == BonusType::SPELL_AFTER_ATTACK)
	{
		const SpellID spell = source->subtype.as<SpellID>();
		if(spell.hasValue())
			return spell.toSpell()->getNameTranslated();
	}

	if(source && source->activation && !source->activation->id.empty())
	{
		std::string textID = source->activation->id;
		std::ranges::replace(textID, ':', '.');
		return LIBRARY->generaltexth->translate(textID + ".name");
	}

	return action.ability();
}

std::string withActivationHeading(const std::string & name, const std::string & description)
{
	if(description.empty())
		return name;

	std::string body = description;
	if(description.front() == '{')
	{
		const auto endOfHeading = description.find("}\n");
		if(endOfHeading != std::string::npos)
			body = description.substr(endOfHeading + 2);
	}

	return body.empty() ? name : name + " - " + body;
}

std::shared_ptr<CIntObject> spellIcon(const CSpell * spell)
{
	if(!spell)
		return nullptr;

	const size_t frame = spell->getId().getNum() + 1;
	const AnimationPath spellIcons = AnimationPath::builtin("spellint");
	const auto animation = ENGINE->renderHandler().loadAnimation(spellIcons, EImageBlitMode::COLORKEY);
	if(animation->size() <= frame || !animation->getImage(frame, 0, false))
		return nullptr;

	return std::make_shared<CAnimImage>(spellIcons, frame);
}

std::shared_ptr<CIntObject> actionIcon(const PossiblePlayerBattleAction & action, const CStack * stack = nullptr)
{
	const auto & source = action.source();
	if(source)
	{
		if(source->type == BonusType::ENCHANTER && stack && stack->unitType())
		{
			const auto & creatureBonuses = stack->unitType()->getExportedBonusList();
			const auto abilityIcon = std::ranges::find_if(creatureBonuses, [](const auto & bonus)
			{
				return bonus->type == BonusType::NONE && bonus->description.toString(&GAME->translator()).starts_with("{.Enchanter.}");
			});
			if(abilityIcon != creatureBonuses.end())
			{
				const ImagePath icon = LIBRARY->bth->bonusToGraphics(*abilityIcon);
				if(!icon.empty())
					return std::make_shared<CPicture>(icon);
			}
		}
		const ImagePath bonusIcon = LIBRARY->bth->bonusToGraphics(std::const_pointer_cast<Bonus>(source));
		if(!bonusIcon.empty())
			return std::make_shared<CPicture>(bonusIcon);

		return std::make_shared<CPicture>(ImagePath::builtin("battle/actionAttack"));
	}

	if(action.spellcast())
		if(auto icon = spellIcon(action.spell().toSpell()))
			return icon;

	return std::make_shared<CPicture>(ImagePath::builtin("battle/actionAttack"));
}

std::shared_ptr<TransparentFilledRectangle> disabledActionOverlay(
	const std::shared_ptr<CToggleButton> & button,
	const std::shared_ptr<CIntObject> & icon)
{
	const Rect bounds = button->pos.include(icon->pos);
	auto result = std::make_shared<TransparentFilledRectangle>(Rect(Point(), bounds.dimensions()), ColorRGBA(0, 0, 0, 144));
	// Keep the shade as a panel child created after the button. Pictures and animation images draw
	// only themselves, so an overlay parented to the icon would never be rendered. Construction
	// captures widgets using parent-relative coordinates; restore the final absolute position here.
	result->moveTo(bounds.topLeft());
	result->setRedrawParent(true);
	return result;
}

}

UnitActionPanel::UnitActionPanel(BattleInterface & owner)
	: CIntObject(0)
	, owner(owner)
{
	OBJECT_CONSTRUCTION;

	addUsedEvents(LCLICK | SHOW_POPUP | MOVE);

	pos = Rect(0, 0, 52, 600);
	background = std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK"), pos);
	rect = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, pos.w + 1, pos.h + 1), ColorRGBA(0, 0, 0, 0), ColorRGBA(241, 216, 120, 255));
}

void UnitActionPanel::restoreAllActions()
{
	owner.actionsController->resetCurrentStackPossibleActions();
}

bool UnitActionPanel::cancelSelectedAction()
{
	bool actionWasSelected = false;
	for(auto & button : buttons)
	{
		if(button.widget->isSelected())
		{
			button.widget->setSelectedSilent(false);
			actionWasSelected = true;
		}
	}

	if(!actionWasSelected)
		return false;

	restoreAllActions();
	redraw();
	ENGINE->fakeMouseMove();
	return true;
}

void UnitActionPanel::setActions(int buttonIndex, const std::vector<PossiblePlayerBattleAction> & filteredActions)
{
	for (const auto & button : buttons)
		if (button.widget != buttons.at(buttonIndex).widget)
			button.widget->setSelectedSilent(false);

	owner.actionsController->setPriorityActions(filteredActions);
	if (filteredActions.front().spellcast())
		owner.actionsController->enterCreatureCastingMode();
	owner.actionsController->setPriorityActions(filteredActions);
}

bool UnitActionPanel::updateButton(ActionButton & button)
{
	const auto focusedAction = std::ranges::find_if(button.actions, [](const auto & action) { return action.focusCost() > 0; });
	const bool available = std::ranges::any_of(button.actions, [](const auto & action) { return action.isActivationAvailable(); });
	const bool limitReached = std::ranges::any_of(button.actions, [](const auto & action) { return action.hasReachedActivationLimit(); });
	const auto cooldownAction = std::ranges::max_element(button.actions, {}, [](const auto & action)
	{
		return action.hasActivationCooldown() ? action.activationCooldownRemaining() : 0;
	});
	const bool cooldownActive = cooldownAction != button.actions.end() && cooldownAction->hasActivationCooldown();
	std::string hoverText = button.baseHoverText;
	std::string helpText = button.baseHelpText;
	if(focusedAction != button.actions.end())
	{
		const std::string costText = focusCostText(focusedAction->focusCost());
		hoverText += "\n" + costText;
		helpText += "\n" + costText;
		if(!available && !limitReached && !cooldownActive)
		{
			hoverText += "\n" + LIBRARY->generaltexth->translate("vcmi.battle.focus.insufficient");
			helpText += "\n" + LIBRARY->generaltexth->translate("vcmi.battle.focus.insufficient");
		}
	}
	if(limitReached)
	{
		hoverText += "\n" + LIBRARY->generaltexth->translate("vcmi.battle.activation.limitReached");
		helpText += "\n" + LIBRARY->generaltexth->translate("vcmi.battle.activation.limitReached");
	}
	if(cooldownActive)
	{
		const std::string cooldown = cooldownText(cooldownAction->activationCooldownRemaining());
		hoverText += "\n" + cooldown;
		helpText += "\n" + cooldown;
	}

	const bool deselected = !available && button.widget->isSelected();
	button.widget->setHelp(CButton::tooltip(hoverText, helpText));
	button.widget->setHoverEnabledWhenBlocked(true);
	button.widget->block(!available);
	button.disabledOverlay->setEnabled(!available);
	if(deselected)
		button.widget->setSelectedSilent(false);
	return deselected;
}

void UnitActionPanel::testAndAddAction(const std::vector<PossiblePlayerBattleAction> & allActions, const std::vector<PossiblePlayerBattleAction::Actions> & actionFilter, const ImagePath & iconPath, const std::string & descriptionTextID)
{
	std::vector<PossiblePlayerBattleAction> filteredActions;

	for (const auto & action : allActions)
		if (vstd::contains(actionFilter, action.get()))
			filteredActions.push_back(action);

	if (filteredActions.empty())
		return;

	int index = buttons.size();

	const auto & callback = [this, filteredActions, index](bool isSelected){ if (isSelected) setActions(index, filteredActions); else restoreAllActions(); };

	MetaString tooltip;
	tooltip.appendTextID(descriptionTextID);

	auto button = std::make_shared<CToggleButton>(Point(2, 7 + 50 * index), AnimationPath::builtin("battleUnitAction"), CButton::tooltip(), callback);
	auto icon = std::make_shared<CPicture>(iconPath);
	button->setOverlay(icon);
	button->setHighlightedBorderColor(Colors::WHITE);
	button->setAllowDeselection(true);
	auto disabledOverlay = disabledActionOverlay(button, icon);
	buttons.push_back({button, filteredActions, tooltip.toString(&GAME->translator()), "", disabledOverlay});
	updateButton(buttons.back());
}

void UnitActionPanel::testAndAddSpell(const std::vector<PossiblePlayerBattleAction> & allActions, const SpellID & spellFilter)
{
	std::vector<PossiblePlayerBattleAction> filteredActions;

	for (const auto & action : allActions)
		if (action.spellcast() && action.spell() == spellFilter)
			filteredActions.push_back(action);

	if (filteredActions.empty())
		return;

	int index = buttons.size();
	const auto & callback = [this, filteredActions, index](bool isSelected){ if (isSelected) setActions(index, filteredActions); else restoreAllActions();};

	MetaString tooltip;
	tooltip.appendTextID("core.genrltxt.26");
	tooltip.replaceName(spellFilter);

	std::string description = spellFilter.toSpell()->getDescriptionTranslated(0);

	auto button = std::make_shared<CToggleButton>(Point(2, 7 + 50 * index), AnimationPath::builtin("battleUnitAction"), CButton::tooltip(), callback);
	auto icon = actionIcon(filteredActions.front());
	button->setOverlay(icon);
	button->setHighlightedBorderColor(Colors::WHITE);
	auto disabledOverlay = disabledActionOverlay(button, icon);
	buttons.push_back({button, filteredActions, tooltip.toString(&GAME->translator()), description, disabledOverlay});
	updateButton(buttons.back());
}

void UnitActionPanel::testAndAddAbility(const PossiblePlayerBattleAction & action)
{
	const int index = buttons.size();
	const std::vector<PossiblePlayerBattleAction> filteredActions{action};
	const auto callback = [this, filteredActions, index](bool isSelected)
	{
		if(isSelected)
			setActions(index, filteredActions);
		else
			restoreAllActions();
	};

	const auto & source = action.source();
	std::string description = source ? LIBRARY->bth->bonusToString(std::const_pointer_cast<Bonus>(source)) : action.ability();
	if(description.empty() && source)
		description = source->Description(GAME->interface()->cb.get());
	if(description.empty())
		description = action.ability();
	description = withActivationHeading(activationName(action, source), description);
	auto button = std::make_shared<CToggleButton>(Point(2, 7 + 50 * index), AnimationPath::builtin("battleUnitAction"), CButton::tooltip(), callback);
	auto icon = actionIcon(action, owner.stacksController->getActiveStack());
	button->setOverlay(icon);
	button->setAllowDeselection(true);
	auto disabledOverlay = disabledActionOverlay(button, icon);
	buttons.push_back({button, filteredActions, description, description, disabledOverlay});
	updateButton(buttons.back());
}

void UnitActionPanel::setPossibleActions(const std::vector<PossiblePlayerBattleAction> & newActions)
{
	OBJECT_CONSTRUCTION;

	buttons.clear();

	static const std::vector actionsMove = { PossiblePlayerBattleAction::MOVE_STACK };
	static const std::vector actionsInfo = { PossiblePlayerBattleAction::CREATURE_INFO, PossiblePlayerBattleAction::HERO_INFO };
	static const std::vector actionsShoot = { PossiblePlayerBattleAction::SHOOT };
	static const std::vector actionsGenie = { PossiblePlayerBattleAction::RANDOM_GENIE_SPELL };
	static const std::vector actionsAttack = { PossiblePlayerBattleAction::ATTACK, PossiblePlayerBattleAction::WALK_AND_ATTACK };
	static const std::vector actionsReturn = { PossiblePlayerBattleAction::ATTACK_AND_RETURN };
	static const std::vector actionsAttackLongWeapon = { PossiblePlayerBattleAction::LONG_WEAPON_ATTACK };

	testAndAddAction(newActions, actionsMove, ImagePath::builtin("battle/actionMove"), "vcmi.battle.action.move");
	testAndAddAction(newActions, actionsReturn, ImagePath::builtin("battle/actionReturn"), "vcmi.battle.action.return");
	testAndAddAction(newActions, actionsAttack, ImagePath::builtin("battle/actionAttack"), "vcmi.battle.action.attack");
	testAndAddAction(newActions, actionsShoot, ImagePath::builtin("battle/actionShoot"), "vcmi.battle.action.shoot");
	testAndAddAction(newActions, actionsGenie, ImagePath::builtin("battle/actionGenie"), "vcmi.battle.action.genie");
	testAndAddAction(newActions, actionsAttackLongWeapon, ImagePath::builtin("battle/actionLongWeapon"), "vcmi.battle.action.attackLongWeapon");

	std::vector<const PossiblePlayerBattleAction *> spells;

	for (const auto & action : newActions)
		if (action.spellcast())
			spells.push_back(&action);

	std::stable_sort(spells.begin(), spells.end(), [](const auto * lhs, const auto * rhs)
	{
		return lhs->focusCost() < rhs->focusCost();
	});

	for (const auto * spell : spells)
		testAndAddSpell(newActions, spell->spell());

	for(const auto & action : newActions)
		if(action.isAbility())
			testAndAddAbility(action);

	// Not really a unit action, so place it at the end
	testAndAddAction(newActions, actionsInfo, ImagePath::builtin("battle/actionInfo"), "vcmi.battle.action.info");

	redraw();
}

void UnitActionPanel::updateActivationAvailability(const std::vector<PossiblePlayerBattleAction> & actions)
{
	bool resetSelectedAction = false;
	for(auto & button : buttons)
	{
		for(auto & previousAction : button.actions)
		{
			auto updatedAction = std::ranges::find(actions, previousAction);
			if(updatedAction != actions.end())
				previousAction = *updatedAction;
		}

		resetSelectedAction |= updateButton(button);
	}

	if(resetSelectedAction)
		owner.actionsController->resetCurrentStackPossibleActions();

	redraw();
}

void UnitActionPanel::show(Canvas & to)
{
	showAll(to);
	CIntObject::show(to);
}
