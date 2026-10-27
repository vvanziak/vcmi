# VCMI bugs seen while playing

Stock VCMI behaviour found during games, not caused by ClaudeBridge. Build: 1.8.0 develop at c81f8c73b. Logs: `vcmi-build` client log under the scratchpad `logs` directory; crash reports in `~/Library/Logs/DiagnosticReports/vcmiclient-*.ips`.

## 1. Client crash: BattleAI in a battle without a fighting hero on one side

- Seen: 2026-10-23, day 54, blue (Nullkiller2 + BattleAI) fighting 44 Harpy Hags; crash right after a `BattleStackMoved`/`EndAction`/`BattleSetActiveStack` sequence.
- Error: `CBattleInfoEssentials::battleGetFightingHero(BattleSide) const called with invalid parameters!`, then `Disaster happened` and terminate.
- Stack: `CBattleInfoEssentials::battleGetFightingHero` <- `DamageCache::buildObstacleDamageCache(HypotheticBattle, BattleSide)` <- `DamageCache::buildDamageCache` <- `BattleEvaluator::BattleEvaluator` <- `CBattleAI::activeStack`.
- Likely cause: `buildObstacleDamageCache` asks for the fighting hero of a side that has none (neutral monsters, or a side whose hero is absent) and the getter throws instead of returning null.
- Related log noise earlier in the same battle: `Script 'coreSCRIPTS/SPELLS/REMOVEOBSTACLE', function 'applicableGeneral': battleGetAllObstacles called with invalid parameters!` (from both `runNetwork` and `MainGUI`).

## 2. GUI error: clicking the hero portrait during a battle

- Seen: day ~40, human player clicked the hero portrait in battle.
- Error: `battleCanCastSpell called with invalid parameters!`; client terminated.
- Likely cause: the spell window opens for a battle state where the casting side or hero is not valid (the AI-controlled side was the active one).

## 3. Server "fishy" messages after over-ordering recruits (client request, not an engine bug)

- `Cannot recruit: cram <= 0!` and `Got false in applying RecruitCreatures... fishy`.
- Cause: a recruit request larger than the available stock (wight and wraith share one dwelling pool). The server rejects the request correctly; the log wording is alarming. A client check before sending would avoid it; ClaudeBridge now reads stock first.

## 4. GUI freeze on the commander level-up modal after a battle

- Seen: 2026-10-23, day 53 of game 2, human player (Inferno) finishing a battle with a commander.
- Log: `Cannot wait for dialogs in gui thread (deadlock risk)!` from `MainGUI`, then no further log output; the client process idles at 0% CPU. A run of `Error: cannot find a hex to attack hex 98 from!` (also `MainGUI`) precedes it.
- Visible state: the commander skill modal stays open and the game does not continue.
- Likely cause: the modal is opened from the GUI thread while the network thread waits for the same dialog.

## 5. Client crash: spectator opens a battle window (--onlyAI --spectate)

- Seen: game 3 (all-AI start), day 1, first battle (Claude slot vs neutral monsters).
- Error: uncaught exception from `CBattleInfoEssentials::battleGetMySide()` <- `battleGetAllObstacles` <- `CBattleInfoCallback::getAccessibility` <- `BattleFieldController::updateAccessibleHexes` <- `BattleWindow::BattleWindow` <- `BattleInterface::BattleInterface` <- `CClient::battleStarted`; `onTerminate`.
- Likely cause: the spectator `CPlayerInterface` has no battle side, so building the battle window throws.
- Workaround: start with `--spectate-skip-battle` (or no `--spectate`).

## 6. Crash: Nullkiller2 `showBlockingDialog` with an invalid tile (all-AI game)

- Seen: game 4, day 22, during the opponent's turn; the whole client terminated (`onTerminate`).
- Error: `CGameInfoCallback::getTile(int3): (-1 -1 -1) is not visible!` thrown inside `NK2AI::AIGateway::showBlockingDialog` on a TBB worker (`AsyncRunner::run` -> `executeActionAsync`), uncaught in the task.
- Likely cause: the dialog's target position is not set (-1,-1,-1) and the AI queries its tile; the exception escapes the async task.

## 7. Crash: server `std::bad_alloc` in `CGameHandler::upgradeCreature` (game 18)

- Seen: bridge `upgrade` tool, hero standing in a town with the upgraded dwelling.
- Error: `std::bad_alloc` from `ResourceSet::ResourceSet(const&)` <- `CGameHandler::upgradeCreature(ObjectInstanceID, SlotID, CreatureID)`; `onTerminate`.
- Cause: our bridge called `cb->upgradeCreature(obj, slot)` with the default `CreatureID::NONE` as the target; `UpgradeInfo::getUpgradeCostsFor` then indexes `upgradesCosts` out of range.
- Fix (bridge side): `UpgradeInfo info(...)`, `cb->fillUpgradeInfo(...)`, pass `info.getUpgrade()` explicitly. The server should reject an upgrade id that is not in `upgradesIDs` instead of reading past the end.

## 8. Crash: BattleAI `battleGetFightingHero` with an invalid side (game 18)

- Seen: blue AI battle at a hero-less town; client terminated.
- Error: `CBattleInfoEssentials::battleGetFightingHero(BattleSide) called with invalid parameters!` in `CBattleAI::activeStack`.
- Status: fixed upstream by 5b4795b24 (PR #8073, "BattleAI: estimate neutral spell obstacles without an invalid side lookup"), pulled on 2026-10-26.

## 9. Crash: client map hover during AI turns (game 21, "Knee Deep in the Dead")

- Seen: day ~52, during blue's turn while the AI moved a hero; `SIGSEGV`, KERN_INVALID_ADDRESS at 0x6c8; no log error, no autosave existed.
- Stack: `NodeStorage::getNode(int3 const&, EPathfindingLayer const&)` <- `NodeStorage::getInitialNodes` <- `CPathfinder::calculatePaths` <- `CGameState::calculatePaths` <- `PathfinderCache::buildPaths(CGHeroInstance const*)` <- `PathfinderCache::getPathsInfo` <- `AdventureMapInterface::onTileHovered` <- `MapViewActions::handleHover` <- `MapViewActions::mouseMoved`.
- Likely cause: mouse hover asks for paths of the selected hero while the game state is being changed by the AI turn (hero gone or position invalid), a race on the GUI thread. Crash report: ~/Library/Logs/DiagnosticReports/vcmiclient-2026-10-26-193812.ips.
- Workaround: keep the mouse off the map while all-AI games run; ask for manual saves often.

## 10. Observations (not bugs)

- `UpgradeInfo.cpp` changed upstream (`positive()` -> `removeNegative()`), no behaviour issue found.
- Bridge `recruit` was blocked by the bridge's own pending-dialog guard for the recruitment dialog (fixed in the bridge: allow kind=recruitment).

## 11. Observer (spectator) mode: stale battle state
- The SPECTATOR interface is never told when a battle ends, so `pendingBattle` stays set and `battle_state` threw "Failed to find battle N of player red". Fixed in the bridge (drop the entry when `cb->getActiveBattles()` no longer lists it); needs a client restart to take effect.
