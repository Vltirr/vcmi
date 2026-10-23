/*
 * HeroManaRegenerationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapObjects/ManaAura.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/gameState/TavernHeroesPool.h"
#include "../../lib/mapObjects/army/CSimpleArmy.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../server/CGameHandler.h"
#include "../../server/processors/NewTurnProcessor.h"
#include "../../lib/networkPacks/PacksForClient.h"
#ifdef ENABLE_NULLKILLER2_AI
#include "../../AI/Nullkiller2/Analyzers/HeroManager.h"
#include "../../AI/Nullkiller2/Helpers/ManaRest.h"
#include "../nullkiller2/NullkillerTest.h"
#include "../../AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "../../AI/Nullkiller2/Goals/Composition.h"
#include "../../AI/Nullkiller2/Goals/ExploreNeighbourTile.h"
#include "../../AI/Nullkiller2/Goals/ExchangeSwapTownHeroes.h"
#include "../../AI/Nullkiller2/Markers/ExplorationPoint.h"
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#endif

class HeroManaRegenerationTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void configurePlayer(PlayerSettings & settings) const override
	{
		settings.bonus = PlayerStartingBonus::GOLD;
	}

	CGHeroInstance * prepare(bool enabled = true, int knowledge = 5, HeroTypeID type = HeroTypeID(0))
	{
		overrideSettingBeforeInit(EGameSettings::HEROES_NATURAL_MANA_REGENERATION_USES_KNOWLEDGE, enabled);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.hero(int3(5, 5, 0), type, PlayerColor(0))
			.heroPrimary(1, 1, 1, static_cast<uint8_t>(knowledge)).heroSecondarySkills({});
		startWithMap(std::move(builder));
		auto * hero = findHeroByOwner(PlayerColor(0));
		hero->mana = 0;
		return hero;
	}

	std::shared_ptr<Bonus> add(CGHeroInstance * hero, BonusType type, int value,
		BonusSource source = BonusSource::ARTIFACT, BonusValueType valueType = BonusValueType::BASE_NUMBER)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, type, source, value, BonusSourceID());
		bonus->valType = valueType;
		hero->addNewBonus(bonus);
		return bonus;
	}
};

TEST_F(HeroManaRegenerationTest, disabledPreservesClassicBaseAndMysticism)
{
	auto * hero = prepare(false);
	EXPECT_EQ(hero->manaRegain(), 1);
	add(hero, BonusType::MANA_REGENERATION, 3);
	EXPECT_EQ(hero->manaRegain(), 4);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->manaRegain(), 7);
}

TEST_F(HeroManaRegenerationTest, knowledgeAndClassicMysticismAreAdditive)
{
	auto * hero = prepare();
	EXPECT_EQ(hero->manaRegain(), 5);
	for(int level = MasteryLevel::BASIC; level <= MasteryLevel::EXPERT; ++level)
	{
		hero->setSecSkillLevel(SecondarySkill::MYSTICISM, level, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(hero->manaRegain(), 5 + level + 1);
	}
	add(hero, BonusType::MANA_REGENERATION, 2);
	add(hero, BonusType::MANA_REGENERATION, 3);
	add(hero, BonusType::MANA_REGENERATION, 4);
	EXPECT_EQ(hero->manaRegain(), 18);
}

TEST_F(HeroManaRegenerationTest, knowledgeChangesInvalidateCachedRecovery)
{
	auto * hero = prepare();
	auto knowledge = add(hero, BonusType::PRIMARY_SKILL, 3);
	knowledge->subtype = BonusSubtypeID(PrimarySkill::KNOWLEDGE);
	hero->nodeHasChanged();
	EXPECT_EQ(hero->manaRegain(), 8);
	hero->removeBonus(knowledge);
	EXPECT_EQ(hero->manaRegain(), 5);
	hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->manaRegain(), 10);
}

TEST_F(HeroManaRegenerationTest, intelligenceDoesNotIncreaseNaturalBase)
{
	auto * hero = prepare(true, 10);
	hero->setSecSkillLevel(SecondarySkill::INTELLIGENCE, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->manaLimit(), 200);
	EXPECT_EQ(hero->manaRegain(), 10);
}

TEST_F(HeroManaRegenerationTest, sourceModifiersApplyToKnowledgeBaseAndExtras)
{
	auto * hero = prepare(true, 10);
	add(hero, BonusType::MANA_REGENERATION, 3);
	auto modifier = add(hero, BonusType::MANA_REGENERATION, 50, BonusSource::OTHER, BonusValueType::PERCENT_TO_TARGET_TYPE);
	modifier->targetSourceType = BonusSource::HERO_BASE_SKILL;
	modifier->targetSourceID = BonusSourceID(hero->id);
	hero->nodeHasChanged();
	EXPECT_EQ(hero->manaRegain(), 18);
	add(hero, BonusType::MANA_REGENERATION, 100, BonusSource::ARTIFACT, BonusValueType::PERCENT_TO_SOURCE);
	EXPECT_EQ(hero->manaRegain(), 21);
}

TEST_F(HeroManaRegenerationTest, percentageCompetesWithFlatRecovery)
{
	auto * hero = prepare(true, 5);
	auto percentage = add(hero, BonusType::MANA_PERCENTAGE_REGENERATION, 7);
	EXPECT_EQ(hero->manaRegain(), 5);
	percentage->val = 25;
	hero->nodeHasChanged();
	EXPECT_EQ(hero->manaRegain(), 12);
	percentage->val = 100;
	hero->nodeHasChanged();
	EXPECT_EQ(hero->getManaNewTurn(), hero->manaLimit());
}

TEST_F(HeroManaRegenerationTest, recoveryPreservesLimitsAndOverfilledMana)
{
	auto * hero = prepare();
	hero->mana = hero->manaLimit() - 1;
	EXPECT_EQ(hero->getManaNewTurn(), hero->manaLimit());
	hero->mana = hero->manaLimit();
	EXPECT_EQ(hero->getManaNewTurn(), hero->manaLimit());
	hero->mana += 20;
	EXPECT_EQ(hero->getManaNewTurn(), hero->mana);
	add(hero, BonusType::MANA_PERCENTAGE_REGENERATION, std::numeric_limits<int>::max());
	EXPECT_EQ(hero->getManaNewTurn(), hero->mana);
	hero->mana = 10;
	EXPECT_EQ(hero->getManaNewTurn(), hero->manaLimit());
}

TEST_F(HeroManaRegenerationTest, zeroNegativeAndHighEffectiveKnowledge)
{
	auto * hero = prepare();
	auto settings = LIBRARY->settingsHandler->getFullConfig();
	auto restore = settings;
	settings["heroes"]["minimalPrimarySkills"][PrimarySkill::KNOWLEDGE.getNum()].Integer() = -10;
	settings["heroes"]["maximalPrimarySkills"][PrimarySkill::KNOWLEDGE.getNum()].Integer() = 200;
	LIBRARY->settingsHandler->loadBase(settings);
	for(int value : {0, -5, 150})
	{
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, value, ChangeValueMode::ABSOLUTE);
		hero->nodeHasChanged();
		EXPECT_EQ(hero->manaRegain(), std::max(0, value));
	}
	LIBRARY->settingsHandler->loadBase(restore);
}

TEST_F(HeroManaRegenerationTest, serverUsesKnowledgeBeforeOneDayEffectsExpire)
{
	auto * hero = prepare();
	auto knowledge = add(hero, BonusType::PRIMARY_SKILL, 3, BonusSource::OBJECT_INSTANCE);
	knowledge->subtype = BonusSubtypeID(PrimarySkill::KNOWLEDGE);
	knowledge->duration = BonusDuration::ONE_DAY;
	hero->nodeHasChanged();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	NewTurnProcessor processor(&handler);
	processor.onNewTurn();
	EXPECT_EQ(hero->mana, 8);
	EXPECT_EQ(hero->manaRegain(), 5);
}

TEST_F(HeroManaRegenerationTest, fullStateSnapshotPreservesSettingsAndNextDayMana)
{
	auto * hero = prepare();
	const auto heroID = hero->id;
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	add(hero, BonusType::MANA_REGENERATION, 3);
	hero->mana = 7;
	auto snapshot = gameState()->saveToMemory();
	gameState()->loadFromMemory(std::move(snapshot));
	auto * restored = gameState()->getHero(heroID);
	ASSERT_NE(restored, nullptr);
	EXPECT_TRUE(gameState()->getSettings().getBoolean(EGameSettings::HEROES_NATURAL_MANA_REGENERATION_USES_KNOWLEDGE));
	EXPECT_EQ(restored->mana, 7);
	EXPECT_EQ(restored->getManaNewTurn(), 19);
}

TEST_F(HeroManaRegenerationTest, baseSourceModifiersAndSpecialtyExtrasRemainAdditive)
{
	auto * hero = prepare(true, 10);
	add(hero, BonusType::MANA_REGENERATION, 1, BonusSource::HERO_SPECIAL, BonusValueType::ADDITIVE_VALUE);
	add(hero, BonusType::MANA_REGENERATION, 50, BonusSource::HERO_BASE_SKILL, BonusValueType::PERCENT_TO_ALL);
	EXPECT_EQ(hero->manaRegain(), 16);
}

TEST_F(HeroManaRegenerationTest, mageGuildStillFillsReserve)
{
	overrideSettingBeforeInit(EGameSettings::HEROES_NATURAL_MANA_REGENERATION_USES_KNOWLEDGE, true);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.town(int3(12, 12, 0), FactionID::CASTLE, PlayerColor(0))
		.hero(int3(5, 5, 0), HeroTypeID(0), PlayerColor(0));
	startWithMap(std::move(builder));
	auto * hero = findHeroByOwner(PlayerColor(0));
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	town->addBuilding(BuildingID::MAGES_GUILD_1);
	hero->setVisitedTown(town, false);
	hero->mana = 0;
	EXPECT_EQ(hero->getManaNewTurn(), hero->manaLimit());
}

TEST_F(HeroManaRegenerationTest, tavernUsesKnowledgeAfterOneDayEffectsExpire)
{
	prepare();
	auto & pool = *gameState()->heroesPool;
	auto unused = pool.unusedHeroesFromPool();
	ASSERT_FALSE(unused.empty());
	auto * hero = unused.begin()->second;
	hero->removeBonuses(Selector::type()(BonusType::MANA_REGENERATION));
	hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 5, ChangeValueMode::ABSOLUTE);
	hero->mana = 0;
	auto knowledge = add(hero, BonusType::PRIMARY_SKILL, 3, BonusSource::OBJECT_INSTANCE);
	knowledge->subtype = BonusSubtypeID(PrimarySkill::KNOWLEDGE);
	knowledge->duration = BonusDuration::ONE_DAY;
	hero->nodeHasChanged();
	CSimpleArmy army;
	pool.setHeroForPlayer(PlayerColor(0), TavernHeroSlot::NATIVE, hero->getHeroTypeID(), army, TavernSlotRole::FULL_ARMY, false);
	pool.onNewDay();
	EXPECT_EQ(hero->mana, 5);
}

TEST_F(HeroManaRegenerationTest, aiHeroUsesSameServerRecovery)
{
	auto * hero = prepare();
	gameState()->players.at(PlayerColor(0)).human = false;
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	NewTurnProcessor processor(&handler);
	processor.onNewTurn();
	EXPECT_EQ(hero->mana, 5);
}

class KnowledgeMysticismTest : public HeroManaRegenerationTest
{
protected:
	CGHeroInstance * prepareRework(bool natural = false, int knowledge = 5, HeroTypeID type = HeroTypeID(0))
	{
		overrideSettingBeforeInit(EGameSettings::HEROES_MYSTICISM_USES_KNOWLEDGE, true);
		return prepare(natural, knowledge, type);
	}
};

TEST_F(KnowledgeMysticismTest, independentRecoveryReplacesNaturalBase)
{
	auto * hero = prepareRework();
	EXPECT_EQ(hero->manaRegain(), 1);
	for(int mastery = MasteryLevel::BASIC; mastery <= MasteryLevel::EXPERT; ++mastery)
	{
		hero->setSecSkillLevel(SecondarySkill::MYSTICISM, mastery, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(hero->manaRegain(), (mastery + 1) * 5);
	}
	add(hero, BonusType::MANA_REGENERATION, 3);
	EXPECT_EQ(hero->manaRegain(), 23);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->manaRegain(), 4);
}

TEST_F(KnowledgeMysticismTest, naturalKnowledgeDoesNotStackWithSkill)
{
	auto * hero = prepareRework(true, 10);
	EXPECT_EQ(hero->manaRegain(), 10);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->manaRegain(), 40);
	add(hero, BonusType::MANA_REGENERATION, 3);
	EXPECT_EQ(hero->manaRegain(), 43);
	hero->setSecSkillLevel(SecondarySkill::INTELLIGENCE, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->manaLimit(), 200);
	EXPECT_EQ(hero->manaRegain(), 43);
	hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 6, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->manaRegain(), 27);
}

TEST_F(KnowledgeMysticismTest, sourceModifiersAndPercentageKeepTheirTargets)
{
	auto * hero = prepareRework();
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	add(hero, BonusType::MANA_REGENERATION, 3);
	auto modifier = add(hero, BonusType::MANA_REGENERATION, 50, BonusSource::OTHER, BonusValueType::PERCENT_TO_TARGET_TYPE);
	modifier->targetSourceType = BonusSource::SECONDARY_SKILL;
	modifier->targetSourceID = BonusSourceID(SecondarySkill(SecondarySkill::MYSTICISM));
	hero->nodeHasChanged();
	EXPECT_EQ(hero->manaRegain(), 33);
	add(hero, BonusType::MANA_REGENERATION, 100, BonusSource::ARTIFACT, BonusValueType::PERCENT_TO_SOURCE);
	EXPECT_EQ(hero->manaRegain(), 36);
	auto percentage = add(hero, BonusType::MANA_PERCENTAGE_REGENERATION, 50);
	EXPECT_EQ(hero->manaRegain(), 36);
	percentage->val = 100;
	hero->nodeHasChanged();
	EXPECT_EQ(hero->manaRegain(), 50);
}

class KnowledgeMysticismSpecialistTest : public KnowledgeMysticismTest, public ::testing::WithParamInterface<int>
{
};

TEST_P(KnowledgeMysticismSpecialistTest, specialtyScalesOnlySkillAndRoundsHalvesUp)
{
	auto * hero = prepareRework(false, 5, HeroTypeID(GetParam()));
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	for(auto [heroLevel, expected] : std::vector<std::pair<int, int>>{{1, 26}, {2, 26}, {10, 30}, {20, 35}})
	{
		hero->level = heroLevel;
		hero->nodeHasChanged();
		EXPECT_EQ(hero->manaRegain(), expected) << heroLevel;
	}
	add(hero, BonusType::MANA_REGENERATION, 3);
	EXPECT_EQ(hero->manaRegain(), 38);
	hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->manaRegain(), 68);
}

INSTANTIATE_TEST_SUITE_P(BaseSpecialists, KnowledgeMysticismSpecialistTest, ::testing::Values(41, 58, 89, 121));

TEST_F(KnowledgeMysticismTest, gameSnapshotPreservesModeAndServerRecovery)
{
	auto * hero = prepareRework(false, 5, HeroTypeID(41));
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const auto id = hero->id;
	gameState()->players.at(PlayerColor(0)).human = false;
	auto snapshot = gameState()->saveToMemory();
	gameState()->loadFromMemory(std::move(snapshot));
	auto * restored = gameState()->getHero(id);
	ASSERT_NE(restored, nullptr);
	EXPECT_TRUE(gameState()->getSettings().getBoolean(EGameSettings::HEROES_MYSTICISM_USES_KNOWLEDGE));
	EXPECT_EQ(restored->manaRegain(), 26);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	NewTurnProcessor processor(&handler);
	processor.onNewTurn();
	EXPECT_EQ(restored->mana, 26);
	restored->mana = restored->manaLimit() + 10;
	EXPECT_EQ(restored->getManaNewTurn(), restored->mana);
}

TEST_F(KnowledgeMysticismTest, zeroKnowledgeDoesNotProduceNegativeRecovery)
{
	auto * hero = prepareRework();
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	auto settings = LIBRARY->settingsHandler->getFullConfig();
	auto restore = settings;
	settings["heroes"]["minimalPrimarySkills"][PrimarySkill::KNOWLEDGE.getNum()].Integer() = -10;
	LIBRARY->settingsHandler->loadBase(settings);
	for(int value : {0, -5})
	{
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, value, ChangeValueMode::ABSOLUTE);
		hero->nodeHasChanged();
		EXPECT_EQ(hero->manaRegain(), 0);
	}
	LIBRARY->settingsHandler->loadBase(restore);
}

#ifdef ENABLE_NULLKILLER2_AI
TEST_F(KnowledgeMysticismTest, mainRuleUsesKnowledgeTiersWithoutNewSkillBoost)
{
	auto * hero = prepareRework();
	NK2AI::SecondarySkillEvaluator evaluator({
		std::make_shared<NK2AI::SecondarySkillScoreMap>(std::map<SecondarySkill, float>{{SecondarySkill::MYSTICISM, -1}}),
		std::make_shared<NK2AI::ExistingSkillRule>(),
		std::make_shared<NK2AI::KnowledgeMysticismRule>()});
	for(auto [knowledge, expected] : std::vector<std::pair<int, float>>{{5, 1}, {10, 1}, {11, 2}, {15, 2}, {16, 2.5f}})
	{
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, knowledge, ChangeValueMode::ABSOLUTE);
		EXPECT_FLOAT_EQ(evaluator.evaluateSecSkill(hero, SecondarySkill::MYSTICISM), expected);
		hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		EXPECT_FLOAT_EQ(evaluator.evaluateSecSkill(hero, SecondarySkill::MYSTICISM), expected);
		hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	}
	auto callback = makeCallback(PlayerColor(0));
	NK2AI::Nullkiller ai;
	ai.cc = callback;
	NK2AI::HeroManager manager(callback.get(), &ai);
	EXPECT_FLOAT_EQ(manager.evaluateSecSkill(SecondarySkill::MYSTICISM, hero), 0);
	hero->patrol.patrolling = true;
	manager.update();
	EXPECT_FLOAT_EQ(manager.evaluateSecSkill(SecondarySkill::MYSTICISM, hero), 2.5f);
	EXPECT_EQ(manager.selectBestSkillIndex(NK2AI::HeroPtr(hero, callback.get()),
		{SecondarySkill::SORCERY, SecondarySkill::MYSTICISM}), 1);
}

TEST_F(HeroManaRegenerationTest, classicMainRuleRemainsUnchangedWithNaturalKnowledge)
{
	auto * hero = prepare(true, 20);
	float score = -1;
	NK2AI::KnowledgeMysticismRule().evaluateScore(hero, SecondarySkill::MYSTICISM, score);
	EXPECT_FLOAT_EQ(score, -1);
}
#endif

class MageGuildManaAuraTest : public HeroManaRegenerationTest
{
protected:
	CGHeroInstance * prepareAura(bool synergy = false, int knowledge = 10, bool secondTown = false, HeroTypeID type = HeroTypeID(0))
	{
		overrideSettingBeforeInit(EGameSettings::HEROES_MYSTICISM_USES_KNOWLEDGE, synergy);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, true).playerActive(PlayerColor(0))
			.town(int3(18, 18, 0), FactionID::CASTLE, PlayerColor(0))
			.hero(int3(5, 5, 0), type, PlayerColor(0))
			.heroPrimary(1, 1, 1, static_cast<uint8_t>(knowledge)).heroSecondarySkills({});
		if(secondTown)
			builder.town(int3(26, 18, 0), FactionID::CASTLE, PlayerColor(0));
		startWithMap(std::move(builder));
		JsonNode settings;
		settings["enabled"].Bool() = true;
		settings["mysticismSynergy"].Bool() = synergy;
		map()->overrideGameSetting(EGameSettings::HEROES_MAGE_GUILD_MANA_AURA, settings);
		for(auto * town : findAll<CGTownInstance>())
			town->addBuilding(BuildingID::MAGES_GUILD_1);
		auto * hero = findHeroByOwner(PlayerColor(0));
		hero->mana = 0;
		return hero;
	}

	int3 atDistance(int distance) const
	{
		return findFirst<CGTownInstance>()->visitablePos() + int3(-distance, 0, 0);
	}
};

TEST_F(MageGuildManaAuraTest, circleAndRoundedDistanceRespectRealBoundary)
{
	prepareAura();
	ManaAura aura(gameState()->getSettings());
	for(int radius : {10, 15, 20})
	{
		aura.radius = radius;
		EXPECT_EQ(aura.distance(int3(0), int3(radius, 0, 0)), radius);
		EXPECT_EQ(aura.distance(int3(0), int3(radius, 1, 0)), -1);
		EXPECT_EQ(aura.distance(int3(0), int3(radius - 1, 1, 0)), radius);
		EXPECT_EQ(aura.distance(int3(0), int3(1, 1, 0)), 2);
		EXPECT_EQ(aura.distance(int3(0), int3(1, 0, 1)), -1);
	}
}

TEST_F(MageGuildManaAuraTest, knowledgeBasedCurveMatchesKnowledgeTenAndFive)
{
	auto * hero = prepareAura();
	const std::vector<int> knowledgeTen{94, 88, 82, 76, 70, 59, 49, 40, 32, 25, 20, 16, 13, 11, 10};
	const std::vector<int> knowledgeFive{47, 44, 41, 38, 35, 30, 25, 20, 16, 13, 10, 8, 7, 6, 5};
	for(int distance = 1; distance <= 15; ++distance)
		EXPECT_EQ(hero->getManaNewTurnAt(atDistance(distance)), knowledgeTen.at(distance - 1)) << distance;
	hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 5, ChangeValueMode::ABSOLUTE);
	for(int distance = 1; distance <= 15; ++distance)
		EXPECT_EQ(hero->getManaNewTurnAt(atDistance(distance)), knowledgeFive.at(distance - 1)) << distance;
	hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
	hero->mana = 90;
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(1)), 100);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(15)), 100);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(16)), 91);
}

TEST_F(MageGuildManaAuraTest, intelligenceChangesCapacityButNotAuraRecovery)
{
	auto * hero = prepareAura(true);
	for(const auto & values : std::vector<std::array<int, 3>>{
		{MasteryLevel::NONE, 100, 125}, {MasteryLevel::BASIC, 125, 156},
		{MasteryLevel::ADVANCED, 150, 188}, {MasteryLevel::EXPERT, 200, 240}})
	{
		hero->setSecSkillLevel(SecondarySkill::INTELLIGENCE, values[0], ChangeValueMode::ABSOLUTE);
		hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		hero->mana = 0;
		ASSERT_EQ(hero->manaLimit(), values[1]);
		EXPECT_EQ(hero->getManaRecoveryAt(atDistance(15)).second, 10);
		EXPECT_EQ(hero->getManaRecoveryAt(atDistance(1)).second, 94);
		EXPECT_EQ(hero->getManaNewTurnAt(atDistance(0)), values[1]);
		hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		hero->mana = 48;
		const auto [balance, potential] = hero->getManaRecoveryAt(atDistance(15));
		EXPECT_EQ(balance, 98);
		EXPECT_EQ(potential, 50);
		EXPECT_EQ(hero->getManaRecoveryAt(atDistance(0)).second, values[2]);
	}
}

TEST_F(MageGuildManaAuraTest, auraUsesEffectiveKnowledgeWithoutOtherCapacityBonuses)
{
	auto * hero = prepareAura(true);
	add(hero, BonusType::MANA_PER_KNOWLEDGE_PERCENTAGE, 500);
	ASSERT_EQ(hero->manaLimit(), 150);
	EXPECT_EQ(hero->getManaRecoveryAt(atDistance(15)).second, 10);
	auto knowledge = add(hero, BonusType::PRIMARY_SKILL, 2);
	knowledge->subtype = BonusSubtypeID(PrimarySkill::KNOWLEDGE);
	hero->nodeHasChanged();
	ASSERT_EQ(hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 12);
	ASSERT_EQ(hero->manaLimit(), 180);
	EXPECT_EQ(hero->getManaRecoveryAt(atDistance(15)).second, 12);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getManaRecoveryAt(atDistance(15)).second, 60);
}

TEST_F(MageGuildManaAuraTest, curveScalesWithRadiusAndStaysBoundedAtSupportedLimits)
{
	prepareAura();
	ManaAura aura(gameState()->getSettings());
	for(int radius : {2, 10, 15, 20, 32767})
	{
		aura.radius = radius;
		for(int edge : {0, 10, 100})
		{
			aura.edgePct = edge;
			int previous = std::numeric_limits<int>::max();
			for(int distance = 0; distance <= radius; ++distance)
			{
				const int recovered = aura.recovery(std::numeric_limits<int>::max(), distance);
				EXPECT_GE(recovered, 0);
				EXPECT_LE(recovered, previous);
				previous = recovered;
			}
			EXPECT_EQ(aura.recovery(100, 0), 100);
			EXPECT_EQ(aura.recovery(100, radius), edge);
			EXPECT_EQ(aura.recovery(100, radius + 1), 0);
			EXPECT_EQ(aura.recovery(0, 1), 0);
		}
	}
}

TEST_F(MageGuildManaAuraTest, ownershipGuildAndLevelFilterSources)
{
	auto * hero = prepareAura();
	auto * town = findFirst<CGTownInstance>();
	const auto target = atDistance(5);
	EXPECT_EQ(hero->getManaNewTurnAt(target), 70);
	town->addBuilding(BuildingID::MAGES_GUILD_2);
	EXPECT_EQ(hero->getManaNewTurnAt(target), 70);
	SetObjectProperty ownership;
	ownership.id = town->id;
	ownership.what = ObjProperty::OWNER;
	ownership.identifier = ObjPropertyID(PlayerColor::NEUTRAL);
	gameState()->apply(ownership);
	EXPECT_EQ(hero->getManaNewTurnAt(target), 1);
	ownership.identifier = ObjPropertyID(PlayerColor(0));
	gameState()->apply(ownership);
	town->removeBuilding(BuildingID::MAGES_GUILD_2);
	town->removeBuilding(BuildingID::MAGES_GUILD_1);
	EXPECT_EQ(hero->getManaNewTurnAt(target), 1);
	town->addBuilding(BuildingID::MAGES_GUILD_1);
	EXPECT_EQ(hero->getManaNewTurnAt(target + int3(0, 0, 1)), 1);
}

TEST_F(MageGuildManaAuraTest, ordinaryBonusCanWinAndExcessSurvives)
{
	auto * hero = prepareAura();
	add(hero, BonusType::MANA_REGENERATION, 50);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(15)), 51);
	hero->mana = 130;
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(1)), 130);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(16)), 130);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(0)), 130);
}

TEST_F(MageGuildManaAuraTest, synergyUsesFinalBalancesAndExpertAccumulates)
{
	auto * hero = prepareAura(true);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	hero->mana = 95;
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(1)), 125);
	hero->mana = 120;
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(1)), 125);
	hero->mana = 130;
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(1)), 130);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(1)), 130);
	hero->mana = 95;
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(1)), 100);
}

TEST_F(MageGuildManaAuraTest, artifactExtrasAndSkillFractionsStaySeparate)
{
	auto * hero = prepareAura(true);
	add(hero, BonusType::MANA_REGENERATION, 3);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(10)), 38);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(10)), 51);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(10)), 68);
}

TEST_F(MageGuildManaAuraTest, cityPreviewMatchesActualCityAndRoundedOverchargeCeiling)
{
	auto * hero = prepareAura(true, 5);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(0)), 63);
	hero->setVisitedTown(findFirst<CGTownInstance>(), true);
	EXPECT_EQ(hero->getManaNewTurn(), 63);
	hero->mana = 60;
	EXPECT_EQ(hero->getManaNewTurn(), 63);
}

TEST_F(MageGuildManaAuraTest, disabledAndInvalidConfigurationHaveSafeDefaults)
{
	auto * hero = prepareAura();
	JsonNode config;
	config["enabled"].Bool() = false;
	config["radius"].Integer() = 1;
	config["auraEdgePct"].Integer() = -5;
	map()->overrideGameSetting(EGameSettings::HEROES_MAGE_GUILD_MANA_AURA, config);
	const ManaAura aura(gameState()->getSettings());
	EXPECT_EQ(aura.radius, 15);
	EXPECT_EQ(aura.edgePct, 10);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(5)), 1);
}

TEST_F(MageGuildManaAuraTest, previewDoesNotMoveHeroOrChangeMana)
{
	auto * hero = prepareAura();
	const auto position = hero->visitablePos();
	const auto visited = hero->getVisitedTown();
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(5)), 70);
	EXPECT_EQ(hero->visitablePos(), position);
	EXPECT_EQ(hero->getVisitedTown(), visited);
	EXPECT_EQ(hero->mana, 0);
}

TEST_F(MageGuildManaAuraTest, potentialUsesZeroManaAndHeroSpecificLimits)
{
	auto * hero = prepareAura(true);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	hero->mana = 72;
	const auto [edgeMana, edgePotential] = hero->getManaRecoveryAt(atDistance(15));
	EXPECT_EQ(edgeMana, 122);
	EXPECT_EQ(edgePotential, 50);
	const auto [outsideMana, outsidePotential] = hero->getManaRecoveryAt(atDistance(16));
	EXPECT_EQ(outsideMana, 100);
	EXPECT_EQ(outsidePotential, 40);
	for(int mana : {0, 1, 72, 100, 125, 130})
	{
		hero->mana = mana;
		for(const auto & point : std::vector<std::pair<int, int>>{{0, 125}, {1, 125}, {5, 110}, {10, 65}, {15, 50}, {16, 40}})
		{
			const auto [finalMana, potential] = hero->getManaRecoveryAt(atDistance(point.first));
			EXPECT_EQ(potential, point.second) << mana << ":" << point.first;
			EXPECT_GE(finalMana, mana);
			if(mana >= 125)
				EXPECT_EQ(finalMana, mana);
		}
		EXPECT_EQ(hero->mana, mana);
	}
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getManaRecoveryAt(atDistance(0)).second, 100);
	EXPECT_EQ(hero->getManaRecoveryAt(atDistance(10)).second, 25);
	add(hero, BonusType::MANA_REGENERATION, 200);
	EXPECT_EQ(hero->getManaRecoveryAt(atDistance(16)).second, 100);
}

#ifdef ENABLE_NULLKILLER2_AI
TEST(ManaRestPolicy, needsLowManaAndUsableMagic)
{
	EXPECT_TRUE(NK2AI::manaRestNeeded(39, 100, true, 5, 3));
	EXPECT_FALSE(NK2AI::manaRestNeeded(40, 100, true, 5, 3));
	EXPECT_FALSE(NK2AI::manaRestNeeded(10, 100, false, 5, 3));
	EXPECT_FALSE(NK2AI::manaRestNeeded(10, 100, true, 4, 3));
	EXPECT_FALSE(NK2AI::manaRestNeeded(10, 100, true, 5, 2));
}

TEST(ManaRestPolicy, comparesIncrementalGainAndMovementWithoutOverchargeIncentive)
{
	EXPECT_TRUE(NK2AI::manaRestWorthwhile(100, 30, 60, 500, 2000));
	EXPECT_FALSE(NK2AI::manaRestWorthwhile(100, 60, 65, 0, 2000));
	EXPECT_FALSE(NK2AI::manaRestWorthwhile(100, 30, 60, 501, 2000));
	EXPECT_FALSE(NK2AI::manaRestWorthwhile(100, 100, 125, 0, 2000));
	EXPECT_FALSE(NK2AI::manaRestWorthwhile(20, 0, 4, 0, 2000));
}
#endif

TEST_F(MageGuildManaAuraTest, newDayMatchesCurrentPositionAndSnapshotPreservesSynergy)
{
	auto * hero = prepareAura(true);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	hero->mana = 95;
	hero->setVisitedTown(findFirst<CGTownInstance>(), false);
	const auto id = hero->id;
	EXPECT_EQ(hero->getManaNewTurn(), 125);
	auto snapshot = gameState()->saveToMemory();
	gameState()->loadFromMemory(std::move(snapshot));
	auto * restored = gameState()->getHero(id);
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->mana, 95);
	EXPECT_EQ(restored->getManaNewTurn(), 125);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	NewTurnProcessor processor(&handler);
	processor.onNewTurn();
	EXPECT_EQ(restored->mana, 125);
}

TEST_F(MageGuildManaAuraTest, customParametersChangeCurveAndRadius)
{
	auto * hero = prepareAura();
	JsonNode config;
	config["enabled"].Bool() = true;
	config["radius"].Integer() = 10;
	config["auraEdgePct"].Integer() = 20;
	map()->overrideGameSetting(EGameSettings::HEROES_MAGE_GUILD_MANA_AURA, config);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(10)), 20);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(3)), 76);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(5)), 50);
	hero->mana = 90;
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(10)), 100);
	hero->mana = 0;
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(11)), 1);
}

#ifdef ENABLE_NULLKILLER2_AI
class ManaRestIntegrationTest : public NullkillerTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void configurePlayer(PlayerSettings & settings) const override { settings.bonus = PlayerStartingBonus::GOLD; }
	CGHeroInstance * prepareRest(int heroX = 12, bool enemyThreat = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.town(int3(18, 18, 0), FactionID::CASTLE, PlayerColor(0))
			.hero(int3(heroX, 18, 0), HeroTypeID(HeroTypeID::decode("halon")), PlayerColor(0))
			.heroPrimary(1, 1, 1, 10).heroSecondarySkills({});
		if(enemyThreat)
			builder.playerActive(PlayerColor(1))
				.hero({15, 20, 0}, HeroTypeID(0), PlayerColor(1))
				.heroGarrison({{CreatureID(CreatureID::decode("archangel")), 100}});
		startWithMap(std::move(builder));
		findFirst<CGTownInstance>()->addBuilding(BuildingID::MAGES_GUILD_1);
		JsonNode config;
		config["enabled"].Bool() = true;
		config["radius"].Integer() = 10;
		map()->overrideGameSetting(EGameSettings::HEROES_MAGE_GUILD_MANA_AURA, config);
		auto * hero = findHeroByOwner(PlayerColor(0));
		hero->mana = 0;
		hero->setMovementPoints(hero->movementPointsLimit() / 5);
		for(const auto spell : {SpellID::MAGIC_ARROW, SpellID::HASTE, SpellID::SLOW})
			hero->addSpellToSpellbook(SpellID(spell));
		revealMap(PlayerColor(0));
		return hero;
	}

	std::shared_ptr<NK2AI::Goals::ExecuteHeroChain> normalTask(CGHeroInstance * hero)
	{
		NK2AI::AIPath path;
		path.targetObjectDanger = 0;
		path.armyLoss = 0;
		path.targetObjectArmyLoss = 0;
		path.targetHero = hero;
		path.heroArmy = hero;
		path.chainMask = 1;
		path.exchangeCount = 1;
		const auto entrance = findFirst<CGTownInstance>()->visitablePos();
		auto node = pathNode(*hero, entrance + int3(-10, 0, 0));
		node.cost = 0.2f;
		path.nodes.push_back(node);
		auto task = std::make_shared<NK2AI::Goals::ExecuteHeroChain>(path);
		task->setpriority(0.01f);
		return task;
	}

	std::pair<NK2AI::Goals::TTask, int> plannedExploration(NK2AI::Nullkiller & ai, CGHeroInstance * hero)
	{
		const auto move = normalTask(hero);
		NK2AI::Goals::TGoalVec tasks{sptr(NK2AI::Goals::Composition()
			.addNext(NK2AI::Goals::ExplorationPoint(move->getPath().targetTile(), 5))
			.addNext(*move)
			.addNext(NK2AI::Goals::ExploreNeighbourTile(hero, 5)))};
		return NK2AI::NullkillerTestAccess::selectTask(ai, std::move(tasks));
	}
};

TEST_F(ManaRestIntegrationTest, choosesCurrentAuraPositionAndLocksHeroUntilNextTurn)
{
	std::shared_lock stateLock(CGameState::mutex);
	auto * hero = prepareRest();
	const auto position = hero->visitablePos();
	const auto gateway = makeGateway(PlayerColor(0));
	NK2AI::NullkillerTestAccess::prepareState(*gateway->nullkiller);
	ASSERT_TRUE(hero->hasSpellbook());
	ASSERT_EQ(gateway->nullkiller->getHeroLockedReason(hero), NK2AI::HeroLockedReason::NOT_LOCKED);
	EXPECT_TRUE(NK2AI::NullkillerTestAccess::tryRestForMana(*gateway->nullkiller, normalTask(hero), NK2AI::PriorityEvaluator::EXPLORE_AND_GATHER));
	EXPECT_EQ(hero->visitablePos(), position);
	EXPECT_EQ(hero->mana, 0);
	EXPECT_EQ(gateway->nullkiller->getHeroLockedReason(hero), NK2AI::HeroLockedReason::MANA_REST);
}

TEST_F(ManaRestIntegrationTest, urgentOrMoreValuableNormalActionPreventsRest)
{
	std::shared_lock stateLock(CGameState::mutex);
	auto * hero = prepareRest();
	const auto gateway = makeGateway(PlayerColor(0));
	NK2AI::NullkillerTestAccess::prepareState(*gateway->nullkiller);
	auto task = normalTask(hero);
	EXPECT_FALSE(NK2AI::NullkillerTestAccess::tryRestForMana(*gateway->nullkiller, task, NK2AI::PriorityEvaluator::KILL));
	task->setpriority(100000.0f);
	EXPECT_FALSE(NK2AI::NullkillerTestAccess::tryRestForMana(*gateway->nullkiller, task, NK2AI::PriorityEvaluator::EXPLORE_AND_GATHER));
	EXPECT_EQ(gateway->nullkiller->getHeroLockedReason(hero), NK2AI::HeroLockedReason::NOT_LOCKED);
}

TEST_F(ManaRestIntegrationTest, plannerSelectedExplorationCanRestAndResumeAfterRecovery)
{
	std::shared_lock stateLock(CGameState::mutex);
	auto * hero = prepareRest(16);
	const auto gateway = makeGateway(PlayerColor(0));
	auto & ai = *gateway->nullkiller;
	NK2AI::NullkillerTestAccess::prepareState(ai);
	const auto [task, tier] = plannedExploration(ai, hero);
	ASSERT_NE(task, nullptr);
	ASSERT_EQ(tier, NK2AI::PriorityEvaluator::ESCAPE);
	ASSERT_GT(task->priority, 1.0f);
	EXPECT_TRUE(NK2AI::NullkillerTestAccess::tryRestForMana(ai, task, tier));
	EXPECT_EQ(ai.getHeroLockedReason(hero), NK2AI::HeroLockedReason::MANA_REST);

	hero->mana = hero->getManaNewTurn();
	NK2AI::NullkillerTestAccess::prepareState(ai);
	EXPECT_EQ(ai.getHeroLockedReason(hero), NK2AI::HeroLockedReason::NOT_LOCKED);
	const auto [nextTask, nextTier] = plannedExploration(ai, hero);
	ASSERT_NE(nextTask, nullptr);
	EXPECT_FALSE(NK2AI::NullkillerTestAccess::tryRestForMana(ai, nextTask, nextTier));
	EXPECT_EQ(ai.getHeroLockedReason(hero), NK2AI::HeroLockedReason::NOT_LOCKED);
}

TEST_F(ManaRestIntegrationTest, plannerSelectedMoreValuableActionKeepsItsPriority)
{
	std::shared_lock stateLock(CGameState::mutex);
	auto * hero = prepareRest(16);
	const auto gateway = makeGateway(PlayerColor(0));
	auto & ai = *gateway->nullkiller;
	NK2AI::NullkillerTestAccess::prepareState(ai);
	const auto [task, tier] = plannedExploration(ai, hero);
	ASSERT_NE(task, nullptr);
	task->priority = 100000.0f;
	EXPECT_FALSE(NK2AI::NullkillerTestAccess::tryRestForMana(ai, task, tier));
	EXPECT_EQ(ai.getHeroLockedReason(hero), NK2AI::HeroLockedReason::NOT_LOCKED);
}

TEST_F(ManaRestIntegrationTest, combatPathIsNotReplacedEvenInOrdinaryPriorityTier)
{
	std::shared_lock stateLock(CGameState::mutex);
	auto * hero = prepareRest(16);
	const auto gateway = makeGateway(PlayerColor(0));
	NK2AI::NullkillerTestAccess::prepareState(*gateway->nullkiller);
	auto path = normalTask(hero)->getPath();
	path.targetObjectDanger = 100;
	const auto task = std::make_shared<NK2AI::Goals::ExecuteHeroChain>(path);
	EXPECT_FALSE(NK2AI::NullkillerTestAccess::tryRestForMana(*gateway->nullkiller, task, NK2AI::PriorityEvaluator::ESCAPE));
	EXPECT_EQ(gateway->nullkiller->getHeroLockedReason(hero), NK2AI::HeroLockedReason::NOT_LOCKED);
}

TEST_F(ManaRestIntegrationTest, compoundTownDefenceIsNotReplacedWithRest)
{
	std::shared_lock stateLock(CGameState::mutex);
	auto * hero = prepareRest(16);
	const auto gateway = makeGateway(PlayerColor(0));
	NK2AI::NullkillerTestAccess::prepareState(*gateway->nullkiller);
	const auto task = taskptr(NK2AI::Goals::Composition()
		.addNext(*normalTask(hero))
		.addNext(NK2AI::Goals::ExchangeSwapTownHeroes(findFirst<CGTownInstance>(), hero, NK2AI::HeroLockedReason::DEFENCE)));
	EXPECT_FALSE(NK2AI::NullkillerTestAccess::tryRestForMana(*gateway->nullkiller, task, NK2AI::PriorityEvaluator::ESCAPE));
	EXPECT_EQ(gateway->nullkiller->getHeroLockedReason(hero), NK2AI::HeroLockedReason::NOT_LOCKED);
}

TEST_F(ManaRestIntegrationTest, nearbyEnemyThreatPreventsRestInEscapeTier)
{
	std::shared_lock stateLock(CGameState::mutex);
	auto * hero = prepareRest(16, true);
	const auto gateway = makeGateway(PlayerColor(0));
	auto & ai = *gateway->nullkiller;
	NK2AI::NullkillerTestAccess::prepareState(ai);
	const auto & threat = ai.dangerHitMap->getTileThreat(hero->visitablePos()).maximumDanger;
	ASSERT_GT(threat.danger, 0);
	ASSERT_LE(threat.turn, 1);
	EXPECT_FALSE(NK2AI::NullkillerTestAccess::tryRestForMana(ai, normalTask(hero), NK2AI::PriorityEvaluator::ESCAPE));
	EXPECT_EQ(ai.getHeroLockedReason(hero), NK2AI::HeroLockedReason::NOT_LOCKED);
}
#endif

TEST_F(MageGuildManaAuraTest, overlappingSourcesChooseBestWithoutAdding)
{
	auto * hero = prepareAura(false, 10, true);
	const auto target = findFirst<CGTownInstance>()->visitablePos() + int3(5, 0, 0);
	EXPECT_EQ(hero->getManaNewTurnAt(target), 82);
}

TEST_F(MageGuildManaAuraTest, specialistFractionIncludesPercentageAndFixedRecovery)
{
	auto * hero = prepareAura(true, 5, false, HeroTypeID(HeroTypeID::decode("halon")));
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->manaRegain(), 26);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(10)), 39);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(0)), 63);
}

TEST_F(MageGuildManaAuraTest, tavernHeroDoesNotReceiveAuraFromRetainedCoordinates)
{
	prepareAura();
	auto unused = gameState()->heroesPool->unusedHeroesFromPool();
	ASSERT_FALSE(unused.empty());
	auto * hero = unused.begin()->second;
	hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	hero->mana = 0;
	hero->setAnchorPos(hero->convertFromVisitablePos(atDistance(5)));
	EXPECT_EQ(hero->getManaNewTurn(), hero->manaRegain());
}

TEST_F(MageGuildManaAuraTest, sourceModifiersApplyToMysticismAndExtrasSeparately)
{
	auto * hero = prepareAura(true);
	hero->setSecSkillLevel(SecondarySkill::MYSTICISM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	add(hero, BonusType::MANA_REGENERATION, 3);
	auto modifier = add(hero, BonusType::MANA_REGENERATION, 50, BonusSource::OTHER, BonusValueType::PERCENT_TO_TARGET_TYPE);
	modifier->targetSourceType = BonusSource::SECONDARY_SKILL;
	modifier->targetSourceID = BonusSourceID(SecondarySkill(SecondarySkill::MYSTICISM));
	hero->nodeHasChanged();
	EXPECT_EQ(hero->manaRegain(), 63);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(10)), 88);
	add(hero, BonusType::MANA_REGENERATION, 100, BonusSource::ARTIFACT, BonusValueType::PERCENT_TO_SOURCE);
	EXPECT_EQ(hero->getManaNewTurnAt(atDistance(10)), 91);
}

TEST_F(HeroManaRegenerationTest, alliedGuildPreviewPreservesTraditionalRefillWithoutEmittingAura)
{
	const PlayerColor player(0);
	const PlayerColor ally(1);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player).playerActive(ally)
		.town(int3(18, 18, 0), FactionID::CASTLE, ally)
		.hero(int3(5, 5, 0), HeroTypeID(0), player)
		.heroPrimary(1, 1, 1, 10).heroSecondarySkills({});
	startWithMap(std::move(builder));
	auto * hero = findHeroByOwner(player);
	auto * town = findFirst<CGTownInstance>();
	hero->mana = 0;
	town->addBuilding(BuildingID::MAGES_GUILD_1);
	EXPECT_EQ(hero->getManaNewTurnAt(town->visitablePos()), hero->manaRegain());
	auto & team = gameState()->teams.at(gameState()->players.at(player).team);
	gameState()->teams.at(gameState()->players.at(ally).team).players.erase(ally);
	team.players.insert(ally);
	gameState()->players.at(ally).team = team.id;
	revealMap(player);
	EXPECT_EQ(hero->getManaNewTurnAt(town->visitablePos()), hero->manaLimit());
	hero->setVisitedTown(town, false);
	EXPECT_EQ(hero->getManaNewTurnAt(town->visitablePos()), hero->getManaNewTurn());
	hero->setVisitedTown(nullptr, false);
	JsonNode config;
	config["enabled"].Bool() = true;
	map()->overrideGameSetting(EGameSettings::HEROES_MAGE_GUILD_MANA_AURA, config);
	EXPECT_EQ(hero->getManaNewTurnAt(town->visitablePos()), hero->manaLimit());
	EXPECT_EQ(hero->getManaNewTurnAt(town->visitablePos() + int3(-1, 0, 0)), hero->manaRegain());
}

#ifdef ENABLE_NULLKILLER2_AI
TEST_F(ManaRestIntegrationTest, artifactGrantedCombatSpellsCountWithoutLearnedSpells)
{
	std::shared_lock stateLock(CGameState::mutex);
	auto * hero = prepareRest();
	hero->removeAllSpells();
	hero->putArtifact(ArtifactPosition::MISC1, map()->createArtifact(ArtifactID(ArtifactID::decode("tomeOfAirMagic"))));
	ASSERT_TRUE(hero->getSpellsInSpellbook().empty());
	ASSERT_TRUE(hero->hasSpellbook());
	const auto gateway = makeGateway(PlayerColor(0));
	NK2AI::NullkillerTestAccess::prepareState(*gateway->nullkiller);
	EXPECT_TRUE(NK2AI::NullkillerTestAccess::tryRestForMana(*gateway->nullkiller, normalTask(hero), NK2AI::PriorityEvaluator::EXPLORE_AND_GATHER));
	EXPECT_EQ(gateway->nullkiller->getHeroLockedReason(hero), NK2AI::HeroLockedReason::MANA_REST);
}
#endif
