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
#include "../../../lib/mapObjects/CGHeroInstance.h"
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
		focus["meleePoints"].Integer() = 0;
		focus["rangedPoints"].Integer() = 0;
		focus["longReachPoints"].Integer() = 0;
		focus["hitPoints"].Integer() = 0;
		map()->overrideGameSetting(EGameSettings::COMBAT_FOCUS, focus);
	}

	CStack * addFocusCaster(const std::vector<ActivationLimit> & limits = {})
	{
		auto * caster = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1);
		auto ability = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLCASTER, BonusSource::CREATURE_ABILITY, 3, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::BLOODLUST)));
		ability->activation = Activation{.costs = {.focus = 1}, .consumeUses = false, .limits = limits};
		caster->addNewBonus(ability);
		return caster;
	}

	CStack * addFocusAttacker(const std::string & id)
	{
		auto * attacker = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1);
		auto ability = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_AFTER_ATTACK, BonusSource::CREATURE_ABILITY, 100, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::SLOW)));
		ability->activation = Activation{.id = id, .costs = {.focus = 1}, .consumeUses = false};
		attacker->addNewBonus(ability);
		return attacker;
	}

	CStack * addFocusManaDrainer(const BattleHex & position)
	{
		auto * drainer = addStack(BattleSide::ATTACKER, CreatureID(0), position, 1);
		auto ability = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MANA_DRAIN, BonusSource::CREATURE_ABILITY, 5, BonusSourceID());
		ability->activation = Activation{
			.id = "focus-demo:wraithManaDrain",
			.costs = {.focus = 1},
			.consumeUses = false,
			.limits = {
				{.key = "focus-demo:wraithManaDrain", .scope = EActivationLimitScope::SIDE, .period = EActivationLimitPeriod::BATTLE, .max = 3},
				{.key = "focus-demo:wraithManaDrain", .scope = EActivationLimitScope::SIDE, .period = EActivationLimitPeriod::ROUND, .max = 1}
			}
		};
		ability->actionReturnsControl = true;
		drainer->addNewBonus(ability);
		return drainer;
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

TEST_F(FocusActivationTest, sharedBattleLimitRejectsSecondStackOnSameSide)
{
	startGame();
	enableFocus();
	startBattle();
	ActivationLimit limit;
	limit.key = "focus-demo:sharedBloodlust";
	limit.scope = EActivationLimitScope::SIDE;
	limit.period = EActivationLimitPeriod::BATTLE;
	limit.max = 1;
	auto * firstCaster = addFocusCaster({limit});
	auto * secondCaster = addFocusCaster({limit});
	beginCombat();
	battle()->setFocusPoints(BattleSide::ATTACKER, 3);

	battle::Target firstTarget;
	firstTarget.emplace_back(firstCaster);
	battle()->activeStack = firstCaster->unitId();
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeCreatureSpellcast(firstCaster, firstTarget, SpellID(SpellID::BLOODLUST))));
	EXPECT_EQ(battle()->getActivationUsage(BattleSide::ATTACKER, limit.key, limit.period), 1);

	battle::Target secondTarget;
	secondTarget.emplace_back(secondCaster);
	battle()->activeStack = secondCaster->unitId();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeCreatureSpellcast(secondCaster, secondTarget, SpellID(SpellID::BLOODLUST))));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 2);
}

TEST_F(FocusActivationTest, roundActivationUsageResetsAtNextRound)
{
	startGame();
	enableFocus();
	startBattle();
	beginCombat();
	battle()->setActivationUsage(BattleSide::DEFENDER, "focus-demo:thunderbolt", EActivationLimitPeriod::ROUND, 1);

	battle()->nextRound();

	EXPECT_EQ(battle()->getActivationUsage(BattleSide::DEFENDER, "focus-demo:thunderbolt", EActivationLimitPeriod::ROUND), 0);
}

