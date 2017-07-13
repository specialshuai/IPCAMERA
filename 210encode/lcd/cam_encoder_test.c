#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <linux/videodev2.h>
#include <linux/fb.h>
#include <linux/types.h>
#include <semaphore.h>
#include "SsbSipH264Encode.h"
#include "LogMsg.h"
#include "performance.h"
#include "post.h"
#include "lcd.h"
#include "MfcDriver.h"
#include "cam_encoder_test.h"
#include <sys/poll.h>
#include "videodev2_samsung.h"
/******************* CAMERA ********************/
#define SAMSUNG_UXGA_S5K3BA

#ifdef RGB24BPP
	#define LCD_24BIT		/* For s3c2443/6400 24bit LCD interface */
	#define LCD_BPP_V4L2		V4L2_PIX_FMT_RGB24
#else
	#define LCD_BPP_V4L2		V4L2_PIX_FMT_RGB565	/* 16 BPP - RGB565 */
#endif

#define PREVIEW_NODE  "/dev/video0"
#define CODEC_NODE  "/dev/video1"

static int cam_p_fp = -1;
/* Camera functions */
static int cam_p_init(void);
static int WaitPic(void);
static int read_data(int fd);


/************* FRAME BUFFER ***************/
#define LCD_BPP	32	/* 24 BPP - RGB888 */
#define LCD_WIDTH 	1280
#define LCD_HEIGHT	1024
int fb_size,LineLen;
#define YUV_FRAME_BUFFER_SIZE	((LCD_WIDTH*LCD_HEIGHT)+(LCD_WIDTH*LCD_HEIGHT)/2)		/* YCBCR 420 */
int bpp;
static unsigned char *win0_fb_addr = NULL;
static int pre_fb_fd = -1;

/* Frame buffer functions */
static int fb_init(int win_num);
static int draw(unsigned char *dest, unsigned char *src, int x, int y);
void StartStream(int fd);
void StopStream(int fd);

/***************** MFC *******************/
static void 	*handle; 
static void *read_yuv(int fd);
/* MFC functions */
static void *mfc_encoder_init(int width, int height, int frame_rate, int bitrate, int gop_num);
static void *mfc_encoder_exe(void *handle, unsigned char *yuv_buf, int frame_size, int first_frame, long *size);
static void mfc_encoder_free(void *handle);
 unsigned char *einbuf;


/***************** etc *******************/
#define TRUE  1
#define FALSE 0
#define SHARED_BUF_NUM						100
#define MFC_LINE_BUF_SIZE_PER_INSTANCE		(204800)
#define YUV_FRAME_NUM						300
static int		film_cnt;
static int		frame_count;
static int 		encoding_flag = FALSE;
static int 		finished;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned char rgb_for_preview[LCD_WIDTH * LCD_HEIGHT * 4];	// MAX
static pthread_t 	pth;
static void encoding_thread(void);
static FILE *encoded_fp;
struct pollfd  m_events_c;
static const int CAPTURE_BUFFER_NUMBER = 1;
struct { void * data; int len; } captureBuffer[1];
int lcdwidth;// real lcd width
int lcdheight;// real lcd height

static void exit_from_app() 
{
	int fb_size;


	//ioctl(pre_fb_fd, SET_OSD_STOP);
	/* Stop previewing */
	switch(LCD_BPP)
	{
		case 16:
			fb_size = LCD_WIDTH * LCD_HEIGHT * 2;
			break;
		case 24:
			fb_size = LCD_WIDTH * LCD_HEIGHT * 4;
			break;
		case 32:
			fb_size = LCD_WIDTH * LCD_HEIGHT * 4;
			break;
		default:
			fb_size = LCD_WIDTH * LCD_HEIGHT * 4;
			printf("LCD supports 16 or 24 bpp\n");
			break;
	}

	close(cam_p_fp);
	//close(cam_c_fp);

	//mfc_encoder_free(handle);
	munmap(win0_fb_addr, fb_size);
	close(pre_fb_fd);
}


