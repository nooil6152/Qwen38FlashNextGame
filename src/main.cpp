// main.cpp - Town3D: first-person walk-around of a low-poly town.
// OpenGL 3.3 core on plain Win32 + wgl, no third-party libraries.
//
//   controls: WASD/arrows move, Shift run, mouse look, T sun on/off,
//             [ ] scrub time, R new town, P screenshot, H hide HUD, Esc quit
//             --selftest : render preset views to shots/*.bmp and exit
#define _CRT_SECURE_NO_WARNINGS 1
#define WIN32_LEAN_AND_MEAN 1
#define NOMINMAX 1   /* windows.h defines max/min macros that break std::max */

#include <windows.h>

#include <math.h>
#include <algorithm>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "gl_core.h"
#include "math3d.h"
#include "people.h"
#include "shaders.h"
#include "world.h"

// ---------------------------------------------------------------------------
// globals
// ---------------------------------------------------------------------------
static const float kPi = 3.14159265358979f;
static const int   SHADOW_RES = 2048;
static const float kDayLen = 600.0f;    // seconds for a full sun revolution
static const float kFogK = 0.0021f;
static const float kEyeHigh = 1.70f;

static HINSTANCE g_inst = nullptr;
static HWND  g_hwnd = nullptr;
static HDC   g_hdc = nullptr;
static HGLRC g_ctx = nullptr;
static int   g_w = 1280, g_h = 720;
static bool  g_run = true;
static bool  g_selftest = false;
static GLint g_msaa = 0;

static volatile LONG g_mx = 0, g_my = 0;
static bool g_keys[256];
static bool g_wantShot = false, g_wantRegen = false, g_hud = true, g_sunAnim = true;

static vec3  g_pos(0, kEyeHigh, 0);
static float g_yaw = 0.0f, g_pitch = 0.0f, g_bob = 0.0f;
static float g_clock = 60.0f;           // sun clock, seconds
static float g_elapsed = 0.0f;          // shader time, seconds
static float g_cloudDrift = 0.0f;
static float g_fps = 0.0f;
static unsigned g_seed = 1337u;
static int   g_frameErrors = 0;

static World  g_world;
static People g_people;              // the crowd: see people.h
static int    g_pedCount = 120;      // --peds N   (0 = nobody about)
static int    g_pedTris  = 0;
static std::vector<Vertex> g_pedMesh;

// GL objects
static GLuint g_progSolid = 0, g_progDepth = 0, g_progSky = 0, g_progWater = 0, g_progCloud = 0;
static GLuint g_vaoSolid = 0, g_vboSolid = 0, g_vaoWater = 0, g_vboWater = 0;
static GLuint g_vaoCloud = 0, g_vboCloud = 0, g_vaoEmpty = 0;
static GLuint g_vaoPed = 0, g_vboPed = 0;
static GLuint g_shadowFbo = 0, g_shadowTex = 0;
static int    g_solidTris = 0, g_waterTris = 0, g_cloudTris = 0;

typedef BOOL(WINAPI* PfnChooseFmt)(HDC, const int*, const float*, UINT, int*, UINT*);
typedef HGLRC(WINAPI* PfnCreateCtx)(HDC, HGLRC, const int*);
static PfnChooseFmt g_chooseFmt = nullptr;
static PfnCreateCtx g_createCtx = nullptr;   // must be fetched while *some* context is current

struct UniSolid
{
    GLint proj, view, lightVP, sunDir, sunCol, ambSky, ambGnd, fogCol, cam, shadow, texel, night, fogK, shadowOn;
};
struct UniDepth { GLint lightVP; };
struct UniSky { GLint cam, f, r, u, sunDir, sunCol, zenith, horizon, haze, tanHalf, aspect, night; };
struct UniWater { GLint proj, view, time, cam, sunDir, sunCol, fogCol, fogK; };
struct UniCloud { GLint proj, view, drift, cam, tint, fogCol, fogK; };

static UniSolid g_uSolid;
static UniDepth g_uDepth;
static UniSky g_uSky;
static UniWater g_uWater;
static UniCloud g_uCloud;

// ---------------------------------------------------------------------------
// logging: console + town3d.log next to the exe (the console is hidden in
// normal play, so the file is the record people will actually read)
// ---------------------------------------------------------------------------
static void logfmt(const char* fmt, ...)
{
    char buf[1200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    fflush(stdout);
    FILE* f = fopen("town3d.log", "a");
    if (f) { fputs(buf, f); fclose(f); }
}

// ---------------------------------------------------------------------------
// sun + sky model
// ---------------------------------------------------------------------------
struct SkyState
{
    vec3 dir, sunCol, zenith, horizon, haze, ambSky, ambGnd, fogCol, cloudTint;
    float elevation, night, day;
};

static void skyFor(float clock, SkyState& s)
{
    const float az = 2.0f * kPi * std::fmod(clock / kDayLen, 1.0f);
    // Sun elevation sweeps from -80 deg (clock 450, midnight) to +69 deg (clock
    // 150, noon). The first version of this used max(0, sin(az)), which parked
    // the sun at 7 deg all night - a "night" sky with a sun disc on the horizon
    // and no stars (ISS-013).
    const float el = 1.30f * std::sin(az) - 0.10f;
    s.elevation = el;
    s.dir = vec3(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));

    const float day = clampf((el - 0.06f) / 0.45f, 0.0f, 1.0f);
    const float dusk = std::exp(-std::pow((el - 0.10f) / 0.25f, 2.0f));
    s.night = clampf((0.14f - el) / 0.26f, 0.0f, 1.0f);
    s.day = day;

    const vec3 zenNight(0.020f, 0.035f, 0.075f), zenDay(0.150f, 0.380f, 0.750f);
    const vec3 horNight(0.045f, 0.060f, 0.110f), horDay(0.620f, 0.760f, 0.900f),
        horDusk(0.930f, 0.460f, 0.230f);
    const vec3 ambNight(0.115f, 0.145f, 0.235f), ambDay(0.400f, 0.500f, 0.620f);
    const vec3 gndNight(0.060f, 0.065f, 0.080f), gndDay(0.200f, 0.190f, 0.155f);
    const vec3 sunDay(1.000f, 0.970f, 0.900f), sunDusk(1.000f, 0.520f, 0.260f);

    s.zenith = lerp(zenNight, zenDay, day);
    s.horizon = lerp(lerp(horNight, horDay, day), horDusk, dusk * 0.80f);
    const float intensity = clampf(std::sin(el) * 2.2f, 0.0f, 1.0f);
    s.sunCol = lerp(sunDusk, sunDay, day) * intensity;
    s.ambSky = lerp(ambNight, ambDay, day) + vec3(0.055f, 0.030f, 0.014f) * dusk;
    s.ambGnd = lerp(gndNight, gndDay, day) + vec3(0.020f, 0.012f, 0.006f) * dusk;
    s.fogCol = s.horizon;
    s.haze = s.horizon * 0.94f;
    vec3 tint = lerp(vec3(0.26f, 0.30f, 0.42f), vec3(1.00f, 0.98f, 0.95f), day);
    tint = lerp(tint, vec3(1.00f, 0.72f, 0.52f), dusk * 0.65f);
    s.cloudTint = tint * (0.55f + 0.45f * intensity);
}

// ---------------------------------------------------------------------------
// shaders
// ---------------------------------------------------------------------------
static GLuint compileShader(GLenum type, const char* src, const char* label)
{
    GLuint sh = gl.CreateShader(type);
    gl.ShaderSource(sh, 1, &src, nullptr);
    gl.CompileShader(sh);
    GLint ok = 0;
    gl.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        GLsizei n = 0;
        gl.GetShaderInfoLog(sh, (GLsizei)sizeof(log), &n, log);
        logfmt("[shader] %s FAILED to compile:\n%s\n", label, log);
    }
    return sh;
}

