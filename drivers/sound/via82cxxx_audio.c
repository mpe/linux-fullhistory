/*
 * Support for VIA 82Cxxx Audio Codecs
 * Copyright 1999,2000 Jeff Garzik <jgarzik@mandrakesoft.com>
 *
 * Distributed under the GNU GENERAL PUBLIC LICENSE (GPL) Version 2.
 * See the "COPYING" file distributed with this software for more info.
 *
 * For a list of known bugs (errata) and documentation,
 * see via82cxxx.txt in linux/Documentation/sound.
 *
 */
 

#define VIA_VERSION	"1.1.6"


#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/sound.h>
#include <linux/poll.h>
#include <linux/soundcard.h>
#include <linux/ac97_codec.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>

/* much better to duplicate this value than include
 * drivers/sound/sound_config.h just for this definition */
#define SND_DEV_DSP16 5 


#undef VIA_DEBUG	/* define to enable debugging output and checks */
#ifdef VIA_DEBUG
/* note: prints function name for you */
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define VIA_NDEBUG	/* define to disable lightweight runtime checks */
#ifdef VIA_NDEBUG
#define assert(expr)
#else
#define assert(expr) \
        if(!(expr)) {					\
        printk( "Assertion failed! %s,%s,%s,line=%d\n",	\
        #expr,__FILE__,__FUNCTION__,__LINE__);		\
        }
#endif

/* user switch: undefine to exclude /proc data */
#define VIA_PROC_FS 1

/* don't mess with this */
#ifndef CONFIG_PROC_FS
#undef VIA_PROC_FS
#endif

#define arraysize(x)            (sizeof(x)/sizeof(*(x)))

#define MAX_CARDS	1

#define	LINE_SIZE	10

#define VIA_CARD_NAME	"VIA 82Cxxx Audio driver " VIA_VERSION
#define VIA_MODULE_NAME "via82cxxx"
#define PFX		VIA_MODULE_NAME ": "

#define VIA_COUNTER_LIMIT	100000

/* size of DMA buffers */
#define VIA_DMA_BUFFERS		16
#define VIA_DMA_BUF_SIZE	PAGE_SIZE

/* 82C686 function 5 (audio codec) PCI configuration registers */
#define VIA_ACLINK_CTRL		0x41
#define VIA_FUNC_ENABLE		0x42
#define VIA_PNP_CONTROL		0x43
#define VIA_FM_NMI_CTRL		0x48

/*
 * controller base 0 (scatter-gather) registers
 *
 * NOTE: Via datasheet lists first channel as "read"
 * channel and second channel as "write" channel.
 * I changed the naming of the constants to be more
 * clear than I felt the datasheet to be.
 */

#define VIA_BASE0_PCM_OUT_CHAN	0x00 /* output PCM to user */
#define VIA_BASE0_PCM_OUT_CHAN_STATUS 0x00
#define VIA_BASE0_PCM_OUT_CHAN_CTRL	0x01
#define VIA_BASE0_PCM_OUT_CHAN_TYPE	0x02
#define VIA_BASE0_PCM_OUT_BLOCK_COUNT	0x0C

#define VIA_BASE0_PCM_IN_CHAN		0x10 /* input PCM from user */
#define VIA_BASE0_PCM_IN_CHAN_STATUS	0x10
#define VIA_BASE0_PCM_IN_CHAN_CTRL	0x11
#define VIA_BASE0_PCM_IN_CHAN_TYPE	0x12

/* offsets from base */
#define VIA_PCM_STATUS			0x00
#define VIA_PCM_CONTROL			0x01
#define VIA_PCM_TYPE			0x02
#define VIA_PCM_TABLE_ADDR		0x04

/* XXX unused DMA channel for FM PCM data */
#define VIA_BASE0_FM_OUT_CHAN		0x20
#define VIA_BASE0_FM_OUT_CHAN_STATUS	0x20
#define VIA_BASE0_FM_OUT_CHAN_CTRL	0x21
#define VIA_BASE0_FM_OUT_CHAN_TYPE	0x22

#define VIA_BASE0_AC97_CTRL		0x80
#define VIA_BASE0_SGD_STATUS_SHADOW	0x84
#define VIA_BASE0_GPI_INT_ENABLE	0x8C

/* VIA_BASE0_AUDIO_xxx_CHAN_TYPE bits */
#define VIA_IRQ_ON_FLAG			(1<<0)	/* int on each flagged scatter block */
#define VIA_IRQ_ON_EOL			(1<<1)	/* int at end of scatter list */
#define VIA_INT_SEL_PCI_LAST_LINE_READ	(0)	/* int at PCI read of last line */
#define VIA_INT_SEL_LAST_SAMPLE_SENT	(1<<2)	/* int at last sample sent */
#define VIA_INT_SEL_ONE_LINE_LEFT	(1<<3)	/* int at less than one line to send */
#define VIA_PCM_FMT_STEREO		(1<<4)	/* PCM stereo format (bit clear == mono) */
#define VIA_PCM_FMT_16BIT		(1<<5)	/* PCM 16-bit format (bit clear == 8-bit) */
#define VIA_PCM_FIFO			(1<<6)	/* enable FIFO?  documented as "reserved" */
#define VIA_RESTART_SGD_ON_EOL		(1<<7)	/* restart scatter-gather at EOL */
#define VIA_PCM_FMT_MASK		(VIA_PCM_FMT_STEREO|VIA_PCM_FMT_16BIT)
#define VIA_CHAN_TYPE_MASK		(VIA_RESTART_SGD_ON_EOL | \
					 VIA_PCM_FIFO | \
					 VIA_IRQ_ON_FLAG | \
					 VIA_IRQ_ON_EOL)
#define VIA_CHAN_TYPE_INT_SELECT	(VIA_INT_SEL_LAST_SAMPLE_SENT)

/* PCI configuration register bits and masks */
#define VIA_CR40_AC97_READY	0x01
#define VIA_CR40_AC97_LOW_POWER	0x02
#define VIA_CR40_SECONDARY_READY 0x04

#define VIA_CR41_AC97_ENABLE	0x80 /* enable AC97 codec */
#define VIA_CR41_AC97_RESET	0x40 /* clear bit to reset AC97 */
#define VIA_CR41_AC97_WAKEUP	0x20 /* wake up from power-down mode */
#define VIA_CR41_AC97_SDO	0x10 /* force Serial Data Out (SDO) high */
#define VIA_CR41_VRA		0x08 /* enable variable sample rate */
#define VIA_CR41_PCM_ENABLE	0x04 /* AC Link SGD Read Channel PCM Data Output */
#define VIA_CR41_FM_PCM_ENABLE	0x02 /* AC Link FM Channel PCM Data Out */
#define VIA_CR41_SB_PCM_ENABLE	0x01 /* AC Link SB PCM Data Output */
#define VIA_CR41_BOOT_MASK	(VIA_CR41_AC97_ENABLE | \
				 VIA_CR41_AC97_WAKEUP | \
				 VIA_CR41_AC97_SDO)
#define VIA_CR41_RUN_MASK	(VIA_CR41_AC97_ENABLE | \
				 VIA_CR41_AC97_RESET | \
				 VIA_CR41_VRA | \
				 VIA_CR41_PCM_ENABLE)

#define VIA_CR42_SB_ENABLE	0x01
#define VIA_CR42_MIDI_ENABLE	0x02
#define VIA_CR42_FM_ENABLE	0x04
#define VIA_CR42_GAME_ENABLE	0x08

#define VIA_CR44_SECOND_CODEC_SUPPORT	(1 << 6)
#define VIA_CR44_AC_LINK_ACCESS		(1 << 7)

#define VIA_CR48_FM_TRAP_TO_NMI		(1 << 2)

/* controller base 0 register bitmasks */
#define VIA_INT_DISABLE_MASK		(~(0x01|0x02))
#define VIA_SGD_STOPPED			(1 << 2)
#define VIA_SGD_ACTIVE			(1 << 7)
#define VIA_SGD_TERMINATE		(1 << 6)
#define VIA_SGD_FLAG			(1 << 0)
#define VIA_SGD_EOL			(1 << 1)
#define VIA_SGD_START			(1 << 7)

#define VIA_CR80_FIRST_CODEC		0
#define VIA_CR80_SECOND_CODEC		(1 << 30)
#define VIA_CR80_FIRST_CODEC_VALID	(1 << 25)
#define VIA_CR80_VALID			(1 << 25)
#define VIA_CR80_SECOND_CODEC_VALID	(1 << 27)
#define VIA_CR80_BUSY			(1 << 24)
#define VIA_CR83_BUSY			(1)
#define VIA_CR83_FIRST_CODEC_VALID	(1 << 1)
#define VIA_CR80_READ			(1 << 23)
#define VIA_CR80_WRITE_MODE		0
#define VIA_CR80_REG_IDX(idx)		((((idx) & 0xFF) >> 1) << 16)

/* h r puff n stuff */
#define VIA_FMT_STEREO	0x01
#define VIA_FMT_16BIT	0x02
#define VIA_FMT_MASK	0x03
#define VIA_DAC_SHIFT	0   
#define VIA_ADC_SHIFT	4

/* undocumented(?) values for setting rate, from Via's source */
#define VIA_SET_RATE_IN		0x00320000 /* set input rate */
#define VIA_SET_RATE_OUT	0x002c0000 /* set output rate */


/* scatter-gather DMA table entry, exactly as passed to hardware */
struct via_sgd_table {
	u32 addr;
	u32 count;	/* includes additional bits also */
};
#define VIA_EOL (1 << 31)
#define VIA_FLAG (1 << 30)


