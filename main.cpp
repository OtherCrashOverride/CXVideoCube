// g++ -o CXVideoCube main.cpp egl.cpp Exception.cpp Matrix4.cpp Vector3.cpp -L/usr/lib/aml_libs/ -lamcodec -lamadec -lamavutils -lasound -lMali -lpthread

#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>	//mmap
#include <sys/ioctl.h>
#include <pthread.h>
#include <sys/time.h>
#include <linux/fb.h>
#include <cstdlib>	//rand
#include <errno.h>
#include <linux/videodev2.h> // V4L

// The headers are not aware C++ exists
extern "C"
{
//#include <amcodec/codec.h>
#include <codec.h>
}

#include "egl.h"
#include <GLES2/gl2.h>

#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/eglext.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2ext.h>


#include "Matrix4.h"
#include "Exception.h"

#include <drm/drm_fourcc.h>


// Ion video header from drivers\staging\android\uapi\ion.h
#include "ion.h"


// Codec parameter flags
//    size_t is used to make it 
//    64bit safe for use on Odroid C2
const size_t EXTERNAL_PTS = 0x01;
const size_t SYNC_OUTSIDE = 0x02;
const size_t USE_IDR_FRAMERATE = 0x04;
const size_t UCODE_IP_ONLY_PARAM = 0x08;
const size_t MAX_REFER_BUF = 0x10;
const size_t ERROR_RECOVERY_MODE_IN = 0x20;

// Buffer size
const int BUFFER_SIZE = 1024 * 32;	// 4K video expected


// EGL_EXT_image_dma_buf_import
#define EGL_LINUX_DMA_BUF_EXT          0x3270

#define EGL_LINUX_DRM_FOURCC_EXT        0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT       0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT   0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT    0x3274
#define EGL_DMA_BUF_PLANE1_FD_EXT       0x3275
#define EGL_DMA_BUF_PLANE1_OFFSET_EXT   0x3276
#define EGL_DMA_BUF_PLANE1_PITCH_EXT    0x3277
#define EGL_DMA_BUF_PLANE2_FD_EXT       0x3278
#define EGL_DMA_BUF_PLANE2_OFFSET_EXT   0x3279
#define EGL_DMA_BUF_PLANE2_PITCH_EXT    0x327A
#define EGL_YUV_COLOR_SPACE_HINT_EXT    0x327B
#define EGL_SAMPLE_RANGE_HINT_EXT       0x327C
#define EGL_YUV_CHROMA_HORIZONTAL_SITING_HINT_EXT  0x327D
#define EGL_YUV_CHROMA_VERTICAL_SITING_HINT_EXT    0x327E

#define EGL_ITU_REC601_EXT   0x327F
#define EGL_ITU_REC709_EXT   0x3280
#define EGL_ITU_REC2020_EXT  0x3281

#define EGL_YUV_FULL_RANGE_EXT    0x3282
#define EGL_YUV_NARROW_RANGE_EXT  0x3283

#define EGL_YUV_CHROMA_SITING_0_EXT    0x3284
#define EGL_YUV_CHROMA_SITING_0_5_EXT  0x3285


// OSD dma_buf experimental support
#define OSD_GET_DMA_BUF_FD		_IOWR('m', 313, int)
#define FBIOPAN_DISPLAY         0x4606
#define FBIO_WAITFORVSYNC       _IOW('F', 0x20, __u32)
#define DIRECT_RENDERING		0

const int MAX_SCREEN_BUFFERS = 2;
const int SWAP_INTERVAL = 0;



// Global variable(s)
bool isRunning;
int dmabuf_fd = -1;
timeval startTime;
timeval endTime;


void ResetTime()
{
	gettimeofday(&startTime, NULL);
	endTime = startTime;
}

float GetTime()
{
	gettimeofday(&endTime, NULL);
	float seconds = (endTime.tv_sec - startTime.tv_sec);
	float milliseconds = (float(endTime.tv_usec - startTime.tv_usec)) / 1000000.0f;

	startTime = endTime;

	return seconds + milliseconds;
}



// Signal handler for Ctrl-C
void SignalHandler(int s)
{
	isRunning = false;
}


#define GL_CheckError(x) ({_GL_CheckError(__FILE__, __LINE__);})
//#define GL_CheckError(x) ({})

