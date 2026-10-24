/*
 * SharedMageGuildsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "mock/TinyH3MBuilder.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/filesystem/Filesystem.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../server/CGameHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"

#include <cstdlib>

namespace
{
class ArchiveGameServer : public RecordingGameServer
{
public:
	std::vector<InfoWindow> notices;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * notice = dynamic_cast<InfoWindow *>(&pack))
			notices.push_back(*notice);
		RecordingGameServer::applyPack(pack);
	}
};

class SharedMageGuildsTest : public BattleTestFixture
{
public:
	ArchiveGameServer archiveServer;
	CGTownInstance * local = nullptr;
	CGTownInstance * remote = nullptr;
	CGTownInstance * enemy = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		overrideSettingBeforeInit(EGameSettings::TOWNS_SHARED_MAGE_GUILDS, true);
		overrideSettingBeforeInit(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER, true);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{CreatureID(0), 1}})
			.hero({7, 7, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{CreatureID(0), 1}})
			.town({12, 12, 0}, FactionID::CASTLE, PlayerColor(0))
			.town({20, 20, 0}, FactionID::TOWER, PlayerColor(0))
			.town({28, 28, 0}, FactionID::CONFLUX, PlayerColor(1));
		startWithMap(std::move(builder));
		archiveServer.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(archiveServer, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, 0, ChangeValueMode::ABSOLUTE);
		auto towns = findAll<CGTownInstance>();
		local = towns.at(0);
		remote = towns.at(1);
		enemy = towns.at(2);
		for(auto * town : towns)
		{
			town->removeAllBuildings();
			town->addBuilding(BuildingID::MAGES_GUILD_1);
			town->spells.assign(GameConstants::SPELL_LEVELS, {});
		}
		local->spells.at(0) = {SpellID::MAGIC_ARROW};
		remote->addBuilding(BuildingID::MAGES_GUILD_5);
		remote->spells.at(0) = {SpellID::MAGIC_ARROW, SpellID::HASTE};
		remote->spells.at(4) = {SpellID::IMPLOSION};
		enemy->spells.at(0) = {SpellID::SLOW};
	}
};

TEST_F(SharedMageGuildsTest, UnionPreservesSourcesAndExcludesOtherOwners)
{
	const auto archive = gameState()->getArcaneArchive(PlayerColor(0));
	EXPECT_EQ(archive.size(), 3u);
	EXPECT_EQ(archive.at(SpellID::MAGIC_ARROW).size(), 2u);
	EXPECT_FALSE(archive.count(SpellID::SLOW));
	gameHandler->giveSpells(local, attackerSideHero);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
}

TEST_F(SharedMageGuildsTest, DisabledUsesLocalGuildAndClassicLearningStillRequiresWisdom)
{
	map()->overrideGameSetting(EGameSettings::TOWNS_SHARED_MAGE_GUILDS, JsonNode(false));
	gameHandler->giveSpells(local, attackerSideHero);
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::HASTE));
	map()->overrideGameSetting(EGameSettings::TOWNS_SHARED_MAGE_GUILDS, JsonNode(true));
	map()->overrideGameSetting(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER, JsonNode(false));
	gameHandler->giveSpells(local, attackerSideHero);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::HASTE));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->giveSpells(local, attackerSideHero);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
}

TEST_F(SharedMageGuildsTest, ResearchAndOwnershipRefreshVisitorWithoutRemovingLearnedSpells)
{
	attackerSideHero->pos = local->visitablePos() + attackerSideHero->getVisitableOffset();
	local->setVisitingHero(attackerSideHero);
	gameHandler->giveSpells(local, attackerSideHero);
	SetResearchedSpells research;
	research.tid = remote->id;
	research.level = 0;
	research.spells = {SpellID::SLOW};
	research.accepted = true;
	gameHandler->sendAndApply(research);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::SLOW));
	EXPECT_FALSE(gameState()->getArcaneArchive(PlayerColor(0)).count(SpellID::HASTE));
	EXPECT_TRUE(gameState()->getArcaneArchive(PlayerColor(0)).count(SpellID::MAGIC_ARROW));
	SetObjectProperty property;
	property.id = remote->id;
	property.what = ObjProperty::OWNER;
	property.identifier = ObjPropertyID(PlayerColor(1));
	gameHandler->sendAndApply(property);
	EXPECT_FALSE(gameState()->getArcaneArchive(PlayerColor(0)).count(SpellID::IMPLOSION));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
}

TEST_F(SharedMageGuildsTest, UnbuiltLevelsExcludedAndAuroraContributionDisappearsWithOwnership)
{
	local->spells.at(4) = {SpellID::TOWN_PORTAL};
	EXPECT_FALSE(gameState()->getMageGuildSpells(local).count(SpellID::TOWN_PORTAL));
	gameHandler->setOwner(enemy, PlayerColor(0));
	enemy->addBuilding(enemy->getTown()->getBuildingType(BuildingSubID::AURORA_BOREALIS));
	const auto full = gameState()->getArcaneArchive(PlayerColor(0));
	EXPECT_GT(full.size(), 3u);
	gameHandler->setOwner(enemy, PlayerColor(1));
	EXPECT_EQ(gameState()->getArcaneArchive(PlayerColor(0)).size(), 3u);
}

TEST_F(SharedMageGuildsTest, PlayerCallbackCannotReadOpponentsArchive)
{
	revealMap(PlayerColor(0));
	const auto cb = makeCallback(PlayerColor(0));
	EXPECT_FALSE(cb->getArcaneArchive(PlayerColor(0)).empty());
	EXPECT_TRUE(cb->getArcaneArchive(PlayerColor(1)).empty());
}

TEST_F(SharedMageGuildsTest, EffectiveSettingsSurviveSaveAndLoad)
{
	const auto saved = gameState()->saveToMemory();
	gameState()->loadFromMemory(saved);
	EXPECT_TRUE(gameState()->getSettings().getBoolean(EGameSettings::TOWNS_SHARED_MAGE_GUILDS));
	EXPECT_FALSE(gameState()->getSettings().getBoolean(EGameSettings::SPELLS_ARCANE_ARCHIVE_PERSONAL));
}

TEST_F(SharedMageGuildsTest, RemoteConstructionTeachesGarrisonAndOnlyNewSpellsProduceNotice)
{
	gameState()->players.at(PlayerColor(0)).human = true;
	attackerSideHero->pos = local->visitablePos() + attackerSideHero->getVisitableOffset();
	local->setGarrisonedHero(attackerSideHero);
	remote->removeBuilding(BuildingID::MAGES_GUILD_5);
	NewStructures construction;
	construction.tid = remote->id;
	construction.bid = {BuildingID::MAGES_GUILD_5};
	gameHandler->sendAndApply(construction);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	ASSERT_EQ(archiveServer.notices.size(), 1u);
	EXPECT_EQ(archiveServer.notices.front().components.size(), 1u);
	construction.bid = {BuildingID::TOWN_HALL};
	gameHandler->sendAndApply(construction);
	EXPECT_EQ(archiveServer.notices.size(), 1u);
}

TEST_F(SharedMageGuildsTest, NewTurnRefreshesStationaryHeroWithoutRetroactiveNotice)
{
	gameState()->players.at(PlayerColor(0)).human = true;
	attackerSideHero->pos = local->visitablePos() + attackerSideHero->getVisitableOffset();
	local->setVisitingHero(attackerSideHero);
	NewTurn turn;
	turn.day = 2;
	gameHandler->sendAndApply(turn);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::IMPLOSION));
	EXPECT_TRUE(archiveServer.notices.empty());
}

TEST_F(SharedMageGuildsTest, LibraryContributesTheAdditionalOfferedSpell)
{
	remote->spells.at(0) = {SpellID::MAGIC_ARROW, SpellID::HASTE, SpellID::SLOW,
		SpellID::SHIELD, SpellID::STONE_SKIN, SpellID::BLESS};
	EXPECT_FALSE(gameState()->getArcaneArchive(PlayerColor(0)).count(SpellID::BLESS));
	NewStructures construction;
	construction.tid = remote->id;
	construction.bid = {remote->getTown()->getBuildingType(BuildingSubID::LIBRARY)};
	gameHandler->sendAndApply(construction);
	EXPECT_TRUE(gameState()->getArcaneArchive(PlayerColor(0)).count(SpellID::BLESS));
}

TEST_F(SharedMageGuildsTest, LargeAuroraAdditionUsesOneTextNotice)
{
	gameState()->players.at(PlayerColor(0)).human = true;
	NewStructures construction;
	construction.tid = enemy->id;
	construction.bid = {enemy->getTown()->getBuildingType(BuildingSubID::AURORA_BOREALIS)};
	gameHandler->sendAndApply(construction);
	archiveServer.notices.clear();
	gameHandler->setOwner(enemy, PlayerColor(0));
	ASSERT_EQ(archiveServer.notices.size(), 1u);
	EXPECT_TRUE(archiveServer.notices.front().components.empty());
	EXPECT_GT(gameState()->getArcaneArchive(PlayerColor(0)).size(), 8u);
}

TEST(ArcaneWebModuleTest, ActiveModulesMatchConfiguration)
{
	const char * selected = std::getenv("ARCANE_WEB_MODULES");
	if(!selected)
		GTEST_SKIP() << "Requires an isolated module-combination profile";
	const std::string combination(selected);
	ASSERT_EQ(combination.size(), 2u);
	const bool arcaneWeb = combination.at(0) == '1';
	const bool lncl = combination.at(1) == '1';
	EXPECT_EQ(LIBRARY->settingsHandler->getBoolean(EGameSettings::TOWNS_SHARED_MAGE_GUILDS), arcaneWeb);
	EXPECT_EQ(LIBRARY->settingsHandler->getBoolean(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER), lncl);
	EXPECT_EQ(LIBRARY->settingsHandler->getBoolean(EGameSettings::SPELLS_ARCANE_ARCHIVE_PERSONAL), arcaneWeb && lncl);
	const auto & mods = LIBRARY->modh->getActiveMods();
	EXPECT_EQ(vstd::contains(mods, std::string("further-horizons.arcane-web-lncl")), arcaneWeb && lncl);
	EXPECT_EQ(CResourceHandler::get()->existsResource(ResourcePath("SPRITES/arcaneArchive/library", EResType::IMAGE)), arcaneWeb);
}

TEST(ArcaneWebSettings, LegacySaveDoesNotActivateNewRulesFromInstalledModules)
{
	GameSettings restored;
	JsonNode enabled;
	enabled["towns"]["sharedMageGuilds"].Bool() = true;
	enabled["spells"]["arcaneArchivePersonal"].Bool() = true;
	restored.loadBase(enabled);
	CMemorySerializer memory;
	memory.oser & JsonNode();
	memory.iser.version = ESerializationVersion::UPSTREAM_CURRENT;
	memory.iser & restored;
	EXPECT_FALSE(restored.getBoolean(EGameSettings::TOWNS_SHARED_MAGE_GUILDS));
	EXPECT_FALSE(restored.getBoolean(EGameSettings::SPELLS_ARCANE_ARCHIVE_PERSONAL));
	EXPECT_TRUE(memory.isFullyRead());
}

TEST(ArcaneWebSettings, InvalidModValuesRetainBooleanDefaultsAndInvalidSaveIsRejected)
{
	for(const auto & path : {std::pair{"towns", "sharedMageGuilds"}, std::pair{"spells", "arcaneArchivePersonal"}})
	{
		GameSettings settings;
		settings.loadBase(LIBRARY->settingsHandler->getFullConfig());
		JsonNode invalid;
		invalid[path.first][path.second].String() = "enabled";
		settings.loadBase(invalid);
		EXPECT_FALSE(settings.getBoolean(EGameSettings::TOWNS_SHARED_MAGE_GUILDS));
		EXPECT_FALSE(settings.getBoolean(EGameSettings::SPELLS_ARCANE_ARCHIVE_PERSONAL));
		CMemorySerializer memory;
		memory.oser & invalid;
		memory.iser.version = ESerializationVersion::CURRENT;
		EXPECT_THROW(memory.iser & settings, std::runtime_error);
	}
}
}