enum via_channel_states {
	sgd_stopped = 0,
	sgd_in_progress = 1,
};


struct via_sgd_data {
	dma_addr_t handle;
	volatile void *cpuaddr;
};


struct via_channel {
	unsigned rate;		/* sample rate */

	u8 pcm_fmt;		/* VIA_PCM_FMT_xxx */
	
	atomic_t state;
	atomic_t buf_in_use;
	atomic_t next_buf;
	
	volatile struct via_sgd_table *sgtable;
	dma_addr_t sgt_handle;
	
	struct via_sgd_data sgbuf [VIA_DMA_BUFFERS];
	
	wait_queue_head_t wait;
	
	long iobase;
};


/* data stored for each chip */
struct via_info {
	struct pci_dev *pdev;
	long baseaddr;
	
	struct ac97_codec ac97;
	spinlock_t lock;
	int card_num;		/* unique card number, from 0 */

	int dev_dsp;		/* /dev/dsp index from register_sound_dsp() */
	
	unsigned rev_h : 1;

	wait_queue_head_t open_wait;
	int open_mode;
	
	struct via_channel ch_in;
	struct via_channel ch_out;
};


/* number of cards, used for assigning unique numbers to cards */
static unsigned via_num_cards = 0;


/****************************************************************
 *
 * prototypes
 *
 *
 */

static int via_init_one (struct pci_dev *dev, const struct pci_device_id *id);
static void via_remove_one (struct pci_dev *pdev);

static ssize_t via_dsp_read(struct file *file, char *buffer, size_t count, loff_t *ppos);
static ssize_t via_dsp_write(struct file *file, const char *buffer, size_t count, loff_t *ppos);
static unsigned int via_dsp_poll(struct file *file, struct poll_table_struct *wait);
static int via_dsp_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
static int via_dsp_open (struct inode *inode, struct file *file);
static int via_dsp_release(struct inode *inode, struct file *file);

static u16 via_ac97_read_reg (struct ac97_codec *codec, u8 reg);
static void via_ac97_write_reg (struct ac97_codec *codec, u8 reg, u16 value);
static u8 via_ac97_wait_idle (struct via_info *card);

static void via_chan_free (struct via_info *card, struct via_channel *chan);
static void via_chan_clear (struct via_channel *chan);
static void via_chan_pcm_fmt (struct via_info *card,
			      struct via_channel *chan, int reset);


/****************************************************************
 *
 * Various data the driver needs
 *
 *
 */


static struct pci_device_id via_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C686_5, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci,via_pci_tbl);


static struct pci_driver via_driver = {
	name:		VIA_MODULE_NAME,
	id_table:	via_pci_tbl,
	probe:		via_init_one,
	remove:		via_remove_one,
};


/****************************************************************
 *
 * Low-level base 0 register read/write helpers
 *
 *
 */

static inline void via_chan_stop (int iobase)
{
	if (inb (iobase + VIA_PCM_STATUS) & VIA_SGD_ACTIVE)
		outb (VIA_SGD_TERMINATE, iobase + VIA_PCM_CONTROL);
}

static inline void via_chan_status_clear (int iobase)
{
	u8 tmp = inb (iobase + VIA_PCM_STATUS);
	
	if (tmp != 0)
		outb (tmp, iobase + VIA_PCM_STATUS);
}

static inline void sg_begin (struct via_channel *chan)
{
	outb (VIA_SGD_START, chan->iobase + VIA_PCM_CONTROL);
}


static inline int via_chan_bufs_in_use (struct via_channel *chan)
{
	return atomic_read(&chan->next_buf) -
	       atomic_read(&chan->buf_in_use);
}


static inline int via_chan_full (struct via_channel *chan)
{
	return (via_chan_bufs_in_use (chan) == VIA_DMA_BUFFERS);
}


static inline int via_chan_empty (struct via_channel *chan)
{
	return (atomic_read(&chan->next_buf) ==
	        atomic_read(&chan->buf_in_use));
}


/****************************************************************
 *
 * Miscellaneous debris
 *
 *
 */

static void via_stop_everything (struct via_info *card)
{
	DPRINTK ("ENTER\n");

	assert (card != NULL);

	/*
	 * terminate any existing operations on audio read/write channels
	 */
	via_chan_stop (card->baseaddr + VIA_BASE0_PCM_OUT_CHAN);
	via_chan_stop (card->baseaddr + VIA_BASE0_PCM_IN_CHAN);
	via_chan_stop (card->baseaddr + VIA_BASE0_FM_OUT_CHAN);

	/*
	 * clear any existing stops / flags (sanity check mainly)
	 */
	via_chan_status_clear (card->baseaddr + VIA_BASE0_PCM_OUT_CHAN);
	via_chan_status_clear (card->baseaddr + VIA_BASE0_PCM_IN_CHAN);
	via_chan_status_clear (card->baseaddr + VIA_BASE0_FM_OUT_CHAN);
	
	/*
	 * clear any enabled interrupt bits, reset to 8-bit mono PCM mode
	 */
	outb (0, card->baseaddr + VIA_BASE0_PCM_OUT_CHAN);
	outb (0, card->baseaddr + VIA_BASE0_PCM_IN_CHAN);
	outb (0, card->baseaddr + VIA_BASE0_FM_OUT_CHAN);
	DPRINTK ("EXIT\n");
}


static int via_set_rate (struct via_info *card, unsigned rate, int inhale_deeply)
{

	DPRINTK ("ENTER, rate = %d, inhale = %s\n",
		 rate, inhale_deeply ? "yes" : "no");

	if (rate > 48000)		rate = 48000;
	if (rate < 4000) 		rate = 4000;
	
	via_ac97_write_reg (&card->ac97, AC97_POWER_CONTROL,
		(via_ac97_read_reg (&card->ac97, AC97_POWER_CONTROL) & ~0x0200) |
		0x0200);
	via_ac97_write_reg (&card->ac97, AC97_PCM_FRONT_DAC_RATE, rate);
	via_ac97_write_reg (&card->ac97, AC97_POWER_CONTROL,
		via_ac97_read_reg (&card->ac97, AC97_POWER_CONTROL) & ~0x0200);

	DPRINTK ("EXIT, returning 0\n");
	return rate;
}


static inline int via_set_adc_rate (struct via_info *card, int rate)
{
	return via_set_rate (card, rate, 1);
}


static inline int via_set_dac_rate (struct via_info *card, int rate)
{
	return via_set_rate (card, rate, 0);
}


/****************************************************************
 *
 * Channel-specific operations
 *
 *
 */
 
static int via_chan_init (struct via_info *card, 
			  struct via_channel *chan, long chan_ofs)
{
	int i;
	unsigned long flags;
	
	DPRINTK ("ENTER\n");

	memset (chan, 0, sizeof (*chan));

	/* alloc DMA-able memory for scatter-gather table */
	chan->sgtable = pci_alloc_consistent (card->pdev,
		(sizeof (struct via_sgd_table) * VIA_DMA_BUFFERS),
		&chan->sgt_handle);
	if (!chan->sgtable) {
		printk (KERN_ERR PFX "DMA table alloc fail, aborting\n");
		DPRINTK ("EXIT\n");
		return -ENOMEM;
	}
	
	memset ((void*)chan->sgtable, 0, 
		(sizeof (struct via_sgd_table) * VIA_DMA_BUFFERS));

	/* alloc DMA-able memory for scatter-gather buffers */
	for (i = 0; i < VIA_DMA_BUFFERS; i++) {
		chan->sgbuf[i].cpuaddr =
			pci_alloc_consistent (card->pdev, VIA_DMA_BUF_SIZE,
					      &chan->sgbuf[i].handle);

		if (!chan->sgbuf[i].cpuaddr)
			goto err_out_nomem;

		if (i < (VIA_DMA_BUFFERS - 1))
			chan->sgtable[i].count = cpu_to_le32 (VIA_DMA_BUF_SIZE | VIA_FLAG);
		else
			chan->sgtable[i].count = cpu_to_le32 (VIA_DMA_BUF_SIZE | VIA_EOL);
		chan->sgtable[i].addr = cpu_to_le32 (chan->sgbuf[i].handle);

#ifndef VIA_NDEBUG
		memset (chan->sgbuf[i].cpuaddr, 0xBC, VIA_DMA_BUF_SIZE);
#endif

#if 1
		DPRINTK ("dmabuf #%d (h=%lx, 32(h)=%lx, v2p=%lx, a=%p)\n",
			 i, (long)chan->sgbuf[i].handle,
			 (long)chan->sgtable[i].addr,
			 virt_to_phys(chan->sgbuf[i].cpuaddr),
			 chan->sgbuf[i].cpuaddr);
#endif
	}

	init_waitqueue_head (&chan->wait);

	chan->pcm_fmt = VIA_PCM_FMT_MASK;
	chan->iobase = card->baseaddr + chan_ofs;
	
	spin_lock_irqsave (&card->lock, flags);

	/* stop any existing channel output */
	via_chan_clear (chan);
	via_chan_status_clear (chan->iobase);
	via_chan_pcm_fmt (card, chan, 1);

	spin_unlock_irqrestore (&card->lock, flags);

	/* set location of DMA-able scatter-gather info table */
	DPRINTK("outl (0x%X, 0x%04lX)\n",
		cpu_to_le32 (chan->sgt_handle),
		card->baseaddr + chan_ofs + VIA_PCM_TABLE_ADDR);

	via_ac97_wait_idle (card);
	outl (cpu_to_le32 (chan->sgt_handle),
	      card->baseaddr + chan_ofs + VIA_PCM_TABLE_ADDR);
	udelay (20);
	via_ac97_wait_idle (card);
	