static void sig_del(int signo)
{
	finished=1;
	//exit_from_app();
}


static void signal_ctrl_c(void)
{
	if (signal(SIGINT, sig_del) == SIG_ERR)
		printf("Signal Error\n");
}



/* Main Process(Camera previewing) */
int main(int argc, char **argv)
{
	int k_id;

	printf("==== Camera Preview & Encode to H264 Test ====\n");
	/* Window0 initialzation for previewing */
	//if ((pre_fb_fd = fb_init(0)) < 0)
		//exit_from_app();
	/* Camera preview initialization */
	if ((cam_p_fp = cam_p_init()) < 0)
		exit_from_app();
	signal_ctrl_c();


	
/****************************************************************************************************************/
	/* Encoding and decoding threads creation */
        StartStream(cam_p_fp);
	k_id = pthread_create(&pth, 0, (void *) &encoding_thread, 0);
	while (1) {
		if (finished)
			break;
		 sleep(10);
		//if (WaitPic()) {

                	//if (read_data(cam_p_fp)) {
                  	//draw(win0_fb_addr, &rgb_for_preview[0], (lcdwidth-LCD_WIDTH)/2, (lcdheight-LCD_HEIGHT)/2);
                	//}

            	//}

	}
       StopStream(pre_fb_fd);
	pthread_join(pth, NULL);

	finished = 1;
	exit_from_app();

	return 0;
}

/***************** Encoding Thread *****************/
void encoding_thread(void)
{
	char file_name[100];
	int yuv_cnt = 0;
	int i;
	int frame_num = YUV_FRAME_NUM;
	unsigned char	*g_yuv;
	unsigned char	*encoded_buf;
	long			encoded_size;
	unsigned int inbufsize=ALIGN_TO_8KB(LCD_WIDTH*LCD_HEIGHT)+ALIGN_TO_8KB(LCD_WIDTH*LCD_HEIGHT/2);
		encoding_flag = TRUE;
		if (encoding_flag == TRUE) {

			//pthread_mutex_lock(&mutex);
			
			handle = mfc_encoder_init(LCD_WIDTH, LCD_HEIGHT, 15,60000, 15);
			
			sprintf(&file_name[0], "Cam_encoding_%dx%d-%d.264", LCD_WIDTH, LCD_HEIGHT, ++film_cnt);
			printf("Name of encoded file : Cam_encoding_%dx%d-%d.264\n", LCD_WIDTH, LCD_HEIGHT, film_cnt);
			fflush(stdout);

			/* file create/open, note to "wb" */
			encoded_fp = fopen(&file_name[0], "wb");
			if (!encoded_fp) {
				perror(&file_name[0]);
			}

			/* Codec start */
			frame_count=0;
			for(yuv_cnt=0; yuv_cnt < frame_num; yuv_cnt++){
				frame_count++;

				/* read from camera device */
                   
				if(frame_count == 1)
					{
						encoded_buf = mfc_encoder_exe(handle, NULL, YUV_FRAME_BUFFER_SIZE, 1, &encoded_size);
						einbuf=SsbSipH264EncodeGetInBuf(handle,inbufsize);
						printf("...uaddr%d\n",einbuf);
						for(i=0;i<22;i++)
						printf("%x ",encoded_buf[i]);
						printf("\n ");
					}
				   else
				   	{
				   		read_yuv(cam_p_fp);
						encoded_buf = mfc_encoder_exe(handle, NULL, YUV_FRAME_BUFFER_SIZE, 0, &encoded_size);
				
				   	}

					printf("frame_count:%d,encoded_size:%ld\n",frame_count,encoded_size);
		
					
				fwrite(encoded_buf, 1, encoded_size, encoded_fp);
					
			}

			frame_count = 0;
			
			/* Codec stop */
			printf("100 frames were encoded\n");
			fclose(encoded_fp);
			mfc_encoder_free(handle);
			encoding_flag= FALSE;
                    finished = 1;		
			pthread_mutex_unlock(&mutex);
		}
	
}



