/*
 * FocusRulesTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"

#include "../mock/mock_battle_Unit.h"
#include "../mock/mock_CStack.h"

#include "../../lib/battle/Activation.h"
#include "../../lib/battle/Focus.h"
#include "../../lib/battle/FocusRules.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"

#include "../mock/BattleFake.h"

using namespace testing;

TEST(FocusRulesTest, convertsPointsIntoChargesAndCapsThem)
{
	JsonNode config;
	config["enabled"].Bool() = true;
	config["pointsPerCharge"].Integer() = 6;
	config["maxCharges"].Integer() = 3;

	FocusRules rules(config);

	EXPECT_EQ(rules.capacityPoints(), 18);
	EXPECT_EQ(rules.charges(5), 0);
	EXPECT_EQ(rules.charges(6), 1);
	EXPECT_EQ(rules.charges(17), 2);
	EXPECT_EQ(rules.clamp(100), 18);
	EXPECT_EQ(rules.charges(100), 3);
}

TEST(FocusRulesTest, disabledRulesDoNotAccumulateFocus)
{
	JsonNode config;
	config["enabled"].Bool() = false;
	config["pointsPerCharge"].Integer() = 0;
	config["maxCharges"].Integer() = -4;

	FocusRules rules(config);

	EXPECT_EQ(rules.pointsPerCharge, 1);
	EXPECT_EQ(rules.maxCharges, 1);
	EXPECT_EQ(rules.capacityPoints(), 0);
	EXPECT_EQ(rules.clamp(42), 0);
}

TEST(FocusRulesTest, awardsPointsOnlyForEffectiveNonRetaliationAttacks)
{
	JsonNode config;
	config["enabled"].Bool() = true;
	FocusRules rules(config);

	EXPECT_EQ(rules.attackerGain(false, false, false, true), 2);
	EXPECT_EQ(rules.attackerGain(false, true, false, true), 1);
	EXPECT_EQ(rules.attackerGain(false, false, true, true), 1);
	EXPECT_EQ(rules.attackerGain(true, false, false, true), 0);
	EXPECT_EQ(rules.attackerGain(false, false, false, false), 0);
	EXPECT_EQ(rules.targetGain(true) + rules.targetGain(true), 2);
	EXPECT_EQ(rules.targetGain(false), 0);
}

std::shared_ptr<Bonus> activationSource(BonusType type, BonusSubtypeID subtype = BonusSubtypeID())
{
	auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::CREATURE_ABILITY, 0, BonusSourceID(), subtype);
	bonus->activation = Activation{.costs = {.focus = 2}, .consumeUses = false};
	return bonus;
}

TEST(FocusRulesTest, resolvesConfiguredSourcesForExistingActionProviders)
{
	test::MockCStack stack;
	const auto bloodlust = SpellID(SpellID::BLOODLUST);
	const auto spellcaster = activationSource(BonusType::SPELLCASTER, BonusSubtypeID(bloodlust));
	const auto randomSpellcaster = activationSource(BonusType::RANDOM_SPELLCASTER);
	const auto healer = activationSource(BonusType::HEALER);
	const auto catapult = activationSource(BonusType::CATAPULT);
	const auto adjacentCaster = activationSource(BonusType::ADJACENT_SPELLCASTER, BonusSubtypeID(bloodlust));

	stack.addNewBonus(spellcaster);
	stack.addNewBonus(randomSpellcaster);
	stack.addNewBonus(healer);
	stack.addNewBonus(catapult);
	stack.addNewBonus(adjacentCaster);

	EXPECT_EQ(battle::getActivationSource(&stack, EActionType::MONSTER_SPELL, bloodlust), spellcaster);
	EXPECT_EQ(battle::getActivationSource(&stack, EActionType::STACK_HEAL, SpellID::NONE), healer);
	EXPECT_EQ(battle::getActivationSource(&stack, EActionType::CATAPULT, SpellID::NONE), catapult);
	EXPECT_EQ(battle::getActivationSource(&stack, EActionType::WALK_AND_CAST, bloodlust), adjacentCaster);
	EXPECT_EQ(battle::getActivationFocusCost(&stack, EActionType::MONSTER_SPELL, bloodlust), 2);
}
TEST(FocusRulesTest, networkUpdateChangesHypotheticalBattleState)
{
	test::battle::BattleFake battle;
	BattleFocusChanged pack;
	pack.side = BattleSide::ATTACKER;
	pack.points = 17;

	EXPECT_CALL(battle, setFocusPoints(BattleSide::ATTACKER, 17));
	battle.accept(pack);
}

TEST(FocusRulesTest, activationCasterCanPreserveOrdinaryUses)
{
	StrictMock<UnitMock> actualCaster;
	spells::ActivationCaster caster(&actualCaster, false);

	caster.spendMana(nullptr, 1);
}

TEST(FocusRulesTest, activationCasterCanConsumeOrdinaryUses)
{
	StrictMock<UnitMock> actualCaster;
	spells::ActivationCaster caster(&actualCaster, true);

	EXPECT_CALL(actualCaster, spendMana(nullptr, 1));
	caster.spendMana(nullptr, 1);
}

TEST(FocusRulesTest, focusSideUsesEffectiveControllerForHypnotizedUnit)
{
	test::battle::BattleFake battle;
	NiceMock<UnitMock> unit;

	EXPECT_CALL(battle, getSidePlayer(BattleSide::ATTACKER)).WillRepeatedly(Return(PlayerColor(0)));
	EXPECT_CALL(battle, getSidePlayer(BattleSide::DEFENDER)).WillRepeatedly(Return(PlayerColor(1)));
	ON_CALL(unit, unitSide()).WillByDefault(Return(BattleSide::ATTACKER));
	ON_CALL(unit, isHypnotized()).WillByDefault(Return(true));

	EXPECT_EQ(battle::focusSide(battle, &unit), BattleSide::DEFENDER);
}
TEST(FocusRulesTest, serializesFocusPointsForCurrentSaves)
{
	SideInBattle source(nullptr);
	source.focusPoints = 13;

	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.oser & source;
	memory.iser.version = ESerializationVersion::CURRENT;

	SideInBattle restored(nullptr);
	memory.iser & restored;
	EXPECT_EQ(restored.focusPoints, 13);
}

TEST(FocusRulesTest, resetsFocusPointsWhenLoadingPreFocusSave)
{
	SideInBattle source(nullptr);
	source.focusPoints = 13;

	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::RECORD_TEXTS_METASTRING;
	memory.oser & source;
	memory.iser.version = ESerializationVersion::RECORD_TEXTS_METASTRING;

	SideInBattle restored(nullptr);
	restored.focusPoints = 9;
	memory.iser & restored;
	EXPECT_EQ(restored.focusPoints, 0);
}
