#include "egl.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>


fbdev_window window;

void _Egl_CheckError(const char* file, int line)
{
	EGLint error = eglGetError();
	if (error != EGL_SUCCESS)
	{
		printf("eglGetError(): %i (0x%.4x) - Failed at %s:%i\n", (int)error, (int)error, file, line);
		exit(1);
	}
}

EGLDisplay Egl_Initialize()
{

	// Get the EGL display (fb0)
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (display == EGL_NO_DISPLAY)
	{
		printf("eglGetDisplay failed.\n");
		exit(1);
	}


	// Initialize EGL
	EGLint major;
	EGLint minor;
	EGLBoolean success = eglInitialize(display, &major, &minor);
	if (success != EGL_TRUE)
	{
		Egl_CheckError();
	}

	printf("EGL: major=%d, minor=%d\n", major, minor);
	printf("EGL: Vendor=%s\n", eglQueryString(display, EGL_VENDOR));
	printf("EGL: Version=%s\n", eglQueryString(display, EGL_VERSION));
	printf("EGL: ClientAPIs=%s\n", eglQueryString(display, EGL_CLIENT_APIS));
	printf("EGL: Extensions=%s\n", eglQueryString(display, EGL_EXTENSIONS));
	printf("EGL: ClientExtensions=%s\n", eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));
	printf("\n");

	return display;
}


EGLSurface Egl_CreateWindow(EGLDisplay display, EGLConfig* outConfig)
{
	// Open the framebuffer to determine its properties
	int fd_fb0 = open("/dev/fb0", O_RDWR);
	printf("file handle: %x\n", fd_fb0);

	fb_var_screeninfo info;
	int ret = ioctl(fd_fb0, FBIOGET_VSCREENINFO, &info);
	if (ret < 0)
	{
		printf("FBIOGET_VSCREENINFO failed.\n");
		exit(1);
	}

	close(fd_fb0);

	int width = info.xres;
	int height = info.yres;
	int bpp = info.bits_per_pixel;
	int dataLen = width * height * (bpp / 8);

	printf("screen info: width=%d, height=%d, bpp=%d\n", width, height, bpp);


	// Set the EGL window size
	window.width = width;
	window.height = height;


	// Find a config
	int redSize;
	int greenSize;
	int blueSize;
	int alphaSize;
	int depthSize = 24;
	int stencilSize = 8;

	if (bpp < 32)
	{
		redSize = 5;
		greenSize = 6;
		blueSize = 5;
		alphaSize = 0;
	}
	else
	{
		redSize = 8;
		greenSize = 8;
		blueSize = 8;
		alphaSize = 8;
	}

	EGLint configAttributes[] =
	{
		EGL_RED_SIZE,            redSize,
		EGL_GREEN_SIZE,          greenSize,
		EGL_BLUE_SIZE,           blueSize,
		EGL_ALPHA_SIZE,          alphaSize,

		EGL_DEPTH_SIZE,          depthSize,
		EGL_STENCIL_SIZE,        stencilSize,

		EGL_SURFACE_TYPE,        EGL_WINDOW_BIT ,

		EGL_NONE
	};


	int num_configs;
	EGLBoolean success = eglChooseConfig(display, configAttributes, NULL, 0, &num_configs);
	if (success != EGL_TRUE)
	{
		Egl_CheckError();
	}


	EGLConfig* configs = new EGLConfig[num_configs];
	success = eglChooseConfig(display, configAttributes, configs, num_configs, &num_configs);
	if (success != EGL_TRUE)
	{
		Egl_CheckError();
	}


	EGLConfig match = 0;

	for (int i = 0; i < num_configs; ++i)
	{
		EGLint configRedSize;
		EGLint configGreenSize;
		EGLint configBlueSize;
		EGLint configAlphaSize;
		EGLint configDepthSize;
		EGLint configStencilSize;

		eglGetConfigAttrib(display, configs[i], EGL_RED_SIZE, &configRedSize);
		eglGetConfigAttrib(display, configs[i], EGL_GREEN_SIZE, &configGreenSize);
		eglGetConfigAttrib(display, configs[i], EGL_BLUE_SIZE, &configBlueSize);
		eglGetConfigAttrib(display, configs[i], EGL_ALPHA_SIZE, &configAlphaSize);
		eglGetConfigAttrib(display, configs[i], EGL_DEPTH_SIZE, &configDepthSize);
		eglGetConfigAttrib(display, configs[i], EGL_STENCIL_SIZE, &configStencilSize);

		if (configRedSize == redSize &&
			configBlueSize == blueSize &&
			configGreenSize == greenSize &&
			configAlphaSize == alphaSize &&
			configDepthSize == depthSize &&
			configStencilSize == stencilSize)
		{
			match = configs[i];
			break;
		}
	}

	delete[] configs;

	if (match == 0)
	{
		printf("No eglConfig match found.\n");
		exit(1);
	}

	*outConfig = match;
	printf("EGLConfig match found: (%p)\n", match);


	EGLint windowAttr[] = {
		EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
		EGL_NONE };

	EGLSurface surface = eglCreateWindowSurface(display, match, (NativeWindowType)&window, windowAttr);

	if (surface == EGL_NO_SURFACE)
	{
		Egl_CheckError();
	}


	return surface;
}

EGLContext Egl_CreateContext(EGLDisplay display, EGLSurface surface, EGLConfig config)
{
	// Create a context
	eglBindAPI(EGL_OPENGL_ES_API);

	EGLint contextAttributes[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE };

	EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
	if (context == EGL_NO_CONTEXT)
	{
		Egl_CheckError();
	}

	EGLBoolean success = eglMakeCurrent(display, surface, surface, context);
	if (success != EGL_TRUE)
	{
		Egl_CheckError();
	}
}