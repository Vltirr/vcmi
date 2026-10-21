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
#include "../../lib/CPlayerState.h"
#include "../../lib/gameState/TavernHeroesPool.h"
#include "../../lib/mapObjects/army/CSimpleArmy.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../server/CGameHandler.h"
#include "../../server/processors/NewTurnProcessor.h"
#ifdef ENABLE_NULLKILLER2_AI
#include "../../AI/Nullkiller2/Analyzers/HeroManager.h"
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
