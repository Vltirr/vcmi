/*
 * ManaRest.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace NK2AI
{
inline bool manaRestNeeded(int mana, int maximum, bool spellbook, const std::vector<int> & combatSpellCosts)
{
	return maximum > 0 && static_cast<int64_t>(mana) * 100 < static_cast<int64_t>(maximum) * 40
		&& spellbook && std::any_of(combatSpellCosts.begin(), combatSpellCosts.end(), [maximum](int cost)
		{
			return cost > 0 && cost <= maximum;
		});
}

inline float manaRestSpellGain(int maximum, int normalFinal, int restingFinal, const std::vector<int> & combatSpellCosts)
{
	const int normal = std::clamp(normalFinal, 0, std::max(0, maximum));
	const int resting = std::clamp(restingFinal, 0, std::max(0, maximum));
	float bestGain = 0;
	for(int cost : combatSpellCosts)
	{
		if(cost <= 0 || cost > maximum)
			continue;
		// Spell choices share one reserve; value progress towards up to two casts, without summing alternatives.
		const float normalCasts = std::min(2.0f, static_cast<float>(normal) / cost);
		const float restingCasts = std::min(2.0f, static_cast<float>(resting) / cost);
		bestGain = std::max(bestGain, (restingCasts - normalCasts) / 2.0f);
	}
	return bestGain;
}

inline bool manaRestWorthwhile(int maximum, int normalFinal, int restingFinal, int sacrificed, int dailyMovement)
{
	const int gain = std::min(maximum, restingFinal) - std::min(maximum, normalFinal);
	return gain >= 5 && static_cast<int64_t>(gain) * 100 >= static_cast<int64_t>(maximum) * 15
		&& dailyMovement > 0 && sacrificed >= 0 && static_cast<int64_t>(sacrificed) * 4 <= dailyMovement;
}
}
