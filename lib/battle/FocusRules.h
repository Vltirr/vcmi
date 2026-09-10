/*
 * FocusRules.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../json/JsonNode.h"
#include <algorithm>
#include <limits>

/// Validated rules for the optional, shared battle resource.
struct DLL_LINKAGE FocusRules
{
	bool enabled = false;
	int32_t pointsPerCharge = 6;
	int32_t maxCharges = 3;
	int32_t meleePoints = 2;
	int32_t rangedPoints = 1;
	int32_t longReachPoints = 1;
	int32_t hitPoints = 1;

	FocusRules() = default;
	explicit FocusRules(const JsonNode & config)
	{
		enabled = config["enabled"].Bool();
		auto read = [&](const char * key, int32_t fallback, int32_t minimum)
		{
			return config[key].isNull() ? fallback : static_cast<int32_t>(std::clamp<int64_t>(config[key].Integer(), minimum, 1000000));
		};
		pointsPerCharge = read("pointsPerCharge", 6, 1);
		maxCharges = read("maxCharges", 3, 1);
		maxCharges = std::min(maxCharges, std::numeric_limits<int32_t>::max() / pointsPerCharge);
		meleePoints = read("meleePoints", 2, 0);
		rangedPoints = read("rangedPoints", 1, 0);
		longReachPoints = read("longReachPoints", 1, 0);
		hitPoints = read("hitPoints", 1, 0);
	}

	int32_t capacityPoints() const { return enabled ? pointsPerCharge * maxCharges : 0; }
	int32_t clamp(int64_t points) const { return static_cast<int32_t>(std::clamp<int64_t>(points, 0, capacityPoints())); }
	int32_t charges(int64_t points) const { return clamp(points) / pointsPerCharge; }
	int32_t attackerGain(bool counterattack, bool ranged, bool longReach, bool landed) const
	{
		if(!enabled || counterattack || !landed)
			return 0;
		return ranged ? rangedPoints : (longReach ? longReachPoints : meleePoints);
	}
	int32_t targetGain(bool landed) const { return enabled && landed ? hitPoints : 0; }
};
