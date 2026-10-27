#!/usr/bin/env python3
"""Digest of JOURNAL lines in the VCMI client log: per day and player, builds, recruits, hires and the last tile of each hero."""
import re
import sys
from collections import OrderedDict

path = sys.argv[1]
player = sys.argv[2] if len(sys.argv) > 2 else None
NAMES = {0: "MageGuild1", 1: "MageGuild2", 2: "MageGuild3", 3: "MageGuild4", 4: "MageGuild5", 5: "Tavern", 6: "Shipyard", 7: "Fort", 8: "Citadel",
         9: "Castle", 10: "VillageHall", 11: "TownHall", 12: "CityHall", 13: "Capitol", 14: "Marketplace", 15: "ResourceSilo", 16: "Blacksmith",
         17: "Special1", 18: "Horde1", 19: "Horde1Upg", 21: "Special2", 22: "Special3", 23: "Special4", 24: "Horde2", 25: "Horde2Upg", 26: "Grail"}
for i in range(7):
    NAMES[30 + i] = "Dwelling%d" % (i + 1)
    NAMES[37 + i] = "Dwelling%dUpg" % (i + 1)
days = OrderedDict()
pattern = re.compile(r"JOURNAL day (\d+) (\w+): (.*)")
for line in open(path, errors="replace"):
    m = pattern.search(line)
    if not m:
        continue
    day, who, text = int(m.group(1)), m.group(2), m.group(3)
    if player and who != player:
        continue
    entry = days.setdefault((day, who), {"build": [], "recruit": [], "hire": [], "boat": 0, "pos": {}, "snap": ""})
    if text.startswith("snapshot"):
        entry["snap"] = text[9:]
    elif text.startswith("build boat"):
        entry["boat"] += 1
    elif text.startswith("build "):
        bid, _, town = text[6:].partition(" in town ")
        entry["build"].append("%s@%s" % (NAMES.get(int(bid), bid), town))
    elif text.startswith("recruit "):
        entry["recruit"].append(text[8:])
    elif text.startswith("hire "):
        entry["hire"].append(text[5:])
    elif text.startswith("move hero "):
        hero, _, dest = text[10:].partition(" to ")
        entry["pos"][hero] = dest
for (day, who), e in days.items():
    parts = []
    if e["build"]:
        parts.append("build " + "; ".join(e["build"]))
    if e["recruit"]:
        parts.append("recruit " + "; ".join(e["recruit"]))
    if e["hire"]:
        parts.append("hire " + "; ".join(e["hire"]))
    if e["boat"]:
        parts.append("boat x%d" % e["boat"])
    if e["snap"]:
        parts.append(e["snap"])
    else:
        parts.append("heroes " + ", ".join("%s@%s" % kv for kv in e["pos"].items()))
    print("day %d %s: %s" % (day, who, " | ".join(parts)))