/***************** Camera driver function *****************/
static int cam_p_init(void) 
{
	int fd= -1;
	int Valid=1;
	int i = 0;
	fd = open("/dev/video0", O_RDWR|O_NONBLOCK);
	if (fd < 0) {
               Valid = 0;
		printf("cannot open device /dev/video0\n");
		return -1;
	}
    
	// Check capability
	struct v4l2_capability cap;
	if(ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        	Valid = 0;
		printf("cannot query capability\n");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        	Valid = 0;
		printf("not a video capture device\n");
		return -1;
	}
    
	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        	Valid = 0;
		printf("does not support streaming i/o\n");
		return -1;
	}
    
    static struct v4l2_input input;
    input.index = 0; 
    if (ioctl(fd, VIDIOC_ENUMINPUT, &input) != 0) {
        	Valid = 0;
		printf( "No matching index found\n");
        return -1;
    }
    if (!input.name) {
        	Valid = 0;
		printf( "No matching index found\n");
        return -1;
    }
    if (ioctl(fd, VIDIOC_S_INPUT, &input) < 0) {
        	Valid = 0;
		printf("VIDIOC_S_INPUT failed\n");
        return -1;
    }
    
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = LCD_WIDTH;
    fmt.fmt.pix.height      = LCD_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12T;
    fmt.fmt.pix.sizeimage = (fmt.fmt.pix.width * fmt.fmt.pix.height * 12) / 8;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        	Valid = 0;
		printf( "app VIDIOC_S_FMT failed\n");
        return -1;
    }
    
	int CouldSetFrameRate = 0;
	struct v4l2_streamparm StreamParam;
	memset(&StreamParam, 0, sizeof StreamParam);
	StreamParam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_G_PARM, &StreamParam) < 0)  {
        	printf("could not set frame rate\n");
	} else {
		CouldSetFrameRate = StreamParam.parm.capture.capability & V4L2_CAP_TIMEPERFRAME;
	}

    // map the capture buffer...
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof req);
    req.count  = 1;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if(ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        	Valid = 0;
		printf( "request capture buffer failed\n");
    	return -1;
    }
    
    if ((int)(req.count) != 1) {
        Valid = 0;
	printf( "capture buffer number is wrong\n");
        return -1;
    }

    for (i = 0; i < CAPTURE_BUFFER_NUMBER; i++) {
    	struct v4l2_buffer b;
    	memset(&b, 0, sizeof b);
    	b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    	b.memory = V4L2_MEMORY_MMAP;
    	b.index = i;
    	if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) {
        	Valid = 0;
		printf("query capture buffer failed\n");
    		return -1;
    	}
        
    	captureBuffer[i].data = mmap(0, b.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, b.m.offset);
    	captureBuffer[i].len = b.length;
        
    	if (captureBuffer[i].data == MAP_FAILED) {
        	Valid = 0;
		printf( "unable to map capture buffer\n");
    		return -1;
    	}
        
        printf("ImageSize[%d] = %d\n", i, b.length);
    }

    struct v4l2_control ctrl;
    ctrl.id = V4L2_CID_CAMERA_CHECK_DATALINE;
    ctrl.value = 0;
    if(ioctl(fd, VIDIOC_S_CTRL,&ctrl)) {
        	Valid = 0;
		printf( "VIDIOC_S_CTRL V4L2_CID_CAMERA_CHECK_DATALINE failed\n");
        return -1;
    }
    if (Valid) {
       	printf( "Open Device OK!\n");
    }
    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = fd;
    m_events_c.events = POLLIN | POLLERR;
    return fd;
}

