/*
 * TrivialRetaliation, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../json/JsonNode.h"

namespace battle { class Unit; }
struct DamageEstimation;

/// Power ratio captured before the incoming hit.
struct DLL_LINKAGE TrivialRetaliation
{
	uint64_t attackerPower = 0;
	uint64_t defenderPower = 0;
	int32_t damagePercent = 100;

	bool active() const { return attackerPower != 0 && defenderPower != 0; }
	int64_t scale(int64_t damage) const;
	void apply(DamageEstimation & estimate, const battle::Unit & target) const;
};

struct DLL_LINKAGE TrivialRetaliationRules
{
	bool enabled = false;
	int32_t thresholdPercent = 10;
	int32_t damagePercent = 100;

	TrivialRetaliationRules() = default;
	explicit TrivialRetaliationRules(const JsonNode & config);
	TrivialRetaliation classify(const battle::Unit & attacker, const battle::Unit & defender) const;
};
