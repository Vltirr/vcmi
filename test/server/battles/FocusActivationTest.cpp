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
#include "../../../lib/GameLibrary.h"
#include "../../../lib/json/JsonBonus.h"

#include <vcmi/CreatureService.h>
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

	void disableFocus()
	{
		JsonNode focus;
		focus["enabled"].Bool() = false;
		focus["pointsPerCharge"].Integer() = 1;
		focus["maxCharges"].Integer() = 3;
		map()->overrideGameSetting(EGameSettings::COMBAT_FOCUS, focus);
	}

	CStack * addFocusCaster(const std::vector<ActivationLimit> & limits = {}, int32_t focusCost = 1, std::optional<ActivationCooldown> cooldown = std::nullopt)
	{
		auto * caster = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1);
		auto ability = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLCASTER, BonusSource::CREATURE_ABILITY, 3, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::BLOODLUST)));
		ability->activation = Activation{.costs = {.focus = focusCost}, .cooldown = std::move(cooldown), .consumeUses = false, .limits = limits};
		caster->addNewBonus(ability);
		return caster;
	}

	CStack * addFailingHealer(const std::vector<ActivationLimit> & limits, std::optional<ActivationCooldown> cooldown)
	{
		auto * healer = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1);
		auto ability = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HEALER, BonusSource::CREATURE_ABILITY, 3, BonusSourceID());
		ability->activation = Activation{.costs = {.focus = 1}, .cooldown = std::move(cooldown), .limits = limits};
		healer->addNewBonus(ability);
		return healer;
	}

	CStack * addFocusAttacker(const std::string & id)
	{
		auto * attacker = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1);
		auto ability = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_AFTER_ATTACK, BonusSource::CREATURE_ABILITY, 100, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::SLOW)));
		ability->activation = Activation{.id = id, .costs = {.focus = 1}, .consumeUses = false};
		attacker->addNewBonus(ability);
		return attacker;
	}

	CStack * addFocusDeathStareAttacker()
	{
		auto * attacker = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 100);
		const auto * mightyGorgon = LIBRARY->creatures()->getByName("core:mightyGorgon");
		EXPECT_NE(mightyGorgon, nullptr);
		if(!mightyGorgon)
			return attacker;

		const auto sources = mightyGorgon->getBonusBearer()->getAllBonuses(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER));
		EXPECT_EQ(sources->size(), 1);
		if(sources->empty())
			return attacker;

		auto ability = std::make_shared<Bonus>(*sources->front());
		ability->activation = Activation{
			.id = "focus-demo:mightyGorgonDeathStare",
			.costs = {.focus = 2},
			.consumeUses = false,
			.limits = {{
				.key = "focus-demo:mightyGorgonDeathStare",
				.scope = EActivationLimitScope::SIDE,
				.period = EActivationLimitPeriod::ROUND,
				.max = 1
			}}
		};
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

	CStack * addGenericEnchanter()
	{
		auto * enchanter = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1);
		auto ability = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ENCHANTER, BonusSource::CREATURE_ABILITY, 3, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::BLOODLUST)));
		ability->activation = Activation{
			.id = "vcmi-test:enchanter",
			.costs = {.focus = 0},
			.turn = ActivationTurn{.returnsControl = true},
			.cooldown = ActivationCooldown{.scope = EActivationLimitScope::SIDE, .afterUse = 3},
			.limits = {{.scope = EActivationLimitScope::STACK, .period = EActivationLimitPeriod::TURN, .max = 1}}
		};
		enchanter->addNewBonus(ability);
		return enchanter;
	}
};

TEST_F(FocusActivationTest, zeroCostActivationWorksWhenFocusIsDisabled)
{
	startGame();
	disableFocus();
	startBattle();
	auto * caster = addFocusCaster({}, 0);
	beginCombat();
	battle()->activeStack = caster->unitId();

	battle::Target target;
	target.emplace_back(caster);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeCreatureSpellcast(caster, target, SpellID(SpellID::BLOODLUST))));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 0);
	EXPECT_FALSE(caster->castSpellThisTurn);
}