static void * read_yuv(int fd)
{
	struct v4l2_buffer b;
       while ((!WaitPic()&&(!finished)));
	memset(&b, 0, sizeof b);
	b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	b.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) {
		printf( "cannot fetch picture(VIDIOC_DQBUF failed)\n");
		return (void *)0;
	}
	unsigned char *data=(void *)captureBuffer[b.index].data;
	
	memcpy(einbuf,data , LCD_WIDTH*LCD_HEIGHT);
	memcpy(einbuf+ALIGN_TO_8KB(LCD_WIDTH*LCD_HEIGHT), (void *)captureBuffer[b.index].data+LCD_WIDTH*LCD_HEIGHT, LCD_WIDTH*LCD_HEIGHT/2);
	if (ioctl (fd, VIDIOC_QBUF, &b) < 0) {
		printf( "cannot fetch picture(VIDIOC_QBUF failed)\n");
		return (void *)0;
	}
	return (void *)0;
}

static void decodeYUV420SP(unsigned int* rgbBuf, unsigned char* yuv420sp, int width, int height) {  
    int frameSize = width * height;  
    int i = 0, y = 0,j=0,yp = 0;  
    int uvp = 0, u = 0, v = 0;  
    int y1192 = 0, r = 0, g = 0, b = 0;  
    unsigned int xrgb8888;
    int xrgb8888Index = 0;

    for (j = 0, yp = 0; j < height; j++) {  
        uvp = frameSize + (j >> 1) * width;  
        u = 0;  
        v = 0;  
        for (i = 0; i < width; i++, yp++) {  
            y = (0xff & ((int) yuv420sp[yp])) - 16;  
            if (y < 0) y = 0;  
            if ((i & 1) == 0) {  
                v = (0xff & yuv420sp[uvp++]) - 128;  
                u = (0xff & yuv420sp[uvp++]) - 128;  
            }  

            y1192 = 1192 * y;  
            r = (y1192 + 1634 * v);  
            g = (y1192 - 833 * v - 400 * u);  
            b = (y1192 + 2066 * u);  

            if (r < 0) r = 0; else if (r > 262143) r = 262143;  
            if (g < 0) g = 0; else if (g > 262143) g = 262143;  
            if (b < 0) b = 0; else if (b > 262143) b = 262143; 


            r = (unsigned char)(r >> 10);  
            g = (unsigned char)(g >> 10);  
            b = (unsigned char)(b >> 10); 

            xrgb8888 = (unsigned int)((r << 16) | (g << 8) | b);
            rgbBuf[xrgb8888Index++] = xrgb8888;
        }  
    }  
}
static int read_data(int fd)
{
	struct v4l2_buffer b;
	memset(&b, 0, sizeof b);
	b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	b.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) {
		printf( "cannot fetch picture(VIDIOC_DQBUF failed)\n");
		return 0;
	}
    	void *data_ = captureBuffer[b.index].data;
    	unsigned char* data = (unsigned char*) data_;
    	decodeYUV420SP((unsigned int*)rgb_for_preview, data, LCD_WIDTH, LCD_HEIGHT);
    
	if (ioctl (fd, VIDIOC_QBUF, &b) < 0) {
		printf( "cannot fetch picture(VIDIOC_QBUF failed)\n");
		return 0;
	}
	return 1;
}

