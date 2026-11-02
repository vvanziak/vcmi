/*
 * AIFactory.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "AIFactory.h"

#include "CGlobalAI.h"

#ifdef ENABLE_NULLKILLER2_AI
#  include "../../AI/Nullkiller2/AIGateway.h"
#endif
#ifdef ENABLE_BATTLE_AI
#  include "../../AI/BattleAI/BattleAI.h"
#endif
#ifdef ENABLE_STUPID_AI
#  include "../../AI/StupidAI/StupidAI.h"
#endif
#ifdef ENABLE_MMAI
#  include "../../AI/MMAI/MMAI.h"
#endif
#include "../../AI/EmptyAI/CEmptyAI.h"

namespace
{
std::map<std::string, std::function<std::shared_ptr<CGlobalAI>()>> & externalAIs()
{
	static std::map<std::string, std::function<std::shared_ptr<CGlobalAI>()>> registry;
	return registry;
}

/// "blue=Name,green=Name" as a map from colour name to AI name
std::map<std::string, std::string> parseAssignments(const char * variable)
{
	std::map<std::string, std::string> result;
	const char * value = std::getenv(variable);
	if(!value)
		return result;

	std::stringstream stream(value);
	std::string item;
	while(std::getline(stream, item, ','))
	{
		const auto separator = item.find('=');
		if(separator != std::string::npos && separator > 0 && separator + 1 < item.size())
			result[item.substr(0, separator)] = item.substr(separator + 1);
	}
	return result;
}
}

bool AIFactory::registerAdventureAI(const std::string & name, std::function<std::shared_ptr<CGlobalAI>()> creator)
{
	return externalAIs().emplace(name, std::move(creator)).second;
}

std::string AIFactory::slotOverride(const std::string & colorName)
{
	static const auto slots = parseAssignments("VCMI_AI_SLOTS");
	const auto found = slots.find(colorName);
	return found == slots.end() ? std::string() : found->second;
}

std::pair<std::string, std::string> AIFactory::observerOverride()
{
	static const auto observers = parseAssignments("VCMI_AI_OBSERVE");
	if(observers.empty())
		return {};
	return { observers.begin()->first, observers.begin()->second };
}

std::shared_ptr<CGlobalAI> AIFactory::createAdventureAI(const std::string & name)
{
	logGlobal->info("Creating adventure AI %s", name);

	if(const auto external = externalAIs().find(name); external != externalAIs().end())
		return external->second();

	if(name == "Nullkiller2")
	{
#ifdef ENABLE_NULLKILLER2_AI
		auto ret = std::make_shared<NK2AI::AIGateway>();
		ret->dllName = name;
		return ret;
#else
		throw std::runtime_error("Nullkiller2 is not available in this build!");
#endif
	}

	auto ret = std::make_shared<CEmptyAI>();
	ret->dllName = name;
	return ret;
}

std::shared_ptr<CBattleGameInterface> AIFactory::createBattleAI(const std::string & name)
{
	logGlobal->trace("Creating battle AI %s", name);

	if(name == "BattleAI")
#ifdef ENABLE_BATTLE_AI
		return std::make_shared<CBattleAI>();
#else
		throw std::runtime_error("BattleAI is not available in this build!");
#endif

	if(name == "StupidAI")
#ifdef ENABLE_STUPID_AI
		return std::make_shared<CStupidAI>();
#else
		throw std::runtime_error("StupidAI is not available in this build!");
#endif

	if(name == "MMAI")
#ifdef ENABLE_MMAI
		return std::make_shared<MMAI::BAI::Router>();
#else
		throw std::runtime_error("MMAI is not available in this build!");
#endif

	return std::make_shared<CEmptyAI>();
}

bool AIFactory::isAvailableAdventureAI(const std::string & name)
{
	if(name == "EmptyAI" || externalAIs().count(name))
		return true;
#ifdef ENABLE_NULLKILLER2_AI
	if(name == "Nullkiller2")
		return true;
#endif
	return false;
}
