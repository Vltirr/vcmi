/*
 * FocusActivationTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/battle/Activation.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

class FocusActivationTest : public BattleTestFixture
{
protected:
	void enableFocus()
	{
		JsonNode focus;
		focus["enabled"].Bool() = true;
		focus["pointsPerCharge"].Integer() = 1;
		focus["maxCharges"].Integer() = 3;
		map()->overrideGameSetting(EGameSettings::COMBAT_FOCUS, focus);
	}

	CStack * addFocusCaster()
	{
		auto * caster = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1);
		auto ability = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLCASTER, BonusSource::CREATURE_ABILITY, 3, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::BLOODLUST)));
		ability->activation = Activation{.costs = {.focus = 1}, .consumeUses = false};
		caster->addNewBonus(ability);
		return caster;
	}
};

TEST_F(FocusActivationTest, rejectsUnaffordableConfiguredCreatureSpellBeforeDispatch)
{
	startGame();
	enableFocus();
	startBattle();
	auto * caster = addFocusCaster();
	beginCombat();
	battle()->activeStack = caster->unitId();

	battle::Target target;
	target.emplace_back(caster);
	const auto action = BattleAction::makeCreatureSpellcast(caster, target, SpellID(SpellID::BLOODLUST));

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 0);
}

TEST_F(FocusActivationTest, consumesFocusOnlyWhenConfiguredCreatureSpellIsDispatched)
{
	startGame();
	enableFocus();
	startBattle();
	auto * caster = addFocusCaster();
	beginCombat();
	battle()->activeStack = caster->unitId();
	battle()->setFocusPoints(BattleSide::ATTACKER, 1);

	battle::Target target;
	target.emplace_back(caster);
	const auto action = BattleAction::makeCreatureSpellcast(caster, target, SpellID(SpellID::BLOODLUST));

	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 0);
}
