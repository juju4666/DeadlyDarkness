#!/usr/bin/env python3
"""
acs_check.py - check compiled ACS (BEHAVIOR lumps) against what the Begotten engine can actually run.

  python3 acs_check.py begotten.wad        # every map's BEHAVIOR lump in a wad
  python3 acs_check.py MYMAP.o             # a raw compiled object (what acc writes)

Reports, per map:
  * ZDoom's extended object format ("ACSe"/"ACSE")  -> the engine refuses it at map load
  * instructions the engine does not have            -> the game STOPS with "invalid ACS instruction"
    when that line runs (acc -h still compiles these without complaint)
  * variable indexes outside the engine's limits
  * script types / argument counts the engine does not handle
Exit status: 0 = all fine, 1 = problems found.
"""
import struct, sys, re

SAFE_OPCODES = 102                 # the engine implements opcodes 0..101 (the original Hexen instruction set)
OPERANDS = {3: 1, 4: 1, 5: 1, 6: 1, 7: 1, 8: 1, 9: 2, 10: 3, 11: 4, 12: 5, 13: 6, 25: 1, 26: 1, 27: 1, 28: 1, 29: 1, 30: 1, 31: 1, 32: 1, 33: 1, 34: 1, 35: 1, 36: 1, 37: 1, 38: 1, 39: 1, 40: 1, 41: 1, 42: 1, 43: 1, 44: 1, 45: 1, 46: 1, 47: 1, 48: 1, 49: 1, 50: 1, 51: 1, 52: 1, 53: 1, 56: 1, 58: 2, 60: 2, 62: 1, 64: 1, 66: 2, 68: 2, 79: 1, 82: 1, 84: 2}                   # opcode -> number of 4-byte operands that follow it
VAR_OPS = {25: 'script', 26: 'map', 27: 'world', 28: 'script', 29: 'map', 30: 'world', 31: 'script', 32: 'map', 33: 'world', 34: 'script', 35: 'map', 36: 'world', 37: 'script', 38: 'map', 39: 'world', 40: 'script', 41: 'map', 42: 'world', 43: 'script', 44: 'map', 45: 'world', 46: 'script', 47: 'map', 48: 'world', 49: 'script', 50: 'map', 51: 'world'}                 # opcode -> which variable table its operand indexes
VAR_LIMITS = {'script': 10, 'map': 32, 'world': 64}
NAMES = {102: 'Activatorsound', 103: 'Localambientsound', 104: 'Setlinemonsterblocking', 105: 'Playerblueskull', 106: 'Playerredskull', 107: 'Playeryellowskull', 108: 'Playermasterskull', 109: 'Playerbluecard', 110: 'Playerredcard', 111: 'Playeryellowcard', 112: 'Playermastercard', 113: 'Playerblackskull', 114: 'Playersilverskull', 115: 'Playergoldskull', 116: 'Playerblackcard', 117: 'Playersilvercard', 118: 'Isnetworkgame', 119: 'Playerteam', 120: 'Playerhealth', 121: 'Playerarmorpoints', 122: 'Playerfrags', 123: 'Playerexpert', 124: 'Blueteamcount', 125: 'Redteamcount', 126: 'Blueteamscore', 127: 'Redteamscore', 128: 'Isoneflagctf', 129: 'Getinvasionwave', 130: 'Getinvasionstate', 131: 'Printname', 132: 'Musicchange', 133: 'Consolecommanddirect', 134: 'Consolecommand', 135: 'Singleplayer', 136: 'Fixedmul', 137: 'Fixeddiv', 138: 'Setgravity', 139: 'Setgravitydirect', 140: 'Setaircontrol', 141: 'Setaircontroldirect', 142: 'Clearinventory', 143: 'Giveinventory', 144: 'Giveinventorydirect', 145: 'Takeinventory', 146: 'Takeinventorydirect', 147: 'Checkinventory', 148: 'Checkinventorydirect', 149: 'Spawn', 150: 'Spawndirect', 151: 'Spawnspot', 152: 'Spawnspotdirect', 153: 'Setmusic', 154: 'Setmusicdirect', 155: 'Localsetmusic', 156: 'Localsetmusicdirect', 157: 'Printfixed', 158: 'Printlocalized', 159: 'Morehudmessage', 160: 'Opthudmessage', 161: 'Endhudmessage', 162: 'Endhudmessagebold', 163: 'Setstyle', 164: 'Setstyledirect', 165: 'Setfont', 166: 'Setfontdirect', 167: 'Pushbyte', 168: 'Lspec1directb', 169: 'Lspec2directb', 170: 'Lspec3directb', 171: 'Lspec4directb', 172: 'Lspec5directb', 173: 'Delaydirectb', 174: 'Randomdirectb', 175: 'Pushbytes', 176: 'Push2bytes', 177: 'Push3bytes', 178: 'Push4bytes', 179: 'Push5bytes', 180: 'Setthingspecial', 181: 'Assignglobalvar', 182: 'Pushglobalvar', 183: 'Addglobalvar', 184: 'Subglobalvar', 185: 'Mulglobalvar', 186: 'Divglobalvar', 187: 'Modglobalvar', 188: 'Incglobalvar', 189: 'Decglobalvar', 190: 'Fadeto', 191: 'Faderange', 192: 'Cancelfade', 193: 'Playmovie', 194: 'Setfloortrigger', 195: 'Setceilingtrigger', 196: 'Getactorx', 197: 'Getactory', 198: 'Getactorz', 199: 'Starttranslation', 200: 'Translationrange1', 201: 'Translationrange2', 202: 'Endtranslation', 203: 'Call', 204: 'Calldiscard', 205: 'Returnvoid', 206: 'Returnval', 207: 'Pushmaparray', 208: 'Assignmaparray', 209: 'Addmaparray', 210: 'Submaparray', 211: 'Mulmaparray', 212: 'Divmaparray', 213: 'Modmaparray', 214: 'Incmaparray', 215: 'Decmaparray', 216: 'Dup', 217: 'Swap', 218: 'Writetoini', 219: 'Getfromini', 220: 'Sin', 221: 'Cos', 222: 'Vectorangle', 223: 'Checkweapon', 224: 'Setweapon', 225: 'Tagstring', 226: 'Pushworldarray', 227: 'Assignworldarray', 228: 'Addworldarray', 229: 'Subworldarray', 230: 'Mulworldarray', 231: 'Divworldarray', 232: 'Modworldarray', 233: 'Incworldarray', 234: 'Decworldarray', 235: 'Pushglobalarray', 236: 'Assignglobalarray', 237: 'Addglobalarray', 238: 'Subglobalarray', 239: 'Mulglobalarray', 240: 'Divglobalarray', 241: 'Modglobalarray', 242: 'Incglobalarray', 243: 'Decglobalarray', 244: 'Setmarineweapon', 245: 'Setactorproperty', 246: 'Getactorproperty', 247: 'Playernumber', 248: 'Activatortid', 249: 'Setmarinesprite', 250: 'Getscreenwidth', 251: 'Getscreenheight', 252: 'ThingProjectile2', 253: 'Strlen', 254: 'Sethudsize', 255: 'Getcvar', 256: 'Casegotosorted', 257: 'Setresultvalue', 258: 'Getlinerowoffset', 259: 'Getactorfloorz', 260: 'Getactorangle', 261: 'Getsectorfloorz', 262: 'Getsectorceilingz', 263: 'Lspec5result', 264: 'Getsigilpieces', 265: 'Getlevelinfo', 266: 'Changesky', 267: 'Playeringame', 268: 'Playerisbot', 269: 'Setcameratotexture', 270: 'Endlog', 271: 'Getammocapacity', 272: 'Setammocapacity', 273: 'Printmapchararray', 274: 'Printworldchararray', 275: 'Printglobalchararray', 276: 'Setactorangle', 277: 'Grabinput', 278: 'Setmousepointer', 279: 'Movemousepointer', 280: 'Spawnprojectile', 281: 'Getsectorlightlevel', 282: 'Getactorceilingz', 283: 'Setactorposition', 284: 'Clearactorinventory', 285: 'Giveactorinventory', 286: 'Takeactorinventory', 287: 'Checkactorinventory', 288: 'Thingcountname', 289: 'Spawnspotfacing', 290: 'Playerclass', 291: 'Andscriptvar', 292: 'Andmapvar', 293: 'Andworldvar', 294: 'Andglobalvar', 295: 'Andmaparray', 296: 'Andworldarray', 297: 'Andglobalarray', 298: 'Eorscriptvar', 299: 'Eormapvar', 300: 'Eorworldvar', 301: 'Eorglobalvar', 302: 'Eormaparray', 303: 'Eorworldarray', 304: 'Eorglobalarray', 305: 'Orscriptvar', 306: 'Ormapvar', 307: 'Orworldvar', 308: 'Orglobalvar', 309: 'Ormaparray', 310: 'Orworldarray', 311: 'Orglobalarray', 312: 'Lsscriptvar', 313: 'Lsmapvar', 314: 'Lsworldvar', 315: 'Lsglobalvar', 316: 'Lsmaparray', 317: 'Lsworldarray', 318: 'Lsglobalarray', 319: 'Rsscriptvar', 320: 'Rsmapvar', 321: 'Rsworldvar', 322: 'Rsglobalvar', 323: 'Rsmaparray', 324: 'Rsworldarray', 325: 'Rsglobalarray', 326: 'Getplayerinfo', 327: 'Changelevel', 328: 'Sectordamage', 329: 'Replacetextures', 330: 'Negatebinary', 331: 'Getactorpitch', 332: 'Setactorpitch', 333: 'Printbind', 334: 'Setactorstate', 335: 'Thingdamage2', 336: 'Useinventory', 337: 'Useactorinventory', 338: 'Checkactorceilingtexture', 339: 'Checkactorfloortexture', 340: 'Getactorlightlevel', 341: 'Setmugshotstate', 342: 'Thingcountsector', 343: 'Thingcountnamesector', 344: 'Checkplayercamera', 345: 'Morphactor', 346: 'Unmorphactor', 347: 'Getplayerinput', 348: 'Classifyactor', 349: 'Printbinary', 350: 'Printhex', 351: 'Callfunc', 352: 'Savestring', 353: 'Printmapchrange', 354: 'Printworldchrange', 355: 'Printglobalchrange', 356: 'Strcpytomapchrange', 357: 'Strcpytoworldchrange', 358: 'Strcpytoglobalchrange', 359: 'Pushfunction', 360: 'Callstack', 361: 'Scriptwaitnamed', 362: 'Translationrange3', 363: 'Gotostack', 364: 'Assignscriptarray', 365: 'Pushscriptarray', 366: 'Addscriptarray', 367: 'Subscriptarray', 368: 'Mulscriptarray', 369: 'Divscriptarray', 370: 'Modscriptarray', 371: 'Incscriptarray', 372: 'Decscriptarray', 373: 'Andscriptarray', 374: 'Eorscriptarray', 375: 'Orscriptarray', 376: 'Lsscriptarray', 377: 'Rsscriptarray', 378: 'Printscriptchararray', 379: 'Printscriptchrange', 380: 'Strcpytoscriptchrange', 381: 'Lspec5ex', 382: 'Lspec5exresult', 383: 'Translationrange4', 384: 'Translationrange5', 385: 'PcodeCommandCount'}                    # names of instructions the engine does NOT have (for the report)

