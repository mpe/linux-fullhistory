/*
 * $Id: cx88.h,v 1.40 2004/11/03 09:04:51 kraxel Exp $
 *
 * v4l2 device driver for cx2388x based TV cards
 *
 * (c) 2003,04 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/videodev.h>
#include <linux/kdev_t.h>

#include <media/tuner.h>
#include <media/audiochip.h>
#include <media/video-buf.h>
#include <media/video-buf-dvb.h>

#include "btcx-risc.h"
#include "cx88-reg.h"

#include <linux/version.h>
#define CX88_VERSION_CODE KERNEL_VERSION(0,0,4)

#ifndef TRUE
# define TRUE (1==1)
#endif
#ifndef FALSE
# define FALSE (1==0)
#endif
#define UNSET (-1U)

#define CX88_MAXBOARDS 8

/* ----------------------------------------------------------- */
/* defines and enums                                           */

#define V4L2_I2C_CLIENTS 1

#define FORMAT_FLAGS_PACKED       0x01
#define FORMAT_FLAGS_PLANAR       0x02

#define VBI_LINE_COUNT              17
#define VBI_LINE_LENGTH           2048

/* need "shadow" registers for some write-only ones ... */
#define SHADOW_AUD_VOL_CTL           1
#define SHADOW_AUD_BAL_CTL           2
#define SHADOW_MAX                   2

/* ----------------------------------------------------------- */
/* tv norms                                                    */

struct cx88_tvnorm {
	char                   *name;
	v4l2_std_id            id;
	u32                    cxiformat;
	u32                    cxoformat;
};

static unsigned int inline norm_maxw(struct cx88_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50) ? 768 : 640;
//	return (norm->id & V4L2_STD_625_50) ? 720 : 640;
}

static unsigned int inline norm_maxh(struct cx88_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50) ? 576 : 480;
}

/* ----------------------------------------------------------- */
/* static data                                                 */

struct cx8800_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
	int   flags;
	u32   cxformat;
};

struct cx88_ctrl {
	struct v4l2_queryctrl  v;
	u32                    off;
	u32                    reg;
	u32                    sreg;
	u32                    mask;
	u32                    shift;
};

/* ----------------------------------------------------------- */
/* SRAM memory management data (see cx88-core.c)               */

#define SRAM_CH21 0   /* video */
#define SRAM_CH22 1
#define SRAM_CH23 2
#define SRAM_CH24 3   /* vbi   */
#define SRAM_CH25 4   /* audio */
#define SRAM_CH26 5
#define SRAM_CH28 6   /* mpeg */
/* more */

struct sram_channel {
	char *name;
	u32  cmds_start;
	u32  ctrl_start;
	u32  cdt;
	u32  fifo_start;
	u32  fifo_size;
	u32  ptr1_reg;
	u32  ptr2_reg;
	u32  cnt1_reg;
	u32  cnt2_reg;
};
extern struct sram_channel cx88_sram_channels[];

/* ----------------------------------------------------------- */
/* card configuration                                          */

#define CX88_BOARD_NOAUTO               UNSET
#define CX88_BOARD_UNKNOWN                  0
#define CX88_BOARD_HAUPPAUGE                1
#define CX88_BOARD_GDI                      2
#define CX88_BOARD_PIXELVIEW                3
#define CX88_BOARD_ATI_WONDER_PRO           4
#define CX88_BOARD_WINFAST2000XP            5
#define CX88_BOARD_AVERTV_303               6
#define CX88_BOARD_MSI_TVANYWHERE_MASTER    7
#define CX88_BOARD_WINFAST_DV2000           8
#define CX88_BOARD_LEADTEK_PVR2000          9
#define CX88_BOARD_IODATA_GVVCP3PCI        10
#define CX88_BOARD_PROLINK_PLAYTVPVR       11
#define CX88_BOARD_ASUS_PVR_416            12
#define CX88_BOARD_MSI_TVANYWHERE          13
#define CX88_BOARD_KWORLD_DVB_T            14
#define CX88_BOARD_DVICO_FUSIONHDTV_DVB_T1 15
#define CX88_BOARD_KWORLD_LTV883           16
#define CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD 17
#define CX88_BOARD_HAUPPAUGE_DVB_T1        18
#define CX88_BOARD_CONEXANT_DVB_T1         19
#define CX88_BOARD_PROVIDEO_PV259          20
#define CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_PLUS 21