static GLuint makeProgram(const char* vs, const char* fs, const char* label)
{
    GLuint v = compileShader(GL_VERTEX_SHADER, vs, label);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, label);
    GLuint p = gl.CreateProgram();
    gl.AttachShader(p, v);
    gl.AttachShader(p, f);
    gl.LinkProgram(p);
    GLint ok = 0;
    gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        GLsizei n = 0;
        gl.GetProgramInfoLog(p, (GLsizei)sizeof(log), &n, log);
        logfmt("[shader] %s FAILED to link:\n%s\n", label, log);
    }
    else
    {
        logfmt("[shader] %s ok\n", label);
    }
    gl.DeleteShader(v);
    gl.DeleteShader(f);
    return p;
}

static GLint U(GLuint p, const char* n) { return gl.GetUniformLocation(p, n); }

static void cacheUniforms()
{
    g_uSolid.proj = U(g_progSolid, "uProj");
    g_uSolid.view = U(g_progSolid, "uView");
    g_uSolid.lightVP = U(g_progSolid, "uLightVP");
    g_uSolid.sunDir = U(g_progSolid, "uSunDir");
    g_uSolid.sunCol = U(g_progSolid, "uSunCol");
    g_uSolid.ambSky = U(g_progSolid, "uAmbSky");
    g_uSolid.ambGnd = U(g_progSolid, "uAmbGnd");
    g_uSolid.fogCol = U(g_progSolid, "uFogCol");
    g_uSolid.cam = U(g_progSolid, "uCam");
    g_uSolid.shadow = U(g_progSolid, "uShadow");
    g_uSolid.texel = U(g_progSolid, "uTexel");
    g_uSolid.night = U(g_progSolid, "uNight");
    g_uSolid.fogK = U(g_progSolid, "uFogK");
    g_uSolid.shadowOn = U(g_progSolid, "uShadowOn");

    g_uDepth.lightVP = U(g_progDepth, "uLightVP");

    g_uSky.cam = U(g_progSky, "uCam");
    g_uSky.f = U(g_progSky, "uF");
    g_uSky.r = U(g_progSky, "uR");
    g_uSky.u = U(g_progSky, "uU");
    g_uSky.sunDir = U(g_progSky, "uSunDir");
    g_uSky.sunCol = U(g_progSky, "uSunCol");
    g_uSky.zenith = U(g_progSky, "uZenith");
    g_uSky.horizon = U(g_progSky, "uHorizon");
    g_uSky.haze = U(g_progSky, "uHaze");
    g_uSky.tanHalf = U(g_progSky, "uTanHalf");
    g_uSky.aspect = U(g_progSky, "uAspect");
    g_uSky.night = U(g_progSky, "uNight");

    g_uWater.proj = U(g_progWater, "uProj");
    g_uWater.view = U(g_progWater, "uView");
    g_uWater.time = U(g_progWater, "uTime");
    g_uWater.cam = U(g_progWater, "uCam");
    g_uWater.sunDir = U(g_progWater, "uSunDir");
    g_uWater.sunCol = U(g_progWater, "uSunCol");
    g_uWater.fogCol = U(g_progWater, "uFogCol");
    g_uWater.fogK = U(g_progWater, "uFogK");

    g_uCloud.proj = U(g_progCloud, "uProj");
    g_uCloud.view = U(g_progCloud, "uView");
    g_uCloud.drift = U(g_progCloud, "uDrift");
    g_uCloud.cam = U(g_progCloud, "uCam");
    g_uCloud.tint = U(g_progCloud, "uCloudTint");
    g_uCloud.fogCol = U(g_progCloud, "uFogCol");
    g_uCloud.fogK = U(g_progCloud, "uFogK");
}

// ---------------------------------------------------------------------------
// meshes / VAOs
// ---------------------------------------------------------------------------
static const GLint kStride = (GLint)sizeof(Vertex);

static void uploadMeshes()
{
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vboSolid);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_world.solid.size() * sizeof(Vertex)),
                  g_world.solid.empty() ? nullptr : &g_world.solid[0], GL_STATIC_DRAW);
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vboWater);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_world.water.size() * sizeof(Vertex)),
                  g_world.water.empty() ? nullptr : &g_world.water[0], GL_STATIC_DRAW);
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vboCloud);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_world.clouds.size() * sizeof(CloudVertex)),
                  g_world.clouds.empty() ? nullptr : &g_world.clouds[0], GL_STATIC_DRAW);
    g_solidTris = (int)(g_world.solid.size() / 3);
    g_waterTris = (int)(g_world.water.size() / 3);
    g_cloudTris = (int)(g_world.clouds.size() / 3);
}

static void setupMeshGpuObjects()
{
    gl.GenVertexArrays(1, &g_vaoSolid);
    gl.GenVertexArrays(1, &g_vaoWater);
    gl.GenVertexArrays(1, &g_vaoCloud);
    gl.GenVertexArrays(1, &g_vaoEmpty);
    gl.GenBuffers(1, &g_vboSolid);
    gl.GenBuffers(1, &g_vboWater);
    gl.GenBuffers(1, &g_vboCloud);
    gl.GenVertexArrays(1, &g_vaoPed);
    gl.GenBuffers(1, &g_vboPed);
    uploadMeshes();

    gl.BindVertexArray(g_vaoSolid);
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vboSolid);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (const void*)0);
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride, (const void*)(sizeof(float) * 3));
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, kStride, (const void*)(sizeof(float) * 6));
    gl.EnableVertexAttribArray(3);
    gl.VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, kStride, (const void*)(sizeof(float) * 9));

    gl.BindVertexArray(g_vaoWater);
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vboWater);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (const void*)0);
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride, (const void*)(sizeof(float) * 3));
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, kStride, (const void*)(sizeof(float) * 6));

    gl.BindVertexArray(g_vaoCloud);
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vboCloud);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLint)sizeof(CloudVertex), (const void*)0);
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, (GLint)sizeof(CloudVertex),
                           (const void*)(sizeof(float) * 3));

    gl.BindVertexArray(0);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);
}

// The crowd changes shape every frame, so it gets its own dynamic buffer with
// the same vertex layout as the static town - the solid shader draws it for
// free, which means pedestrians get lighting, fog and shadows for nothing.
static void setupPedGpuObjects()
{
    gl.BindVertexArray(g_vaoPed);
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vboPed);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (const void*)0);
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride, (const void*)(sizeof(float) * 3));
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, kStride, (const void*)(sizeof(float) * 6));
    gl.EnableVertexAttribArray(3);
    gl.VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, kStride, (const void*)(sizeof(float) * 9));
    gl.BindVertexArray(0);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);
}

static void uploadPedMesh()
{
    g_pedMesh.clear();
    g_people.emit(g_pedMesh);
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vboPed);
    // re-specifying the store each frame orphans the old one, so the GPU never
    // stalls waiting for the previous frame to finish reading it
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_pedMesh.size() * sizeof(Vertex)),
                  g_pedMesh.empty() ? nullptr : &g_pedMesh[0], GL_DYNAMIC_DRAW);
    g_pedTris = (int)(g_pedMesh.size() / 3);
}

