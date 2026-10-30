# ClaudeBridge protocol

Local TCP, JSON lines. Every player slot with the ClaudeBridge AI listens on `127.0.0.1:37100 + player index` (red 37100, blue 37101, tan 37102, green 37103, orange 37104, purple 37105, teal 37106, pink 37107). One client per slot. Requests are handled one at a time.

Request: `{"id": 1, "cmd": "move_hero", "args": {"hero": 5, "x": 10, "y": 12, "z": 0}}` on one line.
Reply: `{"id": 1, "ok": true, "result": ...}` or `{"id": 1, "ok": false, "error": "..."}` on one line. `id` is echoed. Commands that act on the map return after movement, visits and battles finish, or when a question opens (120 s timeout). In spectator mode (`VCMI_CLAUDE_SPECTATE`) only read-only commands are accepted.

`mcp/server.py` is the reference client; its `TOOLS` table holds the full description of every command and argument. `?` marks an optional argument.

| Command | Arguments | Purpose |
| --- | --- | --- |
| `ping` | | Connection check; result `"pong"` |
| `get_state` | hero?, radius? | Date, resources, heroes, towns, visible objects, questions, events |
| `status` | | Pending questions and events since the last report |
| `wait_for_turn` | timeoutMs? | Block until our turn starts or a battle needs orders |
| `end_turn` | | End our turn |
| `find_path`, `move_hero` | hero, x, y, z, avoidGuards? (`move_hero`) | Path check; move along the engine pathfinder. With `avoidGuards` the hero stops in front of a guarded tile: reply `stopped: "guarded"` plus `guard: {id, name, pos}` |
| `targets` | hero, limit? | Reachable objects sorted by travel time |
| `tile_info` | x, y, z, radius? | ASCII map around a tile |
| `get_buildable`, `build` | town, all? / building | Town buildings |
| `recruit` | town? or object+hero, creature, count, garrison? | Recruit in a town or at a dwelling |
| `move_army`, `swap_stacks` | from, to, fromSlot, toSlot, count? | Army transfers |
| `upgrade`, `buy_spellbook` | hero, slot / hero | Hero actions in a town |
| `trade` | town, give, get, amount | Marketplace |
| `get_tavern`, `buy_hero` | town, hero | Tavern |
| `build_boat` | shipyard | Buy a boat |
| `artifacts`, `equip` | hero, from, to? | Artifacts |
| `inspect`, `threats`, `thieves_guild`, `lookup` | see `TOOLS` | Intel and data lookup |
| `battle_state`, `battle_act` | see `TOOLS` | Manual battle control |
| `answer_question` | id, choice | Resolve a pending question |
| `save_game` | name | Request a save |

`next_turn` in the MCP server is not a bridge command: it is `end_turn`, `wait_for_turn` and `threats` in sequence.
