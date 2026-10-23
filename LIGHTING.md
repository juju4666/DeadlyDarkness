# Begotten lighting guide

Real-time lights for the software renderer (engine 0.10.36). Everything here works in the software renderer on
every platform; OpenGL is not used by Begotten and ignores these lights.

Lights brighten and tint walls, floors, ceilings and sprites around them. There are two kinds:

* **Stationary lights** (the default) never move. When a level starts, the engine works out what each one lights
  and stores it in small grids on every wall, floor and ceiling it reaches. That calculation traces real rays
  through the map, so **walls, pillars, steps and closed doors cast true shadows**: a column in a lit room leaves a
  dark patch behind it. During play only a lookup is left. Flicker and pulse still work, and a stationary light
  whose thing is destroyed (a shot-out lamp) goes dark.
* **Movable lights** are worked out every frame. Use them for a light on a lift or door, or anything else that
  has to change while you play. They do not cast shadows: their light spreads from their sector through open
  two-sided lines only. Projectiles, explosions, muzzle flashes and the player lantern are always movable.

Because stationary light is baked with the map as it is when the level starts, it does not follow doors and lifts:
a closed door blocks a stationary light for the whole level, an open door never lets it through. Put a light that
shines through a door, or that sits on a lift, in the movable kind. The engine warns about the likely mistakes
(see "Warnings").

## What you can place (Doom Builder things, category "Begotten: Lights")

| Number | Name   | Shape                                   | Default colour | Behaviour       |
|-------:|--------|-----------------------------------------|----------------|-----------------|
| 20101  | Torch  | point, radius 320, height 48            | warm           | flickers        |
| 20102  | Lamp   | point, radius 480, height 32            | white          | steady          |
| 20103  | Spot   | ceiling cone, radius 640, 25/50 degrees | white          | steady          |
| 20104  | Strip  | ceiling tube 128 long, cone 40/85       | cool white     | steady          |
| 20105  | Candle | point, radius 200, height 20            | candle         | flickers        |
| 20106  | Green  | point, radius 400, height 40            | green          | slow pulse      |
| 20107  | Red    | point, radius 400, height 40            | red            | slow pulse      |
| 20108  | Blue   | point, radius 400, height 40            | blue           | steady          |

Each of these has a **movable twin** with the number plus 100: 20201 torch, 20202 lamp, 20203 spot, 20204 strip,
20205 candle, 20206 green, 20207 red, 20208 blue. Same look, but live every frame.

### Gyro lights (engine 0.10.49)

| Number | Name        | Shape                                              | Behaviour             |
|-------:|-------------|----------------------------------------------------|-----------------------|
| 20301  | Red beacon  | ceiling cone 12/30 degrees, tilted 20 down, radius 480 | turns 180 degrees a second |
| 20302  | Amber beacon | same                                               | same                  |
| 20303  | Blue beacon  | same                                               | same                  |

They are always movable (no shadows). The thing's angle is where the beam points when the level starts, so two things
180 degrees apart make a two-beam beacon. Your own: `light 20310 mybeacon radius 400 strength 1 height 24 ceiling spot 10 25 pitch -10 color red spin -90`.

### Switching a light off and on

Any placed light (stationary or movable, gyro or not) follows the Hexen **dormant** state of its thing: give it a Thing ID in
the editor, then `Thing_Deactivate(id)` turns it off and `Thing_Activate(id)` turns it on again (script or line special).
Tick "Dormant" on the thing to start it switched off. `Thing_Remove(id)` still removes it for good.

* Point lights sit `height` units above the thing's floor. Spot and strip lights hang that far below the
  ceiling. Their thing angle gives the tube direction (strip) and the cone's facing yaw.
* A **strip** (the long kitchen-lamp kind) lights along its whole length, not from a centre point: a floor under
  either end is lit as much as under the middle. Rotate the thing to rotate the tube.
* Lights are invisible in-game. In the editor they show with a stand-in sprite.

### Tuning one placed light (thing args, Hexen-format maps)

Every light thing has five args. 0 means "use the definition's own value".

| Arg  | Meaning                                              |
|------|------------------------------------------------------|
| arg0 | Radius, in units of 8 (10 = radius 80, 60 = 480)     |
| arg1 | Strength in percent (50 = half; 100 = as defined)    |
| arg2 | Colour red, 0-255                                    |
| arg3 | Colour green, 0-255                                  |
| arg4 | Colour blue, 0-255                                   |

Colour args all 0 keeps the definition's colour. Only the proportions matter (255 0 0 and 128 0 0 are the same
red). Strength above 100 is allowed but light is capped just below full bright.

