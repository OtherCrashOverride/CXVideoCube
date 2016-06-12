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

// The headers are not aware C++ exists
extern "C"
{
#include <amcodec/codec.h>
}

#include "egl.h"
#include <GLES2/gl2.h>

#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/eglext.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2ext.h>


#include "Matrix4.h"
#include "Exception.h"


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
const int BUFFER_SIZE = 4096;	// 4K (page)

// Copied from https://github.com/hardkernel/linux/blob/odroidc-3.10.y/include/linux/amlogic/amports/amvideocap.h
#define AMVIDEOCAP_IOC_MAGIC 'V'
#define AMVIDEOCAP_IOW_SET_WANTFRAME_FORMAT     		_IOW(AMVIDEOCAP_IOC_MAGIC, 0x01, int)
#define AMVIDEOCAP_IOW_SET_WANTFRAME_WIDTH      		_IOW(AMVIDEOCAP_IOC_MAGIC, 0x02, int)
#define AMVIDEOCAP_IOW_SET_WANTFRAME_HEIGHT     		_IOW(AMVIDEOCAP_IOC_MAGIC, 0x03, int)
#define AMVIDEOCAP_IOW_SET_WANTFRAME_TIMESTAMP_MS     	_IOW(AMVIDEOCAP_IOC_MAGIC, 0x04, u64)
#define AMVIDEOCAP_IOW_SET_WANTFRAME_WAIT_MAX_MS     	_IOW(AMVIDEOCAP_IOC_MAGIC, 0x05, u64)
#define AMVIDEOCAP_IOW_SET_WANTFRAME_AT_FLAGS     		_IOW(AMVIDEOCAP_IOC_MAGIC, 0x06, int)


#define AMVIDEOCAP_IOR_GET_FRAME_FORMAT     		_IOR(AMVIDEOCAP_IOC_MAGIC, 0x10, int)
#define AMVIDEOCAP_IOR_GET_FRAME_WIDTH      		_IOR(AMVIDEOCAP_IOC_MAGIC, 0x11, int)
#define AMVIDEOCAP_IOR_GET_FRAME_HEIGHT     		_IOR(AMVIDEOCAP_IOC_MAGIC, 0x12, int)
#define AMVIDEOCAP_IOR_GET_FRAME_TIMESTAMP_MS     	_IOR(AMVIDEOCAP_IOC_MAGIC, 0x13, int)


#define AMVIDEOCAP_IOR_GET_SRCFRAME_FORMAT      			_IOR(AMVIDEOCAP_IOC_MAGIC, 0x20, int)
#define AMVIDEOCAP_IOR_GET_SRCFRAME_WIDTH       			_IOR(AMVIDEOCAP_IOC_MAGIC, 0x21, int)
#define AMVIDEOCAP_IOR_GET_SRCFRAME_HEIGHT      			_IOR(AMVIDEOCAP_IOC_MAGIC, 0x22, int)


#define AMVIDEOCAP_IOR_GET_STATE     	   			_IOR(AMVIDEOCAP_IOC_MAGIC, 0x31, int)
#define AMVIDEOCAP_IOW_SET_START_CAPTURE   			_IOW(AMVIDEOCAP_IOC_MAGIC, 0x32, int)
#define AMVIDEOCAP_IOW_SET_CANCEL_CAPTURE  			_IOW(AMVIDEOCAP_IOC_MAGIC, 0x33, int)

#define AMVIDEOCAP_IOR_SET_SRC_X                _IOR(AMVIDEOCAP_IOC_MAGIC, 0x40, int)
#define AMVIDEOCAP_IOR_SET_SRC_Y                _IOR(AMVIDEOCAP_IOC_MAGIC, 0x41, int)
#define AMVIDEOCAP_IOR_SET_SRC_WIDTH            _IOR(AMVIDEOCAP_IOC_MAGIC, 0x42, int)
#define AMVIDEOCAP_IOR_SET_SRC_HEIGHT           _IOR(AMVIDEOCAP_IOC_MAGIC, 0x43, int)


// ge2d.h
#define GE2D_ENDIAN_SHIFT       	24
#define GE2D_ENDIAN_MASK            (0x1 << GE2D_ENDIAN_SHIFT)
#define GE2D_BIG_ENDIAN             (0 << GE2D_ENDIAN_SHIFT)
#define GE2D_LITTLE_ENDIAN          (1 << GE2D_ENDIAN_SHIFT)