void _GL_CheckError(const char* file, int line)
{
	int error = glGetError();

	if (error != GL_NO_ERROR)
	{
		printf("glGetError(): %i (0x%.4x)\n", (int)error, (int)error);
		printf("Failed at %s:%i\n", file, line);
		exit(1);
	}
}


#define TRICKMODE_NONE  0x00
#define TRICKMODE_I     0x01
#define TRICKMODE_FFFB  0x02

#define PTS_FREQ       90000
#define AV_SYNC_THRESH PTS_FREQ * 1

#define MIN_FRAME_QUEUE_SIZE  16
#define MAX_WRITE_QUEUE_SIZE  1

void* VideoDecoderThread(void* argument) 
{
	// Initialize the codec
	codec_para_t codecContext = { 0 };

#if 0

	//const char* fileName = "test.h264";
	//codecContext.stream_type = STREAM_TYPE_ES_VIDEO;
	//codecContext.video_type = VFORMAT_H264;
	//codecContext.has_video = 1;
	//codecContext.noblock = 0;
	//codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_H264;
	////codecContext.am_sysinfo.rate = (96000.0 / (24000.0 / 1001.0));	// 24 fps
	//codecContext.am_sysinfo.param = (void*)(SYNC_OUTSIDE);


	// 4K
	const char* fileName = "test.h264";
	codecContext.stream_type = STREAM_TYPE_ES_VIDEO;
	codecContext.video_type = VFORMAT_H264_4K2K;
	codecContext.has_video = 1;
	codecContext.noblock = 0;
	codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_H264_4K2K;
	//codecContext.am_sysinfo.rate = (96000.0 / (24000.0 / 1001.0));	// 24 fps
	codecContext.am_sysinfo.param = (void*)(SYNC_OUTSIDE);

#else

	const char* fileName = "test.hevc";
	codecContext.stream_type = STREAM_TYPE_ES_VIDEO;
	codecContext.video_type = VFORMAT_HEVC;
	codecContext.has_video = 1;
	codecContext.noblock = 0;
	codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_HEVC;
	//codecContext.am_sysinfo.rate = (96000.0 / (24000.0 / 1001.0));	
	codecContext.am_sysinfo.param = (void*)(SYNC_OUTSIDE);
	
#endif


	int api = codec_init(&codecContext);
	if (api != 0)
	{
		printf("codec_init failed (%x).\n", api);
		exit(1);
	}

	//codec_set_cntl_avthresh(&codecContext, AV_SYNC_THRESH);
	//codec_set_cntl_mode(&codecContext, TRICKMODE_NONE);
	//codec_set_cntl_syncthresh(&codecContext, 0);

	// Open the media file
	int fd = open(fileName, O_RDONLY);
	if (fd < 0)
	{
		printf("test file could not be opened.");
		exit(1);
	}


	unsigned char buffer[BUFFER_SIZE];

	while (isRunning)
	{
		// Read the ES video data from the file
		int bytesRead;
		while (true)
		{
			bytesRead = read(fd, &buffer, BUFFER_SIZE);
			if (bytesRead > 0)
			{
				break;
			}

			// Loop the video when the end is reached
			lseek(fd, 0, SEEK_SET);
		}

		// Send the data to the codec
		int api = 0;
		int offset = 0;
		do
		{
			api = codec_write(&codecContext, &buffer + offset, bytesRead - offset);
			if (api == -EAGAIN)
			{				
				usleep(100);
			}
			else if (api == -1)
			{
				// TODO: Sometimes this error is returned.  Ignoring it
				// does not seem to have any impact on video display
			}
			else if (api < 0)
			{
				printf("codec_write error: %x\n", api);
				//codec_reset(&codecContext);
			}
			else if (api > 0)
			{
				offset += api;
			}

		} while (api == -EAGAIN || offset < bytesRead);
	}


	// Close the codec and media file
	codec_close(&codecContext);
	close(fd);


	return NULL;
}

// Helper function to enable/disable a framebuffer
int osd_blank(const char *path, int cmd)
{
	int fd;
	char  bcmd[16];
	fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);

	if (fd >= 0) {
		sprintf(bcmd, "%d", cmd);
		if (write(fd, bcmd, strlen(bcmd)) < 0) {
			printf("osd_blank error during write.\n");
		}
		close(fd);
		return 0;
	}

	return -1;
}


void WriteToFile(const char* path, const char* value)
{
	int fd = open(path, O_RDWR | O_TRUNC, 0644);
	if (fd < 0)
	{
		printf("WriteToFile open failed: %s = %s\n", path, value);
		exit(1);
	}

	if (write(fd, value, strlen(value)) < 0)
	{
		printf("WriteToFile write failed: %s = %s\n", path, value);
		exit(1);
	}

	close(fd);
}