## Things that light up by themselves

* **Player lantern**: a dim candle-coloured light on every player (the Dark-Souls-style carried light). Menu:
  "Player Lantern".
* **Projectiles**: every projectile in flight glows in the colour of its own sprite.
* **Explosions**: a projectile that hits glows brighter and wider, fading over its death frames.
* **Power-ups**: a soul sphere, mega sphere, invulnerability, berserk, radiation suit, computer map or light
  amplification visor lying on the floor glows with a slow pulse in the colour of its sprite, until it is picked up.
* **Flashlight**: the light amplification pickup no longer turns the whole screen full bright. It gives a flashlight
  instead, a cone of light along the player's view (it follows looking up and down when free look is on) for as
  long as the pickup lasts, blinking in its last seconds. Other players' flashlights show too. The beam casts shadows: walls, pillars, steps,
  lowered ceilings and closed doors block it (traced every frame, so a door that opens lets the light through).
  It is held a hand's width to the right of and a little below the eye, because a light exactly at the eye hides
  every shadow behind the thing that casts it. Solid things (monsters, players, columns, barrels) block it too, as upright cylinders of their radius and height, so a monster throws a shadow on the floor and wall behind it. Menu: "Flashlight Shadows" and "Monsters Block Flashlight"; BGLIGHT `thingshadows off` turns the things' shadows off for the whole wad. Other live lights can opt in with
  `shadow` (see below).
* **Bullet puffs and chainsaw sparks**: a small, warm, quick flash where a shot or a saw hits a wall or floor.
* **Muzzle flash**: a warm light in front of a player while their gun flash shows. This replaces Doom's old
  trick of brightening the whole sector when you shoot (the engine no longer does that).

All of these can be tuned in the BGLIGHT lump (below).

## How light combines

* Each light adds brightness with a smooth falloff, `(1 - d^2 / r^2)^2`, times its strength and flicker.
* Walls and floors facing away from a light do not receive it; facing it receives up to full.
* Overlapping lights add up (never past full bright) and their colours **mix** in proportion to how much each
  contributes, so a red and a blue light make purple where they overlap.
* Lighting works in the sector's own light level: the darker the sector, the more a light stands out. Lights
  cannot make a sector brighter than full bright.
* At most the **12 movable lights nearest the view** are used each frame. A level can hold up to **128 stationary
  lights** (no limit per frame: the grids are looked up per wall, floor and ceiling).
* Stationary shadows come from one-sided walls and from the openings of two-sided lines (steps, lowered ceilings,
  closed doors). Middle textures (windows, gratings) do not block light. Light sample spacing is 8 units, so
  shadow edges are slightly soft.

### Colours and palettes

Coloured light is built from the current palette (and COLORMAP), so it follows per-episode palettes. The mixed
colour is quantised to 125 colours (5 levels per channel) and the strength to 12 steps, then matched to the
nearest palette colour. Palettes with few distinct hues will tint less cleanly than rich ones.

## Warnings

When a level is baked (the first frame after it starts) the engine writes to the log (`begotten.log`) a line like

    BGLIGHT warning: stationary light 20101 at (320, 128) is near a moving sector (sector 12, a door or lift): the
    baked light will not follow it - use the movable twin (20201) if this light must change

for every stationary light that is inside, or whose reach touches, a sector that a door, lift, floor or ceiling
action (Hexen-format specials: Door_*, Floor_*, Plat_*, Ceiling_*, Generic_*, Elevator_*) will move. It is only a
hint: a lamp that merely sits in the same big room as a door is fine. The log also says how long the bake took.
Doom-format maps are not checked.

## Dithering (optional, off by default)

Menu: Setup > Compatibility > "Dither Lighting (Retro Look)" (config `begotten_dither`). Off: light falls off in
clean bands. On: the steps between brightness levels are blended with an ordered 8x8 pattern, which removes the
banding and gives a gritty, retro grain (it looks best on a CRT or a low-resolution display). It costs a little
extra frame time.

## The BGLIGHT lump

Optional lump in any wad (loaded after the built-ins, so it can redefine them). One command per line; `#` or `//`
starts a comment.

```
color NAME R G B                  # 0-255; only the proportions matter. Define before use.
light NUMBER NAME [options]       # a placeable light thing
lantern [options]                 # the light each player carries
missile [options]                 # every projectile in flight
explosion [options]               # a projectile that has hit
muzzle [options]                  # a player's gun flash
powerup [options]                 # a power-up lying on the floor
puff [options]                    # a bullet puff or chainsaw spark
flashlight [options]              # the light amplification flashlight; also takes `spot INNER OUTER`, `offset SIDE DROP` (default `10 8`; `0 0` puts it at the eye) and `noshadow`
fog [map MAPxx] [sector TAG] [options]   # haze for a level or for the sectors with one tag (see "Fog" below)
sun [map MAPxx] [options]         # a sun (or moon) for one level (see "Sun" below)
```

