"""The demo table of the script lab (see make_script_lab.py).

One entry per ACS function of the manual, in the manual's order.  Every demo becomes one room and one script:

    script N (void)
    {
        Print(s:"<name>\\n", s:"<the call>");     // the name of the function and the call that follows
        Delay(52);
        <call>                                     // the demonstration
        Delay(<wait>);
        <reset>                                    // puts the room back so the switch can run it again
    }

Placeholders in the strings:  {t} sector tag / line id / polyobject number of the room (= the script number),
{S} tid of the map spot, {Dst} tid of the teleport destination, {M} {M2} tids of the monsters, {H} number of the
helper script, {fw} byte angle that points from the entrance to the far wall, {px} {py} centre of the polyobject.

Fields:   kit     the layout of the room (make_script_lab.KITS)
          call    the demonstration
          wait    tics to wait before the reset
          reset   undo
          helper  extra script text (placeholders as above)
          warn    the demo leaves the map: it only runs from the red switch
          again   the entry line fires every time (not only the first)
          look    what to see (shown in the documentation)
          expect  what the check (BEGOTTEN_DUMP) must see change: sec, thing, line, none (sound, message or the player)
"""

DEMOS = []

def D(grp, name, call, kit='none', wait=105, reset='', helper='', warn=False, again=False, look='', expect='sec',
      text=None, vars='', kopt=None, walk=False):
    DEMOS.append(dict(grp=grp, name=name, call=call, kit=kit, wait=wait, reset=reset, helper=helper, warn=warn,
                      again=again, look=look, expect=expect, text=text, vars=vars, kopt=kopt or {}, walk=walk))

# ---------------------------------------------------------------------------------------------------------- built-in
B = 'Built-in functions'
D(B, 'AmbientSound', 'AmbientSound("dsdoropn", 127);', wait=35, expect='none', look='A door sound plays everywhere.')
D(B, 'ChangeCeiling', 'ChangeCeiling({t}, "CEIL2");', 'ceil', reset='ChangeCeiling({t}, "CEIL1");',
  look='The ceiling flat of the block changes.')
D(B, 'ChangeFloor', 'ChangeFloor({t}, "LAVA1");', 'floor', reset='ChangeFloor({t}, "FLOOR2");',
  look='The floor flat of the block changes.')
D(B, 'ClearLineSpecial', 'Print(s:"This line trigger has cleared itself.\\nLeave and come back: nothing happens.");\n    ClearLineSpecial();',
  again=True, expect='none', look='The line you crossed to enter runs this script once, then removes its own special.',
  text='ClearLineSpecial()', walk=True)
D(B, 'Delay', 'Print(s:"Get ready...");\n    Delay(105);\n    Print(s:"Go!");', wait=35, expect='none', text='Delay(105)',
  look='Three seconds between the two messages.')
D(B, 'GameSkill', 'Print(s:"Skill level ", d:GameSkill());\n    if (GameSkill() >= 3)\n    {\n        Thing_Spawn({S}, 4, 0, {M});\n    }',
  'spot', expect='thing', look='Prints the skill. A pistol trooper appears on skills 3 and up.', text='GameSkill()')
D(B, 'GameType', 'if (GameType() == GAME_SINGLE_PLAYER)\n        Print(s:"Playing alone");\n    else\n        Print(s:"Playing with others");',
  expect='none', text='GameType()', look='Tells single player from co-op or deathmatch.')
D(B, 'LineSide', 'if (LineSide() == SIDE_FRONT)\n        Print(s:"The line was crossed from its front");\n    else\n        Print(s:"The line was crossed from its back");',
  expect='none', text='LineSide()', look='Which side of the line started the script.', walk=True)
D(B, 'PlayerCount', 'Print(s:"Players in the game: ", d:PlayerCount());', expect='none', text='PlayerCount()',
  look='Prints how many players are in the game.')
D(B, 'PolyWait', 'Polyobj_MoveTimes8({t}, 16, {up}, 8);\n    PolyWait({t});\n    Print(s:"The polyobject has arrived");',
  'poly', wait=70, reset='Polyobj_MoveTimes8({t}, 32, {down}, 8);', expect='thing',
  text='PolyWait({t})', look='The script waits until the moving block has stopped.')
D(B, 'Random', 'roll = Random(1, 6);\n    Print(s:"Dice roll: ", d:roll);\n    if (roll == 6)\n        Print(s:"Lucky!");',
  expect='none', text='Random(1, 6)', look='A different number every time.', vars='int roll;')
D(B, 'ScriptWait', 'ACS_Execute({H}, 0, 0, 0, 0);\n    ScriptWait({H});\n    Print(s:"The helper script has finished");',
  helper='script {H} (void)\n{\n    Print(s:"Helper script: two seconds...");\n    Delay(70);\n}', expect='none',
  wait=35, text='ScriptWait({H})', look='The main script waits for the helper.')
D(B, 'SectorSound', 'SectorSound("dsdoropn", 127);', wait=35, expect='none', look='A door sound from the sector of the line.')
D(B, 'SetLineBlocking', 'SetLineBlocking({t}, BLOCK_NOTHING);\n    Print(s:"The invisible wall is gone: take the potion");',
  'cage', reset='SetLineBlocking({t}, BLOCK_EVERYTHING);', expect='line',
  look='The potion sits behind an invisible wall; after the call you can walk to it.')
D(B, 'SetLineSpecial', 'SetLineSpecial({t}, 112, {t}, 255, 0, 0, 0);\n    Print(s:"Now press the dead switch on the wall");',
  'stage', wait=70, reset='SetLineSpecial({t}, 0, 0, 0, 0, 0, 0);\n    Light_ChangeToValue({t}, 128);', expect='line',
  text='SetLineSpecial({t}, 112, ...)', look='The switch on the wall does nothing until the script gives it a special (a light).')
D(B, 'SetLineTexture', 'SetLineTexture({t}, SIDE_FRONT, TEXTURE_MIDDLE, "WALL05");', 'wallseg', wait=105,
  reset='SetLineTexture({t}, SIDE_FRONT, TEXTURE_MIDDLE, "GREY1");', expect='line',
  look='The marked piece of the far wall changes its texture.')
