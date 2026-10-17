/*
 * SerializationCompatibilityTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../lib/battle/SideInBattle.h"
#include "../lib/bonuses/BonusParameters.h"
#include "../lib/bonuses/Propagators.h"
#include "../lib/gameState/ReplayLog.h"
#include "../lib/networkPacks/PacksForClientBattle.h"
#include "../lib/serializer/CMemorySerializer.h"

namespace
{
ESerializationVersion forkVersion(ESerializationVersion upstream, int32_t revision)
{
	return static_cast<ESerializationVersion>(0x40000000 | (static_cast<int32_t>(upstream) << 8) | revision);
}
}

TEST(SerializationCompatibilityTest, distinguishesOfficialFieldsFromForkFields)
{
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::UPSTREAM_CURRENT;
	memory.iser.version = ESerializationVersion::UPSTREAM_CURRENT;
	SideInBattle source(nullptr);
	source.initialMana = 42;
	source.focusPoints = 13;
	source.initialArtifactCharges.push_back({ArtifactInstanceID(7), ArtifactPosition::SPELLBOOK, 3});
	memory.oser & source;

	SideInBattle restored(nullptr);
	restored.focusPoints = 99;
	memory.iser & restored;
	EXPECT_EQ(restored.initialMana, 42);
	EXPECT_EQ(restored.focusPoints, 0);
	ASSERT_EQ(restored.initialArtifactCharges.size(), 1);
	EXPECT_EQ(restored.initialArtifactCharges.front().charges, 3);
	EXPECT_FALSE(memory.iser.hasFeature(ESerializationVersion::BATTLE_FOCUS));
}

TEST(SerializationCompatibilityTest, olderUpstreamCounterDoesNotEnableNewOfficialFields)
{
	CMemorySerializer memory;
	memory.iser.version = forkVersion(ESerializationVersion::RECORD_TEXTS_METASTRING, 8);
	EXPECT_TRUE(memory.iser.hasFeature(ESerializationVersion::BATTLE_FOCUS));
	EXPECT_TRUE(memory.iser.hasFeature(ESerializationVersion::VERSIONED_REPLAY_CHAPTERS));
	EXPECT_FALSE(memory.iser.hasFeature(ESerializationVersion::BONUS_TARGET_SOURCE_ID));
	EXPECT_FALSE(memory.iser.hasFeature(ESerializationVersion::BATTLE_RESTORE_ARTIFACT_CHARGES));
}

TEST(SerializationCompatibilityTest, refusesFutureUpstreamOrForkFormatsIndependently)
{
	const auto futureUpstream = static_cast<ESerializationVersion>(static_cast<int32_t>(ESerializationVersion::UPSTREAM_CURRENT) + 1);
	EXPECT_TRUE(isSupportedSerializationVersion(ESerializationVersion::CURRENT));
	EXPECT_TRUE(isSupportedSerializationVersion(ESerializationVersion::UPSTREAM_CURRENT));
	EXPECT_FALSE(isSupportedSerializationVersion(futureUpstream));
	EXPECT_FALSE(isSupportedSerializationVersion(forkVersion(futureUpstream, 8)));
	EXPECT_FALSE(isSupportedSerializationVersion(forkVersion(ESerializationVersion::UPSTREAM_CURRENT, serializationForkVersion(ESerializationVersion::FORK_CURRENT) + 1)));
	EXPECT_FALSE(isSupportedSerializationVersion(ESerializationVersion::NONE));
}

TEST(SerializationCompatibilityTest, importedReplayRetainsItsFormatAfterResaving)
{
	ReplayChapter original;
	original.day = 3;
	original.gamestateSnapshot = {std::byte{1}, std::byte{2}};
	original.packs = {{std::byte{3}}};
	CMemorySerializer official;
	official.oser.version = ESerializationVersion::UPSTREAM_CURRENT;
	official.iser.version = ESerializationVersion::UPSTREAM_CURRENT;
	official.oser & original;
	ReplayChapter imported;
	official.iser & imported;
	ASSERT_EQ(imported.version, ESerializationVersion::UPSTREAM_CURRENT);

	CMemorySerializer fork;
	fork.oser & imported;
	ReplayChapter restored;
	fork.iser & restored;
	EXPECT_EQ(restored.version, imported.version);
	EXPECT_EQ(restored.gamestateSnapshot, original.gamestateSnapshot);
	EXPECT_EQ(restored.packs, original.packs);
}

TEST(SerializationCompatibilityTest, readsRecordedActionUsingItsSourceVersion)
{
	StartAction original;
	original.battleID = BattleID(1);
	original.ba.actionType = EActionType::DEFEND;
	original.ba.activatedAbility = "test:forkOnly";
	CMemorySerializer official;
	official.oser.version = ESerializationVersion::UPSTREAM_CURRENT;
	const CPackForClient * pointer = &original;
	official.oser & pointer;
	auto restored = ReplayPackSerializer::read(official.extractBuffer(), nullptr, ESerializationVersion::UPSTREAM_CURRENT);
	const auto * action = dynamic_cast<const StartAction *>(restored.get());
	ASSERT_NE(action, nullptr);
	EXPECT_EQ(action->ba.actionType, EActionType::DEFEND);
	EXPECT_TRUE(action->ba.activatedAbility.empty());
}

TEST(SerializationCompatibilityTest, refusesUnsupportedEmbeddedReplayFormat)
{
	ReplayChapter source;
	source.version = forkVersion(ESerializationVersion::UPSTREAM_CURRENT, serializationForkVersion(ESerializationVersion::FORK_CURRENT) + 1);
	CMemorySerializer memory;
	memory.oser & source;
	ReplayChapter restored;
	EXPECT_THROW(memory.iser & restored, std::runtime_error);
}

TEST(SerializationCompatibilityTest, acceptedForkReplayRetainsItsFormatAfterUpgrade)
{
	const auto acceptedVersion = forkVersion(ESerializationVersion::HOTA_HERO_LEVEL_OPTIONS, 8);
	ReplayChapter original;
	original.version = acceptedVersion;
	original.day = 4;
	original.gamestateSnapshot = {std::byte{1}, std::byte{2}};
	original.packs = {{std::byte{3}}};
	CMemorySerializer oldSave;
	oldSave.oser.version = acceptedVersion;
	oldSave.iser.version = acceptedVersion;
	oldSave.oser & original;
	ReplayChapter imported;
	oldSave.iser & imported;

	CMemorySerializer upgradedSave;
	upgradedSave.oser & imported;
	ReplayChapter restored;
	upgradedSave.iser & restored;
	EXPECT_EQ(restored.version, acceptedVersion);
	EXPECT_EQ(restored.gamestateSnapshot, original.gamestateSnapshot);
	EXPECT_EQ(restored.packs, original.packs);
}

TEST(SerializationCompatibilityTest, legacyEnchanterKeepsCooldownAndEqualWeight)
{
	CMemorySerializer memory;
	memory.oser.version = forkVersion(ESerializationVersion::HOTA_HERO_LEVEL_OPTIONS, 8);
	memory.iser.version = memory.oser.version;
	BonusParameters original(int32_t{3});
	memory.oser & original;
	BonusParameters restored;
	memory.iser & restored;
	EXPECT_EQ(restored.toEnchanter().cooldown, 3);
	EXPECT_EQ(restored.toEnchanter().weight, 1);
}
