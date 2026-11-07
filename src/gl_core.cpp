// gl_core.cpp - see gl_core.h
#include "gl_core.h"
#include <stdio.h>

GLFunctions gl = {};

// wglGetProcAddress returns 1..4 (and -1) as error sentinels, not NULL.
static bool badProc(void* p)
{
    INT_PTR v = (INT_PTR)p;
    return (v == 0 || v == 1 || v == 2 || v == 3 || v == 4 || v == -1);
}

static void* fetch(const char* name)
{
    void* p = (void*)wglGetProcAddress(name);
    if (badProc(p))
    {
        // Core-in-opengl32.dll functions (GL 1.1) are not exposed by wglGetProcAddress.
        HMODULE mod = GetModuleHandleA("opengl32.dll");
        p = (void*)GetProcAddress(mod, name);
    }
    if (badProc(p))
    {
        printf("  [gl] missing entry point: %s\n", name);
        return nullptr;
    }
    return p;
}

#define LOAD(member, name)                                        \
    do {                                                          \
        void* p__ = fetch(name);                                  \
        if (!p__) return false;                                   \
        *(void**)&gl.member = p__;                                \
    } while (0)

bool glLoadAll()
{
    LOAD(GenVertexArrays,   "glGenVertexArrays");
    LOAD(BindVertexArray,   "glBindVertexArray");
    LOAD(DeleteVertexArrays,"glDeleteVertexArrays");
    LOAD(GenBuffers,        "glGenBuffers");
    LOAD(DeleteBuffers,     "glDeleteBuffers");
    LOAD(BindBuffer,        "glBindBuffer");
    LOAD(BufferData,        "glBufferData");
    LOAD(EnableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(VertexAttribPointer,     "glVertexAttribPointer");

    LOAD(CreateShader,      "glCreateShader");
    LOAD(ShaderSource,      "glShaderSource");
    LOAD(CompileShader,     "glCompileShader");
    LOAD(GetShaderiv,       "glGetShaderiv");
    LOAD(GetIntegerv,       "glGetIntegerv");
    LOAD(GetShaderInfoLog,  "glGetShaderInfoLog");
    LOAD(DeleteShader,      "glDeleteShader");
    LOAD(CreateProgram,     "glCreateProgram");
    LOAD(AttachShader,      "glAttachShader");
    LOAD(LinkProgram,       "glLinkProgram");
    LOAD(GetProgramiv,      "glGetProgramiv");
    LOAD(GetProgramInfoLog, "glGetProgramInfoLog");
    LOAD(DeleteProgram,     "glDeleteProgram");
    LOAD(UseProgram,        "glUseProgram");
    LOAD(GetUniformLocation,"glGetUniformLocation");
    LOAD(GetAttribLocation, "glGetAttribLocation");
    LOAD(Uniform1i,         "glUniform1i");
    LOAD(Uniform1f,         "glUniform1f");
    LOAD(Uniform2f,         "glUniform2f");
    LOAD(Uniform3f,         "glUniform3f");
    LOAD(UniformMatrix4fv,  "glUniformMatrix4fv");

    LOAD(ActiveTexture,     "glActiveTexture");
    LOAD(GenTextures,       "glGenTextures");
    LOAD(BindTexture,       "glBindTexture");
    LOAD(TexImage2D,        "glTexImage2D");
    LOAD(TexParameteri,     "glTexParameteri");
    LOAD(GenFramebuffers,   "glGenFramebuffers");
    LOAD(BindFramebuffer,   "glBindFramebuffer");
    LOAD(FramebufferTexture2D,"glFramebufferTexture2D");
    LOAD(CheckFramebufferStatus,"glCheckFramebufferStatus");
    LOAD(DeleteFramebuffers,"glDeleteFramebuffers");
    LOAD(GetIntegeri_v,     "glGetIntegeri_v");

    LOAD(DrawArrays,        "glDrawArrays");
    return true;
}
