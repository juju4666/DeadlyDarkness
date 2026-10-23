# game/
Put `begotten.wad` (the game file) in this folder before building.
The build packages it next to the executable / inside the AppImage.
The `*.txt` files here (Freedoom license + credits) are installed with every package. Keep them while any Freedoom lump remains in begotten.wad (see docs/LICENSING.md: palette, colour maps, GENMIDI).

begotten.wad is built from a folder tree with `tools/pack_wad.py`. Since 0.9.13 the tree is all placeholder content;
`tools/make_placeholder_content.py OLD_TREE NEW_TREE` made it. See SOUND_MAP.md in the tree for the sound / music names.
