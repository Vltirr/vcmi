/*
 * MobileShooterAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"

#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/BonusCustomTypes.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../server/CGameHandler.h"

namespace
{

class MobileShooterAITest : public BattleTestFixture
{
public:
	static constexpr int origin = 5 * GameConstants::BFIELD_WIDTH + 4;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
	}

	static void grant(CStack * stack, BonusType type, int value = 0,
		const BonusSubtypeID & subtype = BonusSubtypeID())
	{
		stack->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID(), subtype));
	}

	CStack * addShooter()
	{
		auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(origin), 100);
		grant(shooter, BonusType::SHOOTER, 4, BonusCustomSubtype::mobileShooter);
		return shooter;
	}

	std::shared_ptr<HypotheticBattle> hypothetical()
	{
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		return std::make_shared<HypotheticBattle>(gameHandler.get(), callback);
	}

	static AttackPossibility mobileAttack(CStack * shooter, CStack * target, BattleHex destination,
		DamageCache & damage, std::shared_ptr<HypotheticBattle> state)
	{
		BattleAttackInfo info(shooter, target, 1, true);
		info.mobileShooting = true;
		return AttackPossibility::evaluate(info, destination, damage, state);
	}
};

TEST_F(MobileShooterAITest, AreaShotDoesNotHitAttackerAtItsOldPosition)
{
	auto * shooter = addShooter();
	grant(shooter, BonusType::SHOOTS_ALL_ADJACENT);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(origin + 1), 100);
	const BattleHex destination(origin - 1);
	ASSERT_TRUE(battle()->battleCanMoveAndShoot(shooter, destination, target->getPosition()));
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	DamageCache damage;
	const auto attack = mobileAttack(shooter, target, destination, damage, hypothetical());

	EXPECT_EQ(attack.attackerState->getAvailableHealth(), shooter->getAvailableHealth());
	EXPECT_EQ(attack.attackerState->getPosition(), destination);
	for(const auto & affected : attack.affectedUnits)
		EXPECT_NE(affected->unitId(), shooter->unitId());
}

TEST_F(MobileShooterAITest, ExchangeAppliesDestinationAndKeepsRangedModality)
{
	auto * shooter = addShooter();
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(origin - 1), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(origin + 3), 100);
	const BattleHex destination(origin + 1);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_TRUE(battle()->battleCanMoveAndShoot(shooter, destination, target->getPosition()));
	auto state = hypothetical();
	DamageCache damage;
	const auto attack = mobileAttack(shooter, target, destination, damage, state);
	const int shotsBefore = shooter->shots.available();
	BattleExchangeVariant exchange;
	exchange.trackAttack(attack, state, damage);
	const auto movedShooter = state->battleGetUnitByID(shooter->unitId());

	EXPECT_EQ(movedShooter->getPosition(), destination);
	EXPECT_TRUE(state->battleCanShoot(movedShooter));
	EXPECT_EQ(state->getForUpdate(shooter->unitId())->shots.available(), shotsBefore - 1);
	EXPECT_EQ(shooter->getPosition(), BattleHex(origin));
}

TEST_F(MobileShooterAITest, ExchangeIncludesEnemyThatCanReachDestinationButNotOrigin)
{
	auto * shooter = addShooter();
	const BattleHex destination(origin + 3);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(origin + 5), 100);
	auto * threat = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(origin + 6), 100);
	grant(threat, BonusType::STACKS_SPEED, -2);
	beginCombat();
	battle()->activeStack = shooter->unitId();
	auto state = hypothetical();
	DamageCache damage;
	const auto attack = mobileAttack(shooter, target, destination, damage, state);
	PotentialTargets targets;
	BattleExchangeEvaluator evaluator(state, gameHandler, 1.0f, 1);
	evaluator.updateReachabilityMap(state);
	const auto stationaryReach = evaluator.getOneTurnReachableUnits(0, shooter->getPosition());
	EXPECT_FALSE(vstd::contains(stationaryReach, threat));
	const auto units = evaluator.getExchangeUnits(attack, 0, targets, state);

	EXPECT_TRUE(units.enemyUnitsReachingAttacker.count(threat->unitId()));
	EXPECT_EQ(shooter->getPosition(), BattleHex(origin));
}

TEST_F(MobileShooterAITest, StationaryMeleeStillIncludesObstacleDamageWithoutMobileBonus)
{
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(origin), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(origin + 1), 100);
	auto obstacle = std::make_shared<SpellCreatedObstacle>();
	obstacle->pos = attacker->getPosition();
	obstacle->customSize.insert(obstacle->pos);
	obstacle->casterSide = BattleSide::DEFENDER;
	obstacle->passable = true;
	obstacle->trigger = SpellID::MAGIC_ARROW;
	obstacle->casterSpellPower = 3;
	obstacle->spellLevel = 1;
	battle()->obstacles.push_back(obstacle);
	auto state = hypothetical();
	DamageCache damage;
	damage.buildDamageCache(state, BattleSide::ATTACKER);
	ASSERT_GT(damage.getObstacleDamage(attacker->getPosition(), attacker), 0);
	BattleAttackInfo info(attacker, target, 0, false);
	const auto attack = AttackPossibility::evaluate(info, attacker->getPosition(), damage, state);

	EXPECT_LT(attack.attackerState->getAvailableHealth(), attacker->getAvailableHealth());
	EXPECT_GT(attack.attackerDamageReduce, 0);
}

TEST_F(MobileShooterAITest, FullActionSelectionPrefersStrongerMeleeToPenalizedMobileShot)
{
	auto * shooter = addShooter();
	grant(shooter, BonusType::NO_MELEE_PENALTY);
	// Every moved shot is penalized; the stationary melee attack has neither penalty nor retaliation.
	auto mobile = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SHOOTER,
		BonusSource::OTHER, 4, BonusSourceID(), BonusCustomSubtype::mobileShooter);
	mobile->parameters = std::make_shared<BonusParameters>(1);
	shooter->addNewBonus(mobile);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(origin + 1), 100);
	blockRetaliation(shooter);
	beginCombat();
	// Exercise attack selection in the waiting phase, when another WAIT is not an option.
	shooter->setTurnStatus(battle::UnitTurnStatus::WAITING);
	battle()->activeStack = shooter->unitId();
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_TRUE(battle()->battleCanMoveAndShoot(shooter, BattleHex(origin - 1), target->getPosition()));
	auto state = hypothetical();
	DamageCache damage;
	damage.buildDamageCache(state, BattleSide::ATTACKER);
	PotentialTargets targets(shooter, damage, state);
	bool hasMobile = false;
	bool hasMelee = false;
	for(const auto & attack : targets.possibleAttacks)
	{
		if(attack.attack.defender->unitId() != target->unitId())
			continue;
		hasMobile |= attack.attack.mobileShooting;
		hasMelee |= !attack.attack.shooting;
	}
	EXPECT_TRUE(hasMobile);
	EXPECT_TRUE(hasMelee);
	ASSERT_FALSE(targets.bestAction().attack.shooting);
	auto callback = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(gameHandler, callback, shooter, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 1);
	const auto action = evaluator.selectStackAction(shooter);
	EXPECT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2);
	EXPECT_EQ(action.target[1].hexValue, target->getPosition());
}

}
