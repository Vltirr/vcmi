/*
 * StupidAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"

#include "../../AI/StupidAI/StupidAI.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/BonusCustomTypes.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../server/CGameHandler.h"

namespace
{

class ActionRecordingCallback : public CBattleCallback
{
public:
	std::vector<BattleAction> actions;

	ActionRecordingCallback() : CBattleCallback(PlayerColor(0), nullptr)
	{
	}

	void battleMakeUnitAction(const BattleID & battleID, const BattleAction & action) override
	{
		actions.push_back(action);
	}
};

class StupidAITest : public BattleTestFixture
{
public:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
	}
};

TEST_F(StupidAITest, PrefersMobileTargetWithoutDistancePenalty)
{
	constexpr int origin = 5 * GameConstants::BFIELD_WIDTH + 4;
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(origin), 100);
	auto mobile = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SHOOTER,
		BonusSource::OTHER, 6, BonusSourceID(), BonusCustomSubtype::mobileShooter);
	mobile->parameters = std::make_shared<BonusParameters>(3);
	shooter->addNewBonus(mobile);
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LIMITED_SHOOTING_RANGE,
		BonusSource::OTHER, 1, BonusSourceID()));
	// Equal targets, penalized first: an estimate using the stationary profile would tie and choose it.
	auto * penalized = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(origin + 7), 100);
	auto * fullDamage = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(origin + 2), 100);
	beginCombat();
	battle()->activeStack = shooter->unitId();
	ASSERT_FALSE(battle()->battleCanShoot(shooter, penalized->getPosition()));
	ASSERT_FALSE(battle()->battleCanShoot(shooter, fullDamage->getPosition()));
	auto callback = std::make_shared<ActionRecordingCallback>();
	callback->onBattleStarted(battle());
	CStupidAI ai;
	ai.initBattleInterface(gameHandler, callback);
	ai.activeStack(BattleID(0), shooter);

	ASSERT_EQ(callback->actions.size(), 1);
	const auto & action = callback->actions.front();
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_SHOOT);
	ASSERT_EQ(action.target.size(), 2);
	EXPECT_EQ(action.target[1].unitValue, fullDamage->unitId());
	EXPECT_TRUE(battle()->battleCanMoveAndShoot(shooter, action.target[0].hexValue, fullDamage->getPosition()));
	EXPECT_FALSE(battle()->battleHasDistancePenalty(
		shooter, action.target[0].hexValue, fullDamage->getPosition(), true));
	EXPECT_EQ(shooter->getPosition(), BattleHex(origin));
}

}
