// gl_core.h - minimal, dependency-free OpenGL 3.3 core loader for Windows.
//
// The <GL/gl.h> shipped with the Windows SDK stops at OpenGL 1.1, so every
// 2.x/3.x entry point has to be fetched from the driver with wglGetProcAddress.
// Instead of pulling in GLEW/GLAD/gl3w we declare the handful of tokens and
// function pointers this project actually uses. No external libraries needed:
// linking only requires opengl32.lib / user32.lib / gdi32.lib.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

// ---------------------------------------------------------------------------
// Types missing from the ancient SDK header
// ---------------------------------------------------------------------------
typedef char          GLchar;
typedef long long     GLintptr;
typedef long long     GLsizeiptr;

// ---------------------------------------------------------------------------
// GL tokens (2.0 - 3.3) not present in <GL/gl.h>
// ---------------------------------------------------------------------------
#define GL_ARRAY_BUFFER                     0x8892
#define GL_ELEMENT_ARRAY_BUFFER             0x8893
#define GL_STATIC_DRAW                      0x88E4
#define GL_DYNAMIC_DRAW                     0x88E8

#define GL_FRAGMENT_SHADER                  0x8B30
#define GL_VERTEX_SHADER                    0x8B31
#define GL_COMPILE_STATUS                   0x8B81
#define GL_LINK_STATUS                      0x8B82
#define GL_INFO_LOG_LENGTH                  0x8B84

#define GL_DEPTH_COMPONENT16                0x81A5
#define GL_DEPTH_COMPONENT24                0x81A6
#define GL_DEPTH_COMPONENT32F               0x81A7

#define GL_FRAMEBUFFER                      0x8D40
#define GL_READ_FRAMEBUFFER                 0x8CA8
#define GL_DRAW_FRAMEBUFFER                 0x8CA9
#define GL_FRAMEBUFFER_COMPLETE             0x8CD5
#define GL_DEPTH_ATTACHMENT                 0x8D00
#define GL_COLOR_ATTACHMENT0                0x8CE0

#define GL_TEXTURE_COMPARE_MODE             0x884C
#define GL_TEXTURE_COMPARE_FUNC             0x884D
#define GL_COMPARE_REF_TO_TEXTURE           0x884E
#define GL_LEQUAL                           0x0203
#define GL_TEXTURE_MIN_FILTER               0x2801
#define GL_TEXTURE_MAG_FILTER               0x2800
#define GL_TEXTURE_WRAP_S                   0x2802
#define GL_TEXTURE_WRAP_T                   0x2803
#define GL_LINEAR                           0x2601
#define GL_NEAREST                          0x2600
#define GL_CLAMP_TO_EDGE                    0x812F
#define GL_TEXTURE0                         0x84C0

#define GL_MAJOR_VERSION                    0x82B2
#define GL_MINOR_VERSION                    0x82B3
#define GL_MULTISAMPLE                      0x809D
#ifndef GL_SAMPLES
#define GL_SAMPLES                          0x80A9
#endif

// WGL (ARB_create_context / ARB_multisample)
#define WGL_DRAW_TO_WINDOW_ARB              0x2001
#define WGL_SUPPORT_OPENGL_ARB              0x2010
#define WGL_DOUBLE_BUFFER_ARB               0x2011
#define WGL_PIXEL_TYPE_ARB                  0x2013
#define WGL_TYPE_RGBA_ARB                   0x202B
#define WGL_COLOR_BITS_ARB                  0x2014
#define WGL_DEPTH_BITS_ARB                  0x2022
#define WGL_SAMPLES_ARB                     0x2042
#define WGL_SAMPLE_BUFFERS_ARB              0x2041
#define WGL_CONTEXT_MAJOR_VERSION_ARB       0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB       0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB        0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB    0x00000001

struct GLFunctions
{
    // buffers / VAOs
    void (APIENTRY *GenVertexArrays)(GLsizei, GLuint*);
    void (APIENTRY *BindVertexArray)(GLuint);
    void (APIENTRY *DeleteVertexArrays)(GLsizei, const GLuint*);
    void (APIENTRY *GenBuffers)(GLsizei, GLuint*);
    void (APIENTRY *DeleteBuffers)(GLsizei, const GLuint*);
    void (APIENTRY *BindBuffer)(GLenum, GLuint);
    void (APIENTRY *BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    void (APIENTRY *EnableVertexAttribArray)(GLuint);
    void (APIENTRY *VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    // shaders
    GLuint (APIENTRY *CreateShader)(GLenum);
    void (APIENTRY *ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (APIENTRY *CompileShader)(GLuint);
    // glGetIntegerv is GL 1.1, so it is also reachable through the opengl32.lib
    // import - but in a 3.x core context only the driver's own entry point knows
    // GL_MAJOR_VERSION, so the loader keeps its own pointer too.
    void (APIENTRY *GetIntegerv)(GLenum, GLint*);
    void (APIENTRY *GetShaderiv)(GLuint, GLenum, GLint*);
    void (APIENTRY *GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (APIENTRY *DeleteShader)(GLuint);
    GLuint (APIENTRY *CreateProgram)();
    void (APIENTRY *AttachShader)(GLuint, GLuint);
    void (APIENTRY *LinkProgram)(GLuint);
    void (APIENTRY *GetProgramiv)(GLuint, GLenum, GLint*);
    void (APIENTRY *GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (APIENTRY *DeleteProgram)(GLuint);
    void (APIENTRY *UseProgram)(GLuint);
    GLint (APIENTRY *GetUniformLocation)(GLuint, const GLchar*);
    GLint (APIENTRY *GetAttribLocation)(GLuint, const GLchar*);
    void (APIENTRY *Uniform1i)(GLint, GLint);
    void (APIENTRY *Uniform1f)(GLint, GLfloat);
    void (APIENTRY *Uniform2f)(GLint, GLfloat, GLfloat);
    void (APIENTRY *Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void (APIENTRY *UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    // textures / FBO
    void (APIENTRY *ActiveTexture)(GLenum);
    void (APIENTRY *GenTextures)(GLsizei, GLuint*);
    void (APIENTRY *BindTexture)(GLenum, GLuint);
    void (APIENTRY *TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (APIENTRY *TexParameteri)(GLenum, GLenum, GLint);
    void (APIENTRY *GenFramebuffers)(GLsizei, GLuint*);
    void (APIENTRY *BindFramebuffer)(GLenum, GLuint);
    void (APIENTRY *FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (APIENTRY *CheckFramebufferStatus)(GLenum);
    void (APIENTRY *DeleteFramebuffers)(GLsizei, GLuint*);
    void (APIENTRY *GetIntegeri_v)(GLenum, GLuint, GLint*);
    // drawing
    void (APIENTRY *DrawArrays)(GLenum, GLint, GLsizei);
};

extern GLFunctions gl;

// Fetch every entry point. Requires a current context (any 3.x core context).
// Returns false and lists the missing functions if something could not load.
bool glLoadAll();
