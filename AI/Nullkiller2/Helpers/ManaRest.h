/*
 * ManaRest.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

namespace NK2AI
{
inline bool manaRestNeeded(int mana, int maximum, bool spellbook, int knowledge, int combatSpells)
{
	return maximum > 0 && static_cast<int64_t>(mana) * 100 < static_cast<int64_t>(maximum) * 40
		&& spellbook && knowledge >= 5 && combatSpells >= 3;
}

inline bool manaRestWorthwhile(int maximum, int normalFinal, int restingFinal, int sacrificed, int dailyMovement)
{
	const int gain = std::min(maximum, restingFinal) - std::min(maximum, normalFinal);
	return gain >= 5 && static_cast<int64_t>(gain) * 100 >= static_cast<int64_t>(maximum) * 15
		&& dailyMovement > 0 && sacrificed >= 0 && static_cast<int64_t>(sacrificed) * 4 <= dailyMovement;
}
}
