
#define ALLOW_SELECT
#define ALLOW_BUFFER_MAPPING

#ifdef MODULE
#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#endif

#include <linux/param.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/tty.h>
#include <linux/ctype.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <sys/kd.h>
#include <linux/wait.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/ioport.h>

#include <linux/soundcard.h>

typedef char snd_rw_buf;
typedef caddr_t ioctl_arg;

#define FALSE	0
#define TRUE	1

struct snd_wait {
	  int mode; int aborting;
	};

extern int sound_alloc_dma(int chn, char *deviceID);
extern int sound_open_dma(int chn, char *deviceID);
extern void sound_free_dma(int chn);
extern void sound_close_dma(int chn);

#define RUNTIME_DMA_ALLOC

extern caddr_t sound_mem_blocks[1024];
extern int sound_num_blocks;

#define SND_SA_INTERRUPT

typedef int sound_os_info;
