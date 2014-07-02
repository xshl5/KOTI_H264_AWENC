#include "koti_awenc.h"

#include "encoder_type.h"
#include "libve_typedef.h"

#include "H264encLibApi.h"
#include "capture.h"

typedef struct camera_buf_info
{
        VEnc_FrmBuf_Info                buf_info;
        int                                     id;
}camera_buf_info;

typedef struct bufMrgQ_t
{
        camera_buf_info                 omx_bufhead[ENC_FIFO_LEVEL];
        int                                             write_id;
        int                                             read_id;
        int                                             buf_unused;
}bufMrgQ_t;

bufMrgQ_t	gBufMrgQ;
VENC_DEVICE *g_pCedarV = NULL;


int g_cur_id = -1;
unsigned long long lastTime = 0 ; 


pthread_t thread_camera_id = 0;
pthread_t thread_enc_id = 0;

pthread_mutex_t mut_cam_buf;
pthread_mutex_t mut_ve;

cache_data *save_bit_stream = NULL;
static int koti_awenc_start_flag = 0;
static int koti_camera_thread_flag = 0, koti_awenc_thread_flag = 0;

#ifdef USE_SUNXI_MEM_ALLOCATOR
extern int sunxi_alloc_open();
#endif

extern int koti_awenc_capture_video_width = 352;
extern int koti_awenc_capture_video_height = 288;

static __s32 WaitFinishCB(__s32 uParam1, void *pMsg)
{
	return cedarv_wait_ve_ready();
}

static __s32 GetFrmBufCB(__s32 uParam1,  void *pFrmBufInfo)
{
	int write_id;
	int read_id;
	int buf_unused;
	int ret = -1;
	V4L2BUF_t Buf;
	VEnc_FrmBuf_Info encBuf;

	memset((void*)&encBuf, 0, sizeof(VEnc_FrmBuf_Info));

	write_id 	= gBufMrgQ.write_id;
	read_id 	= gBufMrgQ.read_id;
	buf_unused	= gBufMrgQ.buf_unused;

	if(buf_unused == ENC_FIFO_LEVEL)
	{
		//printf("GetFrmBufCB: no valid fifo\n");
		return -1;
	}

	pthread_mutex_lock(&mut_cam_buf);
	
	encBuf.addrY = gBufMrgQ.omx_bufhead[read_id].buf_info.addrY;
	encBuf.addrCb = gBufMrgQ.omx_bufhead[read_id].buf_info.addrCb;
	encBuf.pts_valid = 1;
	encBuf.pts = (s64)gBufMrgQ.omx_bufhead[read_id].buf_info.pts;

	encBuf.color_fmt = PIXEL_YUV420;
	encBuf.color_space = BT601;

	g_cur_id = gBufMrgQ.omx_bufhead[read_id].id;

	//printf("g_cur_id, GetFrmBufCB: %d\n", g_cur_id);
	gBufMrgQ.buf_unused++;
	gBufMrgQ.read_id++;
	gBufMrgQ.read_id %= ENC_FIFO_LEVEL;
	pthread_mutex_unlock(&mut_cam_buf);

	memcpy(pFrmBufInfo, (void*)&encBuf, sizeof(VEnc_FrmBuf_Info));
	
	return 0;
}



cache_data *save_bitstream_int(int size)
{
	cache_data *save_bit_stream = (cache_data *)malloc(sizeof(cache_data));
	memset(save_bit_stream, 0, sizeof(cache_data));
	save_bit_stream->size = size;
	save_bit_stream->part_num = 0;
	save_bit_stream->can_save_data = 0;
	save_bit_stream->write_offset = 0;
	save_bit_stream->data = (char *)malloc(save_bit_stream->size);
	
	pthread_mutex_init(&save_bit_stream->mut_save_bs,NULL);
	if(save_bit_stream->data == NULL)
	{
		printf("save_bitstream_int malloc fail\n");
		return NULL;
	}
	return save_bit_stream;
}


int save_bitstream_exit(cache_data *save_bit_stream)
{
	if(save_bit_stream)
	{
		if(save_bit_stream->data)
		{
			free(save_bit_stream->data);
			save_bit_stream->data = NULL;
		}

		pthread_mutex_destroy(&save_bit_stream->mut_save_bs);
		free(save_bit_stream);
		save_bit_stream = NULL;
	
	}
	
	return 0;
}



