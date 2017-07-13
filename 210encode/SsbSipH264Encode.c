#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include "MfcDriver.h"
#include "MfcDrvParams.h"
#include "SsbSipH264Encode.h"
#include "LogMsg.h"


extern int mfc_index;
#define _MFCLIB_H264_ENC_MAGIC_NUMBER		0x92242002
typedef struct {
	int width;
	int height;
	int frameRateRes;
	int frameRateDiv;
	int gopNum;
	int bitrate;
} enc_info_t;


void *SsbSipH264EncodeInit(unsigned int uiWidth,     unsigned int uiHeight,
                           unsigned int uiFramerate, unsigned int uiBitrate_kbps,
                           unsigned int uiGOPNum)
{
	_MFCLIB_H264_ENC	*pCTX;
	int					hOpen;
	unsigned char		*addr;


	//////////////////////////////
	/////     CreateFile     /////
	//////////////////////////////
	hOpen = open(MFC_DEV_NAME, O_RDWR|O_NDELAY);
	if (hOpen < 0) {
		LOG_MSG(LOG_ERROR, "SsbSipH264EncodeInit", "MFC Open failure.\n");
		return NULL;
	}

	//////////////////////////////////////////
	//	Mapping the MFC Input/Output Buffer	//
	//////////////////////////////////////////
	// mapping shared in/out buffer between application and MFC device driver
	addr = (unsigned char *) mmap(0, 48*1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, hOpen, 0);
	if (addr == NULL) {
		LOG_MSG(LOG_ERROR, "SsbSipH264EncodeInit", "MFC Mmap failure.\n");
		return NULL;
	}

	pCTX = (_MFCLIB_H264_ENC *) malloc(sizeof(_MFCLIB_H264_ENC));
	if (pCTX == NULL) {
		LOG_MSG(LOG_ERROR, "SsbSipH264EncodeInit", "malloc failed.\n");
		close(hOpen);
		return NULL;
	}
	memset(pCTX, 0, sizeof(_MFCLIB_H264_ENC));
	pCTX->magic = _MFCLIB_H264_ENC_MAGIC_NUMBER;
	pCTX->hOpen = hOpen;
	pCTX->fInit = 0;
	pCTX->mapped_addr	= addr;
        printf("pCTX->mapped_addr%x\n",pCTX->mapped_addr);
	pCTX->width     = uiWidth;
	pCTX->height    = uiHeight;
	pCTX->framerate = uiFramerate;
	pCTX->bitrate   = uiBitrate_kbps;
	pCTX->gop_num   = uiGOPNum;
       pCTX->frametag=0;
	pCTX->enc_strm_size = 0;
	pCTX->codec_type=H264_ENC;

	return (void *) pCTX;
}


