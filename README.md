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
| `--selftest` | Renders 6 fixed views into `shots\*.bmp`, prints frame statistics, exits |
| `--seed N` | Town seed (default 1337) — same seed, same town |
| `--width N` `--height N` | Window size (default 1280x720) |
| `--clock S` | Start time on the 600 s cycle (default 60) |
| `--nomsaa` | No MSAA (plain pixel format) |
| `--noshadows` | Skip the shadow pass — A/B debugging for shadow artefacts |
| `--showlog` | Keep the console visible |
| `--trace` | Log Win32 message traffic + a perf line every 240 frames |

## How it works

| File | What lives there |
|---|---|
| `src/gl_core.h/.cpp` | GL tokens + function-pointer loader (`wglGetProcAddress`, with the GL 1.1 import fallback) |
| `src/math3d.h` | `vec3` / column-major `mat4`, perspective/ortho/lookAt |
| `src/world.h/.cpp` | the whole town generator: rivers, bridges, 4 districts, streets, lamps, trees, clouds, collision boxes |
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

## Verifying it without playing

```
bin\Town3D.exe --selftest
```

writes six BMPs and, for each, a line of measurements — mean luminance, sky-vs-ground luminance, share of
near-black and blown pixels, a horizontal-contrast "is there geometry" figure, and how many distinct colour
buckets appear. That is what catches a black frame, an empty scene or a blown-out sky without anyone having
to look at a picture. Current run: 6/6 shots, **0 GL errors**.

`tools/smoke.ps1` launches the real window, waits, reports CPU/RSS and kills it — a regression check for
"does the app still survive its own message loop" (it did not, once: see LEDGER.md ISS-019).

## Known issues

See **[`LEDGER.md`](LEDGER.md)** — it is the running work journal: what was coded, every design decision with
its reasoning, and the issue register (including the bugs found while getting the first frame on screen).
**[`CONTINUE.md`](CONTINUE.md)** holds the resume notes.

Night is currently very dark (`LEDGER.md` ISS-021), and the rivers are two big quads — ripples come from the
fragment shader, so they look fine, but there is no real wave geometry (ISS-022).
