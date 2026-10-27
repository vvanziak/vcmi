/*
 * ClaudeBridgeAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ClaudeBridgeAI.h"

#include "BridgeServer.h"

#include "../../lib/AsyncRunner.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/CBonusTypeHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/LogicalExpression.h"
#include "../../lib/UnlockGuard.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/Destination.h"
#include "../../lib/CStack.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/gameState/UpgradeInfo.h"
#include "../../lib/gameState/SThievesGuildInfo.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/entities/artifact/CArtifactSet.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/networkPacks/ArtifactLocation.h"
#include "../../lib/entities/faction/CFaction.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/TerrainHandler.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/IObjectInterface.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/networkPacks/Component.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/pathfinder/CGPathNode.h"
#include "../../lib/pathfinder/PathfinderCache.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../../lib/spells/CSpellHandler.h"

namespace
{

constexpr uint16_t BASE_PORT = 37100;
constexpr auto ACTION_TIMEOUT = std::chrono::seconds(120);
constexpr int MAX_MOVEMENT_BATCHES = 64;

si64 requireInt(const JsonNode & args, const std::string & key)
{
	if(!args[key].isNumber())
		throw std::runtime_error("argument \"" + key + "\" must be a number");
	return args[key].Integer();
}

const std::string & requireString(const JsonNode & args, const std::string & key)
{
	if(!args[key].isString())
		throw std::runtime_error("argument \"" + key + "\" must be a string");
	return args[key].String();
}

JsonNode position(const int3 & pos)
{
	JsonNode ret;
	ret["x"].Integer() = pos.x;
	ret["y"].Integer() = pos.y;
	ret["z"].Integer() = pos.z;
	return ret;
}

int3 readPosition(const JsonNode & args)
{
	return int3(requireInt(args, "x"), requireInt(args, "y"), requireInt(args, "z"));
}

std::string objectName(const CGObjectInstance * obj)
{
	return obj->getObjectName().toString(LIBRARY->staticTexts());
}

GameResID readResource(const JsonNode & args, const std::string & key)
{
	const auto & name = requireString(args, key);
	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; i++)
		if(name == GameConstants::RESOURCE_NAMES[i])
			return GameResID(i);
	throw std::runtime_error("argument \"" + key + "\" must be a resource name (wood, mercury, ore, sulfur, crystal, gems, gold)");
}

bool keyMatches(const std::string & key, const std::string & wanted)
{
	return key == wanted || key.ends_with(":" + wanted);
}

JsonNode resources(const ResourceSet & set)
{
	JsonNode ret;
	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; i++)
		ret[GameConstants::RESOURCE_NAMES[i]].Integer() = set[i];
	return ret;
}

JsonNode components(const std::vector<Component> & list)
{
	JsonNode ret;
	ret.Vector();
	for(const auto & component : list)
	{
		JsonNode entry;
		entry["type"].Integer() = static_cast<int>(component.type);
		if(component.value)
			entry["value"].Integer() = *component.value;
		ret.Vector().push_back(entry);
	}
	return ret;
}

JsonNode army(const CArmedInstance * obj)
{
	JsonNode ret;
	ret.Vector();
	for(const auto & [slot, stack] : obj->Slots())
	{
		JsonNode entry;
		entry["slot"].Integer() = slot.getNum();
		entry["creature"].String() = stack->getCreatureID().toEntity(LIBRARY)->getJsonKey();
		entry["name"].String() = stack->getCreature()->getNameSingularTranslated();
		entry["count"].Integer() = stack->getCount();
		ret.Vector().push_back(entry);
	}
	return ret;
}

JsonNode hero(const CGHeroInstance * h)
{
	JsonNode ret;
	ret["id"].Integer() = h->id.getNum();
	ret["name"].String() = h->getHeroType()->getNameTranslated();
	ret["class"].String() = h->getHeroClass()->getJsonKey();
	ret["pos"] = position(h->visitablePos());
	ret["level"].Integer() = h->level;
	ret["exp"].Integer() = h->exp;
	ret["movement"].Integer() = h->movementPointsRemaining();
	ret["movementLimit"].Integer() = h->movementPointsLimit();
	ret["mana"].Integer() = h->mana;
	ret["combatValue"].Integer() = h->estimateHeroCombatValue();
	ret["visitedTown"].Integer() = h->getVisitedTown() ? h->getVisitedTown()->id.getNum() : -1;

	ret["primarySkills"]["attack"].Integer() = h->getPrimSkillLevel(PrimarySkill::ATTACK);
	ret["primarySkills"]["defense"].Integer() = h->getPrimSkillLevel(PrimarySkill::DEFENSE);
	ret["primarySkills"]["spellPower"].Integer() = h->getPrimSkillLevel(PrimarySkill::SPELL_POWER);
	ret["primarySkills"]["knowledge"].Integer() = h->getPrimSkillLevel(PrimarySkill::KNOWLEDGE);

	ret["secondarySkills"].Vector();
	for(const auto & [skill, skillLevel] : h->secSkills)
	{
		JsonNode entry;
		entry["skill"].String() = skill.toEntity(LIBRARY)->getJsonKey();
		entry["level"].Integer() = skillLevel;
		ret["secondarySkills"].Vector().push_back(entry);
	}

	ret["spells"].Vector();
	for(const auto & spell : h->getSpellsInSpellbook())
		ret["spells"].Vector().emplace_back(spell.toEntity(LIBRARY)->getJsonKey());

	ret["army"] = army(h);
	return ret;
}

JsonNode recruitable(const CGDwelling * t)
{
	JsonNode ret;
	ret.Vector();
	for(size_t level = 0; level < t->creatures.size(); level++)
	{
		const auto & [available, options] = t->creatures[level];
		for(const auto & id : options)
		{
			const auto * creature = id.toEntity(LIBRARY);
			JsonNode entry;
			entry["level"].Integer() = level;
			entry["creature"].String() = creature->getJsonKey();
			entry["name"].String() = creature->getNameSingularTranslated();
			entry["available"].Integer() = available;
			entry["cost"] = resources(creature->getFullRecruitCost());
			ret.Vector().push_back(entry);
		}
	}
	return ret;
}

JsonNode town(const CGTownInstance * t)
{
	JsonNode ret;
	ret["id"].Integer() = t->id.getNum();
	ret["name"].String() = objectName(t);
	ret["faction"].String() = t->getFaction()->getJsonKey();
	ret["pos"] = position(t->visitablePos());
	ret["builtThisTurn"].Bool() = t->built != 0;
	ret["visitingHero"].Integer() = t->getVisitingHero() ? t->getVisitingHero()->id.getNum() : -1;
	ret["garrisonHero"].Integer() = t->getGarrisonHero() ? t->getGarrisonHero()->id.getNum() : -1;

	ret["buildings"].Vector();
	const auto & all = t->getTown()->buildings;
	for(const auto & id : t->getBuildings())
	{
		auto it = all.find(id);
		ret["buildings"].Vector().emplace_back(it != all.end() ? it->second->getJsonKey() : std::to_string(id.getNum()));
	}

	ret["recruitable"] = recruitable(t);
	ret["army"] = army(t);
	return ret;
}

const char * buildingStateName(EBuildingState state)
{
	switch(state)
	{
	case EBuildingState::HAVE_CAPITAL: return "have_capital";
	case EBuildingState::NO_WATER: return "no_water";
	case EBuildingState::FORBIDDEN: return "forbidden";
	case EBuildingState::ADD_MAGES_GUILD: return "need_mage_guild";
	case EBuildingState::ALREADY_PRESENT: return "already_present";
	case EBuildingState::CANT_BUILD_TODAY: return "built_today";
	case EBuildingState::NO_RESOURCES: return "no_resources";
	case EBuildingState::ALLOWED: return "allowed";
	case EBuildingState::PREREQUIRES: return "missing_requirements";
	case EBuildingState::MISSING_BASE: return "missing_base";
	case EBuildingState::TOWN_NOT_OWNED: return "not_owned";
	default: return "error";
	}
}

}

ClaudeBridgeAI::ClaudeBridgeAI() = default;

ClaudeBridgeAI::~ClaudeBridgeAI()
{
	server.reset();
	asyncTasks.reset();
}

std::string ClaudeBridgeAI::getBattleAIName() const
{
	return "BattleAI";
}

void ClaudeBridgeAI::initGameInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CCallback> CB)
{
	cb = CB;
	cbc = CB;
	env = ENV;
	human = false;
	playerID = cb->getPlayerID().value();
	observer = std::getenv("VCMI_CLAUDE_SPECTATE") != nullptr;
	cb->waitTillRealize = true;

	asyncTasks = std::make_unique<AsyncRunner>();
	pathfinder = std::make_unique<PathfinderCache>(cb.get(), PathfinderOptions(*cb));
	server = std::make_unique<BridgeServer>(BASE_PORT + playerID.getNum(), [this](const std::string & cmd, const JsonNode & args)
	{
		return execute(cmd, args);
	});
}

void ClaudeBridgeAI::addEvent(const std::string & text)
{
	std::lock_guard lock(mx);
	events.push_back(text);
}

void ClaudeBridgeAI::addQuestion(QuestionID id, JsonNode description)
{
	std::lock_guard lock(mx);
	description["id"].Integer() = id.getNum();
	pendingQuestions[id] = std::move(description);
	cv.notify_all();
}

void ClaudeBridgeAI::answerLater(QuestionID id, int selection)
{
	asyncTasks->run([this, id, selection]()
	{
		std::shared_lock gsLock(CGameState::mutex);
		cb->selectionMade(selection, id);
	});
}

bool ClaudeBridgeAI::waitTillIdle(std::chrono::milliseconds timeout)
{
	auto unlock = vstd::makeUnlockSharedGuard(CGameState::mutex);
	std::unique_lock lock(mx);
	return cv.wait_for(lock, timeout, [this]()
	{
		return !pendingQuestions.empty() || pendingBattle || (requestsInFlight.empty() && !heroMoving && !battleOngoing && visitsInProgress == 0);
	});
}

bool ClaudeBridgeAI::waitForTurn(std::chrono::milliseconds timeout)
{
	std::unique_lock lock(mx);
	return cv.wait_for(lock, timeout, [this]() { return havingTurn || pendingBattle.has_value(); });
}

void ClaudeBridgeAI::yourTurn(QuestionID questionID)
{
	{
		std::lock_guard lock(mx);
		havingTurn = true;
		events.push_back("your turn started");
		cv.notify_all();
	}
	answerLater(questionID, 0);
}

void ClaudeBridgeAI::playerBlocked(int reason, bool start)
{
	std::lock_guard lock(mx);
	if(start && reason == PlayerBlocked::UPCOMING_BATTLE)
		battleOngoing = true;
	if(reason == PlayerBlocked::ONGOING_MOVEMENT)
		heroMoving = start;
	cv.notify_all();
}

void ClaudeBridgeAI::heroVisit(const CGHeroInstance * visitor, const CGObjectInstance * visitedObj, bool start)
{
	std::lock_guard lock(mx);
	visitsInProgress += start ? 1 : -1;
	cv.notify_all();
}

void ClaudeBridgeAI::battleEnded()
{
	std::lock_guard lock(mx);
	battleOngoing = false;
	events.push_back("battle ended");
	cv.notify_all();
}

void ClaudeBridgeAI::showInfoDialog(EInfoWindowMode type, const std::string & text, const std::vector<Component> & components, int soundID)
{
	addEvent(text);
}

void ClaudeBridgeAI::heroGotLevel(const CGHeroInstance * hero, PrimarySkill pskill, const std::vector<SecondarySkill> & skills, bool moreLevelsFollow, QuestionID questionID)
{
	JsonNode question;
	question["kind"].String() = "level_up";
	question["hero"].Integer() = hero->id.getNum();
	question["level"].Integer() = hero->level;
	question["choiceBase"].Integer() = 0;
	question["choices"].Vector();
	for(const auto & skill : skills)
		question["choices"].Vector().emplace_back(skill.toEntity(LIBRARY)->getJsonKey());
	addQuestion(questionID, std::move(question));
}

void ClaudeBridgeAI::commanderGotLevel(const CCommanderInstance * commander, std::vector<ui32> skills, bool moreLevelsFollow, QuestionID questionID)
{
	answerLater(questionID, 0);
}

void ClaudeBridgeAI::showBlockingDialog(const std::string & text, const std::vector<Component> & list, QuestionID questionID, const int soundID, bool selection, bool cancel, bool safeToAutoaccept)
{
	JsonNode question;
	question["kind"].String() = "dialog";
	question["text"].String() = text;
	question["components"] = components(list);
	question["selection"].Bool() = selection;
	question["cancel"].Bool() = cancel;
	question["choiceBase"].Integer() = 0;
	question["help"].String() = selection
		? "choose 1.." + std::to_string(list.size()) + (cancel ? " or 0 to cancel" : "")
		: "1 = yes, 0 = no";
	addQuestion(questionID, std::move(question));
}

void ClaudeBridgeAI::showRecruitmentDialog(const CGDwelling * dwelling, const CArmedInstance * dst, int level, QuestionID questionID)
{
	JsonNode question;
	question["kind"].String() = "recruitment";
	question["object"].Integer() = dwelling->id.getNum();
	question["name"].String() = objectName(dwelling);
	question["recruitable"] = recruitable(dwelling);
	question["help"].String() = "use recruit with object and hero, then answer 0 to close";
	addQuestion(questionID, std::move(question));
}

void ClaudeBridgeAI::showMarketWindow(const IMarket * market, const CGHeroInstance * visitor, QuestionID questionID)
{
	addEvent("market visited, no trading yet");
	answerLater(questionID, 0);
}

void ClaudeBridgeAI::showUniversityWindow(const IMarket * market, const CGHeroInstance * visitor, QuestionID questionID)
{
	addEvent("university visited, no learning yet");
	answerLater(questionID, 0);
}

void ClaudeBridgeAI::showTavernWindow(const CGObjectInstance * object, const CGHeroInstance * visitor, QuestionID questionID)
{
	addEvent("tavern visited, no hiring yet");
	answerLater(questionID, 0);
}

void ClaudeBridgeAI::heroExchangeStarted(ObjectInstanceID hero1, ObjectInstanceID hero2, QuestionID questionID)
{
	JsonNode question;
	question["kind"].String() = "hero_exchange";
	question["heroes"].Vector().emplace_back(static_cast<si64>(hero1.getNum()));
	question["heroes"].Vector().emplace_back(static_cast<si64>(hero2.getNum()));
	question["help"].String() = "use move_army or swap_stacks between the two heroes, then answer 0 to close";
	addQuestion(questionID, std::move(question));
}

void ClaudeBridgeAI::showTeleportDialog(const CGHeroInstance * hero, TeleportChannelID channel, TTeleportExitsList exits, bool impassable, QuestionID questionID)
{
	answerLater(questionID, 0);
}

void ClaudeBridgeAI::showGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down, QuestionID questionID, const MetaString & customTitle)
{
	answerLater(questionID, 0);
}

void ClaudeBridgeAI::showMapObjectSelectDialog(QuestionID questionID, const Component & icon, const MetaString & title, const MetaString & description, const std::vector<ObjectInstanceID> & objects)
{
	asyncTasks->run([this, questionID]()
	{
		std::shared_lock gsLock(CGameState::mutex);
		cb->sendQuestionAnswer(std::nullopt, questionID);
	});
}

std::optional<BattleAction> ClaudeBridgeAI::makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState)
{
	return std::nullopt;
}

JsonNode ClaudeBridgeAI::execute(const std::string & cmd, const JsonNode & args)
{
	if(cmd == "ping")
		return JsonNode("pong");

	if(observer)
	{
		static const std::set<std::string> readOnly = {"get_state", "status", "artifacts", "thieves_guild", "lookup", "battle_state", "inspect", "threats", "targets", "find_path", "tile_info", "get_tavern", "get_buildable"};
		if(!readOnly.count(cmd))
			throw std::runtime_error("spectator mode: '" + cmd + "' is not allowed, the human plays this player");
	}

	if(cmd == "wait_for_turn")
	{
		const auto timeout = std::chrono::milliseconds(args["timeoutMs"].isNumber() ? args["timeoutMs"].Integer() : 30000);
		waitForTurn(timeout);
		std::lock_guard lock(mx);
		JsonNode ret;
		ret["yourTurn"].Bool() = havingTurn;
		ret["battle"].Bool() = pendingBattle.has_value();
		return ret;
	}

	std::shared_lock gsLock(CGameState::mutex);
	{
		// CAdventureAI restores the flag after a battle; a request sent without waiting can leave it off
		std::lock_guard lock(mx);
		if(!battleOngoing)
			cb->waitTillRealize = true;
	}

	if(cmd == "get_state")
		return dumpState(args);
	if(cmd == "status")
		return statusReport();

	if(cmd == "artifacts" || cmd == "equip")
	{
		const auto * hero = cb->getHero(ObjectInstanceID(requireInt(args, "hero")));
		if(!hero || hero->getOwner() != playerID)
			throw std::runtime_error("no such hero");
		if(cmd == "equip")
			equipArtifact(hero, requireInt(args, "from"), args["to"].isNumber() ? std::optional<int>(args["to"].Integer()) : std::nullopt);
		return artifacts(hero);
	}
	if(cmd == "thieves_guild")
	{
		const auto * town = cb->getObj(ObjectInstanceID(requireInt(args, "town")));
		if(!town || town->getOwner() != playerID)
			throw std::runtime_error("town must be one of ours");
		return thievesGuild(town);
	}
	if(cmd == "lookup")
	{
		if(!args["kind"].isString() || !args["name"].isString())
			throw std::runtime_error("lookup needs kind (creature, building or spell) and name");
		return lookup(args["kind"].String(), args["name"].String(), args["faction"].isString() ? args["faction"].String() : "");
	}
	if(cmd == "battle_state")
		return battleState(args["moves"].isBool() && args["moves"].Bool(), args["unit"].isNumber() ? std::optional<uint32_t>(args["unit"].Integer()) : std::nullopt);
	if(cmd == "inspect")
	{
		const auto * object = cb->getObj(ObjectInstanceID(requireInt(args, "id")));
		if(!object)
			throw std::runtime_error("no such object");
		const CGHeroInstance * hero = nullptr;
		if(args["hero"].isNumber())
			hero = cb->getHero(ObjectInstanceID(requireInt(args, "hero")));
		return inspect(object, hero);
	}
	if(cmd == "upgrade")
	{
		const auto * h = cb->getHero(ObjectInstanceID(requireInt(args, "hero")));
		if(!h || h->getOwner() != playerID)
			throw std::runtime_error("no such hero");
		const SlotID slot(requireInt(args, "slot"));
		if(!h->getStackPtr(slot))
			throw std::runtime_error("no stack in that slot");
		const auto before = army(h).toCompactString();
		UpgradeInfo info(h->getStackPtr(slot)->getId());
		cb->fillUpgradeInfo(h, slot, info);
		if(!info.canUpgrade())
			throw std::runtime_error("no upgrade available here: the hero must stand in a town that has the upgraded dwelling");
		// the server crashes on CreatureID::NONE, so name the upgrade explicitly
		cb->upgradeCreature(h, slot, info.getUpgrade());
		JsonNode ret;
		ret["army"] = army(h);
		if(before == ret["army"].toCompactString())
			throw std::runtime_error("nothing changed: the hero must stand in a town with the upgraded dwelling, and the cost must be affordable");
		return ret;
	}
	if(cmd == "buy_spellbook")
	{
		const auto * h = cb->getHero(ObjectInstanceID(requireInt(args, "hero")));
		if(!h || h->getOwner() != playerID)
			throw std::runtime_error("no such hero");
		if(h->hasSpellbook())
			throw std::runtime_error("the hero already has a spellbook");
		cb->buyArtifact(h, ArtifactID::SPELLBOOK);
		JsonNode ret;
		ret["requested"].Bool() = true;
		ret["status"] = statusReport();
		return ret;
	}
	if(cmd == "save_game")
	{
		cb->save(requireString(args, "name"), false);
		JsonNode ret;
		ret["saved"].String() = requireString(args, "name");
		return ret;
	}
	if(cmd == "threats")
	{
		const CGHeroInstance * best = nullptr;
		if(args["hero"].isNumber())
			best = cb->getHero(ObjectInstanceID(requireInt(args, "hero")));
		else
			for(const auto * h : cb->getHeroesInfo())
				if(!best || h->estimateHeroCombatValue() > best->estimateHeroCombatValue())
					best = h;
		return threats(best);
	}
	if(cmd == "targets")
	{
		const auto * h = cb->getHero(ObjectInstanceID(requireInt(args, "hero")));
		if(!h || h->getOwner() != playerID)
			throw std::runtime_error("no such hero");
		return targets(h, args["limit"].isNumber() ? static_cast<size_t>(args["limit"].Integer()) : 40);
	}
	if(cmd == "battle_act")
		return battleAct(args);
	if(cmd == "answer_question")
		return answerQuestion(QuestionID(requireInt(args, "id")), requireInt(args, "choice"));

	if(!havingTurn)
		throw std::runtime_error("it is not our turn");

	// the server drops these (and logs "has to answer activities") while a dialog waits for an answer
	if(cmd == "build" || cmd == "recruit" || cmd == "buy_hero" || cmd == "trade" || cmd == "build_boat" || cmd == "move_hero")
	{
		std::lock_guard lock(mx);
		for(const auto & [id, question] : pendingQuestions)
			if(question["kind"].String() != "hero_exchange" && !(cmd == "recruit" && question["kind"].String() == "recruitment"))
				throw std::runtime_error("answer pending question " + std::to_string(id.getNum()) + " first (answer_question)");
	}

	if(cmd == "find_path")
	{
		const auto * h = cb->getHero(ObjectInstanceID(requireInt(args, "hero")));
		if(!h || h->getOwner() != playerID)
			throw std::runtime_error("no such hero");
		return findPath(h, readPosition(args));
	}
	if(cmd == "move_hero")
		return moveHero(ObjectInstanceID(requireInt(args, "hero")), readPosition(args));
	if(cmd == "end_turn")
		return endTurn();

	if(cmd == "move_army" || cmd == "swap_stacks")
	{
		const auto * from = dynamic_cast<const CArmedInstance *>(cb->getObj(ObjectInstanceID(requireInt(args, "from"))));
		const auto * to = dynamic_cast<const CArmedInstance *>(cb->getObj(ObjectInstanceID(requireInt(args, "to"))));
		if(!from || !to || from->getOwner() != playerID || to->getOwner() != playerID)
			throw std::runtime_error("from and to must be our heroes or towns");
		if(cmd == "move_army")
			return moveArmy(from, to);
		return swapStacks(from, to, SlotID(requireInt(args, "fromSlot")), SlotID(requireInt(args, "toSlot")), args["count"].isNumber() ? args["count"].Integer() : 0);
	}
	if(cmd == "tile_info")
		return tileInfo(readPosition(args), args["radius"].isNumber() ? args["radius"].Integer() : 6);
	if(cmd == "build_boat")
	{
		const auto * yard = dynamic_cast<const IShipyard *>(cb->getObj(ObjectInstanceID(requireInt(args, "shipyard"))));
		if(!yard)
			throw std::runtime_error("no such shipyard");
		return buildBoat(yard);
	}
	if(cmd == "recruit" && args["object"].isNumber())
	{
		const auto * dwelling = dynamic_cast<const CGDwelling *>(cb->getObj(ObjectInstanceID(args["object"].Integer())));
		const auto * h = cb->getHero(ObjectInstanceID(requireInt(args, "hero")));
		if(!dwelling || !h || h->getOwner() != playerID)
			throw std::runtime_error("no such dwelling or hero");
		return recruit(dwelling, h, requireString(args, "creature"), requireInt(args, "count"));
	}

	const auto * t = cb->getTown(ObjectInstanceID(requireInt(args, "town")));
	if(!t || t->getOwner() != playerID)
		throw std::runtime_error("no such town");

	if(cmd == "trade")
		return trade(t, readResource(args, "give"), readResource(args, "get"), requireInt(args, "amount"));
	if(cmd == "get_tavern")
		return tavern(t);
	if(cmd == "buy_hero")
		return buyHero(t, ObjectInstanceID(requireInt(args, "hero")));
	if(cmd == "get_buildable")
		return buildable(t, args["all"].isBool() && args["all"].Bool());
	if(cmd == "build")
		return build(t, requireString(args, "building"));
	if(cmd == "recruit")
	{
		const bool toGarrison = args["garrison"].isBool() && args["garrison"].Bool();
		const CArmedInstance * destination = (t->getVisitingHero() && !toGarrison) ? static_cast<const CArmedInstance *>(t->getVisitingHero()) : t;
		return recruit(t, destination, requireString(args, "creature"), requireInt(args, "count"));
	}

	throw std::runtime_error("unknown command \"" + cmd + "\"");
}

JsonNode ClaudeBridgeAI::dumpState(const JsonNode & args)
{
	JsonNode ret;
	const auto calendar = cb->getCalendar();

	ret["player"].String() = GameConstants::PLAYER_COLOR_NAMES[playerID.getNum()];
	ret["day"].Integer() = calendar.getCurrentDay();
	ret["month"].Integer() = calendar.getMonth();
	ret["week"].Integer() = calendar.getWeek();
	ret["dayOfWeek"].Integer() = calendar.getDayOfWeek();
	ret["mapSize"] = position(cb->getMapSize());
	ret["resources"] = resources(cb->getResourceAmount());

	ret["heroes"].Vector();
	for(const auto * h : cb->getHeroesInfo())
		ret["heroes"].Vector().push_back(hero(h));

	ret["towns"].Vector();
	for(const auto * t : cb->getTownsInfo())
		ret["towns"].Vector().push_back(town(t));

	// optional filter: only objects within "radius" tiles (same level) of the given hero
	std::optional<int3> center;
	const int radius = args["radius"].isNumber() ? args["radius"].Integer() : 0;
	if(radius > 0)
	{
		const auto * h = cb->getHero(ObjectInstanceID(requireInt(args, "hero")));
		if(!h)
			throw std::runtime_error("no such hero");
		center = h->visitablePos();
	}

	ret["visibleObjects"].Vector();
	for(const auto * obj : cb->getAllVisitableObjs())
	{
		const int3 pos = obj->visitablePos();
		if(center && (pos.z != center->z || std::max(std::abs(pos.x - center->x), std::abs(pos.y - center->y)) > radius))
			continue;
		JsonNode entry;
		entry["id"].Integer() = obj->id.getNum();
		entry["type"].String() = obj->getTypeName();
		entry["subtype"].String() = obj->getSubtypeName();
		entry["name"].String() = objectName(obj);
		entry["pos"] = position(obj->visitablePos());
		entry["owner"].Integer() = obj->getOwner().getNum();
		if(const auto * monster = dynamic_cast<const CGCreature *>(obj); monster && !monster->stacks.empty())
			entry["size"].String() = CCreature::getQuantityRangeStringForId(monster->stacks.begin()->second->getQuantityID());
		ret["visibleObjects"].Vector().push_back(entry);
	}

	ret["status"] = statusReport();
	return ret;
}

JsonNode ClaudeBridgeAI::statusReport()
{
	std::lock_guard lock(mx);
	JsonNode ret;
	ret["yourTurn"].Bool() = havingTurn;
	ret["busy"].Bool() = heroMoving || battleOngoing || visitsInProgress != 0;
	ret["battle"].Bool() = pendingBattle.has_value();

	ret["pendingQuestions"].Vector();
	for(const auto & [id, question] : pendingQuestions)
		ret["pendingQuestions"].Vector().push_back(question);

	ret["events"].Vector();
	for(const auto & text : events)
		ret["events"].Vector().emplace_back(text);
	events.clear();
	return ret;
}

JsonNode ClaudeBridgeAI::findPath(const CGHeroInstance * hero, const int3 & dst)
{
	pathfinder->invalidatePaths();
	const auto paths = pathfinder->getPathsInfo(hero);

	JsonNode ret;
	CGPath path;
	ret["reachable"].Bool() = paths->getPath(path, dst);
	if(ret["reachable"].Bool())
	{
		ret["steps"].Integer() = path.nodes.size() - 1;
		ret["turns"].Integer() = path.nodes.front().turns;
		ret["movementLeftAtEnd"].Integer() = path.nodes.front().moveRemains;
	}
	return ret;
}

JsonNode ClaudeBridgeAI::moveHero(ObjectInstanceID heroID, const int3 & dst)
{
	JsonNode ret;
	ret["reached"].Bool() = false;

	for(int batch = 0; batch < MAX_MOVEMENT_BATCHES; batch++)
	{
		const auto * h = cb->getHero(heroID);
		if(!h || h->getOwner() != playerID)
		{
			ret["stopped"].String() = "hero is gone";
			break;
		}

		const int3 start = h->visitablePos();
		const int movementBefore = h->movementPointsRemaining();
		// the Space key of the client: visit the object the hero is standing on
		const bool visitHere = start == dst && batch == 0 && cb->getVisitableObjs(start).size() > 1;
		if(start == dst && !visitHere)
		{
			ret["reached"].Bool() = true;
			break;
		}

		CGPath path;
		if(!visitHere)
		{
			pathfinder->invalidatePaths();
			const auto paths = pathfinder->getPathsInfo(h);
			if(!paths->getPath(path, dst))
			{
				ret["stopped"].String() = "no path";
				break;
			}
			if(path.nextNode().turns != 0)
			{
				ret["stopped"].String() = "not enough movement points this turn";
				break;
			}
		}

		if(visitHere || path.nextNode().isTeleportAction())
		{
			sendWithoutWaiting([&]() { cb->moveHero(h, h->pos, false); });
		}
		else
		{
			const auto & nextNode = path.nextNode();
			const auto layer = nextNode.layer;
			const bool useTransit = layer == EPathfindingLayer::AIR || layer == EPathfindingLayer::WATER;
			std::vector<int3> steps;

			for(const auto & node : std::views::reverse(path.nodes))
			{
				if(node.coord == start)
					continue;
				if(node.isTeleportAction() || node.turns != 0 || node.layer != layer)
					break;

				steps.push_back(h->convertFromVisitablePos(node.coord));
				if(cb->guardingCreaturePosition(node.coord) != int3(-1, -1, -1))
					break;
				if(!cb->getVisitableObjs(node.coord).empty())
					break;
			}
			sendWithoutWaiting([&]() { cb->moveHero(h, steps, useTransit, layer); });
		}

		const bool idle = waitTillIdle(ACTION_TIMEOUT);
		{
			std::lock_guard lock(mx);
			if(!pendingQuestions.empty())
			{
				ret["stopped"].String() = "question pending";
				break;
			}
			if(pendingBattle)
			{
				ret["stopped"].String() = "battle waiting for orders";
				break;
			}
		}
		if(!idle)
		{
			ret["stopped"].String() = "timeout waiting for the server";
			break;
		}

		if(visitHere)
		{
			ret["visited"].Bool() = true;
			ret["reached"].Bool() = true;
			break;
		}

		const auto * after = cb->getHero(heroID);
		if(after && after->visitablePos() == start)
		{
			// pickups and similar visits spend movement without changing the hero position
			if(after->movementPointsRemaining() < movementBefore)
				ret["visited"].Bool() = true;
			else
				ret["stopped"].String() = "hero did not move";
			break;
		}
	}

	if(const auto * h = cb->getHero(heroID))
	{
		ret["pos"] = position(h->visitablePos());
		ret["movement"].Integer() = h->movementPointsRemaining();
	}
	ret["status"] = statusReport();
	return ret;
}

JsonNode ClaudeBridgeAI::buildable(const CGTownInstance * town, bool all) const
{
	JsonNode ret;
	ret.Vector();
	for(const auto & [id, building] : town->getTown()->buildings)
	{
		const auto state = cb->canBuildStructure(town, id);
		if(state == EBuildingState::ALREADY_PRESENT || state == EBuildingState::FORBIDDEN)
			continue;
		if(!all && (state != EBuildingState::ALLOWED && state != EBuildingState::NO_RESOURCES && state != EBuildingState::CANT_BUILD_TODAY))
			continue;
		if(building->getNameTranslated().empty())
			continue;

		JsonNode entry;
		entry["building"].String() = building->getJsonKey();
		entry["name"].String() = building->getNameTranslated();
		entry["state"].String() = buildingStateName(state);
		entry["cost"] = resources(building->resources);
		ret.Vector().push_back(entry);
	}
	return ret;
}

JsonNode ClaudeBridgeAI::build(const CGTownInstance * town, const std::string & building)
{
	for(const auto & [id, candidate] : town->getTown()->buildings)
	{
		if(!keyMatches(candidate->getJsonKey(), building))
			continue;

		JsonNode ret;
		ret["built"].Bool() = cb->canBuildStructure(town, id) == EBuildingState::ALLOWED && cb->buildBuilding(town, id);
		ret["status"] = statusReport();
		return ret;
	}
	throw std::runtime_error("town has no building \"" + building + "\"");
}

JsonNode ClaudeBridgeAI::recruit(const CGDwelling * dwelling, const CArmedInstance * destination, const std::string & creature, int count)
{
	if(count <= 0)
		throw std::runtime_error("count must be positive");

	for(size_t level = 0; level < dwelling->creatures.size(); level++)
	{
		for(const auto & id : dwelling->creatures[level].second)
		{
			if(!keyMatches(id.toEntity(LIBRARY)->getJsonKey(), creature))
				continue;

			if(count > static_cast<int>(dwelling->creatures[level].first))
				throw std::runtime_error("only " + std::to_string(dwelling->creatures[level].first) + " available");
			const ResourceSet cost = id.toEntity(LIBRARY)->getFullRecruitCost() * count;
			if(!cb->getResourceAmount().canAfford(cost))
				throw std::runtime_error("not enough resources: need " + std::to_string(cost[EGameResID::GOLD]) + " gold, have " + std::to_string(cb->getResourceAmount()[EGameResID::GOLD]));

			cb->recruitCreatures(dwelling, destination, id, count, level);

			JsonNode ret;
			ret["army"] = army(destination);
			ret["status"] = statusReport();
			return ret;
		}
	}
	throw std::runtime_error("cannot recruit \"" + creature + "\" here");
}

JsonNode ClaudeBridgeAI::buildBoat(const IShipyard * shipyard)
{
	static const std::array<const char *, 5> states = {"good", "boat_already_built", "tile_blocked", "no_water", "unknown"};
	JsonNode ret;
	ResourceSet cost;
	shipyard->getBoatCost(cost);
	ret["cost"] = resources(cost);
	ret["state"].String() = states.at(shipyard->shipyardStatus());
	if(shipyard->shipyardStatus() == IBoatGenerator::GOOD)
	{
		const auto * yardObject = dynamic_cast<const CGObjectInstance *>(shipyard);
		if(yardObject && yardObject->getOwner() != playerID)
			throw std::runtime_error("the shipyard is not ours: a hero must visit it first");
		cb->buildBoat(shipyard);
		ret["requested"].Bool() = true;
	}
	ret["status"] = statusReport();
	return ret;
}

JsonNode ClaudeBridgeAI::moveArmy(const CArmedInstance * from, const CArmedInstance * to)
{
	std::vector<SlotID> slots;
	for(const auto & [slot, stack] : from->Slots())
		slots.push_back(slot);

	for(const auto & slot : slots)
	{
		const auto * stack = from->getStackPtr(slot);
		if(!stack)
			continue;

		SlotID target = SlotID();
		for(const auto & [toSlot, toStack] : to->Slots())
			if(toStack->getCreatureID() == stack->getCreatureID())
				target = toSlot;
		if(!target.validSlot())
		{
			const auto free = to->getFreeSlots();
			if(free.empty())
				continue;
			target = free.front();
		}
		cb->mergeOrSwapStacks(from, to, slot, target);
	}

	JsonNode ret;
	ret["from"] = army(from);
	ret["to"] = army(to);
	return ret;
}

JsonNode ClaudeBridgeAI::swapStacks(const CArmedInstance * from, const CArmedInstance * to, SlotID fromSlot, SlotID toSlot, int count)
{
	const auto before = army(from).toCompactString() + army(to).toCompactString();
	if(count > 0)
	{
		const auto * source = from->getStackPtr(fromSlot);
		if(!source || count > source->getCount())
			throw std::runtime_error("count exceeds the stack size");
		const auto * target = to->getStackPtr(toSlot);
		if(target && target->getCreatureID() != source->getCreatureID())
			throw std::runtime_error("the target slot holds a different creature");
		// the server sets the target stack to this size, so a non-empty target needs its own count added
		cb->splitStack(from, to, fromSlot, toSlot, target ? target->getCount() + count : count);
	}
	else
		cb->mergeOrSwapStacks(from, to, fromSlot, toSlot);

	JsonNode ret;
	ret["from"] = army(from);
	ret["to"] = army(to);
	if(before == ret["from"].toCompactString() + ret["to"].toCompactString())
		throw std::runtime_error("nothing changed: the server rejected the request; a hero must stand in the town, or two heroes must have an open exchange (walk one onto the other)");
	return ret;
}

JsonNode ClaudeBridgeAI::trade(const CGTownInstance * market, GameResID give, GameResID get, int amount)
{
	if(amount <= 0)
		throw std::runtime_error("amount must be positive");
	if(!market->hasBuiltResourceMarketplace())
		throw std::runtime_error("the town has no marketplace");

	int giveBase = 1;
	int getBase = 1;
	market->getOffer(give.getNum(), get.getNum(), giveBase, getBase, EMarketMode::RESOURCE_RESOURCE);
	amount = amount / giveBase * giveBase;
	if(amount == 0)
		throw std::runtime_error("the rate is " + std::to_string(giveBase) + " " + GameConstants::RESOURCE_NAMES[give.getNum()] + " for " + std::to_string(getBase) + " " + GameConstants::RESOURCE_NAMES[get.getNum()] + "; amount must be a multiple of " + std::to_string(giveBase));

	cb->trade(market->id, EMarketMode::RESOURCE_RESOURCE, TradeItemSell(give), TradeItemBuy(get), amount);

	JsonNode ret;
	ret["rate"]["give"].Integer() = giveBase;
	ret["rate"]["get"].Integer() = getBase;
	ret["resources"] = resources(cb->getResourceAmount());
	ret["status"] = statusReport();
	return ret;
}

JsonNode ClaudeBridgeAI::tileInfo(const int3 & center, int radius) const
{
	radius = std::clamp(radius, 1, 15);
	const int3 size = cb->getMapSize();

	JsonNode ret;
	ret["origin"] = position(int3(center.x - radius, center.y - radius, center.z));
	ret["legend"].String() = "? fog, ~ water, . land, # rock, v visitable object, X blocking object, B boat; one row per y, one char per x from origin";
	ret["rows"].Vector();
	ret["objects"].Vector();
	std::set<ObjectInstanceID> listed;

	for(int y = center.y - radius; y <= center.y + radius; y++)
	{
		std::string row;
		for(int x = center.x - radius; x <= center.x + radius; x++)
		{
			const int3 pos(x, y, center.z);
			if(x < 0 || y < 0 || x >= size.x || y >= size.y || !cb->isVisible(pos))
			{
				row += '?';
				continue;
			}

			const auto * tile = cb->getTile(pos);
			char symbol = tile->isWater() ? '~' : (tile->getTerrain()->isPassable() ? '.' : '#');

			auto note = [&](const std::vector<ObjectInstanceID> & ids, char mark, bool listAll)
			{
				for(const auto & id : ids)
				{
					const auto * obj = cb->tryGetObj(id);
					if(!obj)
						continue;
					symbol = obj->ID == Obj::BOAT ? 'B' : mark;
					const bool interesting = listAll || obj->ID == Obj::HERO || obj->ID == Obj::BOAT || obj->ID == Obj::MONSTER;
					if(interesting && listed.insert(id).second)
					{
						JsonNode entry;
						entry["id"].Integer() = id.getNum();
						entry["type"].String() = obj->getTypeName();
						entry["pos"] = position(obj->visitablePos());
						ret["objects"].Vector().push_back(entry);
					}
				}
			};
			note(tile->visitableObjects, 'v', true);
			note(tile->blockingObjects, 'X', false);
			row += symbol;
		}
		ret["rows"].Vector().emplace_back(row);
	}
	return ret;
}

JsonNode ClaudeBridgeAI::tavern(const CGTownInstance * town) const
{
	JsonNode ret;
	ret.Vector();
	for(const auto * h : cb->getAvailableHeroes(town))
	{
		JsonNode entry;
		entry["id"].Integer() = h->id.getNum();
		entry["name"].String() = h->getHeroType()->getNameTranslated();
		entry["class"].String() = h->getHeroClass()->getJsonKey();
		entry["army"] = army(h);
		ret.Vector().push_back(entry);
	}
	return ret;
}

JsonNode ClaudeBridgeAI::buyHero(const CGTownInstance * town, ObjectInstanceID heroID)
{
	for(const auto * h : cb->getAvailableHeroes(town))
	{
		if(h->id != heroID)
			continue;

		if(town->getVisitingHero())
			throw std::runtime_error("the visiting slot is taken; move the hero out first");
		cb->recruitHero(town, h);

		JsonNode ret;
		ret["resources"] = resources(cb->getResourceAmount());
		ret["status"] = statusReport();
		return ret;
	}
	throw std::runtime_error("that hero is not in the tavern");
}

void ClaudeBridgeAI::requestSent(const CPackForServer * pack, int requestID)
{
	std::lock_guard lock(mx);
	requestsInFlight.insert(requestID);
}

void ClaudeBridgeAI::requestRealized(PackageApplied * pa)
{
	std::lock_guard lock(mx);
	requestsInFlight.erase(pa->requestID);
	if(!pa->result)
		rejectedRequests++;
	cv.notify_all();
}

template<typename Request>
void ClaudeBridgeAI::sendWithoutWaiting(Request && request)
{
	cb->waitTillRealize = false;
	request();
}

void ClaudeBridgeAI::battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed)
{
	{
		std::lock_guard lock(mx);
		autoFight = false;
		events.push_back("battle started");
		if(observer)
			pendingBattle = PendingBattle{battleID, 0};
	}
	CAdventureAI::battleStart(battleID, army1, army2, tile, hero1, hero2, side, replayAllowed);
}

void ClaudeBridgeAI::battleEnd(const BattleID & battleID, const BattleResult * br, QuestionID questionID)
{
	CAdventureAI::battleEnd(battleID, br, questionID);
	// the server asks the only human-flagged side to accept the result of a battle against a neutral or AI side
	if(questionID != QuestionID::NONE)
		answerLater(questionID, 0);
	std::lock_guard lock(mx);
	pendingBattle.reset();
	autoFight = false;
	cv.notify_all();
}

void ClaudeBridgeAI::activeStack(const BattleID & battleID, const CStack * stack)
{
	bool manual;
	{
		std::lock_guard lock(mx);
		manual = !autoFight && server && server->hasClient();
		if(manual)
		{
			pendingBattle = PendingBattle{battleID, stack->unitId()};
			cv.notify_all();
		}
	}
	if(!manual)
		CAdventureAI::activeStack(battleID, stack);
}

void ClaudeBridgeAI::yourTacticPhase(const BattleID & battleID, int distance)
{
	const auto battle = cb->getBattle(battleID);
	const auto stacks = battle->battleGetStacks(CPlayerBattleCallback::ONLY_MINE);
	bool manual;
	{
		std::lock_guard lock(mx);
		manual = !autoFight && server && server->hasClient() && !stacks.empty();
		if(manual)
		{
			pendingBattle = PendingBattle{battleID, stacks.front()->unitId(), true};
			cv.notify_all();
		}
	}
	if(!manual)
		CAdventureAI::yourTacticPhase(battleID, distance);
}

bool ClaudeBridgeAI::waitBattleProgress(std::chrono::milliseconds timeout)
{
	auto unlock = vstd::makeUnlockSharedGuard(CGameState::mutex);
	std::unique_lock lock(mx);
	return cv.wait_for(lock, timeout, [this]() { return pendingBattle.has_value() || !battleOngoing; });
}

namespace
{

const char * quantityName(int quantityId)
{
	static const std::array<const char *, 10> names = {"none", "1-4", "5-9", "10-19", "20-49", "50-99", "100-249", "250-499", "500-999", "1000+"};
	return names.at(std::clamp(quantityId, 0, 9));
}

JsonNode armyInfo(const InfoAboutArmy & info)
{
	JsonNode ret;
	ret["owner"].Integer() = info.owner.getNum();
	ret["name"].String() = info.name.toString(LIBRARY->staticTexts());
	ret["detailed"].Bool() = info.army.isDetailed;
	ret["army"].Vector();
	for(const auto & [slot, stack] : info.army)
	{
		JsonNode entry;
		entry["slot"].Integer() = slot.getNum();
		entry["creature"].String() = stack.getId().toEntity(LIBRARY)->getJsonKey();
		if(info.army.isDetailed)
			entry["count"].Integer() = stack.getCount();
		else
			entry["count"].String() = quantityName(stack.getCount());
		ret["army"].Vector().push_back(entry);
	}
	return ret;
}

/// Sum of count * AI value; quantity ranges of undetailed armies count as their midpoint.
double armyValue(const InfoAboutArmy & info)
{
	static const std::array<double, 10> midpoints = {0, 2.5, 7, 14.5, 34.5, 74.5, 174.5, 374.5, 749.5, 1500};
	double sum = 0;
	for(const auto & [slot, stack] : info.army)
	{
		const double count = info.army.isDetailed ? stack.getCount() : midpoints.at(std::clamp<int>(stack.getCount(), 0, 9));
		sum += count * stack.getId().toEntity(LIBRARY)->getAIValue();
	}
	return sum;
}

double armyValue(const CCreatureSet * set)
{
	double sum = 0;
	for(const auto & [slot, stack] : set->Slots())
		sum += static_cast<double>(stack->getCount()) * stack->getCreature()->getAIValue();
	return sum;
}

const char * threatLabel(double ratio)
{
	static const std::array<double, 11> bounds = {0.1, 0.25, 0.6, 0.9, 1.1, 1.3, 1.8, 2.5, 4, 8, 20};
	static const std::array<const char *, 12> labels = {"effortless", "very weak", "weak", "a bit weaker", "equal", "a bit stronger",
		"strong", "very strong", "challenging", "overpowering", "deadly", "impossible"};
	size_t band = 0;
	while(band < bounds.size() && ratio >= bounds[band])
		band++;
	return labels[band];
}

void addComparison(JsonNode & ret, double theirValue, const CGHeroInstance * compareTo)
{
	ret["estimatedValue"].Float() = theirValue;
	if(!compareTo)
		return;
	const double ours = armyValue(compareTo);
	const double ratio = theirValue / std::max(1.0, ours);
	ret["ourValue"].Float() = ours;
	ret["strengthRatio"].Float() = ratio;
	ret["threat"].String() = threatLabel(ratio);
}

}

JsonNode ClaudeBridgeAI::monsterInfo(const CGCreature * monster, const CGHeroInstance * compareTo) const
{
	if(monster->stacks.empty())
		throw std::runtime_error("empty monster stack");

	const auto & stack = monster->stacks.begin()->second;
	JsonNode ret;
	ret["type"].String() = "monster";
	ret["creature"].String() = stack->getCreatureID().toEntity(LIBRARY)->getJsonKey();
	ret["level"].Integer() = stack->getCreature()->getLevel();
	ret["ranged"].Bool() = stack->getCreature()->getBonusBearer()->hasBonusOfType(BonusType::SHOOTER);
	ret["size"].String() = CCreature::getQuantityRangeStringForId(stack->getQuantityID());

	if(compareTo)
	{
		static const std::array<double, 11> bounds = {0.1, 0.25, 0.6, 0.9, 1.1, 1.3, 1.8, 2.5, 4, 8, 20};
		static const std::array<const char *, 12> labels = {"effortless", "very weak", "weak", "a bit weaker", "equal", "a bit stronger",
			"strong", "very strong", "challenging", "overpowering", "deadly", "impossible"};
		const double ratio = static_cast<double>(monster->estimateCombatValue()) / std::max<ui64>(1, compareTo->estimateHeroCombatValue());
		size_t band = 0;
		while(band < bounds.size() && ratio >= bounds[band])
			band++;
		ret["threat"].String() = labels[band];
		ret["strengthRatio"].Float() = ratio;
	}
	return ret;
}

namespace
{

JsonNode artifactEntry(const CArtifactInstance * art, int slot)
{
	JsonNode entry;
	entry["slot"].Integer() = slot;
	entry["key"].String() = art->getType()->getJsonKey();
	entry["name"].String() = art->getType()->getNameTranslated();
	entry["description"].String() = art->getType()->getDescriptionTranslated();
	entry["fitsSlots"].Vector();
	const auto & possible = art->getType()->getPossibleSlots();
	if(const auto it = possible.find(ArtBearer::HERO); it != possible.end())
		for(const auto & position : it->second)
			entry["fitsSlots"].Vector().emplace_back(static_cast<si64>(position.getNum()));
	return entry;
}

}

JsonNode ClaudeBridgeAI::artifacts(const CGHeroInstance * hero) const
{
	JsonNode ret;
	ret["slotNames"].String() = "0 head, 1 shoulders, 2 neck, 3 right hand, 4 left hand, 5 torso, 6 right ring, 7 left ring, 8 feet, 9-12 misc1-4, 13-16 war machines, 17 spellbook, 18 misc5, 19+ backpack";
	ret["worn"].Vector();
	for(const auto & [position, info] : hero->artifactsWorn)
		if(const auto * art = hero->getArt(position))
			ret["worn"].Vector().push_back(artifactEntry(art, position.getNum()));
	ret["backpack"].Vector();
	for(size_t i = 0; i < hero->artifactsInBackpack.size(); i++)
	{
		const int slot = ArtifactPosition::BACKPACK_START + static_cast<int>(i);
		if(const auto * art = hero->getArt(ArtifactPosition(slot)))
			ret["backpack"].Vector().push_back(artifactEntry(art, slot));
	}
	return ret;
}

void ClaudeBridgeAI::equipArtifact(const CGHeroInstance * hero, int from, std::optional<int> to)
{
	const auto * art = hero->getArt(ArtifactPosition(from));
	if(!art)
		throw std::runtime_error("no artifact in that slot");

	int target;
	if(to)
	{
		target = *to;
	}
	else
	{
		target = -1;
		const auto & possible = art->getType()->getPossibleSlots();
		if(const auto it = possible.find(ArtBearer::HERO); it != possible.end())
		{
			for(const auto & position : it->second)
			{
				if(hero->isPositionFree(position))
				{
					target = position.getNum();
					break;
				}
			}
		}
		if(target < 0)
			throw std::runtime_error("no free slot fits that artifact; pass to explicitly to swap it with a worn one");
	}

	if(!cb->swapArtifacts(ArtifactLocation(hero->id, ArtifactPosition(from)), ArtifactLocation(hero->id, ArtifactPosition(target))))
		throw std::runtime_error("the server did not accept that move");
}

JsonNode ClaudeBridgeAI::thievesGuild(const CGObjectInstance * town) const
{
	SThievesGuildInfo info;
	cb->getThievesGuildInfo(info, town);

	const auto colorName = [](const PlayerColor & color)
	{
		return GameConstants::PLAYER_COLOR_NAMES[color.getNum()];
	};

	JsonNode ret;
	const std::vector<std::pair<std::string, const std::vector<std::vector<PlayerColor>> *>> categories = {
		{"towns", &info.numOfTowns}, {"heroes", &info.numOfHeroes}, {"gold", &info.gold}, {"woodOre", &info.woodOre},
		{"rareResources", &info.mercSulfCrystGems}, {"obelisks", &info.obelisks}, {"artifacts", &info.artifacts},
		{"army", &info.army}, {"income", &info.income}};
	for(const auto & [name, places] : categories)
	{
		ret["ranking"][name].Vector();
		for(const auto & place : *places)
		{
			JsonNode entry;
			entry.Vector();
			for(const auto & color : place)
				entry.Vector().emplace_back(colorName(color));
			ret["ranking"][name].Vector().push_back(entry);
		}
	}
	for(const auto & [color, hero] : info.colorToBestHero)
		if(hero.portraitSource.isValid())
			ret["bestHero"][colorName(color)].String() = hero.name.toString(LIBRARY->staticTexts());
	for(const auto & [color, creature] : info.bestCreature)
		if(creature.hasValue())
			ret["bestCreature"][colorName(color)].String() = creature.toEntity(LIBRARY)->getJsonKey();
	return ret;
}

JsonNode ClaudeBridgeAI::lookup(const std::string & kind, const std::string & query, const std::string & factionFilter) const
{
	const auto lower = [](std::string text)
	{
		std::ranges::transform(text, text.begin(), [](unsigned char c) { return std::tolower(c); });
		return text;
	};
	const std::string needle = lower(query);
	const auto matches = [&](const std::vector<std::string> & texts)
	{
		return std::ranges::any_of(texts, [&](const std::string & text) { return lower(text).find(needle) != std::string::npos; });
	};
	constexpr size_t limit = 6;

	JsonNode ret;
	ret["matches"].Vector();

	if(kind == "creature")
	{
		for(const auto & c : LIBRARY->creh->objects)
		{
			if(!c || !matches({c->getJsonKey(), c->getNameSingularTranslated(), c->getNamePluralTranslated()}))
				continue;
			JsonNode entry;
			entry["key"].String() = c->getJsonKey();
			entry["name"].String() = c->getNameSingularTranslated();
			entry["faction"].String() = c->getFactionID().toEntity(LIBRARY)->getJsonKey();
			entry["level"].Integer() = c->getLevel();
			entry["attack"].Integer() = c->getBaseAttack();
			entry["defense"].Integer() = c->getBaseDefense();
			entry["damage"].String() = std::to_string(c->getBaseDamageMin()) + "-" + std::to_string(c->getBaseDamageMax());
			entry["hp"].Integer() = c->getBaseHitPoints();
			entry["speed"].Integer() = c->getBaseSpeed();
			entry["shots"].Integer() = c->getBaseShots();
			entry["growth"].Integer() = c->getGrowth();
			entry["cost"] = resources(c->getFullRecruitCost());
			entry["doubleWide"].Bool() = c->isDoubleWide();
			entry["aiValue"].Integer() = c->getAIValue();
			entry["upgradesTo"].Vector();
			for(const auto & upgrade : c->upgrades)
				entry["upgradesTo"].Vector().emplace_back(upgrade.toEntity(LIBRARY)->getJsonKey());
			entry["abilities"].Vector();
			std::set<std::string> seen;
			for(const auto & bonus : c->getExportedBonusList())
			{
				const std::string text = LIBRARY->bth->bonusToString(bonus);
				if(!text.empty() && seen.insert(text).second)
					entry["abilities"].Vector().emplace_back(text);
			}
			const std::string description = c->getDescriptionTranslated();
			if(!description.empty())
				entry["description"].String() = description;
			ret["matches"].Vector().push_back(entry);
			if(ret["matches"].Vector().size() >= limit)
				break;
		}
	}
	else if(kind == "building")
	{
		for(const auto & faction : LIBRARY->townh->objects)
		{
			if(!faction || !faction->town || (!factionFilter.empty() && lower(faction->getJsonKey()).find(lower(factionFilter)) == std::string::npos))
				continue;
			const auto & buildings = faction->town->buildings;
			const auto printer = [&](const BuildingID & id)
			{
				const auto it = buildings.find(id);
				JsonNode node;
				node.String() = it != buildings.end() ? it->second->getJsonKey() : std::to_string(id.getNum());
				return node;
			};
			for(const auto & [id, b] : buildings)
			{
				if(!matches({b->getJsonKey(), b->getNameTranslated()}))
					continue;
				JsonNode entry;
				entry["faction"].String() = faction->getJsonKey();
				entry["key"].String() = b->getJsonKey();
				entry["name"].String() = b->getNameTranslated();
				entry["description"].String() = b->getDescriptionTranslated();
				entry["cost"] = resources(b->resources);
				entry["producesPerDay"] = resources(b->produce);
				entry["requires"] = std::visit(LogicalExpressionDetail::Writer<BuildingID>(printer), b->requirements.get());
				if(b->upgrade.hasValue())
					entry["upgrades"] = printer(b->upgrade);
				ret["matches"].Vector().push_back(entry);
				if(ret["matches"].Vector().size() >= limit)
					return ret;
			}
		}
	}
	else if(kind == "spell")
	{
		for(const auto & spell : LIBRARY->spellh->objects)
		{
			if(!spell || !matches({spell->getJsonKey(), spell->getNameTranslated()}))
				continue;
			JsonNode entry;
			entry["key"].String() = spell->getJsonKey();
			entry["name"].String() = spell->getNameTranslated();
			entry["level"].Integer() = spell->getLevel();
			entry["adventure"].Bool() = spell->isAdventure();
			entry["combat"].Bool() = spell->isCombat();
			entry["positive"].Bool() = spell->isPositive();
			entry["negative"].Bool() = spell->isNegative();
			for(int skillLevel = 0; skillLevel < 4; skillLevel++)
				entry["cost"].Vector().emplace_back(static_cast<si64>(spell->getCost(skillLevel)));
			entry["description"].String() = spell->getDescriptionTranslated(0);
			ret["matches"].Vector().push_back(entry);
			if(ret["matches"].Vector().size() >= limit)
				break;
		}
	}
	else
		throw std::runtime_error("lookup kind must be creature, building or spell");

	return ret;
}

JsonNode ClaudeBridgeAI::inspect(const CGObjectInstance * object, const CGHeroInstance * compareTo) const
{
	if(const auto * h = dynamic_cast<const CGHeroInstance *>(object))
	{
		InfoAboutHero info;
		if(!cb->getHeroInfo(h, info))
			throw std::runtime_error("no information about that hero");

		JsonNode ret = armyInfo(info);
		ret["type"].String() = "hero";
		ret["class"].String() = info.hclass->getJsonKey();
		addComparison(ret, armyValue(info), compareTo);
		if(info.details)
		{
			ret["mana"].Integer() = info.details->mana;
			ret["luck"].Integer() = info.details->luck;
			ret["morale"].Integer() = info.details->morale;
			ret["primarySkills"].Vector();
			for(const auto skill : info.details->primskills)
				ret["primarySkills"].Vector().emplace_back(static_cast<si64>(skill));
		}
		return ret;
	}
	if(const auto * t = dynamic_cast<const CGTownInstance *>(object))
	{
		InfoAboutTown info;
		if(!cb->getTownInfo(t, info))
			throw std::runtime_error("no information about that town");

		JsonNode ret = armyInfo(info);
		ret["type"].String() = "town";
		addComparison(ret, armyValue(info), compareTo);
		ret["fortLevel"].Integer() = info.fortLevel;
		ret["builtThisTurn"].Bool() = info.built != 0;
		if(info.details)
		{
			ret["hallLevel"].Integer() = info.details->hallLevel;
			ret["goldIncome"].Integer() = info.details->goldIncome;
			ret["garrisonedHero"].Bool() = info.details->garrisonedHero;
		}
		return ret;
	}
	if(const auto * m = dynamic_cast<const CGCreature *>(object))
		return monsterInfo(m, compareTo);
	throw std::runtime_error("only heroes, towns and monsters can be inspected");
}

JsonNode ClaudeBridgeAI::threats(const CGHeroInstance * compareTo) const
{
	JsonNode ret;
	ret["compareTo"].Integer() = compareTo ? compareTo->id.getNum() : -1;
	ret["enemies"].Vector();

	for(const auto * obj : cb->getAllVisitableObjs())
	{
		if(obj->getOwner() == playerID)
			continue;
		const auto * h = dynamic_cast<const CGHeroInstance *>(obj);
		const auto * t = dynamic_cast<const CGTownInstance *>(obj);
		if(!h && !t)
			continue;
		if(obj->getOwner() == PlayerColor::NEUTRAL && !t)
			continue;

		JsonNode entry;
		try
		{
			entry = inspect(obj, compareTo);
		}
		catch(const std::runtime_error &)
		{
			continue;
		}
		entry["id"].Integer() = obj->id.getNum();
		entry["pos"] = position(obj->visitablePos());
		entry["ownerColor"].String() = obj->getOwner().isValidPlayer() ? GameConstants::PLAYER_COLOR_NAMES[obj->getOwner().getNum()] : "neutral";

		int nearest = -1;
		for(const auto * own : cb->getTownsInfo())
			if(own->visitablePos().z == obj->visitablePos().z)
			{
				const int d = std::max(std::abs(own->visitablePos().x - obj->visitablePos().x), std::abs(own->visitablePos().y - obj->visitablePos().y));
				nearest = nearest < 0 ? d : std::min(nearest, d);
			}
		entry["tilesToNearestOwnTown"].Integer() = nearest;
		ret["enemies"].Vector().push_back(entry);
	}
	return ret;
}

JsonNode ClaudeBridgeAI::targets(const CGHeroInstance * hero, size_t limit)
{
	pathfinder->invalidatePaths();
	const auto paths = pathfinder->getPathsInfo(hero);

	struct Candidate
	{
		int turns;
		size_t steps;
		JsonNode entry;
	};
	std::vector<Candidate> found;

	for(const auto * obj : cb->getAllVisitableObjs())
	{
		if(obj->id == hero->id || obj->getOwner() == playerID)
			continue;
		CGPath path;
		if(!paths->getPath(path, obj->visitablePos()))
			continue;

		JsonNode entry;
		entry["id"].Integer() = obj->id.getNum();
		entry["type"].String() = obj->getTypeName();
		entry["subtype"].String() = obj->getSubtypeName();
		entry["name"].String() = objectName(obj);
		entry["pos"] = position(obj->visitablePos());
		entry["owner"].Integer() = obj->getOwner().getNum();
		entry["steps"].Integer() = path.nodes.size() - 1;
		entry["turns"].Integer() = path.nodes.front().turns;
		if(const auto * monster = dynamic_cast<const CGCreature *>(obj); monster && !monster->stacks.empty())
		{
			const JsonNode info = monsterInfo(monster, hero);
			entry["size"] = info["size"];
			entry["threat"] = info["threat"];
			entry["strengthRatio"] = info["strengthRatio"];
		}
		found.push_back({path.nodes.front().turns, path.nodes.size() - 1, std::move(entry)});
	}

	std::sort(found.begin(), found.end(), [](const Candidate & a, const Candidate & b)
	{
		return std::tie(a.turns, a.steps) < std::tie(b.turns, b.steps);
	});

	JsonNode ret;
	ret["targets"].Vector();
	for(size_t i = 0; i < found.size() && i < limit; i++)
		ret["targets"].Vector().push_back(std::move(found[i].entry));
	ret["reachableCount"].Integer() = found.size();
	return ret;
}

JsonNode ClaudeBridgeAI::battleState(bool includeMoves, std::optional<uint32_t> unitOverride)
{
	std::optional<PendingBattle> pending;
	{
		std::lock_guard lock(mx);
		pending = pendingBattle;
	}

	JsonNode ret;
	if(!pending)
	{
		ret["waiting"].Bool() = false;
		return ret;
	}

	const auto battle = cb->getBattle(pending->battleID);
	const battle::Unit * active = battle->battleGetUnitByID(unitOverride && pending->tactics ? *unitOverride : pending->unitId);
	if(observer)
	{
		// a spectator never gets an active stack: describe the battle from any living unit
		const auto alive = battle->battleGetUnitsIf([](const battle::Unit * u) { return u->alive(); });
		active = alive.empty() ? nullptr : alive.front();
	}
	if(!active)
		throw std::runtime_error("the active unit is gone");

	ret["waiting"].Bool() = true;
	ret["tactics"].Bool() = pending->tactics;
	ret["hexWidth"].Integer() = GameConstants::BFIELD_WIDTH;
	ret["activeUnit"].Integer() = active->unitId();

	ret["units"].Vector();
	const auto units = battle->battleGetUnitsIf([](const battle::Unit * u) { return u->alive(); });
	uint64_t ourValue = 0;
	uint64_t theirValue = 0;
	for(const auto * u : units)
	{
		(u->unitOwner() == playerID ? ourValue : theirValue) += u->estimateCombatValue();
		JsonNode entry;
		entry["id"].Integer() = u->unitId();
		entry["mine"].Bool() = u->unitOwner() == playerID;
		entry["creature"].String() = u->creatureId().toEntity(LIBRARY)->getJsonKey();
		entry["count"].Integer() = u->getCount();
		entry["firstUnitHp"].Integer() = u->getFirstHPleft();
		entry["totalHp"].Integer() = u->getAvailableHealth();
		entry["hex"].Integer() = u->getPosition().toInt();
		entry["doubleWide"].Bool() = u->doubleWide();
		entry["canShoot"].Bool() = u->canShoot();
		entry["attack"].Integer() = u->getAttack(false);
		entry["defense"].Integer() = u->getDefense(false);
		entry["damage"].String() = std::to_string(u->getMinDamage(false)) + "-" + std::to_string(u->getMaxDamage(false));
		entry["speed"].Integer() = u->getMovementRange();
		ret["units"].Vector().push_back(entry);
	}

	ret["strength"]["ours"].Integer() = ourValue;
	ret["strength"]["theirs"].Integer() = theirValue;

	auto available = battle->battleGetAvailableHexes(active, true);
	if(includeMoves)
	{
		ret["moveTo"].Vector();
		for(const auto & hex : available)
			ret["moveTo"].Vector().emplace_back(static_cast<si64>(hex.toInt()));
	}

	if(const auto * hero = battle->battleGetFightingHero(battle->battleGetMySide()))
	{
		ret["hero"]["id"].Integer() = hero->id.getNum();
		ret["hero"]["mana"].Integer() = hero->mana;
		ret["hero"]["spellPower"].Integer() = hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);
		ret["hero"]["spells"].Vector();
		for(const auto & spellId : hero->getSpellsInSpellbook())
		{
			const auto * spell = spellId.toEntity(LIBRARY);
			JsonNode entry;
			entry["spell"].String() = spell->getJsonKey();
			entry["cost"].Integer() = battle->battleGetSpellCost(spell, hero);
			ret["hero"]["spells"].Vector().push_back(entry);
		}
	}
	ret["canRetreat"].Bool() = battle->battleCanFlee();
	ret["canSurrender"].Bool() = battle->battleCanSurrender(playerID);

	const auto distances = battle->battleGetDistances(active, active->getPosition());
	ret["targets"].Vector();
	for(const auto * u : units)
	{
		if(u->unitOwner() == playerID)
			continue;

		JsonNode entry;
		entry["id"].Integer() = u->unitId();
		entry["canShoot"].Bool() = active->canShoot() && battle->battleCanShoot(active, u->getPosition());
		entry["attackFrom"].Vector();
		auto hexes = available;
		hexes.insert(active->getPosition());
		for(const auto & hex : hexes)
		{
			const auto & around = active->getSurroundingHexes(hex);
			for(const auto & targetHex : u->getHexes())
			{
				if(around.contains(targetHex))
				{
					entry["attackFrom"].Vector().emplace_back(static_cast<si64>(hex.toInt()));
					break;
				}
			}
		}
		if(!entry["attackFrom"].Vector().empty() || entry["canShoot"].Bool())
			ret["targets"].Vector().push_back(entry);
	}
	return ret;
}

JsonNode ClaudeBridgeAI::battleAct(const JsonNode & args)
{
	std::optional<PendingBattle> pending;
	{
		std::lock_guard lock(mx);
		pending = pendingBattle;
	}
	if(!pending)
		throw std::runtime_error("no unit is waiting for orders");

	const auto battle = cb->getBattle(pending->battleID);
	const auto * active = battle->battleGetUnitByID(pending->unitId);
	if(!active || !active->alive())
		throw std::runtime_error("the active unit is gone");

	const std::string action = requireString(args, "action");
	bool keepPending = false;

	if(pending->tactics)
	{
		if(action == "move")
		{
			const auto * unit = battle->battleGetUnitByID(requireInt(args, "unit"));
			if(!unit || unit->unitOwner() != playerID)
				throw std::runtime_error("tactics: unit must be one of ours");
			int rejectedBefore;
			{
				std::lock_guard lock(mx);
				rejectedBefore = rejectedRequests;
			}
			cb->battleMakeUnitAction(pending->battleID, BattleAction::makeMove(unit, BattleHex(requireInt(args, "hex"))));
			{
				// requests inside a battle are not waited for; let the server apply the move first
				auto unlock = vstd::makeUnlockSharedGuard(CGameState::mutex);
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
			}
			JsonNode state = battleState(args["moves"].isBool() && args["moves"].Bool(), unit->unitId());
			std::lock_guard lock(mx);
			state["rejected"].Bool() = rejectedRequests != rejectedBefore;
			return state;
		}
		if(action == "end_tactics" || action == "auto" || action == "autofight")
		{
			const BattleID bid = pending->battleID;
			{
				std::lock_guard lock(mx);
				pendingBattle.reset();
				autoFight = action == "autofight";
			}
			if(action == "end_tactics")
				cb->battleMakeTacticAction(bid, BattleAction::makeEndOFTacticPhase(battle->battleGetMySide()));
			else
				CAdventureAI::yourTacticPhase(bid, 0);
			waitBattleProgress(ACTION_TIMEOUT);
			bool waiting;
			{
				std::lock_guard lock(mx);
				waiting = pendingBattle.has_value();
			}
			if(waiting)
				return battleState();
			JsonNode ret;
			ret["battleOver"].Bool() = true;
			ret["status"] = statusReport();
			return ret;
		}
		throw std::runtime_error("tactics phase: use move (unit, hex), end_tactics or auto");
	}

	const bool leaving = action == "retreat" || action == "surrender";
	int rejectedBefore;
	{
		std::lock_guard lock(mx);
		rejectedBefore = rejectedRequests;
	}

	auto clearPending = [this]()
	{
		std::lock_guard lock(mx);
		pendingBattle.reset();
	};

	if(action == "auto" || action == "autofight")
	{
		const auto * stack = dynamic_cast<const CStack *>(active);
		if(!stack)
			throw std::runtime_error("the active unit cannot be handed to BattleAI");
		{
			std::lock_guard lock(mx);
			pendingBattle.reset();
			autoFight = action == "autofight";
		}
		CAdventureAI::activeStack(pending->battleID, stack);
	}
	else if(action == "wait" || action == "defend")
	{
		clearPending();
		cb->battleMakeUnitAction(pending->battleID, action == "wait" ? BattleAction::makeWait(active) : BattleAction::makeDefend(active));
	}
	else if(action == "retreat" || action == "surrender")
	{
		const bool retreat = action == "retreat";
		if(retreat ? !battle->battleCanFlee() : !battle->battleCanSurrender(playerID))
			throw std::runtime_error("the battle does not allow that");
		clearPending();
		const auto side = battle->battleGetMySide();
		cb->battleMakeUnitAction(pending->battleID, retreat ? BattleAction::makeRetreat(side) : BattleAction::makeSurrender(side));
	}
	else if(action == "move")
	{
		const BattleHex hex(requireInt(args, "hex"));
		clearPending();
		cb->battleMakeUnitAction(pending->battleID, BattleAction::makeMove(active, hex));
	}
	else if(action == "shoot")
	{
		const auto * target = battle->battleGetUnitByID(requireInt(args, "target"));
		if(!target || !battle->battleCanShoot(active, target->getPosition()))
			throw std::runtime_error("cannot shoot that target");
		clearPending();
		cb->battleMakeUnitAction(pending->battleID, BattleAction::makeShotAttack(active, target));
	}
	else if(action == "attack")
	{
		const auto * target = battle->battleGetUnitByID(requireInt(args, "target"));
		if(!target || target->unitOwner() == playerID)
			throw std::runtime_error("no such enemy unit");

		auto available = battle->battleGetAvailableHexes(active, true);
		available.insert(active->getPosition());
		const auto distances = battle->battleGetDistances(active, active->getPosition());

		BattleHex from = BattleHex::INVALID;
		if(args["from"].isNumber())
		{
			from = BattleHex(args["from"].Integer());
		}
		else
		{
			uint32_t best = std::numeric_limits<uint32_t>::max();
			for(const auto & hex : available)
			{
				const auto & around = active->getSurroundingHexes(hex);
				for(const auto & targetHex : target->getHexes())
				{
					if(around.contains(targetHex) && distances.at(hex.toInt()) < best)
					{
						best = distances.at(hex.toInt());
						from = hex;
					}
				}
			}
		}
		if(!from.isValid())
			throw std::runtime_error("that target cannot be reached this turn");

		clearPending();
		cb->battleMakeUnitAction(pending->battleID, BattleAction::makeMeleeAttack(active, target, from));
	}
	else if(action == "cast")
	{
		const auto * hero = battle->battleGetFightingHero(battle->battleGetMySide());
		if(!hero)
			throw std::runtime_error("no hero to cast");

		const SpellID * spellId = nullptr;
		for(const auto & spell : hero->getSpellsInSpellbook())
		{
			if(keyMatches(spell.toEntity(LIBRARY)->getJsonKey(), requireString(args, "spell")))
				spellId = &spell;
		}
		if(!spellId)
			throw std::runtime_error("the hero does not know that spell");

		battle::Target target;
		if(args["target"].isNumber())
		{
			const auto * unit = battle->battleGetUnitByID(args["target"].Integer());
			if(!unit)
				throw std::runtime_error("no such target unit");
			target.emplace_back(unit);
		}
		else if(args["hex"].isNumber())
		{
			target.emplace_back(BattleHex(args["hex"].Integer()));
		}

		BattleAction spellcast;
		spellcast.actionType = EActionType::HERO_SPELL;
		spellcast.spell = *spellId;
		spellcast.setTarget(target);
		spellcast.side = battle->battleGetMySide();
		spellcast.stackNumber = -1;
		cb->battleMakeSpellAction(pending->battleID, spellcast);
		keepPending = true; // casting does not end the unit's turn
	}
	else
	{
		throw std::runtime_error("unknown battle action \"" + action + "\"");
	}

	if(leaving)
	{
		auto unlock = vstd::makeUnlockSharedGuard(CGameState::mutex);
		std::unique_lock lock(mx);
		cv.wait_for(lock, ACTION_TIMEOUT, [this]() { return !battleOngoing; });
		pendingBattle.reset();
	}
	else if(keepPending)
		waitBattleProgress(std::chrono::seconds(2));
	else
		waitBattleProgress(ACTION_TIMEOUT);

	bool stillWaiting;
	bool rejected;
	{
		std::lock_guard lock(mx);
		stillWaiting = pendingBattle.has_value();
		rejected = rejectedRequests != rejectedBefore;
	}
	if(stillWaiting)
	{
		JsonNode state = battleState(args["moves"].isBool() && args["moves"].Bool());
		state["rejected"].Bool() = rejected;
		return state;
	}

	// battle over: let hero movement and visits settle
	waitTillIdle(ACTION_TIMEOUT);
	JsonNode ret;
	ret["rejected"].Bool() = rejected;
	ret["battleOver"].Bool() = true;
	ret["status"] = statusReport();
	return ret;
}

JsonNode ClaudeBridgeAI::answerQuestion(QuestionID id, int selection)
{
	{
		std::lock_guard lock(mx);
		auto question = pendingQuestions.find(id);
		if(question == pendingQuestions.end())
			throw std::runtime_error("no such pending question");

		if(question->second["kind"].String() == "level_up")
		{
			const auto count = static_cast<int>(question->second["choices"].Vector().size());
			if(selection < 0 || selection >= count)
				throw std::runtime_error("level_up choice must be 0.." + std::to_string(count - 1));
		}
		pendingQuestions.erase(question);
	}

	sendWithoutWaiting([&]() { cb->selectionMade(selection, id); });
	waitTillIdle(ACTION_TIMEOUT);
	return statusReport();
}

JsonNode ClaudeBridgeAI::endTurn()
{
	waitTillIdle(ACTION_TIMEOUT);
	{
		std::lock_guard lock(mx);
		if(!pendingQuestions.empty())
			throw std::runtime_error("answer pending questions first");
		havingTurn = false;
	}
	cb->endTurn();
	return statusReport();
}
