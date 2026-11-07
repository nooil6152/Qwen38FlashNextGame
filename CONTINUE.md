# CONTINUE.md — Town3D resume notes

**Last saved:** `v0.3` = **`4d6c83a`** (ground following for walkers and player), tagged and pushed. Repo
**`github.com/nooil6152/Qwen38FlashNextGame`**, `main` is that commit plus a docs commit. Older tags: `v0.2` =
`8a36c80` (pedestrians, pre-fix), `v0.1` = `91b4974` (first working build).
**What `v0.3` is:** it **runs** — that is the bar for a tag here, not correctness. `build.bat Release` compiles with
clang-cl; `--selftest` = 7 shots + crowd audit, 0 GL errors; interactive = 60 fps at 120 and 400 walkers.
**What is still broken:** ISS-031 every BMP is saved vertically mirrored, and ISS-032 walkers still read as sunk —
and ISS-032 exists because **V-10 was a circular test**: it checked `pos.y` against `surfaceAt()`, the very function
the fix uses, and never once looked at the triangles that reach the GPU. Companion document: `LEDGER.md` (work
journal, D-1…D-16, ISS-001…ISS-032, V-1…V-10, and **§0a - the prompt log**).

**Work happens in `C:\Source\Qwen38FlashNextGame` — that is the git checkout.** `C:\Source\game` is the *stale copy
from before the project was moved*: no `.git`, older sources, no `people.*`, no `tools\`. Editing there loses work
silently. If in doubt, `git -C <path> rev-parse --show-toplevel` before touching anything.

**Next thing to do:** work §0 below, in that order.

## 0. RESUME HERE (written 15:39, session ended by the user mid-investigation)

Checkpoint: working tree clean at **`main` = `6cfa871`** (docs) on top of **`v0.3` = `4d6c83a`** (code). Both pushed.
`bin\Town3D.exe` on disk is built from `4d6c83a` (linked 15:00:51, newer than every source) and is known to run:
selftest 7 shots / 0 GL errors, interactive 60.0 fps with `eye 1.75` logged. **Nothing is half-edited.** Start by
rebuilding to prove the toolchain is still sane: `cmd //c build.bat`.

### 0.1 Job one — ISS-031, the mirrored screenshot (one line, do it first)

`src/main.cpp`, `captureBmp()` (function starts line 1015). The loop at 1031 says *"BMP is bottom-up, GL is bottom-up
too"* — correct — and then contradicts itself on line **1034**:

```cpp
unsigned char* dst = &out[(size_t)(h - 1 - y) * row];   // WRONG: reverses rows that already match
unsigned char* dst = &out[(size_t)y * row];             // FIX: GL row y -> file row y
```

Leave `analyzeFrame()` alone (line 968, `skyFrom = h*2/3` with the comment "row 0 is the screen bottom") — it reads
the GL-ordered buffer, so every statistic ever logged is right; only the files lie. Header is `biHeight > 0`
(putU32 at 1050), i.e. bottom-up, which is what the fix assumes.

Verify, in this order: (1) rebuild; (2) `--selftest`; (3) check orientation numerically with node (no python on this
box) — for a street shot the *stored* bytes must now run dark→bright with increasing file offset, and the mean luma of
the last third of the pixel data must match the logged `sky` figure within ~2:

```js
// flipcheck.mjs  -  node flipcheck.mjs bin/shots/01_downtown_street.bmp
const b = require('fs').readFileSync(process.argv[2]);
const w = b.readInt32LE(18), h = b.readInt32LE(22), off = b.readInt32LE(10);
const row = ((w * 3 + 3) / 4) * 4;
const rowLuma = y => { let a = 0, n = 0;
  for (let x = 0; x < w; x += 7) { const s = off + y * row + 3 * x;      // stored B,G,R
    a += 0.2126 * b[s + 2] + 0.7152 * b[s + 1] + 0.0722 * b[s]; n++; }
  return a / n; };
let bot = 0, top = 0;
for (let y = 0; y < h / 3; ++y) bot += rowLuma(y);            // first file rows = image BOTTOM
for (let y = Math.floor(h * 2 / 3); y < h; ++y) top += rowLuma(y);
console.log('image bottom band', (bot / (h / 3)).toFixed(1),
            ' image top band', (top / (h / 3)).toFixed(1));
// fixed file    =>  top band ~= logged `sky` (~73 in daylight), bottom ~= logged `gnd` (~18)
// mirrored file =>  the two numbers swap
```

