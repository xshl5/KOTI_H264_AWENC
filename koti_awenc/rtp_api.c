/*
 **************************************************************************************
 *                      CopyRight (C) jhsys Corp, Ltd.
 *
 *       Filename:  rtp_api.c
 *    Description:  source file
 *
 *        Version:  1.0
 *        Created:  Friday, June 16, 2012 10:15:10 CST
 *         Author:  dujf     [dujf@koti.cn]
 *
 *       Revision:  initial draft;
 **************************************************************************************
 */

#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#include "rtp_api.h"
#include "../inc/rtp_all.h"

#include <android/log.h>

#define SAVE_TO_H264_FILE 0
#define VIDEO_BUF_LEN  1024*76
//#define H264_FILE_PATH "/mnt/sdcard/"

#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "rtp_api", __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG , "rtp_api", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO , "rtp_api", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN , "rtp_api", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR , "rtp_api", __VA_ARGS__)

static struct RTP_SESSION* g_session = NULL;
static CREATE_RTP_PARAMS* g_params = NULL;
static int g_loop = 0;
static int g_TimerCreated = 0;
static int g_InitChecked = 0;
static int g_IsRecvSpsAndPps = 0;
static int g_RtpStoped = 1, rtp_send_thread_stoped = 1;
static unsigned int g_PFrameCount = 0;
static void *g_Cokies = NULL;


static rtpCallBackFunc ProcessH264Data = NULL;

static pthread_t thread_id, send_thread_id;
static u_int32 unTimestamp = 0;

#if SAVE_TO_H264_FILE
static char filename[256] = {0};
static FILE *g_pFile = NULL;
#endif

//pthread_cond_t rtpCond = PTHREAD_COND_INITIALIZER;
//pthread_mutex_t rtpLock = PTHREAD_MUTEX_INITIALIZER;

#include "../koti_awenc/koti_awenc.h"
const int CAPTURE_VIDEO_WIDTH = 640;//640; //1280;
const int CAPTURE_VIDEO_HEIGHT = 384;//480; //720;

#define KOTI_PREVIEW_VIDEO_PATH "/kotidata/preview.h264"


void RtpSetParams(CREATE_RTP_PARAMS* params)
{
    //LOGD("RtpSetParams");
    if(g_params != NULL && params != NULL)
    {
        g_params->bvoiptype = params->bvoiptype;
        g_params->mode = params->mode;
        g_params->daddr = params->daddr;
        g_params->dport = params->dport;
        g_params->sport = params->sport;
        
        g_params->payloadtype = params->payloadtype;
        g_params->dspmsgtype = params->dspmsgtype;
        g_params->packettime = params->packettime;
        
        g_params->rtptos = params->rtptos;
        g_params->rtcptos = params->rtcptos;
        g_params->direction = params->direction;
        g_params->bindwidth = params->bindwidth;
        g_params->audiochannel = params->audiochannel;
        g_params->videochannel = params->videochannel;
        
        memcpy(g_params->spec, params->spec, sizeof(params->spec));
        
        g_params->redundancy = params->redundancy;
        g_params->telephone_event = params->telephone_event;

        g_params->X_redundancy = params->X_redundancy;
        g_params->X_telephone_event = params->X_telephone_event;
    }
}

void RtpSetTimerCallBackFunc()
{
    if(!g_TimerCreated)
    {
        g_TimerCreated = 1;
        TimerModuleMain();
    }

    RTP_CALLBACK callback_func;
    callback_func.timer_create = Timer_create;
    callback_func.timer_delete= Timer_destroy;
    callback_func.timer_start= Timer_start;
    callback_func.timer_stop = Timer_stop;
    callback_func.process_pkt  = NULL;

    SP_RTP_setCallbackFunc (&callback_func);
}

void RtpSetProcessCallBackFunc(void* cokies, rtpCallBackFunc callbackFunc)
{
    g_Cokies = cokies;
    ProcessH264Data = callbackFunc;
}