	DPRINTK("inl (0x%lX) = %x\n",
		chan->iobase + VIA_PCM_TABLE_ADDR,
		inl(chan->iobase + VIA_PCM_TABLE_ADDR));

	DPRINTK ("EXIT\n");
	return 0;

err_out_nomem:
	printk (KERN_ERR PFX "DMA buffer alloc fail, aborting\n");
	via_chan_free (card, chan);
	DPRINTK ("EXIT\n");
	return -ENOMEM;
}


static void via_chan_free (struct via_info *card, struct via_channel *chan)
{
	int i;
	unsigned long flags;
	
	DPRINTK ("ENTER\n");
	
	synchronize_irq();

	spin_lock_irqsave (&card->lock, flags);

	/* stop any existing channel output */
	via_chan_stop (chan->iobase);
	via_chan_status_clear (chan->iobase);
	via_chan_pcm_fmt (card, chan, 1);

	spin_unlock_irqrestore (&card->lock, flags);

	/* zero location of DMA-able scatter-gather info table */
	via_ac97_wait_idle(card);
	outl (0, chan->iobase + VIA_PCM_TABLE_ADDR);

	for (i = 0; i < VIA_DMA_BUFFERS; i++)
		if (chan->sgbuf[i].cpuaddr) {
			pci_free_consistent (card->pdev, VIA_DMA_BUF_SIZE,
					     (void*)chan->sgbuf[i].cpuaddr,
					     chan->sgbuf[i].handle);
			chan->sgbuf[i].cpuaddr = NULL;
			chan->sgbuf[i].handle = 0;
		}

	if (chan->sgtable) {
		pci_free_consistent (card->pdev, 
			(sizeof (struct via_sgd_table) * VIA_DMA_BUFFERS),
			(void*)chan->sgtable, chan->sgt_handle);
		chan->sgtable = NULL;
	}

	DPRINTK ("EXIT\n");
}


static void via_chan_pcm_fmt (struct via_info *card,
			      struct via_channel *chan, int reset)
{
	DPRINTK ("ENTER, pcm_fmt=0x%02X, reset=%s\n",
		 chan->pcm_fmt, reset ? "yes" : "no");

	assert (card != NULL);
	assert (chan != NULL);

	if (reset)
		/* reset to 8-bit mono mode */
		chan->pcm_fmt = 0;
	
	/* enable interrupts on FLAG and EOL */
	chan->pcm_fmt |= VIA_CHAN_TYPE_MASK;
	
	/* set interrupt select bits where applicable (PCM & FM out channels) */
	if (chan == &card->ch_out)
		chan->pcm_fmt |= VIA_CHAN_TYPE_INT_SELECT;

	outb (chan->pcm_fmt, chan->iobase + 2);

	DPRINTK ("EXIT, pcm_fmt = 0x%02X, reg = 0x%02X\n",
		 chan->pcm_fmt,
		 inb (chan->iobase + 2));
}


static void via_chan_clear (struct via_channel *chan)
{
	via_chan_stop (chan->iobase);
	atomic_set (&chan->state, sgd_stopped);
	atomic_set (&chan->buf_in_use, 0);
	atomic_set (&chan->next_buf, 0);
}


static int via_chan_set_speed (struct via_info *card,
			       struct via_channel *chan, int val)
{
	DPRINTK ("ENTER, requested rate = %d\n", val);

	via_chan_clear (chan);

	val = via_set_rate (card, val, chan == &card->ch_in);	
	
	DPRINTK ("EXIT, returning %d\n", val);
	return val;
}


static int via_chan_set_fmt (struct via_info *card,
			     struct via_channel *chan, int val)
{
	DPRINTK ("ENTER, val=%s\n",
		 val == AFMT_U8 ? "AFMT_U8" :
	 	 val == AFMT_S16_LE ? "AFMT_S16_LE" :
		 "unknown");

	via_chan_clear (chan);
	
	switch (val) {
	case AFMT_U8:
		chan->pcm_fmt &= ~VIA_PCM_FMT_16BIT;
		via_chan_pcm_fmt (card, chan, 0);
		break;
	case AFMT_S16_LE:
		chan->pcm_fmt |= VIA_PCM_FMT_16BIT;
		via_chan_pcm_fmt (card, chan, 0);
		break;
	default:
		printk (KERN_WARNING PFX "unknown AFMT\n");
		val = -EINVAL;
		break;
	}
	
	DPRINTK ("EXIT, returning %d\n", val);
	return val;
}


static int via_chan_set_stereo (struct via_info *card,
			        struct via_channel *chan, int val)
{
	DPRINTK ("ENTER, channels = %d\n", val);

	via_chan_clear (chan);
	
	switch (val) {

	/* mono */
	case 1:
		chan->pcm_fmt &= ~VIA_PCM_FMT_STEREO;
		via_chan_pcm_fmt (card, chan, 0);
		break;

	/* stereo */
	case 2:
		chan->pcm_fmt |= VIA_PCM_FMT_STEREO;
		via_chan_pcm_fmt (card, chan, 0);
		break;

	/* unknown */
	default:
		printk (KERN_WARNING PFX "unknown number of channels\n");
		val = -EINVAL;
		break;
	}
	
	DPRINTK ("EXIT, returning %d\n", val);
	return val;
}


#if 0
static void via_chan_dump_bufs (struct via_channel *chan)
{
	int i;
	
	for (i = 0; i < VIA_DMA_BUFFERS; i++) {
		DPRINTK ("#%02d: addr=%x, count=%u, flag=%d, eol=%d\n",
			 i, chan->sgtable[i].addr,
			 chan->sgtable[i].count & 0x00FFFFFF,
			 chan->sgtable[i].count & VIA_FLAG ? 1 : 0,
			 chan->sgtable[i].count & VIA_EOL ? 1 : 0);
	}
	DPRINTK ("buf_in_use = %d, nextbuf = %d\n",
		 atomic_read (&chan->buf_in_use),
		 atomic_read (&chan->next_buf));
}
#endif


/****************************************************************
 *
 * Interface to ac97-codec module
 *
 *
 */
 
static u8 via_ac97_wait_idle (struct via_info *card)
{
	u8 tmp8;
	int counter = VIA_COUNTER_LIMIT;
	
	DPRINTK ("ENTER/EXIT\n");

	assert (card != NULL);
	assert (card->pdev != NULL);
	
	do {
		if (current->need_resched)
			schedule ();
		else
			udelay (10);

		spin_lock_irq (&card->lock);
		tmp8 = inb (card->baseaddr + 0x83);
		spin_unlock_irq (&card->lock);
	} while ((tmp8 & VIA_CR83_BUSY) && (counter-- > 0));

	return tmp8;
}