// Enable framebuffers (overrides console blanking)
void init_display()
{
	osd_blank("/sys/class/graphics/fb0/blank", 0);
	osd_blank("/sys/class/graphics/fb1/blank", 0);
}

void SetVfmState()
{
	/*
	echo "rm default" > /sys/class/vfm/map
	echo "add default decoder ionvideo" > /sys/class/vfm/map
	echo 1 > /sys/module/amvdec_h265/parameters/double_write_mode
	 */

	// Connect Ionvideo
	WriteToFile("/sys/class/vfm/map", "rm default");
	WriteToFile("/sys/class/vfm/map", "add default decoder ionvideo");

	// Use NV21 instead of compressed format for hevc
	WriteToFile("/sys/module/amvdec_h265/parameters/double_write_mode", "1");
}

void ResetVfmState()
{
	// TODO
}

const float quad[] =
{
	-1,  1, 0,
	-1, -1, 0,
	1, -1, 0,

	1, -1, 0,
	1,  1, 0,
	-1,  1, 0
};

const float quadUV[] =
{
	0, 0,
	0, 1,
	1, 1,

	1, 1,
	1, 0,
	0, 0
};

const float cube[] =
{
	/* FRONT */
	-1.0f, -1.0f,  1.0f,
	1.0f, -1.0f,  1.0f,
	-1.0f,  1.0f,  1.0f,

	-1.0f,  1.0f,  1.0f,
	1.0f, -1.0f,  1.0f,
	1.0f,  1.0f,  1.0f,

	/* BACK */
	-1.0f, -1.0f, -1.0f,
	-1.0f,  1.0f, -1.0f,
	1.0f, -1.0f, -1.0f,

	1.0f, -1.0f, -1.0f,
	-1.0f,  1.0f, -1.0f,
	1.0f,  1.0f, -1.0f,

	/* LEFT */
	-1.0f, -1.0f,  1.0f,
	-1.0f,  1.0f,  1.0f,
	-1.0f, -1.0f, -1.0f,

	-1.0f, -1.0f, -1.0f,
	-1.0f,  1.0f,  1.0f,
	-1.0f,  1.0f, -1.0f,

	/* RIGHT */
	1.0f, -1.0f, -1.0f,
	1.0f,  1.0f, -1.0f,
	1.0f, -1.0f,  1.0f,

	1.0f, -1.0f,  1.0f,
	1.0f,  1.0f, -1.0f,
	1.0f,  1.0f,  1.0f,

	/* TOP */
	-1.0f,  1.0f,  1.0f,
	1.0f,  1.0f,  1.0f,
	-1.0f,  1.0f, -1.0f,

	-1.0f,  1.0f, -1.0f,
	1.0f,  1.0f,  1.0f,
	1.0f,  1.0f, -1.0f,

	/* BOTTOM */
	-1.0f, -1.0f,  1.0f,
	-1.0f, -1.0f, -1.0f,
	1.0f, -1.0f,  1.0f,

	1.0f, -1.0f,  1.0f,
	-1.0f, -1.0f, -1.0f,
	1.0f, -1.0f, -1.0f
};

/** Texture coordinates for the quad. */
const float cubeUV[] =
{
	0.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  0.0f,

	1.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  1.0f,


	0.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  0.0f,

	1.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  1.0f,


	0.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  0.0f,

	1.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  1.0f,


	0.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  0.0f,

	1.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  1.0f,


	0.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  0.0f,

	1.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  1.0f,


	0.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  0.0f,

	1.0f,  0.0f,
	0.0f,  1.0f,
	1.0f,  1.0f

};

#if DIRECT_RENDERING
const char* vertexSource = "\n \
attribute mediump vec4 Attr_Position;\n \
attribute mediump vec2 Attr_TexCoord0;\n \
\n \
uniform mat4 WorldViewProjection;\n \
\n \
varying mediump vec2 TexCoord0;\n \
\n \
void main()\n \
{\n \
\n \
  gl_Position = Attr_Position * WorldViewProjection;\n \
  TexCoord0 = Attr_TexCoord0;\n \
  TexCoord0.y = 1.0 -TexCoord0.y;\n \
}\n \
\n \
 ";
