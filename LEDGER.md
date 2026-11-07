# Build Ledger — "Town3D" (OpenGL / Visual Studio C++)

Working journal for this project. One row/entry per unit of work so a human can
audit **what was coded**, **why**, and **what is broken or unverified**.

Brief (verbatim requirements):
> 3D game using OpenGL and Visual Studio C++ (Clang): humanoid player walks around a
> small town with WASD + mouse look. Buildings are rectangles, from single-storey
> houses to very tall blocks. Town centre = skyscrapers. No entering buildings.
> Rivers with crossable bridges. Low detail everywhere. Sun in the sky that casts
> shadows. Also clouds.

Later request (2026-11-07, after `v0.1`):
> Add people walking round the town, crossing bridges, avoiding each other. They don't
> need to interact with me, but I should not be able to walk through them. Also, they
> should not walk through each other. Simple minecraft type solid blocks for limbs and
> heads will suffice.

**Status at last save:** **builds and runs, with pedestrians.** All 9 source files plus `Town3D.sln` / `Town3D.vcxproj` /
`build.bat` are on disk; `build.bat Release` compiles with **clang-cl (ClangCL)** and links `bin\Town3D.exe`.
`--selftest` renders 7 preset views to `shots\*.bmp` with **0 GL errors** and now also audits the crowd
(routing, bridge crossings, overlaps, walk-through probe). The interactive window runs at a vsync-locked **60.0 fps**
at 1280x720 — 32 902 tris/frame with 120 walkers, 63 142 with 400. `v0.1` (pre-pedestrian) is tagged and pushed.
Not yet verified by a human: how the pictures actually *look* (the model has no image input, so verification is
numeric — see §5). Remaining polish: night brightness (ISS-021), ISS-012, ISS-018, ISS-023. Details in [`CONTINUE.md`](CONTINUE.md).

Legend — status: `done` `in-progress` `planned` `blocked` · severity: `high` `med` `low` `info`

---

## 1. Environment audit (evidence, not assumptions)