static u16 via_ac97_read_reg (struct ac97_codec *codec, u8 reg)
{
	u32 data;
	struct via_info *card;
	int counter;
	
	DPRINTK ("ENTER\n");

	assert (codec != NULL);
	assert (codec->private_data != NULL);

	card = codec->private_data;

	data = (reg << 16) | VIA_CR80_READ;

	outl (data, card->baseaddr + VIA_BASE0_AC97_CTRL);
	udelay (20);

	for (counter = VIA_COUNTER_LIMIT; counter > 0; counter--) {
		if (inl (card->baseaddr + 0x80) & VIA_CR80_VALID)
			goto out;
		udelay(10);
		if (current->need_resched)
			schedule ();
	}

	printk (KERN_WARNING PFX "timeout while reading AC97 codec\n");
	goto err_out;

out:
	data = inl (card->baseaddr + 0x80);
	outb (0x02, card->baseaddr + 0x83);

	if (((data & 0x007F0000) >> 16) == reg) {
		DPRINTK ("EXIT, success, data=0x%x, retval=0x%x\n",
			 data, data & 0x0000FFFF);
		return data & 0x0000FFFF;
	}

	DPRINTK ("WARNING: not our index: reg=0x%x, newreg=0x%x\n",
		 reg, ((data & 0x007F0000) >> 16));

err_out:
	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static void via_ac97_write_reg (struct ac97_codec *codec, u8 reg, u16 value)
{
	u32 data;
	struct via_info *card;
	int counter;
	
	DPRINTK ("ENTER\n");

	assert (codec != NULL);
	assert (codec->private_data != NULL);

	card = codec->private_data;

	data = (reg << 16) + value;
	outl (data, card->baseaddr + VIA_BASE0_AC97_CTRL);
	udelay (20);

	for (counter = VIA_COUNTER_LIMIT; counter > 0; counter--) {
		if ((inb (card->baseaddr + 0x83) & VIA_CR83_BUSY) == 0)
			goto out;
		udelay(10);
		if (current->need_resched)
			schedule ();
	}
	
	printk (KERN_WARNING PFX "timeout after AC97 codec write\n");

out:
	DPRINTK ("EXIT\n");
}


static int via_mixer_open (struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct via_info *card;
	struct pci_dev *pdev;
	struct pci_driver *drvr;
	
	DPRINTK ("ENTER\n");

	MOD_INC_USE_COUNT;

	pci_for_each_dev(pdev) {
		drvr = pci_dev_driver (pdev);
		if (drvr == &via_driver) {
			assert (pdev->driver_data != NULL);
			
			card = pdev->driver_data;
			if (card->ac97.dev_mixer == minor)
				goto match;
		}
	}

	DPRINTK ("EXIT, returning -ENODEV\n");
	MOD_DEC_USE_COUNT;
	return -ENODEV;

match:
	file->private_data = &card->ac97;

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static int via_mixer_release (struct inode *inode, struct file *file)
{
	DPRINTK ("ENTER\n");

	MOD_DEC_USE_COUNT;

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static int via_mixer_ioctl (struct inode *inode, struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct ac97_codec *codec = file->private_data;
	int rc;

	assert (codec != NULL);
	
	rc = codec->mixer_ioctl(codec, cmd, arg);
	
	return rc;
}


static loff_t via_llseek(struct file *file, loff_t offset, int origin)
{
	DPRINTK ("ENTER\n");

	DPRINTK("EXIT, returning -ESPIPE\n");
	return -ESPIPE;
}


static struct file_operations via_mixer_fops = {
	open:		via_mixer_open,
	release:	via_mixer_release,
	llseek:		via_llseek,
	ioctl:		via_mixer_ioctl,
};


#if 0 /* values reasoned from debugging dumps of via's driver */
static struct {
	u8 reg;
	u16 data;
} mixer_init_vals[] __devinitdata = {
	{ 0x2, 0x404 },
	{ 0x4, 0x404 },
	{ 0x6, 0x404 },
	{ 0x18, 0x404 },
	{ 0x10, 0x404 },
	{ 0x1a, 0x404 },
	{ 0x1c, 0x404 },
	{ 0x1a, 0x404 },
	{ 0x1c, 0xc0c },
	{ 0x12, 0x808 },
	{ 0x10, 0x808 },
	{ 0xe, 0x2 },
	{ 0x2, 0x808 },
	{ 0x18, 0x808 },
};
#endif


static int __init via_ac97_reset (struct via_info *card)
{
	struct pci_dev *pdev = card->pdev;
	u8 tmp8;
	u16 tmp16;
	
	DPRINTK ("ENTER\n");

	assert (pdev != NULL);
	
#ifndef NDEBUG
	{
		u8 r40,r41,r42,r43,r44,r48;
		pci_read_config_byte (card->pdev, 0x40, &r40);
		pci_read_config_byte (card->pdev, 0x41, &r41);
		pci_read_config_byte (card->pdev, 0x42, &r42);
		pci_read_config_byte (card->pdev, 0x43, &r43);
		pci_read_config_byte (card->pdev, 0x44, &r44);
		pci_read_config_byte (card->pdev, 0x48, &r48);
		DPRINTK("PCI config: %02X %02X %02X %02X %02X %02X\n",
			r40,r41,r42,r43,r44,r48);

		spin_lock_irq (&card->lock);
		DPRINTK ("regs==%02X %02X %02X %08X %08X %08X %08X\n",
			 inb (card->baseaddr + 0x00),
			 inb (card->baseaddr + 0x01),
			 inb (card->baseaddr + 0x02),
			 inl (card->baseaddr + 0x04),
			 inl (card->baseaddr + 0x0C),
			 inl (card->baseaddr + 0x80),
			 inl (card->baseaddr + 0x84));
		spin_unlock_irq (&card->lock);
		
	}
#endif

        /*
         * reset AC97 controller: enable, disable, enable
         * pause after each command for good luck
         */
        pci_write_config_byte (pdev, VIA_ACLINK_CTRL, VIA_CR41_AC97_ENABLE |
                               VIA_CR41_AC97_RESET | VIA_CR41_AC97_WAKEUP);
        udelay (100);
 
        pci_write_config_byte (pdev, VIA_ACLINK_CTRL, 0);
        udelay (100);
 
        pci_write_config_byte (pdev, VIA_ACLINK_CTRL,
			       VIA_CR41_AC97_ENABLE | VIA_CR41_PCM_ENABLE |
                               VIA_CR41_VRA | VIA_CR41_AC97_RESET);
        udelay (100);

	/* disable legacy stuff */
	pci_write_config_byte (pdev, 0x42, 0x00);
	udelay(10);

	/* route FM trap to IRQ, disable FM trap */
	pci_write_config_byte (pdev, 0x48, 0x05);
	udelay(10);
	
	/* disable all codec GPI interrupts */
	outl (0, pci_resource_start (pdev, 0) + 0x8C);

	/* enable variable rate */
	tmp16 = via_ac97_read_reg (&card->ac97, 0x2A);
	via_ac97_write_reg (&card->ac97, 0x2A, tmp16 | 0x01);
	
	/* boost headphone vol if disabled */
	tmp16 = via_ac97_read_reg (&card->ac97, AC97_HEADPHONE_VOL);
	if (tmp16 == 0)
		via_ac97_write_reg (&card->ac97, AC97_HEADPHONE_VOL, 0x1F1F);
	
	pci_read_config_byte (pdev, VIA_ACLINK_CTRL, &tmp8);
	if ((tmp8 & (VIA_CR41_AC97_ENABLE | VIA_CR41_AC97_RESET)) == 0) {
		printk (KERN_ERR PFX "cannot enable AC97 controller, aborting\n");
		DPRINTK ("EXIT, tmp8=%X, returning -ENODEV\n", tmp8);
		return -ENODEV;
	}
	
	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static int __init via_ac97_init (struct via_info *card)
{
	int rc;
	u16 tmp16;
	
	DPRINTK ("ENTER\n");

	assert (card != NULL);

	memset (&card->ac97, 0, sizeof (card->ac97));	
	card->ac97.private_data = card;
	card->ac97.codec_read = via_ac97_read_reg;
	card->ac97.codec_write = via_ac97_write_reg;
	
	card->ac97.dev_mixer = register_sound_mixer (&via_mixer_fops, -1);
	if (card->ac97.dev_mixer < 0) {
		printk (KERN_ERR PFX "unable to register AC97 mixer, aborting\n");
		DPRINTK("EXIT, returning -EIO\n");
		return -EIO;
	}

	rc = via_ac97_reset (card);
	if (rc) {
		printk (KERN_ERR PFX "unable to reset AC97 codec, aborting\n");
		goto err_out;
	}

	if (ac97_probe_codec (&card->ac97) == 0) {
		printk (KERN_ERR PFX "unable to probe AC97 codec, aborting\n");
		rc = -EIO;
		goto err_out;
	}

	tmp16 = via_ac97_read_reg (&card->ac97, 0x2A);
	via_ac97_write_reg (&card->ac97, 0x2A, tmp16 | 0x01);
	
	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out:
	unregister_sound_mixer (card->ac97.dev_mixer);
	DPRINTK("EXIT, returning %d\n", rc);
	return rc;
}


static void via_ac97_cleanup (struct via_info *card)
{
	DPRINTK("ENTER\n");
	
	assert (card != NULL);
	assert (card->ac97.dev_mixer >= 0);
	
	unregister_sound_mixer (card->ac97.dev_mixer);

	DPRINTK("EXIT\n");
}



/****************************************************************
 *
 * Interrupt-related code
 *
 */
 
static inline void via_interrupt_write (struct via_channel *chan)
{
	assert (atomic_read(&chan->buf_in_use) <
		atomic_read(&chan->next_buf));

	/* XXX sanity check: read h/w counter to 
	 * ensure no lost frames */

	atomic_inc (&chan->buf_in_use);

	/* if SG ptr catches up with userland ptr, stop playback */
	if (atomic_read(&chan->buf_in_use) == atomic_read(&chan->next_buf)) {
		atomic_set (&chan->state, sgd_stopped);
		via_chan_stop (chan->iobase);
	}

	/* wake up anybody listening */
	if (waitqueue_active (&chan->wait))
		wake_up (&chan->wait);
}



static void via_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct via_info *card = dev_id;
	struct via_channel *chan;
	u8 status;
	int unhandled = 1;
	static long intcount = 0;

	assert (irq == card->pdev->irq);
	
	intcount++;
	
	status = inb (card->baseaddr + 0x00);
	if (status) {
		assert (card->open_mode & FMODE_WRITE);
		
		chan = &card->ch_out;
		unhandled = 0;
		
		if (status & VIA_SGD_FLAG) {
			assert ((status & VIA_SGD_EOL) == 0);
			outb (VIA_SGD_FLAG, chan->iobase + 0x00);
			DPRINTK("FLAG intr, status=0x%02X, intcount=%ld\n",
				status, intcount);
			via_interrupt_write (chan);
		}
		
		if (status & VIA_SGD_EOL) {
			assert ((status & VIA_SGD_FLAG) == 0);
			outb (VIA_SGD_EOL, chan->iobase + 0x00);
			DPRINTK("EOL intr, status=0x%02X, intcount=%ld\n",
				status, intcount);
			via_interrupt_write (chan);
		}
		
		if (status & VIA_SGD_STOPPED) {
			outb (VIA_SGD_STOPPED, chan->iobase + 0x00);
			DPRINTK("STOPPED intr, status=0x%02X, intcount=%ld\n",
				status, intcount);
		}

#if 0
		via_chan_dump_bufs (&card->ch_out);
#endif
	}
	
	if (unhandled)
		printk (KERN_WARNING PFX "unhandled interrupt, st=%02x, st32=%08x\n",
			status, inl (card->baseaddr + 0x84));
}


static void via_interrupt_disable (struct via_info *card)
{
	u8 tmp8;
	unsigned long flags;

	DPRINTK ("ENTER\n");

	assert (card != NULL);
	
	spin_lock_irqsave (&card->lock, flags);

	pci_read_config_byte (card->pdev, VIA_FM_NMI_CTRL, &tmp8);
	if ((tmp8 & VIA_CR48_FM_TRAP_TO_NMI) == 0) {
		tmp8 |= VIA_CR48_FM_TRAP_TO_NMI;
		pci_write_config_byte (card->pdev, VIA_FM_NMI_CTRL, tmp8);
	}

	outb (inb (card->baseaddr + VIA_BASE0_PCM_OUT_CHAN_TYPE) &
	      VIA_INT_DISABLE_MASK,
	      card->baseaddr + VIA_BASE0_PCM_OUT_CHAN_TYPE);
	outb (inb (card->baseaddr + VIA_BASE0_PCM_IN_CHAN_TYPE) &
	      VIA_INT_DISABLE_MASK,
	      card->baseaddr + VIA_BASE0_PCM_IN_CHAN_TYPE);
	outb (inb (card->baseaddr + VIA_BASE0_FM_OUT_CHAN_TYPE) &
	      VIA_INT_DISABLE_MASK,
	      card->baseaddr + VIA_BASE0_FM_OUT_CHAN_TYPE);

	spin_unlock_irqrestore (&card->lock, flags);

	DPRINTK ("EXIT\n");
}


static int via_interrupt_init (struct via_info *card)
{
	DPRINTK ("ENTER\n");

	assert (card != NULL);
	assert (card->pdev != NULL);
	
	/* check for sane IRQ number. can this ever happen? */
	if (card->pdev->irq < 2) {
		printk (KERN_ERR PFX "insane IRQ %d, aborting\n",
			card->pdev->irq);
		DPRINTK ("EXIT, returning -EIO\n");
		return -EIO;
	}
	
	if (request_irq (card->pdev->irq, via_interrupt, SA_SHIRQ, VIA_MODULE_NAME, card)) {
		printk (KERN_ERR PFX "unable to obtain IRQ %d, aborting\n",
			card->pdev->irq);
		DPRINTK ("EXIT, returning -EBUSY\n");
		return -EBUSY;
	}

	/* we don't want interrupts until we're opened */
	via_interrupt_disable (card);

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static void via_interrupt_cleanup (struct via_info *card)
{
	DPRINTK ("ENTER\n");

	assert (card != NULL);
	assert (card->pdev != NULL);

	via_interrupt_disable (card);

	free_irq (card->pdev->irq, card);

	DPRINTK ("EXIT\n");
}


/****************************************************************
 *
 * OSS DSP device
 *
 */
 
static struct file_operations via_dsp_fops = {
	open:		via_dsp_open,
	release:	via_dsp_release,
	read:		via_dsp_read,
	write:		via_dsp_write,
	poll:		via_dsp_poll,
	llseek: 	via_llseek,
	ioctl:		via_dsp_ioctl,
};


static int __init via_dsp_init (struct via_info *card)
{
	u8 tmp8;
	
	DPRINTK ("ENTER\n");

	assert (card != NULL);

	/* turn off legacy features, if not already */
	pci_read_config_byte (card->pdev, VIA_FUNC_ENABLE, &tmp8);
	tmp8 &= ~(VIA_CR42_SB_ENABLE | VIA_CR42_MIDI_ENABLE |
		  VIA_CR42_FM_ENABLE);
	pci_write_config_byte (card->pdev, VIA_FUNC_ENABLE, tmp8);

	via_stop_everything (card);

	card->dev_dsp = register_sound_dsp (&via_dsp_fops, -1);
	if (card->dev_dsp < 0) {
		DPRINTK ("EXIT, returning -ENODEV\n");
		return -ENODEV;
	}
	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static void via_dsp_cleanup (struct via_info *card)
{
	DPRINTK ("ENTER\n");

	assert (card != NULL);
	assert (card->dev_dsp >= 0);

	via_stop_everything (card);

	unregister_sound_dsp (card->dev_dsp);

	DPRINTK ("EXIT\n");
}


static ssize_t via_dsp_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct via_info *card;

	DPRINTK ("ENTER\n");
	
	assert (file != NULL);
	assert (buffer != NULL);
	card = file->private_data;
	assert (card != NULL);

	DPRINTK("EXIT, returning -EINVAL\n");
	return -EINVAL;
}



#define sgcount(n) (sgtable[(n)].count & 0x00FFFFFF)
#define NEXTBUF (atomic_read(&chan->next_buf) % VIA_DMA_BUFFERS)
#define BUF_IN_USE (atomic_read(&chan->buf_in_use) % VIA_DMA_BUFFERS)
#define STATE_STOPPED (atomic_read (state) == sgd_stopped)
#define STATE_STARTED (atomic_read (state) == sgd_in_progress)
static ssize_t via_dsp_do_write (struct via_info *card,
				 const char *userbuf, size_t count,
				 int non_blocking)
{
	const char *orig_userbuf = userbuf;
	struct via_channel *chan = &card->ch_out;
	volatile struct via_sgd_table *sgtable = chan->sgtable;
	atomic_t *state = &chan->state;
	size_t size;
	int nextbuf, prevbuf, n, realcount;
	ssize_t rc;
	
	while (count > 0) {
		if (current->need_resched)
			schedule ();

		spin_lock_irq (&card->lock);
		DPRINTK ("regs==%02X %02X %02X %08X %08X %08X %08X\n",
			 inb (card->baseaddr + 0x00),
			 inb (card->baseaddr + 0x01),
			 inb (card->baseaddr + 0x02),
			 inl (card->baseaddr + 0x04),
			 inl (card->baseaddr + 0x0C),
			 inl (card->baseaddr + 0x80),
			 inl (card->baseaddr + 0x84));
		spin_unlock_irq (&card->lock);
		
		size = (count < VIA_DMA_BUF_SIZE) ? count : VIA_DMA_BUF_SIZE;

		/* case 1: SGD not active, list is ours for the mangling */

		if (STATE_STOPPED) {
			DPRINTK ("case 1\n");

			if (copy_from_user ((void*)chan->sgbuf[0].cpuaddr,
					     userbuf, size))
				return -EFAULT;

			assert (sgtable[0].addr == cpu_to_le32 (chan->sgbuf[0].handle));
			sgtable[0].count = size | VIA_FLAG;

			atomic_set (state, sgd_in_progress);
			atomic_set (&chan->buf_in_use, 0);
			atomic_set (&chan->next_buf, 1);
			
			count -= size;
			userbuf += size;

			spin_lock_irq (&card->lock);
			sg_begin (chan);
			spin_unlock_irq (&card->lock);

			continue;
		}

		nextbuf = NEXTBUF;
		if (nextbuf)
			prevbuf = nextbuf - 1;
		else
			prevbuf = VIA_DMA_BUFFERS - 1;

		/* case 2: if final buffer is (a) a fragment, and (b) not
		 * currently being consumed by the SGD engine, then append
		 * as much data as possible to the fragment. */

		realcount = sgcount(prevbuf);
		if (STATE_STARTED && (prevbuf != BUF_IN_USE) &&
		    (realcount < VIA_DMA_BUF_SIZE)) {
			DPRINTK ("case 2\n");
			DPRINTK ("st=%d, fb=%d -- nb=%d, pb=%d, n=%d, rc=%d\n",
				 atomic_read (state),
				 BUF_IN_USE,
				 nextbuf,
				 prevbuf,
				 prevbuf /* n */,
				 realcount);

			n = prevbuf;

			if ((VIA_DMA_BUF_SIZE - realcount) < size)
				size = VIA_DMA_BUF_SIZE - realcount;

			if (copy_from_user ((void*)(chan->sgbuf[n].cpuaddr +
					     realcount),
					     userbuf, size))
				return -EFAULT;

			/* slack way to try and prevent races */
			if (prevbuf == BUF_IN_USE || !STATE_STARTED)
				continue;

			assert (sgtable[n].addr == cpu_to_le32 (chan->sgbuf[n].handle));
			if (n == (VIA_DMA_BUFFERS - 1))
				sgtable[n].count = (realcount + size) | VIA_EOL;
			else
				sgtable[n].count = (realcount + size) | VIA_FLAG;

			count -= size;
			userbuf += size;
			continue;
		}

		/* case 3: if there are buffers left, use one
		 * XXX needs more review for possible races */

		else if (STATE_STARTED && !via_chan_full (chan)) {
			DPRINTK ("case 3\n");
			DPRINTK ("st=%d, fb=%d -- nb=%d, pb=%d, n=%d\n",
				 atomic_read (state),
				 BUF_IN_USE,
				 nextbuf,
				 prevbuf,
				 nextbuf /* n */);

			n = nextbuf;

			if (copy_from_user ((void*)chan->sgbuf[n].cpuaddr,
					     userbuf, size))
				return -EFAULT;

			if (n == (VIA_DMA_BUFFERS - 1))
				sgtable[n].count = size | VIA_EOL;
			else
				sgtable[n].count = size | VIA_FLAG;

			/* if SGD stopped during data copy or SG table update,
			 * then loop back to the beginning without updating
			 * any pointers.
			 * ie. slack way to prevent race */
			if (!STATE_STARTED)
				continue;

			atomic_inc (&chan->next_buf);

			count -= size;
			userbuf += size;
			continue;
		}

		/* case 4, final SGT active case: no free buffers, wait for one */

		else {
			DPRINTK ("case 4\n");
			DPRINTK ("st=%d, fb=%d -- nb=%d, pb=%d\n",
				 atomic_read (state),
				 BUF_IN_USE,
				 nextbuf,
				 prevbuf);

			/* if playback stopped, no need to sleep */
			if (!STATE_STARTED)
				continue;
			
			/* if buffer free, no need to sleep */
			if (!via_chan_full (chan))
				continue;
			
			if (non_blocking) {
				rc = userbuf - orig_userbuf;
				if (rc == 0)
					rc = -EAGAIN;
				return rc;
			}

			DPRINTK ("sleeping\n");
			interruptible_sleep_on (&chan->wait);
			if (signal_pending (current))
				return -ERESTARTSYS;
		}
	}

#if 0
	{
		u8 r40,r41,r42,r43,r44,r48;
		pci_read_config_byte (card->pdev, 0x40, &r40);
		pci_read_config_byte (card->pdev, 0x41, &r41);
		pci_read_config_byte (card->pdev, 0x42, &r42);
		pci_read_config_byte (card->pdev, 0x43, &r43);
		pci_read_config_byte (card->pdev, 0x44, &r44);
		pci_read_config_byte (card->pdev, 0x48, &r48);
		DPRINTK("PCI config: %02X %02X %02X %02X %02X %02X\n",
			r40,r41,r42,r43,r44,r48);
	}
#endif

	DPRINTK ("EXIT, returning %d\n",
		 userbuf - orig_userbuf);
	return userbuf - orig_userbuf;
}
#undef sgcount
#undef NEXTBUF
#undef BUF_IN_USE
#undef STATE_STOPPED
#undef STATE_STARTED


static ssize_t via_dsp_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct via_info *card;
	ssize_t rc;

	DPRINTK ("ENTER, file=%p, buffer=%p, count=%u, ppos=%lu\n",
		 file, buffer, count, ppos ? ((unsigned long)*ppos) : 0);
	
	assert (file != NULL);
	assert (buffer != NULL);
	card = file->private_data;
	assert (card != NULL);

	if (ppos != &file->f_pos) {
		DPRINTK ("EXIT, returning -ESPIPE\n");
		return -ESPIPE;
	}

	rc = via_dsp_do_write (card, buffer, count, (file->f_flags & O_NONBLOCK));

	DPRINTK("EXIT, returning %ld\n",(long) rc);
	return rc;
}


static unsigned int via_dsp_poll(struct file *file, struct poll_table_struct *wait)
{
	struct via_info *card;
	unsigned int mask = 0;

	DPRINTK ("ENTER\n");

	assert (file != NULL);
	assert (wait != NULL);
	card = file->private_data;
	assert (card != NULL);

	if ((file->f_mode & FMODE_WRITE) &&
	    (atomic_read (&card->ch_out.state) != sgd_stopped)) {
		poll_wait(file, &card->ch_out.wait, wait);
		
		/* XXX is this correct */
		if (atomic_read (&card->ch_out.buf_in_use) <
		    atomic_read (&card->ch_out.next_buf))
			mask |= POLLOUT | POLLWRNORM;
	}

	DPRINTK("EXIT, returning %u\n", mask);
	return mask;
}


static int via_dsp_drain_dac (struct via_info *card, int non_block)
{
	DPRINTK ("ENTER, non_block = %d\n", non_block);

	while (!via_chan_empty (&card->ch_out)) {
		if (non_block) {
			DPRINTK ("EXIT, returning -EBUSY\n");
			return -EBUSY;
		}
		if (signal_pending (current)) {
			DPRINTK ("EXIT, returning -ERESTARTSYS\n");
			return -ERESTARTSYS;
		}

#ifndef NDEBUG
	{
		u8 r40,r41,r42,r43,r44,r48;
		pci_read_config_byte (card->pdev, 0x40, &r40);
		pci_read_config_byte (card->pdev, 0x41, &r41);
		pci_read_config_byte (card->pdev, 0x42, &r42);
		pci_read_config_byte (card->pdev, 0x43, &r43);
		pci_read_config_byte (card->pdev, 0x44, &r44);
		pci_read_config_byte (card->pdev, 0x48, &r48);
		DPRINTK("PCI config: %02X %02X %02X %02X %02X %02X\n",
			r40,r41,r42,r43,r44,r48);

		spin_lock_irq (&card->lock);
		DPRINTK ("regs==%02X %02X %02X %08X %08X %08X %08X\n",
			 inb (card->baseaddr + 0x00),
			 inb (card->baseaddr + 0x01),
			 inb (card->baseaddr + 0x02),
			 inl (card->baseaddr + 0x04),
			 inl (card->baseaddr + 0x0C),
			 inl (card->baseaddr + 0x80),
			 inl (card->baseaddr + 0x84));
		spin_unlock_irq (&card->lock);
		
	}
#endif

		DPRINTK ("sleeping\n");		
		interruptible_sleep_on (&card->ch_out.wait);
	}
	
	DPRINTK ("EXIT\n");
	return 0;
}


static int via_dsp_ioctl_space (struct via_info *card,
				struct via_channel *chan,
				void *arg)
{
	audio_buf_info info;
	int n;

	info.fragstotal = VIA_DMA_BUFFERS;
	info.fragsize = VIA_DMA_BUF_SIZE;

	/* number of full fragments we can read without blocking */
	n = atomic_read (&chan->next_buf) - atomic_read (&chan->buf_in_use);
	info.fragments = VIA_DMA_BUFFERS - n;

	/* number of bytes that can be read or written immediately
	 * without blocking.  FIXME: we are lazy and ignore partially-full
	 * buffers.
	 */
	info.bytes = info.fragments * VIA_DMA_BUF_SIZE;

	DPRINTK ("EXIT, returning fragstotal=%d, fragsize=%d, fragments=%d, bytes=%d\n",
		info.fragstotal,
		info.fragsize,
		info.fragments,
		info.bytes);

	return copy_to_user (arg, &info, sizeof (info));
}



static int via_dsp_ioctl (struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	int rc = -EINVAL, rd=0, wr=0, val=0;
	struct via_info *card;

	DPRINTK ("ENTER, cmd = 0x%08X\n", cmd);

	assert (file != NULL);
	card = file->private_data;
	assert (card != NULL);

	if (file->f_mode & FMODE_WRITE)
		wr = 1;
	if (file->f_mode & FMODE_READ)
		rd = 1;
	
	switch (cmd) {

	/* OSS API version.  XXX unverified */		
	case OSS_GETVERSION:
		DPRINTK("EXIT, returning SOUND_VERSION\n");
		return put_user (SOUND_VERSION, (int *)arg);

	/* list of supported PCM data formats */
	case SNDCTL_DSP_GETFMTS:
		DPRINTK("EXIT, returning AFMT U8|S16_LE\n");
                return put_user (AFMT_U8 | AFMT_S16_LE, (int *)arg);

	/* query or set current channel's PCM data format */
	case SNDCTL_DSP_SETFMT:
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val != AFMT_QUERY) {
			rc = 0;

			spin_lock_irq (&card->lock);
			if (rc == 0 && rd)
				rc = via_chan_set_fmt (card, &card->ch_in, val);
			if (rc == 0 && wr)
				rc = via_chan_set_fmt (card, &card->ch_out, val);
			spin_unlock_irq (&card->lock);

			if (rc <= 0)
				return rc ? rc : -EINVAL;
			val = rc;
		} else {
			spin_lock_irq (&card->lock);
			if ((rd && (card->ch_in.pcm_fmt & VIA_PCM_FMT_16BIT)) ||
			    (wr && (card->ch_out.pcm_fmt & VIA_PCM_FMT_16BIT)))
				val = AFMT_S16_LE;
			else
				val = AFMT_U8;
			spin_unlock_irq (&card->lock);
		}
		DPRINTK("SETFMT EXIT, returning %d\n", val);
                return put_user (val, (int *)arg);

	/* query or set number of channels (1=mono, 2=stereo) */
        case SNDCTL_DSP_CHANNELS:
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val != 0) {
			rc = 0;
			spin_lock_irq (&card->lock);
			if (rc == 0 && rd)
				rc = via_chan_set_stereo (card, &card->ch_in, val);
			if (rc == 0 && wr)
				rc = via_chan_set_stereo (card, &card->ch_out, val);
			spin_unlock_irq (&card->lock);
			if (rc <= 0)
				return rc ? rc : -EINVAL;
			val = rc;
		} else {
			spin_lock_irq (&card->lock);
			if ((rd && (card->ch_in.pcm_fmt & VIA_PCM_FMT_STEREO)) ||
			    (wr && (card->ch_out.pcm_fmt & VIA_PCM_FMT_STEREO)))
				val = 2;
			else
				val = 1;
			spin_unlock_irq (&card->lock);
		}
		DPRINTK("CHANNELS EXIT, returning %d\n", val);
                return put_user (val, (int *)arg);
	
	/* enable (val is not zero) or disable (val == 0) stereo */
        case SNDCTL_DSP_STEREO:
		get_user_ret(val, (int *)arg, -EFAULT);
		rc = 0;
		spin_lock_irq (&card->lock);
		if (rc == 0 && rd)
			rc = via_chan_set_stereo (card, &card->ch_in, val ? 2 : 1);
		if (rc == 0 && wr)
			rc = via_chan_set_stereo (card, &card->ch_out, val ? 2 : 1);
		spin_unlock_irq (&card->lock);
		if (rc <= 0)
			return rc ? rc : -EINVAL;
		DPRINTK("STEREO EXIT, returning %d\n", val);
                return 0;
	
	/* query or set sampling rate */
        case SNDCTL_DSP_SPEED:
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val < 0)
			return -EINVAL;
		if (val > 0) {
			rc = 0;
			spin_lock_irq (&card->lock);
			if (rc == 0 && rd)
				rc = via_chan_set_speed (card, &card->ch_in, val);
			if (rc == 0 && wr)
				rc = via_chan_set_speed (card, &card->ch_out, val);
			spin_unlock_irq (&card->lock);
			if (rc <= 0)
				return rc ? rc : -EINVAL;
			val = rc;
		} else {
			spin_lock_irq (&card->lock);
			if (rd)
				val = card->ch_in.rate;
			else if (wr)
				val = card->ch_out.rate;
			else
				val = 0;
			spin_unlock_irq (&card->lock);
		}
		DPRINTK("SPEED EXIT, returning %d\n", val);
                return put_user (val, (int *)arg);
	
	/* wait until all buffers have been played, and then stop device */
	case SNDCTL_DSP_SYNC:
		if (wr) {
			DPRINTK("SYNC EXIT (after calling via_dsp_drain_dac)\n");
			return via_dsp_drain_dac (card, file->f_flags & O_NONBLOCK);
		}
		break;

	/* stop recording/playback immediately */
        case SNDCTL_DSP_RESET:
		spin_lock_irq (&card->lock);
		if (rd) {
			via_chan_clear (&card->ch_in);
			via_chan_pcm_fmt (card, &card->ch_in, 1);
		}
		if (wr) {
			via_chan_clear (&card->ch_out);
			via_chan_pcm_fmt (card, &card->ch_out, 1);
		}
		spin_unlock_irq (&card->lock);
		DPRINTK("RESET EXIT, returning 0\n");
		return 0;

	/* obtain bitmask of device capabilities, such as mmap, full duplex, etc. */
	case SNDCTL_DSP_GETCAPS:
		DPRINTK("GETCAPS EXIT\n");
		return put_user(DSP_CAP_REVISION, (int *)arg);
		
	/* obtain bitmask of device capabilities, such as mmap, full duplex, etc. */
	case SNDCTL_DSP_GETBLKSIZE:
		DPRINTK("GETBLKSIZE EXIT\n");
		return put_user(VIA_DMA_BUF_SIZE, (int *)arg);
		
	/* obtain information about input buffering */
	case SNDCTL_DSP_GETISPACE:
		DPRINTK("GETISPACE EXIT\n");
		return via_dsp_ioctl_space (card, &card->ch_in, (void*) arg);
	
	/* obtain information about output buffering */
	case SNDCTL_DSP_GETOSPACE:
		DPRINTK("GETOSPACE EXIT\n");
		return via_dsp_ioctl_space (card, &card->ch_out, (void*) arg);

	/* return number of bytes remaining to be played by DMA engine */
	case SNDCTL_DSP_GETODELAY:
		{
		int n;
		
		n = atomic_read (&card->ch_out.next_buf) -
		    atomic_read (&card->ch_out.buf_in_use);
		assert (n >= 0);

		if (n == 0)
			val = 0;
		else {
			val = (n - 1) * VIA_DMA_BUF_SIZE;
			val += inl (card->ch_out.iobase + VIA_BASE0_PCM_OUT_BLOCK_COUNT);
		}
		
		DPRINTK("GETODELAY EXIT, val = %d bytes\n", val);
                return put_user (val, (int *)arg);
		}

	/* set fragment size.  implemented as a successful no-op for now */
	case SNDCTL_DSP_SETFRAGMENT:
		get_user_ret(val, (int *)arg, -EFAULT);

		DPRINTK ("SNDCTL_DSP_SETFRAGMENT (fragshift==0x%04X (%d), maxfrags==0x%04X (%d))\n",
			 val & 0xFFFF,
			 val & 0xFFFF,
			 (val >> 16) & 0xFFFF,
			 (val >> 16) & 0xFFFF);

		/* just to shut up some programs */
		return 0;

	/* inform device of an upcoming pause in input (or output).  not implemented */
	case SNDCTL_DSP_POST:
		DPRINTK("POST EXIT (null ioctl, returning -EINVAL)\n");
                return -EINVAL;

	/* not implemented */
	default:
		DPRINTK ("unhandled ioctl\n");
		break;
	}
		
	DPRINTK("EXIT, returning %d\n", rc);
	return rc;
}


