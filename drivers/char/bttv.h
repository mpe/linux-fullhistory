/* 
    bttv - Bt848 frame grabber driver

    Copyright (C) 1996,97 Ralph Metzler (rjkm@thp.uni-koeln.de)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _BTTV_H_
#define _BTTV_H_

#define BTTV_VERSION_CODE KERNEL_VERSION(0,7,21) 

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/videodev.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

#include "audiochip.h"
#include "bt848.h"

#define WAIT_QUEUE                 wait_queue_head_t

/* returns card type, 
   for possible values see lines below beginning with #define BTTV_UNKNOWN
   returns negative value if error ocurred 
*/
extern int bttv_get_id(unsigned int card);

/* sets GPOE register (BT848_GPIO_OUT_EN) to new value:
   data | (current_GPOE_value & ~mask)
   returns negative value if error ocurred
*/
extern int bttv_gpio_enable(unsigned int card,
			    unsigned long mask, unsigned long data);

/* fills data with GPDATA register contents
   returns negative value if error ocurred
*/
extern int bttv_read_gpio(unsigned int card, unsigned long *data);

/* sets GPDATA register to new value:
  (data & mask) | (current_GPDATA_value & ~mask)
  returns negative value if error ocurred 
*/
extern int bttv_write_gpio(unsigned int card, 
			    unsigned long mask, unsigned long data);

/* returns pointer to task queue which can be used as parameter to 
   interruptible_sleep_on
   in interrupt handler if BT848_INT_GPINT bit is set - this queue is activated
   (wake_up_interruptible) and following call to the function bttv_read_gpio 
   should return new value of GPDATA,
   returns NULL value if error ocurred or queue is not available
   WARNING: because there is no buffer for GPIO data, one MUST 
   process data ASAP
*/
extern WAIT_QUEUE* bttv_get_gpio_queue(unsigned int card);


#ifndef O_NONCAP  
#define O_NONCAP	O_TRUNC
#endif

#define MAX_GBUFFERS	64
#define RISCMEM_LEN	(32744*2)
#define VBI_MAXLINES    16
#define VBIBUF_SIZE     (2048*VBI_MAXLINES*2)

#define BTTV_MAX_FBUF	0x208000
#define I2C_CLIENTS_MAX 8

#ifdef __KERNEL__

struct bttv_window 
{
	int x, y;
	ushort width, height;
	ushort bpp, bpl;
	ushort swidth, sheight;
	unsigned long vidadr;
	ushort freq;
	int norm;
	int interlace;
	int color_fmt;
	ushort depth;
};

struct bttv_pll_info {
	unsigned int pll_ifreq;	   /* PLL input frequency 	 */
	unsigned int pll_ofreq;	   /* PLL output frequency       */
	unsigned int pll_crystal;  /* Crystal used for input     */
	unsigned int pll_current;  /* Currently programmed ofreq */
};

struct bttv_gbuf {
	int stat;
#define GBUFFER_UNUSED       0
#define GBUFFER_GRABBING     1
#define GBUFFER_DONE         2
#define GBUFFER_ERROR        3

	u16 width;
	u16 height;
	u16 fmt;
	
	u32 *risc;
	unsigned long ro;
	unsigned long re;
};

struct bttv
{
	struct video_device video_dev;
	struct video_device radio_dev;
	struct video_device vbi_dev;
	struct video_picture picture;		/* Current picture params */
	struct video_audio audio_dev;		/* Current audio params */

        struct semaphore lock;
	int user;
	int capuser;

	/* i2c */
	struct i2c_adapter         i2c_adap;
	struct i2c_algo_bit_data   i2c_algo;
	struct i2c_client          i2c_client;
	int                        i2c_state;
	struct i2c_client         *i2c_clients[I2C_CLIENTS_MAX];

        int tuner_type;
        int channel;
        
        unsigned int nr;
	unsigned short id;
	struct pci_dev *dev;
	unsigned int irq;          /* IRQ used by Bt848 card */
	unsigned char revision;
	unsigned long bt848_adr;      /* bus address of IO mem returned by PCI BIOS */
	unsigned char *bt848_mem;   /* pointer to mapped IO memory */
	unsigned long busriscmem; 
	u32 *riscmem;
  
	unsigned char *vbibuf;
	struct bttv_window win;
	int fb_color_ctl;
	int type;            /* card type  */
	int audio;           /* audio mode */
	int audio_chip;      /* set to one of the chips supported by bttv.c */
	int radio;

	u32 *risc_jmp;
	u32 *vbi_odd;
	u32 *vbi_even;
	u32 bus_vbi_even;
	u32 bus_vbi_odd;
        WAIT_QUEUE vbiq;
	WAIT_QUEUE capq;
	WAIT_QUEUE capqo;
	WAIT_QUEUE capqe;
	int vbip;

	u32 *risc_scr_odd;
	u32 *risc_scr_even;
	u32 risc_cap_odd;
	u32 risc_cap_even;
	int scr_on;
	int vbi_on;
	struct video_clip *cliprecs;

	struct bttv_gbuf *gbuf;
	int gqueue[MAX_GBUFFERS];
	int gq_in,gq_out,gq_grab;
        char *fbuffer;

	struct bttv_pll_info pll;
	unsigned int Fsc;
	unsigned int field;
	unsigned int last_field; /* number of last grabbed field */
	int i2c_command;
	int triton1;

	WAIT_QUEUE gpioq;
	int shutdown;
};
#endif