/***************** Display driver function *****************/
static int fb_init(int win_num)
{
	int 			dev_fp = -1;
	//s3c_win_info_t	fb_info_to_driver;
	dev_fp = open("/dev/fb0", O_RDWR);
	if (dev_fp < 0) {
		perror(FB_DEV_NAME);
		return -1;
	}
        struct fb_fix_screeninfo Fix;
        struct fb_var_screeninfo Var;
		if (ioctl(dev_fp, FBIOGET_FSCREENINFO, &Fix) < 0||ioctl(dev_fp, FBIOGET_VSCREENINFO, &Var) < 0) {
			perror("FBIOGET_FSCREENINFO");
			return -1;
		}
        bpp = Var.bits_per_pixel;
        if(bpp!=32)
	{
	  	perror("only bpp32 surport.");
		return -1;
	}
       	lcdwidth  = Var.xres;
        lcdheight = Var.yres;
	LineLen= Fix.line_length;
      	fb_size = LineLen * lcdheight;
	int PageSize = getpagesize();
	fb_size = (fb_size + PageSize - 1) / PageSize * PageSize ;
	if ((win0_fb_addr = (unsigned char*) mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, dev_fp, 0)) < 0) {
		printf("mmap() error in fb_init()");
		return -1;
	}
	return dev_fp;
}
int WaitPic()
{
    int ret = poll(&m_events_c,  1, 10000);
    if (ret > 0) {
        return 1;
    }
    return 0;
}
static int draw(unsigned char *dest, unsigned char *src, int x, int y)
{
		int i;
		if (bpp != 32 ) {
			// don't support that yet
			printf("does not support other than 32 BPP yet");
			exit(1);
		}

		// clip
		int x0, y0, x1, y1;
		x0 = x;
		y0 = y;
		x1 = x0 + LCD_WIDTH - 1;
		y1 = y0 + LCD_HEIGHT - 1;
		if (x0 < 0) {
			x0 = 0;
		}
		if (x0 > lcdwidth - 1) {
			return 1;
		}
		if( x1 < 0) {
			return 1;
		}
		if (x1 > lcdwidth - 1) {
			x1 = lcdwidth - 1;
		}
		if (y0 < 0) {
			y0 = 0;
		}
		if (y0 > lcdheight - 1) {
			return 1;
		}
		if (y1 < 0) {
			return 1;
		}
		if (y1 > lcdheight - 1) {
			y1 = lcdheight -1;
		}
                //printf("2x0:%d,y0:%d,x1:%d,y1:%d,x:%d,y:%d\n",x0,y0,x1,y1,x,y);
		int copyLineLen = (x1 + 1 - x0) * bpp / 8;
		unsigned char *DstPtr = dest;
		const  unsigned char *SrcPtr = src + LCD_WIDTH *4 *(y0 - y) + (x0 - x) * bpp / 8;
		for (i = y0; i <= y1; i++) {
			memcpy(DstPtr, SrcPtr, copyLineLen);
			DstPtr += lcdwidth*bpp / 8;
			SrcPtr += LCD_WIDTH *bpp / 8;
		}
		
		
		return 1;
}
void StartStream(int fd)
{
    int Valid=1,i;
    for (i = 0; i < CAPTURE_BUFFER_NUMBER; i++) {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof b);
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = 0;
        if (ioctl (fd, VIDIOC_QBUF, &b) < 0) {
            Valid = 0;
            printf("queue capture failed\n");
            return;
        }
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl (fd, VIDIOC_STREAMON, &type) < 0) {
            Valid = 0;
            printf( "cannot start stream\n");
        return;
    }

    if (Valid) {
        printf( "StartStream OK!\n");
    }
}

void StopStream(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl (fd, VIDIOC_STREAMOFF, &type) < 0) {
        printf("cannot stop stream\n");
        return;
    }
}


/***************** MFC driver function *****************/
void *mfc_encoder_init(int width, int height, int frame_rate, int bitrate, int gop_num)
{
	int				frame_size;
	void			*handle;


	frame_size	= (width * height * 3) >> 1;

	handle = SsbSipH264EncodeInit(width, height, frame_rate, bitrate, gop_num);
	if (handle == NULL) {
		LOG_MSG(LOG_ERROR, "Test_Encoder", "SsbSipH264EncodeInit Failed\n");
		return NULL;
	}
	return handle;
}

void *mfc_encoder_exe(void *handle, unsigned char *yuv_buf, int frame_size, int first_frame, long *size)
{
	unsigned char	*p_inbuf;
	int				ret;
	_MFCLIB_H264_ENC *pCTX  = (_MFCLIB_H264_ENC *) handle;
	ret = SsbSipH264EncodeExe(handle);
	*size=pCTX ->enc_strm_size;
	if (first_frame) {

		*size=pCTX ->enc_hdr_size;
		printf("Header Size : %ld\n", *size);
		}
	return (void *)(pCTX->out_u_addr.strm_ref_y);
}

void mfc_encoder_free(void *handle)
{
	SsbSipH264EncodeDeInit(handle);
}

