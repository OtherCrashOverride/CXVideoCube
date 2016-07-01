#pragma once

#include <stdio.h>
#include <EGL/egl.h>
#include <stdlib.h>

#define Egl_CheckError(x) ({_Egl_CheckError(__FILE__, __LINE__);})

void _Egl_CheckError(const char* file, int line);
EGLDisplay Egl_Initialize();
EGLSurface Egl_CreateWindow(EGLDisplay display, EGLConfig* outConfig);
EGLContext Egl_CreateContext(EGLDisplay display, EGLSurface surface, EGLConfig config);
