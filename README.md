# Qwen38FlashNextGame

Walk around the town - created entirely by Qwen 3,8 Flash Next.

Repo: <https://github.com/nooil6152/Qwen38FlashNextGame> · Windows · Win32 + OpenGL 3.3 core · no external dependencies

A first-person walk-around of a procedurally generated town: streets, houses down to
skyscrapers, two rivers with bridges you can actually cross, a moving sun with
shadow mapping, and drifting clouds. Low-poly by design — everything is boxes,
quads and pyramids, no textures, no model files, no third-party libraries.

* **Language / toolchain:** C++17, built with **clang-cl (ClangCL)** — MSVC `v145` also compiles it.
* **API:** raw Win32 (`RegisterClassEx` / `CreateWindowEx` / raw input) + **OpenGL 3.3 core**.
* **Dependencies:** none. `opengl32.lib`, `user32.lib`, `gdi32.lib` only. The GL 3.x entry points are
  loaded by a small hand-written loader (`src/gl_core.*`) instead of GLEW/GLAD/gl3w.

## Versions

| Tag | Commit | What it is |
|---|---|---|
| **`v0.3`** | `4d6c83a` | **current** — adds ground following: walkers stand on the surface instead of 5 cm inside the road, and the player's eye rides decks (`LEDGER.md` ISS-028/029) |
| `v0.2` | `8a36c80` | pedestrians: they walk the street network, cross at bridges, avoid one another, and are solid to you |
| `v0.1` | `91b4974` | first working version — town, two rivers and bridges, sun with shadow mapping, clouds, 600 s day/night cycle |

A tag means **this compiles and runs**, not that it is finished or correct: `v0.3` still has two open visual defects
(mirrored screenshots, walkers that read as sunk) and `LEDGER.md` is the place that says what is broken. Tags are cut
on every green build, so `git clone -b v0.3` always gets you a build that starts.

Both are annotated tags carrying a one-line message. They are **unsigned** — no signing key is configured on the
machine that builds this — so GitHub shows no *Verified* badge; that is expected, not a warning.
`main` may sit a commit or two ahead of the newest tag when the change is documentation only.

### Which document is which

| File | For whom | Contains |
|---|---|---|
| `README.md` | you, running it | this guide: build, run, controls, what is inside the code, how to check it works |
| [`LEDGER.md`](LEDGER.md) | anyone auditing the work | running journal: every design decision with its reasoning, issue register (found / fixed / still open), verification numbers, requirement-by-requirement traceability |
| [`CONTINUE.md`](CONTINUE.md) | whoever picks the code up next | resume notes: current state, locked-in parameters, gotchas that already bit once |

## Build

```bat
build.bat                :: Release x64, then runs the self-test
build.bat Debug          :: Debug x64
build.bat Release --run  :: build and start the game
```

`build.bat` finds VS through `vswhere`, looks for `clang-cl.exe` and passes `-p:UseClang=true` when it is
there (it is on this machine). Without that flag the project builds with MSVC `v145` instead — the sources
are toolset-neutral. Output is `bin\Town3D.exe`.

Or open `Town3D.sln` in Visual Studio and press F5.

## Run

```
WASD / arrows   walk            Shift   run
mouse           look            Esc     quit
T               sun auto-advance on/off
[  ]            scrub the day clock backwards / forwards
R               regenerate the town with a new seed
P               screenshot -> shots\*.bmp
H               hide/show the HUD
```

The window title bar shows nothing useful on purpose: the HUD overlay in the window carries fps, triangle
count, position, compass bearing, sun elevation, day clock and seed. Everything also goes to `town3d.log`.

Day length is 600 s. Sun elevation runs from **+69° at noon (clock 150) to −80° at midnight (clock 450)**,
so the night is genuinely dark, the stars come out and the windows in the buildings light up.

### Command line

| Flag | Effect |
|---|---|
| `--selftest` | Renders 7 fixed views into `shots\*.bmp`, prints frame statistics and a crowd audit, exits |
| `--seed N` | Town seed (default 1337) — same seed, same town |
| `--width N` `--height N` | Window size (default 1280x720) |
| `--clock S` | Start time on the 600 s cycle (default 60) |
| `--nomsaa` | No MSAA (plain pixel format) |
| `--noshadows` | Skip the shadow pass — A/B debugging for shadow artefacts |
| `--peds N` | How many pedestrians are about (default 120, `0` for an empty town, max 400) |
| `--showlog` | Keep the console visible |
| `--trace` | Log Win32 message traffic + a perf line every 240 frames |

## How it works

| File | What lives there |
|---|---|
| `src/gl_core.h/.cpp` | GL tokens + function-pointer loader (`wglGetProcAddress`, with the GL 1.1 import fallback) |
| `src/math3d.h` | `vec3` / column-major `mat4`, perspective/ortho/lookAt |
| `src/world.h/.cpp` | the whole town generator: rivers, bridges, 4 districts, streets, lamps, trees, clouds, collision boxes |
| `src/people.h/.cpp` | the pedestrians: street navigation graph, routes, avoidance, blocky bodies |
| `src/shaders.h` | the 5 GLSL programs (solid, depth-only, sky, water, cloud) |
| `src/main.cpp` | window, wgl bootstrap, day/night model, render passes, player, HUD, screenshots, self-test |