TEST_F(FocusActivationTest, positiveCostActivationIsRejectedWhenFocusIsDisabled)
{
	startGame();
	disableFocus();
	startBattle();
	auto * caster = addFocusCaster({}, 1);
	beginCombat();
	battle()->activeStack = caster->unitId();

	battle::Target target;
	target.emplace_back(caster);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeCreatureSpellcast(caster, target, SpellID(SpellID::BLOODLUST))));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 0);
}

TEST_F(FocusActivationTest, loaderRejectsPositiveCostActivationWhenFocusIsDisabled)
{
	startGame();
	disableFocus();

	const std::string content = R"({
		"type" : "SPELLCASTER",
		"subtype" : "bloodlust",
		"activation" : { "costs" : { "focus" : 1 } }
	})";
	JsonNode ability(content.data(), content.size(), "FocusActivationTest");
	ability.setModScope("vcmi-test");
	Bonus parsed;

	EXPECT_FALSE(JsonUtils::parseBonus(ability, &parsed, TextIdentifier("vcmi-test:positiveFocusCost")));
}

TEST_F(FocusActivationTest, loaderAcceptsZeroCostActivationWhenFocusIsDisabled)
{
	startGame();
	disableFocus();

	const std::string content = R"({
		"type" : "SPELLCASTER",
		"subtype" : "bloodlust",
		"activation" : { }
	})";
	JsonNode ability(content.data(), content.size(), "FocusActivationTest");
	ability.setModScope("vcmi-test");
	Bonus parsed;

	EXPECT_TRUE(JsonUtils::parseBonus(ability, &parsed, TextIdentifier("vcmi-test:zeroFocusCost")));
}

TEST_F(FocusActivationTest, loaderAcceptsDerivedNativeIdentityWithoutExplicitKeys)
{
	startGame();

	const std::string content = R"({
		"type" : "SPELLCASTER",
		"subtype" : "bloodlust",
		"activation" : {
			"limits" : [ { "scope" : "stack", "period" : "turn", "max" : 1 } ]
		}
	})";
	JsonNode ability(content.data(), content.size(), "FocusActivationTest");
	ability.setModScope("vcmi-test");
	Bonus parsed;

	EXPECT_TRUE(JsonUtils::parseBonus(ability, &parsed, TextIdentifier("vcmi-test:derivedIdentity")));
}

TEST_F(FocusActivationTest, loaderRejectsDeliberateActivationWithoutId)
{
	startGame();

	const std::string content = R"({
		"type" : "MANA_DRAIN",
		"activation" : { }
	})";
	JsonNode ability(content.data(), content.size(), "FocusActivationTest");
	ability.setModScope("vcmi-test");
	Bonus parsed;

	EXPECT_FALSE(JsonUtils::parseBonus(ability, &parsed, TextIdentifier("vcmi-test:missingId")));
}

TEST_F(FocusActivationTest, loaderRejectsAmbiguousCooldownScope)
{
	startGame();

	const std::string content = R"({
		"type" : "SPELLCASTER",
		"subtype" : "bloodlust",
		"activation" : {
			"limits" : [
				{ "scope" : "side", "period" : "battle", "max" : 1 },
				{ "scope" : "stack", "period" : "round", "max" : 1 }
			],
			"cooldown" : { "afterUse" : 1 }
		}
	})";
	JsonNode ability(content.data(), content.size(), "FocusActivationTest");
	ability.setModScope("vcmi-test");
	Bonus parsed;

	EXPECT_FALSE(JsonUtils::parseBonus(ability, &parsed, TextIdentifier("vcmi-test:ambiguousCooldown")));
}

TEST_F(FocusActivationTest, activationIgnoresNativeSpellTurnMarker)
{
	startGame();
	disableFocus();
	startBattle();
	auto * caster = addFocusCaster({}, 0);
	beginCombat();
	battle()->activeStack = caster->unitId();
	caster->castSpellThisTurn = true;

	battle::Target target;
	target.emplace_back(caster);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeCreatureSpellcast(caster, target, SpellID(SpellID::BLOODLUST))));
}

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

