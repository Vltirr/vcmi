/*
 * TrivialRetaliation, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../json/JsonNode.h"

namespace battle { class Unit; }
struct DamageEstimation;

/// Retaliation consumption exemption captured before the incoming hit.
struct DLL_LINKAGE TrivialRetaliation
{
	bool preservesUse = false;

	bool active() const { return preservesUse; }
	void apply(DamageEstimation & estimate) const;
};

struct DLL_LINKAGE TrivialRetaliationRules
{
	bool enabled = false;
	int32_t thresholdPercent = 10;

	TrivialRetaliationRules() = default;
	explicit TrivialRetaliationRules(const JsonNode & config);
	TrivialRetaliation classify(const battle::Unit & attacker, const battle::Unit & defender) const;
};