Regenerate all screenshots afterwards, and treat **every** BMP written before the fix as mirrored — including the
user's reference capture `bin\shots\manual_26200031.bmp` and everything in `shots\`.

### 0.2 Job two — ISS-032, walkers still read as sunk

Already eliminated by reading, **do not re-check**:
- `People::emit()` (people.cpp:116) builds all 10 boxes at local y **0 … 1.765 above `feet = p.pos`** (line 125), and
  `addBox`'s `toWorld` (48–62) is swing-about-x → yaw-about-y → `+feet`. No negation, no offset.
- `p.pos.y = w.surfaceAt(...)` is people.cpp:**527**, in its own final loop over *all* peds after every push. The
  `continue`s in the main steering loop cannot skip it, and `update()` only bails early on `dt <= 0 || peds.empty()`.
- `VS_SOLID` (shaders.h:9–23) passes `aPos` through untouched — no camera-relative maths, no y flip — and the ped VAO
  uses the same stride/offsets as the static VAO (`kStride`, float offsets 0/3/6/9, main.cpp:321–333).
- `g_pedTris` is a steady 12 960 = 120 × 108, so `emit()` is not accumulating stale vertices.
- The mirror in ISS-031 **preserves** ground contact, so it cannot explain sinking on its own.

So the disagreement is between `surfaceAt()` and the geometry, or between `pos.y` and what reaches the GPU. Write
**both** audits; neither may use `surfaceAt()` as its reference — that circularity is what made V-10 worthless.

**(a) Is the drawn ground where `surfaceAt()` says it is?** Ray-cast the actual triangle soup, `World::solid`
(`Vertex` is 44 bytes, triangles are consecutive triples, `g_solidTris*3` verts; `World::water` is a separate
vector so it is excluded automatically):

```
for each triangle: project to XZ; skip if |2D area| < 1e-6 (near-vertical faces)
                   if (x,z) inside the projected triangle (same-sign edge tests, allow 1e-4 slop)
                   solve the plane for y at that point (barycentric interpolation)
highest hit = meshTopAt(x,z)
```

Sample and print `surfaceAt` vs `meshTop` side by side, worst offender with its coordinates: street centre lines
(`-364+56i+7`, both axes), street edges (±6.9 from centre), block interiors, deck centres (`bridgeZones`, and 0.5 m
inside each edge), kerb bands (`|x - 91|` ∈ [11, 12.05] for the vertical river, `|z + 77|` ∈ [14, 15.05] for the
horizontal), and 20 random ped positions. **Pass: |diff| < 0.01 m everywhere.** Watch specifically for a paving or
kerb quad drawn *over* the asphalt (then the true top is higher than `surfaceAt()` says = a walker that looks sunk
while every existing test stays green).

**(b) Is the body where `pos.y` says it is?** Right after `emit()` in the selftest, walk `g_pedMesh` ped by ped
(108 verts each, in emit order) and for ped *k* record min/max vertex y; assert
`|minY - peds[k].pos.y| < 0.005` and `|(maxY - peds[k].pos.y) - 1.765| < 0.02`, and print the worst walker. Expect
sole at exactly `pos.y` and hair top at `pos.y + 1.765`.

