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
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/mapping/CMap.h"
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
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(shooter->unitSide()), action);
	}

	void configureFocus(bool enabled = true)
	{
		JsonNode focus;
		focus["enabled"].Bool() = enabled;
		focus["pointsPerCharge"].Integer() = 6;
		focus["maxCharges"].Integer() = 3;
		focus["rangedPoints"].Integer() = 2;
		focus["hitPoints"].Integer() = 3;
		map()->overrideGameSetting(EGameSettings::COMBAT_FOCUS, focus);
	}

	void expectFocus(int attacker, int defender)
	{
		EXPECT_EQ(battle()->getFocusPoints(BattleSide::ATTACKER), attacker);
		EXPECT_EQ(battle()->getFocusPoints(BattleSide::DEFENDER), defender);
	}
};

TEST_F(MobileShooterTest, MobileShotGeneratesConfiguredFocusOnce)
{
	configureFocus();
	auto * shooter = addShooter(4);
	auto * target = addTarget(targetAtRangeThreeHex);
	beginCombat();
	ASSERT_TRUE(moveAndShoot(shooter, target));
	expectFocus(2, 3);
}

TEST_F(MobileShooterTest, StationaryShotGeneratesSameFocus)
{
	configureFocus();
	auto * shooter = addShooter(4);
	auto * target = addTarget(targetAtRangeThreeHex);
	beginCombat();
	battle()->activeStack = shooter->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, target)));
	expectFocus(2, 3);
}

TEST_F(MobileShooterTest, MoveWithoutShotGeneratesNoFocus)
{
	configureFocus();
	auto * shooter = addShooter(4);
	addTarget(targetAtRangeThreeHex);
	beginCombat();
	battle()->activeStack = shooter->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMove(shooter, BattleHex(destinationHex))));
	expectFocus(0, 0);
}

TEST_F(MobileShooterTest, RejectedMobileShotGeneratesNoFocus)
{
	configureFocus();
	auto * shooter = addShooter(2);
	auto * target = addTarget(targetAtRangeThreeHex);
	beginCombat();
	const auto shots = shooter->shots.available();
	ASSERT_FALSE(moveAndShoot(shooter, target));
	EXPECT_EQ(shooter->getPosition(), BattleHex(shooterHex));
	EXPECT_EQ(shooter->shots.available(), shots);
	expectFocus(0, 0);
}

TEST_F(MobileShooterTest, AdditionalMobileShotGeneratesFocusPerAttack)
{
	configureFocus();
	auto * shooter = addShooter(4);
	grant(shooter, BonusType::ADDITIONAL_ATTACK, 1);
	auto * target = addTarget(targetAtRangeThreeHex);
	beginCombat();
	ASSERT_TRUE(moveAndShoot(shooter, target));
	expectFocus(4, 6);
}

TEST_F(MobileShooterTest, MobileShotRespectsFocusCapacity)
{
	configureFocus();
	auto * shooter = addShooter(4);
	auto * target = addTarget(targetAtRangeThreeHex);
	beginCombat();
	battle()->setFocusPoints(BattleSide::ATTACKER, 17);
	battle()->setFocusPoints(BattleSide::DEFENDER, 18);
	ASSERT_TRUE(moveAndShoot(shooter, target));
	expectFocus(18, 18);
}

TEST_F(MobileShooterTest, MobileShotWithFocusDisabledGeneratesNoFocus)
{
	configureFocus(false);
	auto * shooter = addShooter(4);
	auto * target = addTarget(targetAtRangeThreeHex);
	beginCombat();
	ASSERT_TRUE(moveAndShoot(shooter, target));
	expectFocus(0, 0);
}

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

}