D(B, 'SoundSequence', 'SoundSequence("Door");', wait=70, expect='none', look='The sound sequence of a door plays.')
D(B, 'TagWait', 'Door_Open({t}, 32);\n    TagWait({t});\n    Print(s:"The door has finished opening");', 'door_c',
  wait=70, reset='Door_Close({t}, 64);', look='The message comes when the door is all the way open.')
D(B, 'ThingCount', 'Thing_Spawn({S}, 23, 0, {M});\n    Delay(5);\n    Print(s:"Things with that tag: ", d:ThingCount(0, {M}));',
  'spot', expect='thing', wait=70, reset='Thing_Remove({M});', look='Counts the potion that was just made.')
D(B, 'ThingSound', 'ThingSound({M}, "dsdoropn", 127);', 'mon', wait=35, expect='none', look='The sound comes from the monster.')
D(B, 'Timer', 'start = Timer();\n    Delay(35);\n    Print(s:"One second took ", d:Timer() - start, s:" tics");',
  expect='none', wait=70, vars='int start;', text='Timer()', look='About 35.')
D(B, 'Print / PrintBold', 'Print(s:"Monsters killed: ", d:12, s:" of ", d:20);\n    Delay(105);\n    PrintBold(s:"PrintBold looks like this");',
  expect='none', wait=70, text='Print(...) and PrintBold(...)', look='A normal message, then a bold one.')

# ---------------------------------------------------------------------------------------------------------- doors
G = 'Doors'
D(G, 'Door_Close', 'Door_Close({t}, 32);', 'door_o', reset='Door_Open({t}, 32);', look='The open block closes.')
D(G, 'Door_Open', 'Door_Open({t}, 32);', 'door_c', reset='Door_Close({t}, 32);', look='The shut block opens and stays open.')
D(G, 'Door_Raise', 'Door_Raise({t}, 32, 105);', 'door_c', wait=210, look='Opens, waits three seconds, shuts.')
D(G, 'Door_LockedRaise', 'Door_LockedRaise({t}, 32, 105, 1);\n    Print(s:"Locked: fetch the blue key, then press the switch");',
  'door_k', wait=70, expect='none', look='Stays shut until the player holds the blue key; then the switch opens it.', walk=True)
D(G, 'Door_WaitRaise', 'Door_WaitRaise({t}, 32, 70, 70);', 'door_c', wait=245, reset='Door_Close({t}, 32);',
  look='Waits, then opens and closes again.')
D(G, 'Door_WaitClose', 'Door_WaitClose({t}, 32, 70);', 'door_o', wait=210, reset='Door_Open({t}, 32);', look='Waits, then closes.')
D(G, 'Door_CloseWaitOpen', 'Door_CloseWaitOpen({t}, 32, 105);', 'door_o', wait=245, look='Closes, waits, opens.')

# ---------------------------------------------------------------------------------------------------------- floors
F = 'Floors'
FR = 'Floor_MoveToValue({t}, 64, 32, 0);'
D(F, 'Floor_LowerByValue', 'Floor_LowerByValue({t}, 16, 24);', 'floor', reset=FR, look='Down by 24 units.')
D(F, 'Floor_LowerToLowest', 'Floor_LowerToLowest({t}, 16);', 'floor', reset=FR, look='Down to the lowest neighbour floor (the dark pit).')
D(F, 'Floor_LowerToNearest', 'Floor_LowerToNearest({t}, 16);', 'floor', reset=FR, look='Down to the next lower neighbour floor.')
D(F, 'Floor_RaiseByValue', 'Floor_RaiseByValue({t}, 16, 32);', 'floor', reset=FR, look='Up by 32 units.')
D(F, 'Floor_RaiseToHighest', 'Floor_RaiseToHighest({t}, 16);', 'floor', reset=FR, look='Up to the highest neighbour floor.')
D(F, 'Floor_RaiseToNearest', 'Floor_RaiseToNearest({t}, 16);', 'floor', reset=FR, look='Up to the next higher neighbour floor.')
D(F, 'Floor_RaiseAndCrush', 'Floor_RaiseAndCrush({t}, 8, 10);', 'floor_v', wait=245, reset=FR,
  look='The floor rises and crushes the trooper against the ceiling.')
D(F, 'Floor_RaiseByValueTimes8', 'Floor_RaiseByValueTimes8({t}, 16, 4);', 'floor', reset=FR, look='4 times 8 = 32 units up.')
D(F, 'Floor_LowerByValueTimes8', 'Floor_LowerByValueTimes8({t}, 16, 3);', 'floor', reset=FR, look='3 times 8 = 24 units down.')
D(F, 'Floor_MoveToValue', 'Floor_MoveToValue({t}, 16, 64, 0);', 'floor', reset=FR, look='Goes to height 64.')
D(F, 'Floor_CrushStop', 'Floor_RaiseAndCrush({t}, 8, 10);\n    Delay(35);\n    Floor_CrushStop({t});', 'floor_v', wait=70,
  reset=FR, look='The crushing floor is stopped half way.')
D(F, 'Floor_LowerInstant', 'Floor_LowerInstant({t}, 0, 3);', 'floor', wait=70, reset=FR, look='Down by 3 times 8 = 24, at once.')
D(F, 'Floor_RaiseInstant', 'Floor_RaiseInstant({t}, 0, 4);', 'floor', wait=70, reset=FR, look='Up by 4 times 8 = 32, at once.')
D(F, 'Floor_MoveToValueTimes8', 'Floor_MoveToValueTimes8({t}, 16, 8, 0);', 'floor', reset=FR, look='Goes to height 8 times 8 = 64.')
D(F, 'Floor_RaiseAndCrushDoom', 'Floor_RaiseAndCrushDoom({t}, 8, 10);', 'floor_v', wait=245, reset=FR, look='Like Floor_RaiseAndCrush, Doom style.')
D(F, 'Floor_Waggle', 'Floor_Waggle({t}, 128, 60, 0, 5);', 'floor', wait=245, reset=FR, look='The floor wobbles for five seconds.')
D(F, 'Floor_TransferTrigger', 'Floor_TransferTrigger({t});', 'floor', wait=105, reset='ChangeFloor({t}, "FLOOR2");',
  look='The floor takes the flat of the sector next to the line that started the script.', walk=True)
