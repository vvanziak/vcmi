> ARCHIVE: chronological notes from games 1-11. Read ../QUICKSTART.md first; some advice below is superseded.

# Playbook

Read at the start of a game, update at the end. Keep entries short and specific. Log of individual games: `games.md`.

## Tool facts

- `move_hero` result `visited: true` means the hero spent movement on an object without changing tile (pickups, eyes); not a failure.
- The hero moves as far as movement allows and stops with `not enough movement points this turn`; call it again next turn with the same target.
- A chest asks gold vs experience. Choice 1 = gold, 2 = experience. 500/1000/1500 XP offers scale with the day.
- Level-up `choices` is a 0-based index.
- `get_buildable` lists only buildable things by default (`all: true` for everything). The Grail building shows as allowed; do not build it.
- `get_state` with `hero` and `radius` keeps the object list short; without them it lists every visible object.
- One build per town per day.
- Recruiting while the hero is not in the town puts units in the garrison. Units in a garrison need `move_army` to join the hero, so keep the hero in town on recruiting days.
- `build_boat` works from anywhere (no hero visit needed); costs 1000 gold and 10 wood.
- New tools need a rebuild and a client restart. Save before quitting; autosave on day start loads fine because the AI keeps no state.
- Restarting a game with a human: launch `VCMI_CLAUDE_PLAYER=green vcmiclient --nointro` and load the save from the menu. `--testsave` makes every other slot an AI. Kill any old `vcmiclient` first (`pkill -9`); a leftover process holds port 37103 and the new client crashes in `BridgeServer`.
- Do not visit dwellings, markets, taverns or other heroes with a build older than the dialog handlers; the server waits for an answer.

## Map knowledge

- Pathfinding does not cross water. Anything `reachable: false` over a gap needs a boat.
- Guards (sprites, centaurs, skeleton warriors) in front of mines are easy for 40+ skeletons; Necromancy raises a few units after each fight.

## Lessons from losses

- Game 1 (vs human on Conflux): Sandro (level 4, ~100 units, mostly Skeletons) parked 5 tiles from the enemy town and was killed within a turn or two. Weak stacks (Skeletons) die fast to flyers (Firebirds); the high-tier units I recruited were few. Keep the main hero out of reach of enemy hero + town garrison unless the army is clearly stronger, and never leave it adjacent to an enemy town overnight.
- Before `move_hero` over a long distance, run `find_path` and read `steps`: a target that looks near can route around the whole island (the Windmill detour wasted a turn and moved Sandro next to the enemy town).
- Recruit into the hero, not the garrison, and do it every week: the first week of unspent stock is lost growth.
- BattleAI fights all battles and wasted high-tier units (Wights 9→1, Wraiths 7→2 after small fights). Manual combat control is a priority tool.
- An enemy hero appears in `visibleObjects` only when its tile is visible; check `game_state` with `radius` at the start of each turn to see what is near.
- Marketplace: with one Marketplace the rate is ~20 wood per sulfur. Not worth building early.

- Game 1, day 32: attacked the human's Conflux town with Clavius (~150 units, mostly Skeletons) after seeing only that a hero was inside. The garrison was 8 Firebirds, 8 Psychic Elementals, 29 Water Elementals, 37 Fire Elementals, 34 Pixies, 7 Griffins, an Astral Spirit commander and 3 Arrow Towers (about 4,300 HP vs my 1,500). Lost the whole army. NEVER attack a town or hero without knowing its army. Weak 1-HP-class stacks (Skeletons, Walking Dead) are fodder, not an army.
- In battle: Pixies (3 HP) die to any real stack; Firebirds (150 HP, flying) wipe Wraiths and Vampires in one round; spells with a weak hero (2 spell power, 10 mana) do nothing meaningful.
- Tactics that worked: Vampires one-shot a 34-Pixie stack with no losses; Haste on the best unit. Everything else was too weak.

- Game 1, day 41: successful defense of the home town against a weakened attacking army (2 Firebirds left, Elementals, Griffins, Peasants). Kill priority was the Firebirds (high damage, low remaining HP): Haste on the Vampires, Vampires then Wights/Skeleton Warriors on the Firebird. Check `battle_state` stats (attack/defense/damage/speed) and remaining HP of the dangerous stack before committing. Defending in a town: retreat and surrender are not allowed.
- An attack on my town shows up only as `wait_for_turn` returning `battle: true`. Check `game_state` with a radius at the start of every turn for enemy heroes near my towns.