* **Town:** a 13×13 grid of 56 m blocks. Radial districts — glass towers 80–155 m in the core, 2×2 quarter-lot
  mid-rises around them, detached houses with hip roofs in the suburbs, scattered houses and trees at the fringe.
* **Rivers** occupy two street corridors, which is why no lot ever overlaps water and why **bridges can only
  exist where a street meets a river** — exactly the requirement. Every other corridor crossing is impassable.
* **Collision** is a list of AABBs; the player is a 0.45 m circle swept in 0.25 m sub-steps, so running at a
  railing at 6 m/s cannot tunnel through it. Water is unwalkable except on the 13 bridge decks.
* **Shadows:** one 2048² depth map from an ortho box centred on the player (165 m radius, texel-snapped so the
  map does not shimmer), 3×3 PCF, and the shadow term fades out before the box edge.
* **Windows** are not textures: the fragment shader draws a procedural grid per façade, with a per-cell hash
  that decides whether a window is lit at night.
* **Clouds** are ~90 groups of flat-tinted quads at 205–300 m, drawn in three tiled offsets so they wrap
  without tearing, with alpha fading out at the fog line.
* **People** plan on a graph of street intersections, not on the ground plane: a walker picks an errand,
  pathfinds over the lattice, and follows it. Links across a river exist **only** where a bridge deck is, so
  pedestrians cross at bridges because there is literally no other way — the same rule keeps them off lamp
  posts and trees, since a junction is a node only if the town's own collision test says it is free. Each body
  is 10 solid boxes (torso, head, hair, two arms, two legs, two shoes) drawn with the town's shader, so they are
  lit, fogged and shadow-casting like everything else.
* **Nobody shares space.** Soft steering makes them edge round each other; a hard pass afterwards guarantees
  overlap is zero, pushing along X or Z separately so a shove never puts somebody through a wall. The player is
  immovable in that exchange — walkers step around you, you never push one along, and you cannot walk through
  one either.
* **Feet on the ground.** There are five walkable heights within 18 cm of each other — fields, block paving,
  asphalt, bridge decks, river kerbs — so "how high is the floor here" is a question you ask the world
  (`World::surfaceAt`) and never a constant you assume. Walkers are re-seated on that surface every frame and
  your eye height follows it too, which is why a walker's legs stop disappearing into the road and you stop
  floating 12 cm above a bridge deck as you cross it.

## Verifying it without playing

```
bin\Town3D.exe --selftest
```

writes seven BMPs and, for each, a line of measurements — mean luminance, sky-vs-ground luminance, share of
near-black and blown pixels, a horizontal-contrast "is there geometry" figure, how many distinct colour
buckets appear, and how many walkers are within 60 m of that camera. That is what catches a black frame, an
empty scene or a blown-out sky without anyone having to look at a picture. Current run: 7/7 shots,
**0 GL errors**.

Before the pictures it also walks the crowd for 180 simulated seconds and audits it:

```
[crowd] 120 walkers, 157/169 walkable junctions, 268 street links | 119 walking 1 waiting 9 on bridge |
        66 crossings | overlaps 0 in-walls 0 in-water 0
[crowd] walk-through probe: closest approach 0.75 m against a 0.75 m body ok
[crowd] ground contact: worst foot error 0.000 m | standing on street 108 deck 12 kerb 0 block 0
[surface] street 0.05  block 0.00  field -0.04  kerb 0.14  deck 0.12 (deck centre 0.12)
```

The 12 unusable junctions are exactly the ones standing in open water, `66 crossings` says the routes really do
use the bridges, the three zeroes are the "they must not walk through each other / into walls / into rivers"
requirement, and the probe is "I cannot walk through them": walking into a person stops you at 0.75 m, which is
their 0.30 m radius plus your 0.45 m one. The last two lines are the ground-contact audit — every walker's feet
are exactly on the surface they are standing on (108 of them on asphalt, 12 on a bridge deck) — and the per-shot
lines log the player's own eye height, `1.75` m on a street and `1.82` m on the deck shot.

`tools/smoke.ps1` launches the real window, waits, reports CPU/RSS and kills it — a regression check for
"does the app still survive its own message loop" (it did not, once: see LEDGER.md ISS-019). Add
`-Extra "--peds 400"` to stress the crowd: still 60 fps, about 63 000 triangles a frame.

## Known issues

See **[`LEDGER.md`](LEDGER.md)** — it is the running work journal: what was coded, every design decision with
its reasoning, and the issue register (including the bugs found while getting the first frame on screen).
**[`CONTINUE.md`](CONTINUE.md)** holds the resume notes.

Night is currently very dark (`LEDGER.md` ISS-021), and the rivers are two big quads — ripples come from the
fragment shader, so they look fine, but there is no real wave geometry (ISS-022).

The crowd walks, crosses bridges and refuses to overlap, but it has no *life* yet: the same number of people are
out at midnight as at noon, there is no rush hour and nobody waits to cross (ISS-026). Pedestrians are also
animated on the CPU today, which is cheap enough at 400 walkers but is the first thing to instance if the crowd
grows (ISS-027). Kerbs and bridge mouths have no ramp geometry, so a walker snaps 9–14 cm when it steps onto a
kerb or a deck and your eye smooths over the same step (ISS-030).