int RtpInit(CREATE_RTP_PARAMS* params)
{
    LOGD("RtpInit");
    if(g_params == NULL)
    {
        g_params = (CREATE_RTP_PARAMS*)malloc(sizeof(CREATE_RTP_PARAMS));
        if(g_params == NULL)
        {
            goto FAIL;
        }
    }

    RtpSetParams(params);
    RtpSetTimerCallBackFunc();

    g_session = SP_RTP_create(g_params);
    if(g_session == NULL)
    {
        LOGE("SP_RTP_create FAIL.");
        free(g_params);
        g_params = NULL;
        goto FAIL;
    }

    LOGE("Sockets: rtpfd(%d), rtcpfd(%d)", g_session->rtpfd, g_session->rtcpfd);

    unTimestamp = 0/*rand() * RAND_MAX*/;
    g_InitChecked = 1;
    g_loop = 0;
    g_RtpStoped = 1;
    rtp_send_thread_stoped = 1;
    return RTP_RETURN_SUCCESS;

FAIL:
    LOGE("Rtp_init Fail.\n");
    g_InitChecked = 0;
    return RTP_RETURN_FAIL;
}

#if SAVE_TO_H264_FILE
void StoreH264RData(void *data, unsigned int dataLen, FILE* pfile)
{
    if(NULL == data || 0 == dataLen || NULL == pfile)
    {
        return;
    }

    //char startStr[] = {0x00, 0x00, 0x00, 0x01};
    //fwrite(startStr, sizeof(startStr), 1, pfile);
    
    char *header = (char *)data;
    fwrite(header, dataLen, 1, pfile);
}
#endif