TEST_F(FocusActivationTest, genericCooldownBlocksUntilItsAbsoluteReadyRound)
{
	startGame();
	enableFocus();
	startBattle();
	auto * caster = addFocusCaster({}, 1, ActivationCooldown{.afterUse = 2});
	beginCombat();
	battle()->activeStack = caster->unitId();
	battle()->setFocusPoints(BattleSide::ATTACKER, 3);

	battle::Target target;
	target.emplace_back(caster);
	const auto action = BattleAction::makeCreatureSpellcast(caster, target, SpellID(SpellID::BLOODLUST));
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const auto resolution = battle::resolveActivation(*battle(), caster, action.actionType, action.spell);
	ASSERT_TRUE(resolution.cooldown.has_value());
	EXPECT_EQ(battle()->getActivationCooldownReadyRound(BattleSide::ATTACKER, *resolution.cooldown), battle()->getRound() + 2);
	EXPECT_FALSE(resolution.cooldownAvailable);
	EXPECT_EQ(resolution.cooldownRoundsRemaining, 2);

	battle()->nextRound();
	EXPECT_EQ(battle::resolveActivation(*battle(), caster, action.actionType, action.spell).cooldownRoundsRemaining, 1);
	battle()->nextRound();
	EXPECT_TRUE(battle::resolveActivation(*battle(), caster, action.actionType, action.spell).cooldownAvailable);
}

TEST_F(FocusActivationTest, genericCooldownInitialBlocksBeforeAnyUse)
{
	startGame();
	enableFocus();
	startBattle();
	auto * caster = addFocusCaster({}, 0, ActivationCooldown{.initial = 2});
	beginCombat();
	battle()->activeStack = caster->unitId();

	const auto resolution = battle::resolveActivation(*battle(), caster, EActionType::MONSTER_SPELL, SpellID(SpellID::BLOODLUST));
	ASSERT_TRUE(resolution.cooldown.has_value());
	EXPECT_FALSE(resolution.cooldownAvailable);
	EXPECT_EQ(resolution.cooldownRoundsRemaining, std::max(0, 3 - battle()->getRound()));
}

TEST_F(FocusActivationTest, failedEffectRestoresFocusLimitsAndCooldown)
{
	startGame();
	enableFocus();
	startBattle();
	const ActivationLimit limit{.key = "rollback", .scope = EActivationLimitScope::STACK, .period = EActivationLimitPeriod::BATTLE, .max = 1};
	auto * caster = addFailingHealer({limit}, ActivationCooldown{.afterUse = 2});
	beginCombat();
	battle()->activeStack = caster->unitId();
	battle()->setFocusPoints(BattleSide::ATTACKER, 2);

	const auto action = BattleAction::makeHeal(caster, caster);
	const auto resolution = battle::resolveActivation(*battle(), caster, action.actionType, action.spell);
	ASSERT_TRUE(resolution.cooldown.has_value());

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 2);
	EXPECT_EQ(battle()->getActivationUsage(BattleSide::ATTACKER, resolution.limits.front()), 0);
	EXPECT_EQ(battle()->getActivationCooldownReadyRound(BattleSide::ATTACKER, *resolution.cooldown), 0);
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

TEST_F(FocusActivationTest, waitingKeepsTurnUsageButMoraleCreatesNewOpportunity)
{
	startGame();
	startBattle();
	auto * caster = addFocusCaster({}, 0);
	beginCombat();
	const ActivationStateKey key{"turnUsage", EActivationLimitScope::STACK, static_cast<int32_t>(caster->unitId()), EActivationLimitPeriod::TURN};
	battle()->setActivationUsage(BattleSide::ATTACKER, key, 1);

	caster->waiting = true;
	battle()->nextTurn(caster->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(battle()->getActivationUsage(BattleSide::ATTACKER, key), 1);

	battle()->nextTurn(caster->unitId(), BattleUnitTurnReason::MORALE);
	EXPECT_EQ(battle()->getActivationUsage(BattleSide::ATTACKER, key), 0);
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

TEST_F(FocusActivationTest, genericEnchanterUsesActivationCooldownInsteadOfLegacyCounter)
{
	startGame();
	disableFocus();
	startBattle();
	auto * enchanter = addGenericEnchanter();
	beginCombat();
	battle()->activeStack = enchanter->unitId();
	battle()->getSide(BattleSide::ATTACKER).enchanterCounter = 7;

	const auto action = BattleAction::makeCreatureAbility(enchanter, "vcmi-test:enchanter");
	const auto resolution = battle::resolveActivation(*battle(), enchanter, action.actionType, action.spell, action.activatedAbility);
	ASSERT_TRUE(resolution.cooldown.has_value());
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).enchanterCounter, 7);
	EXPECT_EQ(battle()->getActivationCooldownReadyRound(BattleSide::ATTACKER, *resolution.cooldown), battle()->getRound() + 3);
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

	const auto casts = server.castsOf(SpellID(SpellID::SLOW));
	ASSERT_EQ(casts.size(), 1);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines,
		[](const std::string & line) { return line.find("consumes 1 Focus") != std::string::npos; }));
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