D(F, 'Floor_TransferNumeric', 'Floor_TransferNumeric({t});', 'floor', wait=105, reset='ChangeFloor({t}, "FLOOR2");', expect='none',
  look='The floor takes the flat of a neighbouring sector picked by the engine\'s numeric model; here the neighbours share the flat, so nothing visibly changes.', walk=True)
D(F, 'Floor_RaiseToLowestCeiling', 'Floor_RaiseToLowestCeiling({t}, 16);', 'floor', reset=FR, look='Up to the lowest ceiling around.')
D(F, 'Floor_RaiseByValueTxTy', 'Floor_RaiseByValueTxTy({t}, 16, 24);', 'floor', reset=FR + '\n    ChangeFloor({t}, "FLOOR2");',
  look='Up by 24 and takes over the texture of a neighbour.')
D(F, 'Floor_RaiseByTexture', 'Floor_RaiseByTexture({t}, 16);', 'floor', reset=FR, look='Up by the height of the shortest lower texture.')
D(F, 'Floor_LowerToLowestTxTy', 'Floor_LowerToLowestTxTy({t}, 16);', 'floor', reset=FR + '\n    ChangeFloor({t}, "FLOOR2");',
  look='Down to the lowest neighbour and takes over its texture.')
D(F, 'Floor_LowerToHighest', 'Floor_LowerToHighest({t}, 16, 0);', 'floor', reset=FR, look='Down to the highest neighbour floor that is lower.')
D(F, 'Floor_Donut', 'Floor_Donut({t}, 16, 16);', 'donut', wait=210, reset='Floor_MoveToValue({t}, 64, 48, 0);\n    Floor_MoveToValue({ring}, 64, -24, 0);',
  look='The pillar sinks and the ring around it rises.')

# ---------------------------------------------------------------------------------------------------------- ceilings
C = 'Ceilings'
CR = 'Ceiling_MoveToValue({t}, 64, 112, 0);'
D(C, 'Ceiling_Waggle', 'Ceiling_Waggle({t}, 128, 60, 0, 5);', 'ceil', wait=245, reset=CR, look='The ceiling wobbles for five seconds.')
D(C, 'Ceiling_LowerByValue', 'Ceiling_LowerByValue({t}, 16, 32);', 'ceil', reset=CR, look='Down by 32.')
D(C, 'Ceiling_RaiseByValue', 'Ceiling_RaiseByValue({t}, 16, 32);', 'ceil', reset=CR, look='Up by 32.')
D(C, 'Ceiling_CrushAndRaise', 'Ceiling_CrushAndRaise({t}, 16, 10);', 'ceil_v', wait=175, reset='Ceiling_CrushStop({t});\n    ' + CR,
  look='The ceiling crushes the trooper, rises, and goes on.')
D(C, 'Ceiling_LowerAndCrush', 'Ceiling_LowerAndCrush({t}, 16, 10);', 'ceil_v', wait=175, reset='Ceiling_CrushStop({t});\n    ' + CR,
  look='The ceiling lowers and crushes.')
D(C, 'Ceiling_CrushStop', 'Ceiling_CrushAndRaise({t}, 8, 10);\n    Delay(35);\n    Ceiling_CrushStop({t});', 'ceil_v', wait=70,
  reset=CR, look='The crusher is stopped half way.')
D(C, 'Ceiling_CrushRaiseAndStay', 'Ceiling_CrushRaiseAndStay({t}, 16, 10);', 'ceil_v', wait=175, reset='Ceiling_CrushStop({t});\n    ' + CR,
  look='Crushes once, rises, stays up.')
D(C, 'Ceiling_MoveToValue', 'Ceiling_MoveToValue({t}, 16, 140, 0);', 'ceil', reset=CR, look='Goes to height 140.')
D(C, 'Ceiling_MoveToValueTimes8', 'Ceiling_MoveToValueTimes8({t}, 16, 10, 0);', 'ceil', reset=CR, look='Goes to height 10 times 8 = 80.')
D(C, 'Ceiling_LowerAndCrushDist', 'Ceiling_LowerAndCrushDist({t}, 16, 10, 8, 0);', 'ceil_v', wait=175, reset='Ceiling_CrushStop({t});\n    ' + CR,
  look='Lowers and crushes, leaving a gap of 8.')
D(C, 'Ceiling_CrushAndRaiseSilentDist', 'Ceiling_CrushAndRaiseSilentDist({t}, 8, 16, 10, 0);', 'ceil_v', wait=175,
  reset='Ceiling_CrushStop({t});\n    ' + CR, look='A silent crusher with a gap of 8.')
D(C, 'Ceiling_CrushAndRaiseDist', 'Ceiling_CrushAndRaiseDist({t}, 8, 16, 10, 0);', 'ceil_v', wait=175,
  reset='Ceiling_CrushStop({t});\n    ' + CR, look='A crusher with a gap of 8.')
D(C, 'Ceiling_LowerToHighestFloor', 'Ceiling_LowerToHighestFloor({t}, 16);', 'ceil', reset=CR, look='Down to the highest floor around.')
D(C, 'Ceiling_LowerInstant', 'Ceiling_LowerInstant({t}, 0, 4);', 'ceil', wait=70, reset=CR, look='Down by 4 times 8 = 32, at once.')
D(C, 'Ceiling_RaiseInstant', 'Ceiling_RaiseInstant({t}, 0, 4);', 'ceil', wait=70, reset=CR, look='Up by 32, at once.')
D(C, 'Ceiling_CrushRaiseAndStayA', 'Ceiling_CrushRaiseAndStayA({t}, 16, 8, 10, 0);', 'ceil_v', wait=175,
  reset='Ceiling_CrushStop({t});\n    ' + CR, look='Fast down, slow up, stays up.')
