/*
 * Activation.h, part of VCMI engine
 */
#pragma once

#include "../Global.h"
#include "../constants/Enumerations.h"
#include "../spells/ProxyCaster.h"

struct Bonus;
class CStack;
class SpellID;
class CBattleInfoCallback;

namespace battle
{
class Unit;
}

enum class EActivationLimitScope : int8_t
{
	SIDE,
	STACK
};

enum class EActivationLimitPeriod : int8_t
{
	BATTLE,
	ROUND,
	TURN
};

/// Shared usage cap for activated actions. The key lets several abilities use one counter.
struct DLL_LINKAGE ActivationLimit
{
	std::string key;
	EActivationLimitScope scope = EActivationLimitScope::SIDE;
	EActivationLimitPeriod period = EActivationLimitPeriod::BATTLE;
	int32_t max = 1;

	template <typename Handler> void serialize(Handler & h)
	{
		h & key;
		h & scope;
		h & period;
		h & max;
	}
};

/// State address after defaults have been resolved. Stack is unset for side-wide policies.
struct DLL_LINKAGE ActivationStateKey
{
	std::string key;
	EActivationLimitScope scope = EActivationLimitScope::SIDE;
	int32_t stack = -1;
	EActivationLimitPeriod period = EActivationLimitPeriod::BATTLE;

	bool operator<(const ActivationStateKey & other) const
	{
		return std::tie(key, scope, stack, period) < std::tie(other.key, other.scope, other.stack, other.period);
	}

	bool operator==(const ActivationStateKey & other) const = default;

	template <typename Handler> void serialize(Handler & h)
	{
		h & key;
		h & scope;
		h & stack;
		h & period;
	}
};

struct DLL_LINKAGE ActivationTurn
{
	bool returnsControl = false;
	template <typename Handler> void serialize(Handler & h) { h & returnsControl; }
};

struct DLL_LINKAGE ActivationCooldown
{
	std::string key;
	std::optional<EActivationLimitScope> scope;
	int32_t initial = 0;
	int32_t afterUse = 0;
	template <typename Handler> void serialize(Handler & h)
	{
		h & key;
		h & scope;
		h & initial;
		h & afterUse;
	}
};

/// Costs and policies required to activate an existing action. Effects remain owned by the action.
struct DLL_LINKAGE ActivationCosts
{
	int32_t focus = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & focus;
	}
};

struct DLL_LINKAGE Activation
{
	/// Stable identity used when a bonus exposes a deliberate creature ability.
	/// Existing actions whose identity is already carried by EActionType/spell leave it empty.
	std::string id;
	ActivationCosts costs;
	std::optional<ActivationTurn> turn;
	std::optional<ActivationCooldown> cooldown;
	/// Deprecated input compatibility only. Presence never controls runtime behavior.
	bool consumeUses = true;
	std::vector<ActivationLimit> limits;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.hasFeature(Handler::Version::ACTIVATED_CREATURE_ABILITIES))
			h & id;
		else if(!h.saving)
			id.clear();
		h & costs;
		if(h.hasFeature(Handler::Version::GENERIC_ACTIVATION_POLICIES))
		{
			h & turn;
			h & cooldown;
		}
		else if(!h.saving)
		{
			turn.reset();
			cooldown.reset();
		}
		h & consumeUses;
		if(h.hasFeature(Handler::Version::ACTIVATION_LIMITS))
			h & limits;
		else if(!h.saving)
			limits.clear();
	}
};

namespace battle
{
/// Enumerates one source per stable id for deliberate bonus-backed abilities of an action type.
DLL_LINKAGE std::vector<std::shared_ptr<const Bonus>> getDeliberateActivationSources(const CStack * stack, EActionType action);
/// Finds the existing ability definition that supplies an action's activation policy.
/// The caller owns validation and payment; this only maps actions to their configured source.
DLL_LINKAGE std::shared_ptr<const Bonus> getActivationSource(const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability = {});
DLL_LINKAGE int32_t getActivationFocusCost(const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability = {});
struct DLL_LINKAGE ActivationResolution
{
	std::shared_ptr<const Bonus> source;
	std::string identity;
	int32_t focusCost = 0;
	std::vector<ActivationStateKey> limits;
	std::optional<ActivationStateKey> cooldown;
	bool returnsControl = false;
	bool focusAvailable = true;
	bool limitsAvailable = true;
	bool cooldownAvailable = true;
	int32_t cooldownRoundsRemaining = 0;

	bool available() const { return focusAvailable && limitsAvailable && cooldownAvailable; }
};

/// Resolves one authoritative policy used by UI, server and AIs.
DLL_LINKAGE ActivationResolution resolveActivation(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability = {});
DLL_LINKAGE bool areActivationLimitsAvailable(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability = {});
DLL_LINKAGE bool actionReturnsControl(const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability = {});
/// Whether the configured activated effect can affect the selected target. This is independent
/// from whether the underlying attack can reach the target.
DLL_LINKAGE bool isActivationTargetAvailable(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability, const Unit * target);
/// Whether the configured activation costs and limits currently allow the action.
DLL_LINKAGE bool isActivationAvailable(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell, const std::string & ability = {});
}

namespace spells
{
/// Keeps an action's existing caster and effect while optionally suppressing its normal use cost.
class DLL_LINKAGE ActivationCaster final : public ProxyCaster
{
	bool consumeUses;
public:
	ActivationCaster(const Caster * actualCaster, bool consumeUses_)
		: ProxyCaster(actualCaster), consumeUses(consumeUses_)
	{
	}

	void spendMana(ServerCallback * server, const int32_t spellCost) const override
	{
		if(consumeUses)
			ProxyCaster::spendMana(server, spellCost);
	}
};
}
