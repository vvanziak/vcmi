# ClaudeBridge: playing VCMI through tool calls

Local experiment, not intended for upstream. Goal: let Claude (via Claude Code and MCP) control one player slot in a VCMI game, using structured state instead of screenshots, and keep a knowledge base between games.

## Why this seam

A player is a `CGameInterface` (events in) plus a `CCallback` (queries and requests out). The human UI (`CPlayerInterface`) and every AI use the same pair, so a new AI gets exactly the information and rights a human has, including fog of war. The server validates every request as for any other client.

`AIFactory` creates adventure AIs by name. The name comes from `PlayerSettings::name` or from `settings["ai"]["adventureEnemyAI"]` / `adventureAlliedAI`, so selecting the plugin needs no UI change.

## Architecture

```
Claude Code <--stdio MCP--> bridge server (Python/TS) <--local TCP, JSON lines--> ClaudeBridgeAI (in the client process)
```

- `ClaudeBridgeAI` lives in the `vcmi` shared library like the other AIs.
- `yourTurn` only marks the turn as started. Commands run on the bridge's own thread through `CCallback`, one request at a time, and the turn lasts until `end_turn`.
- Dialogs (`showBlockingDialog`, level-ups, garrison, teleport, object select) become pending questions. The tool results list them, and `answer_question` resolves them. A turn cannot end while a question is pending.
- The bridge process is a thin translator. It holds no game logic, so it can be replaced by a direct API agent loop later without touching the plugin.

## Status

Phase 1 (JSON dump) and phase 2/3 (live loop) are done and tested headless: Claude (red) against Nullkiller2 (green), and two ClaudeBridge slots driven by a script.

- `ClaudeBridgeAI` derives from `CAdventureAI`, so `BattleAI` fights every battle.
- `BridgeServer` serves one local TCP client per player at `127.0.0.1:37100 + player index` (red 37100, blue 37101, tan 37102, green 37103, ...). Requests and replies are one JSON object per line.
- `mcp/server.py` is a stdlib-only MCP stdio server that maps tools to bridge commands.
- Commands: `ping`, `wait_for_turn`, `get_state`, `status`, `find_path`, `move_hero`, `get_buildable`, `build`, `recruit`, `answer_question`, `end_turn`.
- Dialogs and level-ups become pending questions; `end_turn` refuses while any is open. Teleport, garrison, map-object-select and commander level-up dialogs are answered automatically.
- Waiting: every action waits until hero movement, visits and battles are over, or a question opens (120 s timeout).
- Threading: commands run on the bridge thread under a shared `CGameState::mutex`; `waitTillRealize` is on, so requests return after the server applied them. Event callbacks on the network thread only touch the mutex-guarded status.

## Running

```sh
# one slot for Claude, the rest use the configured AI
VCMI_CLAUDE_PLAYER=red vcmiclient --headless --disable-video --nointro --testmap "Maps/A Warm and Familiar Place"
# register the MCP server with Claude Code
claude mcp add vcmi -- python3 /path/to/vcmi/AI/ClaudeBridge/mcp/server.py
```

`VCMI_CLAUDE_PLAYER` is read in `CClient::aiNameForPlayer`. Set `CLAUDE_BRIDGE_PORT` for the MCP server when the Claude slot is not red.

## Combat

`battle_state` and `battle_act` give manual control. The bridge hands a unit to BattleAI (`auto`, `autofight`) or lets it fight alone whenever no tool client is connected. Actions: move, attack, shoot, wait, defend, cast, retreat, surrender. `inspect` shows what the game UI would show about a visible enemy hero or town (exact counts only when detailed). `rejected` in a battle reply means the server refused the action.

Requests that can start a battle (`move_hero`, `answer_question`) are sent without waiting for the server, because the server answers only after the battle; `requestSent` and `requestRealized` track them.

## Next

- Hot-seat or network game with a human in the GUI (not tried yet; the GUI process should behave like any AI slot).
- Army tools: `swap_stacks`, `merge`, `split`, hero exchange, `buy_hero`, adventure spells, markets.
- Observation size: `get_state` returns every visible object; add radius and relevance filters and a per-turn diff.
- Dialog components are only numeric (`type`, `value`); give them readable names.
- Some objects 3-4 tiles from the start report `reachable: false`; not investigated.
- Knowledge base: `notes/` playbook and per-game log.

## Decisions to make later

- Timeouts: the server can enforce turn timers. Games against Claude should disable them.
- Determinism and saves: the AI holds no serialized state. Anything added later that must survive save and load needs the serialization version rules from `AGENTS.md`.
- Upstreaming: the `VCMI_CLAUDE_PLAYER` hook and the `ClaudeBridge` entries in `settings.json` schema and `AIFactory` are local experiment code.

## Tool list (as of game 2)

`mcp/server.py` exposes: game_state, game_status, wait_for_turn, find_path, move_hero (self-tile move visits the object), get_buildable, build, recruit, move_army, swap_stacks, trade, get_tavern, buy_hero, build_boat, battle_state (strength, tactics, units), battle_act (move, attack, shoot, wait, defend, cast, retreat, surrender, auto, autofight, tactics: move, end_tactics), inspect (heroes, towns, monsters), tile_info, answer_question, end_turn, lookup, thieves_guild, artifacts, equip.

## Running a game

1. `VCMI_CLAUDE_PLAYER=green vcmiclient --nointro --logLocation <dir>` and load the save from the menu.
2. `claude mcp add vcmi -e CLAUDE_BRIDGE_PORT=37103 -- python3 <repo>/AI/ClaudeBridge/mcp/server.py`; `/mcp` to reconnect after each client restart.
3. Rebuild with `ninja` in `~/Projects/vcmi-build`; new tools need a client restart and `/mcp`.
4. Notes for learning: `notes/playbook.md` (lessons), `notes/games.md` (per game log), `notes/vcmi-bugs.md` (stock VCMI bugs).

## Tools added after game 3
- `threats` (visible enemy heroes and towns: rough army value, ratio and label against our strongest hero, tiles to our nearest town), `targets` (reachable objects of a hero sorted by turns/steps, with monster threat), `next_turn` (end_turn + wait + threats), `combatValue` in each hero of `game_state`, `strengthRatio`/`threat` on `inspect` of heroes and towns when `hero` is given.
- Setups without a human: `--onlyAI --spectate --spectate-skip-battle`, `VCMI_CLAUDE_PLAYER=<color of the slot>`; the MCP server tries ports 37100-37107.