TEST_F(FocusActivationTest, manaDrainReturnsControlAndSharesItsRoundLimit)
{
	startGame();
	defenderSideHero->mana = 20;
	enableFocus();
	startBattle();
	auto * first = addFocusManaDrainer(BattleHex(leftHex));
	auto * second = addFocusManaDrainer(BattleHex(leftHex - 1));
	addStack(BattleSide::DEFENDER, CreatureID(1), BattleHex(rightHex), 1);
	beginCombat();
	battle()->setFocusPoints(BattleSide::ATTACKER, 3);

	battle()->activeStack = first->unitId();
	const auto firstAction = BattleAction::makeCreatureAbility(first, "focus-demo:wraithManaDrain");
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), firstAction));
	EXPECT_EQ(defenderSideHero->mana, 15);
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 2);
	EXPECT_EQ(battle()->getActiveStackID(), first->unitId());
	EXPECT_EQ(battle()->getActivationUsage(BattleSide::ATTACKER, "focus-demo:wraithManaDrain", EActivationLimitPeriod::BATTLE), 1);
	EXPECT_EQ(battle()->getActivationUsage(BattleSide::ATTACKER, "focus-demo:wraithManaDrain", EActivationLimitPeriod::ROUND), 1);

	battle()->activeStack = second->unitId();
	const auto secondAction = BattleAction::makeCreatureAbility(second, "focus-demo:wraithManaDrain");
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), secondAction));
	EXPECT_EQ(defenderSideHero->mana, 15);
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 2);

	battle()->nextRound();
	battle()->activeStack = second->unitId();
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), secondAction));
	EXPECT_EQ(defenderSideHero->mana, 10);
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 1);
	EXPECT_EQ(battle()->getActivationUsage(BattleSide::ATTACKER, "focus-demo:wraithManaDrain", EActivationLimitPeriod::BATTLE), 2);
	EXPECT_EQ(battle()->getActivationUsage(BattleSide::ATTACKER, "focus-demo:wraithManaDrain", EActivationLimitPeriod::ROUND), 1);
}

TEST_F(FocusActivationTest, deliberateAttackAbilityConsumesFocusAndAppliesItsExistingEffect)
{
	startGame();
	enableFocus();
	startBattle();
	auto * attacker = addFocusAttacker("focus-demo:slowAttack");
	auto * defender = addStack(BattleSide::DEFENDER, CreatureID(1), BattleHex(rightHex), 100);
	blockRetaliation(attacker);
	beginCombat();
	battle()->activeStack = attacker->unitId();
	battle()->setFocusPoints(BattleSide::ATTACKER, 1);

	auto action = BattleAction::makeMeleeAttack(attacker, defender->getPosition(), attacker->getPosition());
	action.activatedAbility = "focus-demo:slowAttack";

	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 0);
	EXPECT_TRUE(vstd::contains(defender->activeSpells(), SpellID(SpellID::SLOW)));
}

TEST_F(FocusActivationTest, ordinaryAttackDoesNotConsumeOrApplyDeliberateAbility)
{
	startGame();
	enableFocus();
	startBattle();
	auto * attacker = addFocusAttacker("focus-demo:slowAttack");
	auto * defender = addStack(BattleSide::DEFENDER, CreatureID(1), BattleHex(rightHex), 100);
	blockRetaliation(attacker);
	beginCombat();
	battle()->activeStack = attacker->unitId();
	battle()->setFocusPoints(BattleSide::ATTACKER, 1);

	const auto action = BattleAction::makeMeleeAttack(attacker, defender->getPosition(), attacker->getPosition());

	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(vstd::contains(defender->activeSpells(), SpellID(SpellID::SLOW)));
}

TEST_F(FocusActivationTest, rejectsUnknownDeliberateAbilityWithoutChargingFocus)
{
	startGame();
	enableFocus();
	startBattle();
	auto * attacker = addFocusAttacker("focus-demo:slowAttack");
	auto * defender = addStack(BattleSide::DEFENDER, CreatureID(1), BattleHex(rightHex), 100);
	beginCombat();
	battle()->activeStack = attacker->unitId();
	battle()->setFocusPoints(BattleSide::ATTACKER, 1);

	auto action = BattleAction::makeMeleeAttack(attacker, defender->getPosition(), attacker->getPosition());
	action.activatedAbility = "focus-demo:unknown";

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 1);
}