// ---------------------------------------------------------------------------
// window + input
// ---------------------------------------------------------------------------
static bool  g_trace = false;   // --trace: log the Win32 message traffic

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        RAWINPUTDEVICE rid;
        rid.usUsagePage = 0x01;         // generic desktop
        rid.usUsage = 0x02;             // mouse
        rid.dwFlags = RIDEV_INPUTSINK;
        rid.hwndTarget = h;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        return 0;
    }
    case WM_INPUT:
    {
        if (g_trace) logfmt("[win] WM_INPUT\n");
        RAWINPUT ri;
        UINT size = sizeof(ri);
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
            return 0;
        if (ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
        {
            InterlockedAdd(&g_mx, ri.data.mouse.lLastX);
            InterlockedAdd(&g_my, ri.data.mouse.lLastY);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (w < 256) g_keys[w] = true;
        if (w == 'T') g_sunAnim = !g_sunAnim;
        if (w == 'H') g_hud = !g_hud;
        if (w == 'P') g_wantShot = true;
        if (w == 'R') g_wantRegen = true;
        if (w == VK_ESCAPE) g_run = false;
        return 0;
    case WM_KEYUP:
        if (w < 256) g_keys[w] = false;
        return 0;
    case WM_SIZE:
        g_w = LOWORD(l) ? LOWORD(l) : 1;
        g_h = HIWORD(l) ? HIWORD(l) : 1;
        glViewport(0, 0, g_w, g_h);
        return 0;
    case WM_SETFOCUS:
        InterlockedExchange(&g_mx, 0);
        InterlockedExchange(&g_my, 0);
        return 0;
    case WM_CLOSE:
        if (h != g_hwnd) return DefWindowProcA(h, msg, w, l);   // bootstrap dummy window
        if (g_trace) logfmt("[win] WM_CLOSE\n");
        return DefWindowProcA(h, msg, w, l);
    case WM_DESTROY:
        if (h != g_hwnd)
            return 0;   // the 1x1 window used to bootstrap wglChoosePixelFormatARB.
                        // It must NOT post quit - doing so ended the app before the
                        // first frame (ISS-013).
        logfmt("[win] WM_DESTROY\n");
        g_run = false;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(h, msg, w, l);
}

static void setCursorHidden(bool hide)
{
    static int depth = 0;
    while (depth < (hide ? 1 : 0)) { ShowCursor(FALSE); ++depth; }
    while (depth > (hide ? 1 : 0)) { ShowCursor(TRUE); --depth; }
}

// ---------------------------------------------------------------------------
// GL context bootstrap
// ---------------------------------------------------------------------------
static void fillBasicPfd(PIXELFORMATDESCRIPTOR& pfd)
{
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
}

static bool usePixelFormat(HDC dc, int samples)
{
    if (g_chooseFmt && samples > 0)
    {
        int attribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, 1, WGL_SUPPORT_OPENGL_ARB, 1, WGL_DOUBLE_BUFFER_ARB, 1,
            WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB, WGL_COLOR_BITS_ARB, 32, WGL_DEPTH_BITS_ARB, 24,
            WGL_SAMPLE_BUFFERS_ARB, 1, WGL_SAMPLES_ARB, samples, 0
        };
        int fmt = 0;
        UINT got = 0;
        if (g_chooseFmt(dc, attribs, nullptr, 1, &fmt, &got) && got > 0 && fmt)
        {
            PIXELFORMATDESCRIPTOR pfd;
            if (DescribePixelFormat(dc, fmt, sizeof(pfd), &pfd) && SetPixelFormat(dc, fmt, &pfd))
            {
                logfmt("[gl] pixel format %d, %dx MSAA\n", fmt, samples);
                return true;
            }
        }
    }
    PIXELFORMATDESCRIPTOR pfd;
    fillBasicPfd(pfd);
    int idx = ChoosePixelFormat(dc, &pfd);
    if (!idx) return false;
    logfmt("[gl] plain pixel format %d (no MSAA)\n", idx);
    return SetPixelFormat(dc, idx, &pfd) != FALSE;
}

static HGLRC createCoreContext(HDC dc, int major, int minor)
{
    if (!g_createCtx)
    {
        logfmt("[gl] wglCreateContextAttribsARB missing - ancient driver?\n");
        return nullptr;
    }
    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, major,
        WGL_CONTEXT_MINOR_VERSION_ARB, minor,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    return g_createCtx(dc, nullptr, attribs);
}

static bool initGL(int samples)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "Town3DWnd";
    if (!RegisterClassA(&wc))
    {
        logfmt("[win] RegisterClassA failed (%lu)\n", GetLastError());
        return false;
    }

    // Throw-away window + context: wglGetProcAddress needs a current context
    // before the real one exists (GLFW does the same trick).
    HWND dummy = CreateWindowExA(0, "Town3DWnd", "dummy", 0, 0, 0, 1, 1,
                                 nullptr, nullptr, g_inst, nullptr);
    if (!dummy) { logfmt("[win] dummy window failed\n"); return false; }
    HDC ddc = GetDC(dummy);
    PIXELFORMATDESCRIPTOR bp;
    fillBasicPfd(bp);
    int bi = ChoosePixelFormat(ddc, &bp);
    if (!bi || !SetPixelFormat(ddc, bi, &bp)) { logfmt("[gl] basic pixel format failed\n"); return false; }
    HGLRC tmp = wglCreateContext(ddc);
    if (!tmp || !wglMakeCurrent(ddc, tmp)) { logfmt("[gl] temp context failed\n"); return false; }

    g_chooseFmt = (PfnChooseFmt)(INT_PTR)wglGetProcAddress("wglChoosePixelFormatARB");
    g_createCtx = (PfnCreateCtx)(INT_PTR)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!g_createCtx)
        logfmt("[gl] bootstrap: no wglCreateContextAttribsARB (GL %s)\n",
               glGetString(GL_VERSION) ? (const char*)glGetString(GL_VERSION) : "?");

    const DWORD style = WS_VISIBLE | WS_OVERLAPPEDWINDOW;
    RECT rc = { 0, 0, g_w, g_h };
    AdjustWindowRect(&rc, style, FALSE);
    g_hwnd = CreateWindowExA(0, "Town3DWnd", "Town3D - OpenGL walk-around", style,
                             CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, g_inst, nullptr);
    if (!g_hwnd) { logfmt("[win] main window failed\n"); return false; }
    if (g_selftest) ShowWindow(g_hwnd, SW_HIDE);
    g_hdc = GetDC(g_hwnd);

    // dummy context is still current here, which wglChoosePixelFormatARB wants
    if (!usePixelFormat(g_hdc, samples)) { logfmt("[gl] pixel format on main window failed\n"); return false; }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tmp);
    ReleaseDC(dummy, ddc);
    DestroyWindow(dummy);

    g_ctx = createCoreContext(g_hdc, 3, 3);
    for (int minor = 2; minor >= 0 && !g_ctx; --minor) g_ctx = createCoreContext(g_hdc, 3, minor);
    if (!g_ctx) { logfmt("[gl] could not create an OpenGL 3.x core context\n"); return false; }
    if (!wglMakeCurrent(g_hdc, g_ctx)) { logfmt("[gl] wglMakeCurrent failed\n"); return false; }

    typedef BOOL(WINAPI* PfnSwapInterval)(int);
    PfnSwapInterval si = (PfnSwapInterval)(INT_PTR)wglGetProcAddress("wglSwapIntervalEXT");
    if (si) si(1);

    if (!glLoadAll()) return false;

    GLint mj = 0, mn = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &mj);
    glGetIntegerv(GL_MINOR_VERSION, &mn);
    glGetIntegerv(GL_SAMPLES, &g_msaa);
    // The opengl32.lib import of glGetIntegerv is the GL 1.1 thunk; in a 3.x core
    // context the driver's own entry point is authoritative and some drivers only
    // answer the version query that way. Last resort: parse GL_VERSION.
    int mj2 = 0, mn2 = 0;
    if (gl.GetIntegerv)
    {
        gl.GetIntegerv(GL_MAJOR_VERSION, &mj2);
        gl.GetIntegerv(GL_MINOR_VERSION, &mn2);
    }
    const GLint mjImport = mj, mnImport = mn;
    if (mj2 > mj) { mj = mj2; mn = mn2; }
    const char* vstr = (const char*)glGetString(GL_VERSION);
    if (mj < 3 && vstr)
    {
        int a = 0, b = 0;
        if (sscanf(vstr, "%d.%d", &a, &b) == 2) { mj = a; mn = b; }
    }
    logfmt("[gl] context %d.%d (import %d.%d / driver %d.%d) | %s | %s | MSAA %d | err 0x%04x\n",
           (int)mj, (int)mn, (int)mjImport, (int)mnImport, mj2, mn2,
           vstr ? vstr : "?",
           glGetString(GL_RENDERER) ? (const char*)glGetString(GL_RENDERER) : "?",
           (int)g_msaa, (unsigned)glGetError());
    if (mj < 3) { logfmt("[gl] need OpenGL 3.x\n"); return false; }
    return true;
}

