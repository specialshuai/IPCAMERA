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
#include <sys/sem.h>
#include <pthread.h>
//#include <uapi/linux/videodev2.h>
#include <linux/fb.h>
#include <linux/types.h>
#include <semaphore.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "SsbSipH264Encode.h"
#include "LogMsg.h"
#include "performance.h"
#include "post.h"
#include "lcd.h"
#include "MfcDriver.h"
#include "cam_encoder_test.h"
#include <sys/poll.h>
#include "videodev2_samsung.h"
#include "videodev2.h"
/******************* CAMERA ********************/
#define SAMSUNG_UXGA_S5K3BA

#ifdef RGB24BPP
	#define CAMERA_24BIT		/* For s3c2443/6400 24bit CAMERA interface */
	#define CAMERA_BPP_V4L2		V4L2_PIX_FMT_RGB24
#else
	#define CAMERA_BPP_V4L2		V4L2_PIX_FMT_RGB565	/* 16 BPP - RGB565 */
#endif

#define PREVIEW_NODE  "/dev/video1"
#define CAMERA_DEV_NAME   "/dev/video1"
static int cam_p_fp = -1;
/* Camera functions */
static int cam_p_init(void);
static int WaitPic(void);

/************* FRAME BUFFER ***************/
#define CAMERA_WIDTH 	1920
#define CAMERA_HEIGHT	1088
int fb_size,LineLen;
#define YUV_FRAME_BUFFER_SIZE	((CAMERA_WIDTH*CAMERA_HEIGHT)+(CAMERA_WIDTH*CAMERA_HEIGHT)/2)		/* YCBCR 420 */
int bpp;
/* Frame buffer functions */
void StartStream(int fd);
void StopStream(int fd);

/***************** MFC *******************/
//#define 	FILEWRIT 
static void 	*handle; 
static void *read_yuv(int fd);
/* MFC functions */
static void *mfc_encoder_init(int width, int height, int frame_rate, int bitrate, int gop_num);
static void *mfc_encoder_exe(void *handle, unsigned char *yuv_buf, int frame_size, int first_frame, long *size);
static void mfc_encoder_free(void *handle);
 unsigned char *einbuf;
int mfc_index;

/***************** etc *******************/
#define TRUE  1
#define FALSE 0
#define SHARED_BUF_NUM						100
#define MFC_LINE_BUF_SIZE_PER_INSTANCE		(204800)
#define YUV_FRAME_NUM						300
static int		film_cnt;
static int		frame_count;
static int 		finished;
//static int              encode_start=0;
//static int     		read_ok=0;
//static unsigned char yuvbuf[15667200];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t wrmutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t		sem_idw,sem_idr;

static pthread_t 	pth;
static void encoding_thread(void);
static FILE *encoded_fp;
struct pollfd  m_events_c;
static const int CAPTURE_BUFFER_NUMBER = 4;
struct { void * ldata; void * cdata;int llen;int clen; } captureBuffer[4];
typedef struct list_node *LIST;
struct list_node{
       int data;
       LIST next;
};

//struct {}
static void exit_from_app() 
{
	int fb_size;

	/* Stop previewing */
	switch(32)
	{
		case 16:
			fb_size = CAMERA_WIDTH * CAMERA_HEIGHT * 2;
			break;
		case 24:
			fb_size = CAMERA_WIDTH * CAMERA_HEIGHT * 4;
			break;
		case 32:
			fb_size = CAMERA_WIDTH * CAMERA_HEIGHT * 4;
			break;
		default:
			fb_size = CAMERA_WIDTH * CAMERA_HEIGHT * 4;
			printf("CAMERA supports 16 or 24 bpp\n");
			break;
	}
        
	close(cam_p_fp);
	mfc_encoder_free(handle);
}