D(C, 'Ceiling_CrushAndRaiseA', 'Ceiling_CrushAndRaiseA({t}, 16, 8, 10, 0);', 'ceil_v', wait=175,
  reset='Ceiling_CrushStop({t});\n    ' + CR, look='Fast down, slow up, repeats.')
D(C, 'Ceiling_CrushAndRaiseSilentA', 'Ceiling_CrushAndRaiseSilentA({t}, 16, 8, 10, 0);', 'ceil_v', wait=175,
  reset='Ceiling_CrushStop({t});\n    ' + CR, look='The same, without sound.')
D(C, 'Ceiling_RaiseByValueTimes8', 'Ceiling_RaiseByValueTimes8({t}, 16, 4);', 'ceil', reset=CR, look='Up by 32.')
D(C, 'Ceiling_LowerByValueTimes8', 'Ceiling_LowerByValueTimes8({t}, 16, 4);', 'ceil', reset=CR, look='Down by 32.')
D(C, 'Ceiling_RaiseToNearest', 'Ceiling_RaiseToNearest({t}, 16);', 'ceil', reset=CR, look='Up to the next higher ceiling around.')
D(C, 'Ceiling_LowerToLowest', 'Ceiling_LowerToLowest({t}, 16);', 'ceil', reset=CR, look='Down to the lowest ceiling around.')
D(C, 'Ceiling_LowerToFloor', 'Ceiling_LowerToFloor({t}, 16);', 'ceil', reset=CR, look='All the way down to the floor.')
D(C, 'Ceiling_CrushRaiseAndStaySilA', 'Ceiling_CrushRaiseAndStaySilA({t}, 16, 8, 10, 0);', 'ceil_v', wait=175,
  reset='Ceiling_CrushStop({t});\n    ' + CR, look='Silent: crushes once and stays up.')

# ---------------------------------------------------------------------------------------------------------- floor and ceiling
FC = 'Floor and ceiling together'
FCR = 'Floor_MoveToValue({t}, 64, 32, 0);\n    Ceiling_MoveToValue({t}, 64, 104, 0);'
D(FC, 'FloorAndCeiling_LowerByValue', 'FloorAndCeiling_LowerByValue({t}, 16, 24);', 'fc', reset=FCR, look='Both go down by 24.')
D(FC, 'FloorAndCeiling_RaiseByValue', 'FloorAndCeiling_RaiseByValue({t}, 16, 24);', 'fc', reset=FCR, look='Both go up by 24.')
D(FC, 'FloorAndCeiling_LowerRaise', 'FloorAndCeiling_LowerRaise({t}, 16, 16);', 'fc', reset=FCR,
  look='The floor sinks to the lowest neighbour while the ceiling rises to the highest.')

# ---------------------------------------------------------------------------------------------------------- platforms
P = 'Platforms and lifts'
PR = 'Plat_Stop({t});\n    Floor_MoveToValue({t}, 64, 40, 0);'
D(P, 'Plat_PerpetualRaise', 'Plat_PerpetualRaise({t}, 16, 105);', 'lift', wait=420, reset=PR, look='Moves up and down between the neighbours, for ever.')
D(P, 'Plat_Stop', 'Plat_PerpetualRaise({t}, 16, 105);\n    Delay(105);\n    Plat_Stop({t});', 'lift', wait=70, reset=PR,
  look='The moving platform is stopped.')
D(P, 'Plat_DownWaitUpStay', 'Plat_DownWaitUpStay({t}, 32, 105);', 'lift', wait=280, reset=PR, look='The classic lift: down, wait, up.')
D(P, 'Plat_DownByValue', 'Plat_DownByValue({t}, 32, 105, 8);', 'lift', wait=280, reset=PR, look='Down by 8 times 8 = 64, wait, up.')
D(P, 'Plat_UpWaitDownStay', 'Plat_UpWaitDownStay({t}, 32, 105);', 'lift', wait=280, reset=PR, look='Up, wait, down.')
D(P, 'Plat_UpByValue', 'Plat_UpByValue({t}, 32, 105, 8);', 'lift', wait=280, reset=PR, look='Up by 64, wait, down.')
D(P, 'Plat_UpNearestWaitDownStay', 'Plat_UpNearestWaitDownStay({t}, 32, 105);', 'lift', wait=280, reset=PR, look='Up to the next higher floor, wait, down.')
D(P, 'Plat_DownWaitUpStayLip', 'Plat_DownWaitUpStayLip({t}, 32, 105, 8, 0);', 'lift', wait=280, reset=PR, look='Down, but 8 units short of the pit floor.')
D(P, 'Plat_PerpetualRaiseLip', 'Plat_PerpetualRaiseLip({t}, 16, 105, 8);', 'lift', wait=420, reset=PR, look='Up and down for ever, 8 units short of the ends.')
D(P, 'Plat_RaiseAndStayTx0', 'Plat_RaiseAndStayTx0({t}, 16, 0);', 'lift', wait=175, reset=PR + '\n    ChangeFloor({t}, "FLOOR2");',
  look='Rises to the next floor and takes over its texture.')
D(P, 'Plat_UpByValueStayTx', 'Plat_UpByValueStayTx({t}, 16, 6);', 'lift', wait=175, reset=PR + '\n    ChangeFloor({t}, "FLOOR2");',
  look='Rises by 6 times 8 = 48 and takes over a texture.')
D(P, 'Plat_ToggleCeiling', 'Ceiling_CrushAndRaise({t}, 8, 10);\n    Delay(70);\n    Plat_ToggleCeiling({t});\n    Delay(105);\n    Plat_ToggleCeiling({t});',
  'ceil_v', wait=105, reset='Ceiling_CrushStop({t});\n    ' + CR,
  look='A crusher is paused, then set going again.')

# ---------------------------------------------------------------------------------------------------------- elevators
E = 'Elevators'
D(E, 'Elevator_RaiseToNearest', 'Elevator_RaiseToNearest({t}, 16);', 'fc', wait=105, reset=FCR, look='Floor and ceiling rise together to the next floor.')
D(E, 'Elevator_MoveToFloor', 'Elevator_MoveToFloor({t}, 16);', 'fc', wait=105, reset=FCR,
  look='Goes to the floor height of the sector of the line that started the script.', walk=True)