static void createGpuObjects()
{
    g_progSolid = makeProgram(VS_SOLID, FS_SOLID, "solid");
    g_progDepth = makeProgram(VS_DEPTH, FS_DEPTH, "depth");
    g_progSky = makeProgram(VS_SKY, FS_SKY, "sky");
    g_progWater = makeProgram(VS_WATER, FS_WATER, "water");
    g_progCloud = makeProgram(VS_CLOUD, FS_CLOUD, "cloud");
    cacheUniforms();
    setupMeshGpuObjects();

    gl.GenTextures(1, &g_shadowTex);
    gl.BindTexture(GL_TEXTURE_2D, g_shadowTex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_RES, SHADOW_RES, 0,
                  GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    gl.GenFramebuffers(1, &g_shadowFbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, g_shadowFbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_shadowTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    GLenum st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
    logfmt("[gl] shadow map %dx%d, FBO %s (0x%04x)\n", SHADOW_RES, SHADOW_RES,
           st == GL_FRAMEBUFFER_COMPLETE ? "complete" : "INCOMPLETE", (unsigned)st);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    gl.BindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_MULTISAMPLE);
}

// ---------------------------------------------------------------------------
// camera + sun light matrix
// ---------------------------------------------------------------------------
static vec3 forwardDir()
{
    const float cp = std::cos(g_pitch);
    return vec3(cp * std::sin(g_yaw), std::sin(g_pitch), -cp * std::cos(g_yaw));
}

// Is the player's cylinder clear here? Walls, railings, water - and pedestrians,
// who are as solid as a house wall. One place decides it, so the selftest probe
// exercises the same rule the game does.
static bool playerFreeAt(float x, float z)
{
    return !g_world.blockedAt(x, z) && !g_people.blocks(vec3(x, 0.0f, z), TW_PLAYER_R);
}

// Orthographic shadow volume centred on the player, snapped to shadow texels so
// the shadow edges do not crawl while walking (ISS-004).
static void lightMatrix(mat4& outVP, const vec3& sunDir, const vec3& around)
{
    const float R = 165.0f;
    const vec3 c(around.x, 0.0f, around.z);
    const vec3 eye = c + sunDir * 340.0f;
    mat4 view = mat4LookAt(eye, c, vec3(0.0f, 1.0f, 0.0f));
    mat4 proj = mat4Ortho(-R, R, -R, R, 20.0f, 700.0f);

    const mat4 vp = mat4Mul(proj, view);
    const float px = vp.m[0] * c.x + vp.m[4] * c.y + vp.m[8] * c.z + vp.m[12];
    const float py = vp.m[1] * c.x + vp.m[5] * c.y + vp.m[9] * c.z + vp.m[13];
    const float t = 2.0f / (float)SHADOW_RES;   // ortho keeps w = 1 => px/py are NDC
    proj.m[12] += std::floor(px / t) * t - px;
    proj.m[13] += std::floor(py / t) * t - py;
    outVP = mat4Mul(proj, view);
}

static void setFogLike(GLint fogCol, GLint fogK, const SkyState& sky)
{
    gl.Uniform3f(fogCol, sky.fogCol.x, sky.fogCol.y, sky.fogCol.z);
    gl.Uniform1f(fogK, kFogK);
}

// ---------------------------------------------------------------------------
// frame
// ---------------------------------------------------------------------------
static bool g_shadowsOn = true;

static unsigned g_frames = 0;

static void render(const SkyState& sky)
{
    ++g_frames;
    const vec3 f = forwardDir();
    const vec3 r = normalize(cross(f, vec3(0.0f, 1.0f, 0.0f)));
    const vec3 u = cross(r, f);
    const float aspect = (float)g_w / (float)g_h;
    const float fov = 70.0f * kPi / 180.0f;
    const float tanHalf = std::tan(fov * 0.5f);
    const mat4 proj = mat4Perspective(fov, aspect, 0.15f, 1800.0f);
    const mat4 view = mat4LookAt(g_pos, g_pos + f, vec3(0.0f, 1.0f, 0.0f));

    mat4 lvp;
    lightMatrix(lvp, sky.dir, g_pos);
    const bool castShadows = g_shadowsOn && sky.day > 0.02f;

    uploadPedMesh();          // the crowd walks, so its mesh is rebuilt per frame

    // ---- pass 1: sun depth -------------------------------------------------
    gl.BindFramebuffer(GL_FRAMEBUFFER, g_shadowFbo);
    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glClear(GL_DEPTH_BUFFER_BIT);
    if (castShadows)
    {
        gl.UseProgram(g_progDepth);
        gl.UniformMatrix4fv(g_uDepth.lightVP, 1, GL_FALSE, lvp.m);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.9f, 4.5f);
        gl.BindVertexArray(g_vaoSolid);
        gl.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(g_solidTris * 3));
        if (g_pedTris > 0)                        // people cast shadows too
        {
            gl.BindVertexArray(g_vaoPed);
            gl.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(g_pedTris * 3));
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

    // ---- pass 2: colour ---------------------------------------------------
    glViewport(0, 0, g_w, g_h);
    glClearColor(sky.haze.x, sky.haze.y, sky.haze.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // sky (full-screen triangle, no depth)
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    gl.UseProgram(g_progSky);
    gl.Uniform3f(g_uSky.cam, g_pos.x, g_pos.y, g_pos.z);
    gl.Uniform3f(g_uSky.f, f.x, f.y, f.z);
    gl.Uniform3f(g_uSky.r, r.x, r.y, r.z);
    gl.Uniform3f(g_uSky.u, u.x, u.y, u.z);
    gl.Uniform3f(g_uSky.sunDir, sky.dir.x, sky.dir.y, sky.dir.z);
    gl.Uniform3f(g_uSky.sunCol, sky.sunCol.x, sky.sunCol.y, sky.sunCol.z);
    gl.Uniform3f(g_uSky.zenith, sky.zenith.x, sky.zenith.y, sky.zenith.z);
    gl.Uniform3f(g_uSky.horizon, sky.horizon.x, sky.horizon.y, sky.horizon.z);
    gl.Uniform3f(g_uSky.haze, sky.haze.x, sky.haze.y, sky.haze.z);
    gl.Uniform1f(g_uSky.tanHalf, tanHalf);
    gl.Uniform1f(g_uSky.aspect, aspect);
    gl.Uniform1f(g_uSky.night, sky.night);
    gl.BindVertexArray(g_vaoEmpty);
    gl.DrawArrays(GL_TRIANGLES, 0, 3);

    // solids
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    gl.UseProgram(g_progSolid);
    gl.UniformMatrix4fv(g_uSolid.proj, 1, GL_FALSE, proj.m);
    gl.UniformMatrix4fv(g_uSolid.view, 1, GL_FALSE, view.m);
    gl.UniformMatrix4fv(g_uSolid.lightVP, 1, GL_FALSE, lvp.m);
    gl.Uniform3f(g_uSolid.sunDir, sky.dir.x, sky.dir.y, sky.dir.z);
    gl.Uniform3f(g_uSolid.sunCol, sky.sunCol.x, sky.sunCol.y, sky.sunCol.z);
    gl.Uniform3f(g_uSolid.ambSky, sky.ambSky.x, sky.ambSky.y, sky.ambSky.z);
    gl.Uniform3f(g_uSolid.ambGnd, sky.ambGnd.x, sky.ambGnd.y, sky.ambGnd.z);
    gl.Uniform3f(g_uSolid.cam, g_pos.x, g_pos.y, g_pos.z);
    gl.Uniform1f(g_uSolid.texel, 1.0f / (float)SHADOW_RES);
    gl.Uniform1f(g_uSolid.night, sky.night);
    gl.Uniform1f(g_uSolid.shadowOn, castShadows ? 1.0f : 0.0f);
    setFogLike(g_uSolid.fogCol, g_uSolid.fogK, sky);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, g_shadowTex);
    gl.Uniform1i(g_uSolid.shadow, 0);
    gl.BindVertexArray(g_vaoSolid);
    gl.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(g_solidTris * 3));
    if (g_pedTris > 0)
    {
        gl.BindVertexArray(g_vaoPed);
        gl.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(g_pedTris * 3));
    }

    // water (translucent, no depth write)
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.UseProgram(g_progWater);
    gl.UniformMatrix4fv(g_uWater.proj, 1, GL_FALSE, proj.m);
    gl.UniformMatrix4fv(g_uWater.view, 1, GL_FALSE, view.m);
    gl.Uniform1f(g_uWater.time, g_elapsed);
    gl.Uniform3f(g_uWater.cam, g_pos.x, g_pos.y, g_pos.z);
    gl.Uniform3f(g_uWater.sunDir, sky.dir.x, sky.dir.y, sky.dir.z);
    gl.Uniform3f(g_uWater.sunCol, sky.sunCol.x, sky.sunCol.y, sky.sunCol.z);
    setFogLike(g_uWater.fogCol, g_uWater.fogK, sky);
    gl.BindVertexArray(g_vaoWater);
    gl.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(g_waterTris * 3));

    // clouds: wind blows along +X, so three tiled copies of the field cover the
    // wrap without tearing quads at a modulo seam (D-8)
    glDisable(GL_CULL_FACE);
    gl.UseProgram(g_progCloud);
    gl.UniformMatrix4fv(g_uCloud.proj, 1, GL_FALSE, proj.m);
    gl.UniformMatrix4fv(g_uCloud.view, 1, GL_FALSE, view.m);
    gl.Uniform3f(g_uCloud.cam, g_pos.x, g_pos.y, g_pos.z);
    gl.Uniform3f(g_uCloud.tint, sky.cloudTint.x, sky.cloudTint.y, sky.cloudTint.z);
    setFogLike(g_uCloud.fogCol, g_uCloud.fogK, sky);
    const float period = 2.0f * TW_CLOUD_RANGE;
    gl.BindVertexArray(g_vaoCloud);
    for (int k = -1; k <= 1; ++k)
    {
        gl.Uniform3f(g_uCloud.drift, g_cloudDrift + period * (float)k, 0.0f, 0.0f);
        gl.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(g_cloudTris * 3));
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    gl.BindVertexArray(0);
}