int update_bitstream_to_cache(cache_data *save_bit_stream, char *output_data, int data_size)
{
	int left_size;
	int offset;
	int last_write_offset;

	pthread_mutex_lock(&save_bit_stream->mut_save_bs);

	if(save_bit_stream->size < data_size)
	{
		pthread_mutex_unlock(&save_bit_stream->mut_save_bs);
		return -1;
	}

	last_write_offset = save_bit_stream->write_offset;
	if((save_bit_stream->write_offset + data_size) >= save_bit_stream->size)
	{
		left_size = save_bit_stream->write_offset + data_size - save_bit_stream->size;
		offset = data_size - left_size;
		
		memcpy(save_bit_stream->data + save_bit_stream->write_offset, output_data, data_size - left_size);

		if(left_size > 0)
		{
			memcpy(save_bit_stream->data, output_data + offset, left_size);
		}
		
		save_bit_stream->write_offset = left_size;
	}
	else
	{
		memcpy(save_bit_stream->data + save_bit_stream->write_offset, output_data, data_size);
		save_bit_stream->write_offset = save_bit_stream->write_offset + data_size;
	}

    if(last_write_offset > save_bit_stream->write_offset)
	{
		save_bit_stream->part_num = 0;
		save_bit_stream->can_save_data = 1;
	}

	else
	{
		if(save_bit_stream->write_offset >= save_bit_stream->size/2 && last_write_offset < save_bit_stream->size/2)
		{
			save_bit_stream->part_num = 1;
			save_bit_stream->can_save_data = 1;
		}
	}

	pthread_mutex_unlock(&save_bit_stream->mut_save_bs);
	
	return 0;
}

int get_bitstream_for_save(cache_data *save_bit_stream, char * tem_data, int *datasize)
{
	int tmp_size;
	
	pthread_mutex_lock(&save_bit_stream->mut_save_bs);
	if(save_bit_stream->can_save_data == 1)
	{
		tmp_size = save_bit_stream->size/2;
		//*datasize = tem_data;
		if(save_bit_stream->part_num ==1)
		{
			memcpy(tem_data, save_bit_stream->data, tmp_size);
			*datasize = tmp_size;
		}
		else
		{
			memcpy(tem_data, save_bit_stream->data + tmp_size, save_bit_stream->size - tmp_size);
			*datasize = save_bit_stream->size - tmp_size;
		}

		save_bit_stream->can_save_data = 0;
	}
	else
	{
		*datasize = 0;
	}
	
	pthread_mutex_unlock(&save_bit_stream->mut_save_bs);
	return 0;
}

int save_left_bitstream(cache_data *save_bit_stream, char * tem_data, int *datasize)
{
	int offset;
	pthread_mutex_lock(&save_bit_stream->mut_save_bs);

	if(save_bit_stream->can_save_data == 0)
	{
		if(save_bit_stream->part_num == 1)
		{
			offset = save_bit_stream->size/2;

			if(save_bit_stream->write_offset > offset)
			{
				memcpy(tem_data, save_bit_stream->data + offset, save_bit_stream->write_offset - offset);
			}

			*datasize = save_bit_stream->write_offset - offset;
		}
		else
		{
			if(save_bit_stream->write_offset > 0)
			{
				memcpy(tem_data, save_bit_stream->data, save_bit_stream->write_offset);
			}

			*datasize = save_bit_stream->write_offset;
		}
	}

	else
	{
		printf("save left bitstream error\n");
	}

	
	pthread_mutex_unlock(&save_bit_stream->mut_save_bs);
	return 0;
}







VENC_DEVICE * CedarvEncInit(__u32 width, __u32 height, __u32 avg_bit_rate)
{
	int ret = -1;
	VENC_DEVICE *pCedarV = NULL;
	
	/* init camera FIFO buffer */
        memset((void*)&gBufMrgQ, 0, sizeof(bufMrgQ_t));
        gBufMrgQ.buf_unused = ENC_FIFO_LEVEL;
#ifdef USE_SUNXI_MEM_ALLOCATOR
	sunxi_alloc_open();
#endif
	pCedarV = H264EncInit(&ret);
	if (ret < 0)
	{
		printf("H264EncInit failed\n");
	}

	__video_encode_format_t enc_fmt;
    enc_fmt.src_width = width;
    enc_fmt.src_height = height;
	enc_fmt.width = width;
	enc_fmt.height = height;
    enc_fmt.frame_rate = 25 * 1000;
	enc_fmt.color_format = PIXEL_YUV420;
	enc_fmt.color_space = BT601;
	enc_fmt.qp_max = 40;
	enc_fmt.qp_min = 20;
	enc_fmt.avg_bit_rate = avg_bit_rate;
	enc_fmt.maxKeyInterval = 8;
	
    //enc_fmt.profileIdc = 77; /* main profile */

    enc_fmt.orientation = 90;
	enc_fmt.profileIdc = 66; /* baseline profile */
	enc_fmt.levelIdc = 31;

	pCedarV->IoCtrl(pCedarV, VENC_SET_ENC_INFO_CMD, &enc_fmt);

	ret = pCedarV->open(pCedarV);
	if (ret < 0)
	{
		printf("open H264Enc failed\n");
	}
	
	pCedarV->GetFrmBufCB = GetFrmBufCB;
	pCedarV->WaitFinishCB = WaitFinishCB;

	return pCedarV;
}