**(c) Cross-check on pixels.** Add a shot that pins one walker 4 m in front of a camera at eye height on a street, then
measure the ped blob in the corrected BMP by colour mask (palettes: `kSkin`/`kHair`/`kShirt`/`kTrousers`/`kShoes`,
people.cpp:25–42; skin is the only strongly warm `R ≫ B` colour in the scene). A body should span
`px = 1.765 · (h/2) / (d · tan(35°))` — the projection really is **vertical** fov 70° (`mat4Perspective(fovyRad,…)`,
`m[5] = 1/tan(fov/2)`, main.cpp:678), so that is **≈ 227 px for a walker at 4 m in a 720 px window**. Recompute it if
the shot distance or window height changes rather than trusting the number. If the blob is far shorter, or its bottom
row sits above where the road meets it, the bug is in the draw path and (a)/(b) will say which.

### 0.3 Job three — make the user's screenshots reproducible

Manual captures (key press, main.cpp:1355–1359 → `shots/manual_<GetTickCount()>.bmp`, which lands in `bin\shots\`
because that is the cwd) log only the luma. Add one `logfmt` with pos, yaw, pitch, clock, `--peds` count and the
nearest ped's distance, so "look at manual_xxx.bmp" becomes a scene that can be re-rendered and measured instead of
debated. (Their capture `manual_26200031.bmp` from 15:18 is the ISS-032 evidence and is unreproducible as it stands.)

### 0.4 Facts that cost me time today

- `stat -c %Y` is mtime; **`%s` is file size**. My first "is the binary current?" check compared mtimes to the size and
  "passed" all nine sources. Check again with `%Y`, or `find src -newer bin/Town3D.exe`.
- `--peds 0` **skips the whole crowd-audit block** — no `[crowd]`, no `[surface]`, only 6 shots. Never use it to check
  that the audits work.
- No python on this machine (Store stub only). **node v22 works** and is the right tool for BMP forensics: 54-byte
  header, `off=readInt32LE(10)`, rows padded to 4 bytes, bottom-up, stored **B,G,R**.
- `read` on an image returns *"model does not support images"* — no amount of BMP→PNG conversion makes seeing
  possible. All verification here must be numeric.
- `build.bat` = Release + selftest (~40 s). `tools/smoke.ps1 -Seconds N [-Extra "--peds 400"]` drives the real window.
- This shell still starts in the stale `C:\Source\game`. `cd /c/Source/Qwen38FlashNextGame` first, always.

### 0.5 Release workflow — the user's standing rule, every green build

"Working" means **it runs**, not *it is correct*: open bugs never hold a tag back. After each successful build:
commit (explicit paths, never `-A` — a `strata-config.json` lives in the tree and must stay untracked), push, cut an
annotated tag, verify the remote, leak-scan, then **tell the user it is done** and beep.

```bash
git add <paths> && git -c user.name="nooil6152" -c user.email="nooil6152@outlook.com" commit -F - << 'MSG' … MSG
git -c user.name="nooil6152" -c user.email="nooil6152@outlook.com" tag -a v0.4 <sha> -m "v0.4 - <what>; builds and runs"
export GH_CONFIG_DIR="$LOCALAPPDATA/Temp/gh-nooil"   # the ISOLATED gh login dir, not the default one
NOOIL_TOKEN=$(gh auth token)
git -c credential.helper= -c credential.helper="!f(){ echo username=nooil6152; echo password=$NOOIL_TOKEN; };f" push origin main v0.4
git fetch -q origin --tags && git ls-remote --heads --tags origin
# scan all revisions for the identity patterns in LEDGER 3a (real name + personal email);
# never spell those strings into a tracked file, or the doc becomes the leak. must print 0
powershell -NoProfile -Command "[console]::beep(880,180); [console]::beep(1175,260)"
```

Tags are **unsigned** (no key on this box) so GitHub shows no *Verified* badge — expected, not a fault. Never push as
the machine default identity. Then `LEDGER.md`: work-log row + issue rows + a §0a prompt-log line for every
user prompt that changed code (git prompts and anything naming the user are excluded — published repo).

### 0.6 After ISS-031/032

Then, in order: ISS-021 night too dark (`gnd` ≈ 5/255, needs the user's taste), ISS-026 crowd has no daily rhythm,
ISS-030 kerb/deck steps need ramp quads, ISS-027 instance the crowd, and the §6 requirement play-test in LEDGER.

---

## 1. What the project is

First-person walk-around of a procedurally generated low-poly town. OpenGL 3.3 core, plain Win32 + wgl,
**zero third-party libraries** (hand-rolled GL loader). Requirements checklist: `LEDGER.md` §6.

## 2. Environment (verified twice — the first audit was wrong, do not re-audit)

- VS Community **2026 (v18)**, `C:\Program Files\Microsoft Visual Studio\18\Community`
- MSBuild `…\MSBuild\Current\Bin\MSBuild.exe`; MSVC **v145** (14.51.36231); SDK **10.0.26100.0**
- **clang-cl IS installed**: `…\VC\Tools\Llvm\x64\bin\clang-cl.exe`. `build.bat` detects it → `-p:UseClang=true`.
  (The 08:37 audit claimed the opposite; corrected in `LEDGER.md` ISS-001. Both toolsets compile clean.)
- GPU **AMD Radeon RX 9070 XT**, driver 32.0.32015.7029, GL **3.3 core context OK**. Two displays (3440x1440 + 1920x1080).
- `glGetIntegerv(GL_MAJOR_VERSION)` returns **0** on this driver even in a real 3.3 core context (both the
  opengl32 import and the wgl pointer, with `GL_INVALID_ENUM` left pending) → `initGL` parses `GL_VERSION`
  instead. Do not "fix" this by re-adding a hard version-query abort: it breaks startup here. (ISS-018)
- No `ffmpeg`, no `python` on this box. `powershell` works. Bash heredoc appends >~120 lines can be silently
  truncated by the output limit — chunk them.

## 3. File status — everything compiles and runs

| File | Lines | Notes |
|---|---|---|
| `src/gl_core.h/.cpp` | ~150/~90 | loader; `GetIntegerv` is loaded on purpose (driver's own pointer, see ISS-018) |
| `src/math3d.h` | 102 | `vec3` has **no** single-float ctor — use `v * s` |
| `src/people.h/.cpp` | 99/605 | **the crowd**: nav lattice over the street grid, BFS routes, downtown-biased errands, separation steering, hard non-overlap on a 4 m grid, 10-box bodies, `blocks()`/`pushOut()` for the player, `simulate()`/`stats()` for the selftest audit |
| `src/world.h/.cpp` | 79/730 | town generator; seed 1337 → 330 buildings, 453 trees, 24 lamps, 13 bridges, 845 blockers. Also owns `surfaceAt()` - the only correct answer to "what height is the ground here" |
| `src/shaders.h` | ~200 | 5 programs; shadow fade `smoothstep(118,158,d)` inside the 165 m ortho radius |
| `src/main.cpp` | ~1380 | complete: bootstrap, sky, render passes, player (incl. `followGround`), crowd wiring, HUD, BMP, selftest, `main()` |
| `Town3D.vcxproj/.sln`, `build.bat` | — | ClangCL via `-p:UseClang=true`, MSVC `v145` default; Console subsystem |
| `tools/smoke.ps1` | — | launch/wait/report/kill the real window (needs `-File` invocation; inline `-Command` quoting breaks in bash) |
| `README.md`, `LEDGER.md`, this file | — | docs |

## 4. Locked-in parameters (don't drift these; they are interdependent)

- Camera: fov 60°, near 0.25, far 1400, eye 1.7 m, walk limit 1050 m.
- Fog: `exp2`-ish `1-exp(-(d*uFogK)^1.8)`, `uFogK = 1/620` day → far plane must stay hidden by fog.
- Shadow: 2048², ortho half-extent **165 m**, centred on the player, texel-snapped, fade `smoothstep(118,158,d)`.
- Sun: `az = 2π·(clock/600)`, `el = 1.30·sin(az) − 0.10` → +69° noon / −80° midnight; `intensity = clamp(sin(el)·2.2, 0, 1)`.
- Player: radius 0.45, walk 2.6 m/s, run 5.6 m/s, collision sub-step 0.25 m.
- Pass order: shadow depth (cull FRONT, poly offset) → sky (depth masked, drawn first in colour pass) → solids → water (blend, depth-mask off) → clouds.
- Selftest clocks: day 60, dusk 288, **night 450** (clock 300 is only −5.7°, i.e. just-after-sunset, not midnight).

Ground heights — **ask `World::surfaceAt(x,z)`, never assume** (D-16). Five walkable surfaces, 18 cm apart, and every
one of them has swallowed a pedestrian at least once:

| surface | Y | constant |
|---|---|---|
| fields outside the grid | −0.04 | `TW_FIELD_Y` |
| block paving / grass | 0.00 | `TW_LOT_Y` |
| asphalt streets | +0.05 | `TW_STREET_Y` |
| bridge decks | +0.12 | `TW_DECK_Y` |
| river kerbs (copings) | +0.14 | `TW_QUAY_Y`, width `TW_QUAY_W` = 1.05 |

`surfaceAt` tests deck → kerb → street → block → field **in that order**: a deck crosses the kerbs it passes through,
and a `bridgeZone` has to be inflated by `TW_RAIL_T` to recover the slab footprint the builder actually draws. Walkers
are re-seated with `pos.y = surfaceAt(...)` at the *end* of `People::update()` (after every push, or the number is a
lie); the player's eye is `g_groundY + kEyeHigh` from `followGround(dt)` in `main.cpp`, smoothed, `dt >= 1` = snap
(spawn, screenshots). `TW_LEVEL` is **deleted** - one shared "ground level" was the bug (ISS-028). Selftest prints the
surface table and asserts worst foot error **0.000 m**.

Pedestrian parameters (load-bearing):

- Corridor centre line `cor(i) = -364 + 56i`; node *is* the intersection of corridor i and j, so the graph and the
  drawn street lattice can never drift apart. Vertical river = corridor **8** (centre x = 91, half width 11 → banks at
  80 and 102), horizontal = corridor **5** (centre z = −77, half width 14); bridges only at **even** corridors (the
  `j%2==0` / `i%2==0 && i!=8` predicates in `world.cpp` are mirrored in `people.cpp` — change one, change the other,
  then `--selftest` will prove it by the walkable-node count, which must stay **157/169** for seed 1337).
- Body radius 0.30 m, walk 1.35 m/s ±25 %, 10 boxes → 108 tris. Player radius 0.45 m, so contact distance is 0.75 m.
- Pedestrians reuse `World::blockedAt()`, i.e. they collide with a 0.45 m circle, not their own 0.30 m one
  (conservative on purpose: ISS-025).
- Body is 1.765 m tall and **0.335 m is its widest reach from the centre line** (the shoulder). Keep that under the
  0.45 m collision radius or a walker pressed against a wall will clip it. Soles sit exactly on the feet plane, the
  head sits on the torso (no see-through neck), and there is **no vertical bob** - all three were "missing body"
  reports (ISS-029).

## 5. Open work, best-value first

1. **ISS-031 then ISS-032, in that order.** Fix the mirrored BMP first (`captureBmp`, the copy loop reverses rows it
   should copy straight) — every picture anyone has judged, including the report of sunk walkers, came from a flipped
   file, and although a mirror preserves ground contact it is still the first variable to remove. Then measure with
   **independent** ground truth, never with the same helper the fix uses:
   (a) ray-cast `World::solid` for the highest triangle over a grid of street/deck/kerb/block points and compare that
   with `surfaceAt()` — if they disagree, the picture shows a sunk walker whatever `People::update()` computes;
   (b) read back the emitted ped vertex range and compare it with the walker's feet and its 1.765 m height — that
   catches a `addBox`/`toWorld` sign or stride error that `pos.y` audits cannot see;
   (c) then, and only then, look at a corrected screenshot.
2. **Human verification (V-5).** The model has no image input, so all verification here is numeric. The user has now
   looked at it twice and found both of the above, which is the whole reason numeric audits must be independent of
   the code they are auditing.
3. **ISS-021 night is too dark** (`gnd` luma ≈ 5/255). Options in the ledger row; needs the user's taste.
4. **ISS-026 the crowd has no rhythm** — identical numbers of walkers at midnight and at noon, no rush hour,
   nobody waits to cross. Cheapest version: scale the active count by hour (park the rest in `wait` overnight).
5. **Play-test the requirements** (`LEDGER.md` §6): can't enter buildings, bridges are the only crossings,
   shadows track the sun, clouds drift. Then mark B/V rows accordingly.
6. **ISS-027** CPU skinning of the crowd (~13 k tris re-uploaded per frame at 120 walkers) — instance it if the
   crowd ever grows. **ISS-022** water = 2 quads (cosmetic). **ISS-030** kerb and deck edges are steps, not ramps:
   add ramp quads at the bridge mouths in `world.cpp` if the 9–14 cm snap shows up on camera.
7. Optional: moon + moonlight at night, lamp-pool lights, `--walktest N` headless collision sweep (would turn
   "can't walk into buildings" into an automated check instead of a play-test, the same way the crowd probe
   already proves "can't walk into people").

## 6. Commands

```bat
build.bat                      :: Release + selftest  (clang-cl here)
build.bat Debug --run          :: Debug + launch the game
bin\Town3D.exe --selftest --showlog
bin\Town3D.exe --peds 0        :: empty town — A/B the crowd's effect on a shot
bin\Town3D.exe --noshadows     :: A/B the shadow pass
bin\Town3D.exe --trace         :: message traffic + perf line every 240 frames
powershell -File tools\smoke.ps1
powershell -File tools\smoke.ps1 -Extra "--peds 400"    :: stress the crowd
```

Useful log greps: `grep -E "shot|luma" town3d.log`, `grep -E "^\[crowd\]" town3d.log`,
`grep -E "error|gl\]" town3d.log`.

## 7. Gotchas that already bit once

- `wglGetProcAddress` answers **only while a context is current** — grab `wglCreateContextAttribsARB` inside the
  bootstrap, before deleting the throwaway context (ISS-017).
- The bootstrap's hidden 1×1 window shares `wndProc`: only `h == g_hwnd` may `PostQuitMessage` (ISS-019).
- `NOMINMAX` before `<windows.h>`, in the header too (ISS-016).
- Never call `glUseProgram` etc. bare — only `gl.UseProgram` exists (ISS-013). GL 1.1 calls *are* fine bare.
- `windows.h`'s `CreateDirectoryA` needs 2 args; `BITMAPFILEHEADER` is not used, the BMP header is hand-rolled.
- **`near` and `far` are `windows.h` macros.** A local `int near = 0;` silently becomes `int = 0;` and the
  compiler reports "expected unqualified-id" several lines away (ISS-023). Also avoid `small`, `large`,
  `interface` as identifiers in anything that includes `windows.h`.
- Do not put a statement **inside** the argument-parsing `else if` chain: it splits the chain and every flag
  falls through to "ignoring argument …" while still appearing to work (ISS-024). Clamp after the loop.
- `World::blockedAt` is the single source of truth for walkability. Anything new that can be walked into must be
  added to `blockers` — then pedestrians exclude it from the nav graph automatically, with no code there.
- **Append every prompt that changes code to `LEDGER.md` section 0a**, the prompt log, in order, as received.
- **Every green build gets committed, pushed and tagged** (user's standing instruction, 2026-11-07), then said out
  loud as done. "Green" = it compiles and runs; a tag is a restore point, not a quality award, so open bugs do NOT
  hold a tag back. Annotated tags, repo-local identity, verify the remote afterwards and run the name-leak scan over
  every revision.
- **`captureBmp` mirrors the image vertically** (ISS-031) while `analyzeFrame` reads GL row order (row 0 = bottom).
  So the logged `sky`/`gnd` figures are correct and comparable, but every `.bmp` on disk is upside down. Until it is
  fixed, flip any screenshot before judging it.
  Requests about git operations, and anything containing personal identifying details, are left out and must
  stay out — that log is published inside the repository.