## Exploration (game 1, day 50+)

- Sitting in the castle is almost never right (advice from the human). Take mines, chests and artifacts with a hero while the town garrison holds; leave the weak stacks (Skeleton Warriors, Zombies) in the town with `swap_stacks`.
- `find_path` says `reachable: false` for fogged tiles; it is not proof that a boat is needed. Walk to the edge of the explored area and test again. The Bow of Elven Cherrywood looked boat-only and was a 2-turn walk.
- A monster stack that is much weaker than the army "scatters" and asks whether to pursue; answering 1 fights it and Necromancy still raises skeletons.
- Gold is the surplus resource. Take experience from chests; gold-to-resource trades are 5000:1 (crystal, gems, mercury, sulfur) and 2500:1 (wood).
- `inspect` only works on heroes and towns; a monster stack's size shows only in the attack prompt.
- The turn watcher: after `end_turn`, start a background `tail -F` of the client log with `grep -m1 "Server ends turn of blue"` so a finished command wakes the session when green's turn begins.

## Missing tools (found in battle)

- retreat / surrender (no way to leave a lost fight)
- enemy intel before attacking: army of a visible town/hero (the engine shows it in the UI hover)
- hero mana, known spells and costs; feedback when a cast or action is rejected (a rejected cast was silent)
- unit stats in battle: attack, defense, damage, speed, buffs; turn order; wall and tower state in sieges
- a compact battle_state (the full unit list is repeated on every call)

## Economy (advice from the human opponent, day 32)

- Town Hall -> City Hall -> Capitol is the priority. The Capitol costs 10,000 gold and returns about 4,000 gold/day, so it pays back in about 2.5 days. Gold buys the army; I ran a Town Hall economy for 20 days and lost the arms race.
- Build order from day 1: Town Hall as soon as possible, City Hall and Capitol before the Castle and before secondary buildings. Spend extra gold on creatures only after the next hall is paid for.

## Opening (Necropolis)

- Day 1: recruit all available creatures, build Town Hall (+500 gold/day), pick up nearby chests, return to town.
- Gold is plentiful early (10k); build slots and hero level are the bottleneck, so take XP from chests when it reaches a level.
- Capture the ore and wood mines early; Mausoleum needs 10 ore and 10 sulfur, Mage Guild 2 needs 4 of each gem resource plus 5 ore.
- Mercury is scarce: the wraith upgrade (5 mercury) blocks Mage Guild 2 later. Trade at the marketplace or find a mercury source.
- Hire a second hero early to scout; one hero on a coastal strip wastes days.

## Lessons from game 1 ending (day 70)

- Luna (Elementalist, ~115 Ice, 79 Storm, 59 Energy, 24 Magic Elementals, 10 Phoenixes) sailed down the lake and beat Nagash's doomstack (62 Wraiths, 40 Vampires, 10 Black Knights, 12 Power Liches plus the garrison) at my own town with walls and three Arrow Towers. Total enemy HP was about 12,800 against my 5,000; ranged stacks (Ice, Storm) outdamage melee defenders.
- Count HP, not units, when comparing armies: `inspect` ranges (100-249) are enough to see a 2-3x deficit early. I saw Luna three turns before the fight and could not close the gap, because the gold surplus had no outlet.
- A gold surplus from the Capitol (30k+ banked) is wasted if the weekly stock is the only sink. Hire more heroes early and recruit every week; build Mage Guild levels and ranged units. I held 20-40k gold unspent for 20 days.
- Fighting inside the town merged the garrison into the hero's army (the battle showed it as one army). Pooling stacks into one hero first was right, but it also meant one defeat removed everything.
- The human opponent scouted and massed units while I stayed home for 20 days. Explore and take mines from day 1.
- `swap_stacks` and `move_army` between two heroes need the exchange window, which opens when one hero walks onto the other; without it they return unchanged with no error.
- Retreat and surrender are not allowed when defending a town, so a lost defense loses the army.

## Opening advice from the human (game 2, Castle, day 1)