void* RtpRecvDataThread(void* param)
{
    int listen = -1;
    int retriResult = -1;
    char* header = NULL;
    char* buff = NULL;
    int len = 0;
    int tempLen = 0;

    RTP_MEDIA_DATA* pMediaData = NULL;
    
    fd_set readFD;
    FD_ZERO(&readFD);

    if(g_session == NULL || g_loop == 0)
    {
        LOGE("g_session == NULL or g_loop == 0, RtpRecvDataThread stop.");
        if(g_params != NULL)
        {
            free(g_params);
            g_params = NULL;
        }
        g_RtpStoped = 1;
        return NULL;
    }

    if(g_session->payloadtype == RTP_PT_H264)
    {
        char h264StartCode[4] = {0x00, 0x00, 0x00, 0x01};
        int countIFrameRecv = 0;
        struct timeval timeout;
        u_int timestamp = 0;
        u_int seq = -1;
        u_int num = 0;


        char spsBuff[] = {0x80, 0xe2, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb3, 
            0x24, 0x09, 0xb6, 0xa5, 0x67, 0x42, 0x00, 0x1e, 0xa6, 0x81, 0xd0, 0xc6, 0x40};
/*
        char spsBuff[] = {0x67, 0x42, 0x00, 0x1e, 0xa6, 0x81, 0xd0, 0xc6, 0x40};

        RTP_HEADER RtpHead;
        RtpHead.ver = 2;
        RtpHead.cc = 0;
        RtpHead.ext = 0;
        RtpHead.pad = 0;
        RtpHead.mark = 1;
        RtpHead.pt = 98;
        RtpHead.seq = 0;
        RtpHead.time = 0;

        char totBuff[256] = {0};
        memcpy(totBuff, &RtpHead, sizeof(RTP_HEADER));
        memcpy
*/        
        unsigned short *pSeqNum = (unsigned short *)(&spsBuff[2]);
        unsigned int  *pTimeStamp = (unsigned int *)(&spsBuff[4]);

        unsigned short seqNum = 0;//ntohs(*pSeqNum);
        unsigned int  TimeStamp = 1;//ntohl(*pTimeStamp);
        LOGE("+++++++++seqNum(%u), TimeStamp(%u)+++++++++++", seqNum, TimeStamp);
        
        struct sockaddr_in to;
        bzero(&to,sizeof(struct sockaddr_in));
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = htonl(g_session->dstaddr);
        to.sin_port = htons(g_session->dstports[0]);

        if(g_loop)
        {
            LOGE("RtpRecvDataThread Begin !");
        }
        while(g_loop)
        {
            FD_SET(g_session->rtpfd, &readFD);
            timeout.tv_sec  = 0;
            timeout.tv_usec = 200000;

//            num++;
            
            *pSeqNum = htons(seqNum);
            *pTimeStamp = htonl(TimeStamp);
            seqNum++;
            TimeStamp++;
            

            listen = select((g_session->rtpfd)+1, &readFD, NULL, NULL, &timeout);
            //LOGE("listen(%d)", listen);
            if (listen > 0)
            {
                if (NULL == header)
                {
                    header = (char*)malloc(VIDEO_BUF_LEN);
                    if (NULL == header)
                    {
                        continue;
                    }
                    memcpy(header, h264StartCode, 4);
                    buff = header+4;
                    len = 4;
                }

                if (len >= (VIDEO_BUF_LEN - 1500))
                {
                    free(header);
                    header = NULL;
                    buff   = NULL;
                    len    = 0;
                    continue;
                }

                tempLen = 0;
                retriResult = SP_RTP_receive_h264_packet(g_session, buff, &tempLen, &timestamp, &seq);
#if 0
                int temp = seq;
                if (lastSeq != (temp - 1))
                {
                    errCount++;
                    printf("----------------lastSeq:%d**seq:%d", lastSeq, temp);
                }
                lastSeq = temp;
#endif 
                //LOGE("retriResult(%d)", retriResult);
                if (RTP_RCV_END == retriResult)
                {
                    if (header != NULL)
                    {
                        free(header);
                    }
                    header = NULL;
                    buff   = NULL;
                    len    = 0;
                }
                else if (RTP_RCV_1FRAME == retriResult)
                {
                    RTP_Data msg;
                    msg.buf  = header;
                    msg.len  = len + tempLen;
                    msg.mark = 1;
                    msg.seq  = seq;
                    msg.tm   = timestamp;

                    if (countIFrameRecv < 300 && !g_IsRecvSpsAndPps)
                    {
                        int nalType = header[4] & 0x1f;
                        if(nalType == 7)//get sps and pps
                        {
                            LOGE("+++++++++get SPS && PPS+++++++++\n");
                            g_IsRecvSpsAndPps = 1;
                        }
                        else
                        {
                            g_session->intra = 1;
                            //intra_send_rtcp(g_session);
                            countIFrameRecv++;
                        }
                    }
                    else
                    {
                        g_session->intra = 0;
                    }

#if SAVE_TO_H264_FILE
                    if(g_pFile == NULL)
                    {
                        time_t t = time(0);
                        memset(filename, '\0', sizeof(filename));
                        strftime(filename, sizeof(filename), "/mnt/sdcard/%Y%m%d%H%M%S", localtime(&t));
                        strcat(filename,"pre.h264");
                        g_pFile = fopen(filename, "a+");
                        if(g_pFile == NULL)
                        {
                            LOGE("Cannot open file(%s)\n", filename);
                        }
                    }

                    StoreH264RData(msg.buf, msg.len, g_pFile);
#endif

/*********************process h264 data here********************/
                    if((ProcessH264Data != NULL) && (g_IsRecvSpsAndPps==1))
                    //if(ProcessH264Data != NULL)
                    {
                        (*ProcessH264Data)(g_Cokies, &msg);
                        num++;
                    }
/*********************process h264 data here********************/

                    //header = NULL;
                    buff   = header+4;//NULL;
                    len    = 4;
                }
                else if(RTP_RCV_1FU== retriResult)
                {
                    buff += tempLen;
                    len  += tempLen;
                }
                else if (RTP_RCV_CONTINUE == retriResult)
                {
                    buff += tempLen;
                    memcpy(buff, h264StartCode, sizeof(h264StartCode));
                    buff += 4;
                    len  += (tempLen + 4);
                }
                
                if(num%3 == 0 && num != 0)
                {
                    if(sendto(g_session->rtpfd, spsBuff, sizeof(spsBuff), 0, (struct sockaddr *)&to, sizeof(to)) < 0)
                    {
                        LOGE("+++++Cannot send spsBuff to %s(%u), error(%s)+++++", (char *)inet_ntoa(to.sin_addr.s_addr), ntohs(to.sin_port), strerror(errno));
                    }
                    //else
                    //{
                    //    LOGE("Sending spsBuff to %s(%u)", (char *)inet_ntoa(to.sin_addr.s_addr), ntohs(to.sin_port));
                    //}
                }

            }
            else
            {
                //LOGE("TimeStamp1(%d), TimeStamp2(%d), TimeStamp3(%d), TimeStamp4(%d)", 
                //            spsBuff[4], spsBuff[5], spsBuff[6], spsBuff[7]);
                //LOGE("SeqNum(%u), TimeStamp(%u) in SPS", *pSeqNum, *pTimeStamp);
//                if(sendto(g_session->rtpfd, spsBuff, sizeof(spsBuff), 0, (struct sockaddr *)&to, sizeof(to)) < 0)
//                {
//                    LOGE("-----Cannot send spsBuff to %s(%u), error(%s)------", (char *)inet_ntoa(to.sin_addr.s_addr), ntohs(to.sin_port), strerror(errno));
//                }
                //else
                //{
                //    LOGE("Sending spsBuff to %s(%u)", (char *)inet_ntoa(to.sin_addr.s_addr), ntohs(to.sin_port));
                //}
                
                if(num%30 == 0)
                {
                    LOGE("NO data Coming...");
                }
            }
        }
    }

    if(header != NULL)
    {
        free(header);
        header = NULL;
        buff = NULL;
    }
    
#if SAVE_TO_H264_FILE
    if(g_pFile != NULL)
    {
        fclose(g_pFile);
        g_pFile = NULL;
    }
#endif

    LOGE("RtpRecvDataThread has stoped.");
    g_RtpStoped = 1;
    
    return NULL;
}

