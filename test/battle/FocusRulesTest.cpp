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
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/Focus.h"
#include "../../lib/battle/FocusRules.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"

#include "../mock/BattleFake.h"
#include "../../AI/BattleAI/StackWithBonuses.h"

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

TEST(FocusRulesTest, resolvesOnlyTheSelectedDeliberateAbility)
{
	test::MockCStack stack;
	auto activeAttack = activationSource(BonusType::SPELL_AFTER_ATTACK, BonusSubtypeID(SpellID(SpellID::SLOW)));
	activeAttack->activation->id = "focus-demo:slowAttack";
	stack.addNewBonus(activeAttack);

	EXPECT_EQ(battle::getActivationSource(&stack, EActionType::WALK_AND_ATTACK, SpellID::NONE, "focus-demo:slowAttack"), activeAttack);
	EXPECT_EQ(battle::getActivationSource(&stack, EActionType::WALK_AND_ATTACK, SpellID::NONE, "focus-demo:other"), nullptr);
	EXPECT_EQ(battle::getActivationSource(&stack, EActionType::WALK_AND_ATTACK, SpellID::NONE), nullptr);
}
TEST(FocusRulesTest, deliberateAbilityIdsAreListedOnlyOnce)
{
	test::MockCStack stack;
	auto first = activationSource(BonusType::SPELL_AFTER_ATTACK, BonusSubtypeID(SpellID(SpellID::ACID_BREATH_DAMAGE)));
	first->activation->id = "focus-demo:rustDragonAcidBreath";
	auto second = activationSource(BonusType::SPELL_AFTER_ATTACK, BonusSubtypeID(SpellID(SpellID::ACID_BREATH_DEFENSE)));
	second->activation->id = first->activation->id;
	stack.addNewBonus(first);
	stack.addNewBonus(second);

	const auto sources = battle::getDeliberateActivationSources(&stack, EActionType::WALK_AND_ATTACK);
	ASSERT_EQ(sources.size(), 1);
	EXPECT_EQ(sources.front()->activation->id, "focus-demo:rustDragonAcidBreath");
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

TEST(FocusRulesTest, activationUsageNetworkUpdateChangesHypotheticalBattleState)
{
	test::battle::BattleFake battle;
	BattleActivationUsageChanged pack;
	pack.side = BattleSide::DEFENDER;
	pack.key = "focus-demo:wraithManaDrain";
	pack.period = EActivationLimitPeriod::BATTLE;
	pack.uses = 2;

	EXPECT_CALL(battle, setActivationUsage(BattleSide::DEFENDER, pack.key, EActivationLimitPeriod::BATTLE, 2));
	battle.accept(pack);
}

TEST(FocusRulesTest, hypotheticalNextRoundResetsOnlyRoundActivationUsage)
{
	auto realBattle = std::make_shared<NiceMock<test::battle::BattleFake>>();
	ON_CALL(*realBattle, getActivationUsage(BattleSide::ATTACKER, "roundAbility", EActivationLimitPeriod::ROUND)).WillByDefault(Return(1));
	ON_CALL(*realBattle, getActivationUsage(BattleSide::ATTACKER, "battleAbility", EActivationLimitPeriod::BATTLE)).WillByDefault(Return(2));

	HypotheticBattle hypothetical(nullptr, realBattle);
	EXPECT_EQ(hypothetical.getActivationUsage(BattleSide::ATTACKER, "roundAbility", EActivationLimitPeriod::ROUND), 1);
	EXPECT_EQ(hypothetical.getActivationUsage(BattleSide::ATTACKER, "battleAbility", EActivationLimitPeriod::BATTLE), 2);

	hypothetical.nextRound();

	EXPECT_EQ(hypothetical.getActivationUsage(BattleSide::ATTACKER, "roundAbility", EActivationLimitPeriod::ROUND), 0);
	EXPECT_EQ(hypothetical.getActivationUsage(BattleSide::ATTACKER, "battleAbility", EActivationLimitPeriod::BATTLE), 2);
}

TEST(FocusRulesTest, serializesSharedActivationUsage)
{
	SideInBattle source(nullptr);
	source.activationUsageBattle["focus-demo:resurrection"] = 1;
	source.activationUsageRound["focus-demo:thunderbolt"] = 1;

	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.oser & source;
	memory.iser.version = ESerializationVersion::CURRENT;

	SideInBattle restored(nullptr);
	memory.iser & restored;
	EXPECT_EQ(restored.activationUsageBattle.at("focus-demo:resurrection"), 1);
	EXPECT_EQ(restored.activationUsageRound.at("focus-demo:thunderbolt"), 1);
}

TEST(FocusRulesTest, concreteAbilityCanReturnControlWithoutChangingSpell)
{
	test::MockCStack stack;
	const auto bloodlust = SpellID(SpellID::BLOODLUST);
	auto source = activationSource(BonusType::SPELLCASTER, BonusSubtypeID(bloodlust));
	source->actionReturnsControl = true;
	stack.addNewBonus(source);

	EXPECT_TRUE(battle::actionReturnsControl(&stack, EActionType::MONSTER_SPELL, bloodlust));
	EXPECT_FALSE(battle::actionReturnsControl(&stack, EActionType::CATAPULT, SpellID::NONE));
}

TEST(FocusRulesTest, serializesSelectedAbilityInBattleAction)
{
	BattleAction source;
	source.actionType = EActionType::WALK_AND_ATTACK;
	source.activatedAbility = "focus-demo:slowAttack";

	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.oser & source;
	memory.iser.version = ESerializationVersion::CURRENT;

	BattleAction restored;
	memory.iser & restored;
	EXPECT_EQ(restored.activatedAbility, source.activatedAbility);
}

TEST(FocusRulesTest, clearsSelectedAbilityWhenLoadingOlderBattleAction)
{
	BattleAction source;
	source.actionType = EActionType::WALK_AND_ATTACK;
	source.activatedAbility = "focus-demo:slowAttack";

	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::ABILITY_RETURNS_CONTROL;
	memory.oser & source;
	memory.iser.version = ESerializationVersion::ABILITY_RETURNS_CONTROL;

	BattleAction restored;
	restored.activatedAbility = "stale";
	memory.iser & restored;
	EXPECT_TRUE(restored.activatedAbility.empty());
}