#else
const char* vertexSource = "\n \
attribute mediump vec4 Attr_Position;\n \
attribute mediump vec2 Attr_TexCoord0;\n \
\n \
uniform mat4 WorldViewProjection;\n \
\n \
varying mediump vec2 TexCoord0;\n \
\n \
void main()\n \
{\n \
\n \
  gl_Position = Attr_Position * WorldViewProjection;\n \
  TexCoord0 = Attr_TexCoord0;\n \
}\n \
\n \
 ";
#endif

const char* fragmentSource = "\n \
uniform lowp sampler2D DiffuseMap;\n \
\n \
varying mediump vec2 TexCoord0;\n \
\n \
void main()\n \
{\n \
  lowp vec4 rgba = texture2D(DiffuseMap, TexCoord0);\n \
\n \
  gl_FragColor = rgba;\n \
}\n \
\n \
";

const char* fragmentSourceNV12 = "#extension GL_OES_EGL_image_external : require\n \
uniform lowp samplerExternalOES DiffuseMap;\n \
\n \
varying mediump vec2 TexCoord0;\n \
\n \
void main()\n \
{\n \
  lowp vec4 rgba = texture2D(DiffuseMap, TexCoord0);\n \
\n \
  gl_FragColor = rgba;\n \
}\n \
\n \
";


struct FrameBufferDmaInfo
{
	int Width;
	int Height;
	int BitsPerPixel;
	int LengthInBytes;
	int DmaBufferHandleFileDescriptor;
	fb_var_screeninfo var_info;
	int fd;
};


FrameBufferDmaInfo GetFrameBufferDmabufFd()
{
	FrameBufferDmaInfo info = { 0 };
	
	info.fd = open("/dev/fb0", O_RDWR);
	printf("file handle: %x\n", info.fd);


	//fb_var_screeninfo var_info;
	int ret = ioctl(info.fd, FBIOGET_VSCREENINFO, &info.var_info);
	if (ret < 0)
	{
		printf("FBIOGET_VSCREENINFO failed.\n");
		exit(1);
	}

	info.Width = info.var_info.xres;
	info.Height = info.var_info.yres;
	info.BitsPerPixel = info.var_info.bits_per_pixel;
	info.LengthInBytes = info.Width * info.Height * (info.BitsPerPixel / 8);

	printf("screen info: width=%d, height=%d, bpp=%d\n", info.Width, info.Height, info.BitsPerPixel);


	int result = -1;
	ret = ioctl(info.fd, OSD_GET_DMA_BUF_FD, &result);
	if (ret < 0)
	{
		printf("OSD_GET_DMA_BUF_FD failed. (%d)\n", ret);
		exit(1);
	}
	else
	{
		printf("OSD_GET_DMA_BUF_FD = %d\n", result);
	}

	info.DmaBufferHandleFileDescriptor = result;

	//close(fd_fb0);
	return info;
}



const int BUFFER_COUNT = 4;

#if 0
// HD
const int VIDEO_WIDTH = 1920;
const int VIDEO_HEIGHT = 1080;
#else
// 4K
const int VIDEO_WIDTH = 3840;
const int VIDEO_HEIGHT = 2160;
#endif

const int VIDEO_FRAME_SIZE = VIDEO_WIDTH * VIDEO_HEIGHT;

#if 1
const unsigned int VIDEO_FORMAT = V4L2_PIX_FMT_NV12;
#else
const unsigned int VIDEO_FORMAT = V4L2_PIX_FMT_RGB32;
#endif


struct IonInfo
{
	int IonFD;
	int IonVideoFD;
	int VideoBufferDmaBufferFD[BUFFER_COUNT];
};

