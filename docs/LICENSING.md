# Begotten: licensing notes

*Written by Claude as a working checklist, not legal advice. Before the commercial release, have a lawyer who knows open-source licensing read sections 3 and 4 (especially Steam).*

## 1. What Begotten is, legally

- The **engine** is a modified fork of **dsda-doom 0.30.0**. dsda-doom declares **GPL-2.0-only** (`prboom2/vcpkg.json`; `prboom2/COPYING` is GPLv2). It descends from PrBoom+, PrBoom, Boom, MBF, LxDoom and id Software's Doom source (GPL since 1999), plus Heretic/Hexen code (Raven Software, GPL since 2008) and Chocolate Doom's textscreen library. Nuked-OPL3 (`src/MUSIC/opl3.c`) is LGPL 2.1+.
- Everything you or I changed in the engine is a **derivative work** and is therefore GPLv2 too. You are one copyright holder among many. You cannot relicense it, and nobody can ask all the contributors.
- The **game data** (`begotten.wad`: maps, art, sounds, music, ACS, DEHACKED, UMAPINFO, your BG* lumps) is **separate from the engine**. The GPL does not reach it just because the engine loads it, in the same way that id Software's commercial IWADs run on GPL ports. It can stay proprietary, with the exceptions in section 2.

## 2. Obligations whenever you hand out a build

"Hand out" includes testers, friends, itch.io, Steam and any download link.

