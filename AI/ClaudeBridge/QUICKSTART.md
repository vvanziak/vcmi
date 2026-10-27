# ClaudeBridge quickstart (read this first; notes/ is the archive)

Record: 4 wins (game 18 on Crimson and Clover, day ~103) (easy d44, medium d78) and many losses with Dungeon/Jeddite on "Dungeon Keeper" (36x36, z1 home at (15,15), z0 blue Rampart town (17,18), gate (25,26,z1) <-> (14,4,z0)). Layout is fixed, resource piles are randomized per game.

## Strategy state
Keep `notes/current-game.md` (goals, needs vs sources, explored/unexplored). Read it at the start of every session and every game week; update it when a mine is found or a goal changes (human feedback, game 17).

## Setup
- All-AI game: `cd /Users/valerii/Projects/vcmi-build/bin && VCMI_CLAUDE_PLAYER=red nohup ./vcmiclient --nointro --onlyAI --spectate --spectate-skip-battle --logLocation <scratchpad>/logs &` (pkill -9 vcmiclient first). User picks difficulty and starts the map, runs `/mcp`, says "play".
- Save with `save_game` every ~5 days; no autosaves in all-AI games.
- Human-game journal: `tools/journal_digest.py <log> red`.

## How the human decides (not a fixed script)
Build order depends on difficulty, faction and what the map offers. Principles:
- Gold-rich start (easy): buy dwellings at one building per day, gold is not the limit. Gold-poor start: economy first (Town Hall -> Marketplace + Blacksmith + Mage Guild 1 -> City Hall -> Citadel -> Castle -> Capitol), interleaved with the cheapest dwellings.
- The weekly unit stock is the real cap. Buy the whole stock every week, upgraded dwellings first. Unspent gold at week start is waste.
- Primary resource (Dungeon: sulfur) is the gate for the top dwelling. Take every free pile and mine early; never sell sulfur below ~25. Keep ~10 gems, 6 crystal, 15 ore, 15 wood for Mage Guild/Labyrinth/Dragon Cave. Buying resources is expensive (sulfur 5000 gold, ore->sulfur 20:1, gems->crystal 10:1).
- One main hero holds the whole army, takes chests as XP (option 2) and every Diplomacy join (free troops). A second hero ferries recruits/holds home; never leave it outside walls near an enemy hero.
- Early on take chests as gold only while a key building waits for money.
- Spellcaster main hero: Jeddite (Warlock) starts with Resurrection + Advanced Wisdom. Resurrection is only permanent at Advanced Earth Magic. Take Earth Magic, Diplomacy, Intelligence when offered.
- Avoid shooting stacks unless they guard sulfur/a key pickup or you are far stronger. Gold Dragons ignore spells up to level 3; Wraiths drain mana.

## Fight rules
- `threats` every turn. Compare `ourValue` with the enemy `estimatedValue`; engine strength is ~1.17x AI value. Fight roaming heroes only at >= 1.3x (equal = coin flip, lost half the army), or at your own castle. A hero with 100+ centaurs plus Gold Dragons grows to 150-170k by day 50.
- `inspect id hero` threat bands: only attack neutrals at ratio <= ~0.5 early; later 'very weak' stacks cost 0-2 units.
- Autofight is fine for easy fights. For key fights cast first (`battle_act cast spell target`; status lags one call), then autofight. Retreat keeps the hero but loses the army.
- Blue's home garrison drops to ~5k when its heroes are out; take the town (z0 route: gate -> (11,5) arena -> (13,13) lab -> (11,17) stone -> (7,18..21) -> y=22 east -> (16,21) -> town). A player with no town is removed ~7 days later. Defeating every hero is not needed.

## Tool gotchas
- Level-up answers are 0-based; chest/yes-no answers are 1-based (1 = gold, 2 = XP). A wrong level-up number is rejected now.
- Recruit goes to the visiting hero only; with 7 full stacks it silently buys nothing (the reply shows the unchanged army). Free a slot or `swap_stacks hero<->town` first. Garrison stock can be pulled with `move_army town hero`.
- Heroes without a spellbook (Overlord, Death Knight start) cast nothing: stand in a town with a Mage Guild and call `buy_spellbook` (added game 18, active after client restart). A hero cannot give away his last stack. Split first: `swap_stacks` with `count` (human tip) moves that many creatures and leaves the rest; to leave 1 behind use `count = total-1` into an empty slot of the other army (active after the client restart following game 13). Hero-to-hero transfers need one hero to walk onto the other (exchange question, answer 0 to close).
- Heroes in 1-tile corridors block each other's paths ("no path"); the pathfinder ignores fog, so step toward known tiles. Check `find_path` steps/turns.
- Gate: stand on the tile, then move to the same tile again. A hero standing on the other end blocks arrival.
- Pending dialogs (commander resurrection 1500 gold, level-ups) block everything; answer them. Commander is worth the gold.
- Marketplace `trade amount` = units given. Shops: School of Magic/War cost 1000 gold per +1.

## Per-turn loop
`threats` -> build (one per town per day) -> recruit/ferry -> hero `targets` (pickups, then monsters by ratio) -> answer dialogs -> `next_turn`. Save every ~5 days.

## Server "has to answer activities" errors
Seen as `Player RED has to answer activities ... BlockingDialog [question N]` in the client log. Cause: an action was sent while a chest/level-up/commander dialog was still open (the server refuses all red actions until it is answered). Harmless but wastes the call. Answer each dialog before the next action. The bridge now rejects move_hero/build/recruit/trade/buy_hero/build_boat while a non-exchange question is pending (takes effect after the next client restart).

## Second hero job (human advice, game 12)
Do not leave him idle in town: he follows the main hero picking up resources/mines/chests the main hero skipped, visits windmills/water wheels every week, and ferries recruits. Give him a route each turn (targets list) instead of parking him.

