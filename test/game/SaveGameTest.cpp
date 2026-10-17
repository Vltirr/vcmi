/*
 * SaveGameTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "GameStateTest.h"

#include "../../lib/campaign/CampaignState.h"
#include "../../lib/gameState/SaveMetadata.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapping/CMapHeader.h"
#include "../../lib/rmg/CMapGenOptions.h"
#include "../../lib/serializer/CLoadFile.h"
#include "../../lib/serializer/CLegacyLoadFile.h"
#include "../../lib/serializer/CSerializer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/CSaveFile.h"

class SaveGameTest : public GameStateTest
{
protected:
	boost::filesystem::path directory;

	void SetUp() override
	{
		GameStateTest::SetUp();
		directory = boost::filesystem::current_path() / boost::filesystem::unique_path("vcmi-savegame-%%%%-%%%%-%%%%-%%%%");
		ASSERT_TRUE(boost::filesystem::create_directory(directory));
	}

	void TearDown() override
	{
		boost::filesystem::remove_all(directory);
		GameStateTest::TearDown();
	}
};

TEST_F(SaveGameTest, savedGameIsRestored)
{
	startTestGame();
	const auto saveFile = directory / "test.vsgm1";

	{
		CMemorySerializer header;
		gameState->saveHeader(header.oser);
		CMemorySerializer game;
		gameState->saveGame(game.oser);

		CSaveFile save;
		save.addChunk(SaveFileFormat::CHUNK_METADATA, SaveMetadata::create(*gameState).toBytes());
		save.addChunk(SaveFileFormat::CHUNK_HEADER, header.extractBuffer());
		save.addChunk(SaveFileFormat::CHUNK_GAMESTATE, game.extractBuffer());
		save.write(saveFile);
	}

	CLoadFile loader(saveFile);

	const JsonNode metadata = SaveMetadata::parse(loader.readChunk(SaveFileFormat::CHUNK_METADATA), "test");
	EXPECT_EQ(metadata["map"]["width"].Integer(), map->width);
	EXPECT_EQ(metadata["game"]["humanPlayers"].Integer(), gameState->getStartInfo()->playerInfos.size());
	EXPECT_NO_THROW(SaveMetadata::verifyMods(metadata));

	CMapHeader loadedHeader;
	StartInfo loadedStartInfo;
	auto headerChunk = loader.openChunk(SaveFileFormat::CHUNK_HEADER, nullptr);
	headerChunk->iser & loadedHeader;
	headerChunk->iser & loadedStartInfo;
	EXPECT_TRUE(headerChunk->isFullyRead());
	EXPECT_EQ(loadedHeader.width, map->width);
	EXPECT_EQ(loadedStartInfo.mapname, gameState->getStartInfo()->mapname);

	auto loadedState = std::make_shared<CGameState>();
	loadedState->preInit(&services);
	auto gameChunk = loader.openChunk(SaveFileFormat::CHUNK_GAMESTATE, loadedState.get());
	loadedState->loadGame(gameChunk->iser);
	EXPECT_TRUE(gameChunk->isFullyRead());
	EXPECT_EQ(loadedState->day, gameState->day);
	EXPECT_EQ(loadedState->getMap().width, map->width);
	EXPECT_EQ(loadedState->players.size(), gameState->players.size());
}

TEST_F(SaveGameTest, invalidMetadataIsRejected)
{
	startTestGame();

	JsonNode metadata = SaveMetadata::create(*gameState);
	metadata["mods"]["testMod"]["name"].String() = "mod without version";
	EXPECT_THROW(SaveMetadata::parse(metadata.toBytes(), "test"), std::runtime_error);

	metadata = SaveMetadata::create(*gameState);
	metadata.Struct().erase("map");
	EXPECT_THROW(SaveMetadata::parse(metadata.toBytes(), "test"), std::runtime_error);
}

TEST_F(SaveGameTest, acceptedForkSaveUsesLegacyHeaderReader)
{
	startTestGame();
	const auto acceptedVersion = static_cast<ESerializationVersion>(0x40000000
		| (static_cast<int32_t>(ESerializationVersion::HOTA_HERO_LEVEL_OPTIONS) << 8) | 8);
	CMemorySerializer header;
	header.oser.version = acceptedVersion;
	gameState->saveHeader(header.oser);
	const auto data = header.extractBuffer();
	std::vector<std::byte> prefix;
	for(char c : SaveFileFormat::FILE_MAGIC)
		prefix.push_back(static_cast<std::byte>(c));
	SaveFileFormat::writeUInt32(prefix, static_cast<uint32_t>(acceptedVersion));
	const auto saveFile = directory / "accepted-fork.vsgm1";
	{
		std::ofstream file(saveFile.c_str(), std::ios::binary);
		file.exceptions(std::ios::failbit | std::ios::badbit);
		file.write(reinterpret_cast<const char *>(prefix.data()), prefix.size());
		file.write(SAVEGAME_MAGIC.data(), SAVEGAME_MAGIC.size());
		file.write(reinterpret_cast<const char *>(data.data()), data.size());
	}
	const auto version = CLoadFile::readVersion(saveFile);
	EXPECT_EQ(version, acceptedVersion);
	EXPECT_FALSE(serializationHasFeature(version, ESerializationVersion::SAVE_CHUNKS));
	CLegacyLoadFile loader(saveFile, nullptr);
	CMapHeader restoredHeader;
	StartInfo restoredStartInfo;
	loader.loadHeader(restoredHeader, restoredStartInfo);
	EXPECT_EQ(restoredHeader.width, map->width);
	EXPECT_EQ(restoredStartInfo.mapname, gameState->getStartInfo()->mapname);
}
