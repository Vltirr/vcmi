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
#ifdef ENABLE_BATTLE_AI
#include "../../AI/BattleAI/BattleAI.h"
#endif
#include "../../lib/mapObjects/CGHeroInstance.h"
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

#ifdef ENABLE_BATTLE_AI
enum class ReturningAbilityScenario
{
	ENCHANTER_WITH_ENEMY_HERO,
	ENCHANTER_WITHOUT_ENEMY_HERO,
	DRAIN_WITH_ENEMY_MANA,
	DRAIN_WITHOUT_ENEMY_MANA,
	DRAIN_WITHOUT_ENEMY_HERO
};

class ReturningAbilityAITest : public StupidAITest, public ::testing::WithParamInterface<std::tuple<bool, ReturningAbilityScenario>>
{
};

TEST_P(ReturningAbilityAITest, UsesVisibleHeroInfoToSelectCreatureAbilities)
{
	const auto [useBattleAI, scenario] = GetParam();
	const bool enchanter = scenario == ReturningAbilityScenario::ENCHANTER_WITH_ENEMY_HERO
		|| scenario == ReturningAbilityScenario::ENCHANTER_WITHOUT_ENEMY_HERO;
	const bool enemyHasHero = scenario != ReturningAbilityScenario::ENCHANTER_WITHOUT_ENEMY_HERO
		&& scenario != ReturningAbilityScenario::DRAIN_WITHOUT_ENEMY_HERO;
	const bool enemyHasMana = scenario == ReturningAbilityScenario::DRAIN_WITH_ENEMY_MANA;
	defenderSideHero->mana = enemyHasMana ? 10 : 0;
	if(!enemyHasHero)
		battle()->sides[BattleSide::DEFENDER].heroID = ObjectInstanceID::NONE;

	auto * stack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
	auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		enchanter ? BonusType::ENCHANTER : BonusType::MANA_DRAIN, BonusSource::CREATURE_ABILITY,
		enchanter ? 3 : 5, BonusSourceID(), enchanter ? BonusSubtypeID(SpellID(SpellID::BLOODLUST)) : BonusSubtypeID());
	bonus->activation = Activation{
		.id = "vcmi-test:returningAbility",
		.costs = {.focus = 0},
		.turn = ActivationTurn{.returnsControl = true}
	};
	stack->addNewBonus(bonus);
	beginCombat();
	battle()->activeStack = stack->unitId();
	auto callback = std::make_shared<ActionRecordingCallback>();
	callback->onBattleStarted(battle());
	ASSERT_EQ(callback->getBattle(BattleID(0))->battleGetMySide(), BattleSide::ATTACKER);
	std::optional<BattleAction> selected;
	if(useBattleAI)
	{
		CBattleAI ai;
		ai.initBattleInterface(gameHandler, callback);
		selected = ai.useReturningCreatureAbility(BattleID(0), stack);
	}
	else
	{
		CStupidAI ai;
		ai.initBattleInterface(gameHandler, callback);
		ai.activeStack(BattleID(0), stack);
		ASSERT_EQ(callback->actions.size(), 1);
		if(callback->actions.front().actionType == EActionType::CREATURE_ABILITY)
			selected = callback->actions.front();
	}
	const bool shouldActivate = enchanter || (enemyHasHero && enemyHasMana);
	ASSERT_EQ(selected.has_value(), shouldActivate);
	if(selected)
	{
		EXPECT_EQ(selected->actionType, EActionType::CREATURE_ABILITY);
		EXPECT_EQ(selected->activatedAbility, bonus->activation->id);
	}
}

INSTANTIATE_TEST_SUITE_P(PlayerPerspective, ReturningAbilityAITest,
	::testing::Combine(::testing::Bool(), ::testing::Values(
		ReturningAbilityScenario::ENCHANTER_WITH_ENEMY_HERO,
		ReturningAbilityScenario::ENCHANTER_WITHOUT_ENEMY_HERO,
		ReturningAbilityScenario::DRAIN_WITH_ENEMY_MANA,
		ReturningAbilityScenario::DRAIN_WITHOUT_ENEMY_MANA,
		ReturningAbilityScenario::DRAIN_WITHOUT_ENEMY_HERO)));
#endif

}