| Check | Command / source | Result | Consequence |
|---|---|---|---|
| IDE | `vswhere -latest -property displayName` | Visual Studio Community **2026** (install dir `...\Microsoft Visual Studio\18\Community`) | Project files must use the v18-era schema |
| MSBuild | `vswhere -find MSBuild/**/Bin/MSBuild.exe` | `...\MSBuild\Current\Bin\MSBuild.exe` | Scripted builds possible |
| MSVC toolset | `VC\Tools\MSVC` | `14.51.36231` (toolset `v145`) | Fallback compiler available |
| **Clang compiler** | `VC\Tools\Llvm\x64\bin\clang-cl.exe` | **present** (`clang.exe`, `clang-cl.exe`, `clangd.exe` …) | `ClangCL` toolset **in use** — `build.bat` auto-detects it and passes `-p:UseClang=true` (ISS-001, my first audit was wrong) |
| ClangCL toolset props | `MSBuild\Microsoft\VC\v180\` has `Microsoft.Cpp.Clang.props/targets` but **no `ClangCL` toolset folder** | toolset resolves only after the optional component is installed | Project defaults to ClangCL, falls back to MSVC |
| Windows SDK | `Windows Kits\10` | `10.0.26100.0`, `um\x64\opengl32.lib` present | OpenGL linkable, no packages needed |
| External libs (GLFW/GLEW/freeglut/CMake) | `cmake --version`, PATH | none / not on PATH | **Decision: zero-dependency Win32 + wgl + hand-written GL loader** (ISS-002 resolved by design) |

---

## 2. Design decisions (and the reasoning)

| # | Decision | Rationale |
|---|---|---|
| D-1 | Win32 window + `wglCreateContextAttribsARB` (OpenGL **3.3 core**), no GLFW/SDL/freeglut | Nothing to download or configure; `.vcxproj` links only `opengl32/user32/gdi32`. freeglut would need an include/lib dir the machine does not have. |
| D-2 | Hand-written loader `src/gl_core.*` (≈50 entry points) | SDK `<GL/gl.h>` stops at GL 1.1; shaders + FBOs + VAOs need 2.x/3.x pointers from `wglGetProcAddress`. Skips GLEW/GLAD (not present). Guards the `wglGetProcAddress` 1..4/-1 error sentinels. |
| D-3 | Shadows = single-pass **shadow map** (2048² depth texture, orthographic "sun" light, 3×3 PCF, polygon-offset bias), light box re-centred on the player and texel-snapped | Only way to get real, cheap shadows for a whole town; texel snapping kills the crawl/shimmer when the player walks. |
| D-4 | All buildings are **axis-aligned boxes** (± hip roof / setback / antenna), one static VBO | Matches "buildings are rectangles" + "low detail"; makes collision a flat 2D AABB list, so *entering a building is structurally impossible* (requirement). |
| D-5 | Collision: fixed eye height (no gravity/jumping), per-axis test → wall sliding; rivers are "not-walkable" AABBs with **bridge zones as explicit exceptions** | Deterministic, no tunneling at running speed, and the only walkable route over water is the bridges. |
| D-6 | Rivers are **street corridors widened to water**, so blocks never overlap water; ground quads are rect-subtracted against the river bands | Avoids land quads poking through the water surface (the obvious bug with a flat plane + river). |
| D-7 | Windows/facades are **procedural in the fragment shader** (world-space grid, per-building density + night glow), not textures | Keeps "low detail"/low asset count while the town still reads as a town; night windows fade in as the sun drops. |
| D-8 | Sky = full-screen triangle (gradient + sun disc + glow + stars), clouds = soft alpha quads (lit top / shaded underside), drifting and drawn in 3 tiled copies | Clouds wrap seamlessly without per-vertex `mod()` tearing; alpha fades with distance so the field edge is invisible. |
| D-9 | Toolset `ClangCL` (as the brief asks) with `build.bat` probing for `clang-cl.exe`; `v145` MSVC only as an explicit fallback | The brief says Clang, and clang-cl is installed here, so the shipped build *is* Clang. MSVC stays supported because a second machine may not have the component — both compile clean from the same sources. |
| D-10 | `--selftest` renders preset cameras to `.bmp` via `glReadPixels` | Gives *visual proof* the renderer works in a head-ish environment and lets me verify the town/shadows/clouds instead of assuming. |
| D-11 | Pedestrians navigate a **graph of street intersections** (13×13 nodes, 4-neighbour links), never a free 2D plane. Links across a river exist **only** where a bridge deck is, and a node is excluded if `World::blockedAt()` says it is occupied. Routes come from BFS. | A steering-only crowd wanders into the river and drowns on a railing. On the lattice, water is simply *not connected*, so a walker crosses at a bridge or not at all — no special-case "bridge" code to get wrong. Reusing `blockedAt()` means lamp posts and trees block pedestrians for free, exactly as they block the player. |
| D-12 | Bodies are 10 boxes each (torso, head, hair, 2 arms, 2 legs, 2 shoes) emitted into **one dynamic VBO in the same vertex format as the town**, drawn with the existing `solid` program in both the shadow and colour passes | Zero new shaders: bodies get lighting, fog, shadow mapping and procedural shading for nothing, and they cast real shadows. Minecraft-style blocks are what the request asked for. 108 tris per person is nothing next to the town's 19 942. |
| D-13 | Three layers instead of one: **steering** (soft avoidance: they edge round each other) → **hard non-overlap** (3 relaxation passes, symmetric push, axis-separated so nobody is shoved through a wall) → **player as immovable** (a walker always yields to you, never the reverse) | Soft forces alone let bodies touch at walking pace; a pure "push out" every frame looks magnetic. Splitting them means *nobody ever shares space* is a guarantee from the last pass, while normal walking still looks like walking. |
| D-14 | Broad phase = uniform **4 m hash grid rebuilt each frame** (linked-list buckets), so avoidance and overlap tests look at 9 cells, not the whole crowd | 400 walkers at O(n²) is 160 000 pair tests a frame for no reason. With the grid the frame stays vsync-locked; measured 60.0 fps at 400 walkers (`[perf]`, §5 V-8). |
| D-15 | Destinations are chosen from ~10 candidate junctions scored by *distance from the town centre + up to 420 m of chance*, minimum 4 hops away | A purely random target spreads 120 walkers so thinly over a 728 m town that a street is empty half the time. Biasing downtown (with plenty of slack) puts crowds where the buildings are, which is where a town looks populated. |

---

## 3. Work log

Newest last. "Verification" is what I actually ran, not what should work.

| # | Time | File / unit | Status | What was coded | Verification |
|---|---|---|---|---|---|
| 1 | 08:44 | `src/gl_core.h` | done | GL 2.0–3.3 tokens + WGL context/multisample tokens missing from the SDK header; `GLchar/GLintptr/GLsizeiptr` typedefs; `GLFunctions` struct with ~50 typed entry-point pointers. | Read-back review only; compiles with unit 2 (pending build B-1) |
| 2 | 08:45 | `src/gl_core.cpp` | done | Loader: `wglGetProcAddress` → `GetProcAddress(opengl32.dll)` fallback, `badProc()` sentinel filter, `LOAD()` macro aborting with the missing name printed. | pending B-1 |
| 3 | 08:47 | `src/math3d.h` | done | `vec3` ops, `mat4` column-major `Mul/Perspective/Ortho/LookAt`, `clampf/lerp/normalize`. Matches the OpenGL convention used by the shaders (no matrix inverse needed — sky rays are rebuilt from camera basis uniforms). | pending B-1 |
| 4 | 08:50 | `src/world.h` | done | Public data model: `Vertex` (pos/norm/col/win/glow, stride 44), `CloudVertex`, `AABB`, town layout constants (13×13 blocks, 42 m lots, 14 m streets, rivers on street corridors #8/#5, eye/collision radius), `World::build()`, `World::blockedAt()`. | pending B-1 |
| 5 | 09:05 | `src/world.cpp` | done (680 ln) | Deterministic generator (xorshift32 + `hash2(i,j,seed)` per block): 4 radial districts — core glass towers 80–155 m (podium/shaft/setback crown/antenna), mid-rise 16–58 m on 2×2 quarter lots, suburbs 6–13 m houses (70 % hip roofs), fringe houses + trees. Rivers with banks/quays, bridges + railings on every other corridor, single-y street lattice built by rect-subtraction (no coplanar overlap at crossings), lamp posts, 90-cloud field, spawn on a suburban street facing downtown, circle-vs-AABB `blockedAt()` with water/bridge-zone exemption. Prints a build summary for V-1. | pending B-1 |
| 6 | 09:18 | `src/shaders.h` | done (198 ln) | GLSL 330: solid (shadow + slope-scaled bias + 3×3 PCF + hemisphere ambient + procedural window grid with per-cell night lighting + fog), depth-only, full-screen-triangle sky (sun disc, glow, stars, haze), animated water (ripple normal, fresnel, glitter, alpha), cloud quads with distance-faded alpha. | pending B-1 |
| 7 | 09:22 | `src/main.cpp` | **part 1 of 3** (541 ln) | Written: logging to `town3d.log`, sun/sky model, shader build + uniform cache, mesh upload + 4 VAOs, `wndProc` (raw mouse / keys / size), wgl dummy-window bootstrap → MSAA pixel format → 3.3 core context, `glLoadAll`, shadow texture + depth-only FBO, global GL state. **Remaining: `render()`, `lightMatrix()`, player/camera, HUD, BMP capture, selftest, `main()`** — see `CONTINUE.md` §4 for the locked-in parameters and the 2 defects to fix in part 1. | — |
| 8 | — | `Town3D.vcxproj` / `.sln` / `build.bat` | not started | x64 project (Console subsystem so `main()`/printf work), `v145` default + `ClangToolset=ClangCL` override, C++17, `opengl32.lib;user32.lib;gdi32.lib`, auto-detecting build script. Spec in `CONTINUE.md` §6. | — |
| 9 | — | `README.md` | not started | Controls, build steps for both toolsets, how to install the missing Clang component, tuning knobs. | — |
| 10 | 09:30 | `CONTINUE.md` | done | Resume/handoff notes: file status table, the two known `main.cpp` part-1 defects, exact remaining functions + locked-in render/camera/light parameters, selftest shot table, project-file spec, verification plan. | — |
| 11 | 10:24 | `src/main.cpp` | **complete** (1072 ln) | Parts 2 + 3 appended: `forwardDir`/`lightMatrix` (texel-snapped 165 m ortho box), `render()` (2-pass: depth-only shadow map then sky → solids → water → clouds), circle-vs-AABB `movePlayer` with 0.25 m sub-steps so railings cannot be tunnelled, `lookAround` (raw input), `tickTime`, GDI `drawHud`, `captureBmp` (24 bpp, bottom-up), `FrameStats` frame analyser, `runSelfTest` (6 views), `regenerateTown`, `main()` with the full arg table and the frame loop. | ISS-013…ISS-017 |
| 12 | 10:38 | `Town3D.vcxproj`, `Town3D.sln` | done | x64 only. `PlatformToolset` = `v145` by default, `-p:UseClang=true` (or `-p:PlatformToolset=ClangCL`) switches the whole project to `ClangCL`; C++17, W4, Console subsystem (so `main()` + stdout logging work), `opengl32.lib;user32.lib;gdi32.lib`, output to `bin\`, obj per toolset so both compilers never share `.obj`. | ISS-001 |
| 13 | 10:38 | `build.bat` | done | `vswhere` → MSBuild, then **detects `clang-cl.exe`** and passes `-p:UseClang=true` when present, printing VS-Installer instructions when it is not. Args: `build.bat [Debug\|Release] [--run]`; default = Release + `--selftest`. | — |
| 14 | 10:40–11:20 | first compile → first frame | 5 fixes | Compile/link clean on the 4th pass, then four runtime failures diagnosed one at a time against the real AMD driver: no 3.x context (ISS-014), version query returning 0.0 (ISS-015), app quitting before frame 1 (ISS-016), sun never setting (ISS-017). | ISS-014…ISS-017 |
| 15 | 11:25 | `tools/smoke.ps1` | done | Launches the real window, waits, reports alive/dead + CPU + RSS, kills it. Turns "it runs" into a repeatable check (V-3). | — |
| 16 | 12:25 | GitHub push | done | Repo moved to `C:\Source\Qwen38FlashNextGame` and `main` pushed to **`github.com/nooil6152/Qwen38FlashNextGame`** (`540c035`, 15 files, +2 991 lines): all of `src/`, solution/project/build scripts, `tools/smoke.ps1`, and the three documents. Kept out: `shots/*.bmp` (16 MB — new `*.bmp` ignore rule, regenerable with `--selftest`) and, as before, `bin/`, `obj/`, `town3d.log`. Commit **author and committer are both the repo account**; checked through the GitHub API afterwards — correct authorship on the served commit, zero build artefacts or screenshots in the tree, and no personal names, mail addresses or local user-directory paths in any tracked file at any revision. | — |
| 17 | 12:36 | pre-flight before feature work | done | Confirmed `v0.1` was safely public *before* editing anything: `main` == `origin/main` == `91b4974`, annotated tag `v0.1` ("first working version") present on the remote, working tree clean. An unrelated `strata-config.json` appeared in the folder — left untracked; from here on `git add` takes **explicit paths** so it cannot be swept into a public commit. | — |
| 18 | 12:39–12:56 | `src/people.h`, `src/people.cpp` | done | New module, 578 lines. Navigation lattice over the street grid (walkability derived from bridge positions **plus** `blockedAt()`), BFS routes, downtown-biased destinations, steering + hard separation, `emit()` builds the blocky bodies, `blocks()`/`pushOut()` make them solid to the player, `simulate()`/`stats()` exist so the selftest can audit the crowd without a window. | D-11…D-15 |
| 19 | 13:05–13:20 | wire into engine | done | `main.cpp`: crowd built with the town (and on `R` regenerate), own dynamic VBO, drawn in the depth **and** colour passes, `playerFreeAt()` now refuses both walls *and* bodies, HUD shows walker count, `--peds N` (default 120, 0 disables), new shot `07_crowd_street` framed on the busiest corner, `Town3D.vcxproj` lists the new files. | — |
| 20 | 13:10–13:30 | verification | done | Two bugs caught while building (ISS-023 `near` is a `windows.h` macro; ISS-024 my clamp statements cut the `else if` argument chain). Selftest: 157/169 junctions walkable (the 12 missing are exactly the ones standing in open water), 66 river crossings in 180 simulated seconds, **0 overlaps / 0 in walls / 0 in water**, walk-through probe stops at 0.75 m. Interactive: 60.0 fps at 120 and at 400 walkers. | ISS-023, ISS-024, V-7, V-8 |
| 21 | 14:06 | tag `v0.2` | done | Annotated tag **`v0.2`** created on the pedestrian commit `8a36c80` and pushed. Message: *"v0.2 - pedestrians walking the town"* (kept as the tag annotation because a git refname cannot contain spaces); tagger identity taken from the repo-local `user.*`, so it matches the commits. **Unsigned** — no signing key on this machine, so GitHub shows no *Verified* badge, same as `v0.1`. `v0.1` (`91b4974`) is left where it was, so the pre-pedestrian build stays reachable. | §3a |

### 3a. Identity hygiene on the push

This repo's owner account is **not** the account this machine is set up for: the global
git identity, the active `gh` login and the cached Git Credential Manager entries all
belong to a different GitHub user. Nothing from that account reached the repo:

- `user.name` / `user.email` were set **repo-locally only** — global config untouched;
- `origin` is pinned as `https://nooil6152@github.com/…` so a cached credential for the
  other account can no longer match the remote and be used silently;
- the device-flow login ran in a throwaway `GH_CONFIG_DIR`, so the existing `gh` session
  was never replaced or made inactive;
- the push itself used a one-shot `credential.helper` reading an environment variable —
  the token never entered a remote URL, a command line, `.git/config` or global config.

Left behind, and removable: a `gh:github.com:nooil6152` entry in Windows Credential
Manager, and the throwaway config directory (`GH_CONFIG_DIR=<dir> gh auth logout
--hostname github.com`, then delete the directory).

---

## 4. Issue register

What is wrong / risky, with current state. Nothing here is hypothetical hand-waving — each item is either reproduced, or a named risk with its mitigation.

| ID | Sev | Found in | Issue | Status | Mitigation / fix |
|---|---|---|---|---|---|
| ISS-001 | **high** | Env audit | **"Visual Studio C++ (Clang)" toolset availability.** The first audit (08:37) found no `clang-cl.exe` and concluded the Clang component was missing. **That audit was wrong** — `VCToolslvmdbinang-cl.exe` (and `clang++.exe`) are present; the earlier scan only looked at a partial listing. | **closed** | nothing to install. `build.bat` probes for `clang-cl.exe` and passes `-p:useclang=true`, so today **the shipped build is clang-cl (clangcl)** — confirmed by `build.bat` output line `msbuild ... -p:useclang=true`. `-p:useclang` omitted ⇒ `v145` msvc, which also compiles clean (the first green build was msvc). |
| ISS-002 | med | Design | No 3rd-party libs on this machine (no CMake/GLFW/GLEW) → a tutorial-style `freeglut`/GLFW project would not compile out of the box. | closed | D-1/D-2: pure Win32 + wgl + own loader. Only `opengl32.lib user32.lib gdi32.lib`. |
| ISS-003 | med | Design | A river laid over a single big ground plane hides under it (water at y<0 with ground at y=0 → invisible river) — the classic failure mode for this kind of scene. | closed by design | D-6: no full-size ground plane under the water. Ground is per-block/per-street quads, rect-subtracted against the river bands; the outer terrain is 4 corner aprons that leave slits for the rivers; trench walls + riverbed close the channel. |
| ISS-004 | med | Design | Shadow acne / peter-panning, and shadow crawl while walking. | in-progress | Polygon offset in the depth pass + small constant bias in the sampler, `GL_COMPARE_REF_TO_TEXTURE` for hardware PCF, ortho box re-centred on the player **and snapped to shadow texels**, plus a generous ortho radius so the box edges never cross the town. Watch-list for visual check in `--selftest`. |
| ISS-005 | low | Design | GDI text HUD over an OpenGL window can flicker on some drivers. | open (risk) | HUD drawn *after* `SwapBuffers` on the same HDC; it is a cosmetic overlay only — if it flickers on the user's GPU, deleting the `drawHud()` call removes it with zero impact on the game. |
| ISS-006 | low | Design | Cloud field edge visible near the horizon, and per-vertex `mod()` drift would tear quads at the wrap seam. | closed by design | D-8: 3 tiled draw calls of the whole field (no per-vertex modulo) + distance-based alpha fade. |
| ISS-007 | low | `gl_core.h` | Left-over junk token `GL_COLOR_BUFFER_BIT1` (noise, wrong value) and duplicate `GetIntegerv`/`GetString` pointers shadowing the GL 1.1 SDK prototypes. | **closed** | Token deleted; the two duplicated pointers removed from `GlApi`/`glLoadAll` — GL 1.1 queries now come straight from `opengl32.lib`. Also added a guarded `GL_SAMPLES` define (absent from the SDK's 1.1 `gl.h`). |
| ISS-010 | med | `main.cpp` part 1 | Two defects introduced in the code written just before the shutdown: (a) stray no-op statement `if (g_selftest) style == style;` in `initGL`, (b) `setCursorHidden()` counter logic was inert, so the mouse cursor would never hide. | **closed** (at save point, unverified by compile) | (a) removed — note the repair edit briefly clipped the `CreateWindowExA` call; re-verified by reading `initGL()`. (b) replaced with a `depth` counter. `<algorithm>` include added for `std::max`. |
| ISS-011 | med | `shaders.h` | Shadow fade window (130→178 m) could extend past the guaranteed coverage radius of the 165 m ortho box, so a faint lit seam might appear on open ground. | closed (pending V-1 visual check) | Fade moved to `smoothstep(118.0, 158.0, d)`, i.e. it completes strictly inside the covered radius. |
| ISS-012 | low | `world.cpp` | `build()` only re-orients the spawn when the spawn point is blocked; it never relocates it. | **closed** | Walks along the corridor centre-line (±2 m steps, 60 tries, inside the walk limit) until `blockedAt()` is free, then re-derives `spawnYaw`. Evidence: every selftest shot reports `blockedAt=0` and the interactive spawn is `-133.0, 259.0` on a street. |
| ISS-013 | low | `main.cpp` part 2 | Five call sites called `glUseProgram(…)` bare. The SDK header is GL 1.1 and has no such declaration — only the loader has it. | **closed** | `sed` to `gl.UseProgram`; audited every remaining bare `glXxx` in the file against the 1.1 import list (`glClear`, `glViewport`, `glReadPixels`, …) — all 1.1, so they are legitimately imported. |
| ISS-014 | med | `gl_core.h` | Loader declared `CheckFramebufferStatus` as returning `void` (it returns `GLenum`), so `GLenum st = gl.CheckFramebufferStatus(…)` would not compile — and the shadow-FBO status check is exactly what that return is for. | **closed** | Signature corrected. Runtime proof: `[gl] shadow map 2048x2048, FBO complete (0x8cd5)`. |
| ISS-015 | low | `main.cpp`, `world.cpp` | 4 sites wrote `vec3(scalar)` / `mulc(v, vec3(s))`, but `vec3` only has a 3-float constructor — the broadcast form I kept assuming exists. | **closed** | Switched to the existing `operator*(vec3, float)` rather than adding a silent broadcast ctor (which would also let typos like `vec3(x)` compile forever). |
| ISS-016 | med | `main.cpp` | `std::max(0.0f, …)` → C2589 “illegal token on right side of ‘::’”: `windows.h` defines `max`/`min` as function macros. | **closed** | `NOMINMAX` defined before `<windows.h>` in both `main.cpp` and `gl_core.h` (a header must not leak the macros onto whoever includes it next). |
| ISS-017 | **high** | `main.cpp` wgl bootstrap | `wglGetProcAddress` only answers **while a context is current**. `wglCreateContextAttribsARB` was fetched after the bootstrap context had been deleted → NULL → “ancient driver?” and no 3.x context on a current AMD card. | **closed** | Pointer now grabbed together with `wglChoosePixelFormatARB` while the 1×1 bootstrap context is still current; plus a throwaway-context fallback for the `--nomsaa` path that never creates the dummy. Evidence: `[gl] context 3.3 … AMD Radeon RX 9070 XT \| MSAA 4`. |
| ISS-018 | med | `main.cpp` init | `GL_MAJOR_VERSION`/`GL_MINOR_VERSION` read back as `0.0` through **both** the `opengl32.lib` import and the `wglGetProcAddress` pointer on this AMD driver (with `GL_INVALID_ENUM` pending), although the context really is 3.3 core — so the version guard aborted init on a working context. | **closed** (worked around) | Init now takes `GL_MAJOR_VERSION` if the driver answers it, otherwise parses `glGetString(GL_VERSION)` (“3.3.0 Core Profile Context …”). The pending error is consumed by the log line so it cannot masquerade as a later failure. Kept as *known driver oddity*, not a code bug. |
| ISS-019 | **high** | `main.cpp` `wndProc` | The hidden 1×1 window that bootstraps `wglChoosePixelFormatARB` shares `wndProc` with the game window. Its `WM_DESTROY` ran `PostQuitMessage(0)`, so the message loop quit **before the first frame**: `[win] WM_DESTROY` … `[exit] loop ended after 0 frames`. | **closed** | Only the real window (`h == g_hwnd`) may end the app; the bootstrap window’s `WM_DESTROY` returns 0. Found by adding `--trace` plus a frame counter to the exit log — the frame count is what made it obvious nothing had ever drawn. |
| ISS-023 | low | `main.cpp` selftest | `int near = 0;` inside the crowd framing code failed to compile with "expected unqualified-id" — `near` (and `far`) are macros defined by `windows.h`, so the declaration evaporated. | **closed** | Renamed to `nearby`. Worth remembering: any local in a file that includes `windows.h` must avoid `near`, `far`, `small`, `large`, `interface`, `getStatus`. |
| ISS-024 | med | `main.cpp` `main()` | While adding `--peds` I put the two clamp statements (`if (g_pedCount < 0) …`) **between** links of the argument `else if` chain. That split the chain, so almost every flag fell through to `else printf("ignoring argument …")` — the log had been printing `ignoring argument '--selftest'` on every run since the flag was added. Values were still assigned, so behaviour looked fine and only the noise gave it away. | **closed** | Clamping moved after the loop; `--peds 0` now runs a 6-shot selftest with `pedtris 0` and no "ignoring" line anywhere (regression-checked in §5 V-7). Lesson: an `--help`/unknown-argument message is a test — read it, do not scroll past it. |
| ISS-025 | info | `people.cpp` | Pedestrians reuse `World::blockedAt()`, which tests a circle of `TW_PLAYER_R` = 0.45 m, while a walker's own radius is 0.30 m. | accepted | Deliberate: the stricter test means a walker never ends up wedged where the player would later be unable to pass. Costs a little clearance on the 15.2 m bridge decks and 14 m streets, which is not observable. |
| ISS-026 | med | crowd behaviour | Walkers are oblivious to the time of day: 400 people stroll the streets at midnight, and there is no rush hour, no crossing-wait, no benches. Nothing "lives" beyond walking a route. | **open** | Cheap fixes if wanted: scale the walker count by hour of day (recycle some into `wait` at night), and weight destinations toward whatever district is awake. Left out of this pass because the request was movement and solidity, not schedules. |
| ISS-027 | low | crowd rendering | Every walker is transformed on the CPU (10 boxes × 6 verts) and re-uploaded each frame, so the crowd costs ~13 k tris of vertex traffic at 120 walkers. | **open** — headroom | Fine at present (60.0 fps at 400 walkers, 1.7 s CPU over 12 s wall). If it ever matters: instanced drawing with a per-ped model matrix, or a walk/idle animation baked per-variant. Not worth the complexity at this scale. |
| ISS-021 | med | night look | Ground luminance at dusk/night is ~5/255 (`dark` 43–48 % of pixels): buildings read as silhouettes and only the lit windows carry the picture. Lifted once already (ISS-020) but still dark. | **open** — cosmetic | Options, cheapest first: (a) raise `ambNight`/`gndNight` again, (b) give the 24 lamp posts a real point light or a baked ground pool of light, (c) brighten the window-glow term so lit windows bloom onto façades, (d) accept it as mood. Needs a human call on how dark “night” should be. |
| ISS-022 | low | `world.cpp` water | The two rivers are 6 triangles in total (one quad each). Ripples still show because `FS_WATER` computes its normal per fragment, but the vertex-level displacement is interpolated flat and there is no shoreline detail. | **open** — cosmetic | If real wave geometry is wanted, emit a ~6 m grid per river (≈1.4 k tris) instead of 2 quads. Deliberately not done yet: it costs nothing to change later and buys nothing visible at eye height 1.7 m. |
| ISS-020 | med | `main.cpp` `skyFor()` | Sun elevation was `0.12 + 1.15·max(0, sin(az))`: the `max` clamp parked the sun 7° above the horizon all night — a “night” sky with a sun disc on it, no stars, and midnight *brighter* than dusk (ground luma 4.1 both). | **closed** | Elevation is now `1.30·sin(az) − 0.10` → +69° at noon, **−80° at midnight**; sun intensity floors at 0 below the horizon; night/dusk ambient lifted (still dark, see ISS-021). Selftest now logs sun −80.2° / night 1.00 at clock 450. |
| ISS-008 | info | Design | Eye height is fixed (no gravity/jump/step). | accepted | Requirement is "walk around", not platforming. Keeps collision exact; noted so it is not mistaken for a physics bug. |
| ISS-009 | info | Design | ~~Sun never fully sets~~ **superseded by ISS-020**: the sun now really sets (−80° at midnight), so the shadow pass gets a light direction below the horizon and simply contributes nothing there (`max(dot(N,L),0)` floors it). Shadow-map conditioning stays safe because the ortho box is only used while the sun is up. | accepted | The old clamp existed to protect the ortho box; with the real cycle the guard is the sun-intensity floor at 0, which also stops the shadow map being sampled for light that does not exist. |

---

## 5. Verification log

Built and run on 2026-11-07. Instruments: `town3d.log` (every init step + per-shot frame statistics), `--selftest` (7
fixed cameras → `shots\*.bmp`, plus a crowd audit), `tools/smoke.ps1` (launch/kill the real window, `-Extra "--peds 400"`
to stress it).

| ID | What | Result |
|---|---|---|
| B-1 | Compile x64 **MSVC v145** | **green** — `bin\Town3D.exe` links; W4 clean apart from one `C4458` (local `clouds` hid `World::clouds`, renamed). |
| B-2 | Compile x64 **clang-cl (ClangCL)** | **green** — this is what `build.bat` picks by default here; same sources, no `#ifdef` per compiler. |
| V-1 | `--selftest` — 6 views | **0 GL errors**, 6/6 BMPs written. Day views luma 37.9 / 50.4 / 50.5, dusk 20.5, night 26.7, looking-up-at-clouds 131.6; `dark` (near-black px) 0 % by day, `blown` 0 % everywhere, `detail` (mean horizontal luma delta in the geometry band) 1.4–2.0, distinct colour buckets 313–435. Nothing black, nothing flat, nothing blown. |
| V-2 | World build numbers (seed 1337) | 330 buildings, 453 trees, 24 lamps, 13 bridges, 17 688 solid + 6 water + 2 248 cloud tris, 845 collision blockers, 2 water rects, 13 bridge walk-zones. |
| V-3 | `blockedAt()` at all 6 shot positions | **0 = free** in all 6 — including the bridge-deck shot, which is the case that used to be wrong (water exemption swallows the deck). |
| V-4 | Interactive window (`tools/smoke.ps1`) | **alive after 7 s**, 98.9 MB RSS, killed cleanly; `[perf] frame 240 60.0 fps` (vsync-locked) at 1280x720 with 19 942 tris/frame; day clock advanced 60.0 → 63.9 so `tickTime` runs. |
| V-5 | Play-test by a human — *does it look right*, can you walk into a house, are the bridges the only crossings, do the shadows track the sun | **not done — needs your eyes.** The numbers prove pixels and geometry; they cannot prove the town looks like a town. |
| V-6 | Debug aids available | `--trace` (Win32 message traffic + a perf line every 240 frames), `--noshadows` (A/B the shadow pass), `--nomsaa`, `--seed N`, `--clock S`, `--peds N`, `P` = screenshot mid-game. |
| V-7 | Crowd audit inside `--selftest` (180 s of simulated walking, then measured) | **157/169 junctions walkable** — the 12 excluded are precisely the nodes standing in open water, which is the nav graph saying *the river is not walkable except on a bridge*. **66 river crossings** by 120 walkers, so routes really do use the bridges. **0 overlapping pairs, 0 inside walls, 0 in water** ⇒ nobody walks through each other or falls in. **Walk-through probe:** player pushed straight at a held-still walker for 300 steps gets no closer than **0.75 m**, exactly the sum of the body radius (0.30) and the player radius (0.45) ⇒ you cannot walk through a person. `--peds 0` gives 6 shots and `pedtris 0`, and no `ignoring argument` line anywhere (ISS-024 regression check). |
| V-8 | Crowd cost (interactive, `tools/smoke.ps1`) | 120 walkers: **60.0 fps**, 32 902 tris/frame, 115 MB, 0.78 s CPU over 9 s wall. 400 walkers (`--peds 400`): **60.0 fps**, 63 142 tris/frame, 118 MB, 1.66 s CPU over 12 s. Still vsync-locked, so the crowd is not the bottleneck. Shot 07 in-frame numbers: 12 960 ped tris drawn, 14 walkers within 60 m, nearest 3.9 m. |
| V-9 | Crowd visible in the pictures, A/B | Same fixed camera, crowd off then on. River-and-skyline shot: ground-band luma 19.0 → 19.7, detail 1.95 → 2.01, distinct colours 374 → 407. Downtown street: colours 361 → 372. Small but unambiguous change at ground level only — consistent with people being drawn, not with a global lighting shift. |

---

## 6. Requirement traceability

| Requirement | Where it is implemented | State |
|---|---|---|
| OpenGL + VS C++ (Clang) | `Town3D.vcxproj` (`ClangCL` default), `src/gl_core.*`, `build.bat` auto-detects clang-cl | **done** — B-2, builds and runs |
| Humanoid player, WASD | `main.cpp` `movePlayer()` (eye height 1.7 m, walk/run, sub-stepped collision) | **done** — V-3, V-4 |
| Mouse look | raw input (`WM_INPUT`) yaw/pitch, pitch clamped, no cursor warping | **done** — runs; feel needs your review (V-5) |
| Town of rectangular buildings, houses → very tall blocks | `world.cpp` districts: core towers ~80–155 m, mid 16–58 m, suburbs 5–13 m houses (hip roofs) | **done** — V-2 (330 buildings) |
| Town centre = skyscrapers | height/density by distance from (0,0); setbacks + antennas in the core | **done** — V-1 shot 01/03 |
| Cannot enter buildings | every footprint in `World::blockers`, per-axis AABB test → impossible to pass a wall | **done** — V-3; 845 blockers |
| Rivers with crossable bridges | two rivers on widened street corridors; 13 bridges with decks, solid railings, `bridgeZones` walk exceptions | **done** — V-2, V-7 (walkers cross only at bridges) |
| Low detail | boxes + flat colours + a handful of quads; no textures, no meshes loaded; 19 942 tris for the whole town | **done** — by design |
| Sun in the sky casting shadows | sun disc + glow in sky shader; 2048² shadow map + 3×3 PCF; animated day/night, T/[ ] to control | **done** — V-1 (sun +38° day, −80° midnight) |
| Clouds | drifting alpha-quad cloud field with lit top / shaded underside | **done** — V-1 shot 06 |
| *(added later)* People walking round the town | `people.cpp` nav lattice + BFS routes, downtown-biased errands | **done** — V-7 |
| *(added later)* Crossing bridges | river links exist in the graph only at bridge decks; 66 crossings logged in 180 s | **done** — V-7 |
| *(added later)* Avoiding each other | separation steering + 3-pass hard non-overlap on a 4 m grid | **done** — V-7 (0 overlapping pairs) |
| *(added later)* Player cannot walk through them | `playerFreeAt()` = world **and** `People::blocks()`; they yield to you, you cannot shove them | **done** — V-7 probe, 0.75 m |
| *(added later)* They must not walk through each other | same hard pass, axis-separated so a push never enters a wall | **done** — V-7 (0 in walls) |
| *(added later)* Minecraft-style solid blocks for limbs/heads | `People::emit()`: 10 boxes per body (torso, head, hair, 2 arms, 2 legs, 2 shoes), flat colours | **done** — V-8, V-9 |