#define GE2D_FMT_S8_Y            	0x00000 /* 00_00_0_00_0_00 */
#define GE2D_FMT_S8_CB           	0x00040 /* 00_01_0_00_0_00 */
#define GE2D_FMT_S8_CR           	0x00080 /* 00_10_0_00_0_00 */
#define GE2D_FMT_S8_R            	0x00000 /* 00_00_0_00_0_00 */
#define GE2D_FMT_S8_G            	0x00040 /* 00_01_0_00_0_00 */
#define GE2D_FMT_S8_B            	0x00080 /* 00_10_0_00_0_00 */
#define GE2D_FMT_S8_A            	0x000c0 /* 00_11_0_00_0_00 */
#define GE2D_FMT_S8_LUT          	0x00020 /* 00_00_1_00_0_00 */
#define GE2D_FMT_S16_YUV422      	0x20102 /* 01_00_0_00_0_00 */
#define GE2D_FMT_S16_RGB         	(GE2D_LITTLE_ENDIAN|0x00100) /* 01_00_0_00_0_00 */
#define GE2D_FMT_S24_YUV444      	0x20200 /* 10_00_0_00_0_00 */
#define GE2D_FMT_S24_RGB         	(GE2D_LITTLE_ENDIAN|0x00200) /* 10_00_0_00_0_00 */
#define GE2D_FMT_S32_YUVA444     	0x20300 /* 11_00_0_00_0_00 */
#define GE2D_FMT_S32_RGBA        	(GE2D_LITTLE_ENDIAN|0x00300) /* 11_00_0_00_0_00 */
#define GE2D_FMT_M24_YUV420      	0x20007 /* 00_00_0_00_1_11 */
#define GE2D_FMT_M24_YUV422      	0x20006 /* 00_00_0_00_1_10 */
#define GE2D_FMT_M24_YUV444      	0x20004 /* 00_00_0_00_1_00 */
#define GE2D_FMT_M24_RGB         	0x00004 /* 00_00_0_00_1_00 */
#define GE2D_FMT_M24_YUV420T     	0x20017 /* 00_00_0_10_1_11 */
#define GE2D_FMT_M24_YUV420B     	0x2001f /* 00_00_0_11_1_11 */

#define GE2D_FMT_M24_YUV420SP		0x20207
#define GE2D_FMT_M24_YUV420SPT		0x20217 /* 01_00_0_00_1_11 nv12 &nv21, only works on m6. */
#define GE2D_FMT_M24_YUV420SPB		0x2021f /* 01_00_0_00_1_11 nv12 &nv21, only works on m6. */

#define GE2D_FMT_S16_YUV422T     	0x20110 /* 01_00_0_10_0_00 */
#define GE2D_FMT_S16_YUV422B     	0x20138 /* 01_00_0_11_0_00 */
#define GE2D_FMT_S24_YUV444T     	0x20210 /* 10_00_0_10_0_00 */
#define GE2D_FMT_S24_YUV444B     	0x20218 /* 10_00_0_11_0_00 */

/*32 bit*/
#define GE2D_COLOR_MAP_SHIFT 20

#define GE2D_COLOR_MAP_RGBA8888		(0 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_YUVA8888		(0 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_ARGB8888     (1 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_AYUV8888     (1 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_ABGR8888     (2 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_AVUY8888     (2 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_BGRA8888     (3 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_VUYA8888     (3 << GE2D_COLOR_MAP_SHIFT)

#define GE2D_FORMAT_S32_ARGB        (GE2D_FMT_S32_RGBA    | GE2D_COLOR_MAP_ARGB8888) 
#define GE2D_FORMAT_S32_ABGR        (GE2D_FMT_S32_RGBA    | GE2D_COLOR_MAP_ABGR8888) 
#define GE2D_FORMAT_S32_BGRA        (GE2D_FMT_S32_RGBA    | GE2D_COLOR_MAP_BGRA8888) 

//
#define GE2D_FORMAT_S32_RGBA (GE2D_FMT_S32_RGBA | GE2D_COLOR_MAP_RGBA8888) 


// Global variable(s)
bool isRunning;




// Signal handler for Ctrl-C
void SignalHandler(int s)
{
	isRunning = false;
}