Every `light` line, and the `missile`, `explosion` and `powerup` lines, also take `halo SIZE` (the glow drawn round the
light) and `corona PIXELS` with `coronafade NEAR FAR` (the star on the light; see "Glows and coronas" below). The built-in torches, lamps, candles, coloured lights, missiles, explosions and power-ups have
one; a `light` line of your own has none unless it says so.

Options for `light`:

| Option                | Meaning                                                              |
|-----------------------|----------------------------------------------------------------------|
| `radius R`            | reach in map units                                                   |
| `strength S`          | 0-1 (can exceed 1, light is capped below full bright)                |
| `height H`            | units above the floor (below the ceiling with `ceiling`)             |
| `color NAME`          | a colour defined above (default white)                               |
| `ceiling`             | hang from the ceiling                                                |
| `movable`             | live every frame, not baked (lights on lifts and doors)              |
| `spot INNER OUTER`    | a cone: full inside INNER degrees, fading out at OUTER (half angles) |
| `pitch DEG`           | cone direction, -90 = straight down                                  |
| `shadow`              | with `movable`: the live light is blocked by walls, steps and closed doors (a ray per lit point, so use it on one or two lights, not twenty) |
| `spin DPS`            | the cone or tube turns round DPS degrees a second (negative = the other way); implies `movable` |
| `tube LEN`            | a strip light LEN units long instead of a point                      |
| `flicker`             | random flicker (see below)                                           |
| `pulse`               | smooth pulsing (see below)                                           |
| `rate N`              | flicker: how many times a second the level changes (default 11.7)    |
| `depth D`             | flicker/pulse: how far it dims, 0.3 = down to 70% (defaults: 0.26 flicker, 0.4 pulse) |
| `period S`            | pulse: seconds per cycle (default 1.5)                               |

`lantern`, `missile`, `explosion`, `muzzle`, `powerup`, `puff` and `flashlight` take `radius`, `strength`, `height` and `color`. For `missile`, `explosion` and
`powerup` the colour may be `color auto` (the default), which takes the colour from the projectile's sprite.

Examples:

```
color amber 255 170 60
light 20109 brazier radius 380 strength 0.95 height 40 color amber flicker rate 9 depth 0.35
light 20110 beacon  radius 500 strength 1.0 height 64 color red pulse period 3 depth 0.8
light 20111 neon    radius 300 strength 0.9 height 8 ceiling tube 96 spot 40 85 pitch -90 color cool
muzzle radius 420 strength 1.0 color warm
missile radius 180 strength 0.7
```

`movable` makes a light live every frame (the default is stationary). Defining a light with a new number makes a new placeable thing; add it to your Doom Builder thing list (see
`Begotten_lights.cfg`) to place it. Up to 48 light definitions can be loaded.

### Flicker, in plain words

* `flicker` picks a new random brightness `rate` times per second, each dimming the light by anywhere from nothing
  up to `depth`. Fast and deep = a guttering torch; slow and shallow = a steady candle.
* `pulse` swings smoothly between full and `1 - depth` once every `period` seconds. Add depth 0.8 and period 3
  for a lighthouse beacon.

## Glows and coronas

A light with a `halo` size has a soft glow round it on the screen, as if the air near it were lit: a torch, a lamp, a
fireball, an explosion, a power-up. The glow is the light's colour added to what is behind it. Things and walls in front
of the light hide the glow, things behind it are lit by it, and close up it fades so it never fills the screen. It is
drawn like a sprite, so it costs little (a few thousand pixels per visible light). Players switch it in the options menu
("Glow Around Lights", config `begotten_halo`). Strip lights and ceiling spots have none by default (no glow
round a long tube); a light that is behind the viewer or off screen has none either. A level without `halo` on its
lights looks as before.

A **corona** is the other effect: a small bright star (a hot core, a soft body and a faint cross of streaks) on the light
that keeps the **same size on the screen at any distance**, so a lamp far down a hall is still a point of light. It
fades to nothing as you come close: `coronafade NEAR FAR` gives the distances in map units, invisible at NEAR and full
at FAR (default 40 and 140). `corona PIXELS` is its radius on a 320-wide view (it scales with the window). It is
hidden by anything in front of the light, exactly like the glow, and has its own menu switch ("Coronas On Lights",
config `begotten_corona`). The built-in torches, lamps, candles, coloured lights and missiles have one.