D(E, 'Elevator_LowerToNearest', 'Elevator_LowerToNearest({t}, 16);', 'fc', wait=105, reset=FCR, look='Floor and ceiling sink to the next floor.')

# ---------------------------------------------------------------------------------------------------------- stairs and pillars
ST = 'Stairs and pillars'
SR = ('Floor_MoveToValue({t}, 64, 0, 0);\n    Floor_MoveToValue({t1}, 64, 0, 0);\n    Floor_MoveToValue({t2}, 64, 0, 0);\n'
      '    Floor_MoveToValue({t3}, 64, 0, 0);\n    Floor_MoveToValue({t4}, 64, 0, 0);\n    Floor_MoveToValue({t5}, 64, 0, 0);')
SRH = ('Floor_MoveToValue({t}, 64, 96, 0);\n    Floor_MoveToValue({t1}, 64, 96, 0);\n    Floor_MoveToValue({t2}, 64, 96, 0);\n'
       '    Floor_MoveToValue({t3}, 64, 96, 0);\n    Floor_MoveToValue({t4}, 64, 96, 0);\n    Floor_MoveToValue({t5}, 64, 96, 0);')
D(ST, 'Stairs_BuildDown', 'Stairs_BuildDown({t}, 4, 16, 0, 0);', 'stairs_hi', wait=175, reset=SRH, look='A staircase sinks away from the first step.')
D(ST, 'Stairs_BuildUp', 'Stairs_BuildUp({t}, 4, 16, 0, 0);', 'stairs', wait=175, reset=SR, look='A staircase rises, 16 units a step.')
D(ST, 'Pillar_Build', 'Pillar_Build({t}, 16, 64);', 'pillar', wait=105, reset='Floor_MoveToValue({t}, 64, 0, 0);', look='The block rises from the floor.')
D(ST, 'Pillar_Open', 'Pillar_Open({t}, 16, 32, 32);', 'pillar_c', wait=105, reset='Floor_MoveToValue({t}, 64, 56, 0);\n    Ceiling_MoveToValue({t}, 64, 56, 0);',
  look='A closed pillar opens: floor down, ceiling up.')
D(ST, 'Stairs_BuildDownSync', 'Stairs_BuildDownSync({t}, 4, 16, 0);', 'stairs_hi', wait=175, reset=SRH, look='All steps move at once.')
D(ST, 'Stairs_BuildUpSync', 'Stairs_BuildUpSync({t}, 4, 16, 0);', 'stairs', wait=175, reset=SR, look='All steps rise at once.')
D(ST, 'Pillar_BuildAndCrush', 'Pillar_BuildAndCrush({t}, 16, 48, 10);', 'pillar_v', wait=175, reset='Floor_MoveToValue({t}, 64, 0, 0);',
  look='The block rises and crushes the trooper.')
D(ST, 'Stairs_BuildUpDoom', 'Stairs_BuildUpDoom({t}, 4, 16, 0, 0);', 'stairs', wait=175, reset=SR, look='Doom-style stairs.')

# ---------------------------------------------------------------------------------------------------------- lights
L = 'Lights'
LR = 'Light_ChangeToValue({t}, 128);'
D(L, 'Light_RaiseByValue', 'Light_RaiseByValue({t}, 64);', 'stage', wait=70, reset=LR, look='The stage gets 64 brighter.')
D(L, 'Light_LowerByValue', 'Light_LowerByValue({t}, 64);', 'stage', wait=70, reset=LR, look='The stage gets 64 darker.')
D(L, 'Light_ChangeToValue', 'Light_ChangeToValue({t}, 255);', 'stage', wait=70, reset=LR, look='Full brightness.')
D(L, 'Light_Fade', 'Light_Fade({t}, 32, 70);', 'stage', wait=105, reset=LR, look='Fades down over two seconds.')
D(L, 'Light_Glow', 'Light_Glow({t}, 255, 64, 35);', 'stage', wait=210, reset='Light_Stop({t});\n    ' + LR, look='Pulses between bright and dim.')
D(L, 'Light_Flicker', 'Light_Flicker({t}, 200, 40);', 'stage', wait=210, reset='Light_Stop({t});\n    ' + LR, look='Flickers at random.')
D(L, 'Light_Strobe', 'Light_Strobe({t}, 255, 0, 10, 20);', 'stage', wait=210, reset='Light_Stop({t});\n    ' + LR, look='Flashes on and off.')
D(L, 'Light_Stop', 'Light_Glow({t}, 255, 64, 35);\n    Delay(105);\n    Light_Stop({t});', 'stage', wait=70, reset=LR, look='The glowing light is stopped where it is.')
D(L, 'Light_StrobeDoom', 'Light_StrobeDoom({t}, 10, 20);', 'stage', wait=210, reset='Light_Stop({t});\n    ' + LR, look='Doom-style strobe.')
D(L, 'Light_MinNeighbor', 'Light_MinNeighbor({t});', 'stage', wait=70, reset=LR, look='Takes the light of the darkest neighbour (the dark side).')
D(L, 'Light_MaxNeighbor', 'Light_MaxNeighbor({t});', 'stage', wait=70, reset=LR, look='Takes the light of the brightest neighbour (the bright side).')

# ---------------------------------------------------------------------------------------------------------- sectors
SE = 'Sectors'
D(SE, 'ForceField', 'ForceField();\n    Print(s:"The force field heals you and pushes you back");', expect='none', text='ForceField()',
  look='Gives the player health and thrusts him back.', walk=True)
D(SE, 'ClearForceField', 'ClearForceField({t});', 'cage_w', wait=35, expect='line', look='The wall round the potion disappears (once: reload the map to see it again).')
D(SE, 'Sector_ChangeFlags', 'Floor_LowerByValue({t}, 16, 24);\n    TagWait({t});\n    Sector_ChangeFlags({t}, 1, 0);\n    Floor_RaiseByValue({t}, 16, 24);',
  'floor', wait=35, reset='Sector_ChangeFlags({t}, 0, 1);', look='The first move makes a sound, the second (silent flag set) does not.')
