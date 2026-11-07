# CONTINUE.md — Town3D resume notes

**Last saved:** after the first fully working build. `build.bat Release` compiles with clang-cl and
`bin\Town3D.exe` runs: `--selftest` = 6 shots, 0 GL errors; interactive = 60 fps, killed cleanly after 7 s.
Companion document: `LEDGER.md` (work journal, design decisions, issue register, verification log).

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
| `src/world.h/.cpp` | ~660 | town generator; seed 1337 → 330 buildings, 453 trees, 24 lamps, 13 bridges, 845 blockers |
| `src/shaders.h` | ~200 | 5 programs; shadow fade `smoothstep(118,158,d)` inside the 165 m ortho radius |
| `src/main.cpp` | ~1180 | complete: bootstrap, sky, render passes, player, HUD, BMP, selftest, `main()` |
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

## 5. Open work, best-value first

1. **Human verification (V-5).** Nobody has *seen* it yet — the model has no image input, so all verification so
   far is numeric. Ask the user to run `bin\Town3D.exe` and report how it looks; screenshots are in `shots\`.
2. **ISS-021 night is too dark** (`gnd` luma ≈ 5/255). Options in the ledger row; needs the user's taste.
3. **Play-test the requirements** (`LEDGER.md` §6): can't enter buildings, bridges are the only crossings,
   shadows track the sun, clouds drift. Then mark B/V rows accordingly.
4. **ISS-022** water = 2 quads (cosmetic; only matters if real wave geometry is wanted).
5. Optional: moon + moonlight at night, lamp-pool lights, `--walktest N` headless collision sweep (would turn
   "can't walk into buildings" into an automated check instead of a play-test).

## 6. Commands

```bat
build.bat                      :: Release + selftest  (clang-cl here)
build.bat Debug --run          :: Debug + launch the game
bin\Town3D.exe --selftest --showlog
bin\Town3D.exe --noshadows     :: A/B the shadow pass
bin\Town3D.exe --trace         :: message traffic + perf line every 240 frames
powershell -File tools\smoke.ps1
```

Useful log greps: `grep -E "shot|luma" town3d.log`, `grep -E "error|gl\]" town3d.log`.

## 7. Gotchas that already bit once

- `wglGetProcAddress` answers **only while a context is current** — grab `wglCreateContextAttribsARB` inside the
  bootstrap, before deleting the throwaway context (ISS-017).
- The bootstrap's hidden 1×1 window shares `wndProc`: only `h == g_hwnd` may `PostQuitMessage` (ISS-019).
- `NOMINMAX` before `<windows.h>`, in the header too (ISS-016).
- Never call `glUseProgram` etc. bare — only `gl.UseProgram` exists (ISS-013). GL 1.1 calls *are* fine bare.
- `windows.h`'s `CreateDirectoryA` needs 2 args; `BITMAPFILEHEADER` is not used, the BMP header is hand-rolled.