## Fog

A level can have a haze: things fade toward the haze colour with distance, and lights glow in it (a lit wall far away
is still brighter than an unlit one). It is per level, set in BGLIGHT:

```
fog map MAP01 color 34 40 54 density 0.0018 scatter 0.9 max 0.97
```

| Option    | Meaning                                                                                     | Default            |
|-----------|---------------------------------------------------------------------------------------------|--------------------|
| `map`     | the level's lump name (`MAP01`, `E1M1`); a line without `map` applies to every level without its own | all levels |
| `color`   | haze colour, R G B 0-255                                                                    | 40 46 58           |
| `density` | thickness per map unit; the haze is 63% thick at 1/density units (0.0018 is about 550 units) | 0.0012             |
| `scatter` | how much the lights light the haze itself, 0 (none) to 1                                    | 0.8                |
| `max`     | the thickest the haze ever gets (0-1); lower keeps far walls readable                       | 0.97               |

**Fog in parts of a level.** Add `sector TAG` and the line applies to the sectors with that tag, instead of the whole level:

```
fog map MAP01 color 34 40 54 density 0.0018        # the level's own fog
fog map MAP01 sector 5 color 56 34 74 density 0.0035 scatter 0.5   # the sectors tagged 5: thick and violet
fog map MAP01 sector 6 density 0                   # the sectors tagged 6: clear air
```

Sectors without a line of their own have the level's fog (or none). Up to 15 zones per level. A surface is seen through
the thicker of its own sector's fog and the fog the viewer is standing in (so a clear courtyard seen from inside a foggy
room is hazy, and the other way round the haze stays in the room). The change from one fog to another is at the
sector's edge, with no blending, so put zone edges at doorways and windows. Works in Doom-format and UDMF maps alike
(sector tags are all it needs).

Players can switch fog off in the options menu ("Fog (Where a Level Has It)", config `begotten_fog`). Limits: it is
per level, not per sector; the sky is not fogged; the first-person weapon is not fogged; there is no halo drawn around
a light, only the light on the haze it passes through. It costs about 5-10% of the frame rate in a view full of walls
(measured at 90-94 against 99 fps with it off, in the showcase map on the build machine); the colour tables it needs
are cached per frame, so a very large number of different light and haze mixes in one view costs more.

## Sun and moon

A level can have one directional light: sunlight (or moonlight) that falls the same way everywhere. It is baked when
the level starts, like the stationary lights, with real shadows:

```
color sunlight 255 236 200
sun map MAP01 angle 250 elevation 50 color sunlight strength 0.9
```

| Option      | Meaning                                                                             | Default        |
|-------------|-------------------------------------------------------------------------------------|----------------|
| `map`       | the level's lump name; a line without `map` applies to every level without its own   | all levels     |
| `angle`     | where the sun is, in degrees on the map: 0 east, 90 north, 180 west, 270 south       | 45             |
| `elevation` | how high it is above the horizon, 3 (low, long shadows) to 89 (overhead)              | 40             |
| `color`     | a colour defined with `color NAME R G B`                                              | white          |
| `strength`  | how bright it is on a surface that faces it squarely                                  | 0.9            |