// ---------------------------------------------------------------------------
// player
// ---------------------------------------------------------------------------
// Height of the ground under the player, kept smooth. The town offers five
// walkable heights inside 18 cm - fields, block paving, asphalt, river kerbs,
// bridge decks - and if the eye is measured from y=0 instead you hover over a
// deck while every walker looks sunk into the road. dt >= 1 means "snap": used
// on spawn and when the selftest teleports between shots.
static float g_groundY = 0.0f;
static void followGround(float dt)
{
    const float want = g_world.surfaceAt(g_pos.x, g_pos.z);
    if (dt >= 1.0f) { g_groundY = want; return; }
    g_groundY += (want - g_groundY) * clampf(dt * 14.0f, 0.0f, 1.0f);
    if (std::fabs(want - g_groundY) < 1e-4f) g_groundY = want;
}

static void resetPlayerToSpawn()
{
    g_pos = g_world.spawn;
    followGround(1.0f);
    g_pos.y = g_groundY + kEyeHigh;
    g_yaw = g_world.spawnYaw;
    g_pitch = 0.0f;
    g_bob = 0.0f;
}

static void lookAround()
{
    const LONG dx = InterlockedExchange(&g_mx, 0);
    const LONG dy = InterlockedExchange(&g_my, 0);
    if (!dx && !dy) return;
    const float s = 0.0023f;
    g_yaw += (float)dx * s;
    g_pitch = clampf(g_pitch - (float)dy * s, -1.52f, 1.52f);
    if (g_yaw > kPi) g_yaw -= 2.0f * kPi;
    if (g_yaw < -kPi) g_yaw += 2.0f * kPi;
}

// Circle-vs-AABB sweep, sub-stepped so thin railings cannot be tunnelled at run
// speed, and resolved per axis so brushing a wall slides instead of sticking.
static void movePlayer(float dt)
{
    float mf = 0.0f, ms = 0.0f;
    if (g_keys['W'] || g_keys[VK_UP]) mf += 1.0f;
    if (g_keys['S'] || g_keys[VK_DOWN]) mf -= 1.0f;
    if (g_keys['D'] || g_keys[VK_RIGHT]) ms += 1.0f;
    if (g_keys['A'] || g_keys[VK_LEFT]) ms -= 1.0f;

    const vec3 fwd(std::sin(g_yaw), 0.0f, -std::cos(g_yaw));
    const vec3 rgt(std::cos(g_yaw), 0.0f, std::sin(g_yaw));
    vec3 want = fwd * mf + rgt * ms;
    const float len = length(want);
    if (len > 1e-4f) want = want * (1.0f / len);

    const float speed = g_keys[VK_SHIFT] ? 13.0f : 5.5f;
    const float dist = (len > 1e-4f) ? speed * dt : 0.0f;
    if (dist > 0.0f)
    {
        int steps = (int)ceilf(dist / 0.25f);
        if (steps < 1) steps = 1;
        const float sd = dist / (float)steps;
        for (int s = 0; s < steps; ++s)
        {
            const float nx = g_pos.x + want.x * sd;
            const float nz = g_pos.z + want.z * sd;
            const bool freeX = playerFreeAt(nx, g_pos.z);
            const bool freeZ = playerFreeAt(g_pos.x, nz);
            if (freeX) g_pos.x = nx;
            if (freeZ) g_pos.z = nz;
        }
        g_bob += dist * 1.5f;
    }
    followGround(dt);
    g_pos.y = g_groundY + kEyeHigh + (dist > 0.0f ? std::sin(g_bob) * 0.05f : 0.0f);
}

static void tickTime(float dt)
{
    if (g_sunAnim) g_clock += dt;
    if (g_keys[VK_OEM_4]) g_clock -= dt * 25.0f;   // [  scrub back
    if (g_keys[VK_OEM_6]) g_clock += dt * 25.0f;   // ]  scrub forward
    g_clock = std::fmod(g_clock, kDayLen);
    if (g_clock < 0.0f) g_clock += kDayLen;

    g_elapsed += dt;
    g_cloudDrift += dt * 2.0f;
    if (g_cloudDrift > TW_CLOUD_RANGE) g_cloudDrift -= 2.0f * TW_CLOUD_RANGE;
}