int SsbSipH264EncodeExe(void *openHandle)
{
	_MFCLIB_H264_ENC	*pCTX;
	int					r;
	struct mfc_common_args 			in_param;
         memset(&in_param,0,sizeof(struct mfc_common_args));
	////////////////////////////////
	//  Input Parameter Checking  //
	////////////////////////////////
	if (openHandle == NULL) {
		LOG_MSG(LOG_ERROR, "SsbSipH264EncodeExe", "openHandle is NULL\n");
		return SSBSIP_H264_ENC_RET_ERR_INVALID_HANDLE;
	}

	pCTX  = (_MFCLIB_H264_ENC *) openHandle;
	if (!pCTX->fInit) {

		in_param.args.enc_init_h264.in_codec_type=pCTX->codec_type;
		in_param.args.enc_init_h264.in_width		= pCTX->width;
		in_param.args.enc_init_h264.in_height		= pCTX->height;
		in_param.args.enc_init_h264.in_gop_num		= pCTX->gop_num;
		in_param.args.enc_init_h264.in_mapped_addr	= (unsigned int)(pCTX->mapped_addr);
		in_param.args.enc_init_h264.in_interlace_mode	=0;
		in_param.args.enc_init_h264.in_frame_qp		=30;
		in_param.args.enc_init_h264.in_transform8x8_mode=1;
		in_param.args.enc_init_h264.in_reference_num	=5;
		in_param.args.enc_init_h264.in_ref_num_p	=5;
		in_param.args.enc_init_h264.in_profile_level	=ENC_PROFILE_LEVEL(ENC_PROFILE_H264_HIGH,40);
		in_param.args.enc_init_h264.in_symbolmode	=1;
		//in_param.args.enc_init_h264.in_RC_framerate	= pCTX->framerate;
		in_param.args.enc_init_h264.in_mb_refresh	=20;
#if 1             
		in_param.args.enc_init_h264.in_RC_framerate	= pCTX->framerate;
		in_param.args.enc_init_h264.in_RC_bitrate	= pCTX->bitrate;
                in_param.args.enc_init_h264.in_RC_frm_enable=1;
		in_param.args.enc_init_h264.in_RC_mb_enable=1;
		in_param.args.enc_init_h264.in_RC_qbound	=ENC_RC_QBOUND(0,30);
#endif
		pCTX->frametag=0;    

		////////////////////////////////////////////////
		/////          (DeviceIoControl)           /////
		/////       IOCTL_MFC_H264_DEC_INIT        /////
		////////////////////////////////////////////////
		r = ioctl(pCTX->hOpen, IOCTL_MFC_ENC_INIT, &in_param); 
		if ((r < 00) || (in_param.ret_code < 0)) {
			LOG_MSG(LOG_ERROR, "SsbSipH264EncodeInit", "IOCTL_MFC_H264_ENC_INIT failed.\n");
			return SSBSIP_H264_ENC_RET_ERR_ENCODE_FAIL;
		}
              if(in_param.args.enc_init_h264.out_header_size > 0) {
			pCTX->enc_hdr_size = in_param.args.enc_init_h264.out_header_size;
			LOG_MSG(LOG_TRACE, "SsbSipH264EncodeExe", "HEADER SIZE = %d\n", pCTX->enc_hdr_size);
		}
		pCTX->out_p_addr.strm_ref_y=in_param.args.enc_init_h264.out_p_addr.strm_ref_y;
		pCTX->out_u_addr.strm_ref_y=in_param.args.enc_init_h264.out_u_addr.strm_ref_y;
		pCTX->fInit = 1;
             
		return SSBSIP_H264_ENC_RET_OK;
	}


	/////////////////////////////////////////////////
	/////           (DeviceIoControl)           /////
	/////       IOCTL_MFC_MPEG4_DEC_EXE         /////
	/////////////////////////////////////////////////
	in_param.args.enc_exe.in_codec_type=pCTX->codec_type;
	//in_param.args.enc_exe.in_frametag=pCTX->frametag++;
	in_param.args.enc_exe.in_strm_st=pCTX->out_p_addr.strm_ref_y;
	
	in_param.args.enc_exe.in_Y_addr=pCTX->yuv_paddr[mfc_index];
	in_param.args.enc_exe.in_CbCr_addr=pCTX->yuv_paddr[mfc_index]+ALIGN_TO_8KB(pCTX->width*pCTX->height);	
	r = ioctl(pCTX->hOpen, IOCTL_MFC_ENC_EXE, &in_param);
	if ((r < 0)|| (in_param.ret_code < 0)) {
		return SSBSIP_H264_ENC_RET_ERR_ENCODE_FAIL;
	}

	// Encoded stream size is saved. (This value is returned in SsbSipH264EncodeGetOutBuf)
	pCTX->enc_strm_size = in_param.args.enc_exe.out_encoded_size;
	//pCTX->out_y_addr=in_param.args.enc_exe.out_encoded_Y_paddr;
	//pCTX->out_CyCr_addr=in_param.args.enc_exe.out_encoded_C_paddr;

	return SSBSIP_H264_ENC_RET_OK;
}


