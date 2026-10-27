#!/usr/bin/env python3
"""MCP stdio server that forwards tool calls to a ClaudeBridge AI inside a running VCMI client.

Environment: CLAUDE_BRIDGE_PORT (default 37100 + player index, so red = 37100, blue = 37101, ...).
"""
import json
import os
import socket
import sys

PORT = int(os.environ.get("CLAUDE_BRIDGE_PORT", "37100"))


def connect():
    """Tries the configured port first, then the other player ports (the slot depends on the map)."""
    last = None
    for port in [PORT] + [p for p in range(37100, 37108) if p != PORT]:
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=2)
        except OSError as e:
            last = e
    raise last

INT = {"type": "integer"}
POS = {"x": INT, "y": INT, "z": INT}

# name -> (bridge command, description, properties, required)
TOOLS = {
    "game_state": ("get_state", "State of our player: date, resources, heroes, towns (with buildings and recruitable creatures), visible map objects, pending questions and new events. Pass hero and radius to list only map objects near that hero.", {"hero": INT, "radius": INT}, []),
    "game_status": ("status", "Pending questions and events since the last report; cheap.", {}, []),
    "wait_for_turn": ("wait_for_turn", "Block until our turn starts or one of our units needs battle orders (the result has yourTurn and battle flags), or the timeout passes. Call after end_turn.", {"timeoutMs": INT}, []),
    "find_path": ("find_path", "Check whether a hero can reach a tile and how many turns it takes.", {"hero": INT, **POS}, ["hero", "x", "y", "z"]),
    "move_hero": ("move_hero", "Move a hero toward a tile along the engine pathfinder, until it arrives, runs out of movement, or a question/battle interrupts. Battles are fought by BattleAI. Targeting the tile the hero already stands on visits the object there (like the Space key): use it to re-enter a Subterranean Gate or visit a building again.", {"hero": INT, **POS}, ["hero", "x", "y", "z"]),
    "get_buildable": ("get_buildable", "Buildings a town can build now (allowed, no_resources, built_today). Pass all=true to include ones with missing requirements.", {"town": INT, "all": {"type": "boolean"}}, ["town"]),
    "build": ("build", "Build a structure in a town (one per town per day). 'building' is the key from get_buildable.", {"town": INT, "building": {"type": "string"}}, ["town", "building"]),
    "recruit": ("recruit", "Recruit creatures. In our town: pass town (goes to the visiting hero, else the garrison; with garrison=true always to the town garrison, which has its own 7 slots). At a map dwelling the hero stands on: pass object and hero.", {"town": INT, "object": INT, "hero": INT, "creature": {"type": "string"}, "count": INT, "garrison": {"type": "boolean"}}, ["creature", "count"]),
    "move_army": ("move_army", "Move every stack of one of our heroes/towns into another (merging same creatures). 'from' and 'to' are object ids; both must be adjacent (hero in or next to the town, or two heroes together).", {"from": INT, "to": INT}, ["from", "to"]),
    "swap_stacks": ("swap_stacks", "Merge or swap one stack with another slot. Slots are 0-6. With 'count', moves that many creatures (a split) and leaves the rest, so a hero can keep 1 creature and hand over the others.", {"from": INT, "to": INT, "fromSlot": INT, "toSlot": INT, "count": INT}, ["from", "to", "fromSlot", "toSlot"]),
    "trade": ("trade", "Trade resources at a town marketplace: give N of one resource for another (wood, mercury, ore, sulfur, crystal, gems, gold). Amount is rounded down to the rate's base quantity; the result shows the rate. One Marketplace gives poor rates.", {"town": INT, "give": {"type": "string"}, "get": {"type": "string"}, "amount": INT}, ["town", "give", "get", "amount"]),
    "get_tavern": ("get_tavern", "Heroes available to hire in a town's tavern.", {"town": INT}, ["town"]),
    "buy_hero": ("buy_hero", "Hire a tavern hero (ids from get_tavern). The town's visiting slot must be empty.", {"town": INT, "hero": INT}, ["town", "hero"]),
    "battle_state": ("battle_state", "When a unit of ours is waiting for orders in a battle (or, with tactics: true, during the deployment phase before the first round, when our units can be moved inside our start zone: pass unit with moves=true for the hexes that unit may go to): combat strength of both sides (strength.ours/theirs), all units (id, creature, count, hex, attack/defense/damage/speed), the active unit, our hero's mana and spells with costs, retreat/surrender availability, and targets with the hexes to attack from. Pass moves=true to also list the hexes the active unit can move to. Hex id = y*17 + x.", {"moves": {"type": "boolean"}, "unit": INT}, []),
    "battle_act": ("battle_act", "Order for the waiting unit: during the tactics phase only: move (unit id, hex) places one of our units, end_tactics starts the fight, auto lets BattleAI deploy. Otherwise action = retreat or surrender (leave the battle), move (hex), attack (target unit id, optional from hex), shoot (target), wait, defend, cast (spell key, target unit id or hex; does not end the unit's turn), auto (BattleAI plays this unit), autofight (BattleAI plays the rest of the battle). Returns the next battle_state or battleOver.", {"action": {"type": "string"}, "target": INT, "unit": INT, "hex": INT, "from": INT, "spell": {"type": "string"}}, ["action"]),
    "inspect": ("inspect", "What we can see of a visible enemy (or own) hero, town or map monster: owner, army (exact counts only when detailed, otherwise size ranges), hero skills and mana, town fort level and income. For a monster stack: creature, size range and, when 'hero' (our hero id) is given, the same threat rating the game shows (effortless .. impossible) comparing the stack with that hero's army. Use before attacking.", {"id": INT, "hero": INT}, ["id"]),
    "threats": ("threats", "Every visible hero and town that is not ours, with owner colour, rough army value (count x AI value, ranges at midpoint), ratio and threat label against our strongest hero (or 'hero'), and distance in tiles to our nearest town. Call at the start of every turn. Ignores hero skills and artifacts, so treat 'a bit weaker' as even.", {"hero": INT}, []),
    "targets": ("targets", "Objects the hero can reach now, sorted by turns then steps: id, type, pos, steps, turns, and for monsters size and threat. Use it to chain fights and pickups without detours. Does not list unreachable (fogged or walled-off) objects.", {"hero": INT, "limit": INT}, ["hero"]),
    "artifacts": ("artifacts", "Artifacts of one of our heroes: worn (slot, key, name, description, fitsSlots) and backpack (slots 19+).", {"hero": INT}, ["hero"]),
    "equip": ("equip", "Move an artifact of a hero: from = its slot (backpack slots are 19+), to = target slot (omit to use the first free slot that fits; pass a worn slot to swap, or 19+ to unequip). Returns the hero's artifacts.", {"hero": INT, "from": INT, "to": INT}, ["hero", "from"]),
    "thieves_guild": ("thieves_guild", "Intel on all players, as the Guild of Thieves shows it: ranking (first place first, tied players grouped) of towns, heroes, gold, wood and ore, rare resources, obelisks, artifacts, army and income, plus best hero and best creature per player. Pass one of our towns; more Taverns in our towns reveal more categories.", {"town": INT}, ["town"]),
    "lookup": ("lookup", "Look up game data without needing a visible object: kind = creature, building or spell, name = part of the key or name (e.g. 'archer', 'city hall', 'fly'). Creatures: stats, cost, growth, abilities, upgrades. Buildings (all factions): description, cost, daily production, requirements, what they upgrade. Spells: level, mana cost per skill level, description. Buildings exist in every faction: pass faction (e.g. castle) to narrow. At most 6 matches.", {"kind": {"type": "string"}, "name": {"type": "string"}, "faction": {"type": "string"}}, ["kind", "name"]),
    "tile_info": ("tile_info", "ASCII map around a tile (terrain, fog, objects incl. boats) plus an object list with ids. Radius up to 15.", {**POS, "radius": INT}, ["x", "y", "z"]),
    "build_boat": ("build_boat", "Buy a boat at a shipyard map object (id from game_state). Then move the hero onto the boat tile to embark.", {"shipyard": INT}, ["shipyard"]),
    "answer_question": ("answer_question", "Answer a pending question from game_status. Level-up: choice is the index in choices. Dialog: 1 = yes or the component number, 0 = no/cancel.", {"id": INT, "choice": INT}, ["id", "choice"]),
    "upgrade": ("upgrade", "Upgrade the creature stack in a hero's slot to its best upgrade (e.g. Red -> Black Dragon). The hero must stand in a town that has the upgraded dwelling; costs the price difference.", {"hero": INT, "slot": INT}, ["hero", "slot"]),
    "buy_spellbook": ("buy_spellbook", "Buy a spellbook (Mage Guild) for a hero that has none; the hero must stand in a town with a Mage Guild.", {"hero": INT}, ["hero"]),
    "save_game": ("save_game", "Request a save under the given name. In all-AI games this did NOT produce a loadable file (game 18); do not rely on it, ask the human to save manually.", {"name": {"type": "string"}}, ["name"]),
    "next_turn": ("end_turn", "End our turn, wait until our next turn (or a battle that needs orders) starts, and return the wait result plus the threats report. One call per turn instead of end_turn + wait_for_turn + threats.", {}, []),
    "end_turn": ("end_turn", "End our turn. Fails while questions are pending.", {}, []),
}