D(SE, 'Sector_SetCeilingScale2', 'Sector_SetCeilingScale2({t}, 2.0, 2.0);', 'flat', wait=105, reset='Sector_SetCeilingScale2({t}, 1.0, 1.0);', look='The ceiling flat is twice as big.')
D(SE, 'Sector_SetFloorScale2', 'Sector_SetFloorScale2({t}, 2.0, 2.0);', 'flat', wait=105, reset='Sector_SetFloorScale2({t}, 1.0, 1.0);', look='The floor flat is twice as big.')
D(SE, 'Sector_SetRotation', 'Sector_SetRotation({t}, 45, 45);', 'flat', wait=105, reset='Sector_SetRotation({t}, 0, 0);', look='Floor and ceiling flats turn by 45 degrees.')
D(SE, 'Sector_SetCeilingPanning', 'Sector_SetCeilingPanning({t}, 16, 0, 16, 0);', 'flat', wait=105, reset='Sector_SetCeilingPanning({t}, 0, 0, 0, 0);', look='The ceiling flat slides.')
D(SE, 'Sector_SetFloorPanning', 'Sector_SetFloorPanning({t}, 16, 0, 16, 0);', 'flat', wait=105, reset='Sector_SetFloorPanning({t}, 0, 0, 0, 0);', look='The floor flat slides.')
D(SE, 'Sector_SetCeilingScale', 'Sector_SetCeilingScale({t}, 2, 0, 2, 0);', 'flat', wait=105, reset='Sector_SetCeilingScale({t}, 1, 0, 1, 0);', look='The ceiling flat is twice as big.')
D(SE, 'Sector_SetFloorScale', 'Sector_SetFloorScale({t}, 2, 0, 2, 0);', 'flat', wait=105, reset='Sector_SetFloorScale({t}, 1, 0, 1, 0);', look='The floor flat is twice as big.')
D(SE, 'Sector_SetDamage', 'Sector_SetDamage({t}, 10, 0, 32);\n    Print(s:"The slab now hurts: step on it");', 'hazard', wait=140,
  reset='Sector_SetDamage({t}, 0, 0, 0);', expect='sec', look='A harmless floor starts to hurt.')
D(SE, 'Sector_SetGravity', 'Sector_SetGravity({t}, 0, 20);\n    Print(s:"Low gravity on the slab: jump on it");', 'hazard', wait=140,
  reset='Sector_SetGravity({t}, 1, 0);', expect='sec', look='Gravity on the slab drops to a fifth.')

# ---------------------------------------------------------------------------------------------------------- lines and walls
LN = 'Lines and walls'
D(LN, 'Scroll_Wall', 'Scroll_Wall({t}, 1, 0, 0, 2);', 'wallseg', wait=175, reset='Scroll_Wall({t}, 0, 0, 0, 2);', expect='line',
  look='The marked piece of the far wall scrolls sideways.')
D(LN, 'Line_SetTextureOffset', 'Line_SetTextureOffset({t}, 32.0, 16.0, 0, 3);', 'wallseg', wait=105, reset='Line_SetTextureOffset({t}, 0.0, 0.0, 0, 3);', expect='line',
  look='The marked wall piece shifts its texture.')
D(LN, 'Line_SetBlocking', 'Line_SetBlocking({t}, 0, 31);\n    Print(s:"The invisible wall is gone");', 'cage', wait=105,
  reset='Line_SetBlocking({t}, 31, 0);', expect='line', look='Clears the blocking flags of the invisible wall round the potion.')
D(LN, 'Line_SetTextureScale', 'Line_SetTextureScale({t}, 2.0, 2.0, 0, 3);', 'wallseg', wait=105, reset='Line_SetTextureScale({t}, 1.0, 1.0, 0, 3);', expect='line',
  look='The marked wall piece scales its texture.')

# ---------------------------------------------------------------------------------------------------------- things
T = 'Things'
D(T, 'Thing_Raise', 'Thing_Spawn({S}, 4, 0, {M});\n    Delay(10);\n    Thing_Damage({M}, 500);\n    Delay(70);\n    Thing_Raise({M});',
  'spot', expect='thing', wait=140, reset='Thing_Remove({M});', look='A trooper is made, killed, and brought back to life.')
D(T, 'Thing_Stop', 'ThrustThingZ({M}, 30, 1, 0);\n    Delay(8);\n    Thing_Stop({M});', 'mon', expect='thing', wait=105,
  look='The trooper is thrown up and stopped in mid-air.')
D(T, 'ThrustThing', 'ThrustThing(64, 12);', expect='none', text='ThrustThing(64, 12)', look='The player is pushed to the side.', walk=True)
D(T, 'DamageThing', 'DamageThing(10);', expect='none', text='DamageThing(10)', look='The player loses 10 health.', walk=True)
D(T, 'Thing_Damage', 'Thing_Damage({M}, 50);', 'mon', expect='thing', wait=70, reset='Thing_Remove({M});', look='The trooper is hurt for 50.')
D(T, 'Radius_Quake', 'Radius_Quake(3, 70, 0, 8, {S});', 'spot', expect='none', wait=105, look='The ground shakes round the spot.')
D(T, 'Thing_Move', 'Thing_Move({M}, {S}, 1);', 'mon_spot', expect='thing', wait=105, reset='Thing_Remove({M});', look='The trooper jumps to the map spot.')
D(T, 'Thing_SetSpecial', 'Thing_SetSpecial({M}, 11, {t}, 32, 0);\n    Delay(10);\n    Thing_Damage({M}, 500);', 'mon_door', expect='sec',
  wait=140, reset='Door_Close({t}, 32);', look='The trooper is given the special "open the door". When it dies the door opens.')