/**
The FU indicator octet has the following format:
   +---------------+
   |0|1|2|3|4|5|6|7|
   +-+-+-+-+-+-+-+-+
   |F|NRI|  Type   |
   +---------------+
**/
typedef struct{
    unsigned char TYPE:5;
    unsigned char NRI:2;
    unsigned char F:1;
}H264_NALU_OR_FU_INDICATOR;

/**
The FU header has the following format:
   +---------------+
   |0|1|2|3|4|5|6|7|
   +-+-+-+-+-+-+-+-+
   |S|E|R|  Type   |
   +---------------+
**/
typedef struct
{
    unsigned char TYPE:5;
    unsigned char R:1;
    unsigned char E:1;
    unsigned char S:1;
}H264_FU_HEADER;

#define MAX_UDP_PACKET_SIZE 1100
#define H264_START_CODE_LENGTH 4

static inline char* find_h264_start_code(char* buf, int len)
{
    int i = 0, tmp_len = len - 3;
    for(; i<tmp_len; ++i)
    {
        if(buf[i] == 0 && buf[i+1] == 0 &&
                buf[i+2] == 0 && buf[i+3] == 1)
//        if(buf[i] == 0 && buf[i+1] == 0 &&
//                buf[i+2] == 1)
        {
            return buf+i;
        }
    }

    return NULL;
}

static inline char* find_h264_end_code(char* buf, int len)
{
    int i = 0;
    for(; i<len; ++i)
    {
        if( (buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 0) ||
               (buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 1) )
        {
            return buf+i;
        }
    }

    return NULL;
}

/**
 * @brief koti_nalu_in_buf
 * @param in_buf
 * @param in_buf_len
 * @param out_nalu_buf
 *
 * @return
 * -1, No start_code found, Discard the in_buf
 *  0, Found start_code(0x000001), but no end_code(0x000000/0x000001) found; Found end_code next time.
 * >0, Both start_code and end_code found, Output nalu_buf and Return nalu_buf_length
 */