IonInfo OpenIonVideoCapture()
{
	IonInfo info = { 0 };

	info.IonVideoFD = open("/dev/video13", O_RDWR ); //| O_NONBLOCK
	if (info.IonVideoFD < 0)
	{
		printf("open ionvideo failed.");
		exit(1);
	}

	printf("ionvideo file handle: %x\n", info.IonVideoFD);


	info.IonFD = open("/dev/ion", O_RDWR);
	if (info.IonFD < 0)
	{
		printf("open ion failed.");
		exit(1);
	}

	printf("ion file handle: %x\n", info.IonFD);


	// Set the capture format
	v4l2_format format = { 0 };
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix_mp.width = VIDEO_WIDTH;
	format.fmt.pix_mp.height = VIDEO_HEIGHT;
	format.fmt.pix_mp.pixelformat = VIDEO_FORMAT;
	
	int v4lcall = ioctl(info.IonVideoFD, VIDIOC_S_FMT, &format);
	if (v4lcall < 0)
	{
		printf("ionvideo VIDIOC_S_FMT failed: 0x%x", v4lcall);
		exit(1);
	}


	// Request buffers
	v4l2_requestbuffers requestBuffers = { 0 };
	requestBuffers.count = BUFFER_COUNT;
	requestBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	requestBuffers.memory = V4L2_MEMORY_DMABUF;
	
	v4lcall = ioctl(info.IonVideoFD, VIDIOC_REQBUFS, &requestBuffers);
	if (v4lcall < 0)
	{
		printf("ionvideo VIDIOC_REQBUFS failed: 0x%x", v4lcall);
		exit(1);
	}


	// Allocate buffers
	int videoFrameLength = 0;
	switch (format.fmt.pix_mp.pixelformat)
	{
	case V4L2_PIX_FMT_RGB32:
		videoFrameLength = VIDEO_FRAME_SIZE * 4;
		break;

	case V4L2_PIX_FMT_NV12:
		videoFrameLength = VIDEO_FRAME_SIZE * 2;
		break;

	default:
		printf("Unsupported video formated.\n");
		exit(1);
		break;
	}

	for (int i = 0; i < BUFFER_COUNT; ++i)
	{
		// Allocate a buffer
		ion_allocation_data allocation_data = { 0 };
		allocation_data.len = videoFrameLength;
		allocation_data.heap_id_mask = ION_HEAP_CARVEOUT_MASK;
		allocation_data.flags = ION_FLAG_CACHED;

		int ionCall = ioctl(info.IonFD, ION_IOC_ALLOC, &allocation_data);
		if (ionCall != 0)
		{
			printf("failed to allocate ion buffer: %d\n", i);
			exit(1);
		}


		// Export the dma_buf
		ion_fd_data fd_data = { 0 };
		fd_data.handle = allocation_data.handle;
		
		ionCall = ioctl(info.IonFD, ION_IOC_SHARE, &fd_data);
		if (ionCall < 0)
		{
			printf("failed to retrieve ion dma_buf handle: %d\n", i);
			exit(1);
		}

		info.VideoBufferDmaBufferFD[i] = fd_data.fd;


		// Queue the buffer for V4L to use
		v4l2_buffer buffer = { 0 };

		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_DMABUF;
		buffer.index = i;
		buffer.m.fd = info.VideoBufferDmaBufferFD[i];
		buffer.length = 1;	// Ionvideo only supports single plane

		v4lcall = ioctl(info.IonVideoFD, VIDIOC_QBUF, &buffer);
		if (v4lcall < 0)
		{
			printf("failed to queue ion buffer #%d: 0x%x\n", i, v4lcall);
			exit(1);
		}

		// DEBUG
		printf("Queued v4l2_buffer:\n");
		printf("\tindex=%x\n", buffer.index);
		printf("\ttype=%x\n", buffer.type);
		printf("\tm.fd=%x\n", buffer.m.fd);
	}


	// Start "streaming"
	int bufferType = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	v4lcall = ioctl(info.IonVideoFD, VIDIOC_STREAMON, &bufferType);
	if (v4lcall < 0)
	{
		printf("ionvideo VIDIOC_STREAMON failed: 0x%x", v4lcall);
		exit(1);
	}


	return info;
}


//------------------------------


