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
#include <boost/multiprecision/cpp_int.hpp>

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

int ManaAura::recovery(int baseMana, int distance) const
{
	if(distance < 0 || distance > radius || radius < 2)
		return 0;
	int64_t denominator = 100LL * radius;
	int64_t numerator = denominator - static_cast<int64_t>(100 - edgePct) * distance;
	if(3 * distance > radius)
	{
		const int64_t remaining = radius - distance;
		denominator = 200LL * radius * radius;
		numerator = 2LL * edgePct * radius * radius + 3LL * (100 - edgePct) * remaining * remaining;
	}
	// The maximum supported radius and mana require more than 64 bits for this product.
	const boost::multiprecision::int128_t scaled = boost::multiprecision::int128_t(std::max(0, baseMana)) * numerator;
	return static_cast<int>((scaled + denominator - 1) / denominator);
}
