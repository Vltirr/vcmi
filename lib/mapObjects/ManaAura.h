/*
 * ManaAura.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../int3.h"

class IGameSettings;
class JsonNode;

struct DLL_LINKAGE ManaAura
{
	bool enabled = false;
	bool mysticismSynergy = false;
	int radius = 15;
	int edgePct = 10;

	static void normalize(JsonNode & config);
	explicit ManaAura(const IGameSettings & settings);
	/// Returns the rounded-up Euclidean distance, or -1 outside the circle.
	int distance(const int3 & source, const int3 & position) const;
	int recovery(int baseMana, int distance) const;
};
