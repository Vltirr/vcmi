/*
 * LearnNowCastLaterTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/entities/hero/CHero.h"
#include <vcmi/HeroTypeService.h>

#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/networkPacks/ArtifactLocation.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/rewardable/Limiter.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{

class LearnNowCastLaterTest : public BattleTestFixture
{
public:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		overrideSettingBeforeInit(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER, true);
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		defenderSideHero->removeAllSpells();
		attackerSideHero->mana = 100;
	}

	void learn(SpellID spell)
	{
		gameHandler->changeSpells(attackerSideHero, true, {spell});
	}

	std::shared_ptr<Bonus> grantSpell(BonusType type, const BonusSubtypeID & subtype, int value = 1)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			BonusSource::OTHER, value, BonusSourceID(), subtype);
		attackerSideHero->addNewBonus(bonus);
		return bonus;
	}
};

TEST_F(LearnNowCastLaterTest, LearnsAboveWisdomAndUnlocksWithoutRelearning)
{
	const auto spell = SpellID(SpellID::IMPLOSION);
	ASSERT_EQ(attackerSideHero->maxCastableSpellLevel(), 2);
	ASSERT_TRUE(attackerSideHero->canLearnSpell(spell.toSpell()));
	learn(spell);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(spell));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(spell.toSpell()));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(spell).empty());
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	EXPECT_EQ(attackerSideHero->getSpellsInSpellbook().size(), 1u);
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(spell));
}

TEST_F(LearnNowCastLaterTest, LowLevelSpellStillUsable)
{
	learn(SpellID::MAGIC_ARROW);
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::MAGIC_ARROW).toSpell()));
}

TEST_F(LearnNowCastLaterTest, OrdinarySpellDamageBonusDoesNotBypassWisdom)
{
	learn(SpellID::INFERNO);
	grantSpell(BonusType::SPECIFIC_SPELL_DAMAGE, BonusSubtypeID(SpellID(SpellID::INFERNO)), 10);
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(SpellID(SpellID::INFERNO).toSpell()));
}

TEST_F(LearnNowCastLaterTest, ServerAllowsKnownSpecialtySpellWithoutWisdom)
{
	learn(SpellID::INFERNO);
	auto specialty = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPECIAL_SPELL_SCALING,
		BonusSource::HERO_SPECIAL, 5, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::INFERNO)));
	attackerSideHero->addNewBonus(specialty);
	startBattle();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	beginCombat();
	const auto mana = attackerSideHero->mana;
	EXPECT_TRUE(castAsHero(attackerSideHero, SpellID::INFERNO, target));
	EXPECT_LT(attackerSideHero->mana, mana);
	EXPECT_FALSE(server.castsOf(SpellID::INFERNO).empty());
}

class SpellSpecialistWisdomTest : public TinyMapGameTest, public ::testing::WithParamInterface<std::string> {};

TEST_P(SpellSpecialistWisdomTest, SpecialtyRemainsUsableWithoutWisdom)
{
	overrideSettingBeforeInit(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER, true);
	const auto * heroType = LIBRARY->heroTypes()->getByName("core:" + GetParam());
	ASSERT_NE(heroType, nullptr);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(heroType->getIndex()), PlayerColor(0));
	startWithMap(std::move(builder));
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	hero->setSecSkillLevel(SecondarySkill::WISDOM, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(hero->getHeroType()->spells.size(), 1u);
	const auto specialtySpell = *hero->getHeroType()->spells.begin();
	ASSERT_GT(specialtySpell.toSpell()->getLevel(), hero->maxCastableSpellLevel());
	ASSERT_TRUE(hero->spellbookContainsSpell(specialtySpell));
	EXPECT_TRUE(hero->canCastThisSpell(specialtySpell.toSpell()));
	EXPECT_FALSE(hero->getSourcesForSpell(specialtySpell).empty());

	ChangeSpells knowledge;
	knowledge.hid = hero->id;
	knowledge.learn = true;
	knowledge.spells = {SpellID::IMPLOSION};
	apply(knowledge);
	EXPECT_FALSE(hero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));

	CMemorySerializer memory;
	memory.oser & hero;
	memory.iser.cb = gameState().get();
	memory.iser.loadingGamestate = true;
	std::unique_ptr<CGHeroInstance> restored;
	memory.iser & restored;
	restored->restoreBonusSystem(*gameState());
	EXPECT_TRUE(restored->canCastThisSpell(specialtySpell.toSpell()));
	EXPECT_FALSE(restored->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	hero->removeAllSpells();
	EXPECT_FALSE(hero->canCastThisSpell(specialtySpell.toSpell()));
	hero->removeSpellbook();
	EXPECT_FALSE(hero->canCastThisSpell(specialtySpell.toSpell()));
}

INSTANTIATE_TEST_SUITE_P(Heroes, SpellSpecialistWisdomTest,
	::testing::Values("xyron", "solmyr", "deemer", "aislinn"));

TEST_F(LearnNowCastLaterTest, BlockedCombatSpellDoesNotIncreaseHeroMagicStrength)
{
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
	attackerSideHero->mana = attackerSideHero->manaLimit();
	learn(SpellID::IMPLOSION);
	EXPECT_DOUBLE_EQ(attackerSideHero->getMagicStrength(), 1.0);
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_GT(attackerSideHero->getMagicStrength(), 1.0);
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_DOUBLE_EQ(attackerSideHero->getMagicStrength(), 1.0);
	grantSpell(BonusType::SPELL, BonusSubtypeID(SpellID(SpellID::IMPLOSION)));
	EXPECT_GT(attackerSideHero->getMagicStrength(), 1.0);
}

TEST_F(LearnNowCastLaterTest, RewardLimiterAllowsLearningAboveWisdom)
{
	Rewardable::Limiter limiter;
	limiter.canLearnSpells = {SpellID::IMPLOSION};
	EXPECT_TRUE(limiter.heroAllowed(attackerSideHero));
	learn(SpellID::IMPLOSION);
	EXPECT_FALSE(limiter.heroAllowed(attackerSideHero));
}

TEST_F(LearnNowCastLaterTest, GuildTeachesOnlyBuiltLevelsWithoutWisdom)
{
	CGTownInstance town(gameState().get());
	town.ID = Obj::TOWN;
	town.subID = MapObjectSubID(FactionID::TOWER.getNum());
	town.addBuilding(BuildingID::MAGES_GUILD_4);
	town.spells.resize(GameConstants::SPELL_LEVELS);
	town.spells.at(3) = {SpellID::RESURRECTION};
	town.spells.at(4) = {SpellID::IMPLOSION};
	gameHandler->giveSpells(&town, attackerSideHero);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::RESURRECTION));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(SpellID(SpellID::RESURRECTION).toSpell()));
	town.addBuilding(BuildingID::MAGES_GUILD_5);
	gameHandler->giveSpells(&town, attackerSideHero);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
}

TEST_F(LearnNowCastLaterTest, LearningStillRequiresBook)
{
	defenderSideHero->removeSpellbook();
	EXPECT_FALSE(defenderSideHero->canLearnSpell(SpellID(SpellID::IMPLOSION).toSpell()));
}

TEST_F(LearnNowCastLaterTest, ExternalIndividualGrantBypassesWisdom)
{
	const auto spell = SpellID(SpellID::IMPLOSION);
	learn(spell);
	auto grant = grantSpell(BonusType::SPELL, BonusSubtypeID(spell));
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	attackerSideHero->removeBonus(grant);
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(spell));
}

TEST_F(LearnNowCastLaterTest, ExternalLevelGrantBypassesWisdom)
{
	const auto spell = SpellID(SpellID::IMPLOSION);
	auto grant = grantSpell(BonusType::SPELLS_OF_LEVEL, BonusCustomSubtype::spellLevel(5));
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell));
	attackerSideHero->removeBonus(grant);
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
}

TEST_F(LearnNowCastLaterTest, ScholarTransfersKnownBlockedSpellsWithinItsOwnLimit)
{
	attackerSideHero->setSecSkillLevel(SecondarySkill::SCHOLAR, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	learn(SpellID::RESURRECTION);
	learn(SpellID::IMPLOSION);
	ASSERT_FALSE(attackerSideHero->canCastThisSpell(SpellID(SpellID::RESURRECTION).toSpell()));
	gameHandler->useScholarSkill(attackerSideHero->id, defenderSideHero->id);
	EXPECT_TRUE(defenderSideHero->spellbookContainsSpell(SpellID::RESURRECTION));
	EXPECT_FALSE(defenderSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	EXPECT_FALSE(defenderSideHero->canCastThisSpell(SpellID(SpellID::RESURRECTION).toSpell()));
}

TEST_F(LearnNowCastLaterTest, ServerRejectsBlockedCombatSpellWithoutManaOrEffects)
{
	learn(SpellID::IMPLOSION);
	startBattle();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	beginCombat();
	const auto mana = attackerSideHero->mana;
	const auto count = target->getCount();
	BattleAction action;
	action.side = BattleSide::ATTACKER;
	action.actionType = EActionType::HERO_SPELL;
	action.spell = SpellID::IMPLOSION;
	action.aimToUnit(target);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(target->getCount(), count);
	EXPECT_TRUE(server.casts.empty());
}

TEST_F(LearnNowCastLaterTest, LearnedSpellPackRoundTripKeepsBlockedKnowledge)
{
	ChangeSpells original;
	original.hid = attackerSideHero->id;
	original.learn = true;
	original.spells = {SpellID::IMPLOSION};
	auto loaded = CMemorySerializer::deepCopy(original, gameState().get());
	gameHandler->sendAndApply(*loaded);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
}

TEST_F(LearnNowCastLaterTest, ServerRejectsBlockedAdventureSpellWithoutManaOrMovement)
{
	learn(SpellID::DIMENSION_DOOR);
	const auto mana = attackerSideHero->mana;
	const auto position = attackerSideHero->visitablePos();
	gameHandler->castSpell(attackerSideHero, SpellID::DIMENSION_DOOR, int3(6, 5, 0));
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(attackerSideHero->visitablePos(), position);
}

TEST_F(LearnNowCastLaterTest, ExternalSchoolGrantBypassesWisdom)
{
	const auto spell = SpellID(SpellID::IMPLOSION);
	ASSERT_FALSE(spell.toSpell()->schools.empty());
	auto grant = grantSpell(BonusType::SPELLS_OF_SCHOOL, BonusSubtypeID(*spell.toSpell()->schools.begin()));
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	attackerSideHero->removeBonus(grant);
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
}

TEST_F(LearnNowCastLaterTest, KnownHighLevelSpellSurvivesHeroSerialization)
{
	learn(SpellID::IMPLOSION);
	auto loaded = CMemorySerializer::deepCopy(*attackerSideHero, gameState().get());
	loaded->restoreBonusSystem(*gameState());
	EXPECT_TRUE(loaded->spellbookContainsSpell(SpellID::IMPLOSION));
	EXPECT_FALSE(loaded->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	loaded->setSecSkillLevel(SecondarySkill::WISDOM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(loaded->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
}

TEST_F(LearnNowCastLaterTest, AuroraBorealisStillRequiresGuildButTeachesAllLevels)
{
	CGTownInstance town(gameState().get());
	town.ID = Obj::TOWN;
	town.subID = MapObjectSubID(FactionID::CONFLUX.getNum());
	town.addBuilding(town.getTown()->getBuildingType(BuildingSubID::AURORA_BOREALIS));
	gameHandler->giveSpells(&town, attackerSideHero);
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	town.addBuilding(BuildingID::MAGES_GUILD_1);
	gameHandler->giveSpells(&town, attackerSideHero);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
}

TEST_F(LearnNowCastLaterTest, RewardGrantLearnsAboveWisdomWithoutDuplicatingKnowledge)
{
	struct RewardSource : CRewardableObject
	{
		using CRewardableObject::CRewardableObject;
		using Rewardable::Interface::grantRewardAfterLevelup;
	};
	RewardSource source(gameState().get());
	Rewardable::VisitInfo reward;
	reward.reward.spells = {SpellID::IMPLOSION};
	source.grantRewardAfterLevelup(*gameHandler, reward, attackerSideHero, {});
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	source.grantRewardAfterLevelup(*gameHandler, reward, attackerSideHero, {});
	EXPECT_EQ(attackerSideHero->getSpellsInSpellbook().size(), 1u);
}

TEST_F(LearnNowCastLaterTest, EagleEyePreBattlePreservesSourceLevelLimit)
{
	grantSpell(BonusType::LEARN_BATTLE_SPELL_LEVEL_LIMIT_PRE_BATTLE, BonusSubtypeID(), 4);
	grantSpell(BonusType::LEARN_BATTLE_SPELL_CHANCE_PRE_BATTLE, BonusSubtypeID(), 100);
	gameHandler->changeSpells(defenderSideHero, true, {SpellID::RESURRECTION, SpellID::IMPLOSION});
	startBattle();
	gameHandler->battles->tryLearnEnemySpellsPreBattle(battle(), BattleSide::ATTACKER);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::RESURRECTION));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(SpellID(SpellID::RESURRECTION).toSpell()));
}

TEST_F(LearnNowCastLaterTest, EagleEyePreBattleStillRequiresChance)
{
	grantSpell(BonusType::LEARN_BATTLE_SPELL_LEVEL_LIMIT_PRE_BATTLE, BonusSubtypeID(), 5);
	gameHandler->changeSpells(defenderSideHero, true, {SpellID::IMPLOSION});
	startBattle();
	gameHandler->battles->tryLearnEnemySpellsPreBattle(battle(), BattleSide::ATTACKER);
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
}

TEST_F(LearnNowCastLaterTest, EquippedScrollBypassesWisdomAndRemovalRestoresBlock)
{
	learn(SpellID::IMPLOSION);
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, SpellID::IMPLOSION, ArtifactPosition::MISC1));
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	gameHandler->removeArtifact(ArtifactLocation(attackerSideHero->id, ArtifactPosition::MISC1));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
}

TEST_F(LearnNowCastLaterTest, EquippedTomeBypassesWisdomWithoutTeachingSpell)
{
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode("core:tomeOfEarthMagic")), ArtifactPosition::MISC1);
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	gameHandler->removeArtifact(ArtifactLocation(attackerSideHero->id, ArtifactPosition::MISC1));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
}

class ClassicSpellLearningTest : public LearnNowCastLaterTest
{
public:
	void SetUp() override
	{
		LearnNowCastLaterTest::SetUp();
		map()->overrideGameSetting(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER, JsonNode(false));
	}
};

TEST_F(ClassicSpellLearningTest, WisdomLimitsLearningButNotPreconfiguredKnowledge)
{
	EXPECT_FALSE(LIBRARY->settingsHandler->getBoolean(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER));
	EXPECT_EQ(attackerSideHero->maxSpellLevel(), 2);
	EXPECT_TRUE(attackerSideHero->canLearnSpell(SpellID(SpellID::MAGIC_ARROW).toSpell()));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	learn(SpellID::IMPLOSION);
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	attackerSideHero->removeAllSpells();
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(attackerSideHero->canLearnSpell(SpellID(SpellID::IMPLOSION).toSpell()));
}

TEST_F(ClassicSpellLearningTest, GuildAndAuroraRespectWisdom)
{
	CGTownInstance town(gameState().get());
	town.ID = Obj::TOWN;
	town.subID = MapObjectSubID(FactionID::CONFLUX.getNum());
	town.addBuilding(BuildingID::MAGES_GUILD_1);
	town.addBuilding(BuildingID::MAGES_GUILD_5);
	town.spells.resize(GameConstants::SPELL_LEVELS);
	town.spells.at(0) = {SpellID::MAGIC_ARROW};
	town.spells.at(4) = {SpellID::IMPLOSION};
	gameHandler->giveSpells(&town, attackerSideHero);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::MAGIC_ARROW));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	town.addBuilding(town.getTown()->getBuildingType(BuildingSubID::AURORA_BOREALIS));
	gameHandler->giveSpells(&town, attackerSideHero);
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->giveSpells(&town, attackerSideHero);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
}

TEST_F(ClassicSpellLearningTest, ScholarRespectsRecipientsWisdom)
{
	attackerSideHero->setSecSkillLevel(SecondarySkill::SCHOLAR, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	learn(SpellID::RESURRECTION);
	gameHandler->useScholarSkill(attackerSideHero->id, defenderSideHero->id);
	EXPECT_FALSE(defenderSideHero->spellbookContainsSpell(SpellID::RESURRECTION));
	defenderSideHero->setSecSkillLevel(SecondarySkill::WISDOM, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	gameHandler->useScholarSkill(attackerSideHero->id, defenderSideHero->id);
	EXPECT_TRUE(defenderSideHero->spellbookContainsSpell(SpellID::RESURRECTION));
}

TEST_F(ClassicSpellLearningTest, ExternalGrantsStillBypassWisdom)
{
	auto grant = grantSpell(BonusType::SPELL, BonusSubtypeID(SpellID(SpellID::IMPLOSION)));
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	attackerSideHero->removeBonus(grant);
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
}

TEST_F(ClassicSpellLearningTest, EagleEyeKeepsItsOwnLearningLimitWithoutWisdom)
{
	grantSpell(BonusType::LEARN_BATTLE_SPELL_LEVEL_LIMIT_PRE_BATTLE, BonusSubtypeID(), 4);
	grantSpell(BonusType::LEARN_BATTLE_SPELL_CHANCE_PRE_BATTLE, BonusSubtypeID(), 100);
	gameHandler->changeSpells(defenderSideHero, true, {SpellID::RESURRECTION, SpellID::IMPLOSION});
	startBattle();
	gameHandler->battles->tryLearnEnemySpellsPreBattle(battle(), BattleSide::ATTACKER);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::RESURRECTION));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::RESURRECTION).toSpell()));
}

TEST_F(LearnNowCastLaterTest, ModSettingsCanEnableAndDisableTheRule)
{
	GameSettings settings;
	settings.loadBase(LIBRARY->settingsHandler->getFullConfig());
	EXPECT_FALSE(settings.getBoolean(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER));
	JsonNode overrides;
	overrides["spells"]["learnNowCastLater"].Bool() = true;
	settings.loadBase(overrides);
	EXPECT_TRUE(settings.getBoolean(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER));
	overrides["spells"]["learnNowCastLater"].Bool() = false;
	settings.loadBase(overrides);
	EXPECT_FALSE(settings.getBoolean(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER));
}

TEST_F(LearnNowCastLaterTest, SavedEffectiveSettingSurvivesDifferentModDefaults)
{
	for(const bool enabled : {false, true})
	{
		JsonNode config = LIBRARY->settingsHandler->getFullConfig();
		config["spells"]["learnNowCastLater"].Bool() = enabled;
		GameSettings original;
		original.loadBase(config);
		CMemorySerializer memory;
		memory.oser & original;
		config["spells"]["learnNowCastLater"].Bool() = !enabled;
		GameSettings restored;
		restored.loadBase(config);
		memory.iser.version = ESerializationVersion::CURRENT;
		memory.iser & restored;
		EXPECT_EQ(restored.getBoolean(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER), enabled);
		EXPECT_TRUE(memory.isFullyRead());
	}
}

TEST_F(LearnNowCastLaterTest, LegacySettingsDistinguishForkAndOfficialSaves)
{
	for(const auto version : {ESerializationVersion::CURRENT, ESerializationVersion::UPSTREAM_CURRENT})
	{
		CMemorySerializer memory;
		memory.oser & JsonNode();
		GameSettings restored;
		restored.loadBase(LIBRARY->settingsHandler->getFullConfig());
		memory.iser.version = version;
		memory.iser & restored;
		EXPECT_EQ(restored.getBoolean(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER), version == ESerializationVersion::CURRENT);
		EXPECT_TRUE(memory.isFullyRead());
	}
}

TEST_F(LearnNowCastLaterTest, InvalidSavedSettingReportsTheField)
{
	JsonNode overrides;
	overrides["spells"]["learnNowCastLater"].String() = "enabled";
	CMemorySerializer memory;
	memory.oser & overrides;
	GameSettings restored;
	restored.loadBase(LIBRARY->settingsHandler->getFullConfig());
	memory.iser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(memory.iser & restored, std::runtime_error);
}

}