class Bridge:
    def __init__(self):
        self.sock = None
        self.reader = None
        self.next_id = 1

    def call(self, cmd, args, timeout=300):
        request = {"id": self.next_id, "cmd": cmd, "args": args}
        self.next_id += 1
        payload = (json.dumps(request) + "\n").encode()
        # reconnect once if the connection died before the request was delivered; never resend after a read timeout
        for attempt in (0, 1):
            try:
                if self.sock is None:
                    self.sock = connect()
                    self.reader = self.sock.makefile("r", encoding="utf-8")
                self.sock.settimeout(timeout)
                self.sock.sendall(payload)
                break
            except OSError:
                self.sock = None
                if attempt == 1:
                    raise
        try:
            line = self.reader.readline()
            if not line:
                raise ConnectionError("bridge closed the connection")
            return json.loads(line)
        except (OSError, ConnectionError):
            self.sock = None
            raise


bridge = Bridge()


def tool_list():
    return [
        {
            "name": name,
            "description": description,
            "inputSchema": {"type": "object", "properties": props, "required": required},
        }
        for name, (_, description, props, required) in TOOLS.items()
    ]


def next_turn():
    reply = bridge.call("end_turn", {})
    if not reply.get("ok"):
        return {"content": [{"type": "text", "text": "error: " + str(reply.get("error"))}], "isError": True}
    result = {}
    for _ in range(10):
        waited = bridge.call("wait_for_turn", {"timeoutMs": 60000})
        if not waited.get("ok"):
            return {"content": [{"type": "text", "text": "error: " + str(waited.get("error"))}], "isError": True}
        result = waited.get("result") or {}
        if result.get("yourTurn") or result.get("battle"):
            break
    if result.get("yourTurn"):
        threats = bridge.call("threats", {})
        result["threats"] = threats.get("result") if threats.get("ok") else threats.get("error")
    return {"content": [{"type": "text", "text": json.dumps(result, separators=(",", ":"))}]}