// ---------------------------------------------------------------------------
// HUD: GDI text drawn on the window DC after SwapBuffers (ISS-005: cosmetic
// overlay only - deleting the drawHud() call changes nothing else)
// ---------------------------------------------------------------------------
static void hudText(HDC dc, int x, int y, const char* s)
{
    SetTextColor(dc, RGB(0, 0, 0));
    TextOutA(dc, x + 1, y + 1, s, (int)strlen(s));
    SetTextColor(dc, RGB(246, 246, 236));
    TextOutA(dc, x, y, s, (int)strlen(s));
}

static const char* compass(float yaw)
{
    static const char* names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    float deg = std::fmod(yaw * 180.0f / kPi, 360.0f);
    if (deg < 0.0f) deg += 360.0f;
    return names[(int)((deg + 22.5f) / 45.0f) % 8];
}

static void drawHud(const SkyState& sky)
{
    static HFONT font = nullptr;
    if (!font)
        font = CreateFontA(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           FIXED_PITCH | FF_MODERN, "Consolas");
    HDC dc = GetDC(g_hwnd);
    HFONT old = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    char line[320];
    const int deg = (int)(sky.elevation * 180.0f / kPi);
    int bearing = (int)(g_yaw * 180.0f / kPi) % 360;
    if (bearing < 0) bearing += 360;
    const char* phase = sky.night > 0.60f ? "night" : (sky.day < 0.55f ? "dusk/dawn" : "day");

    sprintf(line, " Town3D   %5.1f fps   %7d tris   %dx%d   MSAA %d   shadow %dx%d ",
            (double)g_fps, g_solidTris + g_waterTris + g_cloudTris + g_pedTris, g_w, g_h, (int)g_msaa,
            SHADOW_RES, SHADOW_RES);
    hudText(dc, 10, 8, line);
    sprintf(line, " pos %8.1f %8.1f   facing %-2s (%3d)   sun %3d deg %-9s  clock %5.1f  seed %u  peds %d ",
            (double)g_pos.x, (double)g_pos.z, compass(g_yaw), bearing, deg, phase,
            (double)g_clock, g_seed, (int)g_people.peds.size());
    hudText(dc, 10, 28, line);
    sprintf(line, " WASD move   Shift run   mouse look   T sun %s   [ ] time   R new town   P shot   H hud   Esc quit ",
            g_sunAnim ? "AUTO" : "HELD");
    hudText(dc, 10, 48, line);
    if (!g_shadowsOn) hudText(dc, 10, 68, " SHADOWS OFF (debug) ");

    SelectObject(dc, old);
    ReleaseDC(g_hwnd, dc);
}

// ---------------------------------------------------------------------------
// screenshots: 24 bpp BMP written straight out of the back buffer
// ---------------------------------------------------------------------------
static void putU16(unsigned char* p, unsigned v) { p[0] = (unsigned char)(v & 255); p[1] = (unsigned char)((v >> 8) & 255); }
static void putU32(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char)(v & 255); p[1] = (unsigned char)((v >> 8) & 255);
    p[2] = (unsigned char)((v >> 16) & 255); p[3] = (unsigned char)((v >> 24) & 255);
}

static double g_lastLuma = -1.0;

// Every screenshot is also measured, because "it rendered" is not the same as
// "it rendered the town": these numbers catch black frames, blown highlights
// and an empty scene (flat geometry band) without anyone having to eyeball a
// picture.
struct FrameStats
{
    double luma = 0, skyLuma = 0, gndLuma = 0, dark = 0, blown = 0, detail = 0, chroma = 0;
    int colors = 0;
};
static FrameStats g_stats;

static void analyzeFrame(const std::vector<unsigned char>& img, int w, int h)
{
    static unsigned char buckets[32 * 32 * 32];
    memset(buckets, 0, sizeof(buckets));

    double lumaSum = 0.0, skySum = 0.0, gndSum = 0.0, chroma = 0.0, detail = 0.0;
    long n = 0, nSky = 0, nGnd = 0, nDark = 0, nBlown = 0, nDetail = 0;
    const int skyFrom = h * 2 / 3;            // row 0 is the screen bottom (GL order)
    const int gndFrom = h / 3;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const size_t i = ((size_t)y * w + x) * 3;
            const double r = img[i], g = img[i + 1], b = img[i + 2];
            const double L = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            lumaSum += L; ++n;
            if (L < 8.0) ++nDark;
            if (r > 245.0 && g > 245.0 && b > 245.0) ++nBlown;
            chroma += fabs(r - b);
            if (y >= skyFrom) { skySum += L; ++nSky; }
            else if (y < gndFrom) { gndSum += L; ++nGnd; }
            buckets[(((int)r >> 3) << 10) | (((int)g >> 3) << 5) | ((int)b >> 3)] = 1;
            if (x + 4 < w && y < skyFrom)
            {   // horizontal neighbour delta: flat = nothing drawn = suspicious
                const size_t j = ((size_t)y * w + x + 4) * 3;
                detail += fabs(L - (0.2126 * img[j] + 0.7152 * img[j + 1] + 0.0722 * img[j + 2]));
                ++nDetail;
            }
        }
    }
    int colors = 0;
    for (size_t i = 0; i < sizeof(buckets); ++i) colors += buckets[i] ? 1 : 0;

    FrameStats s;
    s.luma = lumaSum / (double)n;
    s.skyLuma = nSky ? skySum / nSky : 0.0;
    s.gndLuma = nGnd ? gndSum / nGnd : 0.0;
    s.dark = 100.0 * (double)nDark / (double)n;
    s.blown = 100.0 * (double)nBlown / (double)n;
    s.detail = nDetail ? detail / (double)nDetail : 0.0;
    s.chroma = chroma / (double)n;
    s.colors = colors;
    g_stats = s;
    g_lastLuma = s.luma;
}

static bool captureBmp(const char* path)
{
    const int w = g_w, h = g_h;
    std::vector<unsigned char> img((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, &img[0]);

    double acc = 0.0;
    for (size_t i = 0; i < img.size(); i += 3)
        acc += 0.2126 * img[i] + 0.7152 * img[i + 1] + 0.0722 * img[i + 2];
    g_lastLuma = acc / (double)((size_t)w * h);
    analyzeFrame(img, w, h);

    const int row = ((w * 3 + 3) / 4) * 4;
    std::vector<unsigned char> out((size_t)row * h, 0);
    for (int y = 0; y < h; ++y)                      // BMP is bottom-up, GL is bottom-up too
    {
        const unsigned char* src = &img[(size_t)y * w * 3];
        unsigned char* dst = &out[(size_t)(h - 1 - y) * row];
        for (int x = 0; x < w; ++x)
        {
            dst[x * 3 + 0] = src[x * 3 + 2];         // B
            dst[x * 3 + 1] = src[x * 3 + 1];         // G
            dst[x * 3 + 2] = src[x * 3 + 0];         // R
        }
    }

    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    putU32(hdr + 2, 54u + (unsigned)out.size());
    putU32(hdr + 10, 54u);
    putU32(hdr + 14, 40u);
    putU32(hdr + 18, (unsigned)w);
    putU32(hdr + 22, (unsigned)h);
    putU16(hdr + 26, 1u);
    putU16(hdr + 28, 24u);
    putU32(hdr + 34, (unsigned)out.size());

    FILE* f = fopen(path, "wb");
    if (!f) { logfmt("[shot] cannot write %s\n", path); return false; }
    const bool ok = fwrite(hdr, 1, 54, f) == 54 && fwrite(&out[0], 1, out.size(), f) == out.size();
    fclose(f);
    if (!ok) logfmt("[shot] short write on %s\n", path);
    return ok;
}

// ---------------------------------------------------------------------------
// --selftest: render fixed views to shots/*.bmp and report numbers, so the run
// is verifiable without a human at the keyboard (verification log V-1)
// ---------------------------------------------------------------------------
struct Shot { const char* name; float x, z, yaw, pitch, clock; };

static float corline(int i) { return -TW_CITY_HALF + (float)i * TW_CELL; }
static float yawToward(const vec3& from, const vec3& to)
{
    return std::atan2(to.x - from.x, -(to.z - from.z));
}

static int drainGlErrors(const char* when)
{
    int n = 0;
    GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR) { logfmt("[gl] error 0x%04x %s\n", (unsigned)e, when); ++n; }
    return n;
}