void CedarvEncExit(VENC_DEVICE *pCedarV)
{
	if (pCedarV)
	{
		pCedarV->close(pCedarV);
		H264EncExit(pCedarV);
		pCedarV = NULL;
	}
}


void *thread_camera()
{
	int ret = -1;
	V4L2BUF_t Buf;

	while(1)
	{	
		int		write_id;
		int		read_id;
		int		buf_unused;
		
		if (koti_awenc_start_flag == 0) 
		{
			printf("Exit camera thread\n");		
			break;
		}

		buf_unused	= gBufMrgQ.buf_unused;

		//printf("buf_unused: %d\n", buf_unused);
		
		if(buf_unused == 0)
		{
			usleep(10*1000);
			continue;
		}
				
		// get one frame
		ret = GetPreviewFrame(&Buf);

		//printf("GetPreviewFrame: %d\n", ret);
		if (ret != 0)
		{
			usleep(2*1000);
			printf("GetPreviewFrame failed\n");

		}

		pthread_mutex_lock(&mut_cam_buf);

		write_id 	= gBufMrgQ.write_id;
		read_id 	= gBufMrgQ.read_id;
		buf_unused	= gBufMrgQ.buf_unused;
		if(buf_unused != 0)
		{
			
			gBufMrgQ.omx_bufhead[write_id].buf_info.pts = Buf.timeStamp;
			gBufMrgQ.omx_bufhead[write_id].id = Buf.index;

			gBufMrgQ.omx_bufhead[write_id].buf_info.addrY = Buf.addrPhyY;
            gBufMrgQ.omx_bufhead[write_id].buf_info.addrCb = Buf.addrPhyY + koti_awenc_capture_video_width* koti_awenc_capture_video_height;

			gBufMrgQ.buf_unused--;
			gBufMrgQ.write_id++;
			gBufMrgQ.write_id %= ENC_FIFO_LEVEL;
		}
		else
		{
			
			printf("IN OMX_ErrorUnderflow\n");
		}
		pthread_mutex_unlock(&mut_cam_buf);
		
	}

    koti_camera_thread_flag = 0;
	return (void *)0;  
}


