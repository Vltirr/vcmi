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
#include "../../client/replay/ReplayPackFilter.h"

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

TEST(FocusRulesTest, deliberateSourcesKeepTheReturnedBonusListAliveDuringEnumeration)
{
	class TransientBonusStack : public CStack
	{
	public:
		mutable BonusList bonuses;
		mutable bool released = false;

		TConstBonusListPtr getAllBonuses(const CSelector &, const std::string &) const override
		{
			// Invalidate the contents when the last owner is released, without reading freed memory in the test.
			return TConstBonusListPtr(&bonuses, [this](const BonusList *)
			{
				released = true;
				bonuses.clear();
			});
		}
	};

	TransientBonusStack stack;
	auto attack = activationSource(BonusType::SPELL_AFTER_ATTACK, BonusSubtypeID(SpellID(SpellID::SLOW)));
	attack->activation->id = "focus-demo:slowAttack";
	stack.bonuses.push_back(attack);
	stack.bonuses.push_back(activationSource(BonusType::SPELLCASTER));

	const auto sources = battle::getDeliberateActivationSources(&stack, EActionType::WALK_AND_ATTACK);
	EXPECT_TRUE(stack.released);
	EXPECT_TRUE(stack.bonuses.empty());
	ASSERT_EQ(sources.size(), 1);
	EXPECT_EQ(sources.front(), attack);
	EXPECT_EQ(sources.front()->activation->id, "focus-demo:slowAttack");
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
TEST(FocusRulesTest, serializesFocusFieldsAccordingToForkRevision)
{
	// Component versions exercise defaults for fork fields introduced later.
	constexpr auto firstFocusSaveVersion = ESerializationVersion::BATTLE_FOCUS;
	constexpr auto integratedSaveVersion = ESerializationVersion::FOCUS_MOBILE_SHOOTER_INTEGRATION;
	for(const auto version : {firstFocusSaveVersion, integratedSaveVersion, ESerializationVersion::CURRENT})
	{
		SCOPED_TRACE(static_cast<int>(version));
		SideInBattle source(nullptr);
		source.focusPoints = 13;

		CMemorySerializer memory;
		memory.oser.version = version;
		memory.oser & source;
		memory.iser.version = version;

		SideInBattle restored(nullptr);
		memory.iser & restored;
		EXPECT_EQ(restored.focusPoints, 13);
	}
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
	pack.scope = EActivationLimitScope::STACK;
	pack.stack = 42;
	pack.period = EActivationLimitPeriod::BATTLE;
	pack.uses = 2;

	EXPECT_CALL(battle, setActivationUsage(BattleSide::DEFENDER,
		ActivationStateKey{pack.key, pack.scope, pack.stack, pack.period}, 2));
	battle.accept(pack);
}

TEST(FocusRulesTest, cooldownNetworkUpdateChangesHypotheticalBattleState)
{
	test::battle::BattleFake battle;
	BattleActivationCooldownChanged pack;
	pack.side = BattleSide::ATTACKER;
	pack.key = "focus-demo:enchanter";
	pack.scope = EActivationLimitScope::SIDE;
	pack.stack = -1;
	pack.readyRound = 5;

	EXPECT_CALL(battle, setActivationCooldownReadyRound(BattleSide::ATTACKER,
		ActivationStateKey{pack.key, pack.scope, pack.stack, EActivationLimitPeriod::BATTLE}, 5));
	battle.accept(pack);
}

TEST(FocusRulesTest, activationStatePacketsAreBattleReplayPackets)
{
	BattleActivationUsageChanged usagePack;
	BattleActivationCooldownChanged cooldownPack;
	EXPECT_EQ(ReplayPackFilter::classify(usagePack), EReplayPackKind::BATTLE);
	EXPECT_EQ(ReplayPackFilter::classify(cooldownPack), EReplayPackKind::BATTLE);
}

TEST(FocusRulesTest, hypotheticalNextRoundResetsOnlyRoundActivationUsage)
{
	auto realBattle = std::make_shared<NiceMock<test::battle::BattleFake>>();
	const ActivationStateKey roundKey{"roundAbility", EActivationLimitScope::STACK, 7, EActivationLimitPeriod::ROUND};
	const ActivationStateKey battleKey{"battleAbility", EActivationLimitScope::SIDE, -1, EActivationLimitPeriod::BATTLE};
	const ActivationStateKey cooldownKey{"cooldownAbility", EActivationLimitScope::SIDE, -1, EActivationLimitPeriod::BATTLE};
	ON_CALL(*realBattle, getRound()).WillByDefault(Return(2));
	ON_CALL(*realBattle, getActivationUsage(BattleSide::ATTACKER, roundKey)).WillByDefault(Return(1));
	ON_CALL(*realBattle, getActivationUsage(BattleSide::ATTACKER, battleKey)).WillByDefault(Return(2));
	ON_CALL(*realBattle, getActivationCooldownReadyRound(BattleSide::ATTACKER, cooldownKey)).WillByDefault(Return(4));

	HypotheticBattle hypothetical(nullptr, realBattle);
	EXPECT_EQ(hypothetical.getRound(), 2);
	EXPECT_EQ(hypothetical.getActivationUsage(BattleSide::ATTACKER, roundKey), 1);
	EXPECT_EQ(hypothetical.getActivationUsage(BattleSide::ATTACKER, battleKey), 2);
	EXPECT_EQ(hypothetical.getActivationCooldownReadyRound(BattleSide::ATTACKER, cooldownKey) - hypothetical.getRound(), 2);

	hypothetical.nextRound();

	EXPECT_EQ(hypothetical.getRound(), 3);
	EXPECT_EQ(hypothetical.getActivationUsage(BattleSide::ATTACKER, roundKey), 0);
	EXPECT_EQ(hypothetical.getActivationUsage(BattleSide::ATTACKER, battleKey), 2);
	EXPECT_EQ(hypothetical.getActivationCooldownReadyRound(BattleSide::ATTACKER, cooldownKey) - hypothetical.getRound(), 1);

	hypothetical.nextRound();
	EXPECT_EQ(hypothetical.getRound(), 4);
	EXPECT_EQ(hypothetical.getActivationCooldownReadyRound(BattleSide::ATTACKER, cooldownKey) - hypothetical.getRound(), 0);
}

TEST(FocusRulesTest, serializesActivationUsageAndCooldown)
{
	SideInBattle source(nullptr);
	source.activationUsage[{"focus-demo:resurrection", EActivationLimitScope::SIDE, -1, EActivationLimitPeriod::BATTLE}] = 1;
	source.activationUsage[{"focus-demo:thunderbolt", EActivationLimitScope::STACK, 8, EActivationLimitPeriod::ROUND}] = 1;
	source.activationCooldownReadyRound[{"focus-demo:enchanter", EActivationLimitScope::SIDE, -1, EActivationLimitPeriod::BATTLE}] = 5;
	source.activationCooldownReadyRound[{"focus-demo:stackSpell", EActivationLimitScope::STACK, 8, EActivationLimitPeriod::BATTLE}] = 7;

	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.oser & source;
	memory.iser.version = ESerializationVersion::CURRENT;

	SideInBattle restored(nullptr);
	memory.iser & restored;
	EXPECT_EQ(restored.activationUsage.at({"focus-demo:resurrection", EActivationLimitScope::SIDE, -1, EActivationLimitPeriod::BATTLE}), 1);
	EXPECT_EQ(restored.activationUsage.at({"focus-demo:thunderbolt", EActivationLimitScope::STACK, 8, EActivationLimitPeriod::ROUND}), 1);
	EXPECT_EQ(restored.activationCooldownReadyRound.at({"focus-demo:enchanter", EActivationLimitScope::SIDE, -1, EActivationLimitPeriod::BATTLE}), 5);
	EXPECT_EQ(restored.activationCooldownReadyRound.at({"focus-demo:stackSpell", EActivationLimitScope::STACK, 8, EActivationLimitPeriod::BATTLE}), 7);
}

TEST(FocusRulesTest, activationTurnReturnsControlTakesPrecedenceOverLegacyFallback)
{
	test::MockCStack stack;
	const auto bloodlust = SpellID(SpellID::BLOODLUST);
	auto source = activationSource(BonusType::SPELLCASTER, BonusSubtypeID(bloodlust));
	source->actionReturnsControl = true;
	source->activation->turn = ActivationTurn{false};
	stack.addNewBonus(source);

	EXPECT_FALSE(battle::actionReturnsControl(&stack, EActionType::MONSTER_SPELL, bloodlust));
	EXPECT_FALSE(battle::actionReturnsControl(&stack, EActionType::CATAPULT, SpellID::NONE));
}

TEST(FocusRulesTest, legacyReturnControlRemainsFallbackWithoutActivationTurn)
{
	test::MockCStack stack;
	const auto bloodlust = SpellID(SpellID::BLOODLUST);
	auto source = activationSource(BonusType::SPELLCASTER, BonusSubtypeID(bloodlust));
	source->actionReturnsControl = true;
	stack.addNewBonus(source);

	EXPECT_TRUE(battle::actionReturnsControl(&stack, EActionType::MONSTER_SPELL, bloodlust));
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

TEST(FocusRulesTest, integratedActionsRemainDistinctAndClassified)
{
	EXPECT_EQ(static_cast<int>(EActionType::CREATURE_ABILITY), 18);
	EXPECT_EQ(static_cast<int>(EActionType::WALK_AND_SHOOT), 19);
	for(auto type : {EActionType::CREATURE_ABILITY, EActionType::WALK_AND_SHOOT})
	{
		BattleAction source;
		source.actionType = type;
		source.activatedAbility = type == EActionType::CREATURE_ABILITY ? "test:ability" : "";
		source.aimToHex(BattleHex(90));
		CMemorySerializer memory;
		memory.oser.version = ESerializationVersion::CURRENT;
		memory.iser.version = ESerializationVersion::CURRENT;
		memory.oser & source;
		BattleAction restored;
		memory.iser & restored;
		EXPECT_EQ(restored.actionType, type);
		EXPECT_EQ(restored.activatedAbility, source.activatedAbility);
		ASSERT_EQ(restored.target.size(), 1);
		EXPECT_EQ(restored.target.front().hexValue, BattleHex(90));
		EXPECT_TRUE(restored.isUnitAction());
		EXPECT_NE(restored.actionType, EActionType::HERO_SPELL);
	}
	BattleAction heroSpell;
	heroSpell.actionType = EActionType::HERO_SPELL;
	EXPECT_FALSE(heroSpell.isUnitAction());
}

TEST(FocusRulesTest, preservesOfficialActionWithoutSelectedAbility)
{
	BattleAction legacy;
	legacy.actionType = EActionType::WALK_AND_ATTACK;
	legacy.aimToHex(BattleHex(90));
	legacy.aimToHex(BattleHex(93));
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::RECORD_TEXTS_METASTRING;
	memory.iser.version = ESerializationVersion::RECORD_TEXTS_METASTRING;
	memory.oser & legacy;
	BattleAction restored;
	memory.iser & restored;
	EXPECT_EQ(restored.actionType, EActionType::WALK_AND_ATTACK);
	EXPECT_TRUE(restored.activatedAbility.empty());
	ASSERT_EQ(restored.target.size(), 2);
	EXPECT_EQ(restored.target.back().hexValue, BattleHex(93));
}




TEST(FocusRulesTest, registryForkFormatRejectsPreviousForkFiles)
{
	const auto previousFork = static_cast<ESerializationVersion>(0x40000000 | (926 << 8) | 8);
	EXPECT_FALSE(isSupportedSerializationVersion(previousFork));
	EXPECT_TRUE(isSupportedSerializationVersion(ESerializationVersion::CURRENT));
	EXPECT_TRUE(isSupportedSerializationVersion(ESerializationVersion::UPSTREAM_CURRENT));
}
