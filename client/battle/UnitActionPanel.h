/*
 * UnitActionPanel.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../gui/CIntObject.h"
#include "../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../lib/filesystem/ResourcePath.h"

class CFilledTexture;
class TransparentFilledRectangle;
class CToggleButton;
class CLabel;
class BattleInterface;

class UnitActionPanel : public CIntObject
{
private:
	std::shared_ptr<CFilledTexture> background;
	std::shared_ptr<TransparentFilledRectangle> rect;

	struct ActionButton
	{
		std::shared_ptr<CToggleButton> widget;
		std::vector<PossiblePlayerBattleAction> actions;
		std::string baseHoverText;
		std::string baseHelpText;
		std::shared_ptr<TransparentFilledRectangle> disabledOverlay;
	};
	std::vector<ActionButton> buttons;

	BattleInterface & owner;

	void testAndAddAction(const std::vector<PossiblePlayerBattleAction> & allActions, const std::vector<PossiblePlayerBattleAction::Actions> & actionFilter, const ImagePath & iconPath, const std::string & descriptionTextID );
	void testAndAddSpell(const std::vector<PossiblePlayerBattleAction> & allActions, const SpellID & spellFilter );
	void testAndAddAbility(const PossiblePlayerBattleAction & action);
	bool updateButton(ActionButton & button);

	void restoreAllActions();
	void setActions(int buttonIndex, const std::vector<PossiblePlayerBattleAction> & newActions);
public:
	static constexpr int ACTION_SLOTS = 12;

	UnitActionPanel(BattleInterface & owner);

	void setPossibleActions(const std::vector<PossiblePlayerBattleAction> & actions);
	void updateActivationAvailability(const std::vector<PossiblePlayerBattleAction> & actions);
	/// Deselects the action chosen in this panel and restores the stack's complete action set.
	bool cancelSelectedAction();

	std::vector<std::tuple<SpellID, bool>> getSpells() const;

	void show(Canvas & to) override;
};