static inline int/*out_nalu_buf_len*/ koti_nalu_in_buf(char* in_buf, int in_buf_len, char* out_nalu_buf,
                                                       char** next_in_buf, int* next_in_buf_len)
{
    int out_nalu_buf_len = -1;
    char* nalu_start, *nalu_end = NULL, *buf_pos;
    int n = 0;
    int tmp_len = 0;

    nalu_start = find_h264_start_code(in_buf, in_buf_len);
    // If no start_code found
    if(nalu_start == NULL)
        goto func_exit;

    /**
     * xx  xx  xx  xx  00  00  01   .......
     * --------------  nalu_start --buf_pos
    **/
    buf_pos = nalu_start + H264_START_CODE_LENGTH;
    tmp_len = in_buf_len - ((nalu_start - in_buf) + H264_START_CODE_LENGTH) - 2;

    out_nalu_buf_len = 0;
    for(n=0; n<tmp_len; ++n)
    {
//        if( buf_pos[n] == 0 && buf_pos[n+1] == 0 && (buf_pos[n+2] == 1 || buf_pos[n+2] == 0) )
        if(buf_pos[n] == 0 && buf_pos[n+1] == 0 &&
                        buf_pos[n+2] == 0 && buf_pos[n+3] == 1)
        {
            nalu_end = buf_pos + n;
            break;
        }
        else
        {
            ++out_nalu_buf_len;
            out_nalu_buf[n] = buf_pos[n];
        }
    }

    // Found start_code(0x000001), but no end_code(0x000000/0x000001) found
    if( nalu_end == NULL )
    {
        out_nalu_buf_len = 0;
        *next_in_buf = nalu_start;
        *next_in_buf_len = in_buf_len - (nalu_start - in_buf);
    }
    // Both start_code and end_code found
    else
    {
        if(out_nalu_buf_len >= 2)
        {
            *next_in_buf = nalu_end;
            *next_in_buf_len = in_buf_len - (nalu_end - in_buf);
        }
        // out_nalu_buf_len < 2, err on nalu
        else
        {
            out_nalu_buf_len = 0;
            *next_in_buf = buf_pos;
            *next_in_buf_len = tmp_len+2;
        }
    }

func_exit:
    return out_nalu_buf_len;
}

