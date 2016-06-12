#pragma once

#include <stdio.h>
#include <EGL/egl.h>
#include <stdlib.h>

inline void Egl_CheckError()
{
	EGLint error = eglGetError();
	if (error != EGL_SUCCESS)
	{
		printf("eglGetError(): %i (0x%.4x)\n", (int)error, (int)error);
		printf("Failed at %s:%i\n", __FILE__, __LINE__);
		exit(1);
	}
}


EGLDisplay Egl_Initialize();
EGLSurface Egl_CreateWindow(EGLDisplay display, EGLConfig* outConfig);
EGLContext Egl_CreateContext(EGLDisplay display, EGLSurface surface, EGLConfig config);