D(T, 'ThrustThingZ', 'ThrustThingZ({M}, 40, 1, 0);', 'mon', expect='thing', wait=105, look='The trooper is thrown up.')
D(T, 'Thing_Activate', 'Thing_Activate({M});', 'mon_dormant', expect='thing', wait=105, look='The sleeping trooper wakes up.')
D(T, 'Thing_Deactivate', 'Thing_Deactivate({M});', 'mon', expect='thing', wait=105, look='The trooper is put to sleep.')
D(T, 'Thing_Remove', 'Thing_Remove({M});', 'mon', expect='thing', wait=70, look='The trooper vanishes.')
D(T, 'Thing_Destroy', 'Thing_Destroy({M}, 0);', 'mon', expect='thing', wait=70, look='The trooper is killed.')
D(T, 'Thing_Projectile', 'Thing_Projectile({S}, 10, {fwb}, 16, 0);', 'spot', expect='none', wait=105, look='A fireball flies from the spot to the far wall.')
D(T, 'Thing_Spawn', 'Thing_Spawn({S}, 4, 0, {M});', 'spot', expect='thing', wait=70, reset='Thing_Remove({M});', look='A trooper appears in a puff of fog.')
D(T, 'Thing_ProjectileGravity', 'Thing_ProjectileGravity({S}, 10, {fwb}, 16, 16);', 'spot', expect='none', wait=105, look='A fireball flies in an arc.')
D(T, 'Thing_SpawnNoFog', 'Thing_SpawnNoFog({S}, 5, 0, {M});', 'spot', expect='thing', wait=70, reset='Thing_Remove({M});', look='A fire elemental appears with no fog.')
D(T, 'Thing_SpawnFacing', 'Thing_SpawnFacing({S}, 5, 1, {M});', 'spot', expect='thing', wait=70, reset='Thing_Remove({M});', look='A fire elemental appears, turned the way the spot faces.')
D(T, 'NoiseAlert', 'NoiseAlert(0, 0);', 'sleepers', expect='thing', wait=70, text='NoiseAlert(0, 0)', look='The two sleeping troopers wake up.')
D(T, 'Thing_ProjectileIntercept', 'Thing_ProjectileIntercept({S}, 10, 16, {M}, 0);', 'mon_spot', expect='none', wait=105, look='A fireball aimed at where the target will be.')
D(T, 'Thing_ChangeTID', 'Thing_ChangeTID({M}, {M2});\n    Print(s:"Old tag: ", d:ThingCount(0, {M}), s:" thing, new tag: ", d:ThingCount(0, {M2}));',
  'mon', expect='thing', wait=70, reset='Thing_ChangeTID({M2}, {M});', look='The trooper gets a new tag.')
D(T, 'Thing_Hate', 'Thing_Hate({M}, {M2}, 0);', 'duo', expect='thing', wait=210, look='Two fire elementals start fighting each other.')
D(T, 'Thing_ProjectileAimed', 'Thing_ProjectileAimed({S}, 10, 16, {M}, 0);', 'mon_spot', expect='none', wait=105, look='A fireball aimed at the target.')
D(T, 'HealThing', 'HealThing(25);', expect='none', text='HealThing(25)', look='The player gains 25 health.', walk=True)
# ---------------------------------------------------------------------------------------------------------- teleporting
TP = 'Teleporting'
D(TP, 'Teleport_ZombieChanger', 'Teleport_ZombieChanger({Dst}, {t});', 'tele', expect='none', wait=70, look='Needs a dead monster in the sector (Doom special 105); the call runs without effect on the living.', walk=True)
D(TP, 'Teleport', 'Teleport({Dst});', 'tele', expect='none', wait=70, again=True, look='The player is teleported to the far side of the room.', walk=True)
D(TP, 'Teleport_NoFog', 'Teleport_NoFog({Dst}, 1);', 'tele', expect='none', wait=70, again=True, look='The same, with no fog and no sound.', walk=True)
D(TP, 'Teleport_NewMap', 'Teleport_NewMap(2, 0);', 'none', warn=True, expect='none', look='Leaves for map 2. Press the red switch.')
D(TP, 'Teleport_EndGame', 'Teleport_EndGame();', 'none', warn=True, expect='none', look='Ends the game. Press the red switch.')
D(TP, 'TeleportOther', 'TeleportOther({M}, {S}, 1);', 'mon_spot', expect='thing', wait=105, reset='Thing_Remove({M});', look='The trooper is teleported to the map spot.')
D(TP, 'TeleportGroup', 'TeleportGroup({M}, {S}, {Dst}, 1, 1);', 'group', expect='thing', wait=105, look='Three barrels move together from the spot to the destination.')
D(TP, 'TeleportInSector', 'TeleportInSector({t}, {S}, {Dst}, 1, 0);', 'sectormon', expect='thing', wait=105, look='Everything standing on the slab is teleported.')
D(TP, 'Teleport_NoStop', 'Teleport_NoStop({Dst}, 0, 1);', 'tele', expect='none', wait=70, again=True, look='The player is teleported and keeps moving.', walk=True)
D(TP, 'Teleport_Line', 'Teleport_Line(0, {t}, 0);', 'telline', expect='none', wait=70, again=True, look='The player is sent to the marked line in the middle of the room.', walk=True)
# ---------------------------------------------------------------------------------------------------------- exits and scripts
X = 'Level exits and scripts'
D(X, 'ACS_Execute', 'ACS_Execute({H}, 0, 10, 20, 30);', 'none', helper='script {H} (int a, int b, int c)\n{\n    Print(s:"Arguments added up: ", d:a + b + c);\n}',
  expect='none', wait=105, look='A second script receives three numbers and prints their sum.')
D(X, 'ACS_Suspend', 'ACS_Execute({H}, 0, 0, 0, 0);\n    Delay(70);\n    ACS_Suspend({H}, 0);\n    Print(s:"The flashing script is suspended");',
  'stage', helper='script {H} (void)\n{\n    while (1)\n    {\n        Light_ChangeToValue({t}, 255);\n        Delay(10);\n        Light_ChangeToValue({t}, 40);\n        Delay(10);\n    }\n}',
  wait=70, reset=LR, look='The helper flashes the stage; the main script suspends it.')
D(X, 'ACS_Terminate', 'ACS_Execute({H}, 0, 0, 0, 0);\n    Delay(70);\n    ACS_Terminate({H}, 0);\n    Print(s:"The flashing script is stopped");',
  'stage', helper='script {H} (void)\n{\n    while (1)\n    {\n        Light_ChangeToValue({t}, 255);\n        Delay(10);\n        Light_ChangeToValue({t}, 40);\n        Delay(10);\n    }\n}',
  wait=70, reset=LR, look='The helper flashes the stage; the main script terminates it.')