static int via_dsp_open (struct inode *inode, struct file *file)
{
	int open_mode, rc = -EINVAL, minor = MINOR(inode->i_rdev);
	int got_read_chan = 0, is_busy;
	struct via_info *card;
	struct pci_dev *pdev;
	struct pci_driver *drvr;
	unsigned long flags;

	DPRINTK ("ENTER, minor=%d, file->f_mode=0x%x\n", minor, file->f_mode);
	
	MOD_INC_USE_COUNT;
	
	if (file->f_mode & FMODE_READ) /* no input ATM */
		goto err_out;

	card = NULL;
	pci_for_each_dev(pdev) {
		drvr = pci_dev_driver (pdev);
		if (drvr == &via_driver) {
			assert (pdev->driver_data != NULL);
			
			card = pdev->driver_data;	
			DPRINTK ("dev_dsp = %d, minor = %d, assn = %d\n",
				 card->dev_dsp, minor,
				 (card->dev_dsp ^ minor) & ~0xf);

			if (((card->dev_dsp ^ minor) & ~0xf) == 0)
				goto match;
		}
	}
	
	DPRINTK ("no matching %s found\n", card ? "minor" : "driver");
	rc = -ENODEV;
	goto err_out;

match:
	file->private_data = card;

	/* wait for device to become free */
	spin_lock_irqsave (&card->lock, flags);
	open_mode = card->open_mode;
	if (open_mode & file->f_mode)
		is_busy = 1;
	else {
		is_busy = 0;
		card->open_mode |= file->f_mode;
		open_mode = card->open_mode;
	}
	spin_unlock_irqrestore (&card->lock, flags);
	if (is_busy) {
		rc = -EBUSY;
		goto err_out;
	}

	DPRINTK("open_mode now 0x%x\n", open_mode);

	/* handle input from analog source */
	if (file->f_mode & FMODE_READ) {
		rc = via_chan_init (card, &card->ch_in, 0x10);
		if (rc)
			goto err_out_clear_mode;
			
		got_read_chan = 1;
		
		/* why is this forced to 16-bit stereo in all drivers? */
		card->ch_in.pcm_fmt =
			VIA_PCM_FMT_16BIT | VIA_PCM_FMT_STEREO;

		spin_lock_irqsave (&card->lock, flags);
		via_chan_pcm_fmt (card, &card->ch_out, 0);
		spin_unlock_irqrestore (&card->lock, flags);

		via_set_adc_rate (card, 8000);
	}

	/* handle output to analog source */
	if (file->f_mode & FMODE_WRITE) {
		rc = via_chan_init (card, &card->ch_out, 0x00);
		if (rc)
			goto err_out_clear_mode;
		
		if ((minor & 0xf) == SND_DEV_DSP16)
			card->ch_out.pcm_fmt |= VIA_PCM_FMT_16BIT;

		spin_lock_irqsave (&card->lock, flags);
		via_chan_pcm_fmt (card, &card->ch_out, 0);
		spin_unlock_irqrestore (&card->lock, flags);

		via_set_dac_rate (card, 8000);
	}

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out_clear_mode:
	if (got_read_chan)
		via_chan_free (card, &card->ch_in);
	spin_lock_irqsave (&card->lock, flags);
	card->open_mode &= ~file->f_mode;
	spin_unlock_irqrestore (&card->lock, flags);
err_out:
	MOD_DEC_USE_COUNT;
	DPRINTK("ERROR EXIT, returning %d\n", rc);
	return rc;
}


