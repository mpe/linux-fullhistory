
#define ALLOW_SELECT
#undef NO_INLINE_ASM
#define SHORT_BANNERS
#define MANUAL_PNP

#ifdef MODULE
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/version.h>
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif
#endif
#if LINUX_VERSION_CODE > 131328
#define LINUX21X
#endif

#include <linux/param.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/ctype.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <linux/wait.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/utsname.h>

#include <linux/wrapper.h>

#include <linux/soundcard.h>

#define FALSE	0
#define TRUE	1



struct snd_wait {
	  int opts;
	};

extern int sound_alloc_dma(int chn, char *deviceID);
extern int sound_open_dma(int chn, char *deviceID);
extern void sound_free_dma(int chn);
extern void sound_close_dma(int chn);

#define RUNTIME_DMA_ALLOC

extern caddr_t sound_mem_blocks[1024];
extern int sound_nblocks;

#undef PSEUDO_DMA_AUTOINIT
#define ALLOW_BUFFER_MAPPING

