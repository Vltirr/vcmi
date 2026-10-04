/*
 * MobileShooterTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/json/JsonBonus.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{

class MobileShooterTest : public BattleTestFixture
{
public:
	static constexpr int shooterHex = 5 * GameConstants::BFIELD_WIDTH + 4;
	static constexpr int destinationHex = shooterHex + 1;
	static constexpr int targetAtRangeTwoHex = destinationHex + 2;
	static constexpr int targetAtRangeThreeHex = destinationHex + 3;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
	}

	CStack * addShooter(int mobileRange, std::optional<int> fullDamageRange = std::nullopt)
	{
		CStack * result = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(shooterHex), 100);
		auto mobileShooter = grant(result, BonusType::SHOOTER, mobileRange, BonusCustomSubtype::mobileShooter);
		if(fullDamageRange)
			mobileShooter->parameters = std::make_shared<BonusParameters>(*fullDamageRange);
		return result;
	}

	CStack * addTarget(int position)
	{
		return addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(position), 100);
	}

	static std::shared_ptr<Bonus> grant(CStack * stack, BonusType type, int32_t value,
		const BonusSubtypeID & subtype = BonusSubtypeID(), std::optional<int> fullDamageRange = std::nullopt)
	{
		auto bonus = std::make_shared<Bonus>(
			BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID(), subtype);
		if(fullDamageRange)
			bonus->parameters = std::make_shared<BonusParameters>(*fullDamageRange);
		stack->addNewBonus(bonus);
		return bonus;
	}

	bool moveAndShoot(CStack * shooter, CStack * target)
	{
		battle()->activeStack = shooter->unitId();
		BattleAction action = BattleAction::makeWalkAndShoot(
			shooter, BattleHex(destinationHex), ::battle::Destination(target));
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), battle()->sideToPlayer(shooter->unitSide()), action);
	}

	void addObstacle(SpellID trigger, bool stopsMovement)
	{
		auto obstacle = std::make_shared<SpellCreatedObstacle>();
		obstacle->uniqueID = 0;
		obstacle->pos = BattleHex(destinationHex);
		obstacle->customSize.insert(obstacle->pos);
		obstacle->passable = true;
		obstacle->trap = stopsMovement;
		obstacle->trigger = trigger;
		obstacle->spellLevel = 1;
		obstacle->casterSpellPower = 3;
		obstacle->casterSide = BattleSide::DEFENDER;
		obstacle->removeOnTrigger = true;
		obstacle->hidden = stopsMovement;
		obstacle->nativeVisible = false;
		battle()->obstacles.push_back(obstacle);
	}
};

TEST_F(MobileShooterTest, ExecutesMovementAndShotAsOneAction)
{
	CStack * shooter = addShooter(2);
	CStack * target = addTarget(targetAtRangeTwoHex);
	beginCombat();

	const int shotsBefore = shooter->shots.available();
	const int countBefore = target->getCount();

	EXPECT_TRUE(moveAndShoot(shooter, target));
	EXPECT_EQ(shooter->getPosition(), BattleHex(destinationHex));
	EXPECT_EQ(shooter->shots.available(), shotsBefore - 1);
	EXPECT_LT(target->getCount(), countBefore);
}

TEST_F(MobileShooterTest, UsesBlockingAtDestinationInsteadOfStartingHex)
{
	CStack * shooter = addShooter(2);
	addTarget(shooterHex - 1);
	CStack * target = addTarget(targetAtRangeTwoHex);

	EXPECT_FALSE(battle()->battleCanShoot(shooter));
	EXPECT_TRUE(battle()->battleCanMoveAndShoot(shooter, BattleHex(destinationHex), target->getPosition()));
}

TEST_F(MobileShooterTest, RejectsShotWhenDestinationIsBlocked)
{
	CStack * shooter = addShooter(2);
	addTarget(destinationHex + 1);
	CStack * target = addTarget(targetAtRangeTwoHex);

	EXPECT_FALSE(battle()->battleCanMoveAndShoot(shooter, BattleHex(destinationHex), target->getPosition()));
}

TEST_F(MobileShooterTest, InheritsMaximumRangeWhenMobileRangeIsOmitted)
{
	CStack * shooter = addShooter(0);
	grant(shooter, BonusType::LIMITED_SHOOTING_RANGE, 4);
	CStack * target = addTarget(targetAtRangeThreeHex);

	EXPECT_TRUE(battle()->battleCanMoveAndShoot(shooter, BattleHex(destinationHex), target->getPosition()));
}

TEST_F(MobileShooterTest, MobileMaximumRangeOverridesNormalMaximumRange)
{
	CStack * shooter = addShooter(3);
	grant(shooter, BonusType::LIMITED_SHOOTING_RANGE, 2);
	CStack * target = addTarget(targetAtRangeThreeHex);

	EXPECT_FALSE(battle()->battleCanShoot(shooter, target->getPosition()));
	EXPECT_TRUE(battle()->battleCanMoveAndShoot(shooter, BattleHex(destinationHex), target->getPosition()));
}

TEST_F(MobileShooterTest, InheritsFullDamageRangeWhenMobileAddInfoIsOmitted)
{
	CStack * shooter = addShooter(4);
	grant(shooter, BonusType::LIMITED_SHOOTING_RANGE, 4, BonusSubtypeID(), 2);
	CStack * target = addTarget(targetAtRangeThreeHex);

	EXPECT_TRUE(battle()->battleHasDistancePenalty(
		shooter, BattleHex(destinationHex), target->getPosition(), true));
}

TEST_F(MobileShooterTest, MobileFullDamageRangeOverridesNormalFullDamageRange)
{
	CStack * shooter = addShooter(4, 3);
	grant(shooter, BonusType::LIMITED_SHOOTING_RANGE, 4, BonusSubtypeID(), 1);
	CStack * target = addTarget(targetAtRangeThreeHex);

	EXPECT_FALSE(battle()->battleHasDistancePenalty(
		shooter, BattleHex(destinationHex), target->getPosition(), true));
}

TEST_F(MobileShooterTest, RejectsOutOfRangeActionWithoutMovingOrUsingShots)
{
	CStack * shooter = addShooter(2);
	CStack * target = addTarget(targetAtRangeThreeHex);
	beginCombat();
	const int shotsBefore = shooter->shots.available();
	const auto healthBefore = target->getAvailableHealth();

	EXPECT_FALSE(moveAndShoot(shooter, target));
	EXPECT_EQ(shooter->getPosition(), BattleHex(shooterHex));
	EXPECT_EQ(shooter->shots.available(), shotsBefore);
	EXPECT_EQ(target->getAvailableHealth(), healthBefore);
	// StartAction marks the activation before dispatch, even for rejected ordinary actions.
}

TEST_F(MobileShooterTest, RejectsOccupiedDestinationWithoutUsingShots)
{
	CStack * shooter = addShooter(2);
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(destinationHex), 10);
	CStack * target = addTarget(targetAtRangeTwoHex);
	beginCombat();
	const int shotsBefore = shooter->shots.available();

	EXPECT_FALSE(moveAndShoot(shooter, target));
	EXPECT_EQ(shooter->getPosition(), BattleHex(shooterHex));
	EXPECT_EQ(shooter->shots.available(), shotsBefore);
}

TEST_F(MobileShooterTest, RejectsActionWithoutAmmunition)
{
	CStack * shooter = addShooter(2);
	CStack * target = addTarget(targetAtRangeTwoHex);
	beginCombat();
	shooter->shots.use(shooter->shots.available());

	EXPECT_FALSE(moveAndShoot(shooter, target));
	EXPECT_EQ(shooter->getPosition(), BattleHex(shooterHex));
	EXPECT_EQ(shooter->shots.available(), 0);
}

TEST_F(MobileShooterTest, IncapacitatingObstacleConsumesActionWithoutShooting)
{
	CStack * shooter = addShooter(2);
	CStack * target = addTarget(targetAtRangeTwoHex);
	addObstacle(SpellID::PARALYZE, false);
	beginCombat();
	const int shotsBefore = shooter->shots.available();
	const auto healthBefore = target->getAvailableHealth();

	EXPECT_TRUE(moveAndShoot(shooter, target));
	EXPECT_EQ(shooter->getPosition(), BattleHex(destinationHex));
	EXPECT_TRUE(shooter->hasBonusOfType(BonusType::NOT_ACTIVE));
	EXPECT_TRUE(shooter->movedThisRound);
	EXPECT_EQ(shooter->shots.available(), shotsBefore);
	EXPECT_EQ(target->getAvailableHealth(), healthBefore);
	EXPECT_FALSE(battle()->battleCanShootAfterMoving(shooter, target->getPosition()));
}

TEST_F(MobileShooterTest, StoppingObstacleConsumesActionWithoutShooting)
{
	CStack * shooter = addShooter(2);
	CStack * target = addTarget(targetAtRangeTwoHex);
	addObstacle(SpellID::NONE, true);
	beginCombat();
	const int shotsBefore = shooter->shots.available();
	const auto healthBefore = target->getAvailableHealth();

	EXPECT_TRUE(moveAndShoot(shooter, target));
	EXPECT_EQ(shooter->getPosition(), BattleHex(destinationHex));
	EXPECT_TRUE(shooter->movedThisRound);
	EXPECT_EQ(shooter->shots.available(), shotsBefore);
	EXPECT_EQ(target->getAvailableHealth(), healthBefore);
}

TEST_F(MobileShooterTest, FreeShootingAllowsBlockedDestinationButNotAdjacentTarget)
{
	CStack * shooter = addShooter(2);
	grant(shooter, BonusType::FREE_SHOOTING, 0, BonusCustomSubtype::freeShootingExceptAdjacent);
	CStack * adjacent = addTarget(destinationHex + 1);
	CStack * target = addTarget(targetAtRangeTwoHex);
	beginCombat();
	const int shotsBefore = shooter->shots.available();

	EXPECT_FALSE(battle()->battleCanMoveAndShoot(shooter, BattleHex(destinationHex), adjacent->getPosition()));
	EXPECT_TRUE(moveAndShoot(shooter, target));
	EXPECT_EQ(shooter->getPosition(), BattleHex(destinationHex));
	EXPECT_EQ(shooter->shots.available(), shotsBefore - 1);
}

TEST_F(MobileShooterTest, WideShooterCanMoveOverItsOwnRearHex)
{
	CStack * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:behemoth"), BattleHex(shooterHex), 10);
	grant(shooter, BonusType::SHOOTER, 2, BonusCustomSubtype::mobileShooter);
	grant(shooter, BonusType::SHOTS, 10);
	CStack * target = addTarget(targetAtRangeTwoHex);
	beginCombat();

	ASSERT_TRUE(shooter->doubleWide());
	EXPECT_TRUE(moveAndShoot(shooter, target));
	EXPECT_EQ(shooter->getPosition(), BattleHex(destinationHex));
	EXPECT_TRUE(shooter->coversPos(BattleHex(shooterHex)));
}

TEST_F(MobileShooterTest, AdditionalRangedAttackConsumesTwoShots)
{
	CStack * shooter = addShooter(2);
	grant(shooter, BonusType::ADDITIONAL_ATTACK, 1);
	CStack * target = addTarget(targetAtRangeTwoHex);
	beginCombat();
	const int shotsBefore = shooter->shots.available();

	EXPECT_TRUE(moveAndShoot(shooter, target));
	EXPECT_EQ(shooter->shots.available(), shotsBefore - 2);
	EXPECT_EQ(shooter->getPosition(), BattleHex(destinationHex));
}

TEST_F(MobileShooterTest, ServerUsesMobileFullDamageProfile)
{
	CStack * shooter = addShooter(4, 3);
	grant(shooter, BonusType::LIMITED_SHOOTING_RANGE, 4, BonusSubtypeID(), 1);
	forceMaximumDamage(shooter);
	CStack * target = addTarget(targetAtRangeThreeHex);
	beginCombat();
	BattleAttackInfo mobileAttack(shooter, target, 1, true);
	mobileAttack.attackerPos = BattleHex(destinationHex);
	mobileAttack.mobileShooting = true;
	const auto expected = battle()->battleEstimateDamage(mobileAttack).damage.max;
	const auto healthBefore = target->getAvailableHealth();

	EXPECT_TRUE(moveAndShoot(shooter, target));
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), expected);
}

TEST_F(MobileShooterTest, ParsesOptionalMobileProfileFromJson)
{
	constexpr char json[] = R"({"type":"SHOOTER","subtype":"bonusSubtype.mobileShooter","addInfo":3})";
	JsonNode node(json, std::size(json), "Mobile shooter test");
	node.setModScope("core");
	const auto bonus = JsonUtils::parseBonus(node);
	ASSERT_NE(bonus, nullptr);
	EXPECT_EQ(bonus->type, BonusType::SHOOTER);
	EXPECT_EQ(bonus->subtype, BonusSubtypeID(BonusCustomSubtype::mobileShooter));
	EXPECT_EQ(bonus->val, 0);
	CStack * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(shooterHex), 100);
	shooter->addNewBonus(bonus);
	grant(shooter, BonusType::LIMITED_SHOOTING_RANGE, 4);
	EXPECT_EQ(battle()->battleGetMobileShooterRange(shooter), 4);
	EXPECT_EQ(battle()->battleGetMobileShooterFullDamageRange(shooter), 3);
}

TEST_F(MobileShooterTest, BerserkMovesWhenNearestTargetIsOutsideStationaryRange)
{
	CStack * shooter = addShooter(2);
	shooter->setPosition(BattleHex(destinationHex));
	grant(shooter, BonusType::ATTACKS_NEAREST_CREATURE, 0);
	grant(shooter, BonusType::LIMITED_SHOOTING_RANGE, 2);
	CStack * target = addTarget(targetAtRangeThreeHex);
	const auto action = battle()->getBerserkForcedAction(shooter);

	EXPECT_EQ(action.type, EActionType::WALK_AND_SHOOT);
	EXPECT_EQ(action.target, target);
	EXPECT_TRUE(battle()->battleCanMoveAndShoot(shooter, action.position, target->getPosition()));
}

TEST_F(MobileShooterTest, LegacyNumericAndMissingShooterSubtypesStayStationary)
{
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(shooterHex), 100);
	for(const std::string & json : {
		std::string(R"({"type":"SHOOTER","subtype":0})"),
		std::string(R"({"type":"SHOOTER","subtype":1})"),
		std::string(R"({"type":"SHOOTER","subtype":"bonusSubtype.creatureDamageMin"})"),
		std::string(R"({"type":"SHOOTER"})")})
	{
		SCOPED_TRACE(json);
		JsonNode node(json.data(), json.size(), "Legacy shooter test");
		node.setModScope("core");
		const auto bonus = JsonUtils::parseBonus(node);
		ASSERT_NE(bonus, nullptr);
		EXPECT_NE(bonus->subtype, BonusSubtypeID(BonusCustomSubtype::mobileShooter));
		shooter->addNewBonus(bonus);
		EXPECT_EQ(battle()->battleGetMobileShooterRange(shooter), 0);
	}
}

TEST_F(MobileShooterTest, SerializedShooterSubtypesRequireNewSaveVersion)
{
	for(const auto version : {ESerializationVersion::RECORD_TEXTS_METASTRING, ESerializationVersion::CURRENT})
	{
		for(const auto subtype : {BonusSubtypeID(), BonusSubtypeID(BonusCustomSubtype(0)),
			BonusSubtypeID(BonusCustomSubtype::mobileShooter)})
		{
			SCOPED_TRACE(static_cast<int>(version));
			SCOPED_TRACE(subtype.getNum());
			CMemorySerializer memory;
			memory.oser.version = version;
			memory.iser.version = version;
			Bonus original(BonusDuration::PERMANENT, BonusType::SHOOTER, BonusSource::OTHER,
				0, BonusSourceID(), subtype);
			memory.oser & original;
			Bonus loaded;
			memory.iser & loaded;
			const bool mobile = version == ESerializationVersion::CURRENT
				&& subtype == BonusSubtypeID(BonusCustomSubtype::mobileShooter);
			EXPECT_EQ(loaded.subtype == BonusSubtypeID(BonusCustomSubtype::mobileShooter), mobile);
		}
	}
}

class BerserkCorridorTest : public MobileShooterTest, public testing::WithParamInterface<BattleSide>
{
};

TEST_P(BerserkCorridorTest, NearbyMeleeTargetIsNotReplacedByDistantMobileTarget)
{
	auto * shooter = addShooter(2);
	grant(shooter, BonusType::ATTACKS_NEAREST_CREATURE, 0);
	grant(shooter, BonusType::STACKS_SPEED, -2);
	const BattleHex closePosition(5, 5);
	const BattleHex farPosition(2, 4);
	const BattleHex intermediate(3, 5);
	const BattleHex firingPosition(2, 6);
	auto * close = addStack(GetParam(), creatureByName("core:pikeman"), closePosition, 100);
	auto * far = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), farPosition, 100);
	auto obstacle = std::make_shared<SpellCreatedObstacle>();
	obstacle->passable = false;
	const BattleHexArray corridor{shooter->getPosition(), closePosition, farPosition, intermediate, firingPosition};
	for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
		if(!corridor.contains(BattleHex(hex)))
			obstacle->customSize.insert(BattleHex(hex));
	battle()->obstacles.push_back(obstacle);

	ASSERT_FALSE(battle()->battleCanMoveAndShoot(shooter, intermediate, farPosition));
	ASSERT_TRUE(battle()->battleCanMoveAndShoot(shooter, firingPosition, farPosition));
	const auto action = battle()->getBerserkForcedAction(shooter);
	EXPECT_EQ(action.type, EActionType::WALK_AND_ATTACK);
	EXPECT_EQ(action.target, close);
	EXPECT_EQ(action.position, shooter->getPosition());
	EXPECT_NE(action.target, far);
}

INSTANTIATE_TEST_SUITE_P(TargetOwners, BerserkCorridorTest,
	testing::Values(BattleSide::ATTACKER, BattleSide::DEFENDER));

TEST_F(MobileShooterTest, SelectedMeleePositionContactsNearHalfOfWideDefender)
{
	auto * shooter = addShooter(2);
	grant(shooter, BonusType::FREE_SHOOTING, 0, BonusCustomSubtype::freeShootingExceptAdjacent);
	const BattleHex firingPosition(destinationHex);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:behemoth"), BattleHex(destinationHex + 1), 10);
	auto * rangedTarget = addTarget(BattleHex(5, 3).toInt());
	const auto contacts = battle()->meleeAttackHexes(shooter, target, firingPosition);
	ASSERT_TRUE(target->doubleWide());
	EXPECT_TRUE(contacts.contains(target->getPosition()));
	EXPECT_FALSE(contacts.contains(target->occupiedHex()));
	EXPECT_TRUE(battle()->isMeleeAttackPossible(shooter, target, firingPosition));
	EXPECT_EQ(BattleHex::mutualPosition(target->occupiedHex(), firingPosition), BattleHex::NONE);
	EXPECT_TRUE(battle()->battleCanMoveAndShoot(shooter, firingPosition, rangedTarget->getPosition()));
	EXPECT_FALSE(battle()->battleCanMoveAndShoot(shooter, firingPosition, target->occupiedHex()));
}

class TowerShootingTest : public MobileShooterTest, public testing::WithParamInterface<int>
{
};

TEST_P(TowerShootingTest, ManualShotFromSpecialPosition)
{
	CStack * tower = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::ARROW_TOWERS), BattleHex(GetParam()), 1);
	grant(tower, BonusType::CREATURE_DAMAGE, 10, BonusCustomSubtype::creatureDamageBoth);
	auto manual = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MANUAL_CONTROL,
		BonusSource::OTHER, 100, BonusSourceID(), BonusSubtypeID(tower->creatureId()));
	defenderSideHero->addNewBonus(manual);
	CStack * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(shooterHex), 100);
	beginCombat();
	const auto healthBefore = target->getAvailableHealth();
	battle()->activeStack = tower->unitId();

	EXPECT_TRUE(battle()->battleCanShoot(tower));
	EXPECT_TRUE(battle()->battleCanShoot(tower, target->getPosition()));
	const auto action = BattleAction::makeShotAttack(tower, target);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action));
	EXPECT_LT(target->getAvailableHealth(), healthBefore);
}

TEST_P(TowerShootingTest, AutomaticShotFromSpecialPosition)
{
	CStack * tower = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::ARROW_TOWERS), BattleHex(GetParam()), 1);
	grant(tower, BonusType::CREATURE_DAMAGE, 10, BonusCustomSubtype::creatureDamageBoth);
	grant(tower, BonusType::CPU_CONTROLLED, 0);
	CStack * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(shooterHex), 100);
	// The layout's token stack is another legal target; make it less attractive to the automatic selector.
	for(const auto * stack : battle()->battleAliveUnits())
		if(stack->unitSide() == BattleSide::ATTACKER && stack != target)
			grant(battle()->getStack(stack->unitId()), BonusType::NOT_ACTIVE, 0);
	const auto healthBefore = target->getAvailableHealth();
	beginCombat();

	EXPECT_TRUE(tower->movedThisRound);
	EXPECT_LT(target->getAvailableHealth(), healthBefore);
}

INSTANTIATE_TEST_SUITE_P(SiegePositions, TowerShootingTest, testing::Values(-2, -3, -4));

class AutomaticShooterTest : public MobileShooterTest, public testing::WithParamInterface<std::tuple<bool, int>>
{
};

TEST_P(AutomaticShooterTest, ManualControlDecisionIsSharedByRangedAndMeleeRoutes)
{
	const auto [mobile, manualChance] = GetParam();
	CStack * shooter = mobile ? addShooter(2)
		: addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(shooterHex), 100);
	grant(shooter, BonusType::CPU_CONTROLLED, 0);
	grant(shooter, BonusType::STACKS_SPEED, 30);
	auto manual = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MANUAL_CONTROL,
		BonusSource::OTHER, manualChance, BonusSourceID(), BonusSubtypeID(shooter->creatureId()));
	attackerSideHero->addNewBonus(manual);
	addTarget(shooterHex - 1);
	addTarget(targetAtRangeTwoHex);
	const int shotsBefore = shooter->shots.available();
	beginCombat();

	if(manualChance == 100)
	{
		EXPECT_EQ(battle()->battleActiveUnit(), shooter);
		EXPECT_EQ(shooter->getPosition(), BattleHex(shooterHex));
		EXPECT_EQ(shooter->shots.available(), shotsBefore);
		EXPECT_FALSE(shooter->movedThisRound);
	}
	else
	{
		EXPECT_TRUE(shooter->movedThisRound);
		EXPECT_EQ(shooter->shots.available(), shotsBefore - (mobile ? 1 : 0));
	}
}

INSTANTIATE_TEST_SUITE_P(ControlModes, AutomaticShooterTest,
	testing::Values(std::make_tuple(false, 0), std::make_tuple(false, 100),
		std::make_tuple(true, 0), std::make_tuple(true, 100)));

TEST_F(MobileShooterTest, AreaShotCanTargetAnEmptyHexAfterMoving)
{
	CStack * shooter = addShooter(3);
	grant(shooter, BonusType::SPELL_LIKE_ATTACK, 1, BonusSubtypeID(SpellID(SpellID::FIREBALL)));
	CStack * target = addTarget(targetAtRangeThreeHex);
	beginCombat();
	battle()->activeStack = shooter->unitId();
	const int shotsBefore = shooter->shots.available();
	const auto healthBefore = target->getAvailableHealth();
	const BattleHex emptyHex(targetAtRangeTwoHex);
	const auto action = BattleAction::makeWalkAndShoot(
		shooter, BattleHex(destinationHex), ::battle::Destination(emptyHex));

	ASSERT_TRUE(battle()->battleCanTargetEmptyHex(shooter));
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(shooter->getPosition(), BattleHex(destinationHex));
	EXPECT_EQ(shooter->shots.available(), shotsBefore - 1);
	EXPECT_LT(target->getAvailableHealth(), healthBefore);
}

}