int main_ionvideo()
{
	// Intialize
	isRunning = true;

	// Trap signal to clean up
	signal(SIGINT, SignalHandler);

	// Unblank display
	init_display();
	
	// Ionvideo will not generate frames until connected
	SetVfmState();

	// Create EGL/GL rendering contexts
	EGLDisplay display = Egl_Initialize();

	EGLConfig config;
	EGLSurface surface = Egl_CreateWindow(display, &config);

	EGLContext context = Egl_CreateContext(display, surface, config);


	FrameBufferDmaInfo info = GetFrameBufferDmabufFd();	


#if DIRECT_RENDERING
	// fb0 direct rendering (render target)
	EGLImageKHR frameBufferImage[MAX_SCREEN_BUFFERS] = { 0 };
	
	GLuint frameBufferTexture[MAX_SCREEN_BUFFERS] = { 0 };
	glGenTextures(MAX_SCREEN_BUFFERS, frameBufferTexture);
	GL_CheckError();
	
	for (int i = 0; i < MAX_SCREEN_BUFFERS; ++i)
	{

		EGLint img_attrs[] = {
			EGL_WIDTH, info.Width,
			EGL_HEIGHT, info.Height,
			EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_BGRA8888,	//DRM_FORMAT_RGBA8888
			EGL_DMA_BUF_PLANE0_FD_EXT,	info.DmaBufferHandleFileDescriptor,
			EGL_DMA_BUF_PLANE0_OFFSET_EXT, info.LengthInBytes * i,
			EGL_DMA_BUF_PLANE0_PITCH_EXT, info.Width * 4,
			EGL_NONE
		};

		frameBufferImage[i] = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, 0, img_attrs);
		Egl_CheckError();

		printf("frameBufferImage = %p\n", frameBufferImage[i]);

		glBindTexture(GL_TEXTURE_2D, frameBufferTexture[i]);
		GL_CheckError();

		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		GL_CheckError();

		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		GL_CheckError();

		glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, frameBufferImage[i]);
		GL_CheckError();
	}


	//// Z-Buffer
	//GLuint frameBufferZBuffer;
	//glGenRenderbuffers(1, &frameBufferZBuffer);
	//GL_CheckError();

	//glBindRenderbuffer(GL_RENDERBUFFER, frameBufferZBuffer);
	//GL_CheckError();

	//glRenderbufferStorage(GL_RENDERBUFFER,
	//	GL_DEPTH_COMPONENT24_OES,
	//	info.Width,
	//	info.Height);
	//GL_CheckError();


	// GL Framebuffer
	GLuint frameBuffer[MAX_SCREEN_BUFFERS] = { 0 };
	glGenFramebuffers(MAX_SCREEN_BUFFERS, frameBuffer);
	GL_CheckError();

	for (int i = 0; i < MAX_SCREEN_BUFFERS; ++i)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer[i]);
		GL_CheckError();

		glFramebufferTexture2D(GL_FRAMEBUFFER,	//target
			GL_COLOR_ATTACHMENT0,				//attachment
			GL_TEXTURE_2D,						// textarget
			frameBufferTexture[i],				// texture
			0);									// level
		GL_CheckError();

		//glFramebufferRenderbuffer(GL_FRAMEBUFFER,
		//	GL_DEPTH_ATTACHMENT,
		//	GL_RENDERBUFFER,
		//	frameBufferZBuffer);
		//GL_CheckError();

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			printf("Framebuffer is NOT complete.\n");
		}
		else
		{
			printf("Framebuffer COMPLETE.\n");
		}
	}