int SsbSipH264EncodeDeInit(void *openHandle)
{
	_MFCLIB_H264_ENC  *pCTX;


	////////////////////////////////
	//  Input Parameter Checking  //
	////////////////////////////////
	if (openHandle == NULL) {
		LOG_MSG(LOG_ERROR, "SsbSipH264EncodeDeInit", "openHandle is NULL\n");
		return SSBSIP_H264_ENC_RET_ERR_INVALID_HANDLE;
	}

	pCTX  = (_MFCLIB_H264_ENC *) openHandle;


	munmap(pCTX->mapped_addr, BUF_SIZE);
	close(pCTX->hOpen);


	return SSBSIP_H264_ENC_RET_OK;
}


void *SsbSipH264EncodeGetInBuf(void *openHandle, long size)
{
	_MFCLIB_H264_ENC	    *pCTX;
	int			    r;
	int 			    i;
	struct mfc_common_args 	    in_param;
	

	////////////////////////////////
	//  Input Parameter Checking  //
	////////////////////////////////
	if (openHandle == NULL) {
		LOG_MSG(LOG_ERROR, "SsbSipH264EncodeGetInBuf", "openHandle is NULL\n");
		return NULL;
	}
	if ((size < 0) || (size > 0x300000)) {
		LOG_MSG(LOG_ERROR, "SsbSipH264EncodeGetInBuf", "size is invalid. (size=%d)\n", size);
		return NULL;
	}

	pCTX  = (_MFCLIB_H264_ENC *) openHandle;


	/////////////////////////////////////////////////
	/////           (DeviceIoControl)           /////
	/////     IOCTL_MFC_GET_INPUT_BUF_ADDR      /////
	/////////////////////////////////////////////////
        pCTX->index=0;
        for(i=0;i<4;i++)
	{
		memset(&in_param,0,sizeof(struct mfc_common_args));
		in_param.args.mem_alloc.mapped_addr=(int)pCTX->mapped_addr;
		in_param.args.mem_alloc.buff_size=size;
		in_param.args.mem_alloc.codec_type=pCTX->codec_type;
		r = ioctl(pCTX->hOpen, IOCTL_MFC_GET_IN_BUF, &in_param);
		if ((r < 0) || (in_param.ret_code < 0)) {
			LOG_MSG(LOG_ERROR, "SsbSipH264EncodeGetInBuf", "Failed in get FRAM_BUF address\n");
			return NULL;
		}
		printf("in_param.args.mem_alloc.out_paddr%d:%d\n",i,in_param.args.mem_alloc.out_paddr);
       		pCTX->yuv_paddr[pCTX->index++]=ALIGN_TO_8KB(in_param.args.mem_alloc.out_paddr);
		pCTX->yuv_uaddr=in_param.args.mem_alloc.out_uaddr+(pCTX->yuv_paddr-in_param.args.mem_alloc.out_paddr);
	}
	 pCTX->index=0;
	return (void *)in_param.args.mem_alloc.out_uaddr;
}
int SsbSipH264EncodeSetConfig(void *openHandle, H264_ENC_CONF conf_type, void *value)
{
	_MFCLIB_H264_ENC  	*pCTX;
	MFC_ARGS			mfc_args;
	int					r;

	unsigned int		num_slices[2];


	////////////////////////////////
	//  Input Parameter Checking  //
	////////////////////////////////
	if (openHandle == NULL) {
		LOG_MSG(LOG_ERROR, "SsbSipH264EncodeSetConfig", "openHandle is NULL\n");
		return SSBSIP_H264_ENC_RET_ERR_INVALID_HANDLE;
	}
	if (value == NULL) {
		LOG_MSG(LOG_ERROR, "SsbSipH264EncodeSetConfig", "value is NULL\n");
		return SSBSIP_H264_ENC_RET_ERR_INVALID_PARAM;
	}

	pCTX  = (_MFCLIB_H264_ENC *) openHandle;

	switch (conf_type) {
	case H264_ENC_SETCONF_NUM_SLICES:
			
		num_slices[0] = ((unsigned int *)value)[0];
		num_slices[1] = ((unsigned int *)value)[1];
		printf("num slices[0] = %d\n", num_slices[0]);
		printf("num slices[1] = %d\n", num_slices[1]);
		
		mfc_args.set_config.in_config_param		= MFC_SET_CONFIG_ENC_SLICE_MODE;
		mfc_args.set_config.in_config_value[0]	= num_slices[0];
		mfc_args.set_config.in_config_value[1]	= num_slices[1];
		
		r = ioctl(pCTX->hOpen, IOCTL_MFC_SET_CONFIG, &mfc_args);
		if ( (r < 0) || (mfc_args.set_config.ret_code < 0) ) {
			LOG_MSG(LOG_ERROR, "SsbSipH264EncodeSetConfig", "Error in H264_ENC_SETCONF_NUM_SLICES.\n");
			return SSBSIP_H264_ENC_RET_ERR_SETCONF_FAIL;
		}
		break;

	case H264_ENC_SETCONF_PARAM_CHANGE:

		mfc_args.set_config.in_config_param		= MFC_SET_CONFIG_ENC_PARAM_CHANGE;
		mfc_args.set_config.in_config_value[0]	= ((unsigned int *) value)[0];
		mfc_args.set_config.in_config_value[1]	= ((unsigned int *) value)[1];

		r = ioctl(pCTX->hOpen, IOCTL_MFC_SET_CONFIG, &mfc_args);
		if ( (r < 0) || (mfc_args.set_config.ret_code < 0) ) {
			LOG_MSG(LOG_ERROR, "SsbSipH264EncodeSetConfig", "Error in H264_ENC_SETCONF_PARA_CHANGE.\n");
			return SSBSIP_H264_ENC_RET_ERR_SETCONF_FAIL;
		}
		break;

	case H264_ENC_SETCONF_CUR_PIC_OPT:

		mfc_args.set_config.in_config_param     = MFC_SET_CONFIG_ENC_CUR_PIC_OPT;
		mfc_args.set_config.in_config_value[0]  = ((unsigned int *) value)[0];
		mfc_args.set_config.in_config_value[1]  = ((unsigned int *) value)[1];

		r = ioctl(pCTX->hOpen, IOCTL_MFC_SET_CONFIG, &mfc_args);
		if ( (r < 0) || (mfc_args.set_config.ret_code < 0) ) {
			LOG_MSG(LOG_ERROR, "SsbSipH264EncodeSetConfig", "Error in H264_ENC_SETCONF_CUR_PIC_OPT.\n");
			return SSBSIP_H264_ENC_RET_ERR_SETCONF_FAIL;
		}
		break;
		
	default:
		LOG_MSG(LOG_ERROR, "SsbSipH264EncodeSetConfig", "No such conf_type is supported.\n");
		return SSBSIP_H264_ENC_RET_ERR_SETCONF_FAIL;
	}

	
	return SSBSIP_H264_ENC_RET_OK;
}


int SsbSipH264EncodeGetConfig(void *openHandle, H264_ENC_CONF conf_type, void *value)
{
	_MFCLIB_H264_ENC  *pCTX;


	////////////////////////////////
	//  Input Parameter Checking  //
	////////////////////////////////
	if (openHandle == NULL) {
		return -1;
	}

	pCTX  = (_MFCLIB_H264_ENC *) openHandle;


	switch (conf_type) {

	case H264_ENC_GETCONF_HEADER_SIZE:
		*((int *)value) = pCTX->enc_hdr_size;
		break;

	default:
		break;
	}


	return SSBSIP_H264_ENC_RET_OK;
}

