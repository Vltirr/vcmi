#include "StdInc.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/json/JsonUtils.h"

TEST(JsonTest, conflictDetectionTestNoConflict)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : 5 } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : 10 } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");

	EXPECT_EQ(result.Struct().size(), 1);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner"), 1);
	EXPECT_EQ(result["test/sameKey/keyInner"].Struct().size(), 1);
}

TEST(JsonTest, conflictDetectionTestSimpleConflict)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : 5 } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : 10 } })";
	constexpr char textC[] = R"({ "keyC" : 1, "sameKey" : { "keyInner" : 15 } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode jsonC(textC, std::size(textC), "Test C");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");
	jsonC.setModScope("modC");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");
	JsonUtils::detectConflicts(result, jsonA, jsonC, "test");

	EXPECT_EQ(result.Struct().size(), 1);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner"), 1);
	EXPECT_EQ(result["test/sameKey/keyInner"].Struct().size(), 2);
}

TEST(JsonTest, conflictDetectionTestArrayConflict)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : [ 10 ] } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : [ 20 ] } })";
	constexpr char textC[] = R"({ "keyC" : 1, "sameKey" : { "keyInner" : [ 30 ] } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode jsonC(textC, std::size(textC), "Test C");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");
	jsonC.setModScope("modC");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");
	JsonUtils::detectConflicts(result, jsonA, jsonC, "test");

	EXPECT_EQ(result.Struct().size(), 1);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner"), 1);
	EXPECT_EQ(result["test/sameKey/keyInner"].Struct().size(), 2);
}

TEST(JsonTest, conflictDetectionTestArrayModifyConflict)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : [ 10, 20 ] } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : { "modify@1" : 20 } })";
	constexpr char textC[] = R"({ "keyC" : 1, "sameKey" : { "keyInner" : { "modify@1" : 30 } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode jsonC(textC, std::size(textC), "Test C");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");
	jsonC.setModScope("modC");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");
	JsonUtils::detectConflicts(result, jsonA, jsonC, "test");

	EXPECT_EQ(result.Struct().size(), 1);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner/1"), 1);
	EXPECT_EQ(result["test/sameKey/keyInner/1"].Struct().size(), 2);
}

TEST(JsonTest, conflictDetectionTestArrayModifySafe)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : [ 10, 20 ] } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : { "modify@1" : 20 } })";
	constexpr char textC[] = R"({ "keyC" : 1, "sameKey" : { "keyInner" : { "modify@2" : 30 } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode jsonC(textC, std::size(textC), "Test C");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");
	jsonC.setModScope("modC");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");
	JsonUtils::detectConflicts(result, jsonA, jsonC, "test");

	EXPECT_EQ(result.Struct().size(), 2);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner/1"), 1);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner/2"), 1);
	EXPECT_EQ(result["test/sameKey/keyInner/1"].Struct().size(), 1);
	EXPECT_EQ(result["test/sameKey/keyInner/2"].Struct().size(), 1);
}

TEST(JsonTest, conflictDetectionTestArrayAppendAlwaysSafe)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : [ 10, 20 ] } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : { "append" : 20 } })";
	constexpr char textC[] = R"({ "keyC" : 1, "sameKey" : { "keyInner" : { "append" : 30 } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode jsonC(textC, std::size(textC), "Test C");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");
	jsonC.setModScope("modC");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");
	JsonUtils::detectConflicts(result, jsonA, jsonC, "test");

	EXPECT_EQ(result.Struct().size(), 0);
}

/// A script declares what its kind needs: every one of them a schema, and a combat event script
/// also the description and the priority that decide how its ability reads and when it runs.
class ScriptSchemaTest : public ::testing::Test
{
public:
	static JsonNode scriptNode(const std::string & text)
	{
		JsonNode node(text.data(), text.size(), "ScriptSchemaTest");
		// core is exempt from required-entry checks, so the sample has to come from somewhere else
		node.setModScope("vcmi-test");
		return node;
	}

	static bool isValid(const std::string & text)
	{
		return JsonUtils::validate(scriptNode(text), "vcmi:script", "ScriptSchemaTest");
	}
};

TEST_F(ScriptSchemaTest, combatEventScriptDeclaresEverythingItsKindNeeds)
{
	EXPECT_TRUE(isValid(R"({
		"implements" : "combatEvent",
		"script" : "combat/spikes",
		"patches" : [ ],
		"priority" : 0,
		"schema" : { "properties" : {}, "additionalProperties" : false },
		"description" : "{Spikes}"
	})"));
}

TEST_F(ScriptSchemaTest, combatEventScriptWithoutPriorityIsRejected)
{
	EXPECT_FALSE(isValid(R"({
		"implements" : "combatEvent",
		"script" : "combat/spikes",
		"patches" : [ ],
		"schema" : { "properties" : {}, "additionalProperties" : false },
		"description" : "{Spikes}"
	})"));
}

TEST_F(ScriptSchemaTest, combatEventScriptWithoutDescriptionIsRejected)
{
	EXPECT_FALSE(isValid(R"({
		"implements" : "combatEvent",
		"script" : "combat/spikes",
		"patches" : [ ],
		"priority" : 0,
		"schema" : { "properties" : {}, "additionalProperties" : false }
	})"));
}

TEST_F(ScriptSchemaTest, scriptWithoutSchemaIsRejected)
{
	EXPECT_FALSE(isValid(R"({
		"implements" : "spellEffect",
		"script" : "spells/spikes",
		"patches" : [ ]
	})"));
}