void *thread_enc()
{
	int ret;
	__vbv_data_ctrl_info_t data_info;
	int motionflag = 0;
	int bFirstFrame = 1; //need do something more in first frame

	while(1)
	{

		if (koti_awenc_start_flag == 0) 
		{	
			printf("Exit encode thread\n");	
			break;
		}

		/* the value from 0 to 9 can be used to set the level of the sensitivity of motion detection
		it is recommended to use 0 , which represents the hightest level sensitivity*/
//		g_pCedarV->IoCtrl(g_pCedarV, VENC_LIB_CMD_SET_MD_PARA , 0);


		pthread_mutex_lock(&mut_ve);

		/* in this function , the callback function of GetFrmBufCB will be used to get one frame */
#if 1
		ret = g_pCedarV->encode(g_pCedarV,NULL);
#else
		ret = g_pCedarV->encode(g_pCedarV);
#endif
		
		pthread_mutex_unlock(&mut_ve);

		//printf("encode result: %d\n", ret);


//		if(ret == 0)
//		{
//			/* get the motion detection result ,if the result is 1, it means that motion object have been detected*/
//			g_pCedarV->IoCtrl(g_pCedarV, VENC_LIB_CMD_GET_MD_DETECT, &motionflag);
//			printf("motion detection,result: %d\n", motionflag);
//		}

		
		if (ret != 0)
		{
			/* camera frame buffer is empty */
			usleep(10*1000);
			continue;
		}
	

		/* release the camera frame buffer */
		if(ret == 0)
		{
			pthread_mutex_lock(&mut_cam_buf);

			ReleaseFrame(g_cur_id);	
			
			pthread_mutex_unlock(&mut_cam_buf);

		}


		if(ret == 0)
		{
			memset(&data_info, 0 , sizeof(__vbv_data_ctrl_info_t));
			ret = g_pCedarV->GetBitStreamInfo(g_pCedarV, &data_info);
			
			if(ret == 0)
			{					
				if(1 == bFirstFrame)
				{
					bFirstFrame = 0;

					update_bitstream_to_cache(save_bit_stream, data_info.privateData, data_info.privateDataLen);

					/* encode update data to decode */
				//	decode_update_data_from_enc(hcedarv, &data_info, 1);	

				}
				else
				{
					/* encode update data to decode */
				//	decode_update_data_from_enc(hcedarv, &data_info, 0);
				}

							
				/* save bitstream to cache buffer */
				if (data_info.uSize0 != 0)
				{
					update_bitstream_to_cache(save_bit_stream, data_info.pData0, data_info.uSize0);
				}
				
				
				if (data_info.uSize1 != 0)
				{
					update_bitstream_to_cache(save_bit_stream, data_info.pData1, data_info.uSize1);

				}

								
				/* encode release bitstream */
				g_pCedarV->ReleaseBitStreamInfo(g_pCedarV, data_info.idx);
				
			}
		}
	}

    koti_awenc_thread_flag = 0;
	return NULL;
}


int koti_awenc_init(__u32 width, __u32 height, __u32 avg_bit_rate)
{
	int ret = -1;
	/* init video engine */
    ret = cedarx_hardware_init(0);
    if (ret < 0)
    {
        printf("cedarx_hardware_init failed\n");
        goto exit;
    }

    koti_awenc_capture_video_width = width;
    koti_awenc_capture_video_height = height;

    /* init camera */
    ret = InitCapture();
    if(ret != 0)
    {
        cedarx_hardware_exit(0);
        printf("InitCapture failed\n");
        goto exit;
    }

    /* init  encoder */
    g_pCedarV = CedarvEncInit(width, height, avg_bit_rate);

    /* set VE 320M */
    cedarv_set_ve_freq(320);


    save_bit_stream = save_bitstream_int(avg_bit_rate>>5/*4*1024*/);
    if (save_bit_stream == NULL)
    {
        DeInitCapture();
        CedarvEncExit(g_pCedarV);
        cedarx_hardware_exit(0);
        ret = -1;

        printf("save_bitstream_int failed\n");
        goto exit;
    }

    pthread_mutex_init(&mut_cam_buf,NULL);
    pthread_mutex_init(&mut_ve,NULL);
    koti_camera_thread_flag = 0;
    koti_awenc_thread_flag = 0;
	ret = 0;

exit:
	return ret;
}

int koti_awenc_start()
{
	int ret = -1;
	/* start camera */
	StartStreaming();

	koti_awenc_start_flag = 1;
	/* create camera thread*/
	if(pthread_create(&thread_camera_id, NULL, thread_camera, NULL) != 0)
	{
		printf("Create thread_camera fail !\n");
		return ret;
	}
    koti_camera_thread_flag = 1;
    pthread_detach(thread_camera_id);

	/* create encode thread*/
	if(pthread_create(&thread_enc_id, NULL, thread_enc, NULL) != 0)
	{
		printf("Create thread_enc fail !\n");
		return ret;
	}
    koti_awenc_thread_flag = 1;
	pthread_detach(thread_enc_id);

	return 0;
}

int koti_awenc_get_bitstream(char* data_buf, int* data_size)
{
	get_bitstream_for_save(save_bit_stream, data_buf, data_size);

//	save_left_bitstream(save_bit_stream, tem_data, &data_size);
	return *data_size;
}

int koti_awenc_exit()
{
    if(koti_awenc_start_flag == 0)
        return 1;

	koti_awenc_start_flag = 0;
    while(koti_camera_thread_flag == 1 || koti_awenc_thread_flag == 1)
    {
        usleep(10000);
    }

	pthread_mutex_destroy(&mut_cam_buf);
	pthread_mutex_destroy(&mut_ve);

	save_bitstream_exit(save_bit_stream);

	DeInitCapture();
	CedarvEncExit(g_pCedarV);
	cedarx_hardware_exit(0);
		
	return 0;
}


