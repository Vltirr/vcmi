/*
 * TrivialRetaliation, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "TrivialRetaliation.h"
#include "IBattleInfoCallback.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include <boost/multiprecision/cpp_int.hpp>

using boost::multiprecision::uint256_t;

TrivialRetaliationRules::TrivialRetaliationRules(const JsonNode & config)
{
	if(!config.isStruct())
	{
		if(!config.isNull())
			logGlobal->warn("Mod %s: combat.trivialStackRetaliation must be an object; disabling it", config.getModScope());
		return;
	}
	if(!config["enabled"].isNull() && !config["enabled"].isBool())
		logGlobal->warn("Mod %s: combat.trivialStackRetaliation.enabled must be a boolean; disabling it", config.getModScope());
	enabled = config["enabled"].isBool() && config["enabled"].Bool();
	const auto read = [&](const char * key, int32_t fallback, int32_t maximum)
	{
		const auto & value = config[key];
		if(!value.isNumber() || !std::isfinite(value.Float()) || value.Float() < 0 || value.Float() > maximum || value.Float() != value.Integer())
		{
			if(!value.isNull())
				logGlobal->warn("Mod %s: combat.trivialStackRetaliation.%s must be an integer in [0, %d]; using %d", config.getModScope(), key, maximum, fallback);
			return fallback;
		}
		return static_cast<int32_t>(value.Integer());
	};
	thresholdPercent = read("thresholdPercent", 10, 100);
}

TrivialRetaliation TrivialRetaliationRules::classify(const battle::Unit & attacker, const battle::Unit & defender) const
{
	if(!enabled || !attacker.alive() || !defender.alive())
		return {};
	const auto attackingValue = attacker.unitType()->getAIValue();
	const auto defendingValue = defender.unitType()->getAIValue();
	if(attackingValue <= 0 || defendingValue <= 0)
		return {};
	const uint64_t attackingPower = static_cast<uint64_t>(attackingValue) * attacker.getCount();
	const uint64_t defendingPower = static_cast<uint64_t>(defendingValue) * defender.getCount();
	if(uint256_t(attackingPower) * 100 > uint256_t(defendingPower) * thresholdPercent)
		return {};
	return {true};
}

void TrivialRetaliation::apply(DamageEstimation & estimate) const
{
	if(!active())
		return;
	estimate.noRetaliationConsumption = true;
}
