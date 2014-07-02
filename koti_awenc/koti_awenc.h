#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define ENC_FIFO_LEVEL 5
#define MAX_DISP_ELEMENTS   32

typedef struct cache_data
{
        int size;
        int write_offset;
        int part_num;
        int can_save_data;
        char *data;
        pthread_mutex_t mut_save_bs;
}cache_data;

int koti_awenc_init(__u32 width, __u32 height, __u32 avg_bit_rate);
int koti_awenc_start();
int koti_awenc_get_bitstream(char* data_buf, int* data_size);
int koti_awenc_exit();