1. **Source for every binary you distribute.** The complete, buildable source of the engine as shipped, including your changes, build scripts and any Steam glue code that is part of the same program (see section 4). The simplest way is to publish the source zip you already get with each build (a public repository, or a link on the store page and in the game's README). A written offer valid for 3 years is the alternative, but a link is easier to keep honest.
2. **Keep the licence and notices.** Ship `COPYING` (GPLv2) and the dsda-doom/PrBoom `AUTHORS`; do not strip copyright headers from source files.
3. **Mark your changes.** GPLv2 section 2(a) asks for notices that you changed files and when. `BEGOTTEN_ENGINE.md` (the version history) plus the repository history covers this. Keep it in the source zip.
4. **No extra restrictions.** Section 6 forbids adding terms that limit what recipients may do with the engine: for example an EULA clause that bans redistributing or modifying the engine. Terms for your **game data** are separate and can be restrictive.
5. **Trademarks.** "Doom", "Heretic" and "Hexen" are trademarks of their owners. The GPL gives no trademark rights, so keep them out of the title, store page and marketing. Say "built on dsda-doom" in the credits.
6. **Never ship id Software's or other commercial game files** (`doom.wad`, `doom2.wad`, ...). Begotten's own `begotten.wad` as an IWAD avoids that.

### Freedoom
Since 0.9.13 `begotten.wad` contains **no Freedoom pictures, sounds, music or maps** (all of it is placeholder content made by `tools/make_placeholder_content.py`). Since 0.10.4 the palette, `COLORMAP` and the colour-map / translucency tables are original too (`tools/make_palette.py`). Two lumps still come from Freedoom and are kept because they are tables the engine needs, not art: `GENMIDI` (OPL instrument table) and `DMXGUSC`. Freedoom is under a modified BSD licence: while those remain, your package must keep the copyright notice, the conditions and the disclaimer (`game/FREEDOOM-COPYING.txt`, `FREEDOOM-CREDITS.txt`, which the build already installs), and you may not use the Freedoom name or its contributors to promote Begotten. To drop the notice, replace those lumps with your own (GENMIDI and DMXGUSC can be removed if you do not use OPL / GUS music).

### Bundled libraries
The AppImage and the Windows package carry third-party libraries. Ship a `THIRD-PARTY-NOTICES.txt` with each package listing the licence and where the source can be obtained:

| Library | Licence | Note |
|---|---|---|
| SDL2, SDL2_mixer, SDL2_image, zlib, libzip, libpng, libjpeg, libtiff | zlib / BSD / similar | keep the notices |
| libFLAC, libogg, libvorbis, libopus, opusfile, libsamplerate | BSD | keep the notices |
| libsndfile, libmpg123, libfluidsynth, libinstpatch, libmp3lame | LGPL 2.1 | OK when dynamically loaded; ship licence text, offer the library source, keep them as separate `.so`/`.dll` files |
| libmodplug | public domain | |
| libreadline 8 (GPLv3), libtinfo, libdb 5.3 | GPLv3 / MIT / Sleepycat | **removed from the AppImage since 0.8.14** (readline and tinfo exist on every distro; libdb was not needed by anything) |
| libsystemd, libcrypto (OpenSSL 3), libdbus, libpulse, libapparmor, libgcrypt | various | system libraries still bundled; they are not used by the engine itself (libcrypto only by libzip) and could also be dropped in a later cleanup, after testing on a clean machine |

On Windows the program is built with its libraries **linked in statically** (the zip contains only `begotten.exe` and no DLLs; checked on 0.8.14). That is fine for the LGPL libraries because the whole program is GPLv2 with its source published, but the notices file must ship with the exe and the source must include the build recipe (the vcpkg manifest and the CI workflow) so someone can rebuild with a different library version.

## 3. What can stay closed

- All of `begotten.wad`: maps, art, music, sounds, text, ACS and so on. Note the exceptions: lumps that came from the engine's own data (`begotten-data.wad`, built from `prboom2/data`) are GPL, and any Freedoom asset keeps its licence.
- Your source scripts and tools for making the game's content, as long as they are not part of the shipped engine.
- Steamworks-only code that lives in a **separate program** (section 4).

## 4. Steam

**Can the engine be closed source if I "disclose my changes to DSDA"?** No. Disclosing the changes *is* publishing the source: GPLv2 requires the full source of the whole program to everyone who receives the binary, on any terms they like to redistribute. You can still **sell** it, and the game data stays yours. What you cannot do is keep the engine code private.

**Open-source DSDA without Steam, closed Steam build?** Also no. The Steam build is a distribution of the same derivative work, so its source must be available as well, including the Steam glue if that glue is compiled into the engine. A version of the engine that exists only inside Steam and only as a binary would violate the GPL.

**What does work:**

1. Publish the whole engine source (what you already package). Anyone can build it without Steam.
2. Do Steam features in a way that keeps the GPL clean. The options, from safest to riskiest:
   - **Features that need no Steamworks code at all.** Steam Input (works through SDL, as on your Deck), Steam Cloud (path configuration on the Steamworks site, no code), Remote Play Together, and the overlay (when the game starts from Steam) all work without linking anything. Your own UDP co-op does not use Steam networking.
   - **A separate helper program** for Steamworks calls (achievements, stats, rich presence). The engine writes a plain log or profile file (`begotten_profile.txt` already exists), the helper reads it and calls the Steam API. They are two programs talking through a file, pipe or command line, so the helper is not a derivative work and may stay closed. The engine side of that interface is GPL like everything else.
   - **Steamworks compiled into or loaded by the engine.** Valve's SDK licence forbids redistributing the SDK files (so `steam_api.h` must not go in a public repository), and `steam_api64.dll` ships *with* your game, so the GPL's "system library" exception does not apply to it. Linking GPL code with a proprietary library you distribute alongside it is, by the FSF's reading, a violation unless every copyright holder allows it. Other developers load `steam_api` at run time and accept the risk, but because you are not the sole copyright holder here, **this is the case to ask a lawyer about before you do it.**
3. Steam also sets terms for the *store* (EULA, DRM options). Do not apply Steam DRM wrapping to the engine executable; it would conflict with GPL section 6.

**My recommendation:** publish the full engine source, keep `begotten.wad` closed, and add achievements through the separate-helper route when you get to it. Everything else on Steam works without touching the licence.

## 5. Before a public release

- [ ] Source zip downloadable from the store page, README and in-game credits (a link in the credits screen is enough).
- [ ] `COPYING`, `AUTHORS`, `FREEDOOM-*.txt` (while any Freedoom asset remains) and `THIRD-PARTY-NOTICES.txt` in every package.
- [ ] No `doom.wad`/`doom2.wad` anywhere; no "Doom" in the title or store text.
- [x] libreadline, libtinfo, libdb removed from the AppImage (0.8.14). Consider dropping the remaining system libraries after a clean-machine test.
- [ ] Keep `BEGOTTEN_ENGINE.md` (the change history) with the source.
- [ ] Re-read section 4 with a lawyer before Steamworks code is added.
