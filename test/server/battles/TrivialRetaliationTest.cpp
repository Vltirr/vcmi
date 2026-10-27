/*
 * TrivialRetaliationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/TrivialRetaliation.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../server/CGameHandler.h"
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"

class TrivialRetaliationTest : public BattleTestFixture
{
public:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		configure(true);
		startBattle();
		battle()->stacks.clear();
		addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(1, 1), 1);
		addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(15, 9), 1);
	}

	void configure(bool enabled, int threshold = 10, int damagePercent = 100)
	{
		JsonNode config;
		config["enabled"].Bool() = enabled;
		config["thresholdPercent"].Integer() = threshold;
		config["damagePercent"].Integer() = damagePercent;
		map()->overrideGameSetting(EGameSettings::COMBAT_TRIVIAL_STACK_RETALIATION, config);
	}

	std::pair<CStack *, CStack *> place(int attackerCount = 1, int defenderCount = 100)
	{
		auto attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), attackerCount);
		auto defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), defenderCount);
		forceMaximumDamage(attacker);
		forceMaximumDamage(defender);
		return {attacker, defender};
	}

	std::shared_ptr<HypotheticBattle> hypothetical()
	{
		std::shared_ptr<Environment> environment = gameHandler;
		auto callback = std::make_shared<CBattleCallback>(attackerSideHero->getOwner(), nullptr);
		callback->onBattleStarted(battle());
		return std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	}

	int counters() const
	{
		return std::count_if(server.attacks.begin(), server.attacks.end(), [](const auto & a) { return a.counter(); });
	}
};

TEST_F(TrivialRetaliationTest, ThresholdIsInclusiveAndUsesCounts)
{
	auto [attacker, defender] = place(10, 100);
	auto rules = battle()->getTrivialRetaliationRules();
	EXPECT_TRUE(rules.classify(*attacker, *defender).active());
	configure(true, 9);
	EXPECT_FALSE(battle()->getTrivialRetaliationRules().classify(*attacker, *defender).active());
	configure(false);
	EXPECT_FALSE(battle()->getTrivialRetaliationRules().classify(*attacker, *defender).active());
}

TEST_F(TrivialRetaliationTest, RatioRoundsUpWithoutAnotherMinimum)
{
	TrivialRetaliation ratio{5, 100, 100};
	EXPECT_EQ(ratio.scale(41), 3);
	EXPECT_EQ(ratio.scale(40), 2);
	EXPECT_EQ(ratio.scale(0), 0);
	ratio.damagePercent = 200;
	EXPECT_EQ(ratio.scale(41), 5);
	ratio.damagePercent = 0;
	EXPECT_EQ(ratio.scale(41), 0);
}

TEST_F(TrivialRetaliationTest, ZeroPowerFallsBackAndLargeProductsDoNotOverflow)
{
	EXPECT_EQ((TrivialRetaliation{0, 100, 100}.scale(51)), 51);
	EXPECT_EQ((TrivialRetaliation{5, 0, 100}.scale(51)), 51);
	EXPECT_EQ((TrivialRetaliation{uint64_t(1) << 60, uint64_t(1) << 62, 100}.scale(int64_t(1) << 60)), int64_t(1) << 58);
}

TEST_F(TrivialRetaliationTest, ServerResponseMatchesPredictionAndPreservesUse)
{
	auto [attacker, defender] = place(10, 100);
	DamageEstimation response;
	BattleAttackInfo attackInfo(attacker, defender, 0, false);
	battle()->battleEstimateDamage(attackInfo, &response);
	ASSERT_TRUE(response.noRetaliationConsumption);
	ASSERT_EQ(response.damage.min, response.damage.max);
	const auto initialUses = defender->counterAttacks.available();
	beginCombat();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_EQ(counters(), 1);
	const auto & counter = *std::find_if(server.attacks.begin(), server.attacks.end(), [](const auto & a) { return a.counter(); });
	ASSERT_EQ(counter.bsa.size(), 1);
	EXPECT_EQ(counter.bsa.front().damageAmount, response.damage.min);
	EXPECT_EQ(defender->counterAttacks.available(), initialUses);
	EXPECT_FALSE(server.battleLogLines.empty());
}

TEST_F(TrivialRetaliationTest, DisabledModuleKeepsNormalDamageAndConsumption)
{
	configure(false);
	auto [attacker, defender] = place(10, 100);
	DamageEstimation response;
	battle()->battleEstimateDamage(BattleAttackInfo(attacker, defender, 0, false), &response);
	EXPECT_FALSE(response.noRetaliationConsumption);
	beginCombat();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(counters(), 1);
	EXPECT_FALSE(defender->ableToRetaliate());
}

TEST_F(TrivialRetaliationTest, ExhaustedDefenderCannotRespondToTrivialAttack)
{
	auto [attacker, defender] = place();
	beginCombat();
	defender->counterAttacks.use();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(counters(), 0);
}

TEST_F(TrivialRetaliationTest, BlockedResponseDoesNotConsumeUse)
{
	auto [attacker, defender] = place();
	blockRetaliation(attacker);
	beginCombat();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(counters(), 0);
	EXPECT_TRUE(defender->ableToRetaliate());
}

TEST_F(TrivialRetaliationTest, DeadDefenderDoesNotRespond)
{
	auto [attacker, defender] = place(100, 1);
	configure(true, 100);
	beginCombat();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_FALSE(defender->alive());
	EXPECT_EQ(counters(), 0);
}

TEST_F(TrivialRetaliationTest, PredictionUsesSurvivorsAndPreHitRatio)
{
	auto [attacker, defender] = place(10, 100);
	const auto ratio = battle()->getTrivialRetaliationRules().classify(*attacker, *defender);
	BattleAttackInfo incoming(attacker, defender, 0, false);
	auto damage = battle()->calculateDmgRange(incoming);
	auto remaining = defender->acquireState();
	remaining->damage(damage.damage.min);
	ASSERT_LT(remaining->getCount(), defender->getCount());
	auto normal = battle()->calculateDmgRange(BattleAttackInfo(remaining.get(), attacker, 0, false));
	DamageEstimation response;
	battle()->battleEstimateDamage(incoming, &response);
	EXPECT_EQ(response.damage.min, ratio.scale(normal.damage.min));
	EXPECT_EQ(response.damage.max, ratio.scale(normal.damage.max));
}

TEST_F(TrivialRetaliationTest, BattleAISimulationKeepsResponseForFollowingAttacker)
{
	auto [attacker, defender] = place(10, 100);
	auto hb = hypothetical();
	DamageCache cache;
	cache.buildDamageCache(hb, BattleSide::ATTACKER);
	BattleAttackInfo incoming(attacker, defender, 0, false);
	auto ap = AttackPossibility::evaluate(incoming, attacker->getPosition(), cache, hb);
	BattleExchangeVariant exchange;
	exchange.trackAttack(ap, hb, cache);
	auto survivingDefender = hb->getForUpdate(defender->unitId());
	EXPECT_TRUE(survivingDefender->ableToRetaliate());
	EXPECT_EQ(survivingDefender->counterAttacks.available(), defender->counterAttacks.available());
}
TEST_F(TrivialRetaliationTest, DoubleAttackCanRetaliateTwiceWithoutConsumption)
{
	auto [attacker, defender] = place(10, 100);
	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	beginCombat();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(counters(), 2);
	EXPECT_TRUE(defender->ableToRetaliate());
}

TEST_F(TrivialRetaliationTest, SecondHitCanCrossTheThresholdAndConsumeUse)
{
	configure(true, 10, 0);
	auto [attacker, defender] = place(10, 100);
	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	beginCombat();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_EQ(counters(), 2);
	auto counter = std::find_if(server.attacks.begin(), server.attacks.end(), [](const auto & a) { return a.counter(); });
	ASSERT_FALSE(counter->bsa.empty());
	EXPECT_EQ(counter->bsa.front().damageAmount, 0);
	++counter;
	counter = std::find_if(counter, server.attacks.end(), [](const auto & a) { return a.counter(); });
	ASSERT_NE(counter, server.attacks.end());
	EXPECT_GT(counter->bsa.front().damageAmount, 0);
	EXPECT_FALSE(defender->ableToRetaliate());
}

TEST_F(TrivialRetaliationTest, DisabledDoubleAttackHasOnlyItsNormalResponse)
{
	configure(false);
	auto [attacker, defender] = place(100, 100);
	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	beginCombat();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(counters(), 1);
	EXPECT_EQ(defender->counterAttacks.available(), 1);
}

TEST_F(TrivialRetaliationTest, FirstStrikeRemainsBeforeTheAttackAndKeepsItsUse)
{
	auto [attacker, defender] = place(10, 100);
	defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::FIRST_STRIKE, BonusSource::OTHER, 0, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype::damageTypeAll)));
	beginCombat();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_TRUE(server.attacks.front().counter());
	EXPECT_EQ(server.attacks.front().stackAttacking, defender->unitId());
	EXPECT_TRUE(defender->ableToRetaliate());
}

TEST_F(TrivialRetaliationTest, DeathStareCanKillDefenderBeforeItsTrivialResponse)
{
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(leftHex), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	const auto * gorgon = LIBRARY->creatures()->getByName("core:mightyGorgon");
	ASSERT_NE(gorgon, nullptr);
	const auto abilities = gorgon->getBonusBearer()->getAllBonuses(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER));
	ASSERT_FALSE(abilities->empty());
	auto deathStare = std::make_shared<Bonus>(*abilities->front());
	deathStare->val = 100;
	attacker->addNewBonus(deathStare);
	ASSERT_TRUE(battle()->getTrivialRetaliationRules().classify(*attacker, *defender).active());
	beginCombat();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_FALSE(defender->alive());
	EXPECT_EQ(counters(), 0);
	EXPECT_FALSE(server.castsOf(SpellID::DEATH_STARE).empty());
}

TEST_F(TrivialRetaliationTest, AdditionalUsesAreConsumedOnlyByOrdinaryResponsesInSimulation)
{
	auto [attacker, defender] = place(20, 100);
	defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_RETALIATION, BonusSource::OTHER, 2, BonusSourceID()));
	auto hb = hypothetical();
	DamageCache cache;
	cache.buildDamageCache(hb, BattleSide::ATTACKER);
	auto ap = AttackPossibility::evaluate(BattleAttackInfo(attacker, defender, 0, false), attacker->getPosition(), cache, hb);
	BattleExchangeVariant exchange;
	exchange.trackAttack(ap, hb, cache);
	EXPECT_EQ(hb->getForUpdate(defender->unitId())->counterAttacks.available(), defender->counterAttacks.available() - 1);
}

TEST_F(TrivialRetaliationTest, ExchangeSimulationUsesProportionalResponse)
{
	auto [attacker, defender] = place(10, 100);
	auto hb = hypothetical();
	DamageCache cache;
	cache.buildDamageCache(hb, BattleSide::ATTACKER);
	auto a = hb->getForUpdate(attacker->unitId());
	auto d = hb->getForUpdate(defender->unitId());
	const auto before = a->getAvailableHealth();
	BattleExchangeVariant exchange;
	exchange.trackAttack(a, d, false, true, cache, hb, BattleExchangeVariant::AttackMode::FIRST_STRIKE);
	EXPECT_LT(a->getAvailableHealth(), before);
	EXPECT_TRUE(a->alive());
	EXPECT_EQ(d->counterAttacks.available(), defender->counterAttacks.available());
}

class TrivialRetaliationModuleTest : public BattleTestFixture {};

TEST_F(TrivialRetaliationModuleTest, PackageSelectionActivatesTheConfiguredRules)
{
	const char * selected = std::getenv("SRET_MODULE_ACTIVE");
	if(!selected)
		GTEST_SKIP() << "Run the isolated on/off package variants to verify module selection.";
	startGame();
	startBattle();
	const auto rules = battle()->getTrivialRetaliationRules();
	EXPECT_EQ(rules.enabled, std::string(selected) == "on");
	EXPECT_EQ(rules.thresholdPercent, 10);
	EXPECT_EQ(rules.damagePercent, 100);
}

TEST_F(TrivialRetaliationTest, RangedDoubleAttackKeepsShotsAndRetaliationAccountingSeparate)
{
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(2, 5), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(7, 5), 100);
	forceMaximumDamage(attacker);
	forceMaximumDamage(defender);
	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION, BonusSource::OTHER, 0, BonusSourceID()));
	DamageEstimation response;
	battle()->battleEstimateDamage(BattleAttackInfo(attacker, defender, 0, true), &response);
	EXPECT_TRUE(response.noRetaliationConsumption);
	const auto shotsBefore = defender->shots.available();
	beginCombat();
	ASSERT_TRUE(act(BattleAction::makeShotAttack(attacker, defender)));
	EXPECT_EQ(counters(), 2);
	EXPECT_EQ(defender->shots.available(), shotsBefore - 2);
	EXPECT_TRUE(defender->ableToRetaliate());
}

TEST_F(TrivialRetaliationTest, BattleAIFirstStrikeUsesPreHitDefenderStrength)
{
	auto [attacker, defender] = place(10, 100);
	defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::FIRST_STRIKE, BonusSource::OTHER, 0, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype::damageTypeAll)));
	auto hb = hypothetical();
	DamageCache cache;
	cache.buildDamageCache(hb, BattleSide::ATTACKER);
	auto ap = AttackPossibility::evaluate(BattleAttackInfo(attacker, defender, 0, false), attacker->getPosition(), cache, hb);
	DamageEstimation response;
	battle()->battleEstimateDamage(BattleAttackInfo(attacker, defender, 0, false), &response);
	EXPECT_EQ(ap.attackerState->getAvailableHealth(), attacker->getAvailableHealth() - response.damage.min);
	EXPECT_TRUE(ap.attackerState->alive());
	for(const auto & state : ap.affectedUnits)
		if(state->unitId() == defender->unitId())
			EXPECT_TRUE(state->ableToRetaliate());
}

TEST_F(TrivialRetaliationTest, BattleAIChoosesDifferentTargetsWhenTheConfiguredCostChanges)
{
	auto [attacker, defender] = place(10, 100);
	auto * alternative = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(leftHex - 1), 1);
	const auto choose = [&]()
	{
		auto hb = hypothetical();
		DamageCache cache;
		cache.buildDamageCache(hb, BattleSide::ATTACKER);
		PotentialTargets targets(attacker, cache, hb);
		return targets.bestAction().attack.defender->unitId();
	};
	configure(true, 10, 0);
	EXPECT_EQ(choose(), defender->unitId());
	configure(true, 10, 10000);
	EXPECT_EQ(choose(), alternative->unitId());
}
TEST_F(TrivialRetaliationTest, RangedSimulationHandlesAStationaryAttackWithoutAnExplicitHex)
{
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(2, 5), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(7, 5), 100);
	forceMaximumDamage(attacker);
	forceMaximumDamage(defender);
	defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION, BonusSource::OTHER, 0, BonusSourceID()));
	auto hb = hypothetical();
	DamageCache cache;
	cache.buildDamageCache(hb, BattleSide::ATTACKER);
	auto ap = AttackPossibility::evaluate(BattleAttackInfo(attacker, defender, 0, true), BattleHex::INVALID, cache, hb);
	EXPECT_LT(ap.attackerState->getAvailableHealth(), attacker->getAvailableHealth());
	EXPECT_TRUE(ap.attackerState->alive());
}
TEST_F(TrivialRetaliationTest, SaveRoundTripKeepsTheEffectiveRules)
{
	GameSettings original;
	original.loadBase(LIBRARY->settingsHandler->getFullConfig());
	JsonNode config;
	config["enabled"].Bool() = true;
	config["thresholdPercent"].Integer() = 7;
	config["damagePercent"].Integer() = 275;
	original.addOverride(EGameSettings::COMBAT_TRIVIAL_STACK_RETALIATION, config);
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.oser & original;
	GameSettings restored;
	restored.loadBase(LIBRARY->settingsHandler->getFullConfig());
	memory.iser.version = ESerializationVersion::CURRENT;
	memory.iser & restored;
	const TrivialRetaliationRules rules(restored.getValue(EGameSettings::COMBAT_TRIVIAL_STACK_RETALIATION));
	EXPECT_TRUE(rules.enabled);
	EXPECT_EQ(rules.thresholdPercent, 7);
	EXPECT_EQ(rules.damagePercent, 275);
}

TEST_F(TrivialRetaliationTest, CollateralVictimDoesNotGainItsOwnRetaliation)
{
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:blackDragon"), BattleHex(leftHex), 1);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10000);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 1), 10000);
	forceMaximumDamage(attacker);
	forceMaximumDamage(defender);
	configure(true, 10, 0);
	const auto initialHealth = collateral->getAvailableHealth();
	beginCombat();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_LT(collateral->getAvailableHealth(), initialHealth);
	EXPECT_EQ(counters(), 1);
	EXPECT_TRUE(defender->ableToRetaliate());
	EXPECT_TRUE(collateral->ableToRetaliate());
}