void* RtpSendDataThread(void* param)
{
    int data_size = 0;
    char *tem_data = NULL;
    RTP_MEDIA_DATA packet;
    packet.nPayloadType = RTP_PT_H264;
    packet.ucMark = 0;

    H264_NALU_OR_FU_INDICATOR* h264_nalu = NULL, *fu_indicator = NULL;
    H264_FU_HEADER* fu_header = NULL;
    int offset = 0, h264buf_len = 0, frame_len = 0;
    char* in_buf = NULL;
    char h264_payload[1500] = {0}, out_nalu_buf[100000] = {0};
    int n=0, cur_frame_payload_nums = 0;
    int big_frame_tmp_len = 0;
    const int fu_a_header_length = 2;

    char sps_pps_buf[2][64] = { {0}, {0} };
    RTP_MEDIA_DATA sps_packet, pps_packet;

    // For count-time sending
    struct timeval tv1, tv2 = {0, 0};
    struct timeval tv0; // For preview_video_time counter

    // Preview video to upload.
    const int read_frame_time_interval = 66000; // 25 ms
    int preview_video_time = 0;
    FILE* enc_fp = NULL;
    if(g_params && g_params->mode != 0)
    {
        enc_fp = fopen(KOTI_PREVIEW_VIDEO_PATH, "wb");
        if(enc_fp == NULL)
            LOGE("Created file %s failed.\n", KOTI_PREVIEW_VIDEO_PATH);
        else
            gettimeofday(&tv0, NULL);
    }

    tem_data = malloc(1024*1024);
    if(g_session == NULL || tem_data == NULL)
    {
        LOGE("==== Start RtpSendDataThread failed.\n");
        goto exit;
    }

    while(1)
    {
        koti_awenc_get_bitstream(tem_data + offset, &data_size);
        if(data_size > 0)
        {
            // For preview video to upload, Up to 10 seconds
            if(tv2.tv_sec != 0 && tv0.tv_sec != 0)
                preview_video_time = (tv2.tv_sec-tv0.tv_sec)*1000000 + tv2.tv_usec - tv0.tv_usec;
            if(enc_fp && preview_video_time < 10000000)
                fwrite(tem_data + offset, data_size, 1, enc_fp);
            // no need for countering preview_video_time.
            else
            {
                tv0.tv_sec = 0;
                tv0.tv_usec = 0;
            }

            // Gets h264buf_len
            h264buf_len = data_size + offset;
            in_buf = tem_data;

            while(1)
            {
                // Get start datetime
                if(tv1.tv_sec == 0 && tv1.tv_usec == 0)
                    gettimeofday(&tv1, NULL);

                frame_len = koti_nalu_in_buf(in_buf, h264buf_len, out_nalu_buf,
                                             &in_buf, &h264buf_len);
                // No start_code found
                if(frame_len == -1)
                {
                    offset = 0;
                    break;
                }
                else if(frame_len == 0)
                {
                    for(n=0; n<h264buf_len; ++n)
                        tem_data[n] = in_buf[n];
                    offset = h264buf_len;

                    break;
                }

                // I Frame , sent SPS & PPS frist.
                if( (out_nalu_buf[0] & 0x1f) == 5 && sps_pps_buf[0] != 0 &&  sps_pps_buf[1] != 0)
                {
                    sps_packet.unTimestamp = unTimestamp;
                    if(SP_RTP_send_h264_Packet(g_session, &sps_packet) != 0)
                        LOGE("==== SP_RTP_Transmit_send_h264_Packet send SPS failed.\n");

                    pps_packet.unTimestamp = unTimestamp;
                    if(SP_RTP_send_h264_Packet(g_session, &pps_packet) != 0)
                        LOGE("==== SP_RTP_Transmit_send_h264_Packet send PPS failed.\n");
                }

                // H264 nalu packet sending.
                if(frame_len <= MAX_UDP_PACKET_SIZE)
                {
                    // Copy and skip frame start_code.
                    packet.pDataBuf = out_nalu_buf;
                    packet.nLen = frame_len;
                    packet.ucMark = 1;
                    packet.unTimestamp = unTimestamp;

                    // SPS & PPS, store it.
                    if( (out_nalu_buf[0] & 0x1f) == 7 || (out_nalu_buf[0] & 0x1f) == 8 )
                    {
                        // For sps
                        if( (out_nalu_buf[0] & 0x1f) == 7 )
                        {
                            memcpy(&sps_packet, &packet, sizeof(packet));
                            memcpy(sps_pps_buf[0], out_nalu_buf, frame_len);
                            sps_packet.pDataBuf = sps_pps_buf[0];
                        }
                        // For pps
                        else
                        {
                            memcpy(&pps_packet, &packet, sizeof(packet));
                            memcpy(sps_pps_buf[1], out_nalu_buf, frame_len);
                            pps_packet.pDataBuf = sps_pps_buf[1];
                        }

                        continue;
                    }
                    else
                    {
                        // LOGE("------------------------------ H264 packet: %d, %d, %p, %d\n", packet.unTimestamp, packet.nLen, packet.pDataBuf, (out_nalu_buf[0] & 0x1f));
                        if(SP_RTP_send_h264_Packet(g_session, &packet) != 0)
                            LOGE("==== SP_RTP_Transmit_send_h264_Packet send data failed.\n");
                    }
                }
                // A big frame, must cut up it.
                else
                {
                    big_frame_tmp_len = frame_len-1;
                    cur_frame_payload_nums = big_frame_tmp_len / MAX_UDP_PACKET_SIZE;
                    if(big_frame_tmp_len % MAX_UDP_PACKET_SIZE)
                        cur_frame_payload_nums += 1;

                    n = 0;
                    h264_nalu = out_nalu_buf;
                    fu_indicator = h264_payload;
                    fu_header = h264_payload + 1;
                    for(; n<cur_frame_payload_nums; ++n)
                    {
                        *fu_indicator = *h264_nalu;
//                        fu_indicator->F = 0;
//                        fu_indicator->NRI = 2;
                        fu_indicator->TYPE = /*h264_nalu->TYPE*//*5*//*1*/28; // 28, FU-A; 29, FU-B

                        fu_header->TYPE = h264_nalu->TYPE;
                        fu_header->S = 0;
                        fu_header->E = 0;
                        fu_header->R = 0;
                        // The first RTP packet.
                        if(n == 0)
                            fu_header->S = 1;

                        packet.pDataBuf = h264_payload;
                        packet.nLen = fu_a_header_length;
                        packet.ucMark = 0;
                        packet.unTimestamp = unTimestamp;
                        // The last RTP packet.
                        if(n == cur_frame_payload_nums - 1)
                        {
                            fu_header->E = 1;
                            packet.nLen += big_frame_tmp_len % MAX_UDP_PACKET_SIZE;
                            memcpy(h264_payload+fu_a_header_length, out_nalu_buf+1+MAX_UDP_PACKET_SIZE*n,
                                   big_frame_tmp_len % MAX_UDP_PACKET_SIZE);

                            packet.ucMark = 1;
                        }
                        else
                        {
                            packet.nLen += MAX_UDP_PACKET_SIZE;
                            memcpy(h264_payload+fu_a_header_length, out_nalu_buf+1+MAX_UDP_PACKET_SIZE*n,
                                   MAX_UDP_PACKET_SIZE);
                        }

                        // LOGE("------------------------------ H264 packet: %d, %d\n", packet.unTimestamp, packet.nLen);
                        if(SP_RTP_send_h264_Packet(g_session, &packet) != 0)
                            LOGE("==== SP_RTP_Transmit_send_h264_Packet send data failed.\n");
                    }

                }

                gettimeofday(&tv2, NULL);
                if( (tv2.tv_sec-tv1.tv_sec)*1000000 + tv2.tv_usec - tv1.tv_usec < read_frame_time_interval )
                    usleep(read_frame_time_interval - ((tv2.tv_sec-tv1.tv_sec)*1000000 + tv2.tv_usec - tv1.tv_usec) );
                tv1.tv_sec = 0;
                tv1.tv_usec = 0;

                unTimestamp += 3600;
            }
        }
        else
            usleep(100);

        if(g_loop == 0)
            break;
    }

exit:
    if(tem_data != NULL)
    {
        free(tem_data);
        tem_data = NULL;
    }

    // For preview video to upload.
    if(enc_fp)
        fclose(enc_fp);

    rtp_send_thread_stoped = 1;
    return NULL;
}

