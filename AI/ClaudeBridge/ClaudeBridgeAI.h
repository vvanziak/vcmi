/*
 * ClaudeBridgeAI.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/callback/CAdventureAI.h"
#include "../../lib/json/JsonNode.h"

class AsyncRunner;
class BridgeServer;
class PathfinderCache;
class CGTownInstance;
class CGDwelling;
class CGCreature;
class CGHeroInstance;
class IShipyard;

/// Adventure AI controlled from outside over a local TCP socket. Battles are fought by BattleAI.
class ClaudeBridgeAI : public CAdventureAI
{
	std::shared_ptr<CCallback> cb;
	std::unique_ptr<AsyncRunner> asyncTasks;
	std::unique_ptr<PathfinderCache> pathfinder;
	std::unique_ptr<BridgeServer> server;

	std::mutex mx;
	std::condition_variable cv;
	bool havingTurn = false;
	bool observer = false; ///< VCMI_CLAUDE_SPECTATE: read-only commands only
	bool heroMoving = false;
	bool battleOngoing = false;
	int visitsInProgress = 0;
	std::map<QuestionID, JsonNode> pendingQuestions;
	struct PendingBattle
	{
		BattleID battleID;
		uint32_t unitId;
		bool tactics = false; ///< deployment phase before the first round
	};
	int rejectedRequests = 0; ///< requests the server answered with a failure
	std::set<int> requestsInFlight; ///< ids of our requests the server has not answered yet
	std::optional<PendingBattle> pendingBattle; ///< a unit of ours is waiting for orders
	bool autoFight = false; ///< BattleAI plays the rest of the current battle
	std::vector<std::string> events;

	void addEvent(const std::string & text);
	void addQuestion(QuestionID id, JsonNode description);
	void answerLater(QuestionID id, int selection);

	/// Waits until nothing is in progress or a question needs an answer. The caller holds CGameState::mutex.
	bool waitTillIdle(std::chrono::milliseconds timeout);
	bool waitForTurn(std::chrono::milliseconds timeout);

	JsonNode execute(const std::string & cmd, const JsonNode & args);
	JsonNode dumpState(const JsonNode & args);
	JsonNode statusReport(); ///< pending questions and events accumulated since the last report
	JsonNode findPath(const CGHeroInstance * hero, const int3 & dst);
	JsonNode moveHero(ObjectInstanceID heroID, const int3 & dst);
	JsonNode buildable(const CGTownInstance * town, bool all) const;
	JsonNode build(const CGTownInstance * town, const std::string & building);
	JsonNode recruit(const CGDwelling * dwelling, const CArmedInstance * destination, const std::string & creature, int count);
	JsonNode buildBoat(const IShipyard * shipyard);
	JsonNode moveArmy(const CArmedInstance * from, const CArmedInstance * to);
	JsonNode swapStacks(const CArmedInstance * from, const CArmedInstance * to, SlotID fromSlot, SlotID toSlot, int count);
	JsonNode trade(const CGTownInstance * market, GameResID give, GameResID get, int amount);
	JsonNode tileInfo(const int3 & center, int radius) const;
	JsonNode tavern(const CGTownInstance * town) const;
	JsonNode buyHero(const CGTownInstance * town, ObjectInstanceID heroID);
	/// Sends a request that may start a battle without waiting for the server's answer, which comes after the battle.
	template<typename Request>
	void sendWithoutWaiting(Request && request);
	JsonNode battleState(bool includeMoves = false, std::optional<uint32_t> unitOverride = std::nullopt);
	JsonNode inspect(const CGObjectInstance * object, const CGHeroInstance * compareTo) const;
	JsonNode threats(const CGHeroInstance * compareTo) const;
	JsonNode targets(const CGHeroInstance * hero, size_t limit);
	JsonNode artifacts(const CGHeroInstance * hero) const;
	void equipArtifact(const CGHeroInstance * hero, int from, std::optional<int> to);
	JsonNode thievesGuild(const CGObjectInstance * town) const;
	JsonNode lookup(const std::string & kind, const std::string & query, const std::string & factionFilter) const;
	JsonNode monsterInfo(const CGCreature * monster, const CGHeroInstance * compareTo) const;
	JsonNode battleAct(const JsonNode & args);
	bool waitBattleProgress(std::chrono::milliseconds timeout);
	JsonNode answerQuestion(QuestionID id, int selection);
	JsonNode endTurn();

public:
	ClaudeBridgeAI();
	~ClaudeBridgeAI() override;

	std::string getBattleAIName() const override;

	void initGameInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CCallback> CB) override;
	void yourTurn(QuestionID questionID) override;
	void playerBlocked(int reason, bool start) override;
	void heroVisit(const CGHeroInstance * visitor, const CGObjectInstance * visitedObj, bool start) override;
	void battleEnded() override;
	void requestSent(const CPackForServer * pack, int requestID) override;
	void requestRealized(PackageApplied * pa) override;
	void activeStack(const BattleID & battleID, const CStack * stack) override;
	void yourTacticPhase(const BattleID & battleID, int distance) override;
	void battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed) override;
	void battleEnd(const BattleID & battleID, const BattleResult * br, QuestionID questionID) override;
	void showInfoDialog(EInfoWindowMode type, const std::string & text, const std::vector<Component> & components, int soundID) override;
	void heroGotLevel(const CGHeroInstance * hero, PrimarySkill pskill, const std::vector<SecondarySkill> & skills, bool moreLevelsFollow, QuestionID questionID) override;
	void commanderGotLevel(const CCommanderInstance * commander, std::vector<ui32> skills, bool moreLevelsFollow, QuestionID questionID) override;
	void showBlockingDialog(const std::string & text, const std::vector<Component> & components, QuestionID questionID, const int soundID, bool selection, bool cancel, bool safeToAutoaccept) override;
	void showTeleportDialog(const CGHeroInstance * hero, TeleportChannelID channel, TTeleportExitsList exits, bool impassable, QuestionID questionID) override;
	void showGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down, QuestionID questionID, const MetaString & customTitle) override;
	void showRecruitmentDialog(const CGDwelling * dwelling, const CArmedInstance * dst, int level, QuestionID questionID) override;
	void showMarketWindow(const IMarket * market, const CGHeroInstance * visitor, QuestionID questionID) override;
	void showUniversityWindow(const IMarket * market, const CGHeroInstance * visitor, QuestionID questionID) override;
	void showTavernWindow(const CGObjectInstance * object, const CGHeroInstance * visitor, QuestionID questionID) override;
	void heroExchangeStarted(ObjectInstanceID hero1, ObjectInstanceID hero2, QuestionID questionID) override;
	void showMapObjectSelectDialog(QuestionID questionID, const Component & icon, const MetaString & title, const MetaString & description, const std::vector<ObjectInstanceID> & objects) override;
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState) override;
};