## Human duel map (games 13-16, no building)
- Town is pre-built; the weekly stock is the only growth. Day 1: put old stacks in the garrison with `swap_stacks`, recruit the top 6-7 upgraded creatures into the hero, take the start chest as XP, return to the castle before ending the turn.
- Re-check hero strength in `battle_state` before the fight; `threats` is up to 2 days stale. Below 1.3x: retreat if possible instead of autofight (retreat keeps the hero).
- A second hero in the visiting slot gets the recruits; unbought stock accumulates, so a skipped week is recoverable.
- Against the human: do not wait inside the castle with an army half his size; he merges all heroes. Castle defence did not close a 2.2x gap (game 16).

## Lessons from game 17 (large map, lost day 99)
- At game start write `notes/current-game.md`: needs (gold, ore, wood, crystal, gems, mercury, sulfur) vs sources; re-read weekly. Every need without a source is the next scouting target.
- Economy chain first on a normal start: Town Hall -> Marketplace -> Blacksmith -> Mage Guild 1 -> City Hall (+2000) -> Citadel -> Castle -> Capitol; dwellings after.
- Scout by walking toward unexplored tiles; a hero far from the castle can be caught by a hero with 3x his army in one turn. Keep the main stack at home until it beats the strongest roaming AI hero, or keep a cheap scout hero alone and let it flag mines.
- Spend gold every turn; the stock accumulates if skipped but the army is needed before the AI merges its heroes.

## Bridge/engine crashes seen in game 17-18 (do not repeat)
- `cb->upgradeCreature(obj, slot)` with the default CreatureID::NONE crashes the server (std::bad_alloc in CGameHandler::upgradeCreature). The `upgrade` tool now passes `UpgradeInfo::getUpgrade()` explicitly. Test new commands right after a manual save by the human.
- AI-vs-neutral battle can crash the client when the obstacle generator fails (`battleGetAllObstacles` throws in CBattleAI::activeStack); nothing to do but reload.
- `save_game` never wrote a loadable file in all-AI mode; ask the human for a manual save before risky actions (new commands, big assaults).
- Recruit goes to the visiting hero; with 7 full stacks nothing is bought and no error is returned. Free a slot (swap a stack into the town's own 7 garrison slots), recruit, then swap back. `recruit garrison=true` (new) targets the town garrison directly.

## Winning pattern (game 18)
One main hero with the whole army, only fights at >=2x, buy every town's weekly stock (garrison=true), eliminate AI players one at a time by taking their last town (they vanish ~7 days later), use a cheap second hero for mines. Scout toward the opposite map corner for the last town; AI garrisons are tiny.

## Start without a town (game 19, lost day 8)
No town after 7 days = defeat. Goal 1 is to find the town: read signs, look for keymaster tents and matching border guards/gates, fight only monsters that block the route. Chests, mines and XP wait until the town is owned.

## Never leave the town empty (game 20)
An AI hero walks into an empty town (even from far away) and takes it, buys your stock, and starts the 7-day loss countdown. Before the main hero leaves for a far attack: keep a hero standing in the town and >=15-20k of troops in the garrison (swap_stacks hero->town), or only fight within 1 turn of home.

## Game 20 summary (single-town Castle map, lost d49)
Income decides the AI race: flag all mines with a cheap hero in week 1-2 and take the nearest enemy town while its garrison is 4-9k. Waiting at home lets an AI Overlord grow to 100k. Never fight at <1x (Abandoned Mine 149 Troglodytes cost the first army).

## Spectator mode (human plays, Claude only watches)
Launch without `--onlyAI`: `cd /Users/valerii/Projects/vcmi-build/bin && VCMI_CLAUDE_SPECTATE=red nohup ./vcmiclient --nointro --logLocation <scratchpad>/logs &`. The human picks the map and plays red normally. The bridge is installed as the SPECTATOR interface with red's view (port 37100) and accepts only read-only commands: game_state, game_status, battle_state, inspect, threats, targets, find_path, tile_info, get_tavern, get_buildable, lookup, artifacts, thieves_guild. Every other command is rejected ("spectator mode"). Do not combine with VCMI_CLAUDE_PLAYER. Client edits: `CClient::claudeObservedColor`, branch in `initPlayerInterfaces`.

## Knee Deep in the Dead plan (games 22 win d44, 23 loss d82)
- Timeline that wins: town d4 (route in notes/games.md), economy chain + garrison recruits, mines by d25, Keymaster tent (14,24) -> Border Guard (33,11) -> west along y=4-9 -> blue Necropolis (2,5) with ~100k by d40. Blue's capital is nearly empty early; heroes sit in the NE. After ~d55 blue merges heroes into one 230k -> 370k -> 480k stack and out-earns me with my mines.
- Never end a hop next to unseen tiles; `threats` lists only heroes already seen. Scout with a cheap hero first.
- Do not leave cheap heroes outside the walls near a roaming hero (lost Orrin, Cuthbert). Corridor (3,34)-(8,34) is one tile wide: two heroes cannot pass, plan who is inside before leaving.
- `buy_spellbook` for the main hero and pick Knowledge/Wisdom: siege fights went as pure autofight with 10 mana.
- Gems/crystal come from trade at 5000 gold each, wood/ore at 20:1: keep wood and ore for Angels (1 gem) and Archangels (3 gems). Mines in the east (Gem (31,26), Crystal (24,15)) must be kept flagged.
- Unflagged/lost mines: blue re-flags them; recapture every few days with a cheap fast hero or income stagnates.
- Parity is not enough: at 219k vs 228k I waited and lost the window. Strike lone weak enemy heroes (<0.7x) immediately.