int RtpStart()
{
    int ret = RTP_RETURN_FAIL;
    LOGD("RtpStart");
    if(!g_InitChecked)
    {
        LOGE("Need Init First, Rtp Start Fail.\n");
        return RTP_RETURN_FAIL;
    }

    // Preview video, sleep to wait remote devices.
//    if(g_params && g_params->mode != 0)
//        usleep(3000000);

    g_loop = 1;
    // RtpSendDataThread
    ret = koti_awenc_init(CAPTURE_VIDEO_WIDTH, CAPTURE_VIDEO_HEIGHT, 3072*1024);
    if(ret != 0)
    {
        LOGE("koti_awenc_init failed\n");
        goto rtp_recv_mod;
    }
    ret = koti_awenc_start();
    if(ret != 0)
    {
        LOGE("koti_awenc_start failed\n");
        goto rtp_recv_mod;
    }
    if(pthread_create(&send_thread_id, NULL, RtpSendDataThread, NULL) == 0)
    {
        pthread_detach(send_thread_id);
        rtp_send_thread_stoped = 0;
    }

rtp_recv_mod:
    // RtpRecvDataThread
//    if(0 != pthread_create(&thread_id, NULL, RtpRecvDataThread, NULL))
//    {
//        LOGE("FAIL to create RtpRecvDataThread, RtpStart start FAIL.");
//        g_loop = 0;
//        return RTP_RETURN_FAIL;
//    }
//    pthread_detach(thread_id);
//    g_RtpStoped = 0;

    LOGE("Create RtpRecvDataThread Success.");
    return RTP_RETURN_SUCCESS;
}

int RtpStop()
{
    LOGD("RtpStop");
    g_loop = 0;

    while(g_RtpStoped == 0 || rtp_send_thread_stoped == 0)
    {
        usleep(10000);//10ms
    }
    g_IsRecvSpsAndPps = 0;

    if(g_session != NULL)
    {
        LOGE("SP_RTP_delete");
        SP_RTP_delete(g_session);
        //free(g_session);
        g_session = NULL;
    }

    if(g_params != NULL)
    {
        free(g_params);
        g_params = NULL;
    }

    koti_awenc_exit();

    LOGE("RtpStop Success!!!");
    return RTP_RETURN_SUCCESS;
}

void RtpReset()
{
    ;
}