static void sig_del(int signo)
{
	finished=1;
	printf("sigdel\n");
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
	int frame_count;
	long			encoded_size;
	_MFCLIB_H264_ENC *pCTX  = (_MFCLIB_H264_ENC *) handle;
	unsigned char	*encoded_buf;
	unsigned int bitrate=0,bitnum=0;
	unsigned int inbufsize=ALIGN_TO_8KB(CAMERA_WIDTH*CAMERA_HEIGHT)+ALIGN_TO_8KB(CAMERA_WIDTH*CAMERA_HEIGHT/2);
	struct v4l2_buffer camb;
        struct v4l2_plane camplanes[2];
/****************************************socket************************************************/
	int mysock;  
    	struct sockaddr_in addr; 
    	struct sockaddr_in caddr;     
    	int addr_len,caddr_len;  
   	if (( mysock= socket(AF_INET, SOCK_DGRAM, 0) )<0)  
    	{  
        	perror("error");  
        	exit(1);  
    	}  
   	addr_len=sizeof(struct sockaddr_in);  
   	bzero(&addr,sizeof(addr));  
   	addr.sin_family=AF_INET;  
   	addr.sin_port=htons(1234);  
   	addr.sin_addr.s_addr=htonl(INADDR_ANY); 

   	caddr_len=sizeof(struct sockaddr_in);  
   	bzero(&caddr,sizeof(caddr));  
   	caddr.sin_family=AF_INET;  
   	caddr.sin_port=htons(1234);  
   	caddr.sin_addr.s_addr=inet_addr("10.5.104.107"); 
  	if(bind(mysock,(struct sockaddr*)&addr,sizeof(addr))<0)  
   	{  
       		perror("connect");  
       		exit(1);  
   	} 
/****************************************************************************************/
	printf("==== Camera Preview & Encode to H264 Test ====\n");
	/* Camera encode initialization */
	if ((cam_p_fp = cam_p_init()) < 0)
		exit_from_app();
	signal_ctrl_c();

	mfc_index=0;
        int encoded_fp = fopen("a.h264", "wb");
/****************************************************************************************************************/
	/* Encoding and decoding threads creation */
	handle = mfc_encoder_init(CAMERA_WIDTH, CAMERA_HEIGHT, 25,3000000, 25);
	frame_count=1;
	while (1) {
		if (finished==1)
			break; 	
		if (WaitPic()||(frame_count==1)) 
		{
                	if(frame_count == 1)
			{
				encoded_buf = mfc_encoder_exe(handle, NULL, YUV_FRAME_BUFFER_SIZE, 1, &encoded_size);
				einbuf=SsbSipH264EncodeGetInBuf(handle,inbufsize);
				frame_count=0;
        			StartStream(cam_p_fp);
			}
			else
			{
                                memset(&camb, 0, sizeof camb);
				memset(camplanes, 0, (sizeof (struct v4l2_plane))*2);
				camb.m.planes=camplanes;
        			camb.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        			camb.memory = V4L2_MEMORY_MMAP;
        			camb.index = 0;
        			camb.length = 2;//planes num;
				if (ioctl(cam_p_fp, VIDIOC_DQBUF, &camb) < 0) {
					printf( "cannot fetch picture(VIDIOC_DQBUF failed)\n");
				}
				mfc_index=camb.index;
				encoded_buf = mfc_encoder_exe(handle, NULL, YUV_FRAME_BUFFER_SIZE, 0, &encoded_size);
				//printf("e:%d\n",encoded_size);
				if (ioctl (cam_p_fp, VIDIOC_QBUF, &camb) < 0) {
					printf( "cannot fetch picture(VIDIOC_QBUF failed)\n");
				}
	
			}
                       sendto(mysock,encoded_buf,encoded_size,0,(struct sockaddr*)&caddr,caddr_len);
                       // read_yuv(cam_p_fp);
			bitnum++;
			bitrate+=encoded_size;
			if(bitnum>24)
			{
 				//printf("BITRAT: %ld kB/s\n",bitrate/1000);
				bitnum=0;
				bitrate=0;
			}
			//fwrite(encoded_buf, 1, encoded_size, encoded_fp);		

                }

	}
	fclose(encoded_fp);
       StopStream(cam_p_fp);
	finished = 1;
	exit_from_app();

	return 0;
}
#if 0
/***************** Encoding Thread *****************/
void encoding_thread(void)
{
	char file_name[100];
	int yuv_cnt = 0,i;
	int frame_num = YUV_FRAME_NUM;
	unsigned char	*encoded_buf;
	long			encoded_size;
	unsigned int inbufsize=ALIGN_TO_8KB(CAMERA_WIDTH*CAMERA_HEIGHT)+ALIGN_TO_8KB(CAMERA_WIDTH*CAMERA_HEIGHT/2);
	unsigned int bitrate=0,bitnum=0;
        /*******************************************************************/
#ifndef FILEWRIT
	int mysock;  
    	struct sockaddr_in addr; 
    	struct sockaddr_in caddr;     
    	int addr_len,caddr_len;  
   	if (( mysock= socket(AF_INET, SOCK_DGRAM, 0) )<0)  
    	{  
        	perror("error");  
        	exit(1);  
    	}  
   	addr_len=sizeof(struct sockaddr_in);  
   	bzero(&addr,sizeof(addr));  
   	addr.sin_family=AF_INET;  
   	addr.sin_port=htons(1234);  
   	addr.sin_addr.s_addr=htonl(INADDR_ANY); 

   	caddr_len=sizeof(struct sockaddr_in);  
   	bzero(&caddr,sizeof(caddr));  
   	caddr.sin_family=AF_INET;  
   	caddr.sin_port=htons(1234);  
   	caddr.sin_addr.s_addr=inet_addr("10.5.104.125"); 
  	if(bind(mysock,(struct sockaddr*)&addr,sizeof(addr))<0)  
   	{  
       		perror("connect");  
       		exit(1);  
   	} 
#endif
        /*******************************************************************/
	pthread_mutex_lock(&mutex);
			
	handle = mfc_encoder_init(CAMERA_WIDTH, CAMERA_HEIGHT, 20,3000000, 20);
			
	sprintf(&file_name[0], "Cam_encoding_%dx%d-%d.264", CAMERA_WIDTH, CAMERA_HEIGHT, ++film_cnt);
	printf("Name of encoded file : Cam_encoding_%dx%d-%d.264\n", CAMERA_WIDTH, CAMERA_HEIGHT, film_cnt);
	//fflush(stdout);

	/* file create/open, note to "wb" */
#ifdef FILEWRIT
	encoded_fp = fopen(&file_name[0], "wb");
	if (!encoded_fp) {
		perror(&file_name[0]);
	}
#endif
	/* Codec start */
	frame_count=1;
	while(!finished)
	{
		/* read from camera device */
		sem_wait(&sem_idr);
		if(frame_count == 1)
		{
			encoded_buf = mfc_encoder_exe(handle, NULL, YUV_FRAME_BUFFER_SIZE, 1, &encoded_size);
			einbuf=SsbSipH264EncodeGetInBuf(handle,inbufsize);
			frame_count=0;
		}
		else
		{
			//pthread_mutex_lock(&wrmutex);
			encoded_buf = mfc_encoder_exe(handle, NULL, YUV_FRAME_BUFFER_SIZE, 0, &encoded_size);
			//pthread_mutex_unlock(&wrmutex);	
		}

		bitnum++;
		bitrate+=encoded_size;
               // printf("encoded_size: %d\n",encoded_size);
		if(bitnum>21)
		{
 			printf("BITRAT: %ld kB/s\n",bitrate/1000);
			bitnum=0;
			bitrate=0;
		}	
		

#ifdef FILEWRIT	
		//printf("frame_count:%d,encoded_size:%ld\n",frame_count,encoded_size);	
		fwrite(encoded_buf, 1, encoded_size, encoded_fp);
		
#else
		sendto(mysock,encoded_buf,encoded_size,0,(struct sockaddr*)&caddr,caddr_len);
#endif  
                sem_post(&sem_idw);
          };					
	 frame_count = 0;	
	/* Codec stop */
	fclose(encoded_fp);
        finished = 2;	
	sem_post(&sem_idw);
	pthread_mutex_unlock(&mutex);
	
}
#endif


