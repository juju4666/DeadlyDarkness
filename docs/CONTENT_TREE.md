# Working with a folder tree instead of a WAD (tools/pack_wad.py)

A WAD cannot hold folders, so the game's content lives in an ordinary folder tree on your disk and
`pack_wad.py` builds `begotten.wad` from it in a few seconds. You never need SLADE to arrange,
name or order lumps any more.

Needs Python 3.8+ and Pillow: `pip install pillow`.

## The window (no terminal)
Run the program with no arguments, or double-click it: a window opens with the content folder, the WAD file
and three buttons (Pack folder into WAD, Unpack WAD into folder, Check folder). Windows: the packer is built
by the "Build WAD packer" workflow on GitHub (Actions tab -> Run workflow -> download BegottenWadPacker-Windows);
Linux: BegottenWadPacker-linux-x86_64. With Python + Pillow installed you can also just run pack_wad.py.
The terminal commands below do the same thing.

## One-time conversion
    python pack_wad.py unpack begotten.wad content
    python pack_wad.py unpack --merge --suffix-dups begotten_poses.wad content    # the pose placeholders
Then build:
    python pack_wad.py pack content begotten.wad
Check without writing: `python pack_wad.py check content`.
(Keep the old begotten.wad somewhere until you have played the rebuilt one once.)

## The tree
The FIRST folder tells the packer what a file is. Below it you may nest as deep as you like and move
folders around freely; only the file name matters.

| folder | files | becomes |
|---|---|---|
| `palette/` | PLAYPAL.lmp, COLORMAP.lmp, ENDOOM.lmp ... | raw lumps |
| `sprites/` | `*.png` | pictures between S_START / S_END |
| `patches/` | `*.png` | pictures between P_START / P_END (pieces used by `textures.txt`) |
| `textures/` | `*.png` | each picture is a finished wall texture (patch + texture of the same name) |
| `flats/` | `*.png` | floors / ceilings (64x64, 64x128, 128x128, 256x256). Floor art goes here, not in `textures/` |
| `graphics/` | `*.png` | pictures outside markers: menu, HUD, TITLEPIC, BGPIC ... |
| `sounds/` | `*.wav` | DMX sound lumps (8-bit mono is exact; 16-bit/stereo is converted) |
| `music/` | `*.mus *.mid *.ogg` | raw music lumps |
| `colormaps/` | raw | between C_START / C_END |
| `maps/` | `*.wad` | one small wad per map: open it in your map editor and save over it |
| `data/` | `*.txt *.lmp` | everything else: BGCLASS, BGMONSTER, DEHACKED, UMAPINFO, ANIMATED ... |
| `textures.txt` | text | composite wall textures (what TEXTURE1 / PNAMES hold) |
| `wad.cfg` | `type = IWAD` | IWAD or PWAD |

Example: `sprites/characters/players/witch/idle/WITCA1.png`, `textures/modern/walls/BRICK1.png`,
`flats/modern/floors/TILE1.png`, `sounds/ambient/modern/indoors/HUM1.wav`.

## Rules
* Lump name = file name without extension, capitals, at most 8 characters (`WITCA1.png` -> `WITCA1`).
* Same name twice in one group is an error. `~2`, `~3` ... after the name is dropped, so
  `BGCLASS.txt` and `BGCLASS~2.txt` are both packed, in that order. The engine reads every BGCLASS lump
  in order (the engine's own first, then the game's wads); a later one overrides, `edit <class>` adds.
* Sprites: `SPRTA1.png` = sprite SPRT, frame A, rotation 1; `A2A8` names are mirrored pictures.
  `check` tells you about frames with missing rotations (the engine refuses those).
* Hot spot (offset) of a picture is stored in the PNG as a `grAb` chunk, as SLADE / ZDoom do. A PNG
  without one has offset 0,0. Most editors keep the chunk only if you use "export for Doom/ZDoom"; if
  your editor drops it, tell me and I will add an `offsets.txt` file next to the pictures.
* Colours are matched to `palette/PLAYPAL.lmp` when packing (nearest colour; alpha < 128 = transparent).
  So after you change the palette, just pack again. If the palette has two identical colours the
  lower index is used.
* Pictures can be at most 2048 wide and 1024 tall (taller than 254 rows are written as DeePsea tall patches). Art with more than one pixel per map unit needs a `data/BGSCALE.txt`, see BEGOTTEN_ENGINE.md (0.10.7).
* `textures.txt`:
        NAME width height
          PATCH x y
          PATCH x y
  Pictures in `textures/` are added after these automatically.
* Patch markers P1/P2/P3 and the like are not kept; the packer writes one P_START/P_END, S_START/S_END,
  F_START/F_END.