def read_lumps(path):
    d = open(path, 'rb').read()
    if d[:4] in (b'IWAD', b'PWAD'):
        n, off = struct.unpack('<ii', d[4:12]); out = []; last_map = '?'
        for i in range(n):
            pos, size, nm = struct.unpack('<ii8s', d[off + i * 16:off + i * 16 + 16])
            nm = nm.split(b'\0')[0].decode('latin1').upper()
            if re.fullmatch(r'MAP\d\d|E\dM\d', nm): last_map = nm
            if nm == 'BEHAVIOR' and size > 0: out.append((last_map, d[pos:pos + size]))
        return out
    return [(path, d)]

def check(b):
    problems = []; notes = []
    if len(b) < 12 or b[:4] != b'ACS\0':
        return ['not an original-format ACS object (starts with %r)' % b[:4]], notes
    dirofs = struct.unpack('<i', b[4:8])[0]
    if dirofs < 8 or dirofs + 4 > len(b):
        return ['corrupt script directory offset %d' % dirofs], notes
    if dirofs >= 12 and b[dirofs - 4:dirofs] in (b'ACSe', b'ACSE'):
        return ['ZDoom EXTENDED format (ACSe): the engine refuses this lump. Recompile with acc -h'], notes
    nscripts = struct.unpack('<i', b[dirofs:dirofs + 4])[0]
    scripts = [struct.unpack('<iii', b[dirofs + 4 + k * 12:dirofs + 16 + k * 12]) for k in range(nscripts)]
    # layout: header | code | string text | directory | string offsets.  Code ends at the first string.
    t = dirofs + 4 + 12 * nscripts; code_limit = dirofs
    if t + 4 <= len(b):
        nstr = struct.unpack('<i', b[t:t + 4])[0]
        if 0 <= nstr <= 100000 and t + 4 + 4 * nstr <= len(b) and nstr:
            offs = struct.unpack('<%di' % nstr, b[t + 4:t + 4 + 4 * nstr])
            code_limit = min([dirofs] + [o for o in offs if 8 <= o < dirofs])
    starts = sorted(a for _, a, _ in scripts) + [code_limit]
    for num, addr, argc in scripts:
        real, typ = num % 1000, num // 1000
        if typ == 1: notes.append('script %d is an OPEN script (runs when the map starts)' % real)
        elif typ > 1: problems.append('script %d has ZDoom script type %d (death/enter/respawn/...): this engine treats every type >= 1 as OPEN and starts it at map load' % (real, typ))
        if argc > 3: problems.append('script %d takes %d arguments; ACS_Execute can pass at most 3' % (real, argc))
        end = min([s for s in starts if s > addr] or [code_limit]); p = addr
        while p < end:
            op = struct.unpack('<i', b[p:p + 4])[0]; p += 4
            if op < 0 or op >= SAFE_OPCODES:
                problems.append('script %d @%d: instruction %d (%s) does not exist in this engine: the game stops with an error when it runs' % (real, p - 4, op, NAMES.get(op, '?')))
                break
            n = OPERANDS.get(op, 0)
            if p + 4 * n > len(b): problems.append('script %d: truncated instruction at %d' % (real, p - 4)); break
            if op in VAR_OPS:
                v = struct.unpack('<i', b[p:p + 4])[0]
                if not (0 <= v < VAR_LIMITS[VAR_OPS[op]]):
                    problems.append('script %d @%d: %s variable index %d is outside 0..%d' % (real, p - 4, VAR_OPS[op], v, VAR_LIMITS[VAR_OPS[op]] - 1))
            p += 4 * n
    return problems, notes + ['%d script(s)' % nscripts]

def main(argv):
    if len(argv) != 2: print(__doc__); return 2
    lumps = read_lumps(argv[1]); bad = 0
    if not lumps: print('no BEHAVIOR lumps with content found'); return 0
    for label, b in lumps:
        problems, notes = check(b)
        print('%s: %s' % (label, 'OK' if not problems else 'PROBLEMS'), '(' + '; '.join(notes) + ')' if notes else '')
        for p in problems: print('   - ' + p)
        bad += bool(problems)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
