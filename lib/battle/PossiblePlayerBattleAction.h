/*
 * PossiblePlayerBattleAction.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../GameConstants.h"

struct Bonus;

class PossiblePlayerBattleAction // actions performed at l-click
{
public:
	enum Actions {
		INVALID = -1,
		CREATURE_INFO,
		HERO_INFO,
		MOVE_TACTICS,
		CHOOSE_TACTICS_STACK,

		MOVE_STACK,
		ATTACK,
		LONG_WEAPON_ATTACK,
		WALK_AND_ATTACK,
		ATTACK_ABILITY,
		ATTACK_AND_RETURN,
		SHOOT,
		CATAPULT,
		HEAL,
		WALK_AND_SPELLCAST,

		RANDOM_GENIE_SPELL,   // random spell on a friendly creature
		CREATURE_ABILITY,      // deliberate bonus-backed action without a target

		NO_LOCATION,          // massive spells that affect every possible target, automatic casts
		ANY_LOCATION,
		OBSTACLE,
		TELEPORT,
		SACRIFICE,
		FREE_LOCATION,        // used with Force Field and Fire Wall - all tiles affected by spell must be free
		AIMED_SPELL_CREATURE, // spell targeted at creature
	};

private:
	Actions action;
	SpellID spellToCast;
	int32_t activationFocusCost = 0;
	bool activationAvailable = true;
	bool activationFocusInsufficient = false;
	bool activationLimitReached = false;
	bool activationCooldownActive = false;
	int32_t activationCooldownRoundsRemaining = 0;
	std::string activatedAbility;
	std::shared_ptr<const Bonus> abilitySource;

public:
	bool spellcast() const
	{
		return action == ANY_LOCATION || action == NO_LOCATION || action == OBSTACLE || action == TELEPORT ||
			   action == SACRIFICE || action == FREE_LOCATION || action == AIMED_SPELL_CREATURE || action == WALK_AND_SPELLCAST;
	}

	Actions get() const
	{
		return action;
	}

	SpellID spell() const
	{
		return spellToCast;
	}

	const std::string & ability() const { return activatedAbility; }
	const std::shared_ptr<const Bonus> & source() const { return abilitySource; }
	bool isAbility() const { return !activatedAbility.empty(); }

	int32_t focusCost() const { return activationFocusCost; }
	bool isActivationAvailable() const { return activationAvailable; }
	bool hasReachedActivationLimit() const { return activationLimitReached; }
	bool hasInsufficientActivationFocus() const { return activationFocusInsufficient; }
	bool hasActivationCooldown() const { return activationCooldownActive; }
	int32_t activationCooldownRemaining() const { return activationCooldownRoundsRemaining; }
	void setActivation(int32_t focusCost, bool available, bool limitReached, bool focusInsufficient = false, bool cooldownActive = false, int32_t cooldownRemaining = 0)
	{
		activationFocusCost = focusCost;
		activationAvailable = available;
		activationLimitReached = limitReached;
		activationFocusInsufficient = focusInsufficient;
		activationCooldownActive = cooldownActive;
		activationCooldownRoundsRemaining = cooldownRemaining;
	}

	PossiblePlayerBattleAction(Actions action, SpellID spellToCast = SpellID::NONE):
		action(static_cast<Actions>(action)),
		spellToCast(spellToCast)
	{
		assert((spellToCast != SpellID::NONE) == spellcast());
	}

	PossiblePlayerBattleAction(Actions action, std::string ability, std::shared_ptr<const Bonus> source):
		action(static_cast<Actions>(action)),
		spellToCast(SpellID::NONE),
		activatedAbility(std::move(ability)),
		abilitySource(std::move(source))
	{
		assert((action == ATTACK_ABILITY || action == CREATURE_ABILITY) && !activatedAbility.empty());
	}

	bool operator == (const PossiblePlayerBattleAction & other) const
	{
		return action == other.action && spellToCast == other.spellToCast && activatedAbility == other.activatedAbility;
	}

	bool operator != (const PossiblePlayerBattleAction & other) const
	{
		return !(*this == other);
	}
};
