#if 1
#include "koti_awenc.h"
#include <time.h>

int mVideoWidth = 640; //1280;
int mVideoHeight = 480; //720;

FILE * pEncFile = NULL;

#ifdef __OS_LINUX

char saveFile[128] = "/mnt/h264.dat";

#else
char saveFile[128] = "/mnt/sdcard/h264.dat";
#endif



int main()
{
	int ret = -1;
	int counter = 0;
    const int record_frame_num = 1000;

	int data_size = 0;
	char *tem_data = malloc(1024*1024);
	struct timeval tv = {0, 0};
	
	gettimeofday(&tv, NULL);
	printf("%lu,%lu\n", tv.tv_sec, tv.tv_usec);
    ret = koti_awenc_init(mVideoWidth, mVideoHeight, 3072*1024);
	gettimeofday(&tv, NULL);
	printf("%lu,%lu\n", tv.tv_sec, tv.tv_usec);
	if(ret != 0)
	{
		printf("koti_awenc_init failed\n");
		return ret;
	}

	pEncFile = fopen(saveFile, "wb+");
	if (pEncFile == NULL)
	{
		printf("open %s failed\n", saveFile);
		return -1;
	}

	gettimeofday(&tv, NULL);
	printf("%lu,%lu\n", tv.tv_sec, tv.tv_usec);
	ret = koti_awenc_start();
	gettimeofday(&tv, NULL);
	printf("%lu,%lu\n", tv.tv_sec, tv.tv_usec);
	if(ret != 0)
	{
		printf("koti_awenc_start failed\n");
		return ret;
	}

	while(1)
	{
		koti_awenc_get_bitstream(tem_data, &data_size);
		if(data_size > 0)
		{
	gettimeofday(&tv, NULL);
	printf("%lu,%lu\n", tv.tv_sec, tv.tv_usec);
			++counter;
			fwrite(tem_data, data_size, 1, pEncFile);				
        		printf("============ %d, %d\n", counter, data_size);
		}
		else
			usleep(1000);

		if(counter >= record_frame_num)
		{
			free(tem_data);
			break;
		}
	}


	koti_awenc_exit();
	if (pEncFile)
	{
		fclose(pEncFile);
		pEncFile = NULL;
	}
		
	return 0;
}

#else

/********************************************************
 *
 *   Author : joans@joans-computer
 *   Date   : 2011-08-13
 *   File   : main.c
 *
 *
 ********************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/can/error.h>
#include <string.h>
#include <fcntl.h>
#include <linux/videodev2.h>

int fd;
const char *input_dev = "/dev/video0";
const char *qctrl_name = NULL;
int qctrl_value = 0;

struct v4l2_capability cap;
struct v4l2_queryctrl qctrl;

static void print_qctrl(struct v4l2_queryctrl *qctrl)
{
  struct v4l2_control ctrl;

  ctrl.id = /*qctrl->id*/V4L2_CID_BRIGHTNESS;
  if (ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0) {
    perror("get ctrl failed");
    ctrl.value = -999;
  }

  printf("%-14s : id=%08x, type=%d, minimum=%d, maximum=%d\n"
         "\t\t value = %d, step=%d, default_value=%d\n",
         qctrl->name, qctrl->id, qctrl->type, qctrl->minimum, qctrl->maximum,
         ctrl.value, qctrl->step, qctrl->default_value);
}
static void print_menu(struct v4l2_querymenu *menu)
{
  printf("\t %d : %s\n", menu->index, menu->name);
}
static int set_qctrl(struct v4l2_queryctrl *qctrl)
{
  struct v4l2_control ctrl;

  printf("set %s = %d\n", qctrl_name, qctrl_value);

  ctrl.id = qctrl->id;
  ctrl.value = qctrl_value;
  return ioctl(fd, VIDIOC_S_CTRL, &ctrl);
}
static void deal_qctrl(struct v4l2_queryctrl *qctrl)
{
  print_qctrl(qctrl);
  if (qctrl_name && !strcmp(qctrl_name, qctrl->name))
    set_qctrl(qctrl);
}

static void qctrl_get(int id)
{
    qctrl.id = id;
    if (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
      deal_qctrl(&qctrl);
      if (qctrl.type == V4L2_CTRL_TYPE_MENU) {
        int idx;
        struct v4l2_querymenu menu;
        for (idx = qctrl.minimum; idx <= qctrl.maximum; idx++) {
          menu.id = qctrl.id;
          menu.index = idx;
          if (ioctl(fd, VIDIOC_QUERYMENU, &menu)==0) {
            print_menu(&menu);
          }
        }
      }
    }
}

int main(int argc, char **argv)
{
  int ret, i;

  if (argc == 3) {
    qctrl_name = argv[1];
    qctrl_value = atoi(argv[2]);
  }

  fd = open(input_dev, O_RDWR);
  if (fd < 0) {
    perror("open video failed");
    return -1;
  }
  printf("open video '%s' success\n", input_dev);

  ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
  if (ret < 0) {
    perror("ioctl querycap");
    return -1;
  }

  if ((cap.capabilities &  V4L2_CAP_VIDEO_CAPTURE) == 0) {
    printf("video device donot support capture\n");
    return -1;
  }

  for (i = V4L2_CID_BASE; i < V4L2_CID_LASTP1; i++) {
    qctrl_get(i);
  }

  for (i = V4L2_CID_PRIVATE_BASE; i < V4L2_CID_PRIVATE_BASE+25; i++) {
    qctrl_get(i);
  }


  printf("close video\n");
  close(fd);

  return 0;
}
#endif



