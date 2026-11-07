// math3d.h - tiny column-major mat4 / vec3 helpers (no external math library)
#pragma once
#include <cmath>

struct vec3
{
    float x = 0, y = 0, z = 0;
    vec3() {}
    vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

inline vec3 operator+(const vec3& a, const vec3& b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline vec3 operator-(const vec3& a, const vec3& b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline vec3 operator*(const vec3& a, float s)       { return vec3(a.x * s, a.y * s, a.z * s); }
inline vec3 operator-(const vec3& a)                { return vec3(-a.x, -a.y, -a.z); }

inline float dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3& a, const vec3& b)
{
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float length(const vec3& a) { return std::sqrt(dot(a, a)); }
inline vec3 normalize(const vec3& a)
{
    float l = length(a);
    return l > 1e-8f ? vec3(a.x / l, a.y / l, a.z / l) : vec3(0, 1, 0);
}
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline vec3 lerp(const vec3& a, const vec3& b, float t)
{
    return vec3(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t));
}
inline vec3 mulc(const vec3& a, const vec3& b) { return vec3(a.x * b.x, a.y * b.y, a.z * b.z); }

// Column-major 4x4 matrix, OpenGL layout: m[col*4 + row]
struct mat4
{
    float m[16];
};

inline mat4 mat4Identity()
{
    mat4 r = {};
    for (int i = 0; i < 16; ++i) r.m[i] = 0;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

inline mat4 mat4Mul(const mat4& a, const mat4& b)   // a * b
{
    mat4 r = {};
    for (int c = 0; c < 4; ++c)
        for (int rw = 0; rw < 4; ++rw)
        {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}

inline mat4 mat4Perspective(float fovyRad, float aspect, float znear, float zfar)
{
    const float f = 1.0f / std::tan(fovyRad * 0.5f);
    mat4 r = {};
    for (int i = 0; i < 16; ++i) r.m[i] = 0;
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

inline mat4 mat4Ortho(float l, float r_, float b, float t, float zn, float zf)
{
    mat4 r = {};
    for (int i = 0; i < 16; ++i) r.m[i] = 0;
    r.m[0] = 2.0f / (r_ - l);
    r.m[5] = 2.0f / (t - b);
    r.m[10] = -2.0f / (zf - zn);
    r.m[12] = -(r_ + l) / (r_ - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(zf + zn) / (zf - zn);
    r.m[15] = 1.0f;
    return r;
}

inline mat4 mat4LookAt(const vec3& eye, const vec3& center, const vec3& up)
{
    vec3 f = normalize(center - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    mat4 r = {};
    for (int i = 0; i < 16; ++i) r.m[i] = 0;
    r.m[0] = s.x;  r.m[1] = u.x;  r.m[2] = -f.x; r.m[3] = 0;
    r.m[4] = s.y;  r.m[5] = u.y;  r.m[6] = -f.y; r.m[7] = 0;
    r.m[8] = s.z;  r.m[9] = u.z;  r.m[10] = -f.z; r.m[11] = 0;
    r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye); r.m[15] = 1;
    return r;
}
