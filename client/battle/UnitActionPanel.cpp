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
#include "LibActionEntry.h"
#include "../../lib/CBonusTypeHandler.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/texts/CGeneralTextHandler.h"

#include "../GameEngine.h"
#include "events/InputHandler.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../windows/CSpellWindow.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../GameInstance.h"

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

void UnitActionPanel::setActions(int buttonIndex, const BattleActionEntries & filteredActions)
{
	for (const auto & button : buttons)
		if (button.widget != buttons.at(buttonIndex).widget)
			button.widget->setSelectedSilent(false);

	owner.actionsController->cancelMoveAndShootSelection();
	owner.actionsController->setEntries(filteredActions);
	if(const auto * entry = dynamic_cast<const LibActionEntry *>(filteredActions.front().get()); entry && entry->needsNoTarget())
	{
		if(entry->isLegal(BattleHex::INVALID))
			entry->realize(BattleHex::INVALID);
		return;
	}
	if (filteredActions.front()->getSpell() != SpellID::NONE)
		owner.actionsController->enterCreatureCastingMode();
	owner.actionsController->setEntries(filteredActions);
}

void UnitActionPanel::addButton(const UnitActionButton & button, const BattleActionEntries & entries)
{
	int index = buttons.size();

	const auto & callback = [this, index](bool isSelected){ if (isSelected) setActions(index, buttons.at(index).entries); else restoreAllActions(); };

	std::shared_ptr<CToggleButton> toggle;
	std::shared_ptr<CIntObject> icon;

	if (button.spell == SpellID::NONE)
	{
		MetaString tooltip;
		tooltip.appendTextID(button.tooltipTextID);

		toggle = std::make_shared<CToggleButton>(Point(2, 7 + 50 * index), AnimationPath::builtin("battleUnitAction"), CButton::tooltip(tooltip.toString(&GAME->translator())), callback);
		icon = std::make_shared<CPicture>(button.icon);
		toggle->setOverlay(icon);
		toggle->setAllowDeselection(true);
	}
	else
	{
		MetaString tooltip;
		tooltip.appendTextID("core.genrltxt.26");
		tooltip.replaceName(button.spell);

		std::string hoverText = tooltip.toString(&GAME->translator());
		std::string description = button.spell.toSpell()->getDescriptionTranslated(0);

		toggle = std::make_shared<CToggleButton>(Point(2, 7 + 50 * index), AnimationPath::builtin("battleUnitAction"), CButton::tooltip(hoverText, description), callback);
		icon = std::make_shared<CAnimImage>(AnimationPath::builtin("spellint"), button.spell.getNum() + 1);
		toggle->setOverlay(icon);
	}

	toggle->setHighlightedBorderColor(Colors::WHITE);
	std::string hover = button.spell.hasValue() ? button.spell.toSpell()->getNameTranslated() : LIBRARY->generaltexth->translate(button.tooltipTextID);
	std::string help = button.spell.hasValue() ? button.spell.toSpell()->getDescriptionTranslated(0) : hover;
	if(button.source && !button.activatedAbility.empty())
	{
		help = LIBRARY->bth->bonusToString(std::const_pointer_cast<Bonus>(button.source));
		if(help.empty()) help = hover;
	}
	const Rect bounds = toggle->pos.include(icon->pos);
	auto shade = std::make_shared<TransparentFilledRectangle>(Rect(Point(), bounds.dimensions()), ColorRGBA(0, 0, 0, 144));
	shade->moveTo(bounds.topLeft());
	shade->setRedrawParent(true);
	buttons.push_back({toggle, entries, hover, help, shade, button});
	updateButton(buttons.back());
}

void UnitActionPanel::setPossibleActions(const BattleActionEntries & newActions)
{
	OBJECT_CONSTRUCTION;

	buttons.clear();

	std::vector<std::pair<UnitActionButton, BattleActionEntries>> groups;

	for (const auto & entry : newActions)
	{
		const auto button = entry->getPanelButton();
		if (!button)
			continue;

		auto group = std::find_if(groups.begin(), groups.end(), [&button](const auto & group){ return group.first == *button; });
		if (group == groups.end())
			groups.emplace_back(*button, BattleActionEntries{entry});
		else
			group->second.push_back(entry);
	}

	std::stable_sort(groups.begin(), groups.end(), [](const auto & lhs, const auto & rhs){ return std::tie(lhs.first.order, lhs.first.focusCost) < std::tie(rhs.first.order, rhs.first.focusCost); });

	for (const auto & [button, entries] : groups)
		addButton(button, entries);

	redraw();
}

void UnitActionPanel::show(Canvas & to)
{
	showAll(to);
	CIntObject::show(to);
}


bool UnitActionPanel::updateButton(ActionButton & button)
{
	bool available = false, limit = false;
	int32_t cost = button.identity.focusCost, cooldown = 0;
	for(const auto & entry : button.entries)
		if(const auto info = entry->getPanelButton())
		{
			available |= info->available;
			limit |= info->limitReached;
			cost = std::max(cost, info->focusCost);
			cooldown = std::max(cooldown, info->cooldownRemaining);
		}
	std::string text = button.hoverText, help = button.helpText;
	const auto append = [&](const std::string & value){ text += "\n" + value; help += "\n" + value; };
	if(cost > 0)
	{
		MetaString value = MetaString::createFromTextID(cost == 1 ? "vcmi.battle.focus.cost.one" : "vcmi.battle.focus.cost.many");
		value.replaceNumber(cost);
		append(value.toString(&GAME->translator()));
	}
	if(cooldown > 0)
	{
		MetaString value = MetaString::createFromTextID(cooldown == 1 ? "vcmi.battle.activation.cooldown.one" : "vcmi.battle.activation.cooldown.many");
		value.replaceNumber(cooldown);
		append(value.toString(&GAME->translator()));
	}
	else if(limit)
		append(LIBRARY->generaltexth->translate("vcmi.battle.activation.limitReached"));
	else if(!available && cost > 0 && !button.entries.empty())
		append(LIBRARY->generaltexth->translate("vcmi.battle.focus.insufficient"));
	const bool deselected = !available && button.widget->isSelected();
	button.widget->setHelp(CButton::tooltip(text, help));
	button.widget->setHoverEnabledWhenBlocked(true);
	button.widget->block(!available);
	button.shade->setEnabled(!available);
	if(deselected) button.widget->setSelectedSilent(false);
	return deselected;
}

bool UnitActionPanel::cancelSelectedAction()
{
	bool selected = false;
	for(auto & button : buttons)
	{
		selected |= button.widget->isSelected();
		button.widget->setSelectedSilent(false);
	}
	if(selected) restoreAllActions();
	return selected;
}

void UnitActionPanel::updateActivationAvailability(const BattleActionEntries & actions)
{
	bool reset = false;
	for(auto & button : buttons)
	{
		BattleActionEntries updated;
		const auto & previous = button.identity;
		for(const auto & entry : actions)
			if(const auto current = entry->getPanelButton(); current && current->spell == previous.spell
				&& current->activatedAbility == previous.activatedAbility && current->tooltipTextID == previous.tooltipTextID)
				updated.push_back(entry);
		button.entries = std::move(updated);
		reset |= updateButton(button);
	}
	if(reset) restoreAllActions();
	redraw();
}
