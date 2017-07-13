#ifndef __SAMSUNG_SYSLSI_APDEV_S3C_MFC_H__
#define __SAMSUNG_SYSLSI_APDEV_S3C_MFC_H__
#include "mfc_interface.h"


#ifdef __cplusplus
extern "C" {
#endif



#ifdef __cplusplus
}
#endif

typedef struct
{
      enum ssbsip_mfc_codec_type codec_type; 
	int   	magic;
	int  	hOpen;
	int     fInit;
	int     enc_strm_size;
	int		enc_hdr_size;
	
	unsigned int  	width, height;
	unsigned int  	framerate, bitrate;
	unsigned int  	gop_num;
	unsigned char	*mapped_addr;
	unsigned int    index;
	unsigned int    yuv_paddr[4];
	unsigned int    yuv_uaddr;
	unsigned int out_y_addr,out_CyCr_addr;
	struct mfc_strm_ref_buf_arg out_p_addr;
	struct mfc_strm_ref_buf_arg out_u_addr;
	int frametag;
	
} _MFCLIB_H264_ENC;

#define IOCTL_MFC_MPEG4_DEC_INIT			(0x00800001)
#define IOCTL_MFC_MPEG4_ENC_INIT			(0x00800002)
#define IOCTL_MFC_MPEG4_DEC_EXE				(0x00800003)
#define IOCTL_MFC_MPEG4_ENC_EXE				(0x00800004)

#define IOCTL_MFC_H264_DEC_INIT				(0x00800005)
#define IOCTL_MFC_H264_ENC_INIT				(0x00800006)
#define IOCTL_MFC_H264_DEC_EXE				(0x00800007)
#define IOCTL_MFC_H264_ENC_EXE				(0x00800008)

#define IOCTL_MFC_H263_DEC_INIT				(0x00800009)
#define IOCTL_MFC_H263_ENC_INIT				(0x0080000A)
#define IOCTL_MFC_H263_DEC_EXE				(0x0080000B)
#define IOCTL_MFC_H263_ENC_EXE				(0x0080000C)

#define IOCTL_MFC_VC1_DEC_INIT				(0x0080000D)
#define IOCTL_MFC_VC1_DEC_EXE				(0x0080000E)

#define IOCTL_MFC_GET_LINE_BUF_ADDR			(0x0080000F)
#define IOCTL_MFC_GET_RING_BUF_ADDR			(0x00800010)
#define IOCTL_MFC_GET_FRAM_BUF_ADDR			(0x00800011)
#define IOCTL_MFC_GET_POST_BUF_ADDR			(0x00800012)
#define IOCTL_MFC_GET_PHY_FRAM_BUF_ADDR		(0x00800013)

#define IOCTL_MFC_SET_H263_MULTIPLE_SLICE	(0x00800014)

#define MFCDRV_RET_OK						(0)
#define MFCDRV_RET_ERR_INVALID_PARAM		(-1001)
#define MFCDRV_RET_ERR_HANDLE_INVALIDATED	(-1004)
#define MFCDRV_RET_ERR_OTHERS				(-9001)

// Physical Base Address for the MFC Data Buffer
// Data Buffer = {STRM_BUF, FRME_BUF}
#define S3C6400_BASEADDR_MFC_DATA_BUF	0x57316000
#define BUF_SIZE						0xf89400	// input and output buffer size
#define VIDEO_BUFFER_SIZE				(150*1024)	// 150KB

#define MFC_DEV_NAME	"/dev/s3c-mfc" // ghcstop fix

#endif /* __SAMSUNG_SYSLSI_APDEV_S3C_MFC_H__ */