enum cx88_itype {
	CX88_VMUX_COMPOSITE1 = 1,
	CX88_VMUX_COMPOSITE2,
	CX88_VMUX_COMPOSITE3,
	CX88_VMUX_COMPOSITE4,
	CX88_VMUX_SVIDEO,
	CX88_VMUX_TELEVISION,
	CX88_VMUX_CABLE,
	CX88_VMUX_DVB,
	CX88_VMUX_DEBUG,
	CX88_RADIO,
};

struct cx88_input {
	enum cx88_itype type;
	unsigned int    vmux;
	u32             gpio0, gpio1, gpio2, gpio3;
};

struct cx88_board {
	char                    *name;
	unsigned int            tuner_type;
	int                     tda9887_conf;
	struct cx88_input       input[8];
	struct cx88_input       radio;
	int                     blackbird:1;
	int                     dvb:1;
};

struct cx88_subid {
	u16     subvendor;
	u16     subdevice;
	u32     card;
};

#define INPUT(nr) (&cx88_boards[core->board].input[nr])

/* ----------------------------------------------------------- */
/* device / file handle status                                 */

#define RESOURCE_OVERLAY       1
#define RESOURCE_VIDEO         2
#define RESOURCE_VBI           4

#define BUFFER_TIMEOUT     (HZ/2)  /* 0.5 seconds */
//#define BUFFER_TIMEOUT     (HZ*2)

/* buffer for one video frame */
struct cx88_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	/* cx88 specific */
	unsigned int           bpl;
	struct btcx_riscmem    risc;
	struct cx8800_fmt      *fmt;
	u32                    count;
};

struct cx88_dmaqueue {
	struct list_head       active;
	struct list_head       queued;
	struct timer_list      timeout;
	struct btcx_riscmem    stopper;
	u32                    count;
};

struct cx88_core {
	struct list_head           devlist;
	atomic_t                   refcount;

	/* board name */
	int                        nr;
	char                       name[32];

	/* pci stuff */
	int                        pci_bus;
	int                        pci_slot;
        u32                        __iomem *lmmio;
        u8                         __iomem *bmmio;
	u32                        shadow[SHADOW_MAX];

	/* i2c i/o */
	struct i2c_adapter         i2c_adap;
	struct i2c_algo_bit_data   i2c_algo;
	struct i2c_client          i2c_client;
	u32                        i2c_state, i2c_rc;

	/* config info -- analog */
	unsigned int               board;
	unsigned int               tuner_type;
	unsigned int               tda9887_conf;
	unsigned int               has_radio;

	/* config info -- dvb */
	unsigned int               pll_type;
	unsigned int               pll_addr;
	unsigned int               demod_addr;

	/* state info */
	struct task_struct         *kthread;
	struct cx88_tvnorm         *tvnorm;
	u32                        tvaudio;
	u32                        input;
	u32                        astat;
};

struct cx8800_dev;
struct cx8802_dev;

/* ----------------------------------------------------------- */
/* function 0: video stuff                                     */

struct cx8800_fh {
	struct cx8800_dev          *dev;
	enum v4l2_buf_type         type;
	int                        radio;
	unsigned int               resources;

	/* video overlay */
	struct v4l2_window         win;
	struct v4l2_clip           *clips;
	unsigned int               nclips;

	/* video capture */
	struct cx8800_fmt          *fmt;
	unsigned int               width,height;
	struct videobuf_queue      vidq;

	/* vbi capture */
	struct videobuf_queue      vbiq;
};

struct cx8800_suspend_state {
	int                        disabled;
};

struct cx8800_dev {
	struct cx88_core           *core;
	struct list_head           devlist;
        struct semaphore           lock;
       	spinlock_t                 slock;