/***************** Camera driver function *****************/
static int cam_p_init(void) 
{
	int fd= -1;
	const char *device = CAMERA_DEV_NAME;
	int i = 0;
	fd = open(device, O_RDWR|O_NONBLOCK);
	if (fd < 0) {
		printf( "cannot open device %s\n", device);
		return -1;
	}
    
	// Check capability
	struct v4l2_capability cap;
	if( ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		printf( "cannot query capability\n");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
		printf( "not a video capture device\n");
		return -1;
	}
    
	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		printf( "does not support streaming i/o\n");
		return -1;
	}
    
	static struct v4l2_input input;
    	input.index = 0; 
    	if (ioctl(fd, VIDIOC_ENUMINPUT, &input) != 0) {
        	printf( "No matching index found\n");
        	return -1;
    	}
    	if (!input.name) {
        	printf( "No matching index found\n");
        	return -1;
    	}
    	if (ioctl(fd, VIDIOC_S_INPUT, &input) < 0) {
        	printf( "VIDIOC_S_INPUT failed\n");
        	return -1;
    	}
    	struct v4l2_format fmt;
    	memset(&fmt, 0, sizeof fmt);
    	fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    	fmt.fmt.pix_mp.width       = CAMERA_WIDTH;
    	fmt.fmt.pix_mp.height      = CAMERA_HEIGHT;
    	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12MT;
	fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    	fmt.fmt.pix_mp.num_planes=2;
    	fmt.fmt.pix_mp.plane_fmt[0].sizeimage = (fmt.fmt.pix_mp.width * fmt.fmt.pix_mp.height * 16) /16;
    	fmt.fmt.pix_mp.plane_fmt[0].bytesperline=fmt.fmt.pix_mp.width;
    	fmt.fmt.pix_mp.plane_fmt[1].sizeimage = (fmt.fmt.pix_mp.width * fmt.fmt.pix_mp.height * 16) / 32;
    	fmt.fmt.pix_mp.plane_fmt[1].bytesperline=fmt.fmt.pix_mp.width;
    	if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        	printf( "VIDIOC_S_FMT failed\n");
        	return -1;
    	}
    
	// map the capture buffer...
    	struct v4l2_requestbuffers req;
    	memset(&req, 0, sizeof req);
    	req.count  = CAPTURE_BUFFER_NUMBER;
    	req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    	req.memory = V4L2_MEMORY_MMAP;
    	if(ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
    		printf( "request capture buffer failed\n");
    		return -1;
    	}
    
    	if ((int)(req.count) != CAPTURE_BUFFER_NUMBER) {
    		printf( "capture buffer number is wrong\n");
        	return -1;
    	}
    	struct v4l2_buffer b;
	struct v4l2_plane planes[2];
    	for (i=0; i < CAPTURE_BUFFER_NUMBER; i++) {
    		memset(&b, 0, sizeof b);
		memset(planes, 0, (sizeof (struct v4l2_plane))*2);
		b.m.planes=planes;
    		b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    		b.memory = V4L2_MEMORY_MMAP;
    		b.index = i;
		b.length = 2;//planes num;
    		if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) {
    			printf( "query capture buffer failed\n");
    			return -1;
    		}
        
    		captureBuffer[i].ldata = mmap(0, planes[0].length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, planes[0].m.mem_offset);
    		captureBuffer[i].llen = planes[0].length;
        	captureBuffer[i].cdata = mmap(0, planes[1].length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, planes[1].m.mem_offset);
    		captureBuffer[i].clen = planes[1].length;
    		if ((captureBuffer[i].ldata == MAP_FAILED)&&(captureBuffer[i].cdata == MAP_FAILED)) {
    			printf( "unable to map capture buffer\n");
    			return -1;
    		}
        
        	printf( "ImageSize[%d] = %ld,%ld\n", i, planes[0].length,planes[1].length);
    	}

       printf( "CAPTURE Open Capture Device OK!\n");
    	memset(&m_events_c, 0, sizeof(m_events_c));
    	m_events_c.fd = fd;
    	m_events_c.events = POLLIN | POLLERR;
       //buffstate=0;
    return fd;
}

