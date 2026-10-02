/*
 * SideInBattle.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../GameConstants.h"
#include "../callback/GameCallbackHolder.h"
#include "Activation.h"

class CGHeroInstance;
class CGTownInstance;
class CArmedInstance;

struct DLL_LINKAGE SideInBattle : public GameCallbackHolder
{
	using GameCallbackHolder::GameCallbackHolder;

	PlayerColor color = PlayerColor::CANNOT_DETERMINE;
	ObjectInstanceID heroID; //may be empty if army is not commanded by hero
	ObjectInstanceID armyObjectID; //adv. map object with army that participates in battle; may be same as hero

	uint32_t castSpellsCount = 0; //how many spells each side has been cast this turn
	std::vector<SpellID> usedSpellsHistory; //every time hero casts spell, it's inserted here -> eagle eye skill
	int32_t enchanterCounter = 0; //tends to pass through 0, so sign is needed
	int32_t initialMana = 0;
	int32_t additionalMana = 0;
	int32_t focusPoints = 0;
	std::map<ActivationStateKey, int32_t> activationUsage;
	std::map<ActivationStateKey, int32_t> activationCooldownReadyRound;

	void init(const CGHeroInstance * Hero, const CArmedInstance * Army, const CGTownInstance * town);
	const CArmedInstance * getArmy() const;
	const CGHeroInstance * getHero() const;

	template <typename Handler> void serialize(Handler &h)
	{
		h & color;
		h & heroID;
		h & armyObjectID;
		h & castSpellsCount;
		h & usedSpellsHistory;
		h & enchanterCounter;
		h & initialMana;
		h & additionalMana;
		if(h.hasFeature(Handler::Version::BATTLE_FOCUS))
			h & focusPoints;
		else
			focusPoints = 0;
		if(h.hasFeature(Handler::Version::GENERIC_ACTIVATION_POLICIES))
		{
			h & activationUsage;
			h & activationCooldownReadyRound;
		}
		else if(h.hasFeature(Handler::Version::ACTIVATION_LIMITS))
		{
			std::map<std::string, int32_t> legacyBattle;
			std::map<std::string, int32_t> legacyRound;
			h & legacyBattle;
			h & legacyRound;
			if(!h.saving)
			{
				activationUsage.clear();
				for(const auto & [key, value] : legacyBattle)
					activationUsage[{key, EActivationLimitScope::SIDE, -1, EActivationLimitPeriod::BATTLE}] = value;
				for(const auto & [key, value] : legacyRound)
					activationUsage[{key, EActivationLimitScope::SIDE, -1, EActivationLimitPeriod::ROUND}] = value;
				activationCooldownReadyRound.clear();
			}
		}
		else if(!h.saving)
		{
			activationUsage.clear();
			activationCooldownReadyRound.clear();
		}
	}
};