TEST_F(FocusActivationTest, deliberateDeathStareConsumesTwoChargesAndItsSharedRoundUse)
{
	startGame();
	enableFocus();
	startBattle();
	auto * attacker = addFocusDeathStareAttacker();
	auto * defender = addStack(BattleSide::DEFENDER, CreatureID(1), BattleHex(rightHex), 100);
	blockRetaliation(attacker);
	beginCombat();
	battle()->activeStack = attacker->unitId();
	battle()->setFocusPoints(BattleSide::ATTACKER, 2);

	auto action = BattleAction::makeMeleeAttack(attacker, defender->getPosition(), attacker->getPosition());
	action.activatedAbility = "focus-demo:mightyGorgonDeathStare";
	const auto source = battle::getActivationSource(attacker, action.actionType, action.spell, action.activatedAbility);
	ASSERT_NE(source, nullptr);
	ASSERT_TRUE(source->activation.has_value());
	ASSERT_TRUE(battle::isActivationAvailable(*battle(), attacker, action.actionType, action.spell, action.activatedAbility));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 0);
	EXPECT_EQ(battle()->getActivationUsage(BattleSide::ATTACKER, action.activatedAbility, EActivationLimitPeriod::ROUND), 1);

	const auto casts = server.castsOf(SpellID(SpellID::DEATH_STARE));
	ASSERT_EQ(casts.size(), 1);
	EXPECT_GT(casts.front().killed, 0);
	EXPECT_LE(casts.front().killed, 10);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines,
		[](const std::string & line) { return line.find("consumes 2 Focus Charges") != std::string::npos; }));
}

TEST_F(FocusActivationTest, rejectsDeathStareAgainstNonLivingTargetBeforeChargingFocus)
{
	startGame();
	enableFocus();
	startBattle();
	auto * attacker = addFocusDeathStareAttacker();
	const auto * skeleton = LIBRARY->creatures()->getByName("core:skeleton");
	ASSERT_NE(skeleton, nullptr);
	auto * defender = addStack(BattleSide::DEFENDER, skeleton->getId(), BattleHex(rightHex), 100);
	blockRetaliation(attacker);
	beginCombat();
	battle()->activeStack = attacker->unitId();
	battle()->setFocusPoints(BattleSide::ATTACKER, 2);

	auto action = BattleAction::makeMeleeAttack(attacker, defender->getPosition(), attacker->getPosition());
	action.activatedAbility = "focus-demo:mightyGorgonDeathStare";

	EXPECT_FALSE(battle::isActivationTargetAvailable(*battle(), attacker, action.actionType, action.spell, action.activatedAbility, defender));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 2);
	EXPECT_EQ(battle()->getActivationUsage(BattleSide::ATTACKER, action.activatedAbility, EActivationLimitPeriod::ROUND), 0);
	EXPECT_TRUE(server.castsOf(SpellID(SpellID::DEATH_STARE)).empty());
}

TEST_F(FocusActivationTest, ordinaryAttackDoesNotTriggerConfiguredDeathStare)
{
	startGame();
	enableFocus();
	startBattle();
	auto * attacker = addFocusDeathStareAttacker();
	auto * defender = addStack(BattleSide::DEFENDER, CreatureID(1), BattleHex(rightHex), 100);
	blockRetaliation(attacker);
	beginCombat();
	battle()->activeStack = attacker->unitId();
	battle()->setFocusPoints(BattleSide::ATTACKER, 2);

	const auto action = BattleAction::makeMeleeAttack(attacker, defender->getPosition(), attacker->getPosition());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), 2);
	EXPECT_EQ(battle()->getActivationUsage(BattleSide::ATTACKER, "focus-demo:mightyGorgonDeathStare", EActivationLimitPeriod::ROUND), 0);
	EXPECT_TRUE(server.castsOf(SpellID(SpellID::DEATH_STARE)).empty());
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