TEST_F(ScriptSchemaTest, spellEffectNeedsNoDescriptionOrPriority)
{
	EXPECT_TRUE(isValid(R"({
		"implements" : "spellEffect",
		"script" : "spells/spikes",
		"patches" : [ ],
		"schema" : { "properties" : {}, "additionalProperties" : false }
	})"));
}

class BonusActivationSchemaTest : public ::testing::Test
{
protected:
	static bool isValid(const std::string & text)
	{
		JsonNode node(text.data(), text.size(), "BonusActivationSchemaTest");
		node.setModScope("vcmi-test");
		return JsonUtils::validate(node, "bonusInstance.json", "BonusActivationSchemaTest");
	}
};

TEST_F(BonusActivationSchemaTest, acceptsAnIntegerFocusCost)
{
	EXPECT_TRUE(isValid(R"({
		"type" : "SPELLCASTER",
		"activation" : {
			"costs" : { "focus" : 1 },
			"consumeUses" : false
		}
	})"));
}

TEST_F(BonusActivationSchemaTest, acceptsZeroCostActivationWithoutFocus)
{
	EXPECT_TRUE(isValid(R"({
		"type" : "SPELLCASTER",
		"activation" : { }
	})"));
}

TEST_F(BonusActivationSchemaTest, acceptsStackTurnLimitAndCooldown)
{
	EXPECT_TRUE(isValid(R"({
		"type" : "ENCHANTER",
		"activation" : {
			"id" : "vcmi-test:enchanter",
			"turn" : { "returnsControl" : true },
			"limits" : [ { "scope" : "stack", "period" : "turn", "max" : 1 } ],
			"cooldown" : { "scope" : "side", "initial" : 1, "afterUse" : 3 }
		}
	})"));
}

TEST_F(BonusActivationSchemaTest, rejectsSideTurnLimit)
{
	EXPECT_FALSE(isValid(R"({
		"type" : "SPELLCASTER",
		"activation" : {
			"limits" : [ { "scope" : "side", "period" : "turn", "max" : 1 } ]
		}
	})"));
}

TEST_F(BonusActivationSchemaTest, acceptsAbilitySpecificReturnControl)
{
	EXPECT_TRUE(isValid(R"({
		"type" : "SPELLCASTER",
		"actionReturnsControl" : true,
		"activation" : {
			"costs" : { "focus" : 1 },
			"consumeUses" : false
		}
	})"));
}

TEST_F(BonusActivationSchemaTest, acceptsNonEmptyDeliberateAbilityId)
{
	EXPECT_TRUE(isValid(R"({
		"type" : "SPELL_AFTER_ATTACK",
		"subtype" : "slow",
		"activation" : {
			"id" : "focus-demo:slowAttack",
			"costs" : { "focus" : 1 },
			"consumeUses" : false
		}
	})"));
}

TEST_F(BonusActivationSchemaTest, rejectsEmptyDeliberateAbilityId)
{
	EXPECT_FALSE(isValid(R"({
		"type" : "SPELL_AFTER_ATTACK",
		"subtype" : "slow",
		"activation" : {
			"id" : "",
			"costs" : { "focus" : 1 },
			"consumeUses" : false
		}
	})"));
}

TEST_F(BonusActivationSchemaTest, acceptsSharedActivationLimit)
{
	EXPECT_TRUE(isValid(R"({
		"type" : "SPELLCASTER",
		"activation" : {
			"costs" : { "focus" : 3 },
			"consumeUses" : false,
			"limits" : [ {
				"key" : "focus-demo:archangelResurrection",
				"scope" : "side",
				"period" : "battle",
				"max" : 1
			} ]
		}
	})"));
}

TEST_F(BonusActivationSchemaTest, rejectsInvalidActivationLimit)
{
	EXPECT_FALSE(isValid(R"({
		"type" : "SPELLCASTER",
		"activation" : {
			"costs" : { "focus" : 1 },
			"consumeUses" : false,
			"limits" : [ {
				"key" : "",
				"scope" : "stack",
				"period" : "turn",
				"max" : 0
			} ]
		}
	})"));
}

TEST_F(BonusActivationSchemaTest, rejectsFractionalFocusCost)
{
	EXPECT_FALSE(isValid(R"({
		"type" : "SPELLCASTER",
		"activation" : {
			"costs" : { "focus" : 1.5 },
			"consumeUses" : false
		}
	})"));
}

TEST(JsonTest, floatsMatchTheNearestDouble)
{
	constexpr char text[] = R"({ "a" : 0.7, "b" : 1.75, "c" : -0.15, "d" : 1234.5678, "e" : 1e3 })";

	JsonNode json(text, std::size(text), "Test");

	EXPECT_EQ(json["a"].Float(), 0.7);
	EXPECT_EQ(json["b"].Float(), 1.75);
	EXPECT_EQ(json["c"].Float(), -0.15);
	EXPECT_EQ(json["d"].Float(), 1234.5678);
	EXPECT_EQ(json["e"].Float(), 1e3);
}

TEST(JsonTest, floatsWithMoreDigitsThanADoubleHoldsDoNotOverflow)
{
	constexpr char text[] = R"({ "a" : 0.12345678901234567890123456789, "b" : 9007199254740993.5 })";

	JsonNode json(text, std::size(text), "Test");

	// digits past what a double holds are dropped, so the value is only near - what matters is that
	// gathering them never overflows the mantissa into a number of the wrong magnitude or sign
	EXPECT_NEAR(json["a"].Float() / 0.12345678901234567890123456789, 1.0, 1e-15);
	EXPECT_NEAR(json["b"].Float() / 9007199254740993.5, 1.0, 1e-15);
}
