/*
* Nullkiller.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "StdInc.h"
#include "Nullkiller.h"

#include "../../../lib/CPlayerState.h"
#include "../../../lib/StartInfo.h"
#include "../../../lib/pathfinder/PathfinderCache.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
#include "../AIGateway.h"
#include "../Behaviors/BuildingBehavior.h"
#include "../Behaviors/BuyArmyBehavior.h"
#include "../Behaviors/CaptureObjectsBehavior.h"
#include "../Behaviors/ClusterBehavior.h"
#include "../Behaviors/DefenceBehavior.h"
#include "../Behaviors/DefenceBehaviorUtils.h"
#include "../Behaviors/EscapeBehavior.h"
#include "../Behaviors/ExplorationBehavior.h"
#include "../Behaviors/GatherArmyBehavior.h"
#include "../Behaviors/RecruitHeroBehavior.h"
#include "../Behaviors/StayAtTownBehavior.h"
#include "../Goals/Invalid.h"
#include "Goals/RecruitHero.h"
#include "ResourceTrader.h"
#include "../Helpers/ManaRest.h"
#include "../Goals/ExecuteHeroChain.h"
#include "../Goals/Composition.h"
#include "../../../lib/mapObjects/ManaAura.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"

namespace NK2AI
{

using namespace Goals;

namespace
{
const char * heroLockReasonName(HeroLockedReason reason)
{
	switch(reason)
	{
	case HeroLockedReason::STARTUP:
		return "startup";
	case HeroLockedReason::DEFENCE:
		return "defence";
	case HeroLockedReason::HERO_CHAIN:
		return "hero chain";
	case HeroLockedReason::MANA_REST:
		return "mana rest";
	case HeroLockedReason::NOT_LOCKED:
		return "not locked";
	}

	return "unknown reason";
}

constexpr size_t MAX_FAILED_PATHS_PER_HERO = 2;
}

// while we play vcmieagles graph can be shared
std::unique_ptr<ObjectGraph> Nullkiller::baseGraph;

Nullkiller::Nullkiller()
	: activeHero(nullptr)
	, activeHeroID(ObjectInstanceID::NONE)
	, targetTile(int3(-1))
	, activePathHero(nullptr)
	, activePathHeroID(ObjectInstanceID::NONE)
	, activePathDestination(int3(-1))
	, scanDepth(ScanDepth::MAIN_FULL)
	, useHeroChain(true)
	, lastTaskFailureHadPath(false)
	, memory(std::make_unique<AIMemory>())
{

}

Nullkiller::~Nullkiller() = default;

bool canUseOpenMap(const std::shared_ptr<CCallback>& cb, const PlayerColor playerID)
{
	if(!cb->getStartInfo()->extraOptionsInfo.cheatsAllowed)
	{
		return false;
	}

	const TeamState * team = cb->getPlayerTeam(playerID);

	const auto hasHumanInTeam = vstd::contains_if(
		team->players,
		[cb](const PlayerColor teamMateID) -> bool
		{
			return cb->getPlayerState(teamMateID)->isHuman();
		}
	);

	return !hasHumanInTeam;
}

std::vector<HitMapInfo> getTownThreatsForDefenderReservation(const CGTownInstance * town, const Nullkiller * aiNk)
{
	std::vector<HitMapInfo> threats = aiNk->dangerHitMap->getTownThreats(town);
	threats.push_back(aiNk->dangerHitMap->getObjectThreat(town).fastestDanger);

	return threats;
}

void Nullkiller::init(const std::shared_ptr<CCallback> & cbInput, AIGateway * aiGwInput)
{
	cc = cbInput;
	aiGw = aiGwInput;
	playerID = aiGwInput->playerID;

	settings = std::make_unique<Settings>(cc->getStartInfo()->difficulty);

	PathfinderOptions pathfinderOptions(*cc);
	pathfinderOptions.useTeleportTwoWay = true;
	pathfinderOptions.useTeleportOneWay = false;
	pathfinderOptions.useTeleportOneWayRandom = false;

	pathfinderCache = std::make_unique<PathfinderCache>(cc.get(), pathfinderOptions);

	if(canUseOpenMap(cc, playerID))
	{
		useObjectGraph = settings->isObjectGraphAllowed();
		openMap = settings->isOpenMap() || useObjectGraph;
	}
	else
	{
		useObjectGraph = false;
		openMap = false;
	}

	baseGraph.reset();
	priorityEvaluator.reset(new PriorityEvaluator(this));
	priorityEvaluators.reset(
		new SharedPool<PriorityEvaluator>(
			[&]()->std::unique_ptr<PriorityEvaluator>
			{
				return std::make_unique<PriorityEvaluator>(this);
			}));

	dangerHitMap.reset(new DangerHitMapAnalyzer(this));
	buildAnalyzer.reset(new BuildAnalyzer(this));
	objectClusterizer.reset(new ObjectClusterizer(this));
	dangerEvaluator.reset(new FuzzyHelper(this));
	pathfinder.reset(new AIPathfinder(this));
	armyManager.reset(new ArmyManager(cc.get(), this));
	heroManager.reset(new HeroManager(cc.get(), this));
	decomposer.reset(new DeepDecomposer(this));
	armyFormation.reset(new ArmyFormation(cc, this));
}

TaskPlanItem::TaskPlanItem(const TSubgoal & task)
	: affectedObjects(task->asTask()->getAffectedObjects())
	, task(task)
{
}

Goals::TTaskVec TaskPlan::getTasks() const
{
	Goals::TTaskVec result;

	for(auto & item : tasks)
	{
		result.push_back(taskptr(*item.task));
	}

	vstd::removeDuplicates(result);

	return result;
}

void TaskPlan::mergeAndFilter(const TSubgoal & task)
{
	TGoalVec blockers;

	if (task->asTask()->priority <= 0)
		return;

	for(auto & item : tasks)
	{
		for(auto objid : item.affectedObjects)
		{
			if(task == item.task || task->asTask()->isObjectAffected(objid) || (task->asTask()->getHero() != nullptr && task->asTask()->getHero() == item.task->asTask()->getHero()))
			{
				if(item.task->asTask()->priority >= task->asTask()->priority)
					return;

				blockers.push_back(item.task);
				break;
			}
		}
	}

	vstd::erase_if(tasks, [&](const TaskPlanItem & item)
		{
			return vstd::contains(blockers, item.task);
		});

	tasks.emplace_back(task);
}

TaskFailureAction chooseTaskFailureAction(bool hasAnySuccess, bool hasRemainingTasks, bool canReplan)
{
	if(hasAnySuccess)
		return TaskFailureAction::REPLAN;

	if(hasRemainingTasks)
		return TaskFailureAction::TRY_NEXT_TASK;

	if(canReplan)
		return TaskFailureAction::REPLAN;

	return TaskFailureAction::STOP_TURN;
}

Goals::TTask Nullkiller::choseBestTask(Goals::TGoalVec & tasks) const
{
	if(tasks.empty())
	{
		return taskptr(Invalid());
	}

	for(const TSubgoal & task : tasks)
	{
		if(task->asTask()->priority <= 0)
			task->asTask()->priority = priorityEvaluator->evaluate(task);
	}

	auto bestTask = *vstd::maxElementByFun(tasks, [](const Goals::TSubgoal& task) -> float
		{
			return task->asTask()->priority;
		});

	return taskptr(*bestTask);
}

Nullkiller::EvaluationContextMap Nullkiller::buildEvaluationContexts(const TGoalVec & tasks) const
{
	std::vector<std::optional<EvaluationContext>> contexts(tasks.size());

	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, tasks.size(), 128),
		[this, &tasks, &contexts](const tbb::blocked_range<size_t> & r)
		{
			const auto evaluator = priorityEvaluators->acquire();
			for(size_t i = r.begin(); i != r.end(); ++i)
				contexts[i].emplace(evaluator->buildEvaluationContext(tasks[i]));
		}
	);

	EvaluationContextMap result;
	for(size_t i = 0; i != tasks.size(); ++i)
		result.try_emplace(tasks[i].get(), std::move(contexts[i].value()));

	return result;
}

Goals::TTaskVec Nullkiller::buildPlanAndFilter(
	TGoalVec & tasks,
	const EvaluationContextMap & evaluationContexts,
	int priorityTier) const
{
	TaskPlan taskPlan;

	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, tasks.size(), 128),
		[this, &tasks, &evaluationContexts, priorityTier](const tbb::blocked_range<size_t> & r)
		{
			const auto evaluator = this->priorityEvaluators->acquire();
			for(size_t i = r.begin(); i != r.end(); i++)
			{
				const auto & task = tasks[i];
				if(task->asTask()->priority <= 0 || priorityTier != PriorityEvaluator::PriorityTier::BUILDINGS)
					task->asTask()->priority = evaluator->evaluate(
						task,
						priorityTier,
						evaluationContexts.at(task.get()));
			}
		}
	);

	std::ranges::sort(
		tasks,
		[](const TSubgoal & g1, const TSubgoal & g2) -> bool
		{
			return g2->asTask()->priority < g1->asTask()->priority;
		}
	);

	for(const TSubgoal & task : tasks)
	{
		taskPlan.mergeAndFilter(task);
	}

	return taskPlan.getTasks();
}

void Nullkiller::decompose(Goals::TGoalVec & results, const Goals::TSubgoal& behavior, int decompositionMaxDepth) const
{
	makingTurnInterruption.interruptionPoint();
	logAi->debug("Decomposing behavior %s", behavior->toString());
	const auto start = std::chrono::high_resolution_clock::now();
	decomposer->decompose(results, behavior, decompositionMaxDepth);

	makingTurnInterruption.interruptionPoint();
	logAi->debug("Decomposing behavior %s done in %ld", behavior->toString(), timeElapsed(start));
}

void Nullkiller::resetState()
{
	std::unique_lock lockGuard(aiStateMutex);

	lockedResources = TResources();
	scanDepth = ScanDepth::MAIN_FULL;
	lockedHeroes.clear();
	failedHeroPaths.clear();
	idleInteractions.clear();
	resetTaskExecutionContext();
	dangerHitMap->resetHitmap();
	useHeroChain = true;
	objectClusterizer->reset();

	if(!baseGraph && isObjectGraphAllowed())
	{
		baseGraph = std::make_unique<ObjectGraph>();
		baseGraph->updateGraph(this);
	}
}

void Nullkiller::invalidatePathfinderData()
{
	pathfinderInvalidated = true;
}

void Nullkiller::updateState()
{
	const auto start = std::chrono::high_resolution_clock::now();
	auto startMethod = start;
	std::map<std::string, uint64_t> methodToElapsedMs = {};
	logAi->trace("PERFORMANCE: NK2 AI updateState started");

	makingTurnInterruption.interruptionPoint();
	std::unique_lock lockGuard(aiStateMutex);

	resetTaskExecutionContext();
	decomposer->reset();

	buildAnalyzer->update();
	methodToElapsedMs.emplace("buildAnalyzer->update", timeElapsed(startMethod));

	if(!pathfinderInvalidated && dangerHitMap->isHitMapUpToDate() && dangerHitMap->isTileOwnersUpToDate())
		logAi->trace("Skipping full state regeneration - up to date");
	else
	{
		startMethod = std::chrono::high_resolution_clock::now();
		memory->removeInvisibleOrDeletedObjects(*cc);
		methodToElapsedMs.emplace("memory->removeInvisibleOrDeletedObjects(*cc)", timeElapsed(startMethod));
		startMethod = std::chrono::high_resolution_clock::now();
		dangerHitMap->updateHitMap();
		methodToElapsedMs.emplace("dangerHitMap->updateHitMap()", timeElapsed(startMethod));
		startMethod = std::chrono::high_resolution_clock::now();
		dangerHitMap->calculateTileOwners();
		methodToElapsedMs.emplace("dangerHitMap->calculateTileOwners()", timeElapsed(startMethod));

		startMethod = std::chrono::high_resolution_clock::now();
		makingTurnInterruption.interruptionPoint();
		heroManager->update();
		methodToElapsedMs.emplace("heroManager->update()", timeElapsed(startMethod));

		logAi->trace("Updating paths");
		PathfinderSettings cfg;
		cfg.useHeroChain = useHeroChain;
		cfg.allowBypassObjects = true;

		if(scanDepth == ScanDepth::SMALL || isObjectGraphAllowed())
			cfg.mainTurnDistanceLimit = settings->getMainHeroTurnDistanceLimit();
		if(scanDepth != ScanDepth::ALL_FULL || isObjectGraphAllowed())
			cfg.scoutTurnDistanceLimit = settings->getScoutHeroTurnDistanceLimit();

		makingTurnInterruption.interruptionPoint();
		startMethod = std::chrono::high_resolution_clock::now();
		const auto heroes = getHeroesForPathfinding();
		methodToElapsedMs.emplace("getHeroesForPathfinding()", timeElapsed(startMethod));
		startMethod = std::chrono::high_resolution_clock::now();
		pathfinder->updatePaths(heroes, cfg);
		methodToElapsedMs.emplace("pathfinder->updatePaths(heroes, cfg)", timeElapsed(startMethod));

		if(isObjectGraphAllowed())
		{
			startMethod = std::chrono::high_resolution_clock::now();
			pathfinder->updateGraphs(
				heroes,
				scanDepth == ScanDepth::SMALL ? PathfinderSettings::MaxTurnDistanceLimit : 10, // TODO: Mircea: Move to constant
				scanDepth == ScanDepth::ALL_FULL ? PathfinderSettings::MaxTurnDistanceLimit : 3); // TODO: Mircea: Move to constant
			methodToElapsedMs.emplace("pathfinder->updateGraphs", timeElapsed(startMethod));
		}

		makingTurnInterruption.interruptionPoint();
		startMethod = std::chrono::high_resolution_clock::now();
		objectClusterizer->clusterize();
		methodToElapsedMs.emplace("objectClusterizer->clusterize()", timeElapsed(startMethod));
		pathfinderInvalidated = false;
	}

	startMethod = std::chrono::high_resolution_clock::now();
	armyManager->update();
	methodToElapsedMs.emplace("armyManager->update()", timeElapsed(startMethod));

	if(const auto timeElapsedMs = timeElapsed(start); timeElapsedMs > 250)
	{
		logAi->warn("PERFORMANCE: NK2 updateState took %ld ms", timeElapsedMs);

#if NK2AI_TRACE_LEVEL >= 1
		for(const auto & [name, elapsedMs] : methodToElapsedMs)
		{
			if(elapsedMs > 25)
				logAi->warn("PERFORMANCE: NK2 updateState %s took %ld ms", name, elapsedMs);
		}
#endif
	}
	else
	{
		logAi->info("PERFORMANCE: NK2 updateState took %ld ms", timeElapsedMs);
	}
}

const CGHeroInstance * Nullkiller::findRequiredTownDefender(const CGTownInstance * town) const
{
	const auto threats = getTownThreatsForDefenderReservation(town, this);
	const auto safeAttackRatio = settings->getSafeAttackRatio();
	const CGHeroInstance * result = nullptr;
	int bestCoveredThreats = 0;
	uint64_t bestStrength = 0;

	const auto evaluateHero = [&](const CGHeroInstance * hero)
	{
		if(!hero || hero->getOwner() != playerID)
			return;

		const int coveredThreats = Goals::countTownThreatsCoveredByDefender(*town, *hero, threats, safeAttackRatio);
		const bool reserveDefender = Goals::shouldReserveTownDefender(*town, *hero, threats, safeAttackRatio);
		const uint64_t strength = hero->estimateHeroCombatValue();

		if(!reserveDefender)
			return;

		if(coveredThreats > bestCoveredThreats || (coveredThreats == bestCoveredThreats && strength > bestStrength))
		{
			result = hero;
			bestCoveredThreats = coveredThreats;
			bestStrength = strength;
		}
	};

	evaluateHero(town->getGarrisonHero());
	evaluateHero(town->getVisitingHero());

	return result;
}

void Nullkiller::reserveRequiredTownDefenders()
{
	// Only urgent town threats reserve heroes here. Safe garrison heroes must
	// stay available for extraction, otherwise large town armies become idle.
	for(const auto * town : cc->getTownsInfo())
	{
		const auto * defender = findRequiredTownDefender(town);

		if(!defender || getHeroLockedReason(defender) != HeroLockedReason::NOT_LOCKED)
			continue;

		logAi->debug("Reserving %s as defender of %s", defender->getNameTextID(), town->getNameTextID());
		lockHero(defender, HeroLockedReason::DEFENCE);
	}
}

bool Nullkiller::isHeroLocked(const CGHeroInstance * hero) const
{
	return getHeroLockedReason(hero) != HeroLockedReason::NOT_LOCKED;
}

void Nullkiller::lockHero(const CGHeroInstance * hero, HeroLockedReason lockReason)
{
	if(!hero)
		return;

	// Hero locks record one current reason for this turn. They are not nested;
	// assigning another reason replaces the previous state.
	logAi->debug(
		"Setting lock reason for hero %s to %s (was %s).",
        hero->getNameTextID(),
		heroLockReasonName(lockReason),
		heroLockReasonName(getHeroLockedReason(hero)));
	lockedHeroes[hero] = lockReason;
}

void Nullkiller::unlockHero(const CGHeroInstance * hero)
{
	if(!hero)
		return;

	logAi->debug(
		"Clearing lock for hero %s (was %s).",
        hero->getNameTextID(),
		heroLockReasonName(getHeroLockedReason(hero)));

	lockedHeroes.erase(hero);
}

bool defenderMakesTownStableAfterTurnEnd(const CGTownInstance * town, const CGHeroInstance * defender, const Nullkiller * aiNk)
{
	const auto threats = getTownThreatsForDefenderReservation(town, aiNk);
	const auto defence = Goals::estimateTownDefence(*town, defender);
	const auto safeAttackRatio = aiNk->settings->getSafeAttackRatio();

	for(const auto & threat : threats)
	{
		if(threat.danger == 0 || threat.turn > 1)
			continue;

		if(!Goals::isTownDefenceSufficient(defence, threat, safeAttackRatio))
			return false;
	}

	return true;
}

bool Nullkiller::canReleaseDefenderForTownCapture(const CGHeroInstance * hero, const CGObjectInstance * target, const AIPath & path) const
{
	if(!hero || !target || path.targetHero != hero)
		return false;

	if(getHeroLockedReason(hero) != HeroLockedReason::DEFENCE)
		return false;

	if(path.exchangeCount != 1 || path.turn() > 1 || path.getFirstBlockedAction())
		return false;

	const auto * defendedTown = hero->getVisitedTown();
	if(!defendedTown || defendedTown->getOwner() != playerID)
		return false;

	if(defendedTown->getGarrisonHero() != hero && defendedTown->getVisitingHero() != hero)
		return false;

	const auto relation = cc->getPlayerRelations(target->tempOwner, playerID);
	const auto defenderMakesHomeStable = defenderMakesTownStableAfterTurnEnd(defendedTown, hero, this);
	const auto remainingTownReinforcement = armyManager->howManyReinforcementsCanBuy(defendedTown->getUpperArmy(), defendedTown);
	const auto calendar = cc->getCalendar();

	return Goals::isDefenderReleaseAllowedForTownCapture(
		*hero,
		*target,
		relation == PlayerRelations::ENEMIES,
		defenderMakesHomeStable,
		remainingTownReinforcement,
		calendar.getDayOfWeek(),
		calendar.getDaysInWeek());
}

bool Nullkiller::arePathHeroesLocked(const AIPath & path, const CGHeroInstance * releasedDefender) const
{
	if(getHeroLockedReason(path.targetHero) == HeroLockedReason::STARTUP)
	{
#if NK2AI_TRACE_LEVEL >= 1
		logAi->trace("Hero %s is locked by STARTUP. Discarding %s", path.targetHero->getNameTextID(), path.toString());
#endif
		return true;
	}

	for(const auto & node : path.nodes)
	{
		if(isPathKnownToFail(node))
		{
#if NK2AI_TRACE_LEVEL >= 1
			logAi->trace(
				"Hero %s already failed to reach %s this turn. Discarding %s",
				node.targetHero->getNameTextID(),
				node.coord.toString(),
				path.toString());
#endif
			return true;
		}

		auto lockReason = getHeroLockedReason(node.targetHero);

		if(lockReason != HeroLockedReason::NOT_LOCKED)
		{
			if(releasedDefender && node.targetHero == releasedDefender && lockReason == HeroLockedReason::DEFENCE)
				continue;

#if NK2AI_TRACE_LEVEL >= 1
			logAi->trace("Hero %s is locked by %d. Discarding %s", path.targetHero->getNameTextID(), (int)lockReason,  path.toString());
#endif
			return true;
		}
	}

	return false;
}

HeroLockedReason Nullkiller::getHeroLockedReason(const CGHeroInstance * hero) const
{
	const auto found = lockedHeroes.find(hero);
	return found != lockedHeroes.end() ? found->second : HeroLockedReason::NOT_LOCKED;
}

bool Nullkiller::tryRestForMana(const Goals::TTask & normalTask, int priorityTier)
{
	logAi->trace("MGMA rest: player=%s tier=%d priority=%f task=%s", playerID.toString(), priorityTier, normalTask->priority, normalTask->toString());
	const auto reject = [](const char * reason)
	{
		logAi->trace("MGMA rest: rejected reason=%s", reason);
		return false;
	};
	if(priorityTier != PriorityEvaluator::ESCAPE && priorityTier != PriorityEvaluator::EXPLORE_AND_GATHER)
		return reject("urgent-or-unsupported-priority-tier");
	const ManaAura aura(cc->getSettings());
	const auto * move = dynamic_cast<const Goals::ExecuteHeroChain *>(normalTask.get());
	if(!aura.enabled)
		return reject("aura-disabled");
	Goals::TGoalVec parts;
	if(const auto * composition = dynamic_cast<const Goals::Composition *>(normalTask.get()))
	{
		parts = composition->decompose(this);
		for(const auto & part : parts)
		{
			if(const auto * chain = dynamic_cast<const Goals::ExecuteHeroChain *>(part.get()))
			{
				if(move)
					return reject("compound-multiple-movements");
				move = chain;
			}
			else if(part->goalType != Goals::EXPLORATION_POINT && part->goalType != Goals::UNLOCK_CLUSTER
				&& part->goalType != Goals::EXPLORE_NEIGHBOUR_TILE)
				return reject("compound-special-action");
		}
	}
	if(!move)
		return reject("no-supported-hero-chain");
	if(move->getHeroExchangeCount() != 1)
		return reject("multiple-heroes");
	if(move->getPath().getTotalDanger() > 0)
		return reject("normal-path-combat");
	if(std::ranges::any_of(move->getPath().nodes, [](const auto & node) { return node.specialAction != nullptr; }))
		return reject("normal-path-special-action");
	const auto * hero = move->getHero();
	if(!hero)
		return reject("no-hero");
	if(std::ranges::any_of(parts, [&](const auto & part)
	{
		return part->goalType == Goals::EXPLORE_NEIGHBOUR_TILE && part->hero != hero;
	}))
		return reject("compound-multiple-heroes");
	logAi->trace("MGMA rest: hero=%s position=%s mana=%d maximum=%d knowledge=%d spellbook=%d movement=%d daily=%d lock=%d",
		hero->getNameTextID(), hero->visitablePos().toString(), hero->mana, hero->manaLimit(), hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE),
		hero->hasSpellbook(), hero->movementPointsRemaining(), hero->movementPointsLimit(), static_cast<int>(getHeroLockedReason(hero)));
	if(isHeroLocked(hero))
		return reject("hero-locked");
	int combatSpells = 0;
	if(hero->hasSpellbook())
		for(const auto & spell : LIBRARY->spellh->objects)
		{
			if(spell->isCombat() && hero->canCastThisSpell(spell.get()))
				++combatSpells;
			if(combatSpells >= 3)
				break;
		}
	logAi->trace("MGMA rest: usable-combat-spells=%d (count capped at 3)", combatSpells);
	if(!manaRestNeeded(hero->mana, hero->manaLimit(), hero->hasSpellbook(), hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), combatSpells))
		return reject("needs-mana-below-40-percent-book-knowledge-5-and-three-combat-spells");
	const auto normalContext = priorityEvaluator->buildEvaluationContext(sptr(*move));
	if(normalContext.isDefend || normalContext.isEnemy || normalContext.conquestValue > 0)
		return reject("defence-or-conquest");

	const int3 origin = hero->visitablePos();
	const auto & originThreat = dangerHitMap->getTileThreat(origin).maximumDanger;
	if(originThreat.turn <= 1 && originThreat.danger > 0)
		return reject("threatened-hero-must-continue");
	int3 normalEnd = origin;
	float normalCost = 0;
	for(const auto & node : move->getPath().nodes)
	{
		if(node.targetHero == hero && node.turns == 0 && node.cost >= normalCost)
		{
			normalEnd = node.coord;
			normalCost = node.cost;
		}
	}
	if(normalEnd.z != origin.z)
		return reject("normal-destination-other-level");
	const int normalMana = hero->getManaNewTurnAt(normalEnd);
	logAi->trace("MGMA rest: normal-end=%s predicted-mana=%d", normalEnd.toString(), normalMana);
	std::vector<const CGTownInstance *> sources;
	for(const auto * town : cc->getTownsInfo())
		if(town->getOwner() == hero->getOwner() && town->hasBuilt(BuildingID::MAGES_GUILD_1) && town->visitablePos().z == origin.z)
			sources.push_back(town);
	if(sources.empty())
		return reject("no-owned-guild-on-level");
	auto distanceSquared = [](const int3 & left, const int3 & right)
	{
		const int64_t dx = static_cast<int64_t>(left.x) - right.x;
		const int64_t dy = static_cast<int64_t>(left.y) - right.y;
		return dx * dx + dy * dy;
	};
	std::ranges::sort(sources, [&](const auto * left, const auto * right)
	{
		const auto leftDistance = distanceSquared(left->visitablePos(), normalEnd);
		const auto rightDistance = distanceSquared(right->visitablePos(), normalEnd);
		return leftDistance == rightDistance ? left->id < right->id : leftDistance < rightDistance;
	});
	std::vector<int3> candidates;
	for(const auto * source : sources)
		if(aura.distance(source->visitablePos(), origin) >= 0)
		{
			candidates.push_back(origin);
			break;
		}
	const auto entrance = sources.front()->visitablePos();
	candidates.push_back(entrance);
	// A short step towards the source keeps the normal destination nearby.
	const int dx = entrance.x - normalEnd.x;
	const int dy = entrance.y - normalEnd.y;
	const int scale = std::max(std::abs(dx), std::abs(dy));
	if(scale > 0)
		candidates.emplace_back(normalEnd.x + dx * std::min(3, scale) / scale,
			normalEnd.y + dy * std::min(3, scale) / scale, normalEnd.z);
	if(sources.size() > 1 && candidates.size() < 4)
		candidates.push_back(sources.at(1)->visitablePos());

	const int daily = hero->movementPointsLimit();
	std::optional<AIPath> bestPath;
	int bestCost = std::numeric_limits<int>::max();
	int bestMana = 0;
	int3 bestPosition(-1);
	auto movementDistance = [](const int3 & from, const int3 & to)
	{
		const int x = std::abs(from.x - to.x);
		const int y = std::abs(from.y - to.y);
		return 100 * std::max(x, y) + 41 * std::min(x, y);
	};
	logAi->trace("MGMA rest: sources=%d candidates=%d", sources.size(), candidates.size());
	for(const auto & position : candidates)
	{
		const auto rejectCandidate = [&](const char * reason)
		{
			logAi->trace("MGMA rest: candidate=%s rejected reason=%s", position.toString(), reason);
		};
		if(!cc->isInTheMap(position) || !cc->isVisible(position))
		{
			rejectCandidate("outside-map-or-not-visible");
			continue;
		}
		bool inAura = false;
		for(const auto * source : sources)
			inAura |= aura.distance(source->visitablePos(), position) >= 0;
		if(!inAura)
		{
			rejectCandidate("outside-aura");
			continue;
		}
		if(std::ranges::any_of(sources, [&](const auto * town)
		{
			return town->visitablePos() == position && town->getVisitingHero() && town->getVisitingHero() != hero;
		}))
		{
			rejectCandidate("town-occupied-by-other-hero");
			continue;
		}
		const auto & threat = dangerHitMap->getTileThreat(position).maximumDanger;
		if(threat.turn <= 1 && threat.danger > 0)
		{
			rejectCandidate("enemy-threat-next-turn");
			continue;
		}
		const int finalMana = hero->getManaNewTurnAt(position);
		std::vector<AIPath> paths;
		if(position != origin)
			pathfinder->calculatePathInfo(paths, position);
		std::optional<AIPath> candidatePath;
		int spent = 0;
		if(position != origin)
		{
			for(const auto & path : paths)
			{
				if(path.targetHero != hero || path.exchangeCount != 1 || path.turn() != 0 || path.getTotalDanger()
					|| path.getFirstBlockedAction() || arePathHeroesLocked(path))
				{
					logAi->trace("MGMA rest: candidate=%s discarded-path=%s reason=hero-exchanges-turn-danger-block-or-lock", position.toString(), path.toString());
					continue;
				}
				if(std::ranges::any_of(path.nodes, [](const auto & node) { return node.specialAction != nullptr; }))
				{
					rejectCandidate("path-special-action");
					continue;
				}
				if(!candidatePath || path.movementCost() < candidatePath->movementCost())
					candidatePath = path;
			}
			if(!candidatePath)
			{
				rejectCandidate("no-safe-same-day-single-hero-path");
				continue;
			}
			spent = static_cast<int>(candidatePath->movementCost() * daily);
		}
		const int unused = std::max(0, hero->movementPointsRemaining() - spent);
		const int detour = std::max(0, movementDistance(origin, position) + movementDistance(position, normalEnd) - movementDistance(origin, normalEnd));
		const int sacrificed = unused + detour;
		logAi->trace("MGMA rest: candidate=%s predicted-mana=%d normal-mana=%d spent=%d unused=%d detour=%d sacrificed=%d daily=%d",
			position.toString(), finalMana, normalMana, spent, unused, detour, sacrificed, daily);
		if(!manaRestWorthwhile(hero->manaLimit(), normalMana, finalMana, sacrificed, daily))
		{
			rejectCandidate("gain-below-5-or-15-percent-or-sacrifice-above-25-percent");
			continue;
		}
		EvaluationContext restContext(this);
		restContext.strategicalValue = static_cast<float>(std::min(hero->manaLimit(), finalMana) - std::min(hero->manaLimit(), normalMana)) / hero->manaLimit()
			- static_cast<float>(sacrificed) / daily;
		if(restContext.strategicalValue <= 0)
		{
			rejectCandidate("movement-cost-outweighs-mana-gain");
			continue;
		}
		restContext.movementCost = static_cast<float>(spent + unused) / daily;
		const float restPriority = priorityEvaluator->evaluate(sptr(*move), PriorityEvaluator::EXPLORE_AND_GATHER, restContext);
		logAi->trace("MGMA rest: candidate=%s rest-priority=%f normal-priority=%f", position.toString(), restPriority, normalTask->priority);
		if(normalTask->priority >= restPriority)
		{
			rejectCandidate("normal-priority-at-least-rest-priority");
			continue;
		}
		if(sacrificed < bestCost || (sacrificed == bestCost && finalMana > bestMana))
		{
			bestPosition = position;
			bestCost = sacrificed;
			bestMana = finalMana;
			bestPath = std::move(candidatePath);
		}
	}
	if(bestPosition == int3(-1))
		return reject("no-worthwhile-candidate");
	logAi->trace("MGMA rest: selected=%s predicted-mana=%d sacrificed=%d", bestPosition.toString(), bestMana, bestCost);
	const auto restingHeroID = hero->id;
	if(bestPath && !executeTask(taskptr(Goals::ExecuteHeroChain(*bestPath))))
	{
		logAi->trace("MGMA rest: movement failed or interrupted; replanning");
		return true;
	}
	if(const auto * currentHero = cc->getHero(restingHeroID); currentHero && currentHero->visitablePos() == bestPosition)
	{
		lockHero(currentHero, HeroLockedReason::MANA_REST);
		logAi->debug("Hero %s rests for mana at %s, predicted %d, sacrificed movement %d", currentHero->getNameTextID(), bestPosition.toString(), bestMana, bestCost);
	}
	return true;
}
void Nullkiller::makeTurn()
{
	pathfinderTurnStorageMisses.store(0);
	const int MAX_DEPTH = 10;
	resetState();
	Goals::TGoalVec tasks;
	tracePlayerStatus(true);
	bool resourcesTradedThisTurn = false;

	for(int pass = 1; pass <= settings->getMaxPass() && cc->getPlayerStatus(playerID) == EPlayerStatus::INGAME; pass++)
	{
		if (pathfinderTurnStorageMisses.load() != 0)
		{
			logAi->trace("Nullkiller::makeTurn AINodeStorage had %d nodeAllocationFailures in previous pass", pathfinderTurnStorageMisses.load());
			pathfinderTurnStorageMisses.store(0);
		}

		if (!updateStateAndExecutePriorityPass(tasks, pass))
			return;

		reserveRequiredTownDefenders();

		tasks.clear();
		decompose(tasks, sptr(CaptureObjectsBehavior()), 1);
		decompose(tasks, sptr(ClusterBehavior()), MAX_DEPTH);
		decompose(tasks, sptr(DefenceBehavior()), MAX_DEPTH);
		decompose(tasks, sptr(EscapeBehavior()), 1);
		decompose(tasks, sptr(GatherArmyBehavior()), MAX_DEPTH);
		// decompose(tasks, sptr(StayAtTownBehavior()), MAX_DEPTH);

		decompose(tasks, sptr(ExplorationBehavior()), MAX_DEPTH);

		const auto evaluationContexts = buildEvaluationContexts(tasks);
		TTaskVec selectedTasks;
		int prioOfTask = 0;
		for(int prio = PriorityEvaluator::PriorityTier::INSTAKILL; prio <= PriorityEvaluator::PriorityTier::MAX_PRIORITY_TIER; ++prio)
		{
			prioOfTask = prio;
			selectedTasks = buildPlanAndFilter(tasks, evaluationContexts, prio);
			if (!selectedTasks.empty())
			{
				// Activate for deep debugging, otherwise too noisy even for trace level 2
// #if NK2AI_TRACE_LEVEL >= 2
// 				for(auto t : tasks)
// 				{
// 					logAi->info("task of prio %d: %s, prio: %f", prio, t->toString(), t->asTask()->priority);
// 				}
// 				for(auto t : selectedTasks)
// 				{
// 					logAi->info("selected task of prio %d: %s, prio: %f", prio, t->toString(), t->priority);
// 				}
// #endif
				break;
			}
		}

		std::ranges::sort(selectedTasks, [](const TTask& a, const TTask& b)
		{
			return a->priority > b->priority;
		});

		bool hasAnySuccess = false;
		if(selectedTasks.empty())
		{
			if(hasUnlockedHeroWithMovement() && scanDepth != ScanDepth::ALL_FULL)
			{
				logAi->info(
					"Pass %d: No worthwhile tasks found while unlocked heroes can still move. Increasing to ScanDepth::ALL_FULL",
					pass);
				scanDepth = ScanDepth::ALL_FULL;
				useHeroChain = false;
				hasAnySuccess = true;
			}
			else
			{
				logAi->debug("Pass %d: No worthwhile tasks found.", pass);
			}
		}

		for(size_t selectedTaskIndex = 0; selectedTaskIndex < selectedTasks.size(); ++selectedTaskIndex)
		{
			const auto & selectedTask = selectedTasks[selectedTaskIndex];

			if(cc->getPlayerStatus(playerID) != EPlayerStatus::INGAME)
				return;

			if(!areAffectedObjectsPresent(selectedTask))
			{
				logAi->debug("Affected object not found. Canceling task.");
				continue;
			}

			std::string taskDescription = selectedTask->toString();
			HeroRole heroRole = getTaskRole(selectedTask);

			if(heroRole != HeroRole::MAIN || selectedTask->getHeroExchangeCount() <= 1)
				useHeroChain = false;

			// TODO: Mircea: AI suffers from lack of capability, no point to activate SMALL, to test more before deleting the code entirely
			// old-todo: better to check turn distance here instead of priority
			// if((heroRole != HeroRole::MAIN || selectedTask->priority < SMALL_SCAN_MIN_PRIORITY)
			// 	&& scanDepth == ScanDepth::MAIN_FULL)
			// {
			// 	useHeroChain = false;
			// 	scanDepth = ScanDepth::SMALL;
			//
			// 	logAi->trace(
			// 		"Goal %s has low priority %f so decreasing scan depth to gain performance.",
			// 		taskDescription,
			// 		selectedTask->priority);
			// }

			if(selectedTask->priority <= 0)
			{
				if(hasUnlockedHeroWithMovement() && scanDepth != ScanDepth::ALL_FULL)
				{
					logAi->info(
						"Pass %d: Unlocked heroes can still move but goal %s has too low priority %f. Increasing to ScanDepth::ALL_FULL",
						pass,
						taskDescription,
						selectedTask->priority);

					scanDepth = ScanDepth::ALL_FULL;
					useHeroChain = false;
					hasAnySuccess = true;
					break;
				}

				logAi->trace("Pass %d: Goal %s has too low priority. It is not worth doing it.", pass, taskDescription);
				continue;
			}

			logAi->info("Pass %d: Performing task (prioOfTask %d) %s with prio: %d", pass, prioOfTask, selectedTask->toString(), selectedTask->priority);

			if(HeroPtr heroPtr(selectedTask->getHero(), cc.get()); selectedTask->getHero() && !heroPtr.isVerified(false))
			{
				logAi->error("Nullkiller::makeTurn Skipping pass due to unverified hero: %s", heroPtr.nameOrDefault());
			}
			else
			{
				if(tryRestForMana(selectedTask, prioOfTask))
				{
					hasAnySuccess = true;
					break;
				}
				if(!executeTask(selectedTask))
				{
					if(!lastTaskFailureHadPath)
						lockTaskHeroes(selectedTask, HeroLockedReason::HERO_CHAIN);

					const bool hasRemainingTasks = selectedTaskIndex + 1 < selectedTasks.size();
					const auto failureAction = chooseTaskFailureAction(hasAnySuccess, hasRemainingTasks, hasUnlockedHeroWithMovement());

					if(failureAction == TaskFailureAction::TRY_NEXT_TASK)
					{
						logAi->warn("Task failed to execute. Trying the next available task.");
						continue;
					}

					if(failureAction == TaskFailureAction::REPLAN)
					{
						logAi->warn("Task failed to execute. Replanning.");
						hasAnySuccess = true;
						break;
					}

					return;
				}
				hasAnySuccess = true;
			}
		}

		if(!resourcesTradedThisTurn)
		{
			resourcesTradedThisTurn = ResourceTrader::trade(*buildAnalyzer, *cc, getFreeResources());
			hasAnySuccess |= resourcesTradedThisTurn;
		}

		if(!hasAnySuccess)
		{
			if(hasUnlockedHeroWithMovement())
				logAi->debug("Pass %d: No worthwhile task was found at full scan depth. AI turn is complete.", pass);
			else
				logAi->debug("Pass %d: No unlocked mobile hero remains. AI turn is complete.", pass);

			tracePlayerStatus(false);
			return;
		}

		for(const auto * heroInfo : cc->getHeroesInfo())
			AIGateway::pickBestArtifacts(cc, heroInfo);

		if(pass == settings->getMaxPass())
			logAi->warn("MaxPass reached. Terminating AI turn.");
	}
}

bool Nullkiller::updateStateAndExecutePriorityPass(Goals::TGoalVec & tempResults, const int passIndex)
{
	updateState();

	Goals::TTask bestPrioPassTask = taskptr(Goals::Invalid());
	for(int i = 1; i <= settings->getMaxPriorityPass() && cc->getPlayerStatus(playerID) == EPlayerStatus::INGAME; i++)
	{
		tempResults.clear();

		// TODO: Mircea: Should merge recruiting from DefenseBehavior
		decompose(tempResults, sptr(RecruitHeroBehavior()), 1);
		// TODO: Mircea: Should merge recruiting from DefenseBehavior
		decompose(tempResults, sptr(BuyArmyBehavior()), 1);
		decompose(tempResults, sptr(BuildingBehavior()), 1);

		bestPrioPassTask = choseBestTask(tempResults);

		if(bestPrioPassTask->priority > 0)
		{
			logAi->info("Pass %d: priorityPass %d: Performing task %s with prio: %d", passIndex, i, bestPrioPassTask->toString(), bestPrioPassTask->priority);

			const bool isRecruitHeroGoal = dynamic_cast<RecruitHero*>(bestPrioPassTask.get()) != nullptr;
			HeroPtr heroPtr(bestPrioPassTask->getHero(), cc.get());
			if(!isRecruitHeroGoal && bestPrioPassTask->getHero() && !heroPtr.isVerified(false))
			{
				logAi->error("Nullkiller::updateStateAndExecutePriorityPass Skipping priorityPass due to unverified hero: %s", heroPtr.nameOrDefault());
			}
			else if(!executeTask(bestPrioPassTask))
			{
				logAi->warn("Task failed to execute during priority pass. Continuing with regular turn planning.");
				break;
			}

			updateState();
		}
		else
		{
			break;
		}

		if(i == settings->getMaxPriorityPass())
		{
			logAi->warn("MaxPriorityPass reached. Terminating priorityPass loop.");
		}
	}
	return true;
}

bool Nullkiller::areAffectedObjectsPresent(const Goals::TTask & task) const
{
	auto affectedObjs = task->getAffectedObjects();

	for(auto oid : affectedObjs)
	{
		if(!cc->tryGetObj(oid))
			return false;
	}

	return true;
}

HeroRole Nullkiller::getTaskRole(const Goals::TTask & task) const
{
	HeroPtr heroPtr(task->getHero(), cc.get());
	HeroRole heroRole = HeroRole::MAIN;

	if(heroPtr.isVerified())
		heroRole = heroManager->getHeroRoleOrDefault(heroPtr);

	return heroRole;
}

std::vector<const CGHeroInstance *> Nullkiller::getTaskHeroes(const Goals::TTask & task) const
{
	std::vector<const CGHeroInstance *> heroes;

	if(const auto * hero = task->getHero())
		heroes.push_back(hero);

	for(const auto objectId : task->getAffectedObjects())
	{
		if(const auto * hero = cc->getHero(objectId))
			heroes.push_back(hero);
	}

	vstd::removeDuplicates(heroes);

	return heroes;
}

void Nullkiller::lockTaskHeroes(const Goals::TTask & task, HeroLockedReason lockReason)
{
	for(const auto * hero : getTaskHeroes(task))
	{
		if(hero->getOwner() == playerID)
			lockHero(hero, lockReason);
	}
}

bool Nullkiller::hasUnlockedHeroWithMovement() const
{
	return vstd::contains_if(
		cc->getHeroesInfo(),
		[this](const CGHeroInstance * hero) -> bool
		{
			return !hero->isGarrisoned()
				&& !isHeroLocked(hero)
				&& hero->movementPointsRemaining() > 100;
		});
}

void Nullkiller::resetTaskExecutionContext()
{
	activePathHero = nullptr;
	activePathHeroID = ObjectInstanceID::NONE;
	activePathDestination = int3(-1);
	setActive(nullptr, int3(-1));
	setTargetObject(-1);
	lastTaskFailureHadPath = false;
}

bool Nullkiller::rememberActivePathFailure()
{
	if(!activePathHeroID.hasValue() || !activePathDestination.isValid())
		return false;

	const auto alreadyRemembered = vstd::contains_if(
		failedHeroPaths,
		[this](const FailedHeroPath & failedPath)
		{
			return failedPath.hero == activePathHeroID && failedPath.destination == activePathDestination;
		});

	if(!alreadyRemembered)
		failedHeroPaths.push_back({ activePathHeroID, activePathDestination });

	const size_t heroFailureCount = std::count_if(
		failedHeroPaths.begin(),
		failedHeroPaths.end(),
		[this](const FailedHeroPath & failedPath)
		{
			return failedPath.hero == activePathHeroID;
		});

	if(heroFailureCount >= MAX_FAILED_PATHS_PER_HERO)
	{
		lockHero(activePathHero, HeroLockedReason::HERO_CHAIN);
		logAi->warn(
			"Hero %d failed %zu different paths. Excluding it from further tasks this turn.",
			activePathHeroID.getNum(),
			heroFailureCount);
	}

	logAi->debug(
		"Rejecting failed path for hero %d to %s for the rest of this turn.",
		activePathHeroID.getNum(),
		activePathDestination.toString());
	return true;
}

bool Nullkiller::repeatsIdleInteraction(const CGHeroInstance * hero, const int3 & tile)
{
	const FailedHeroPath interaction{ hero->id, tile };
	const auto sameInteraction = [&interaction](const FailedHeroPath & other)
	{
		return other.hero == interaction.hero && other.destination == interaction.destination;
	};

	if(vstd::contains_if(idleInteractions, sameInteraction))
		return true;

	idleInteractions.push_back(interaction);
	return false;
}

bool Nullkiller::isPathKnownToFail(const AIPathNodeInfo & node) const
{
	if(!node.targetHero)
		return false;

	return vstd::contains_if(
		failedHeroPaths,
		[&node](const FailedHeroPath & failedPath)
		{
			return failedPath.hero == node.targetHero->id && failedPath.destination == node.coord;
		});
}

bool Nullkiller::executeTask(const Goals::TTask & task)
{
	auto start = std::chrono::high_resolution_clock::now();
	std::string taskDescr = task->toString();
	resetTaskExecutionContext();

	makingTurnInterruption.interruptionPoint();
	logAi->debug("Trying to realize %s (value %2.3f)", taskDescr, task->priority);

	try
	{
		task->accept(aiGw);
		logAi->trace("Task %s completed in %lld", taskDescr, timeElapsed(start));
	}
	catch(goalFulfilledException &)
	{
		logAi->trace("Task %s completed in %lld", taskDescr, timeElapsed(start));
	}
	catch(cannotFulfillGoalException & e)
	{
		lastTaskFailureHadPath = rememberActivePathFailure();
		invalidatePathfinderData();
		logAi->error("Failed to realize subgoal of type %s.", taskDescr);
		logAi->error("The error message was: %s", e.what());
		return false;
	}

	return true;
}

TResources Nullkiller::getFreeResources() const
{
	auto freeRes = cc->getResourceAmount() - lockedResources;
	freeRes.positive();
	return freeRes;
}

void Nullkiller::lockResources(const TResources & res)
{
	lockedResources += res;
}

std::shared_ptr<const CPathsInfo> Nullkiller::getPathsInfo(const CGHeroInstance * h) const
{
	return pathfinderCache->getPathsInfo(h);
}

void Nullkiller::invalidatePaths()
{
	pathfinderCache->invalidatePaths();
}

void Nullkiller::tracePlayerStatus(bool beginning) const
{
#if NK2AI_TRACE_LEVEL >= 1
	float totalHeroesStrength = 0;
	int totalTownsLevel = 0;
	for (const auto *heroInfo : cc->getHeroesInfo())
	{
		totalHeroesStrength += heroInfo->estimateHeroCombatValue();
	}
	for (const auto *townInfo : cc->getTownsInfo())
	{
		totalTownsLevel += townInfo->getTownLevel();
	}

	const auto *firstWord = beginning ? "Beginning:" : "End:";
	logAi->info("%s totalHeroesStrength: %f, totalTownsLevel: %d, resources: %s", firstWord, totalHeroesStrength, totalTownsLevel, cc->getResourceAmount().toString());
#endif
}

HeroMap<HeroRole> Nullkiller::getHeroesForPathfinding() const
{
	HeroMap<HeroRole> activeHeroes;
	for(auto hero : cc->getHeroesInfo())
	{
		activeHeroes[hero] = heroManager->getHeroRoleOrDefaultInefficient(hero);
	}
	return activeHeroes;
}

}