static int runSelfTest()
{
    CreateDirectoryA("shots", nullptr);
    const vec3 core(0.0f, 0.0f, 0.0f);
    const float riverH = corline(TW_RIVER_H) + TW_ROAD * 0.5f;   // -77
    Shot shots[7];
    int n = 0;
    shots[n] = { "01_downtown_street", corline(4) + 7.0f, corline(6) + 7.0f,
                 yawToward(vec3(corline(4) + 7.0f, 0, corline(6) + 7.0f), core), 0.05f, 60.0f }; ++n;
    shots[n] = { "02_bridge_deck", corline(6) + 7.0f, riverH, 0.0f, -0.07f, 60.0f }; ++n;
    shots[n] = { "03_river_and_skyline", corline(6) + 7.0f, riverH - 25.0f,
                 yawToward(vec3(corline(6) + 7.0f, 0, riverH - 25.0f), core), 0.0f, 60.0f }; ++n;
    shots[n] = { "04_suburb_dusk", g_world.spawn.x, g_world.spawn.z, g_world.spawnYaw, 0.0f, 288.0f }; ++n;
    shots[n] = { "05_night", g_world.spawn.x, g_world.spawn.z, g_world.spawnYaw, 0.0f, 450.0f }; ++n;
    shots[n] = { "06_clouds", g_world.spawn.x, g_world.spawn.z, g_world.spawnYaw + 0.6f, 0.95f, 60.0f }; ++n;

    int errors = 0;

    // Walk the crowd for a few minutes of simulated time first: it spreads them
    // out for the pictures, and the numbers below are how the simulation is
    // verified without anybody having to look at a screenshot.
    if (!g_people.peds.empty())
    {
        g_people.simulate(g_world, 180.0f, g_world.spawn);
        const People::Stats s = g_people.stats(g_world);
        logfmt("[crowd] %d walkers, %d/%d walkable junctions, %d street links | "
               "%d walking %d waiting %d on bridge | %d crossings | "
               "overlaps %d in-walls %d in-water %d\n",
               s.total, g_people.walkableNodes, g_people.nodes, g_people.edges,
               s.walking, s.waiting, s.onBridge, s.crossings, s.overlaps, s.inSolid, s.inWater);
        if (s.overlaps || s.inSolid || s.inWater)
        {
            ++errors;
            logfmt("[crowd] FAULT: bodies overlapped or entered a wall/river\n");
        }
        if (s.crossings == 0)
            logfmt("[crowd] note: nobody crossed a river this run\n");

        // Shot 07 is framed on the busiest clump of walkers, so it cannot come
        // out empty and the crowd-off / crowd-on comparison means something.
        int best = 0, bestNear = -1;
        for (size_t i = 0; i < g_people.peds.size(); ++i)
        {
            int nearby = 0;   // ('near' is a windows.h macro)
            for (size_t k = 0; k < g_people.peds.size(); ++k)
                if (k != i && length(g_people.peds[k].pos - g_people.peds[i].pos) < 30.0f) ++nearby;
            if (nearby > bestNear) { bestNear = nearby; best = (int)i; }
        }
        const Person& q = g_people.peds[best];
        const vec3 fwd(std::sin(q.yaw), 0.0f, -std::cos(q.yaw));
        float px = q.pos.x - fwd.x * 5.0f, pz = q.pos.z - fwd.z * 5.0f;
        if (g_world.blockedAt(px, pz)) { px = q.pos.x; pz = q.pos.z; }   // cornered: shoot from where they are
        shots[n] = { "07_crowd_street", px, pz, yawToward(vec3(px, 0, pz), q.pos), -0.10f, 150.0f }; ++n;
        logfmt("[crowd] shot 07 framed on the busiest street corner (%d walkers within 30 m)\n", bestNear);

        // Ground contact: a walker's feet must sit exactly on the surface under
        // them. This is what "the pedestrians are sunk into the road" looks like
        // in numbers, and it catches any new surface height somebody adds.
        float maxFoot = 0.0f;
        int onStreet = 0, onDeck = 0, onKerb = 0, onLot = 0;
        for (size_t i = 0; i < g_people.peds.size(); ++i)
        {
            const vec3& q = g_people.peds[i].pos;
            const float srf = g_world.surfaceAt(q.x, q.z);
            maxFoot = std::max(maxFoot, std::fabs(q.y - srf));
            if      (srf == TW_DECK_Y)   ++onDeck;
            else if (srf == TW_QUAY_Y)   ++onKerb;
            else if (srf == TW_STREET_Y) ++onStreet;
            else                         ++onLot;
        }
        logfmt("[crowd] ground contact: worst foot error %.3f m | standing on "
               "street %d deck %d kerb %d block %d\n",
               maxFoot, onStreet, onDeck, onKerb, onLot);
        if (maxFoot > 0.001f) { ++errors; logfmt("[crowd] (!) walkers are not on the ground\n"); }

        // Surface table, sampled where each one is known to exist: proves the
        // heights reported match the heights built.
        {
            const AABB d0 = g_world.bridgeZones.empty() ? AABB{0,0,0,0}
                                                        : g_world.bridgeZones[0];
            logfmt("[surface] street %.2f  block %.2f  field %.2f  kerb %.2f  deck %.2f "
                   "(deck centre %.2f)\n",
                   g_world.surfaceAt(corline(3) + 7.0f, corline(1) + 7.0f),
                   g_world.surfaceAt(corline(3) + TW_ROAD + 3.0f, corline(1) + TW_ROAD + 3.0f),
                   g_world.surfaceAt(TW_CITY_HALF + 40.0f, TW_CITY_HALF + 40.0f),
                   g_world.surfaceAt(corline(TW_RIVER_V) + TW_ROAD * 0.5f + TW_RIVER_VH + 0.5f, corline(3) + 7.0f),
                   TW_DECK_Y,
                   g_world.surfaceAt((d0.minX + d0.maxX) * 0.5f, (d0.minZ + d0.maxZ) * 0.5f));
        }

        // Push the player straight at a walker who is held still, and measure how
        // close they get. Bodies are 0.30 m + 0.45 m apart, so anything under that
        // means the player is inside somebody.
        {
            Person& held = g_people.peds[0];
            const vec3 pin = held.pos;
            vec3 from(pin.x + 3.0f, kEyeHigh, pin.z);
            float best2 = 1e9f;
            for (int step = 0; step < 300; ++step)
            {
                held.pos = pin; held.vel = vec3(0, 0, 0); held.wait = 1.0f;   // stand still for the test
                const float dx = pin.x - from.x, dz = pin.z - from.z;
                const float dl = std::sqrt(dx * dx + dz * dz);
                if (dl < 1e-4f) break;
                const float sd = 0.05f;
                if (playerFreeAt(from.x + dx / dl * sd, from.z)) from.x += dx / dl * sd;
                if (playerFreeAt(from.x, from.z + dz / dl * sd)) from.z += dz / dl * sd;
                best2 = best2 < dl ? best2 : dl;
            }
            const float need = held.radius + TW_PLAYER_R;
            logfmt("[crowd] walk-through probe: closest approach %.2f m against a %.2f m body %s\n",
                   (double)best2, (double)need, best2 > need - 0.06f ? "ok" : "FAULT");
            if (best2 <= need - 0.06f) { ++errors; }
            held.wait = 0.0f;
        }
    }
    for (int i = 0; i < n; ++i)
    {
        g_clock = shots[i].clock;
        g_pos = vec3(shots[i].x, kEyeHigh, shots[i].z);
        followGround(1.0f);                       // stand on the surface, don't float
        g_pos.y = g_groundY + kEyeHigh;
        g_yaw = shots[i].yaw;
        g_pitch = shots[i].pitch;

        SkyState sky;
        skyFor(g_clock, sky);
        render(sky);
        errors += drainGlErrors(shots[i].name);

        // how much crowd is actually in this frame?
        int nearPeds = 0; float nearD = 1e9f;
        for (size_t k = 0; k < g_people.peds.size(); ++k)
        {
            const float d = length(g_people.peds[k].pos - vec3(g_pos.x, 0.0f, g_pos.z));
            if (d < 60.0f) ++nearPeds;
            if (d < nearD) nearD = d;
        }

        char path[256];
        sprintf(path, "shots/%s.bmp", shots[i].name);
        const bool saved = captureBmp(path);
        logfmt("[shot] %-20s luma %6.1f  sky %6.1f gnd %6.1f  dark %5.1f%%  blown %4.2f%%  "
               "detail %5.2f  chroma %4.1f  colors %4d\n",
               shots[i].name, g_stats.luma, g_stats.skyLuma, g_stats.gndLuma, g_stats.dark,
               g_stats.blown, g_stats.detail, g_stats.chroma, g_stats.colors);
        logfmt("       sun %5.1f deg  night %4.2f  clock %5.1f  blockedAt=%d  eye %.2f  pedtris %5d  peds<60m %2d  nearest %6.1f  %s\n",
               sky.elevation * 180.0f / kPi, sky.night, (double)g_clock,
               g_world.blockedAt(g_pos.x, g_pos.z) ? 1 : 0, g_pos.y, g_pedTris, nearPeds,
               g_people.peds.empty() ? 0.0f : nearD, saved ? "saved" : "FAILED");
    }
    logfmt("[selftest] done, %d shots, %d GL errors\n", n, errors);
    return errors ? 2 : 0;
}

