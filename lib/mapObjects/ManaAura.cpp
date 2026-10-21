/*
 * ManaAura.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ManaAura.h"

#include "../IGameSettings.h"
#include "../json/JsonNode.h"

void ManaAura::normalize(JsonNode & config)
{
	if(!config.isStruct())
	{
		logGlobal->warn("%s: heroes.mageGuildManaAura must be an object; using defaults", config.getModScope());
		config = JsonNode();
		config.Struct();
	}
	auto integer = [&](const std::string & key, int fallback, int minimum, int maximum)
	{
		auto & value = config[key];
		const auto & input = std::as_const(value);
		if(!input.isNumber() || input.Float() < minimum || input.Float() > maximum || input.Float() != input.Integer())
		{
			logGlobal->warn("%s: heroes.mageGuildManaAura.%s is invalid; using %d", value.getModScope(), key, fallback);
			value.Integer() = fallback;
		}
	};
	integer("radius", 15, 2, 32767);
	integer("auraEdgePct", 10, 0, 100);
	integer("minimumNearPct", 10, 0, 100);
	integer("minimumEdgePct", 10, 0, 100);
	for(const auto & key : {"enabled", "mysticismSynergy"})
	{
		if(!config[key].isBool())
		{
			logGlobal->warn("%s: heroes.mageGuildManaAura.%s is invalid; using false", config[key].getModScope(), key);
			config[key].Bool() = false;
		}
	}
}

ManaAura::ManaAura(const IGameSettings & settings)
{
	const auto & config = settings.getValue(EGameSettings::HEROES_MAGE_GUILD_MANA_AURA);
	enabled = config["enabled"].Bool();
	mysticismSynergy = config["mysticismSynergy"].Bool();
	radius = config["radius"].Integer();
	edgePct = config["auraEdgePct"].Integer();
	minimumNearPct = config["minimumNearPct"].Integer();
	minimumEdgePct = config["minimumEdgePct"].Integer();
}

int ManaAura::distance(const int3 & source, const int3 & position) const
{
	if(!enabled || radius < 2 || source.z != position.z)
		return -1;
	const int64_t dx = static_cast<int64_t>(source.x) - position.x;
	const int64_t dy = static_cast<int64_t>(source.y) - position.y;
	if(std::abs(dx) > radius || std::abs(dy) > radius)
		return -1;
	const int64_t squared = dx * dx + dy * dy;
	if(squared > static_cast<int64_t>(radius) * radius)
		return -1;
	int low = 0;
	int high = radius;
	while(low < high)
	{
		const int middle = low + (high - low) / 2;
		if(static_cast<int64_t>(middle) * middle < squared)
			low = middle + 1;
		else
			high = middle;
	}
	return low;
}

int ManaAura::recovery(int missingMana, int maximumMana, int distance) const
{
	if(distance < 0 || distance > radius || radius < 2)
		return 0;
	if(distance == 0)
		return std::max(0, missingMana);
	const int64_t mainNumerator = static_cast<int64_t>(std::max(0, missingMana)) * (100LL * radius - (100 - edgePct) * distance);
	const int64_t mainDenominator = 100LL * radius;
	const int64_t minimumNumerator = static_cast<int64_t>(std::max(0, maximumMana))
		* (static_cast<int64_t>(minimumNearPct) * (radius - distance) + static_cast<int64_t>(minimumEdgePct) * (distance - 1));
	const int64_t minimumDenominator = 100LL * (radius - 1);
	return static_cast<int>(std::max((mainNumerator + mainDenominator - 1) / mainDenominator,
		(minimumNumerator + minimumDenominator - 1) / minimumDenominator));
}