- Get ore and wood mines first; most core buildings need them.
- Put the army under one hero so that hero gains experience and primary skills. Use the second hero for scouting and carrying recruits.

# Game 2 (Castle, vs human Inferno, Nullkiller2 Fortress/Castle) - consolidated lessons

## How a session runs (workflow that works)

- Launch: `VCMI_CLAUDE_PLAYER=green vcmiclient --nointro --logLocation <dir>` and load the save from the menu (never `--testsave`). Kill stale `vcmiclient`/`vcmiserver` first with `pkill -9`.
- After `end_turn`, start a background `tail -n0 -F <log> | grep -m1 "(blue) ended turn"`. Blue is the last AI before green. If blue is gone, use "Server ends turn of red", then wait a few seconds before checking `game_status`; checking at once finds `yourTurn: false`. Old watchers fire later and are harmless; the newest one is the signal.
- At the start of each of my turns: `game_state` with `hero` and `radius` ~14, `inspect` every enemy hero visible, then act. An enemy army can appear from fog and attack on its own turn (it killed Rion and later took my home castle).
- Always `find_path` before `move_hero`. A far target can route around the whole map, and fogged tiles read as unreachable (walk to the frontier and test again).
- `battle_state` shows `strength.ours` and `strength.theirs` (engine combat value). Fight at 1.5:1 or better. Below that, avoid the fight, or defend inside a town only if the ratio is near 1.

## Tools added during game 2 (all in the MCP server)

- `inspect` works on monster stacks too (pass `hero`): size range plus threat rating (effortless ... impossible).
- `lookup` (creature, building, spell; optional `faction`), `thieves_guild` (needs `town`; more taverns reveal more categories), `artifacts` and `equip`.
- Tactics phase: `battle_state` returns `tactics: true`; `battle_act` `move` (unit, hex), `end_tactics`, `auto`. Needs the Tactics skill. Corner trick: put shooters in the corner hex with a blocker on each open neighbour.
- `move_hero` onto the hero's own tile visits the object there (Space key); this is how a Subterranean Gate is re-used.
- `swap_stacks` between two heroes needs the exchange window (walk one hero onto the other), or both must be in the same town.

## Economy and build order (Castle on a harder difficulty)

- Town Hall day 1, Marketplace/Blacksmith/Mage Guild 1 then City Hall, Citadel then Castle then Capitol (10,000 gold, about +2,000/day). Capitol was built by day 13.
- Dwellings: Archers' Tower, Barracks (and upgrade), Griffin Tower (and upgrade), Monastery, Stables then Training Grounds (Cavalier/Champion), Portal of Glory (Angels: 3,000 gold plus 1 gem each, so buy gems: 5,000 gold each, 10 mercury each, 20 wood/ore each).
- Marketplace rates with one marketplace: rare resources 10 mercury or 20 wood/ore per unit, gold 5,000 per rare unit.
- Chest choice: gold early, experience when the hero is about to level.
- Upgraded and base units share one weekly pool (Archer/Marksman, Swordsman/Crusader, Cavalier/Champion). Recruiting both fails silently.
- A hero holds 7 stacks. Put weak stacks in the town garrison with `swap_stacks`; recruiting while the hero is outside puts units in the garrison.
- Weekly stock arrives on day 1 of each week, doubled by the Castle building. Skipping a week wastes stock.

## Heroes and experience

- One main hero should carry the army and gain levels; Clerics level into magic (Rion had attack 1, defense 0 at level 7). Knights gain attack and defense (Haart reached defense 6 by level 6). Hire a Knight as the main hero early.
- Free levels: Learning Stone, Tree of Knowledge (each hero once), Witch Hut, Marletto Tower, Fountains. Sites like these give more than clearing guards.
- Artifacts equip automatically on pickup. No attack/defense artifacts came up in game 2.

## Combat lessons

- Enemy shooters kill my shooters first (Sylvan Centaurs wiped 13 Marksmen). Shooters with an adjacent enemy cannot shoot, so send fast units at enemy shooters.
- A strong enemy hero's stats multiply their whole army: Edric's stacks had attack/defense ~25-45 against my 10-20, which the unit counts hid.
- Retreat from the field keeps the hero but loses the army; in a defended town neither retreat nor surrender is allowed.
- Armies lost: Rion (day 31, 67k vs 104k strength on the surface), Kanan (day 51, 49k vs 225k). Both were avoidable by keeping the army near a town and counting enemy heroes first.

