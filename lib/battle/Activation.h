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
	ActivationCosts costs;
	bool consumeUses = true;

	template <typename Handler> void serialize(Handler & h)
	{
		h & costs;
		h & consumeUses;
	}
};

namespace battle
{
/// Finds the existing ability definition that supplies an action's activation policy.
/// The caller owns validation and payment; this only maps actions to their configured source.
DLL_LINKAGE std::shared_ptr<const Bonus> getActivationSource(const CStack * stack, EActionType action, const SpellID & spell);
DLL_LINKAGE int32_t getActivationFocusCost(const CStack * stack, EActionType action, const SpellID & spell);
/// Whether the optional focus cost for the configured action can currently be paid.
DLL_LINKAGE bool isActivationAvailable(const CBattleInfoCallback & battle, const CStack * stack, EActionType action, const SpellID & spell);
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