// ---------------------------------------------------------------------------
// town regeneration (R)
// ---------------------------------------------------------------------------
static void regenerateTown()
{
    g_seed = (unsigned)(GetTickCount() ^ (GetTickCount64() >> 7));
    g_world.build(g_seed);
    g_people.build(g_world, g_seed ^ 0x9E3779B9u, g_pedCount);
    uploadMeshes();
    resetPlayerToSpawn();
    g_people.pushOut(g_pos, TW_PLAYER_R);   // never start inside somebody
    logfmt("[town] regenerated with seed %u\n", g_seed);
}

// ---------------------------------------------------------------------------
// entry point (Console subsystem, so main() + the log work from a shell too)
// ---------------------------------------------------------------------------
static void usage()
{
    printf("Town3D - OpenGL walk-around town\n"
           "  --selftest        render preset views into shots/*.bmp and exit\n"
           "  --seed N          town seed (default 1337)\n"
           "  --width N --height N   window size (default 1280x720)\n"
           "  --clock S         start time on the 600 s day cycle (default 60)\n"
           "  --nomsaa          plain pixel format instead of 4x MSAA\n"
           "  --noshadows       skip the shadow pass (A/B debugging)\n"
           "  --peds N          pedestrians wandering the town (default 120, 0 off)\n"
           "  --showlog         keep the console window visible\n\n"
           "In game: WASD/arrows move, Shift run, mouse look, T sun auto on/off,\n"
           "[ ] scrub time, R new town, P screenshot, H hide HUD, Esc quit.\n");
}

int main(int argc, char** argv)
{
    g_inst = GetModuleHandleA(nullptr);
    int samples = 4;
    bool showLog = false;

    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--selftest")) g_selftest = true;
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) g_seed = (unsigned)strtoul(argv[++i], nullptr, 10);
        else if (!strcmp(argv[i], "--width") && i + 1 < argc) g_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) g_h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--clock") && i + 1 < argc) g_clock = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--nomsaa")) samples = 0;
        else if (!strcmp(argv[i], "--noshadows")) g_shadowsOn = false;
        else if (!strcmp(argv[i], "--peds") && i + 1 < argc) g_pedCount = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--showlog")) showLog = true;
        else if (!strcmp(argv[i], "--trace")) g_trace = true;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else printf("ignoring argument '%s'\n", argv[i]);
    }
    // clamped outside the chain: an if between the else-ifs would break it
    g_pedCount = g_pedCount < 0 ? 0 : (g_pedCount > 400 ? 400 : g_pedCount);
    if (g_w < 160) g_w = 1280;
    if (g_h < 120) g_h = 720;

    { FILE* f = fopen("town3d.log", "wb"); if (f) fclose(f); }
    logfmt("=== Town3D %s ===\n", g_selftest ? "selftest" : "interactive");
    if (!g_selftest && !showLog) ShowWindow(GetConsoleWindow(), SW_HIDE);

    if (!initGL(samples)) { logfmt("[init] OpenGL init failed - see messages above\n"); return 1; }
    g_world.build(g_seed);
    g_people.build(g_world, g_seed ^ 0x9E3779B9u, g_pedCount);
    createGpuObjects();
    setupPedGpuObjects();
    resetPlayerToSpawn();
    g_people.pushOut(g_pos, TW_PLAYER_R);
    drainGlErrors("after init");
    logfmt("[init] ready (seed %u, %d shadow casters)\n", g_seed, g_solidTris);

    if (g_selftest)
    {
        const int rc = runSelfTest();
        logfmt("=== Town3D selftest end (%d) ===\n", rc);
        return rc;
    }

    setCursorHidden(true);

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);
    double fpsAcc = 0.0;
    int fpsFrames = 0;
    int rc = 0;

    while (g_run)
    {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) { logfmt("[win] WM_QUIT after %u frames\n", g_frames); g_run = false; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart);
        prev = now;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.0f) dt = 0.0f;

        lookAround();
        movePlayer(dt);
        g_people.update(g_world, dt, g_pos);
        tickTime(dt);
        if (g_wantRegen) { g_wantRegen = false; regenerateTown(); }

        SkyState sky;
        skyFor(g_clock, sky);
        render(sky);

        if (g_wantShot)
        {
            g_wantShot = false;
            CreateDirectoryA("shots", nullptr);
            char path[256];
            sprintf(path, "shots/manual_%lu.bmp", (unsigned long)GetTickCount());
            if (captureBmp(path)) logfmt("[shot] wrote %s (luma %.2f)\n", path, g_lastLuma);
        }

        SwapBuffers(g_hdc);
        if (g_hud) drawHud(sky);

        fpsAcc += dt;
        ++fpsFrames;
        if (fpsAcc >= 0.5)
        {
            g_fps = (float)((double)fpsFrames / fpsAcc);
            fpsAcc = 0.0;
            fpsFrames = 0;
        }
        if (++g_frameErrors % 600 == 0) rc = drainGlErrors("frame");
        if (g_trace && g_frames && g_frames % 240 == 0)
            logfmt("[perf] frame %u  %.1f fps  pos %.1f,%.1f eye %.2f  clock %.1f  tris %d\n", g_frames,
                   (double)g_fps, (double)g_pos.x, (double)g_pos.z, (double)g_pos.y, (double)g_clock,
                   g_solidTris + g_waterTris + g_cloudTris + g_pedTris);
    }

    setCursorHidden(false);
    logfmt("[exit] loop ended after %u frames (last frame errors %d)\n", g_frames, rc);
    return 0;
}