def call_tool(name, arguments):
    if name == "next_turn":
        try:
            return next_turn()
        except OSError as e:
            return {"content": [{"type": "text", "text": f"cannot reach the game on ports 37100-37107: {e}"}], "isError": True}
    if name not in TOOLS:
        return {"content": [{"type": "text", "text": f"unknown tool {name}"}], "isError": True}
    cmd = TOOLS[name][0]
    try:
        reply = bridge.call(cmd, arguments or {})
    except OSError as e:
        return {"content": [{"type": "text", "text": f"cannot reach the game on ports 37100-37107: {e}"}], "isError": True}
    if not reply.get("ok"):
        return {"content": [{"type": "text", "text": "error: " + str(reply.get("error"))}], "isError": True}
    return {"content": [{"type": "text", "text": json.dumps(reply.get("result"), separators=(",", ":"))}]}


def respond(msg_id, result=None, error=None):
    message = {"jsonrpc": "2.0", "id": msg_id}
    if error is not None:
        message["error"] = error
    else:
        message["result"] = result
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        msg = json.loads(line)
        method = msg.get("method")
        msg_id = msg.get("id")
        if method == "initialize":
            respond(msg_id, {
                "protocolVersion": msg.get("params", {}).get("protocolVersion", "2024-11-05"),
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "vcmi-claude-bridge", "version": "0.1"},
            })
        elif method == "tools/list":
            respond(msg_id, {"tools": tool_list()})
        elif method == "tools/call":
            params = msg.get("params", {})
            respond(msg_id, call_tool(params.get("name"), params.get("arguments")))
        elif method == "ping":
            respond(msg_id, {})
        elif msg_id is not None:
            respond(msg_id, error={"code": -32601, "message": f"method not found: {method}"})


if __name__ == "__main__":
    main()