static void * read_yuv(int fd)
{
	struct v4l2_buffer camb;
        memset(&camb, 0, sizeof camb);
        struct v4l2_plane camplanes[2];
	memset(camplanes, 0, (sizeof (struct v4l2_plane))*2);

	camb.m.planes=camplanes;
        camb.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        camb.memory = V4L2_MEMORY_MMAP;
        camb.index = 0;
        camb.length = 2;//planes num;
	if (ioctl(fd, VIDIOC_DQBUF, &camb) < 0) {
		printf( "cannot fetch picture(VIDIOC_DQBUF failed)\n");
		return (void *)0;
	}
	unsigned char *ldata=(void *)captureBuffer[camb.index].ldata;
	unsigned char *cdata=(void *)captureBuffer[camb.index].cdata;

	//memcpy(einbuf,ldata , CAMERA_WIDTH*CAMERA_HEIGHT);
	//memcpy(einbuf+ALIGN_TO_8KB(CAMERA_WIDTH*CAMERA_HEIGHT), cdata, CAMERA_WIDTH*CAMERA_HEIGHT/2);
	if (ioctl (fd, VIDIOC_QBUF, &camb) < 0) {
		printf( "cannot fetch picture(VIDIOC_QBUF failed)\n");
		return (void *)0;
	}
	return (void *)0;
}

int WaitPic()
{
    int ret = poll(&m_events_c,  1, 1000);
    if (ret > 0) {
        return 1;
    }
    return 0;
}
void StartStream(int fd)
{
   int i = 0;
    	for (i = 0; i < CAPTURE_BUFFER_NUMBER; i++) {
        	struct v4l2_buffer b;
        	memset(&b, 0, sizeof b);
		struct v4l2_plane planes[2];
		memset(planes, 0, (sizeof (struct v4l2_plane))*2);
		b.m.planes=planes;
        	b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        	b.memory = V4L2_MEMORY_MMAP;
        	b.index = i;
        	b.length = 2;//planes num;
        	if (ioctl (fd, VIDIOC_QBUF, &b) < 0) {
            		printf( "queue capture failed\n");
            		return;
        	}
    	}
    	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    	if (ioctl (fd, VIDIOC_STREAMON, &type) < 0) {
        	printf( "cannot start stream\n");
        	return;
    	}

    	printf( "CAPTURE StartStream OK!\n");
}

void StopStream(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

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