## Strategic lessons

- Sitting in the castle early (game 1) and wandering far from towns late (game 2) both lost. Explore for the first two weeks, then keep the main army within a turn of a town.
- Gate pockets: a Subterranean Gate can drop a hero into a cell sealed by guards. Use the self-tile move to leave.
- Taking blue's town (Drakenmoor) with an Angel-led army worked because its garrison was weak. Check the garrison with `inspect` and take it before the AI hero returns.
- Towns win games: the Thieves' Guild ranking showed the human with the most towns. Capture towns early with a strong stack.

## Knowledge gaps to close next time

- No tool for hero spell casting on the adventure map, no spell scroll use, no trading between players, no way to see whether an enemy hero is moving toward a town.
- Watcher is a log hack; a plugin-written "my turn" marker file would be sturdier.

## Hero stats come from exploring (human feedback after game 2)
- Biggest mistake of game 2: too little map exploration. Visiting map objects (Learning Stone, Tree of Knowledge, Arena, stat shrines/gardens, pickups, guarded creatures that are safe to kill) is the main source of experience and primary stats.
- Red's hero at day ~55 had attack/defense/spell power/knowledge 22/17/15/8; Haart had 4/6/1/1 with the same army size. Primary stats multiply the whole army's strength, so they outweigh a few extra units.
- Plan: from day 1 send the main hero (plus a scout) on a loop of safe objects every turn; check `get_state` visibleObjects for experience/stat objects first; inspect guards and fight only when the threat is "weaker" or better.