#endif


	// Shader
	GLuint vertexShader = 0;
	GLuint fragmentShader = 0;

	for (int i = 0; i < 2; ++i)
	{
		GLuint shaderType;
		const char* sourceCode;

		if (i == 0)
		{
			shaderType = GL_VERTEX_SHADER;
			sourceCode = vertexSource;
		}
		else
		{
			shaderType = GL_FRAGMENT_SHADER;
			sourceCode = fragmentSourceNV12;
		}

		GLuint openGLShaderID = glCreateShader(shaderType);
		GL_CheckError();

		const char* glSrcCode[1] = { sourceCode };
		const int lengths[1] = { -1 }; // Tell OpenGL the string is NULL terminated

		glShaderSource(openGLShaderID, 1, glSrcCode, lengths);
		GL_CheckError();

		glCompileShader(openGLShaderID);
		GL_CheckError();


		GLint param;

		glGetShaderiv(openGLShaderID, GL_COMPILE_STATUS, &param);
		GL_CheckError();

		if (param == GL_FALSE)
		{
			throw Exception("Shader Compilation Failed.");
		}

		if (i == 0)
		{
			vertexShader = openGLShaderID;
		}
		else
		{
			fragmentShader = openGLShaderID;
		}
	}


	// Program
	GLuint openGLProgramID = glCreateProgram();
	GL_CheckError();

	glAttachShader(openGLProgramID, vertexShader);
	GL_CheckError();

	glAttachShader(openGLProgramID, fragmentShader);
	GL_CheckError();


	// Bind
	glEnableVertexAttribArray(0);
	GL_CheckError();

	glBindAttribLocation(openGLProgramID, 0, "Attr_Position");
	GL_CheckError();

	glEnableVertexAttribArray(1);
	GL_CheckError();

	glBindAttribLocation(openGLProgramID, 1, "Attr_TexCoord0");
	GL_CheckError();

	glLinkProgram(openGLProgramID);
	GL_CheckError();

	glUseProgram(openGLProgramID);
	GL_CheckError();


	// Get program uniform(s)
	GLuint wvpUniformLocation = glGetUniformLocation(openGLProgramID, "WorldViewProjection");
	GL_CheckError();

	if (wvpUniformLocation < 0)
		throw Exception();


	GLuint diffuseMap = glGetUniformLocation(openGLProgramID, "DiffuseMap");
	GL_CheckError();

	if (diffuseMap < 0)
		throw Exception();


	// Setup OpenGL
	glClearColor(1, 0, 0, 1);	// RED for diagnostic use
	//glClearColor(0, 0, 0, 0);	// Transparent Black
	GL_CheckError();

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	GL_CheckError();

	glEnable(GL_CULL_FACE);
	GL_CheckError();

	glCullFace(GL_BACK);
	GL_CheckError();

	glFrontFace(GL_CCW);
	GL_CheckError();
	
	glViewport(0, 0, info.Width, info.Height);
	GL_CheckError();

	glActiveTexture(GL_TEXTURE0);
	GL_CheckError();


	// ----- IONVIDEO -----
	IonInfo ionInfo = OpenIonVideoCapture();

	// Create EGLImages and Texture for all capture buffers
	EGLImageKHR eglImage[BUFFER_COUNT] = { 0 };

	GLuint texture[BUFFER_COUNT] = { 0 };
	glGenTextures(BUFFER_COUNT, texture);
	GL_CheckError();

	for (int i = 0; i < BUFFER_COUNT; ++i)
	{
		switch (VIDEO_FORMAT)
		{
		case V4L2_PIX_FMT_RGB32:
		{
			EGLint img_attrs[] = {
				EGL_WIDTH, VIDEO_WIDTH,
				EGL_HEIGHT, VIDEO_HEIGHT,
				EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_RGBA8888,
				EGL_DMA_BUF_PLANE0_FD_EXT,	ionInfo.VideoBufferDmaBufferFD[i],
				EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
				EGL_DMA_BUF_PLANE0_PITCH_EXT, VIDEO_WIDTH * 4,
				EGL_NONE
			};

			eglImage[i] = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, 0, img_attrs);
			Egl_CheckError();
		}
		break;

		case V4L2_PIX_FMT_NV12:
		{
			EGLint img_attrs[] = {
				EGL_WIDTH, VIDEO_WIDTH,
				EGL_HEIGHT, VIDEO_HEIGHT,
				EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_NV12,
				EGL_DMA_BUF_PLANE0_FD_EXT,	ionInfo.VideoBufferDmaBufferFD[i],
				EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
				EGL_DMA_BUF_PLANE0_PITCH_EXT, VIDEO_WIDTH,
				EGL_DMA_BUF_PLANE1_FD_EXT,	ionInfo.VideoBufferDmaBufferFD[i],
				EGL_DMA_BUF_PLANE1_OFFSET_EXT, VIDEO_FRAME_SIZE,
				EGL_DMA_BUF_PLANE1_PITCH_EXT, VIDEO_WIDTH * 2,
				EGL_YUV_COLOR_SPACE_HINT_EXT, EGL_ITU_REC601_EXT,	// EGL_ITU_REC601_EXT EGL_ITU_REC709_EXT EGL_ITU_REC2020_EXT
				EGL_SAMPLE_RANGE_HINT_EXT, EGL_YUV_NARROW_RANGE_EXT,	// EGL_YUV_FULL_RANGE_EXT creates a "washed out" picture
				EGL_NONE
			}; 

			eglImage[i] = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, 0, img_attrs);
			Egl_CheckError();
		}
			break;

		default:
			printf("unsupported video format.\n");
			exit(1);
			break;
		}

		glActiveTexture(GL_TEXTURE0 + i);
		GL_CheckError();

		glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture[i]);
		GL_CheckError();

		glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		GL_CheckError();

		glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		GL_CheckError();

		glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, eglImage[i]);
		GL_CheckError();
	}


	// ----- start decoder -----
	pthread_t thread;
	int result_code = pthread_create(&thread, NULL, VideoDecoderThread, NULL);
	if (result_code != 0)
	{
		printf("pthread_create failed.\n");
		exit(1);
	}


	// ----- RENDERING -----
	int frames = 0;
	float totalTime = 0;

	ResetTime();

	int currentBuffer = 0;
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	GL_CheckError();

	while (isRunning)
	{
#if DIRECT_RENDERING
		// Set the buffer to render to
		glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer[currentBuffer]);
		GL_CheckError();
