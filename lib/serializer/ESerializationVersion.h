/*
 * ESerializationVersion.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

/// Official feature IDs retain their upstream values. Fork feature IDs start at 0x40000001.
/// CURRENT combines a format marker, a 16-bit upstream counter and an 8-bit fork counter.
/// When integrating upstream, advance UPSTREAM_CURRENT. For fork layout changes, append a
/// fork feature and advance FORK_CURRENT. Keep old gates and supply defaults for absent fields.
/// Always use h.hasFeature(Handler::Version::FEATURE), never compare encoded versions directly.
/// MINIMAL is the oldest official format accepted; unmarked prototype fork saves are unsupported.
enum class ESerializationVersion : int32_t
{
	NONE = 0,

	HOTA_MAP_STACK_COUNT = 893, // support Hota 1.7 stack count feature

	HOTA_MAP_FORMAT_EXTENSIONS, // support multiple Hota 1.7 map format features
	SPELL_RESEARCH_IMPROVEMENTS, // support counting past spell rerolls
	NAME_MAP_LAYERS, // name map layers
	HOTA_MAP_FORMAT_EXTENSIONS_2, // more Hota 1.7 map format features
	TIMER_MOVEMENT_POINTS, // movement points for timer
	DISABLE_TACTICS, // disable tactics
	REWARDABLE_EXTENSIONS_2, // movement points limiter for rewardables
	BONUS_TRIGGER, // bonus that allows triggered effects in combat
	CUSTOM_GARRISON_TITLE, // GarrisonDialog pack now has custom title parameter
	LUA_SCRIPTS,
	REWARDABLE_RESET_CALENDAR, // rewardable reset period split into days/weeks/months
	CONTROL_LOSS_TRACKING, // track when players ever controlled special defeat-condition objects
	QUEST_REWORK, // quest objects reshape: persist requiredKeys / allowedDifficulties limiter fields, quest-log identity (object or keymaster-colour type)
	HERO_SPECIALTY_ROUNDING, // DivideStackLevelUpdater carries hero level for H3-correct specialty rounding
	MAP_GEN_LEVEL_MAP_LAYERS, // CMapGenOptions per-level map layer IDs
	RETREAT_PERMISSION_BONUSES, // BATTLE_NO_FLEEING replaced by BATTLE_CAN_FLEE, escape tunnel provides bonus instead of hardcoded effect
	SCRIPT_VARIABLES, // per-map script variable storage (mod-namespaced key/value store)
	GAME_REPLAY_RECORDING, // recording of the game (and the battle ID counter it needs), stored in the savegame
	COMBAT_ABILITY_SCRIPTS, // combat abilities that became combat scripts are converted on load; spell effects and combat scripts share one registry, so bonus subtype saves the script as a string
	GAME_SESSION_DIRECTORY, // persistent per-game save directory and campaign start time
	SCENARIO_EVENT_JOURNAL, // per-player history of triggered scenario event messages and components
	MUTARE_DRAKE_OVERRIDE, // campaign header stores hero type override used for Mutare Drake crossover bonus targeting
	TOWN_NAME_TEXT_ID, // renaming a town registers the new name in the map text container instead of storing free-form text
	RECORD_TEXTS_METASTRING, // highscore scenario name and statistics map name are stored unresolved, to be rendered by the reader
	BONUS_TARGET_SOURCE_ID, // bonus can restrict PERCENT_TO_TARGET_TYPE to a single source ID
	SECONDARY_SKILL_OFFER_COOLDOWN, // hero stores level at which skills with offer cooldown were gained
	SEER_HUT_NAME_TEXT_ID, // seer name is stored as text identifier, and a quest stores map-defined timeout message
	SIMTURNS_CONTACT_STATUS, // game state stores players that still play simultaneous turns without contact
	BATTLE_RESTORE_ARTIFACT_CHARGES, // battle stores initial charges of artifacts to restore them if battle is cancelled
	CREATURE_REFUSED_JOINING_REMOVED, // wandering creatures no longer store a refused offer to join, the visit tracks it
	HOTA_HERO_LEVEL_OPTIONS, // persist HotA per-hero experience lock
	BOAT_LAST_HERO, // boat remembers the last hero that used it, so Summon Boat can prefer boat of the caster
	SAVE_CHUNKS, // saved game is stored as compressed chunks: metadata, map header, game state

	BATTLE_FOCUS = 0x40000001, // shared focus state and activation descriptors
	ACTIVATION_LIMITS, // shared per-side battle and round usage counters
	ABILITY_RETURNS_CONTROL, // per-bonus creature action turn behavior
	ACTIVATED_CREATURE_ABILITIES, // selected bonus identity in activation policies and battle actions
	GENERIC_ACTIVATION_POLICIES, // scoped turn limits and generic activation cooldowns
	FOCUS_MOBILE_SHOOTER_INTEGRATION, // distinct action IDs for mobile shooting and creature abilities
	MOBILE_SHOOTER_SUBTYPE, // distinguish explicitly enabled mobile shooters from legacy serialized SHOOTER subtypes
	VERSIONED_REPLAY_CHAPTERS, // preserve the format of embedded snapshots and packs

	RELEASE_170 = HOTA_MAP_STACK_COUNT,
	RELEASE_174 = CUSTOM_GARRISON_TITLE,

	MINIMAL = RELEASE_170,
	UPSTREAM_CURRENT = SAVE_CHUNKS,
	FORK_CURRENT = VERSIONED_REPLAY_CHAPTERS,
	CURRENT = 0x40000000 | (static_cast<int32_t>(UPSTREAM_CURRENT) << 8) | (static_cast<int32_t>(FORK_CURRENT) & 0xff),
};

static_assert(ESerializationVersion::MINIMAL <= ESerializationVersion::CURRENT, "Invalid serialization version definition!");

/// Fork files encode independent upstream and fork counters; feature keys use only one counter.
constexpr int32_t serializationUpstreamVersion(ESerializationVersion version)
{
	const auto value = static_cast<int32_t>(version);
	return value >= 0x40000000 ? (value >> 8) & 0xffff : value;
}

constexpr int32_t serializationForkVersion(ESerializationVersion version)
{
	const auto value = static_cast<int32_t>(version);
	return value >= 0x40000000 ? value & 0xff : 0;
}

constexpr bool serializationHasFeature(ESerializationVersion version, ESerializationVersion feature)
{
	if(static_cast<int32_t>(feature) >= 0x40000000)
		return serializationForkVersion(version) >= serializationForkVersion(feature);
	return serializationUpstreamVersion(version) >= static_cast<int32_t>(feature);
}

constexpr bool isSupportedSerializationVersion(ESerializationVersion version)
{
	const auto value = static_cast<int32_t>(version);
	const bool official = value < 0x40000000;
	const bool validFork = (value & 0x7f000000) == 0x40000000
		&& serializationForkVersion(version) >= serializationForkVersion(ESerializationVersion::VERSIONED_REPLAY_CHAPTERS)
		&& serializationForkVersion(version) <= serializationForkVersion(ESerializationVersion::FORK_CURRENT);
	return (official || validFork)
		&& serializationUpstreamVersion(version) >= static_cast<int32_t>(ESerializationVersion::MINIMAL)
		&& serializationUpstreamVersion(version) <= static_cast<int32_t>(ESerializationVersion::UPSTREAM_CURRENT);
}

static_assert(static_cast<int32_t>(ESerializationVersion::UPSTREAM_CURRENT) < 0x10000);
static_assert(static_cast<int32_t>(ESerializationVersion::FORK_CURRENT) < 0x40000100);
