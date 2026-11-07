// shaders.h - GLSL 330 core sources (kept in one place so they are easy to read
// side by side with the C++ that feeds their uniforms).
#pragma once

// ---------------------------------------------------------------------------
// Solid pass: flat-coloured boxes, one directional (sun) light with a shadow
// map, hemisphere ambient, procedural window grids, exponential-squared fog.
// ---------------------------------------------------------------------------
static const char* VS_SOLID = "#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNrm;\n"
"layout(location=2) in vec3 aCol;\n"
"layout(location=3) in vec2 aWin;\n"
"uniform mat4 uProj, uView, uLightVP;\n"
"out vec3 vN, vC, vW;\n"
"out vec4 vLS;\n"
"out vec2 vWin;\n"
"void main(){\n"
"  vec4 w = vec4(aPos, 1.0);\n"
"  vW = aPos; vN = aNrm; vC = aCol; vWin = aWin;\n"
"  vLS = uLightVP * w;\n"
"  gl_Position = uProj * uView * w;\n"
"}\n";

static const char* FS_SOLID = "#version 330 core\n"
"in vec3 vN, vC, vW;\n"
"in vec4 vLS;\n"
"in vec2 vWin;          // x: window density, y: per-building random seed\n"
"uniform vec3 uSunDir, uSunCol, uAmbSky, uAmbGnd, uFogCol, uCam;\n"
"uniform sampler2DShadow uShadow;\n"
"uniform float uTexel, uNight, uFogK, uShadowOn;\n"
"out vec4 oCol;\n"
"float shadowFactor(float ndl){\n"
"  if (uShadowOn < 0.5) return 1.0;\n"
"  vec3 p = vLS.xyz / vLS.w * 0.5 + 0.5;\n"
"  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;\n"
"  float bias = max(0.0008, 0.0040 * (1.0 - ndl));\n"
"  float s = 0.0;\n"
"  for (int y = -1; y <= 1; ++y)\n"
"    for (int x = -1; x <= 1; ++x)\n"
"      s += texture(uShadow, vec3(p.xy + vec2(float(x), float(y)) * uTexel, p.z - bias));\n"
"  s /= 9.0;\n"
"  // fade the shadow out before it leaves the ortho box (no hard seam)\n"
"  float d = length(vW - uCam);\n"
"  return mix(1.0, s, 1.0 - smoothstep(118.0, 158.0, d));\n"
"}\n"
"float windowMask(vec3 w, vec3 n){\n"
"  float wall = 1.0 - abs(n.y);\n"
"  float u = abs(n.x) > 0.5 ? w.z : w.x;\n"
"  vec2 g = fract(vec2(u / 3.4, (w.y - 1.0) / 3.6));\n"
"  float mx = step(0.17, g.x) * step(g.x, 0.83);\n"
"  float my = step(0.26, g.y) * step(g.y, 0.78);\n"
"  return mx * my * wall;\n"
"}\n"
"float hash1(float x){ return fract(sin(x) * 43758.5453); }\n"
"void main(){\n"
"  vec3 N = normalize(vN);\n"
"  vec3 V = normalize(uCam - vW);\n"
"  float ndl = max(dot(N, uSunDir), 0.0);\n"
"  float sh = shadowFactor(ndl);\n"
"  float win = windowMask(vW, N) * vWin.x;\n"
"  float cell = floor(vW.x / 3.4) * 37.0 + floor((vW.y - 1.0) / 3.6) * 11.0 + vWin.y * 91.0;\n"
"  float lit = step(0.45, hash1(cell));\n"
"  vec3 winDay = vec3(0.10, 0.13, 0.18);\n"
"  vec3 winNight = vec3(1.00, 0.80, 0.50);\n"
"  vec3 winCol = mix(winDay, winNight, uNight * lit);\n"
"  vec3 albedo = mix(vC, winCol, win * 0.9);\n"
"  vec3 amb = uAmbSky * (0.55 + 0.45 * N.y) + uAmbGnd * (0.55 - 0.55 * N.y);\n"
"  vec3 col = albedo * (amb + uSunCol * ndl * sh);\n"
"  col += winCol * win * lit * (0.10 + 1.30 * uNight);\n"
"  float spec = pow(max(dot(reflect(-V, N), uSunDir), 0.0), 26.0) * sh * win;\n"
"  col += uSunCol * spec * 0.30;\n"
"  float d = length(vW - uCam);\n"
"  float fog = 1.0 - exp(-pow(d * uFogK, 1.8));\n"
"  col = mix(col, uFogCol, clamp(fog, 0.0, 1.0));\n"
"  oCol = vec4(col, 1.0);\n"
"}\n";

// ---------------------------------------------------------------------------
// Shadow depth pass (colour output deliberately absent: depth-only FBO)
// ---------------------------------------------------------------------------
static const char* VS_DEPTH = "#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"uniform mat4 uLightVP;\n"
"void main(){ gl_Position = uLightVP * vec4(aPos, 1.0); }\n";

static const char* FS_DEPTH = "#version 330 core\n"
"void main(){ }\n";

// ---------------------------------------------------------------------------
// Sky: one full-screen triangle, ray rebuilt from the camera basis.
// ---------------------------------------------------------------------------
static const char* VS_SKY = "#version 330 core\n"
"out vec2 vNdc;\n"
"void main(){\n"
"  float x = float((gl_VertexID & 1) << 2) - 1.0;\n"
"  float y = float((gl_VertexID & 2) << 1) - 1.0;\n"
"  vNdc = vec2(x, y);\n"
"  gl_Position = vec4(x, y, 1.0, 1.0);\n"
"}\n";