#if defined(__powerpc__) /* big-endian */
extern __inline__ void io_st_le32(volatile unsigned *addr, unsigned val)
{
        __asm__ __volatile__ ("stwbrx %1,0,%2" : \
                            "=m" (*addr) : "r" (val), "r" (addr));
      __asm__ __volatile__ ("eieio" : : : "memory");
}

#define btwrite(dat,adr)  io_st_le32((unsigned *)(btv->bt848_mem+(adr)),(dat))
#define btread(adr)       ld_le32((unsigned *)(btv->bt848_mem+(adr)))
#else
#define btwrite(dat,adr)    writel((dat), (char *) (btv->bt848_mem+(adr)))
#define btread(adr)         readl(btv->bt848_mem+(adr))
#endif

#define btand(dat,adr)      btwrite((dat) & btread(adr), adr)
#define btor(dat,adr)       btwrite((dat) | btread(adr), adr)
#define btaor(dat,mask,adr) btwrite((dat) | ((mask) & btread(adr)), adr)

/* bttv ioctls */

#define BTTV_READEE		_IOW('v',  BASE_VIDIOCPRIVATE+0, char [256])
#define BTTV_WRITEE		_IOR('v',  BASE_VIDIOCPRIVATE+1, char [256])
#define BTTV_FIELDNR		_IOR('v' , BASE_VIDIOCPRIVATE+2, unsigned int)
#define BTTV_PLLSET		_IOW('v' , BASE_VIDIOCPRIVATE+3, struct bttv_pll_info)
#define BTTV_BURST_ON      	_IOR('v' , BASE_VIDIOCPRIVATE+4, int)
#define BTTV_BURST_OFF     	_IOR('v' , BASE_VIDIOCPRIVATE+5, int)
#define BTTV_VERSION  	        _IOR('v' , BASE_VIDIOCPRIVATE+6, int)
#define BTTV_PICNR		_IOR('v' , BASE_VIDIOCPRIVATE+7, int)
#define BTTV_VBISIZE            _IOR('v' , BASE_VIDIOCPRIVATE+8, int)

#define BTTV_UNKNOWN       0x00
#define BTTV_MIRO          0x01
#define BTTV_HAUPPAUGE     0x02
#define BTTV_STB           0x03
#define BTTV_INTEL         0x04
#define BTTV_DIAMOND       0x05 
#define BTTV_AVERMEDIA     0x06 
#define BTTV_MATRIX_VISION 0x07 
#define BTTV_FLYVIDEO      0x08
#define BTTV_TURBOTV       0x09
#define BTTV_HAUPPAUGE878  0x0a
#define BTTV_MIROPRO       0x0b
#define BTTV_ADSTECH_TV    0x0c
#define BTTV_AVERMEDIA98   0x0d
#define BTTV_VHX           0x0e
#define BTTV_ZOLTRIX       0x0f
#define BTTV_PIXVIEWPLAYTV 0x10
#define BTTV_WINVIEW_601   0x11
#define BTTV_AVEC_INTERCAP 0x12
#define BTTV_LIFE_FLYKIT   0x13
#define BTTV_CEI_RAFFLES   0x14
#define BTTV_CONFERENCETV  0x15
#define BTTV_PHOEBE_TVMAS  0x16
#define BTTV_MODTEC_205    0x17
#define BTTV_MAGICTVIEW061 0x18
#define BTTV_VOBIS_BOOSTAR 0x19
#define BTTV_HAUPPAUG_WCAM 0x1a
#define BTTV_MAXI          0x1b
#define BTTV_TERRATV       0x1c
#define BTTV_PXC200        0x1d
#define BTTV_FLYVIDEO_98   0x1e
#define BTTV_IPROTV        0x1f
#define BTTV_INTEL_C_S_PCI 0x20
#define BTTV_TERRATVALUE   0x21
#define BTTV_WINFAST2000   0x22
#define BTTV_CHRONOS_VS2   0x23
#define BTTV_TYPHOON_TVIEW 0x24
#define BTTV_PXELVWPLTVPRO 0x25


#define AUDIO_TUNER        0x00
#define AUDIO_RADIO        0x01
#define AUDIO_EXTERN       0x02
#define AUDIO_INTERN       0x03
#define AUDIO_OFF          0x04 
#define AUDIO_ON           0x05
#define AUDIO_MUTE         0x80
#define AUDIO_UNMUTE       0x81

#define TDA9850            0x01
#define TDA9840            0x02
#define TDA8425            0x03
#define TEA6300            0x04

#define I2C_TSA5522        0xc2
#define I2C_TDA9840        0x84
#define I2C_TDA9850        0xb6
#define I2C_TDA8425        0x82
#define I2C_HAUPEE         0xa0
#define I2C_STBEE          0xae
#define I2C_VHX            0xc0
#define I2C_MSP3400        0x80
#define I2C_TEA6300        0x80
#define I2C_DPL3518	   0x84

#define TDA9840_SW         0x00
#define TDA9840_LVADJ      0x02
#define TDA9840_STADJ      0x03
#define TDA9840_TEST       0x04

#define PT2254_L_CHANEL 0x10
#define PT2254_R_CHANEL 0x08
#define PT2254_DBS_IN_2 0x400
#define PT2254_DBS_IN_10 0x20000
#define WINVIEW_PT2254_CLK  0x40
#define WINVIEW_PT2254_DATA 0x20
#define WINVIEW_PT2254_STROBE 0x80

#endif

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
