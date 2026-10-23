#!/usr/bin/env python3
"""Reads the dump of the engine (BEGOTTEN_DUMP) of a lab built with LAB_AUTORUN=1 and tells which demos changed something.

usage: check_script_lab.py lab_auto.json dump.txt
"""
import json, re, sys, collections

man = json.load(open(sys.argv[1]))
names = man['sectors']
rooms = {r['n']: r for r in man['rooms']}
ev = collections.defaultdict(lambda: collections.defaultdict(list))   # room -> kind -> events

def room_of_sector(i, tag):
    nm = names[i]
    m = re.match(r'^[A-Za-z]+(\d+)', nm)
    if m and nm[0] not in 'BSCL' or (m and nm[:1] in 'BL' and nm[:2] != 'BR' and nm[:2] != 'LD'):
        return int(m.group(1))
    return None

for line in open(sys.argv[2]):
    p = line.split()
    if len(p) < 3:
        continue
    tic, kind = int(p[0]), p[1]
    if tic < 40:
        continue            # the state of the level at the start
    if kind == 'S':
        i = int(p[2])
        n = room_of_sector(i, int(p[4]))
        if n and not names[i].startswith('D') and not names[i].startswith('E'):
            ev[n]['sec'].append(line.strip())
    elif kind == 'L':
        n = int(p[4])
        if n in rooms:
            ev[n]['line'].append(line.strip())
    elif kind == 'P':
        n = int(p[4])
        if n in rooms:
            ev[n]['thing'].append(line.strip())
    elif kind == 'T':
        t = int(p[2]); n = t % 1000 if t < 4000 else None
        if n in rooms:
            ev[n]['thing'].append(line.strip())
# the walk phase: scripts started by the player crossing the entry line, after the automatic phase
walk = collections.defaultdict(list)
if len(sys.argv) > 3:
    for l in open(sys.argv[3]):
        m = re.match(r'ACS start (\d+) map 0 tic (\d+)', l)
        if m and int(m.group(2)) >= 780:
            walk[int(m.group(1))].append(int(m.group(2)))
# jumps of the player (teleports)
jumps = []
last = None
for line in open(sys.argv[2]):
    p = line.split()
    if len(p) > 8 and p[1] == 'U':
        pos = (int(p[3]), int(p[5]))
        if last and abs(pos[0] - last[0]) + abs(pos[1] - last[1]) > 60:
            jumps.append((int(p[0]), pos))
        last = pos
bad = 0
for n in sorted(rooms):
    r = rooms[n]
    if r['warn']:
        continue
    got = {k: len(v) for k, v in ev[n].items()}
    ok = r['expect'] == 'none' or got.get(r['expect'])
    if r.get('walk'):
        got['walk'] = len(walk.get(n, []))
        ok = bool(walk.get(n))
        if r['name'].startswith('Teleport') and r['name'] not in ('Teleport_NewMap', 'Teleport_EndGame', 'TeleportOther', 'TeleportGroup', 'TeleportInSector', 'Teleport_ZombieChanger'):
            t0 = walk[n][0] if walk.get(n) else None
            got['jump'] = sum(1 for j in jumps if t0 and t0 <= j[0] <= t0 + 160)
            ok = ok and got['jump'] > 0
        if r['expect'] in ('sec', 'line', 'thing') and not got.get(r['expect']):
            ok = False
    if not ok:
        bad += 1
    print('%s %3d %-34s kit %-12s expect %-5s saw %s' % ('ok ' if ok else 'BAD', n, r['name'], r['kit'], r['expect'], got or '-'))
print('%d demos without the expected change' % bad)