D(X, 'Exit_Normal', 'Exit_Normal(0);', 'none', warn=True, expect='none', look='Ends the map the normal way. Press the red switch.')
D(X, 'Exit_Secret', 'Exit_Secret(0);', 'none', warn=True, expect='none', look='Ends the map by the secret exit. Press the red switch.')

# ---------------------------------------------------------------------------------------------------------- generic movers
GE = 'Generic movers'
D(GE, 'Generic_Crusher2', 'Generic_Crusher2({t}, 16, 8, 0, 10);', 'ceil_v', wait=175, reset='Ceiling_CrushStop({t});\n    ' + CR, look='A configurable crusher.')
D(GE, 'Generic_Floor', 'Generic_Floor({t}, 16, 32, 0, 16);', 'floor', reset=FR, look='A configurable floor: up by 32.')
D(GE, 'Generic_Ceiling', 'Generic_Ceiling({t}, 16, 32, 0, 16);', 'ceil', reset=CR, look='A configurable ceiling: up by 32.')
D(GE, 'Generic_Door', 'Generic_Door({t}, 32, 0, 105, 0);', 'door_c', wait=245, reset='Door_Close({t}, 32);', look='A configurable door: open, wait, close.')
D(GE, 'Generic_Lift', 'Generic_Lift({t}, 32, 24, 1, 0);', 'lift', wait=280, reset=PR, look='A configurable lift.')
D(GE, 'Generic_Stairs', 'Generic_Stairs({t}, 4, 16, 1, 0);', 'stairs', wait=175, reset=SR, look='A configurable staircase going up.')
D(GE, 'Generic_Crusher', 'Generic_Crusher({t}, 16, 8, 0, 10);', 'ceil_v', wait=175, reset='Ceiling_CrushStop({t});\n    ' + CR, look='A configurable crusher.')

# ---------------------------------------------------------------------------------------------------------- polyobjects
PO = 'Polyobjects'
PORES = 'Polyobj_MoveTimes8({t}, 32, {down}, 8);'
D(PO, 'Polyobj_RotateLeft', 'Polyobj_RotateLeft({t}, 16, 64);', 'poly', wait=105, reset='Polyobj_RotateRight({t}, 16, 64);', expect='thing', look='The block turns left by a quarter.')
D(PO, 'Polyobj_RotateRight', 'Polyobj_RotateRight({t}, 16, 64);', 'poly', wait=105, reset='Polyobj_RotateLeft({t}, 16, 64);', expect='thing', look='The block turns right by a quarter.')
D(PO, 'Polyobj_Move', 'Polyobj_Move({t}, 16, {up}, 48);', 'poly', wait=105, reset=PORES.replace('8);', '6);'), expect='thing', look='The block slides 48 units.')
D(PO, 'Polyobj_MoveTimes8', 'Polyobj_MoveTimes8({t}, 16, {up}, 6);', 'poly', wait=105, reset=PORES.replace('8);', '6);'), expect='thing', look='The block slides 6 times 8 = 48 units.')
D(PO, 'Polyobj_DoorSwing', 'Polyobj_DoorSwing({t}, 16, 64, 105);', 'poly_bar', wait=245, expect='thing', look='The bar swings open on its hinge, waits, swings back.')
D(PO, 'Polyobj_DoorSlide', 'Polyobj_DoorSlide({t}, 16, {up}, 48, 105);', 'poly_bar', wait=245, expect='thing', look='The bar slides open, waits, slides back.')
D(PO, 'Polyobj_OR_MoveToSpot', 'Polyobj_OR_MoveToSpot({t}, 16, {S});', 'poly_spot', wait=105, reset='Polyobj_OR_MoveTo({t}, 16, {px}, {py});', expect='thing', look='The block slides to the map spot (overriding).')
D(PO, 'Polyobj_MoveToSpot', 'Polyobj_MoveToSpot({t}, 16, {S});', 'poly_spot', wait=105, reset='Polyobj_MoveTo({t}, 16, {px}, {py});', expect='thing', look='The block slides to the map spot.')
D(PO, 'Polyobj_Stop', 'Polyobj_RotateLeft({t}, 16, 255);\n    Delay(70);\n    Polyobj_Stop({t});', 'poly', wait=35, expect='thing', look='A block that turns for ever is stopped.')
D(PO, 'Polyobj_MoveTo', 'Polyobj_MoveTo({t}, 16, {px2}, {py2});', 'poly', wait=105, reset='Polyobj_MoveTo({t}, 16, {px}, {py});', expect='thing', look='The block slides to a given spot of the map.')
D(PO, 'Polyobj_OR_MoveTo', 'Polyobj_OR_MoveTo({t}, 16, {px2}, {py2});', 'poly', wait=105, reset='Polyobj_OR_MoveTo({t}, 16, {px}, {py});', expect='thing', look='The same, overriding.')
D(PO, 'Polyobj_OR_RotateLeft', 'Polyobj_OR_RotateLeft({t}, 16, 64);', 'poly', wait=105, reset='Polyobj_OR_RotateRight({t}, 16, 64);', expect='thing', look='Turns left by a quarter (overriding).')
D(PO, 'Polyobj_OR_RotateRight', 'Polyobj_OR_RotateRight({t}, 16, 64);', 'poly', wait=105, reset='Polyobj_OR_RotateLeft({t}, 16, 64);', expect='thing', look='Turns right by a quarter (overriding).')
D(PO, 'Polyobj_OR_Move', 'Polyobj_OR_Move({t}, 16, {up}, 48);', 'poly', wait=105, reset=PORES.replace('8);', '6);'), expect='thing', look='Slides 48 units (overriding).')
D(PO, 'Polyobj_OR_MoveTimes8', 'Polyobj_OR_MoveTimes8({t}, 16, {up}, 6);', 'poly', wait=105, reset=PORES.replace('8);', '6);'), expect='thing', look='Slides 6 times 8 units (overriding).')
