/*
 * AIUtilityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/AIUtility.h"

#include "nullkiller2/NullkillerTest.h"
#include "server/battles/BattleTestFixture.h"

#include "lib/CPlayerState.h"
#include "lib/gameState/CGameState.h"
#include "lib/gameState/QuestInfo.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/Quest.h"
#include "lib/spells/CSpell.h"
#include "server/CGameHandler.h"

namespace
{
const PlayerColor PLAYER = PlayerColor(0);
const int3 HERO_POS(5, 5, 0);
const int3 SEER_POS(7, 5, 0);

TinyH3M::TinyH3MBuilder makeQuestlessSeerMap()
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::HOTA);
	builder
		.hotaVersion(3)
		.size(36, false)
		.name("NK2QuestlessSeer")
		.playerActive(PLAYER)
		.randomTown({2, 2, 0}, PLAYER)
		.hero(HERO_POS, HeroTypeID(0), PLAYER)
		.seerHutMulti(SEER_POS, {}, {});

	return builder;
}

TinyH3M::TinyH3MBuilder makeQuestGuardMap()
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::HOTA);
	builder
		.hotaVersion(3)
		.size(36, false)
		.name("NK2KnownQuestGuard")
		.playerActive(PLAYER)
		.randomTown({2, 2, 0}, PLAYER)
		.hero(HERO_POS, HeroTypeID(0), PLAYER)
		.questGuard(SEER_POS, TinyH3M::TinyH3MBuilder::missionLevel(30));

	return builder;
}

class Nullkiller2_AIUtility : public NullkillerTest {};
}

TEST_F(Nullkiller2_AIUtility, trackedSeerWithoutActiveQuestIsNotVisitable)
{
	ASSERT_NO_FATAL_FAILURE(startWithMap(makeQuestlessSeerMap()));

	const auto * hero = findHeroAt(HERO_POS);
	const auto * seer = expectAt<SeerHut>(SEER_POS);
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(seer->getActiveQuest(), nullptr);

	gameState()->players.at(PLAYER).quests.push_back(seer->getQuestIdentity());

	const auto callback = makeCallback(PLAYER);
	auto gateway = std::make_unique<NK2AI::AIGateway>();
	gateway->initGameInterface(std::shared_ptr<Environment>(), callback);

	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, seer));
}

namespace
{
class Nullkiller2_ScrollValue : public BattleTestFixture {};
}

TEST_F(Nullkiller2_ScrollValue, EquippedScrollKeepsValueWhenItsLearnedSpellIsBlocked)
{
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	gameHandler->changeSpells(attackerSideHero, true, {SpellID::IMPLOSION});
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, SpellID::IMPLOSION, ArtifactPosition::MISC1));
	const auto * scroll = attackerSideHero->getArt(ArtifactPosition::MISC1);
	ASSERT_NE(scroll, nullptr);
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	EXPECT_GT(NK2AI::getArtifactScoreForHero(attackerSideHero, scroll), 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(NK2AI::getArtifactScoreForHero(attackerSideHero, scroll), 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_GT(NK2AI::getArtifactScoreForHero(attackerSideHero, scroll), 0);
}

TEST_F(Nullkiller2_ScrollValue, EquippedScrollKeepsValueWhenSpellIsNotLearned)
{
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, SpellID::IMPLOSION, ArtifactPosition::MISC1));
	const auto * scroll = attackerSideHero->getArt(ArtifactPosition::MISC1);
	ASSERT_NE(scroll, nullptr);
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	EXPECT_GT(NK2AI::getArtifactScoreForHero(attackerSideHero, scroll), 0);
}

// quest objects are never remembered as visited, so a known but unmet guard would be revisited every pass
TEST_F(Nullkiller2_AIUtility, knownUnmetQuestGuardIsNotVisitable)
{
	ASSERT_NO_FATAL_FAILURE(startWithMap(makeQuestGuardMap()));

	const auto * hero = findHeroAt(HERO_POS);
	const auto * guard = expectAt<QuestGuard>(SEER_POS);
	ASSERT_NE(hero, nullptr);

	const auto callback = makeCallback(PLAYER);
	auto gateway = std::make_unique<NK2AI::AIGateway>();
	gateway->initGameInterface(std::shared_ptr<Environment>(), callback);

	EXPECT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, guard));

	gameState()->players.at(PLAYER).quests.push_back(guard->getQuestIdentity());

	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, guard));
}