#endif


		// Debug testing for direct render
		//switch (currentBuffer)
		//{
		//case 0:
		//	glClearColor(1, 0, 0, 1);
		//	break;
		//case 1:
		//	glClearColor(0, 0, 1, 1);
		//	break;
		//case 2:
		//	glClearColor(0, 1, 0, 1);
		//	break;

		//default:
		//	printf("currentBuffer error.");
		//	exit(1);
		//	break;
		//} 
		
		//float red = (rand() % 256) / 255.0f;
		//float green = (rand() % 256) / 255.0f;
		//float blue = (rand() % 256) / 255.0f;
		//glClearColor(red, green, blue, 1);


		// Get a frame
		v4l2_buffer buffer = { 0 };
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_DMABUF;

		int v4lCall = ioctl(ionInfo.IonVideoFD, VIDIOC_DQBUF, &buffer);
		if (v4lCall < 0)
		{
			printf("render: failed to dequeue buffer: 0x%x\n", v4lCall);
			exit(1);
		}

		//// DEBUG
		//printf("Got v4l2_buffer: (v4lCall=0x%x)\n", v4lCall);
		//printf("\tindex=%x\n", buffer.index);
		//printf("\ttype=%x\n", buffer.type);
		//printf("\tm.fd=%x\n", buffer.m.fd);


		//glClear(GL_COLOR_BUFFER_BIT);	//| GL_DEPTH_BUFFER_BIT
		//GL_CheckError();
		

		// Quad
		{
			// Set the quad vertex data
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * 4, quad);
			GL_CheckError();

			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * 4, quadUV);
			GL_CheckError();


			// Set the matrix
#if 0
			float scale = 0.25f;
			Matrix4 scaleMatrix(scale, 0, 0, 0,
				0, scale, 0, 0,
				0, 0, 1, 0,
				0, 0, 0, 1);
#else
			Matrix4 scaleMatrix = Matrix4::Identity;
#endif
			Matrix4 transpose = Matrix4::CreateTranspose(scaleMatrix);
			float* wvpValues = &transpose.M11;

			glUniformMatrix4fv(wvpUniformLocation, 1, GL_FALSE, wvpValues);
			GL_CheckError();


			// Set the texture
#if 1
			glUniform1i(diffuseMap, buffer.index);
			GL_CheckError();
#else		
			glActiveTexture(GL_TEXTURE0);
			GL_CheckError();

			glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture[buffer.index]);
			GL_CheckError();
#endif

			// Draw
			glDrawArrays(GL_TRIANGLES, 0, 3 * 2);
			GL_CheckError();
		}



#if DIRECT_RENDERING
		// swap buffers
		glFinish();


		info.var_info.yoffset = info.Height * currentBuffer;
		ioctl(info.fd, FBIOPAN_DISPLAY, &info.var_info);

		for (int i = 0; i < SWAP_INTERVAL; ++i)
		{
			ioctl(info.fd, FBIO_WAITFORVSYNC, 0);
		}

		++currentBuffer;
		if (currentBuffer >= MAX_SCREEN_BUFFERS)
		{
			currentBuffer = 0;
		}

		GLenum attachments[] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT };
		glDiscardFramebufferEXT(GL_FRAMEBUFFER, 2, attachments);
		GL_CheckError();
#else
			eglSwapBuffers(display, surface);
#endif

		// Return the buffer to V4L
		// Important: Ensure the GPU is done with the buffer before returning it.
		//			  This can be done with glFinish(); or eglSwapBuffers	
		v4lCall = ioctl(ionInfo.IonVideoFD, VIDIOC_QBUF, &buffer);
		if (v4lCall < 0)
		{
			printf("render: failed to queue ion buffer #%d: 0x%x\n", buffer.index, v4lCall);
			exit(1);
		}


		// FPS
		float deltaTime = GetTime();

		totalTime += deltaTime;
		++frames;

		if (totalTime >= 1.0f)
		{
			int fps = (int)(frames / totalTime);
			printf("FPS: %i\n", fps);

			frames = 0;
			totalTime = 0;
		}
	}


	ResetVfmState();

	return 0;
}


int main()
{
	//return main_amvideocap();
	return main_ionvideo();
}