What it lights: a floor or wall point is in the sun when a ray from it toward the sun gets out. **A sector whose
ceiling is a sky flat is open to the sun; any other ceiling stops it.** Walls and pillars cast shadows (a one-sided
wall is as tall as its sector's ceiling), and a two-sided line lets the sun through only inside its opening, so a window
(a thin sector, floor = sill, ceiling = lintel, beside a sky sector) lights a patch of the floor with the sill and the
lintel shading its ends. Walls facing the sun are lit, walls facing away are not, and things (monsters, items) take the
light of the floor they stand on. Ceilings are never lit. The sun adds to the sector's own light level and to other
lights; a dark room with a window is a good use. Open skies get nothing special: the sector light level is what
outdoor floors have in the shade.

Limits: one sun per level; it does not move (no day cycle); sector and door movement after the level starts does not
change it (a door that opens will not let the sun in); only real sky ceilings count, not "outdoor" tricks such as a
very high ceiling; the bake grid is 8 map units, coarser (16, 32...) for very large levels, so a shadow edge is
about that soft. The bake takes about 10 ms for the Gallery of Lights.

## Weather: rain, snow and lightning

A level can have rain or snow. It is cosmetic (it never touches the game rules or the random numbers, so it cannot cause
a desync) and it is drawn as thin specks that walls and things hide properly:

```
weather map MAP01 rain density 1 wind 3 thunder 12
weather map MAP02 snow density 1.5 wind -2
```

| Option    | Meaning                                                                                      | Default    |
|-----------|----------------------------------------------------------------------------------------------|------------|
| `map`     | the level's lump name; a line without `map` applies to every level without its own            | all levels |
| `rain` / `snow` / `none` | what falls (`none` switches inherited weather off for one level)               | rain       |
| `density` | how many specks, 0.1 to 3                                                                     | 1          |
| `wind`    | sideways drift, -16 to 16 (rain drifts about 1.5 units a tic per point, snow 0.5)             | 0          |
| `thunder` | rain only: average seconds between lightning strikes; 0 = no thunder                           | 0          |

Where it falls: only through **sky-ceiling sectors** (the same rule as the sun). A roofed room has no rain inside, a
yard under open sky does, and from indoors you see it through windows and doorways. Specks are made round the viewer
(about 700 map units out), so the cost does not depend on the size of the level: roughly 200 specks, well under a
millisecond a frame.

Lightning: at each strike the whole screen flashes blue-white for about 0.4 s in a quick, flickering pattern, and the
sound `DSTHUNDR` plays 0.4 to 2.4 s later. While it rains and the player stands under open sky, `DSRAIN` plays every two
seconds as a soft loop. Both sounds are lumps you can replace in your wad (`DSRAIN` about 1.5 s of steady hiss that tiles
well, `DSTHUNDR` a rumble). The placeholder wad's are generated noise.

Switches: the option **Rain, Snow And Lightning** in the Begotten options menu (config `begotten_weather`, default on)
turns the whole effect off for a player; the flash also respects the existing "powerup colour" screen-tint option.

Limits: one weather per level; no splashes where drops land; no weather on the automap or in OpenGL mode; the specks
follow the displayed player (in split-screen that is the first one's view only); sectors whose ceiling is not sky stop
it, so a "sky" made of a very high ceiling gets none. Debug: set the environment variable `BEGOTTEN_WXLOG=1` to log the
specks and drawing time, and `BEGOTTEN_WXFIRST=<tic>` to force the first lightning strike at that tic.

## Self-test switches (developers)

`BEGOTTEN_SELFTEST=7` runs the light demo room: `BGLIGHT_COLORS=1` coloured lights, `BGLIGHT_BASE=n` sector light
level, `BGLIGHT_NOLANTERN=1`, `BGLIGHT_DITHER=0|1`, `BGLIGHT_DYN=1` held projectiles and an explosion,
`BGLIGHT_FLASH=1` a held muzzle flash, `BGLIGHT_BAKE=1` bake the demo's lights, `BGLIGHT_NOLANTERN=1` works in any map. `BEGOTTEN_SELFTEST=4` is the live test and checks lights, tints, args,
projectile, explosion and muzzle lights.

## Cost

Lighting costs nothing in a view with no light in range. With lights in view the cost grows with the number of
lights and the screen area they touch (it is a software renderer: every lit column and floor span is shaded
per piece). Baking stationary lights gives them shadows and removes the per-frame light maths, but looking a value
up costs about as much as computing it, so a baked scene runs at about the same speed as the same lights live
(measured in a five-light test room). Dithering adds more. Keep large rooms to a few well-placed lights, and use
small radii where you can. The bake itself took 4 ms for five lights in a small room; expect longer on big maps.

## The showcase map

MAP01 is a walk through every feature, "Gallery of Lights": torches and a candle (stationary, flickering), a door
with a movable torch behind it, a hall of pillars lit by one lamp (true shadows), a low corridor with strip lights
and a spot, a vault of power-ups, a chamber with a red, a green and a blue light round one pillar (coloured shadows
and mixing), a dark arena with monsters throwing fireballs (projectile, explosion and muzzle lights), and a lift
with a movable red light on it. Its source is `tools/make_showcase_map.py` (UDMF; the nodes come from ZDBSP, which
the script calls if it is installed). It is a good template for how to place lights: lights near a door or lift
are movable, the rest stationary, and no stationary light's reach touches a moving sector (so the log has no
warnings).

## Windows and shadows from floors and ceilings

Stationary-light shadows are traced in 3D, so heights matter: a light outside a window lights a patch of the floor
inside, with the sill and the lintel casting their own shadow (the wall below the sill and the floor right under the
window stay dark). Build the window as a thin sector with the sill as its floor and the lintel as its ceiling, so
the two-sided line has an opening of the window's size. Middle textures (glass, bars) do not block light. Put the
light in a sector on the outside, high enough to shine through (a raised terrace, or a light on a pole).