	/* various device info */
	unsigned int               resources;
	struct video_device        *video_dev;
	struct video_device        *vbi_dev;
	struct video_device        *radio_dev;

	/* pci i/o */
	struct pci_dev             *pci;
	unsigned char              pci_rev,pci_lat;

#if 0
	/* video overlay */
	struct v4l2_framebuffer    fbuf;
	struct cx88_buffer         *screen;
#endif

	/* capture queues */
	struct cx88_dmaqueue       vidq;
	struct cx88_dmaqueue       vbiq;

	/* various v4l controls */
	u32                        freq;

	/* other global state info */
	struct cx8800_suspend_state state;
};

/* ----------------------------------------------------------- */
/* function 1: audio/alsa stuff                                */

struct cx8801_dev {
	struct cx88_core           *core;

	/* pci i/o */
	struct pci_dev             *pci;
	unsigned char              pci_rev,pci_lat;
};

/* ----------------------------------------------------------- */
/* function 2: mpeg stuff                                      */

struct cx8802_fh {
	struct cx8802_dev          *dev;
	struct videobuf_queue      mpegq;
};

struct cx8802_suspend_state {
	int                        disabled;
};

struct cx8802_dev {
	struct cx88_core           *core;
        struct semaphore           lock;
       	spinlock_t                 slock;

	/* pci i/o */
	struct pci_dev             *pci;
	unsigned char              pci_rev,pci_lat;

	/* dma queues */
	struct cx88_dmaqueue       mpegq;
	u32                        ts_packet_size;
	u32                        ts_packet_count;

	/* other global state info */
	struct cx8802_suspend_state state;

	/* for blackbird only */
	struct list_head           devlist;
	struct video_device        *mpeg_dev;
	u32                        mailbox;

	/* for dvb only */
	struct videobuf_dvb        dvb;
	void*                      fe_handle;
	int                        (*fe_release)(void *handle);
};

/* ----------------------------------------------------------- */

#define cx_read(reg)             readl(core->lmmio + ((reg)>>2))
#define cx_write(reg,value)      writel((value), core->lmmio + ((reg)>>2))
#define cx_writeb(reg,value)     writeb((value), core->bmmio + (reg))

#define cx_andor(reg,mask,value) \
  writel((readl(core->lmmio+((reg)>>2)) & ~(mask)) |\
  ((value) & (mask)), core->lmmio+((reg)>>2))
#define cx_set(reg,bit)          cx_andor((reg),(bit),(bit))
#define cx_clear(reg,bit)        cx_andor((reg),(bit),0)

#define cx_wait(d) { if (need_resched()) schedule(); else udelay(d); }

/* shadow registers */
#define cx_sread(sreg)		    (core->shadow[sreg])
#define cx_swrite(sreg,reg,value) \
  (core->shadow[sreg] = value, \
   writel(core->shadow[sreg], core->lmmio + ((reg)>>2)))
#define cx_sandor(sreg,reg,mask,value) \
  (core->shadow[sreg] = (core->shadow[sreg] & ~(mask)) | ((value) & (mask)), \
   writel(core->shadow[sreg], core->lmmio + ((reg)>>2)))

/* ----------------------------------------------------------- */
/* cx88-core.c                                                 */

extern char *cx88_pci_irqs[32];
extern char *cx88_vid_irqs[32];
extern char *cx88_mpeg_irqs[32];
extern void cx88_print_irqbits(char *name, char *tag, char **strings,
			       u32 bits, u32 mask);
extern void cx88_print_ioctl(char *name, unsigned int cmd);

extern void cx88_irq(struct cx88_core *core, u32 status, u32 mask);
extern void cx88_wakeup(struct cx88_core *core,
			struct cx88_dmaqueue *q, u32 count);
extern void cx88_shutdown(struct cx88_core *core);
extern int cx88_reset(struct cx88_core *core);

extern int
cx88_risc_buffer(struct pci_dev *pci, struct btcx_riscmem *risc,
		 struct scatterlist *sglist,
		 unsigned int top_offset, unsigned int bottom_offset,
		 unsigned int bpl, unsigned int padding, unsigned int lines);
