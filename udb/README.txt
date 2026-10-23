Begotten: Ultimate Doom Builder configuration (for engine 0.10.47)

1. Copy udb/Begotten_Doom2Hexen.cfg and udb/DSDADoom_DoomUDMF.cfg into UDB's Configurations folder.
2. Copy everything in udb/Includes into UDB's Configurations/Includes folder (overwrite older copies).
3. Copy udb/Scripting/Begotten_ACS.cfg into UDB's Scripting folder. It makes the script editor compile ACS with "acc -h"
   (Hexen-compatible output, the only kind the engine runs). The config above selects it by default. Works on Windows, Linux, macOS.
   (Same thing by hand: put -h at the start of the "parameters" line of UDB's Scripting/ZDoom_ACS.cfg.)
4. Restart UDB, create or open a map and pick "Begotten: Doom 2 (Hexen format)" as the game configuration.
5. Read Begotten_ACS_Manual.html, Part 4 ("Begotten in the editor"): things, lights, fog, sun, weather, co-op rules.

What is in the thing list ("Begotten: ..." categories):
 - Lights      : 20101-20108 stationary (baked, real shadows), 20201-20208 movable; the five args tune each light
 - Monsters    : the new roster under its new names (Modern / Medieval / Alien / Bosses)
 - Items       : the 14 items under their Begotten names and effects (Doom's numbers)
 - Weapons     : the weapon pickups, listed by the number key of their slot
 - Ammo        : 20001-20006
Line actions: 174 Checkpoint (set activation "Player walks over"), 212 Music Change, 213 Music Stop.
Boss marking: give any monster the Tag (thing ID) 30000-30999 and it counts as a boss for the ticket rules.

examples/MAP01_showcase.wad  the Gallery of Lights (lights, window, fog zone, movable light) plus the script lab: a door in the south wall leads to one room per script function
examples/BGLIGHT_example.txt every BGLIGHT command, commented (a text lump named BGLIGHT in your wad)
LIGHTING.md                  the lighting rules in plain text

Not tested inside UDB itself (the files were checked for syntax and against the engine).
If UDB complains about a thing number defined twice, the renamed items and monsters are cosmetic: the game does not care.

Sizes in the editor (important)
The game draws its art at 2.5 picture pixels per map unit (data/BGSCALE.txt), but UDB draws 1 pixel = 1 unit, so
sprites and textures look 2.5 times too big and textures cannot be aligned.  Use the EDITOR WAD instead of begotten.wad:
    python3 tools/make_editor_wad.py <content folder> begotten_editor.wad      (needs Python 3 + Pillow)
then in UDB: Game Configuration -> Resources: replace begotten.wad with begotten_editor.wad (as the IWAD).  Same names,
shrunk to the real size.  Never play with it; play with begotten.wad.  Run the tool again whenever the art changes.

UDB under Wine / Lutris (Linux)
Saving can crash UDB in its "building map nodes" step (Wine's Mono cannot wait for the nodebuilder process).  The map is
already written when that happens.  Build the nodes outside UDB instead:   python3 tools/bgnodes.py MAP.wad
(uses the Linux zdbsp from the Compilers.zip; --zdbsp PATH or $ZDBSP if it is elsewhere).  Installing real .NET 4.8 into the
Wine prefix (winetricks dotnet48) in place of Wine-Mono may also cure the crashes; not tested.