static int via_dsp_release(struct inode *inode, struct file *file)
{
	struct via_info *card;
	unsigned long flags;

	DPRINTK ("ENTER\n");
	
	assert (file != NULL);
	card = file->private_data;
	assert (card != NULL);
	
	if (file->f_mode & FMODE_READ)
		via_chan_free (card, &card->ch_in);

	if (file->f_mode & FMODE_WRITE) {
		via_dsp_drain_dac (card, file->f_flags & O_NONBLOCK);
		via_chan_free (card, &card->ch_out);
	}
			
	spin_lock_irqsave (&card->lock, flags);
	card->open_mode &= ~(file->f_mode);
	spin_unlock_irqrestore (&card->lock, flags);

 	wake_up (&card->open_wait);
	MOD_DEC_USE_COUNT;

	DPRINTK("EXIT, returning 0\n");
	return 0;
}


#ifdef VIA_PROC_FS

/****************************************************************
 *
 * /proc/driver/via/info
 *
 *
 */

static int via_info_read_proc (char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
#define YN(val,bit) (((val) & (bit)) ? "yes" : "no")
#define ED(val,bit) (((val) & (bit)) ? "enable" : "disable")

	int len = 0;
	u8 r40, r41, r42, r44;
	struct via_info *card = data;
	
	DPRINTK ("ENTER\n");

	assert (card != NULL);
	
	len += sprintf (page+len, VIA_CARD_NAME "\n\n");
	
	pci_read_config_byte (card->pdev, 0x40, &r40);
	pci_read_config_byte (card->pdev, 0x41, &r41);
	pci_read_config_byte (card->pdev, 0x42, &r42);
	pci_read_config_byte (card->pdev, 0x44, &r44);
	
	len += sprintf (page+len,
		"Via 82Cxxx PCI registers:\n"
		"\n"
		"40  Codec Ready: %s\n"
		"    Codec Low-power: %s\n"
		"    Secondary Codec Ready: %s\n"
		"\n"
		"41  Interface Enable: %s\n"
		"    De-Assert Reset: %s\n"
		"    Force SYNC high: %s\n"
		"    Force SDO high: %s\n"
		"    Variable Sample Rate On-Demand Mode: %s\n"
		"    SGD Read Channel PCM Data Out: %s\n"
		"    FM Channel PCM Data Out: %s\n"
		"    SB PCM Data Out: %s\n"
		"\n"
		"42  Game port enabled: %s\n"
		"    SoundBlaster enabled: %s\n"
		"    FM enabled: %s\n"
		"    MIDI enabled: %s\n"
		"\n"	
		"44  AC-Link Interface Access: %s\n"
		"    Secondary Codec Support: %s\n"
			
		"\n",
			
		YN (r40, VIA_CR40_AC97_READY),
		YN (r40, VIA_CR40_AC97_LOW_POWER),
		YN (r40, VIA_CR40_SECONDARY_READY),

		ED (r41, VIA_CR41_AC97_ENABLE),
		YN (r41, (1 << 6)),
		YN (r41, (1 << 5)),
		YN (r41, (1 << 4)),
		ED (r41, (1 << 3)),
		ED (r41, (1 << 2)),
		ED (r41, (1 << 1)),
		ED (r41, (1 << 0)),

		YN (r42, VIA_CR42_GAME_ENABLE),
		YN (r42, VIA_CR42_SB_ENABLE),
		YN (r42, VIA_CR42_FM_ENABLE),
		YN (r42, VIA_CR42_MIDI_ENABLE),

		YN (r44, VIA_CR44_AC_LINK_ACCESS),
		YN (r44, VIA_CR44_SECOND_CODEC_SUPPORT)
			
		);

	DPRINTK("EXIT, returning %d\n", len);
	return len;

#undef YN
#undef ED
}