extern int
cx88_risc_databuffer(struct pci_dev *pci, struct btcx_riscmem *risc,
		     struct scatterlist *sglist, unsigned int bpl,
		     unsigned int lines);
extern int
cx88_risc_stopper(struct pci_dev *pci, struct btcx_riscmem *risc,
		  u32 reg, u32 mask, u32 value);
extern void
cx88_free_buffer(struct pci_dev *pci, struct cx88_buffer *buf);

extern void cx88_risc_disasm(struct cx88_core *core,
			     struct btcx_riscmem *risc);
extern int cx88_sram_channel_setup(struct cx88_core *core,
				   struct sram_channel *ch,
				   unsigned int bpl, u32 risc);
extern void cx88_sram_channel_dump(struct cx88_core *core,
				   struct sram_channel *ch);

extern int cx88_set_scale(struct cx88_core *core, unsigned int width,
			  unsigned int height, enum v4l2_field field);
extern int cx88_set_tvnorm(struct cx88_core *core, struct cx88_tvnorm *norm);

extern struct video_device *cx88_vdev_init(struct cx88_core *core,
					   struct pci_dev *pci,
					   struct video_device *template,
					   char *type);
extern struct cx88_core* cx88_core_get(struct pci_dev *pci);
extern void cx88_core_put(struct cx88_core *core,
			  struct pci_dev *pci);

/* ----------------------------------------------------------- */
/* cx88-vbi.c                                                  */

void cx8800_vbi_fmt(struct cx8800_dev *dev, struct v4l2_format *f);
int cx8800_start_vbi_dma(struct cx8800_dev    *dev,
			 struct cx88_dmaqueue *q,
			 struct cx88_buffer   *buf);
int cx8800_stop_vbi_dma(struct cx8800_dev *dev);
int cx8800_restart_vbi_queue(struct cx8800_dev    *dev,
			     struct cx88_dmaqueue *q);
void cx8800_vbi_timeout(unsigned long data);

extern struct videobuf_queue_ops cx8800_vbi_qops;

/* ----------------------------------------------------------- */
/* cx88-i2c.c                                                  */

extern int cx88_i2c_init(struct cx88_core *core, struct pci_dev *pci);
extern void cx88_call_i2c_clients(struct cx88_core *core,
				  unsigned int cmd, void *arg);


/* ----------------------------------------------------------- */
/* cx88-cards.c                                                */

extern struct cx88_board cx88_boards[];
extern const unsigned int cx88_bcount;

extern struct cx88_subid cx88_subids[];
extern const unsigned int cx88_idcount;

extern void cx88_card_list(struct cx88_core *core, struct pci_dev *pci);
extern void cx88_card_setup(struct cx88_core *core);

/* ----------------------------------------------------------- */
/* cx88-tvaudio.c                                              */

#define WW_NONE		 1
#define WW_BTSC		 2
#define WW_NICAM_I	 3
#define WW_NICAM_BGDKL	 4
#define WW_A1		 5
#define WW_A2_BG	 6
#define WW_A2_DK	 7
#define WW_A2_M		 8
#define WW_EIAJ		 9
#define WW_SYSTEM_L_AM	10
#define WW_I2SPT	11
#define WW_FM		12

void cx88_set_tvaudio(struct cx88_core *core);
void cx88_get_stereo(struct cx88_core *core, struct v4l2_tuner *t);
void cx88_set_stereo(struct cx88_core *core, u32 mode);
int cx88_audio_thread(void *data);

/* ----------------------------------------------------------- */
/* cx88-mpeg.c                                                 */

int cx8802_buf_prepare(struct cx8802_dev *dev, struct cx88_buffer *buf);
void cx8802_buf_queue(struct cx8802_dev *dev, struct cx88_buffer *buf);
void cx8802_cancel_buffers(struct cx8802_dev *dev);

int cx8802_init_common(struct cx8802_dev *dev);
void cx8802_fini_common(struct cx8802_dev *dev);

int cx8802_suspend_common(struct pci_dev *pci_dev, u32 state);
int cx8802_resume_common(struct pci_dev *pci_dev);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