static const char* FS_SKY = "#version 330 core\n"
"in vec2 vNdc;\n"
"uniform vec3 uCam, uF, uR, uU, uSunDir, uSunCol, uZenith, uHorizon, uHaze;\n"
"uniform float uTanHalf, uAspect, uNight;\n"
"out vec4 oCol;\n"
"float hash31(vec3 p){\n"
"  p = fract(p * 0.3183099 + vec3(0.11, 0.17, 0.13));\n"
"  p *= 17.0;\n"
"  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));\n"
"}\n"
"void main(){\n"
"  vec3 dir = normalize(uF + uR * (vNdc.x * uAspect * uTanHalf) + uU * (vNdc.y * uTanHalf));\n"
"  vec3 col = mix(uHorizon, uZenith, pow(max(dir.y, 0.0), 0.55));\n"
"  float sd = max(dot(dir, uSunDir), 0.0);\n"
"  col += uSunCol * smoothstep(0.999950, 0.999990, sd) * 5.0;   // the sun itself\n"
"  col += uSunCol * pow(sd, 900.0) * 0.9;                       // tight bloom\n"
"  col += uSunCol * pow(sd, 14.0) * 0.18;                       // inner glow\n"
"  col += uSunCol * pow(sd, 3.0) * 0.05;                        // broad sky glow\n"
"  if (dir.y > 0.0 && uNight > 0.01) {\n"
"    float st = step(0.9988, hash31(floor(dir * 240.0)));\n"
"    col += vec3(st) * uNight * smoothstep(0.02, 0.30, dir.y);\n"
"  }\n"
"  col = mix(col, uHaze, smoothstep(0.03, -0.22, dir.y));\n"
"  oCol = vec4(col, 1.0);\n"
"}\n";

// ---------------------------------------------------------------------------
// Water: translucent, animated ripple normal, sun glitter, fresnel sky sheen
// ---------------------------------------------------------------------------
static const char* VS_WATER = "#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNrm;\n"
"layout(location=2) in vec3 aCol;\n"
"uniform mat4 uProj, uView;\n"
"uniform float uTime;\n"
"out vec3 vW, vC;\n"
"void main(){\n"
"  vec3 p = aPos;\n"
"  p.y += sin(p.x * 0.09 + uTime * 1.1) * 0.05 + cos(p.z * 0.11 + uTime * 0.8) * 0.05;\n"
"  vW = p; vC = aCol;\n"
"  gl_Position = uProj * uView * vec4(p, 1.0);\n"
"}\n";

static const char* FS_WATER = "#version 330 core\n"
"in vec3 vW, vC;\n"
"uniform vec3 uCam, uSunDir, uSunCol, uFogCol;\n"
"uniform float uFogK, uTime;\n"
"out vec4 oCol;\n"
"void main(){\n"
"  vec3 V = normalize(vW - uCam);\n"
"  vec3 N = normalize(vec3(\n"
"      0.075 * sin(vW.x * 0.55 + uTime * 1.7) + 0.045 * cos(vW.z * 0.9 - uTime * 1.1),\n"
"      1.0,\n"
"      0.075 * cos(vW.z * 0.6 - uTime * 1.3) + 0.045 * sin(vW.x * 0.8 + uTime * 0.9)));\n"
"  float ndl = max(dot(N, uSunDir), 0.0);\n"
"  vec3 col = vC * (0.30 + 0.75 * ndl);\n"
"  float fres = pow(1.0 - max(dot(N, -V), 0.0), 3.0);\n"
"  col = mix(col, uFogCol * 0.85 + vec3(0.03, 0.05, 0.07), 0.25 + 0.55 * fres);\n"
"  vec3 R = reflect(-uSunDir, N);\n"
"  col += uSunCol * pow(max(dot(R, -V), 0.0), 90.0) * 1.5;\n"
"  float d = length(vW - uCam);\n"
"  float fog = 1.0 - exp(-pow(d * uFogK, 1.8));\n"
"  col = mix(col, uFogCol, clamp(fog, 0.0, 1.0));\n"
"  oCol = vec4(col, 0.80);\n"
"}\n";

// ---------------------------------------------------------------------------
// Clouds: unlit alpha quads tinted on the CPU side; alpha fades with distance
// so the edge of the cloud field is never visible.
// ---------------------------------------------------------------------------
static const char* VS_CLOUD = "#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec4 aColA;\n"
"uniform mat4 uProj, uView;\n"
"uniform vec3 uDrift;\n"
"out vec4 vC;\n"
"out vec3 vW;\n"
"void main(){\n"
"  vec3 p = aPos + uDrift;\n"
"  vW = p; vC = aColA;\n"
"  gl_Position = uProj * uView * vec4(p, 1.0);\n"
"}\n";

static const char* FS_CLOUD = "#version 330 core\n"
"in vec4 vC;\n"
"in vec3 vW;\n"
"uniform vec3 uCam, uCloudTint, uFogCol;\n"
"uniform float uFogK;\n"
"out vec4 oCol;\n"
"void main(){\n"
"  float d = length(vW - uCam);\n"
"  vec3 col = vC.rgb * uCloudTint;\n"
"  float a = vC.a * (1.0 - smoothstep(1000.0, 2100.0, d));\n"
"  a *= 1.0 - clamp(1.0 - exp(-pow(d * uFogK, 1.8)), 0.0, 1.0) * 0.65;\n"
"  if (a < 0.01) discard;\n"
"  oCol = vec4(mix(col, uFogCol, 0.25), a);\n"
"}\n";