/****************************************************************
 *
 * /proc/driver/via/... setup and cleanup
 *
 *
 */

static int __init via_init_proc (void)
{
	DPRINTK ("ENTER\n");

	if (!proc_mkdir ("driver/via", 0))
		return -EIO;

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static void via_cleanup_proc (void)
{
	DPRINTK ("ENTER\n");

	remove_proc_entry ("driver/via", NULL);

	DPRINTK ("EXIT\n");
}


static int __init via_card_init_proc (struct via_info *card)
{
	char s[32];
	int rc;
	
	DPRINTK ("ENTER\n");

	sprintf (s, "driver/via/%d", card->card_num);
	if (!proc_mkdir (s, 0)) {
		rc = -EIO;
		goto err_out_none;
	}
	
	sprintf (s, "driver/via/%d/info", card->card_num);
	if (!create_proc_read_entry (s, 0, 0, via_info_read_proc, card)) {
		rc = -EIO;
		goto err_out_dir;
	}

	sprintf (s, "driver/via/%d/ac97", card->card_num);
	if (!create_proc_read_entry (s, 0, 0, ac97_read_proc, &card->ac97)) {
		rc = -EIO;
		goto err_out_info;
	}

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out_info:
	sprintf (s, "driver/via/%d/info", card->card_num);
	remove_proc_entry (s, NULL);

err_out_dir:
	sprintf (s, "driver/via/%d", card->card_num);
	remove_proc_entry (s, NULL);

err_out_none:
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}


static void via_card_cleanup_proc (struct via_info *card)
{
	char s[32];

	DPRINTK ("ENTER\n");

	sprintf (s, "driver/via/%d/ac97", card->card_num);
	remove_proc_entry (s, NULL);

	sprintf (s, "driver/via/%d/info", card->card_num);
	remove_proc_entry (s, NULL);

	sprintf (s, "driver/via/%d", card->card_num);
	remove_proc_entry (s, NULL);

	DPRINTK ("EXIT\n");
}


#else

static inline int via_init_proc (void) { return 0; }
static inline void via_cleanup_proc (void) {}
static inline int via_card_init_proc (struct via_info *card) { return 0; }
static inline void via_card_cleanup_proc (struct via_info *card) {}

#endif /* VIA_PROC_FS */


/****************************************************************
 *
 * Chip setup and kernel registration
 *
 *
 */

static int __init via_init_one (struct pci_dev *pdev, const struct pci_device_id *id)
{
	int rc;
	struct via_info *card;
	u8 tmp;
	static int printed_version = 0;
	
	DPRINTK ("ENTER\n");
	
	if (printed_version++ == 0)
		printk (KERN_INFO "Via 686a audio driver " VIA_VERSION "\n");

	if (!request_region (pci_resource_start (pdev, 0),
	    		     pci_resource_len (pdev, 0),
			     VIA_MODULE_NAME)) {
		printk (KERN_ERR PFX "unable to obtain I/O resources, aborting\n");
		rc = -EBUSY;
		goto err_out;
	}

	if (pci_enable_device (pdev)) {
		rc = -EIO;
		goto err_out_none;
	}
	
	card = kmalloc (sizeof (*card), GFP_KERNEL);
	if (!card) {
		printk (KERN_ERR PFX "out of memory, aborting\n");
		rc = -ENOMEM;
		goto err_out_none;
	}

	pdev->driver_data = card;

	memset (card, 0, sizeof (*card));
	card->pdev = pdev;
	card->baseaddr = pci_resource_start (pdev, 0);
	card->card_num = via_num_cards++;
	spin_lock_init (&card->lock);
	init_waitqueue_head(&card->open_wait);
	
	/* if BAR 2 is present, chip is Rev H or later,
	 * which means it has a few extra features */
	if (pci_resource_start (pdev, 2) > 0)
		card->rev_h = 1;

	if (pdev->irq < 1) {
		printk (KERN_ERR PFX "invalid PCI IRQ %d, aborting\n", pdev->irq);
		rc = -ENODEV;
		goto err_out_kfree;
	}

	if (!(pci_resource_flags (pdev, 0) & IORESOURCE_IO)) {
		printk (KERN_ERR PFX "unable to locate I/O resources, aborting\n");
		rc = -ENODEV;
		goto err_out_kfree;
	}
	
	/* 
	 * init AC97 mixer and codec
	 */
	rc = via_ac97_init (card);
	if (rc) {
		printk (KERN_ERR PFX "AC97 init failed, aborting\n");
		goto err_out_kfree;
	}

	/*
	 * init DSP device
	 */
	rc = via_dsp_init (card);
	if (rc) {
		printk (KERN_ERR PFX "DSP device init failed, aborting\n");
		goto err_out_have_mixer;
	}
	
	/*
	 * per-card /proc info
	 */	
	rc = via_card_init_proc (card);
	if (rc) {
		printk (KERN_ERR PFX "card-specific /proc init failed, aborting\n");
		goto err_out_have_dsp;
	}

	/*
	 * init and turn on interrupts, as the last thing we do
	 */
	rc = via_interrupt_init (card);
	if (rc) {
		printk (KERN_ERR PFX "interrupt init failed, aborting\n");
		goto err_out_have_proc;
	}
	
	pci_read_config_byte (pdev, 0x3C, &tmp);
	if ((tmp & 0x0F) != pdev->irq) {
		printk (KERN_WARNING PFX "IRQ fixup, 0x3C==0x%02X\n", tmp);
		tmp &= 0xF0;
		tmp |= pdev->irq;
		pci_write_config_byte (pdev, 0x3C, tmp);
		DPRINTK("new 0x3c==0x%02x\n", tmp);
	} else {
		DPRINTK("IRQ reg 0x3c==0x%02x, irq==%d\n",
			tmp, tmp & 0x0F);
	}

	printk (KERN_INFO PFX "board #%d at 0x%04lX, IRQ %d\n",
		card->card_num + 1, card->baseaddr, pdev->irq);
	
	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out_have_proc:
	via_card_cleanup_proc (card);

err_out_have_dsp:
	via_dsp_cleanup (card);

err_out_have_mixer:
	via_ac97_cleanup (card);

err_out_kfree:
#ifndef VIA_NDEBUG
	memset (card, 0xAB, sizeof (*card)); /* poison memory */
#endif
	kfree (card);

err_out_none:
	release_region (pci_resource_start (pdev, 0), pci_resource_len (pdev, 0));
err_out:
	pdev->driver_data = NULL;
	DPRINTK ("EXIT - returning %d\n", rc);
	return rc;
}


static void __exit via_remove_one (struct pci_dev *pdev)
{
	struct via_info *card;
	
	DPRINTK ("ENTER\n");
	
	assert (pdev != NULL);
	card = pdev->driver_data;
	assert (card != NULL);
	
	via_interrupt_cleanup (card);
	via_card_cleanup_proc (card);
	via_dsp_cleanup (card);

	release_region (pci_resource_start (pdev, 0), pci_resource_len (pdev, 0));

#ifndef VIA_NDEBUG
	memset (card, 0xAB, sizeof (*card)); /* poison memory */
#endif
	kfree (card);

	pdev->driver_data = NULL;
	
	pci_set_power_state (pdev, 3); /* ...zzzzzz */

	DPRINTK ("EXIT\n");
	return;
}


/****************************************************************
 *
 * Driver initialization and cleanup
 *
 *
 */

static int __init init_via82cxxx_audio(void)
{
	int rc;

	DPRINTK ("ENTER\n");
	
	MOD_INC_USE_COUNT;

	rc = via_init_proc ();
	if (rc) {
		MOD_DEC_USE_COUNT;
		DPRINTK ("EXIT, returning %d\n", rc);
		return rc;
	}

	rc = pci_register_driver (&via_driver);
	if (rc < 1) {
		if (rc == 0)
			pci_unregister_driver (&via_driver);
		via_cleanup_proc ();
		MOD_DEC_USE_COUNT;
		DPRINTK ("EXIT, returning -ENODEV\n");
		return -ENODEV;
	}

	MOD_DEC_USE_COUNT;

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}

 
static void __exit cleanup_via82cxxx_audio(void)
{
	DPRINTK("ENTER\n");
	
	pci_unregister_driver (&via_driver);
	via_cleanup_proc ();

	DPRINTK("EXIT\n");
}


module_init(init_via82cxxx_audio);
module_exit(cleanup_via82cxxx_audio);