## Game 3 lessons (Rampart, 36x36 maze map, all-AI setup)
- Surplus gold cannot be turned into units beyond weekly growth. Spend it on: extra heroes (each carries a stack), higher-tier dwellings, Mage Guild spells/artifacts, a second town (capture or build), war machines. Do not let 30k+ gold sit.
- Scout the opponent early (boat along the coast, Observatory, Thieves' Guild). The enemy reached me with three heroes by day 30; 1-4 harpy hero (Sephinroth) was a free kill I could not reach.
- In a defended town retreat is impossible; fight only with a clear strength margin (check `strength` from `inspect` stats before) or leave the town and keep the army alive.
- Maze maps: a hero in a one-tile corridor blocks the other hero's `find_path`; move the blocker first.
- Chest dialogs: option 1 = gold, 2 = experience.

## What a human win looked like (game 5, human red/Dungeon vs Nullkiller blue, won around day 41)
- The AI leaves its home town weakly held while its heroes roam. The human took the enemy's home town (Rampart, (17,18,z0)) about day 33 with one hero and a mid-tier army (Evil Eyes, Medusas, Scorpicores, a few Red Dragons), then recruited from the captured town and finished the remaining heroes.
- Build order of the human: Town Hall d1, dwellings 3,2,4,6 on days 2-5, Mage Guild/Marketplace/Blacksmith d6-8, City Hall d10, Castle d15, Capitol d17, dragon dwelling d22. Recruits are bought in weekly batches. Only two heroes for the first 35 days, both exploring.
- Lesson for me: stop defending and counter-attack. The enemy home on this map is at town 478 around (17,16,z1), reachable through the gate (14,4,z0) -> (25,26,z1). While the AI main army (Synca) roams the surface, a strong army sent to the AI's town is the way to win; scout it with a cheap hero first, check `threats` for the main army's position.
- Check `inspect` on the enemy town for its garrison before the assault (`inspect` accepts town ids); use `save_game` before attacking.

## Strategy hints from the human after game 6
- Each town has a primary resource (Dungeon: sulfur). Secure it early (the mine plus every free-standing pile) so the top-tier dwelling is affordable when you want it. Trading at the marketplace is 10:1, too poor.
- The opponent had its strongest unit (dragons) before I had mine. Build the top dwelling early and assess chances honestly.
- When weaker: avoid confrontation, weaken the opponent (take its mines, kill easy heroes), visit every stat-giving place (attack/defense/knowledge), and pick up artifacts.
- Run unit deliveries: new heroes carry fresh recruits from the castle to the main hero. Send one hero sailing to collect gold and wood; sell the surplus at the marketplace.
- The main army stays away from the enemy town until it is clearly stronger (>= 2x); the town garrison sorties against armies standing near it.

## Plan for a restart as Dungeon (red) on the Dungeon Keeper map
- Opening (worked): Town Hall d1, hire Overlord hero (Arlach-type) as main and keep the other hero as scout; dwellings 3,4,6 on days 2-5; Marketplace/Blacksmith/MG1; City Hall d9; Castle d14; Capitol d19. Income afterwards is 4-5k/day.
- Sulfur is the Dungeon's primary resource: grab the Sulfur Dune (8,10,z0), every free sulfur pile on z1 ((28,22), (30,25), (29,27)) and z0 before blue's heroes take them; Dragon Cave needs 20 sulfur + 15000 gold + 20 crystal... (trade is 10:1, useless).
- Route: town -> east -> (31,21) -> (30,25) -> (29,27) -> (25,26) gate -> (14,4,z0); the pathfinder ignores fog, so step tile by tile.
- Blue's home hero sits in the town with the whole garrison (130k+ by day 38) and sorties against armies within ~3 tiles. Do not approach before a 2x advantage.
- Keep ONE main army; second heroes are carriers/scouts only, with an army of their own, and never go into a fight with a ratio computed for another hero (I lost Edric that way).
- Answer every pending dialog before building/recruiting (the commander-resurrection dialog blocks them silently; the bridge now throws).

## Human's secret in game 8 (from the human, mid-game)
- The human's main hero (Jeddite) has the level-4 spell Resurrection and casts it in every battle, so almost no troops are lost. Effective army strength is much higher than the raw value: losses decide who wins the attrition. My bridge fights with autofight and heroes without spells, so I lose stacks every battle.
- To copy it: pick/hire a spellcaster main hero (Warlock/Wizard classes start with a spellbook), build the Mage Guild up to level 4 (Dungeon: Mage Guild 2-4 need crystals, gems, mercury, sulfur), buy the spellbook/learn spells, and cast manually in battle (the bridge has battle_act with spell).
- Human advice: try the hero Jeddite myself next time. Resurrection can only be cast once per battle and costs a lot of mana early; hero stats (knowledge, spell power) make it better later, so invest in knowledge/spell power.
- Correction from the human: Jeddite starts with Resurrection and Advanced Wisdom, so it is a hero choice, not a build path (a Mage Guild above level 2 is not needed on this small map; Mage Guild 2 is the requirement for the Dragon Cave).
- Human opening narration (game 8): day 1 Town Hall, hire a second hero, take all Troglodytes and merge the whole army under the main hero (Jeddite). Main hero hunts chests and weak monsters; the second hero takes mines and follows the main hero. Dwellings first so more units come every week; money is not the concern early, missing dwelling days is ("no dwelling, no army").
- The main hero also picks up resources when they block the path or the second hero is far away; sulfur is prioritised because it is the main resource for the Dungeon.
- Human rule: avoid attacking shooting creatures even when rated "weak" (they always do damage), unless the hero has boosting spells or fast units. My bridge's `inspect` returns `ranged`: treat ranged stacks as one rating band stronger.
- Example from the human: Chief Orcs (shooters) guard the exit of the gate; the human does not attack them and lets the opponent's heroes wear them down first.
- The human puts primary skill points into Knowledge (more mana for Resurrection). Knowledge places: Garden of Revelation, School of Magic, Redwood-type observatories; Crown of the Supreme Magi (+4 knowledge) was lying at (14,25,z0) in earlier games.
- Wraiths drain the hero's mana, so Resurrection fails against them; expect losses there (or kill them first / skip that fight).
- Diplomacy (human picked it as the better of poor level-up options): monsters joining for free needs a stronger army. Royal Griffins and Hell Hounds offered to join me in earlier games when my army was clearly stronger.
- Magic schools the human wants: Earth and Air (Town Portal/Dimension Door style travel on the map and between towns). Water Magic is mostly useless. Teleporting between towns would also fix my delivery problem of getting troops from the castle to the main hero.
- Human built the Manticore Lair (5000 gold) in week 1, same as me. Manticores are the first strong flyer; weekly stock is only 2-4, so each week of delay costs a few of them.
- Gremlins (Master Gremlins) are shooters and hit harder than their rating suggests; the human avoids them (same ranged rule). In my games a "very weak" Gremlin stack was killed without loss only when my army was 10x stronger.
- Chest choice: the human takes gold from chests (via the second hero) early while there is no City Hall yet and units are needed; XP otherwise. I follow the same rule: gold until City Hall/Capitol are built, XP for the main hero afterwards.
- The human kills very weak melee stacks (e.g. Lots of Hobgoblins) immediately when they guard gold or a pickup; waiting only lets the opponent take it. In my games the matching guard was Hobgoblins at (10,12,z1) near the windmill and chests.
- Exception to the ranged rule: the human attacks a very weak shooter stack (Pack of Archers) when it guards sulfur, and accepts losing some cheap Troglodytes for the sake of the Dragon Cave. Sulfur beats safety for the first 20 sulfur.
- Once the human had Manticores (fast flyers, strong) in the army he stopped fearing neutral stacks: a few strong flyers carry the army where cheap units only die. I fight with a few Manticores plus cheap fillers; the weekly stock of the Manticore Lair is the key unit before the Dragon Cave.
- The Dungeon's Mana Vortex (special2, 1000 gold; requires a Mage Guild level in this build) restores/doubles the visiting hero's mana, so the human can cast Resurrection on Manticores every battle. A visit to the town between fights refills mana.
- Resurrection permanence (verified in config/spells/other.json): at Basic Earth Magic (or none) the spell uses healPower "oneBattle", so the resurrected units fight again in that battle but are not kept after it. At Advanced and Expert Earth Magic it is "permanent" and the units return to the army. This is not a bug. Without Earth Magic the human's Resurrection only helps within a battle; take Earth Magic to Advanced to stop losing troops.
- A hero cannot give away his last creature: move_army/swap_stacks from a hero with a single stack silently do nothing. Leave at least 1 unit (human: "I would usually keep 1"). Workaround used in game 9: first move another stack onto the donor hero, then merge the big stack.
- Level-up answers are 0-based (choices[0] is the first skill); chest/dialog answers are 1-based. A wrong level_up index was swallowed: the bridge dropped the question but the server kept waiting and every red action was refused. The bridge now rejects an out-of-range level_up choice without dropping it.

## Catch-me-if-you-can (human advice after game 9)
- Blue needs many days to raid from its town to mine. Keep the main hero moving, send the second hero as a decoy, run around obstacles and merge reinforcements; when a new week starts, buy army and defend heroes and/or the city. Never let the main hero stand still 9 tiles from a visible threat; check `threats` every turn and compare tilesToNearestOwnTown with the enemy's movement.
- Game 9 rate check: at day 17 my army value was 33k against blue's 73k hero; the human was roughly 3x faster in army growth. Suspects: too much gold into buildings (Capitol d16, Labyrinth d17) before Dragon Cave, cheap filler units (Gogs, Wolf Riders, Troglodytes) instead of Manticores/Evil Eyes/Medusas, fights with 'very weak' stacks that gave little XP, and not upgrading dwellings.
- Next attempt is on the easiest difficulty chosen by the human.

## What won game 10 (day 44, easiest difficulty, me as Jeddite)
- Copy of the human opening plus: all dwellings first (one building per day is the limit, so buy them in order Pillar, Harpy Loft, Chapel, Manticore Lair, Labyrinth, then upgrades), sell surplus resources at the Marketplace when gold runs out, Town Hall -> MG1/Market/Blacksmith -> City Hall, Citadel -> Castle -> Capitol.
- Main hero Jeddite takes every chest as XP (option 2) and every Diplomacy join; the second hero (Malekith) only ferries recruits and holds the home town. Keep 1-2 free slots on the main hero for joins and recruits.
- Joins: Walking Dead, Zombies, Wraith Ghosts, Santa Gremlins (61, with Diplomacy), Beholders; gold-priced joins (Imps, Wolf Riders, Wights, Energy Elementals) were declined.
- Hit blue's home when its garrison was 13.8k and its heroes were far away (day 37); blue lost its only town, hid its heroes and was removed by the game 7 days later.