void GL_CheckError()
{
	int error = glGetError();

	if (error != GL_NO_ERROR)
	{
		printf("eglGetError(): %i (0x%.4x)\n", (int)error, (int)error);
		//printf("Failed at %s:%i\n", __FILE__, __LINE__);
		exit(1);
	}
}

void* VideoDecoderThread(void* argument) 
{
	// Initialize the codec
	codec_para_t codecContext = { 0 };

	codecContext.stream_type = STREAM_TYPE_ES_VIDEO;
	codecContext.video_type = VFORMAT_H264;
	codecContext.has_video = 1;
	codecContext.noblock = 0;
	codecContext.am_sysinfo.format = VIDEO_DEC_FORMAT_H264;
	codecContext.am_sysinfo.rate = (96000.0 / (24000.0 / 1001.0));	// 24 fps
	codecContext.am_sysinfo.param = (void*)(SYNC_OUTSIDE);

	int api = codec_init(&codecContext);
	if (api != 0)
	{
		printf("codec_init failed (%x).\n", api);
		exit(1);
	}


	// Open the media file
	int fd = open("test.h264", O_RDONLY);
	if (fd < 0)
	{
		printf("test.h264 could not be opened.");
		exit(1);
	}


	unsigned char buffer[BUFFER_SIZE];

	while (isRunning)
	{
		// Read the ES video data from the file
		int bytesRead = read(fd, &buffer, BUFFER_SIZE);
		if (bytesRead < 1)
		{
			// Loop the video when the end is reached
			lseek(fd, 0, SEEK_SET);
			if (read(fd, &buffer + bytesRead,
				BUFFER_SIZE - bytesRead) < 1)
			{
				printf("Problem reading file.");
				exit(1);
			}
		}


		// Send the data to the codec
		int api = codec_write(&codecContext, &buffer, BUFFER_SIZE);
		if (api != BUFFER_SIZE)
		{
			printf("codec_write error: %x\n", api);
			codec_reset(&codecContext);
		}
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


// Enable framebuffers (overrides console blanking)
void init_display()
{
	osd_blank("/sys/class/graphics/fb0/blank", 0);
	osd_blank("/sys/class/graphics/fb1/blank", 0);
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

const char* fragmentSource = "\n \
uniform lowp sampler2D DiffuseMap;\n \
\n \
varying mediump vec2 TexCoord0;\n \
\n \
void main()\n \
{\n \
  mediump vec4 rgba = texture2D(DiffuseMap, TexCoord0);\n \
\n \
  gl_FragColor = rgba;\n \
}\n \
\n \
";


int OpenCapture()
{
	int amlfd = open("/dev/amvideocap0", O_RDWR);
	if (amlfd < 0)
	{
		printf("failed to open /dev/amvideocap0\n");
		exit(1);
	}

	if (ioctl(amlfd, AMVIDEOCAP_IOR_SET_SRC_WIDTH, 1920) == -1 ||
		ioctl(amlfd, AMVIDEOCAP_IOR_SET_SRC_HEIGHT, 1080) == -1)
	{
		printf("Failed to configure frame size\n");
		exit(1);
	}

	if (ioctl(amlfd, AMVIDEOCAP_IOW_SET_WANTFRAME_WIDTH, 1920) == -1 ||
		ioctl(amlfd, AMVIDEOCAP_IOW_SET_WANTFRAME_HEIGHT, 1080) == -1)
	{
		printf("Failed to configure frame size\n");
		exit(1);
	}

	if (ioctl(amlfd, AMVIDEOCAP_IOW_SET_WANTFRAME_FORMAT, GE2D_FORMAT_S32_ABGR) == -1)	//GE2D_FORMAT_S32_ABGR, GE2D_FORMAT_S32_RGBA
	{
		printf("Failed to configure frame size\n");
		exit(1);
	}

	return amlfd;
}


int main()
{
	isRunning = true;


	// Trap signal to clean up
	signal(SIGINT, SignalHandler);


	init_display();


	EGLDisplay display = Egl_Initialize();

	EGLConfig config;
	EGLSurface surface = Egl_CreateWindow(display, &config);

	EGLContext context = Egl_CreateContext(display, surface, config);


	// start decoder
	pthread_t thread;
	int result_code = pthread_create(&thread, NULL, VideoDecoderThread, NULL);
	if (result_code != 0)
	{
		printf("pthread_create failed.\n");
		exit(1);
	}


	// Setup capture
	int amlfd = OpenCapture();
	
	void* captureData = mmap(NULL, 1920 * 1080 * 4, PROT_READ, MAP_FILE | MAP_SHARED, amlfd, 0);
	if (captureData == MAP_FAILED)
	{
		printf("mmap failed\n");
		exit(1);
	}

	printf("captureData=%p\n", captureData);


	// Texture
	GLuint texture2D;
	glGenTextures(1, &texture2D);
	GL_CheckError();

	glActiveTexture(GL_TEXTURE0);
	GL_CheckError();

	glBindTexture(GL_TEXTURE_2D, texture2D);
	GL_CheckError();

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	GL_CheckError();

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	GL_CheckError();

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1920, 1080, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const GLvoid *)NULL);
	GL_CheckError();


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
			sourceCode = fragmentSource;
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


	// Setup OpenGL
	//glClearColor(1, 0, 0, 1);	// RED for diagnostic use
	glClearColor(0, 0, 0, 0);	// Transparent Black
	GL_CheckError();

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	GL_CheckError();

	glEnable(GL_CULL_FACE);
	GL_CheckError();

	glCullFace(GL_BACK);
	GL_CheckError();

	glFrontFace(GL_CCW);
	GL_CheckError();


	float rotX = 0;
	float rotY = 0;
	float rotZ = 0;

	// Render loop
	while (isRunning)
	{
		// Capture
		if (ioctl(amlfd, AMVIDEOCAP_IOW_SET_START_CAPTURE, 1000) == 0)
		{
			glClear(GL_COLOR_BUFFER_BIT |
				GL_DEPTH_BUFFER_BIT |
				GL_STENCIL_BUFFER_BIT);


			// Upload texture data
			glActiveTexture(GL_TEXTURE0);
			GL_CheckError();

			glBindTexture(GL_TEXTURE_2D, texture2D);
			GL_CheckError();

			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1920, 1080, GL_RGBA, GL_UNSIGNED_BYTE, captureData);
			GL_CheckError();


			// Quad
			//{
			//	// Set the quad vertex data
			//	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * 4, quad);
			//	GL_CheckError();

			//	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * 4, quadUV);
			//	GL_CheckError();


			//	// Set the matrix
			//	Matrix4 transpose = Matrix4::CreateTranspose(Matrix4::Identity);
			//	float* wvpValues = &transpose.M11;

			//	glUniformMatrix4fv(wvpUniformLocation, 1, GL_FALSE, wvpValues);
			//	GL_CheckError();


			//	// Draw
			//	glDrawArrays(GL_TRIANGLES, 0, 3 * 2);
			//	GL_CheckError();
			//}


			// Cube
			{
				// Set the vertex data
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * 4, cube);
				GL_CheckError();

				glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * 4, cubeUV);
				GL_CheckError();


				const float TwoPI = (float)(M_PI * 2.0);


				rotY += 0.05f;
				while (rotY > TwoPI)
				{
					rotY -= TwoPI;
				}

				rotX += 0.05f;
				while (rotX > TwoPI)
				{
					rotX -= TwoPI;
				}

				Matrix4 world = Matrix4::CreateRotationX(rotX) * Matrix4::CreateRotationY(rotY) * Matrix4::CreateRotationZ(rotZ);
				Matrix4 view = Matrix4::CreateLookAt(Vector3(0, 0, 3.5f), Vector3::Forward, Vector3::Up);

				Matrix4 wvp = world * view * Matrix4::CreatePerspectiveFieldOfView(M_PI_4, 16.0f / 9.0f, 0.1f, 50);

				Matrix4 transpose = Matrix4::CreateTranspose((wvp));
				float* wvpValues = &transpose.M11;


				glUniformMatrix4fv(wvpUniformLocation, 1, GL_FALSE, wvpValues);
				GL_CheckError();


				glDrawArrays(GL_TRIANGLES, 0, 3 * 2 * 6);
				GL_CheckError();
			}


			//glBindTexture(GL_TEXTURE_2D, 0);
			//GL_CheckError();

			// Swap
			eglSwapBuffers(display, surface);
			Egl_CheckError();
		}
	}


	return 0;
}
