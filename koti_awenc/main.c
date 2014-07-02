#include "koti_awenc.h"

int mVideoWidth = 352; //1280;
int mVideoHeight = 288; //720;

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
    const int record_frame_num = 100;

	int data_size = 0;
	char *tem_data = malloc(1024*1024);
	
    ret = koti_awenc_init(mVideoWidth, mVideoHeight, 256*1024);
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

	ret = koti_awenc_start();
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
			++counter;
			fwrite(tem_data, data_size, 1, pEncFile);				
		}
		else
			usleep(10000);

		if(counter >= record_frame_num)
		{
			free(tem_data);
			break;
		}
		printf("============ %d, %d\n", counter, data_size);
	}


	koti_awenc_exit();
	if (pEncFile)
	{
		fclose(pEncFile);
		pEncFile = NULL;
	}
		
	return 0;
}



