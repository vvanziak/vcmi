/*
 * AIFactory.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

class CGlobalAI;
class CBattleGameInterface;

namespace AIFactory
{
	DLL_LINKAGE std::shared_ptr<CGlobalAI> createAdventureAI(const std::string & name);
	DLL_LINKAGE std::shared_ptr<CBattleGameInterface> createBattleAI(const std::string & name);

	/// Returns true if the given name maps to a known statically-linked adventure AI.
	DLL_LINKAGE bool isAvailableAdventureAI(const std::string & name);

	/// Registers an adventure AI built outside of this tree; called from a static initializer of the AI's library.
	DLL_LINKAGE bool registerAdventureAI(const std::string & name, std::function<std::shared_ptr<CGlobalAI>()> creator);

	/// AI the environment hands a player slot (VCMI_AI_SLOTS="blue=Name,green=Name", colour names in lower case); empty when the slot is not overridden.
	DLL_LINKAGE std::string slotOverride(const std::string & colorName);

	/// Read-only observer AI (VCMI_AI_OBSERVE="blue=Name"): the colour whose view it gets and the AI name; empty when not set.
	DLL_LINKAGE std::pair<std::string, std::string> observerOverride();
}
