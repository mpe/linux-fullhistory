/*
 *	Intel i810 and friends ICH driver for Linux
 *	Alan Cox <alan@redhat.com>
 *
 *  Built from:
 *	Low level code:  Zach Brown (original nonworking i810 OSS driver)
 *			 Jaroslav Kysela <perex@suse.cz> (working ALSA driver)
 *
 *	Framework: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *	Extended by: Zach Brown <zab@redhat.com>  
 *			and others..
 *
 *  Hardware Provided By:
 *	Analog Devices (A major AC97 codec maker)
 *	Intel Corp  (you've probably heard of them already)
 *
 * AC97 clues and assistance provided by
 *	Analog Devices
 *	Zach 'Fufu' Brown
 *	Jeff Garzik
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	Intel 810 theory of operation
 *
 *	The chipset provides three DMA channels that talk to an AC97
 *	CODEC (AC97 is a digital/analog mixer standard). At its simplest
 *	you get 48Khz audio with basic volume and mixer controls. At the
 *	best you get rate adaption in the codec. We set the card up so
 *	that we never take completion interrupts but instead keep the card
 *	chasing its tail around a ring buffer. This is needed for mmap
 *	mode audio and happens to work rather well for non-mmap modes too.
 *
 *	The board has one output channel for PCM audio (supported) and
 *	a stereo line in and mono microphone input. Again these are normally
 *	locked to 48Khz only. Right now recording is not finished.
 *
 *	There is no midi support, no synth support. Use timidity. To get
 *	esd working you need to use esd -r 48000 as it won't probe 48KHz
 *	by default. mpg123 can't handle 48Khz only audio so use xmms.
 *
 *	Fix The Sound On Dell
 *
 *	Not everyone uses 48KHz. We know of no way to detect this reliably
 *	and certainly not to get the right data. If your i810 audio sounds
 *	stupid you may need to investigate other speeds. According to Analog
 *	they tend to use a 14.318MHz clock which gives you a base rate of
 *	41194Hz.
 *
 *	This is available via the 'ftsodell=1' option. 
 *
 *	If you need to force a specific rate set the clocking= option
 */
 
#include <linux/module.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/slab.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/ac97_codec.h>
#include <linux/wrapper.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>

#ifndef PCI_DEVICE_ID_INTEL_82801
#define PCI_DEVICE_ID_INTEL_82801	0x2415
#endif
#ifndef PCI_DEVICE_ID_INTEL_82901
#define PCI_DEVICE_ID_INTEL_82901	0x2425
#endif
#ifndef PCI_DEVICE_ID_INTEL_ICH2
#define PCI_DEVICE_ID_INTEL_ICH2	0x2445
#endif
#ifndef PCI_DEVICE_ID_INTEL_ICH3
#define PCI_DEVICE_ID_INTEL_ICH3	0x2485
#endif
#ifndef PCI_DEVICE_ID_INTEL_440MX
#define PCI_DEVICE_ID_INTEL_440MX	0x7195
#endif

static int ftsodell=0;
static int strict_clocking=0;
static unsigned int clocking=48000;
static int spdif_locked=0;

//#define DEBUG
//#define DEBUG2
//#define DEBUG_INTERRUPTS

#define ADC_RUNNING	1
#define DAC_RUNNING	2

#define I810_FMT_16BIT	1
#define I810_FMT_STEREO	2
#define I810_FMT_MASK	3

#define SPDIF_ON	0x0004
#define SURR_ON		0x0010
#define CENTER_LFE_ON	0x0020
#define VOL_MUTED	0x8000

/* the 810's array of pointers to data buffers */

struct sg_item {
#define BUSADDR_MASK	0xFFFFFFFE
	u32 busaddr;	
#define CON_IOC 	0x80000000 /* interrupt on completion */
#define CON_BUFPAD	0x40000000 /* pad underrun with last sample, else 0 */
#define CON_BUFLEN_MASK	0x0000ffff /* buffer length in samples */
	u32 control;
};

/* an instance of the i810 channel */
#define SG_LEN 32
struct i810_channel 
{
	/* these sg guys should probably be allocated
	   seperately as nocache. Must be 8 byte aligned */
	struct sg_item sg[SG_LEN];	/* 32*8 */
	u32 offset;			/* 4 */
	u32 port;			/* 4 */
	u32 used;
	u32 num;
};

/*
 * we have 3 seperate dma engines.  pcm in, pcm out, and mic.
 * each dma engine has controlling registers.  These goofy
 * names are from the datasheet, but make it easy to write
 * code while leafing through it.
 */

#define ENUM_ENGINE(PRE,DIG) 									\
enum {												\
	PRE##_BDBAR =	0x##DIG##0,		/* Buffer Descriptor list Base Address */	\
	PRE##_CIV =	0x##DIG##4,		/* Current Index Value */			\
	PRE##_LVI =	0x##DIG##5,		/* Last Valid Index */				\
	PRE##_SR =	0x##DIG##6,		/* Status Register */				\
	PRE##_PICB =	0x##DIG##8,		/* Position In Current Buffer */		\
	PRE##_PIV =	0x##DIG##a,		/* Prefetched Index Value */			\
	PRE##_CR =	0x##DIG##b		/* Control Register */				\
}

ENUM_ENGINE(OFF,0);	/* Offsets */
ENUM_ENGINE(PI,0);	/* PCM In */
ENUM_ENGINE(PO,1);	/* PCM Out */
ENUM_ENGINE(MC,2);	/* Mic In */

enum {
	GLOB_CNT =	0x2c,			/* Global Control */
	GLOB_STA = 	0x30,			/* Global Status */
	CAS	 = 	0x34			/* Codec Write Semaphore Register */
};

/* interrupts for a dma engine */
#define DMA_INT_FIFO		(1<<4)  /* fifo under/over flow */
#define DMA_INT_COMPLETE	(1<<3)  /* buffer read/write complete and ioc set */
#define DMA_INT_LVI		(1<<2)  /* last valid done */
#define DMA_INT_CELV		(1<<1)  /* last valid is current */
#define DMA_INT_DCH		(1)	/* DMA Controller Halted (happens on LVI interrupts) */
#define DMA_INT_MASK (DMA_INT_FIFO|DMA_INT_COMPLETE|DMA_INT_LVI)

/* interrupts for the whole chip */
#define INT_SEC		(1<<11)
#define INT_PRI		(1<<10)
#define INT_MC		(1<<7)
#define INT_PO		(1<<6)
#define INT_PI		(1<<5)
#define INT_MO		(1<<2)
#define INT_NI		(1<<1)
#define INT_GPI		(1<<0)
#define INT_MASK (INT_SEC|INT_PRI|INT_MC|INT_PO|INT_PI|INT_MO|INT_NI|INT_GPI)


#define DRIVER_VERSION "0.04"

/* magic numbers to protect our data structures */
#define I810_CARD_MAGIC		0x5072696E /* "Prin" */
#define I810_STATE_MAGIC	0x63657373 /* "cess" */
#define I810_DMA_MASK		0xffffffff /* DMA buffer mask for pci_alloc_consist */
#define NR_HW_CH		3

/* maxinum number of AC97 codecs connected, AC97 2.0 defined 4 */
#define NR_AC97		2

/* Please note that an 8bit mono stream is not valid on this card, you must have a 16bit */
/* stream at a minimum for this card to be happy */
static const unsigned sample_size[] = { 1, 2, 2, 4 };
/* Samples are 16bit values, so we are shifting to a word, not to a byte, hence shift */
/* values are one less than might be expected */
static const unsigned sample_shift[] = { -1, 0, 0, 1 };

enum {
	ICH82801AA = 0,
	ICH82901AB,
	INTEL440MX,
	INTELICH2,
	INTELICH3
};

static char * card_names[] = {
	"Intel ICH 82801AA",
	"Intel ICH 82901AB",
	"Intel 440MX",
	"Intel ICH2",
	"Intel ICH3"
};

static struct pci_device_id i810_pci_tbl [] __initdata = {
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, ICH82801AA},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82901,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, ICH82901AB},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_440MX,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, INTEL440MX},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, INTELICH2},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH3,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, INTELICH3},
	{0,}
};

MODULE_DEVICE_TABLE (pci, i810_pci_tbl);

#ifdef CONFIG_PM
#define PM_SUSPENDED(card) (card->pm_suspended)
#else
#define PM_SUSPENDED(card) (0)
#endif

/* "software" or virtual channel, an instance of opened /dev/dsp */
struct i810_state {
	unsigned int magic;
	struct i810_card *card;	/* Card info */

	/* single open lock mechanism, only used for recording */
	struct semaphore open_sem;
	wait_queue_head_t open_wait;

	/* file mode */
	mode_t open_mode;

	/* virtual channel number */
	int virt;

#ifdef CONFIG_PM
	unsigned int pm_saved_dac_rate,pm_saved_adc_rate;
#endif
	struct dmabuf {
		/* wave sample stuff */
		unsigned int rate;
		unsigned char fmt, enable, trigger;

		/* hardware channel */
		struct i810_channel *read_channel;
		struct i810_channel *write_channel;

		/* OSS buffer management stuff */
		void *rawbuf;
		dma_addr_t dma_handle;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;

		/* our buffer acts like a circular ring */
		unsigned hwptr;		/* where dma last started, updated by update_ptr */
		unsigned swptr;		/* where driver last clear/filled, updated by read/write */
		int count;		/* bytes to be consumed or been generated by dma machine */
		unsigned total_bytes;	/* total bytes dmaed by hardware */

		unsigned error;		/* number of over/underruns */
		wait_queue_head_t wait;	/* put process on wait queue when no more space in buffer */

		/* redundant, but makes calculations easier */
		/* what the hardware uses */
		unsigned dmasize;
		unsigned fragsize;
		unsigned fragsamples;

		/* what we tell the user to expect */
		unsigned userfrags;
		unsigned userfragsize;

		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned update_flag;
		unsigned ossfragsize;
		unsigned ossmaxfrags;
		unsigned subdivision;
	} dmabuf;
};


struct i810_card {
	struct i810_channel channel[3];
	unsigned int magic;

	/* We keep i810 cards in a linked list */
	struct i810_card *next;

	/* The i810 has a certain amount of cross channel interaction
	   so we use a single per card lock */
	spinlock_t lock;

	/* PCI device stuff */
	struct pci_dev * pci_dev;
	u16 pci_id;
#ifdef CONFIG_PM	
	u16 pm_suspended;
	u32 pm_save_state[64/sizeof(u32)];
	int pm_saved_mixer_settings[SOUND_MIXER_NRDEVICES][NR_AC97];
#endif
	/* soundcore stuff */
	int dev_audio;

	/* structures for abstraction of hardware facilities, codecs, banks and channels*/
	struct ac97_codec *ac97_codec[NR_AC97];
	struct i810_state *states[NR_HW_CH];

	u16 ac97_features;
	u16 ac97_status;
	u16 channels;
	
	/* hardware resources */
	unsigned long iobase;
	unsigned long ac97base;
	u32 irq;
	
	/* Function support */
	struct i810_channel *(*alloc_pcm_channel)(struct i810_card *);
	struct i810_channel *(*alloc_rec_pcm_channel)(struct i810_card *);
	struct i810_channel *(*alloc_rec_mic_channel)(struct i810_card *);
	void (*free_pcm_channel)(struct i810_card *, int chan);
};

static struct i810_card *devs = NULL;

static int i810_open_mixdev(struct inode *inode, struct file *file);
static int i810_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd,
				unsigned long arg);

static inline unsigned ld2(unsigned int x)
{
	unsigned r = 0;
	
	if (x >= 0x10000) {
		x >>= 16;
		r += 16;
	}
	if (x >= 0x100) {
		x >>= 8;
		r += 8;
	}
	if (x >= 0x10) {
		x >>= 4;
		r += 4;
	}
	if (x >= 4) {
		x >>= 2;
		r += 2;
	}
	if (x >= 2)
		r++;
	return r;
}

static u16 i810_ac97_get(struct ac97_codec *dev, u8 reg);
static void i810_ac97_set(struct ac97_codec *dev, u8 reg, u16 data);

static struct i810_channel *i810_alloc_pcm_channel(struct i810_card *card)
{
	if(card->channel[1].used==1)
		return NULL;
	card->channel[1].used=1;
	return &card->channel[1];
}

static struct i810_channel *i810_alloc_rec_pcm_channel(struct i810_card *card)
{
	if(card->channel[0].used==1)
		return NULL;
	card->channel[0].used=1;
	return &card->channel[0];
}

static struct i810_channel *i810_alloc_rec_mic_channel(struct i810_card *card)
{
	if(card->channel[2].used==1)
		return NULL;
	card->channel[2].used=1;
	return &card->channel[2];
}

static void i810_free_pcm_channel(struct i810_card *card, int channel)
{
	card->channel[channel].used=0;
}

static int i810_valid_spdif_rate ( struct ac97_codec *codec, int rate )
{
	unsigned long id = 0L;

	id = (i810_ac97_get(codec, AC97_VENDOR_ID1) << 16);
	id |= i810_ac97_get(codec, AC97_VENDOR_ID2) & 0xffff;
#ifdef DEBUG
	printk ( "i810_audio: codec = %s, codec_id = 0x%08lx\n", codec->name, id);
#endif
	switch ( id ) {
		case 0x41445361: /* AD1886 */
			if (rate == 48000) {
				return 1;
			}
			break;
		default: /* all other codecs, until we know otherwiae */
			if (rate == 48000 || rate == 44100 || rate == 32000) {
				return 1;
			}
			break;
	}
	return (0);
}

/* i810_set_spdif_output
 * 
 *  Configure the S/PDIF output transmitter. When we turn on
 *  S/PDIF, we turn off the analog output. This may not be
 *  the right thing to do.
 *
 *  Assumptions:
 *     The DSP sample rate must already be set to a supported
 *     S/PDIF rate (32kHz, 44.1kHz, or 48kHz) or we abort.
 */
static void i810_set_spdif_output(struct i810_state *state, int slots, int rate)
{
	int	vol;
	int	aud_reg;
	struct ac97_codec *codec = state->card->ac97_codec[0];

	if(!(state->card->ac97_features & 4)) {
#ifdef DEBUG
		printk(KERN_WARNING "i810_audio: S/PDIF transmitter not available.\n");
#endif
		state->card->ac97_status &= ~SPDIF_ON;
	} else {
		if ( slots == -1 ) { /* Turn off S/PDIF */
			aud_reg = i810_ac97_get(codec, AC97_EXTENDED_STATUS);
			i810_ac97_set(codec, AC97_EXTENDED_STATUS, (aud_reg & ~AC97_EA_SPDIF));

			/* If the volume wasn't muted before we turned on S/PDIF, unmute it */
			if ( !(state->card->ac97_status & VOL_MUTED) ) {
				aud_reg = i810_ac97_get(codec, AC97_MASTER_VOL_STEREO);
				i810_ac97_set(codec, AC97_MASTER_VOL_STEREO, (aud_reg & ~VOL_MUTED));
			}
			state->card->ac97_status &= ~(VOL_MUTED | SPDIF_ON);
			return;
		}

		vol = i810_ac97_get(codec, AC97_MASTER_VOL_STEREO);
		state->card->ac97_status = vol & VOL_MUTED;

		/* Set S/PDIF transmitter sample rate */
		aud_reg = i810_ac97_get(codec, AC97_SPDIF_CONTROL);
		switch ( rate ) {
			case 32000:
				aud_reg = (aud_reg & AC97_SC_SPSR_MASK) | AC97_SC_SPSR_32K; 
				break;
			 case 44100:
			 	aud_reg = (aud_reg & AC97_SC_SPSR_MASK) | AC97_SC_SPSR_44K; 
				break;
			case 48000:
			 	aud_reg = (aud_reg & AC97_SC_SPSR_MASK) | AC97_SC_SPSR_48K; 
				break;
			default:
#ifdef DEBUG
				printk(KERN_WARNING "i810_audio: %d sample rate not supported by S/PDIF.\n", rate);
#endif
				/* turn off S/PDIF */
				aud_reg = i810_ac97_get(codec, AC97_EXTENDED_STATUS);
				i810_ac97_set(codec, AC97_EXTENDED_STATUS, (aud_reg & ~AC97_EA_SPDIF));
				state->card->ac97_status &= ~SPDIF_ON;
				return;
		}

		i810_ac97_set(codec, AC97_SPDIF_CONTROL, aud_reg);
		
		aud_reg = i810_ac97_get(codec, AC97_EXTENDED_STATUS);
		aud_reg = (aud_reg & AC97_EA_SLOT_MASK) | slots | AC97_EA_VRA | AC97_EA_SPDIF;
		i810_ac97_set(codec, AC97_EXTENDED_STATUS, aud_reg);
		state->card->ac97_status |= SPDIF_ON;

		/* Check to make sure the configuration is valid */
		aud_reg = i810_ac97_get(codec, AC97_EXTENDED_STATUS);
		if ( ! (aud_reg & 0x0400) ) {
#ifdef DEBUG
			printk(KERN_WARNING "i810_audio: S/PDIF transmitter configuration not valid (0x%04x).\n", aud_reg);
#endif

			/* turn off S/PDIF */
			i810_ac97_set(codec, AC97_EXTENDED_STATUS, (aud_reg & ~AC97_EA_SPDIF));
			state->card->ac97_status &= ~SPDIF_ON;
			return;
		}
		/* Mute the analog output */
		/* Should this only mute the PCM volume??? */
		i810_ac97_set(codec, AC97_MASTER_VOL_STEREO, (vol | VOL_MUTED));
	}
}

/* i810_set_dac_channels
 *
 *  Configure the codec's multi-channel DACs
 *
 *  The logic is backwards. Setting the bit to 1 turns off the DAC. 
 *
 *  What about the ICH? We currently configure it using the
 *  SNDCTL_DSP_CHANNELS ioctl.  If we're turnning on the DAC, 
 *  does that imply that we want the ICH set to support
 *  these channels?
 *  
 *  TODO:
 *    vailidate that the codec really supports these DACs
 *    before turning them on. 
 */
static void i810_set_dac_channels(struct i810_state *state, int channel)
{
	int	aud_reg;
	struct ac97_codec *codec = state->card->ac97_codec[0];

	aud_reg = i810_ac97_get(codec, AC97_EXTENDED_STATUS);
	aud_reg |= AC97_EA_PRI | AC97_EA_PRJ | AC97_EA_PRK;
	state->card->ac97_status &= ~(SURR_ON | CENTER_LFE_ON);

	switch ( channel ) {
		case 2: /* always enabled */
			break;
		case 4:
			aud_reg &= ~AC97_EA_PRJ;
			state->card->ac97_status |= SURR_ON;
			break;
		case 6:
			aud_reg &= ~(AC97_EA_PRJ | AC97_EA_PRI | AC97_EA_PRK);
			state->card->ac97_status |= SURR_ON | CENTER_LFE_ON;
			break;
		default:
			break;
	}
	i810_ac97_set(codec, AC97_EXTENDED_STATUS, aud_reg);

}


/* set playback sample rate */
static unsigned int i810_set_dac_rate(struct i810_state * state, unsigned int rate)
{	
	struct dmabuf *dmabuf = &state->dmabuf;
	u32 new_rate;
	struct ac97_codec *codec=state->card->ac97_codec[0];
	
	if(!(state->card->ac97_features&0x0001))
	{
		dmabuf->rate = clocking;
#ifdef DEBUG
		printk("Asked for %d Hz, but ac97_features says we only do %dHz.  Sorry!\n",
		       rate,clocking);
#endif		       
		return clocking;
	}
			
	if (rate > 48000)
		rate = 48000;
	if (rate < 8000)
		rate = 8000;
	dmabuf->rate = rate;
		
	/*
	 *	Adjust for misclocked crap
	 */
	rate = ( rate * clocking)/48000;
	if(strict_clocking && rate < 8000) {
		rate = 8000;
		dmabuf->rate = (rate * 48000)/clocking;
	}
	
	new_rate = ac97_set_dac_rate(codec, rate);

	if(new_rate != rate) {
		dmabuf->rate = (new_rate * 48000)/clocking;
	}
#ifdef DEBUG
	printk("i810_audio: called i810_set_dac_rate : asked for %d, got %d\n", rate, dmabuf->rate);
#endif
	rate = new_rate;
	return dmabuf->rate;
}

/* set recording sample rate */
static unsigned int i810_set_adc_rate(struct i810_state * state, unsigned int rate)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	u32 new_rate;
	struct ac97_codec *codec=state->card->ac97_codec[0];
	
	if(!(state->card->ac97_features&0x0001))
	{
		dmabuf->rate = clocking;
		return clocking;
	}
			
	if (rate > 48000)
		rate = 48000;
	if (rate < 8000)
		rate = 8000;
	dmabuf->rate = rate;

	/*
	 *	Adjust for misclocked crap
	 */
	 
	rate = ( rate * clocking)/48000;
	if(strict_clocking && rate < 8000) {
		rate = 8000;
		dmabuf->rate = (rate * 48000)/clocking;
	}

	new_rate = ac97_set_adc_rate(codec, rate);
	
	if(new_rate != rate) {
		dmabuf->rate = (new_rate * 48000)/clocking;
		rate = new_rate;
	}
#ifdef DEBUG
	printk("i810_audio: called i810_set_adc_rate : rate = %d/%d\n", dmabuf->rate, rate);
#endif
	return dmabuf->rate;
}

/* get current playback/recording dma buffer pointer (byte offset from LBA),
   called with spinlock held! */
   
static inline unsigned i810_get_dma_addr(struct i810_state *state, int rec)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int civ, offset;
	struct i810_channel *c;
	
	if (!dmabuf->enable)
		return 0;
	if (rec)
		c = dmabuf->read_channel;
	else
		c = dmabuf->write_channel;
	do {
		civ = inb(state->card->iobase+c->port+OFF_CIV);
		offset = (civ + 1) * dmabuf->fragsize -
			      2 * inw(state->card->iobase+c->port+OFF_PICB);
		/* CIV changed before we read PICB (very seldom) ?
		 * then PICB was rubbish, so try again */
	} while (civ != inb(state->card->iobase+c->port+OFF_CIV));
		 
	return offset;
}

//static void resync_dma_ptrs(struct i810_state *state, int rec)
//{
//	struct dmabuf *dmabuf = &state->dmabuf;
//	struct i810_channel *c;
//	int offset;
//
//	if(rec) {
//		c = dmabuf->read_channel;
//	} else {
//		c = dmabuf->write_channel;
//	}
//	if(c==NULL)
//		return;
//	offset = inb(state->card->iobase+c->port+OFF_CIV);
//	if(offset == inb(state->card->iobase+c->port+OFF_LVI))
//		offset++;
//	offset *= dmabuf->fragsize;
//	
//	dmabuf->hwptr=dmabuf->swptr = offset;
//}
	
/* Stop recording (lock held) */
static inline void __stop_adc(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct i810_card *card = state->card;

	dmabuf->enable &= ~ADC_RUNNING;
	outb(0, card->iobase + PI_CR);
	// wait for the card to acknowledge shutdown
	while( inb(card->iobase + PI_CR) != 0 ) ;
	// now clear any latent interrupt bits (like the halt bit)
	outb( inb(card->iobase + PI_SR), card->iobase + PI_SR );
	outl( inl(card->iobase + GLOB_STA) & INT_PI, card->iobase + GLOB_STA);
}

static void stop_adc(struct i810_state *state)
{
	struct i810_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	__stop_adc(state);
	spin_unlock_irqrestore(&card->lock, flags);
}

static void start_adc(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct i810_card *card = state->card;
	unsigned long flags;

	if (dmabuf->count < dmabuf->dmasize && dmabuf->ready && !dmabuf->enable &&
	    (dmabuf->trigger & PCM_ENABLE_INPUT)) {
		spin_lock_irqsave(&card->lock, flags);
		dmabuf->enable |= ADC_RUNNING;
		outb((1<<4) | (1<<2) | 1, card->iobase + PI_CR);
		spin_unlock_irqrestore(&card->lock, flags);
	}
}

/* stop playback (lock held) */
static inline void __stop_dac(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct i810_card *card = state->card;

	dmabuf->enable &= ~DAC_RUNNING;
	outb(0, card->iobase + PO_CR);
	// wait for the card to acknowledge shutdown
	while( inb(card->iobase + PO_CR) != 0 ) ;
	// now clear any latent interrupt bits (like the halt bit)
	outb( inb(card->iobase + PO_SR), card->iobase + PO_SR );
	outl( inl(card->iobase + GLOB_STA) & INT_PO, card->iobase + GLOB_STA);
}

static void stop_dac(struct i810_state *state)
{
	struct i810_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	__stop_dac(state);
	spin_unlock_irqrestore(&card->lock, flags);
}	

static void start_dac(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct i810_card *card = state->card;
	unsigned long flags;

	if (dmabuf->count > 0 && dmabuf->ready && !dmabuf->enable &&
	    (dmabuf->trigger & PCM_ENABLE_OUTPUT)) {
		spin_lock_irqsave(&card->lock, flags);
		dmabuf->enable |= DAC_RUNNING;
		outb((1<<4) | (1<<2) | 1, card->iobase + PO_CR);
		spin_unlock_irqrestore(&card->lock, flags);
	}
}

#define DMABUF_DEFAULTORDER (16-PAGE_SHIFT)
#define DMABUF_MINORDER 1

/* allocate DMA buffer, playback and recording buffer should be allocated seperately */
static int alloc_dmabuf(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	void *rawbuf= NULL;
	int order, size;
	struct page *page, *pend;

	/* If we don't have any oss frag params, then use our default ones */
	if(dmabuf->ossmaxfrags == 0)
		dmabuf->ossmaxfrags = 4;
	if(dmabuf->ossfragsize == 0)
		dmabuf->ossfragsize = (PAGE_SIZE<<DMABUF_DEFAULTORDER)/dmabuf->ossmaxfrags;
	size = dmabuf->ossfragsize * dmabuf->ossmaxfrags;

	/* alloc enough to satisfy the oss params */
	for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER; order--) {
		if ( (PAGE_SIZE<<order) > size )
			continue;
		if ((rawbuf = pci_alloc_consistent(state->card->pci_dev,
						   PAGE_SIZE << order,
						   &dmabuf->dma_handle)))
			break;
	}
	if (!rawbuf)
		return -ENOMEM;


#ifdef DEBUG
	printk("i810_audio: allocated %ld (order = %d) bytes at %p\n",
	       PAGE_SIZE << order, order, rawbuf);
#endif

	dmabuf->ready  = dmabuf->mapped = 0;
	dmabuf->rawbuf = rawbuf;
	dmabuf->buforder = order;
	
	/* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
	pend = virt_to_page(rawbuf + (PAGE_SIZE << order) - 1);
	for (page = virt_to_page(rawbuf); page <= pend; page++)
		mem_map_reserve(page);

	return 0;
}

/* free DMA buffer */
static void dealloc_dmabuf(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct page *page, *pend;

	if (dmabuf->rawbuf) {
		/* undo marking the pages as reserved */
		pend = virt_to_page(dmabuf->rawbuf + (PAGE_SIZE << dmabuf->buforder) - 1);
		for (page = virt_to_page(dmabuf->rawbuf); page <= pend; page++)
			mem_map_unreserve(page);
		pci_free_consistent(state->card->pci_dev, PAGE_SIZE << dmabuf->buforder,
				    dmabuf->rawbuf, dmabuf->dma_handle);
	}
	dmabuf->rawbuf = NULL;
	dmabuf->mapped = dmabuf->ready = 0;
}

static int prog_dmabuf(struct i810_state *state, unsigned rec)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct i810_channel *c;
	struct sg_item *sg;
	unsigned long flags;
	int ret;
	unsigned fragint;
	int i;

	spin_lock_irqsave(&state->card->lock, flags);
	if(dmabuf->enable & DAC_RUNNING)
		__stop_dac(state);
	if(dmabuf->enable & ADC_RUNNING)
		__stop_adc(state);
	dmabuf->total_bytes = 0;
	dmabuf->count = dmabuf->error = 0;
	dmabuf->swptr = dmabuf->hwptr = 0;
	spin_unlock_irqrestore(&state->card->lock, flags);

	/* allocate DMA buffer if not allocated yet */
	if (dmabuf->rawbuf)
		dealloc_dmabuf(state);
	if ((ret = alloc_dmabuf(state)))
		return ret;

	/* FIXME: figure out all this OSS fragment stuff */
	/* I did, it now does what it should according to the OSS API.  DL */
	dmabuf->dmasize = PAGE_SIZE << dmabuf->buforder;
	dmabuf->numfrag = SG_LEN;
	dmabuf->fragsize = dmabuf->dmasize/dmabuf->numfrag;
	dmabuf->fragsamples = dmabuf->fragsize >> 1;
	dmabuf->userfragsize = dmabuf->ossfragsize;
	dmabuf->userfrags = dmabuf->dmasize/dmabuf->ossfragsize;

	memset(dmabuf->rawbuf, 0, dmabuf->dmasize);

	if(dmabuf->ossmaxfrags == 4) {
		fragint = 8;
		dmabuf->fragshift = 2;
	} else if (dmabuf->ossmaxfrags == 8) {
		fragint = 4;
		dmabuf->fragshift = 3;
	} else if (dmabuf->ossmaxfrags == 16) {
		fragint = 2;
		dmabuf->fragshift = 4;
	} else {
		fragint = 1;
		dmabuf->fragshift = 5;
	}
	/*
	 *	Now set up the ring 
	 */
	if(dmabuf->read_channel)
		c = dmabuf->read_channel;
	else
		c = dmabuf->write_channel;
	while(c != NULL) {
		sg=&c->sg[0];
		/*
		 *	Load up 32 sg entries and take an interrupt at half
		 *	way (we might want more interrupts later..) 
		 */
	  
		for(i=0;i<dmabuf->numfrag;i++)
		{
			sg->busaddr=virt_to_bus(dmabuf->rawbuf+dmabuf->fragsize*i);
			// the card will always be doing 16bit stereo
			sg->control=dmabuf->fragsamples;
			sg->control|=CON_BUFPAD;
			// set us up to get IOC interrupts as often as needed to
			// satisfy numfrag requirements, no more
			if( ((i+1) % fragint) == 0) {
				sg->control|=CON_IOC;
			}
			sg++;
		}
		spin_lock_irqsave(&state->card->lock, flags);
		outb(2, state->card->iobase+c->port+OFF_CR);   /* reset DMA machine */
		outl(virt_to_bus(&c->sg[0]), state->card->iobase+c->port+OFF_BDBAR);
		outb(0, state->card->iobase+c->port+OFF_CIV);
		outb(0, state->card->iobase+c->port+OFF_LVI);
		dmabuf->count = 0;

		spin_unlock_irqrestore(&state->card->lock, flags);

		if(c != dmabuf->write_channel)
			c = dmabuf->write_channel;
		else
			c = NULL;
	}
	
	/* set the ready flag for the dma buffer */
	dmabuf->ready = 1;

#ifdef DEBUG
	printk("i810_audio: prog_dmabuf, sample rate = %d, format = %d,\n\tnumfrag = %d, "
	       "fragsize = %d dmasize = %d\n",
	       dmabuf->rate, dmabuf->fmt, dmabuf->numfrag,
	       dmabuf->fragsize, dmabuf->dmasize);
#endif

	return 0;
}

static void __i810_update_lvi(struct i810_state *state, int rec)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	int x, port;
	
	port = state->card->iobase;
	if(rec)
		port += dmabuf->read_channel->port;
	else
		port += dmabuf->write_channel->port;

	if(dmabuf->mapped) {
		if(rec)
			dmabuf->swptr = (dmabuf->hwptr + dmabuf->dmasize
				       	- dmabuf->count) % dmabuf->dmasize;
		else
			dmabuf->swptr = (dmabuf->hwptr + dmabuf->count)
			       		% dmabuf->dmasize;
	}
	/*
	 * two special cases, count == 0 on write
	 * means no data, and count == dmasize
	 * means no data on read, handle appropriately
	 */
	if(!rec && dmabuf->count == 0) {
		outb(inb(port+OFF_CIV),port+OFF_LVI);
		return;
	}
	if(rec && dmabuf->count == dmabuf->dmasize) {
		outb(inb(port+OFF_CIV),port+OFF_LVI);
		return;
	}
	/* swptr - 1 is the tail of our transfer */
	x = (dmabuf->dmasize + dmabuf->swptr - 1) % dmabuf->dmasize;
	x /= dmabuf->fragsize;
	outb(x&31, port+OFF_LVI);
}

static void i810_update_lvi(struct i810_state *state, int rec)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;

	if(!dmabuf->ready)
		return;
	spin_lock_irqsave(&state->card->lock, flags);
	__i810_update_lvi(state, rec);
	spin_unlock_irqrestore(&state->card->lock, flags);
}

/* update buffer manangement pointers, especially, dmabuf->count and dmabuf->hwptr */
static void i810_update_ptr(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned hwptr;
	int diff;

	/* error handling and process wake up for DAC */
	if (dmabuf->enable == ADC_RUNNING) {
		/* update hardware pointer */
		hwptr = i810_get_dma_addr(state, 1);
		diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;
//		printk("HWP %d,%d,%d\n", hwptr, dmabuf->hwptr, diff);
		dmabuf->hwptr = hwptr;
		dmabuf->total_bytes += diff;
		dmabuf->count += diff;
		if (dmabuf->count > dmabuf->dmasize) {
			/* buffer underrun or buffer overrun */
			/* this is normal for the end of a read */
			/* only give an error if we went past the */
			/* last valid sg entry */
			if(inb(state->card->iobase + PI_CIV) !=
			   inb(state->card->iobase + PI_LVI)) {
				printk(KERN_WARNING "i810_audio: DMA overrun on read\n");
				dmabuf->error++;
			}
		}
		if (dmabuf->count > dmabuf->userfragsize)
			wake_up(&dmabuf->wait);
	}
	/* error handling and process wake up for DAC */
	if (dmabuf->enable == DAC_RUNNING) {
		/* update hardware pointer */
		hwptr = i810_get_dma_addr(state, 0);
		diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;
//		printk("HWP %d,%d,%d\n", hwptr, dmabuf->hwptr, diff);
		dmabuf->hwptr = hwptr;
		dmabuf->total_bytes += diff;
		dmabuf->count -= diff;
		if (dmabuf->count < 0) {
			/* buffer underrun or buffer overrun */
			/* this is normal for the end of a write */
			/* only give an error if we went past the */
			/* last valid sg entry */
			if(inb(state->card->iobase + PO_CIV) !=
			   inb(state->card->iobase + PO_LVI)) {
				printk(KERN_WARNING "i810_audio: DMA overrun on write\n");
				printk("i810_audio: CIV %d, LVI %d, hwptr %x, "
					"count %d\n",
					inb(state->card->iobase + PO_CIV),
					inb(state->card->iobase + PO_LVI),
					dmabuf->hwptr, dmabuf->count);
				dmabuf->error++;
			}
		}
		if (dmabuf->count < (dmabuf->dmasize-dmabuf->userfragsize))
			wake_up(&dmabuf->wait);
	}
}

static int drain_dac(struct i810_state *state, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned long tmo;
	int count;

	if (!dmabuf->ready)
		return 0;

	add_wait_queue(&dmabuf->wait, &wait);
	for (;;) {
		/* It seems that we have to set the current state to TASK_INTERRUPTIBLE
		   every time to make the process really go to sleep */
		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_irqsave(&state->card->lock, flags);
		i810_update_ptr(state);
		count = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (count <= 0)
			break;

		if (signal_pending(current))
			break;

		i810_update_lvi(state,0);
		if (dmabuf->enable != DAC_RUNNING)
			start_dac(state);

		if (nonblock) {
			remove_wait_queue(&dmabuf->wait, &wait);
			set_current_state(TASK_RUNNING);
			return -EBUSY;
		}

		tmo = (dmabuf->dmasize * HZ) / dmabuf->rate;
		tmo >>= 1;
		if (!schedule_timeout(tmo ? tmo : 1) && tmo){
			printk(KERN_ERR "i810_audio: drain_dac, dma timeout?\n");
			break;
		}
	}
	stop_dac(state);
	synchronize_irq();
	remove_wait_queue(&dmabuf->wait, &wait);
	set_current_state(TASK_RUNNING);
	if (signal_pending(current))
		return -ERESTARTSYS;

	return 0;
}

static void i810_channel_interrupt(struct i810_card *card)
{
	int i, count;
	
#ifdef DEBUG_INTERRUPTS
	printk("CHANNEL ");
#endif
	for(i=0;i<NR_HW_CH;i++)
	{
		struct i810_state *state = card->states[i];
		struct i810_channel *c;
		struct dmabuf *dmabuf;
		unsigned long port = card->iobase;
		u16 status;
		
		if(!state)
			continue;
		if(!state->dmabuf.ready)
			continue;
		dmabuf = &state->dmabuf;
		if(dmabuf->enable & DAC_RUNNING)
			c=dmabuf->write_channel;
		else if(dmabuf->enable & ADC_RUNNING)
			c=dmabuf->read_channel;
		else	/* This can occur going from R/W to close */
			continue;
		
		port+=c->port;
		
		status = inw(port + OFF_SR);
#ifdef DEBUG_INTERRUPTS
		printk("NUM %d PORT %X IRQ ( ST%d ", c->num, c->port, status);
#endif
		if(status & DMA_INT_COMPLETE)
		{
			i810_update_ptr(state);
#ifdef DEBUG_INTERRUPTS
			printk("COMP %d ", dmabuf->hwptr /
					dmabuf->fragsize);
#endif
		}
		if(status & DMA_INT_LVI)
		{
			i810_update_ptr(state);
			wake_up(&dmabuf->wait);
#ifdef DEBUG_INTERRUPTS
			printk("LVI ");
#endif
		}
		if(status & DMA_INT_DCH)
		{
			i810_update_ptr(state);
			if(dmabuf->enable & DAC_RUNNING)
				count = dmabuf->count;
			else
				count = dmabuf->dmasize - dmabuf->count;
			if(count > 0) {
				outb(inb(port+OFF_CR) | 1, port+OFF_CR);
			} else {
				wake_up(&dmabuf->wait);
#ifdef DEBUG_INTERRUPTS
				printk("DCH - STOP ");
#endif
			}
		}
		outw(status & DMA_INT_MASK, port + OFF_SR);
	}
#ifdef DEBUG_INTERRUPTS
	printk(")\n");
#endif
}

static void i810_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct i810_card *card = (struct i810_card *)dev_id;
	u32 status;

	spin_lock(&card->lock);

	status = inl(card->iobase + GLOB_STA);

	if(!(status & INT_MASK)) 
	{
		spin_unlock(&card->lock);
		return;  /* not for us */
	}

	if(status & (INT_PO|INT_PI|INT_MC))
		i810_channel_interrupt(card);

 	/* clear 'em */
	outl(status & INT_MASK, card->iobase + GLOB_STA);
	spin_unlock(&card->lock);
}

/* in this loop, dmabuf.count signifies the amount of data that is
   waiting to be copied to the user's buffer.  It is filled by the dma
   machine and drained by this loop. */

static ssize_t i810_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct i810_card *card=state ? state->card : 0;
	struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret;
	unsigned long flags;
	unsigned int swptr;
	int cnt;
        DECLARE_WAITQUEUE(waita, current);

#ifdef DEBUG2
	printk("i810_audio: i810_read called, count = %d\n", count);
#endif

	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (dmabuf->mapped)
		return -ENXIO;
	if (dmabuf->enable & DAC_RUNNING)
		return -ENODEV;
	if (!dmabuf->read_channel) {
		dmabuf->ready = 0;
		dmabuf->read_channel = card->alloc_rec_pcm_channel(card);
		if (!dmabuf->read_channel) {
			return -EBUSY;
		}
	}
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 1)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	dmabuf->trigger &= ~PCM_ENABLE_OUTPUT;
	ret = 0;

        add_wait_queue(&dmabuf->wait, &waita);
	while (count > 0) {
		spin_lock_irqsave(&card->lock, flags);
                if (PM_SUSPENDED(card)) {
                        spin_unlock_irqrestore(&card->lock, flags);
                        set_current_state(TASK_INTERRUPTIBLE);
                        schedule();
                        if (signal_pending(current)) {
                                if (!ret) ret = -EAGAIN;
                                break;
                        }
                        continue;
                }
		swptr = dmabuf->swptr;
		if (dmabuf->count > dmabuf->dmasize) {
			dmabuf->count = dmabuf->dmasize;
		}
		cnt = dmabuf->count - dmabuf->fragsize;
		// this is to make the copy_to_user simpler below
		if(cnt > (dmabuf->dmasize - swptr))
			cnt = dmabuf->dmasize - swptr;
		spin_unlock_irqrestore(&card->lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			unsigned long tmo;
			if(!dmabuf->enable) {
				dmabuf->trigger |= PCM_ENABLE_INPUT;
				start_adc(state);
			}
			i810_update_lvi(state,1);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret) ret = -EAGAIN;
				return ret;
			}
			/* This isnt strictly right for the 810  but it'll do */
			tmo = (dmabuf->dmasize * HZ) / (dmabuf->rate * 2);
			tmo >>= 1;
			/* There are two situations when sleep_on_timeout returns, one is when
			   the interrupt is serviced correctly and the process is waked up by
			   ISR ON TIME. Another is when timeout is expired, which means that
			   either interrupt is NOT serviced correctly (pending interrupt) or it
			   is TOO LATE for the process to be scheduled to run (scheduler latency)
			   which results in a (potential) buffer overrun. And worse, there is
			   NOTHING we can do to prevent it. */
			if (!interruptible_sleep_on_timeout(&dmabuf->wait, tmo)) {
#ifdef DEBUG
				printk(KERN_ERR "i810_audio: recording schedule timeout, "
				       "dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       dmabuf->dmasize, dmabuf->fragsize, dmabuf->count,
				       dmabuf->hwptr, dmabuf->swptr);
#endif
				/* a buffer overrun, we delay the recovery until next time the
				   while loop begin and we REALLY have space to record */
			}
			if (signal_pending(current)) {
				ret = ret ? ret : -ERESTARTSYS;
				return ret;
			}
			continue;
		}

		if (copy_to_user(buffer, dmabuf->rawbuf + swptr, cnt)) {
			if (!ret) ret = -EFAULT;
			goto done;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;

		spin_lock_irqsave(&card->lock, flags);

                if (PM_SUSPENDED(card)) {
                        spin_unlock_irqrestore(&card->lock, flags);
                        continue;
                }
		dmabuf->swptr = swptr;
		dmabuf->count -= cnt;
		spin_unlock_irqrestore(&card->lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
	}
	i810_update_lvi(state,1);
	if(!(dmabuf->enable & ADC_RUNNING))
		start_adc(state);
 done:
        set_current_state(TASK_RUNNING);
        remove_wait_queue(&dmabuf->wait, &waita);

	return ret;
}

/* in this loop, dmabuf.count signifies the amount of data that is waiting to be dma to
   the soundcard.  it is drained by the dma machine and filled by this loop. */
static ssize_t i810_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct i810_card *card=state ? state->card : 0;
	struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret;
	unsigned long flags;
	unsigned int swptr = 0;
	int cnt, x;
        DECLARE_WAITQUEUE(waita, current);

#ifdef DEBUG2
	printk("i810_audio: i810_write called, count = %d\n", count);
#endif

	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (dmabuf->mapped)
		return -ENXIO;
	if (dmabuf->enable & ADC_RUNNING)
		return -ENODEV;
	if (!dmabuf->write_channel) {
		dmabuf->ready = 0;
		dmabuf->write_channel = card->alloc_pcm_channel(card);
		if(!dmabuf->write_channel)
			return -EBUSY;
	}
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 0)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	dmabuf->trigger &= ~PCM_ENABLE_INPUT;
	ret = 0;

        add_wait_queue(&dmabuf->wait, &waita);
	while (count > 0) {
		spin_lock_irqsave(&state->card->lock, flags);
                if (PM_SUSPENDED(card)) {
                        spin_unlock_irqrestore(&card->lock, flags);
                        set_current_state(TASK_INTERRUPTIBLE);
                        schedule();
                        if (signal_pending(current)) {
                                if (!ret) ret = -EAGAIN;
                                break;
                        }
                        continue;
                }

		swptr = dmabuf->swptr;
		if (dmabuf->count < 0) {
			dmabuf->count = 0;
		}
		cnt = dmabuf->dmasize - dmabuf->fragsize - dmabuf->count;
		// this is to make the copy_from_user simpler below
		if(cnt > (dmabuf->dmasize - swptr))
			cnt = dmabuf->dmasize - swptr;
		spin_unlock_irqrestore(&state->card->lock, flags);

#ifdef DEBUG2
		printk(KERN_INFO "i810_audio: i810_write: %d bytes available space\n", cnt);
#endif
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			unsigned long tmo;
			// There is data waiting to be played
			if(!dmabuf->enable && dmabuf->count) {
				/* force the starting incase SETTRIGGER has been used */
				/* to stop it, otherwise this is a deadlock situation */
				dmabuf->trigger |= PCM_ENABLE_OUTPUT;
				start_dac(state);
			}
			// Update the LVI pointer in case we have already
			// written data in this syscall and are just waiting
			// on the tail bit of data
			i810_update_lvi(state,0);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret) ret = -EAGAIN;
				goto ret;
			}
			/* Not strictly correct but works */
			tmo = (dmabuf->dmasize * HZ) / (dmabuf->rate * 4);
			/* There are two situations when sleep_on_timeout returns, one is when
			   the interrupt is serviced correctly and the process is waked up by
			   ISR ON TIME. Another is when timeout is expired, which means that
			   either interrupt is NOT serviced correctly (pending interrupt) or it
			   is TOO LATE for the process to be scheduled to run (scheduler latency)
			   which results in a (potential) buffer underrun. And worse, there is
			   NOTHING we can do to prevent it. */
			if (!interruptible_sleep_on_timeout(&dmabuf->wait, tmo)) {
#ifdef DEBUG
				printk(KERN_ERR "i810_audio: playback schedule timeout, "
				       "dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       dmabuf->dmasize, dmabuf->fragsize, dmabuf->count,
				       dmabuf->hwptr, dmabuf->swptr);
#endif
				/* a buffer underrun, we delay the recovery until next time the
				   while loop begin and we REALLY have data to play */
				//return ret;
			}
			if (signal_pending(current)) {
				if (!ret) ret = -ERESTARTSYS;
				goto ret;
			}
			continue;
		}
		if (copy_from_user(dmabuf->rawbuf+swptr,buffer,cnt)) {
			if (!ret) ret = -EFAULT;
			goto ret;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;

		spin_lock_irqsave(&state->card->lock, flags);
                if (PM_SUSPENDED(card)) {
                        spin_unlock_irqrestore(&card->lock, flags);
                        continue;
                }

		dmabuf->swptr = swptr;
		dmabuf->count += cnt;

		count -= cnt;
		buffer += cnt;
		ret += cnt;
		spin_unlock_irqrestore(&state->card->lock, flags);
	}
	if (swptr % dmabuf->fragsize) {
		x = dmabuf->fragsize - (swptr % dmabuf->fragsize);
		memset(dmabuf->rawbuf + swptr, '\0', x);
	}
	i810_update_lvi(state,0);
	if (!dmabuf->enable && dmabuf->count >= dmabuf->userfragsize)
		start_dac(state);
 ret:
        set_current_state(TASK_RUNNING);
        remove_wait_queue(&dmabuf->wait, &waita);

	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int i810_poll(struct file *file, struct poll_table_struct *wait)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned int mask = 0;

	if(!dmabuf->ready)
		return 0;
	poll_wait(file, &dmabuf->wait, wait);
	spin_lock_irqsave(&state->card->lock, flags);
	i810_update_ptr(state);
	if (file->f_mode & FMODE_READ && dmabuf->enable & ADC_RUNNING) {
		if (dmabuf->count >= (signed)dmabuf->fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE && dmabuf->enable & DAC_RUNNING) {
		if (dmabuf->mapped) {
			if (dmabuf->count >= (signed)dmabuf->fragsize)
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)dmabuf->dmasize >= dmabuf->count + (signed)dmabuf->fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&state->card->lock, flags);

	return mask;
}

static int i810_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	int ret = -EINVAL;
	unsigned long size;

	lock_kernel();
	if (vma->vm_flags & VM_WRITE) {
		if (!dmabuf->write_channel &&
		    (dmabuf->write_channel =
		     state->card->alloc_pcm_channel(state->card)) == NULL) {
			ret = -EBUSY;
			goto out;
		}
	}
	if (vma->vm_flags & VM_READ) {
		if (!dmabuf->read_channel &&
		    (dmabuf->read_channel = 
		     state->card->alloc_rec_pcm_channel(state->card)) == NULL) {
			ret = -EBUSY;
			goto out;
		}
	}
	if ((ret = prog_dmabuf(state, 0)) != 0)
		goto out;

	ret = -EINVAL;
	if (vma->vm_pgoff != 0)
		goto out;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << dmabuf->buforder))
		goto out;
	ret = -EAGAIN;
	if (remap_page_range(vma->vm_start, virt_to_phys(dmabuf->rawbuf),
			     size, vma->vm_page_prot))
		goto out;
	dmabuf->mapped = 1;
	if(vma->vm_flags & VM_WRITE)
		dmabuf->count = dmabuf->dmasize;
	else
		dmabuf->count = 0;
	ret = 0;
#ifdef DEBUG
	printk("i810_audio: mmap'ed %ld bytes of data space\n", size);
#endif
out:
	unlock_kernel();
	return ret;
}

static int i810_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	unsigned int i_glob_cnt;
	int val = 0, mapped, ret;
	struct ac97_codec *codec = state->card->ac97_codec[0];

	mapped = ((file->f_mode & FMODE_WRITE) && dmabuf->mapped) ||
		((file->f_mode & FMODE_READ) && dmabuf->mapped);
#ifdef DEBUG
	printk("i810_audio: i810_ioctl, arg=0x%x, cmd=", arg ? *(int *)arg : 0);
#endif

	switch (cmd) 
	{
	case OSS_GETVERSION:
#ifdef DEBUG
		printk("OSS_GETVERSION\n");
#endif
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_RESET:
#ifdef DEBUG
		printk("SNDCTL_DSP_RESET\n");
#endif
		/* FIXME: spin_lock ? */
		if (dmabuf->enable == DAC_RUNNING) {
			stop_dac(state);
		}
		if (dmabuf->enable == ADC_RUNNING) {
			stop_adc(state);
		}
		synchronize_irq();
		dmabuf->ready = 0;
		dmabuf->swptr = dmabuf->hwptr = 0;
		dmabuf->count = dmabuf->total_bytes = 0;
		return 0;

	case SNDCTL_DSP_SYNC:
#ifdef DEBUG
		printk("SNDCTL_DSP_SYNC\n");
#endif
		if (dmabuf->enable != DAC_RUNNING || file->f_flags & O_NONBLOCK)
			return 0;
		drain_dac(state, 0);
		dmabuf->ready = 0;
		dmabuf->swptr = dmabuf->hwptr = 0;
		dmabuf->count = dmabuf->total_bytes = 0;
		return 0;

	case SNDCTL_DSP_SPEED: /* set smaple rate */
#ifdef DEBUG
		printk("SNDCTL_DSP_SPEED\n");
#endif
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val >= 0) {
			if (file->f_mode & FMODE_WRITE) {
				if ( (state->card->ac97_status & SPDIF_ON) ) {  /* S/PDIF Enabled */
					/* AD1886 only supports 48000, need to check that */
					if ( i810_valid_spdif_rate ( codec, val ) ) {
						/* Set DAC rate */
                                        	i810_set_spdif_output ( state, -1, 0 );
						stop_dac(state);
						dmabuf->ready = 0;
						spin_lock_irqsave(&state->card->lock, flags);
						i810_set_dac_rate(state, val);
						spin_unlock_irqrestore(&state->card->lock, flags);
						/* Set S/PDIF transmitter rate. */
						i810_set_spdif_output ( state, AC97_EA_SPSA_3_4, val );
	                                        if ( ! (state->card->ac97_status & SPDIF_ON) ) {
							val = dmabuf->rate;
						}
					} else { /* Not a valid rate for S/PDIF, ignore it */
						val = dmabuf->rate;
					}
				} else {
					stop_dac(state);
					dmabuf->ready = 0;
					spin_lock_irqsave(&state->card->lock, flags);
					i810_set_dac_rate(state, val);
					spin_unlock_irqrestore(&state->card->lock, flags);
				}
			}
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
				dmabuf->ready = 0;
				spin_lock_irqsave(&state->card->lock, flags);
				i810_set_adc_rate(state, val);
				spin_unlock_irqrestore(&state->card->lock, flags);
			}
		}
		return put_user(dmabuf->rate, (int *)arg);

	case SNDCTL_DSP_STEREO: /* set stereo or mono channel */
#ifdef DEBUG
		printk("SNDCTL_DSP_STEREO\n");
#endif
		if (get_user(val, (int *)arg))
			return -EFAULT;

		if (dmabuf->enable & DAC_RUNNING) {
			stop_dac(state);
		}
		if (dmabuf->enable & ADC_RUNNING) {
			stop_adc(state);
		}
		return put_user(1, (int *)arg);

	case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if (!dmabuf->ready && (val = prog_dmabuf(state, 0)))
				return val;
		}
		if (file->f_mode & FMODE_READ) {
			if (!dmabuf->ready && (val = prog_dmabuf(state, 1)))
				return val;
		}
#ifdef DEBUG
		printk("SNDCTL_DSP_GETBLKSIZE %d\n", dmabuf->userfragsize);
#endif
		return put_user(dmabuf->userfragsize, (int *)arg);

	case SNDCTL_DSP_GETFMTS: /* Returns a mask of supported sample format*/
#ifdef DEBUG
		printk("SNDCTL_DSP_GETFMTS\n");
#endif
		return put_user(AFMT_S16_LE, (int *)arg);

	case SNDCTL_DSP_SETFMT: /* Select sample format */
#ifdef DEBUG
		printk("SNDCTL_DSP_SETFMT\n");
#endif
		if (get_user(val, (int *)arg))
			return -EFAULT;

		switch ( val ) {
			case AFMT_S16_LE:
				break;
			case AFMT_QUERY:
			default:
				val = AFMT_S16_LE;
				break;
		}
		return put_user(val, (int *)arg);

	case SNDCTL_DSP_CHANNELS:
#ifdef DEBUG
		printk("SNDCTL_DSP_CHANNELS\n");
#endif
		if (get_user(val, (int *)arg))
			return -EFAULT;

		if (val > 0) {
			if (dmabuf->enable & DAC_RUNNING) {
				stop_dac(state);
			}
			if (dmabuf->enable & ADC_RUNNING) {
				stop_adc(state);
			}
		} else {
			return put_user(state->card->channels, (int *)arg);
		}

		/* ICH and ICH0 only support 2 channels */
		if ( state->card->pci_id == 0x2415 || state->card->pci_id == 0x2425 ) 
			return put_user(2, (int *)arg);
	
		/* Multi-channel support was added with ICH2. Bits in */
		/* Global Status and Global Control register are now  */
		/* used to indicate this.                             */

                i_glob_cnt = inl(state->card->iobase + GLOB_CNT);

		/* Current # of channels enabled */
		if ( i_glob_cnt & 0x0100000 )
			ret = 4;
		else if ( i_glob_cnt & 0x0200000 )
			ret = 6;
		else
			ret = 2;

		switch ( val ) {
			case 2: /* 2 channels is always supported */
				outl(state->card->iobase + GLOB_CNT, (i_glob_cnt & 0xcfffff));
				/* Do we need to change mixer settings????  */
				break;
			case 4: /* Supported on some chipsets, better check first */
				if ( state->card->channels >= 4 ) {
					outl(state->card->iobase + GLOB_CNT, ((i_glob_cnt & 0xcfffff) | 0x0100000));
					/* Do we need to change mixer settings??? */
				} else {
					val = ret;
				}
				break;
			case 6: /* Supported on some chipsets, better check first */
				if ( state->card->channels >= 6 ) {
					outl(state->card->iobase + GLOB_CNT, ((i_glob_cnt & 0xcfffff) | 0x0200000));
					/* Do we need to change mixer settings??? */
				} else {
					val = ret;
				}
				break;
			default: /* nothing else is ever supported by the chipset */
				val = ret;
				break;
		}

		return put_user(val, (int *)arg);

	case SNDCTL_DSP_POST: /* the user has sent all data and is notifying us */
		/* we update the swptr to the end of the last sg segment then return */
#ifdef DEBUG
		printk("SNDCTL_DSP_POST\n");
#endif
		if(!dmabuf->ready || (dmabuf->enable != DAC_RUNNING))
			return 0;
		if((dmabuf->swptr % dmabuf->fragsize) != 0) {
			val = dmabuf->fragsize - (dmabuf->swptr % dmabuf->fragsize);
			dmabuf->swptr += val;
			dmabuf->count += val;
		}
		return 0;

	case SNDCTL_DSP_SUBDIVIDE:
		if (dmabuf->subdivision)
			return -EINVAL;
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
#ifdef DEBUG
		printk("SNDCTL_DSP_SUBDIVIDE %d\n", val);
#endif
		dmabuf->subdivision = val;
		dmabuf->ready = 0;
		return 0;

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, (int *)arg))
			return -EFAULT;

		dmabuf->ossfragsize = 1<<(val & 0xffff);
		dmabuf->ossmaxfrags = (val >> 16) & 0xffff;
		if (dmabuf->ossmaxfrags <= 4)
			dmabuf->ossmaxfrags = 4;
		else if (dmabuf->ossmaxfrags <= 8)
			dmabuf->ossmaxfrags = 8;
		else if (dmabuf->ossmaxfrags <= 16)
			dmabuf->ossmaxfrags = 16;
		else
			dmabuf->ossmaxfrags = 32;
		val = dmabuf->ossfragsize * dmabuf->ossmaxfrags;
		if (val < 16384)
			val = 16384;
		if (val > 65536)
			val = 65536;
		dmabuf->ossmaxfrags = val/dmabuf->ossfragsize;
		if(dmabuf->ossmaxfrags<4)
			dmabuf->ossfragsize = val/4;
		dmabuf->ready = 0;
#ifdef DEBUG
		printk("SNDCTL_DSP_SETFRAGMENT 0x%x, %d, %d\n", val,
			dmabuf->ossfragsize, dmabuf->ossmaxfrags);
#endif

		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!dmabuf->ready && (val = prog_dmabuf(state, 0)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		i810_update_ptr(state);
		abinfo.fragsize = dmabuf->userfragsize;
		abinfo.fragstotal = dmabuf->userfrags;
		if(dmabuf->mapped)
			abinfo.bytes = dmabuf->count;
		else
			abinfo.bytes = dmabuf->dmasize - dmabuf->count;
		abinfo.fragments = abinfo.bytes / dmabuf->userfragsize;
		spin_unlock_irqrestore(&state->card->lock, flags);
#ifdef DEBUG
		printk("SNDCTL_DSP_GETOSPACE %d, %d, %d, %d\n", abinfo.bytes,
			abinfo.fragsize, abinfo.fragments, abinfo.fragstotal);
#endif
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!dmabuf->ready && (val = prog_dmabuf(state, 0)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		i810_update_ptr(state);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.ptr = dmabuf->hwptr;
		cinfo.blocks = (dmabuf->dmasize - dmabuf->count)/dmabuf->userfragsize;
		if (dmabuf->mapped) {
			dmabuf->count = (dmabuf->dmasize - 
					 (dmabuf->count & (dmabuf->userfragsize-1)));
			__i810_update_lvi(state, 0);
		}
		spin_unlock_irqrestore(&state->card->lock, flags);
#ifdef DEBUG
		printk("SNDCTL_DSP_GETOPTR %d, %d, %d, %d\n", cinfo.bytes,
			cinfo.blocks, cinfo.ptr, dmabuf->count);
#endif
		return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!dmabuf->ready && (val = prog_dmabuf(state, 1)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		i810_update_ptr(state);
		abinfo.fragsize = dmabuf->userfragsize;
		abinfo.fragstotal = dmabuf->userfrags;
		abinfo.bytes = dmabuf->dmasize - dmabuf->count;
		abinfo.fragments = abinfo.bytes / dmabuf->userfragsize;
		spin_unlock_irqrestore(&state->card->lock, flags);
#ifdef DEBUG
		printk("SNDCTL_DSP_GETISPACE %d, %d, %d, %d\n", abinfo.bytes,
			abinfo.fragsize, abinfo.fragments, abinfo.fragstotal);
#endif
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!dmabuf->ready && (val = prog_dmabuf(state, 0)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		i810_update_ptr(state);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.blocks = dmabuf->count/dmabuf->userfragsize;
		cinfo.ptr = dmabuf->hwptr;
		if (dmabuf->mapped) {
			dmabuf->count &= (dmabuf->userfragsize-1);
			__i810_update_lvi(state, 1);
		}
		spin_unlock_irqrestore(&state->card->lock, flags);
#ifdef DEBUG
		printk("SNDCTL_DSP_GETIPTR %d, %d, %d, %d\n", cinfo.bytes,
			cinfo.blocks, cinfo.ptr, dmabuf->count);
#endif
		return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

	case SNDCTL_DSP_NONBLOCK:
#ifdef DEBUG
		printk("SNDCTL_DSP_NONBLOCK\n");
#endif
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETCAPS:
#ifdef DEBUG
		printk("SNDCTL_DSP_GETCAPS\n");
#endif
	    return put_user(DSP_CAP_REALTIME|DSP_CAP_TRIGGER|DSP_CAP_MMAP|DSP_CAP_BIND,
			    (int *)arg);

	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
#ifdef DEBUG
		printk("SNDCTL_DSP_GETTRIGGER 0x%x\n", dmabuf->trigger);
#endif
		return put_user(dmabuf->trigger, (int *)arg);

	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, (int *)arg))
			return -EFAULT;
#ifdef DEBUG
		printk("SNDCTL_DSP_SETTRIGGER 0x%x\n", val);
#endif
		if( !(val & PCM_ENABLE_INPUT) && dmabuf->enable == ADC_RUNNING) {
			stop_adc(state);
		}
		if( !(val & PCM_ENABLE_OUTPUT) && dmabuf->enable == DAC_RUNNING) {
			stop_dac(state);
		}
		dmabuf->trigger = val;
		if(val & PCM_ENABLE_OUTPUT) {
			if (!dmabuf->write_channel) {
				dmabuf->ready = 0;
				dmabuf->write_channel = state->card->alloc_pcm_channel(state->card);
				if (!dmabuf->write_channel)
					return -EBUSY;
			}
			if (!dmabuf->ready && (ret = prog_dmabuf(state, 0)))
				return ret;
			if (dmabuf->mapped) {
				dmabuf->count = dmabuf->dmasize;
				i810_update_lvi(state,0);
			}
			if (!dmabuf->enable && dmabuf->count > dmabuf->userfragsize)
				start_dac(state);
		}
		if(val & PCM_ENABLE_INPUT) {
			if (!dmabuf->read_channel) {
				dmabuf->ready = 0;
				dmabuf->read_channel = state->card->alloc_rec_pcm_channel(state->card);
				if (!dmabuf->read_channel)
					return -EBUSY;
			}
			if (!dmabuf->ready && (ret = prog_dmabuf(state, 1)))
				return ret;
			if (dmabuf->mapped) {
				dmabuf->count = 0;
				i810_update_lvi(state,1);
			}
			if (!dmabuf->enable && dmabuf->count <
			    (dmabuf->dmasize - dmabuf->userfragsize))
				start_adc(state);
		}
		return 0;

	case SNDCTL_DSP_SETDUPLEX:
#ifdef DEBUG
		printk("SNDCTL_DSP_SETDUPLEX\n");
#endif
		return -EINVAL;

	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&state->card->lock, flags);
		i810_update_ptr(state);
		val = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);
#ifdef DEBUG
		printk("SNDCTL_DSP_GETODELAY %d\n", dmabuf->count);
#endif
		return put_user(val, (int *)arg);

	case SOUND_PCM_READ_RATE:
#ifdef DEBUG
		printk("SOUND_PCM_READ_RATE %d\n", dmabuf->rate);
#endif
		return put_user(dmabuf->rate, (int *)arg);

	case SOUND_PCM_READ_CHANNELS:
#ifdef DEBUG
		printk("SOUND_PCM_READ_CHANNELS\n");
#endif
		return put_user(2, (int *)arg);

	case SOUND_PCM_READ_BITS:
#ifdef DEBUG
		printk("SOUND_PCM_READ_BITS\n");
#endif
		return put_user(AFMT_S16_LE, (int *)arg);

	case SNDCTL_DSP_SETSPDIF: /* Set S/PDIF Control register */
#ifdef DEBUG
		printk("SNDCTL_DSP_SETSPDIF\n");
#endif
		if (get_user(val, (int *)arg))
			return -EFAULT;

		/* Check to make sure the codec supports S/PDIF transmitter */

		if((state->card->ac97_features & 4)) {
			/* mask out the transmitter speed bits so the user can't set them */
			val &= ~0x3000;

			/* Add the current transmitter speed bits to the passed value */
			ret = i810_ac97_get(codec, AC97_SPDIF_CONTROL);
			val |= (ret & 0x3000);

			i810_ac97_set(codec, AC97_SPDIF_CONTROL, val);
			if(i810_ac97_get(codec, AC97_SPDIF_CONTROL) != val ) {
				printk(KERN_ERR "i810_audio: Unable to set S/PDIF configuration to 0x%04x.\n", val);
				return -EFAULT;
			}
		}
#ifdef DEBUG
		else 
			printk(KERN_WARNING "i810_audio: S/PDIF transmitter not avalible.\n");
#endif
		return put_user(val, (int *)arg);

	case SNDCTL_DSP_GETSPDIF: /* Get S/PDIF Control register */
#ifdef DEBUG
		printk("SNDCTL_DSP_GETSPDIF\n");
#endif
		if (get_user(val, (int *)arg))
			return -EFAULT;

		/* Check to make sure the codec supports S/PDIF transmitter */

		if(!(state->card->ac97_features & 4)) {
#ifdef DEBUG
			printk(KERN_WARNING "i810_audio: S/PDIF transmitter not avalible.\n");
#endif
			val = 0;
		} else {
			val = i810_ac97_get(codec, AC97_SPDIF_CONTROL);
		}
		//return put_user((val & 0xcfff), (int *)arg);
		return put_user(val, (int *)arg);
   			
	case SNDCTL_DSP_GETCHANNELMASK:
#ifdef DEBUG
		printk("SNDCTL_DSP_GETCHANNELMASK\n");
#endif
		if (get_user(val, (int *)arg))
			return -EFAULT;
		
		/* Based on AC'97 DAC support, not ICH hardware */
		val = DSP_BIND_FRONT;
		if ( state->card->ac97_features & 0x0004 )
			val |= DSP_BIND_SPDIF;

		if ( state->card->ac97_features & 0x0080 )
			val |= DSP_BIND_SURR;
		if ( state->card->ac97_features & 0x0140 )
			val |= DSP_BIND_CENTER_LFE;

		return put_user(val, (int *)arg);

	case SNDCTL_DSP_BIND_CHANNEL:
#ifdef DEBUG
		printk("SNDCTL_DSP_BIND_CHANNEL\n");
#endif
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if ( val == DSP_BIND_QUERY ) {
			val = DSP_BIND_FRONT; /* Always report this as being enabled */
			if ( state->card->ac97_status & SPDIF_ON ) 
				val |= DSP_BIND_SPDIF;
			else {
				if ( state->card->ac97_status & SURR_ON )
					val |= DSP_BIND_SURR;
				if ( state->card->ac97_status & CENTER_LFE_ON )
					val |= DSP_BIND_CENTER_LFE;
			}
		} else {  /* Not a query, set it */
			if (!(file->f_mode & FMODE_WRITE))
				return -EINVAL;
			if ( dmabuf->enable == DAC_RUNNING ) {
				stop_dac(state);
			}
			if ( val & DSP_BIND_SPDIF ) {  /* Turn on SPDIF */
				/*  Ok, this should probably define what slots
				 *  to use. For now, we'll only set it to the
				 *  defaults:
				 * 
				 *   non multichannel codec maps to slots 3&4
				 *   2 channel codec maps to slots 7&8
				 *   4 channel codec maps to slots 6&9
				 *   6 channel codec maps to slots 10&11
				 *
				 *  there should be some way for the app to
				 *  select the slot assignment.
				 */
	
				i810_set_spdif_output ( state, AC97_EA_SPSA_3_4, dmabuf->rate );
				if ( !(state->card->ac97_status & SPDIF_ON) )
					val &= ~DSP_BIND_SPDIF;
			} else {
				int mask;
				int channels;

				/* Turn off S/PDIF if it was on */
				if ( state->card->ac97_status & SPDIF_ON ) 
					i810_set_spdif_output ( state, -1, 0 );
				
				mask = val & (DSP_BIND_FRONT | DSP_BIND_SURR | DSP_BIND_CENTER_LFE);
				switch (mask) {
					case DSP_BIND_FRONT:
						channels = 2;
						break;
					case DSP_BIND_FRONT|DSP_BIND_SURR:
						channels = 4;
						break;
					case DSP_BIND_FRONT|DSP_BIND_SURR|DSP_BIND_CENTER_LFE:
						channels = 6;
						break;
					default:
						val = DSP_BIND_FRONT;
						channels = 2;
						break;
				}
				i810_set_dac_channels ( state, channels );

				/* check that they really got turned on */
				if ( !state->card->ac97_status & SURR_ON )
					val &= ~DSP_BIND_SURR;
				if ( !state->card->ac97_status & CENTER_LFE_ON )
					val &= ~DSP_BIND_CENTER_LFE;
			}
		}
		return put_user(val, (int *)arg);
		
	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_WRITE_FILTER:
	case SOUND_PCM_READ_FILTER:
#ifdef DEBUG
		printk("SNDCTL_* -EINVAL\n");
#endif
		return -EINVAL;
	}
	return -EINVAL;
}

static int i810_open(struct inode *inode, struct file *file)
{
	int i = 0;
	struct i810_card *card = devs;
	struct i810_state *state = NULL;
	struct dmabuf *dmabuf = NULL;

	/* find an avaiable virtual channel (instance of /dev/dsp) */
	while (card != NULL) {
		for (i = 0; i < NR_HW_CH; i++) {
			if (card->states[i] == NULL) {
				state = card->states[i] = (struct i810_state *)
					kmalloc(sizeof(struct i810_state), GFP_KERNEL);
				if (state == NULL)
					return -ENOMEM;
				memset(state, 0, sizeof(struct i810_state));
				dmabuf = &state->dmabuf;
				goto found_virt;
			}
		}
		card = card->next;
	}
	/* no more virtual channel avaiable */
	if (!state)
		return -ENODEV;

found_virt:
	/* initialize the virtual channel */
	state->virt = i;
	state->card = card;
	state->magic = I810_STATE_MAGIC;
	init_waitqueue_head(&dmabuf->wait);
	init_MUTEX(&state->open_sem);
	file->private_data = state;
	dmabuf->trigger = 0;

	/* allocate hardware channels */
	if(file->f_mode & FMODE_READ) {
		if((dmabuf->read_channel = card->alloc_rec_pcm_channel(card)) == NULL) {
			kfree (card->states[i]);
			card->states[i] = NULL;;
			return -EBUSY;
		}
		i810_set_adc_rate(state, 8000);
		dmabuf->trigger |= PCM_ENABLE_INPUT;
	}
	if(file->f_mode & FMODE_WRITE) {
		if((dmabuf->write_channel = card->alloc_pcm_channel(card)) == NULL) {
			kfree (card->states[i]);
			card->states[i] = NULL;;
			return -EBUSY;
		}
		/* Initialize to 8kHz?  What if we don't support 8kHz? */
		/*  Let's change this to check for S/PDIF stuff */
	
		if ( spdif_locked ) {
			i810_set_dac_rate(state, spdif_locked);
			i810_set_spdif_output(state, AC97_EA_SPSA_3_4, spdif_locked);
		} else {
			i810_set_dac_rate(state, 8000);
		}
		dmabuf->trigger |= PCM_ENABLE_OUTPUT;
	}
		
	/* set default sample format. According to OSS Programmer's Guide  /dev/dsp
	   should be default to unsigned 8-bits, mono, with sample rate 8kHz and
	   /dev/dspW will accept 16-bits sample, but we don't support those so we
	   set it immediately to stereo and 16bit, which is all we do support */
	dmabuf->fmt |= I810_FMT_16BIT | I810_FMT_STEREO;
	dmabuf->ossfragsize = 0;
	dmabuf->ossmaxfrags  = 0;
	dmabuf->subdivision  = 0;

	state->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);

	return 0;
}

static int i810_release(struct inode *inode, struct file *file)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct i810_card *card = state->card;
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;

	lock_kernel();

	/* stop DMA state machine and free DMA buffers/channels */
	if(dmabuf->enable & DAC_RUNNING ||
	   (dmabuf->count && (dmabuf->trigger & PCM_ENABLE_OUTPUT))) {
		drain_dac(state,0);
	}
	if(dmabuf->enable & ADC_RUNNING) {
		stop_adc(state);
	}
	spin_lock_irqsave(&card->lock, flags);
	dealloc_dmabuf(state);
	if (file->f_mode & FMODE_WRITE) {
		state->card->free_pcm_channel(state->card, dmabuf->write_channel->num);
	}
	if (file->f_mode & FMODE_READ) {
		state->card->free_pcm_channel(state->card, dmabuf->read_channel->num);
	}

	state->card->states[state->virt] = NULL;
	kfree(state);
	spin_unlock_irqrestore(&card->lock, flags);
	unlock_kernel();

	return 0;
}

static /*const*/ struct file_operations i810_audio_fops = {
	owner:		THIS_MODULE,
	llseek:		no_llseek,
	read:		i810_read,
	write:		i810_write,
	poll:		i810_poll,
	ioctl:		i810_ioctl,
	mmap:		i810_mmap,
	open:		i810_open,
	release:	i810_release,
};

/* Write AC97 codec registers */

static u16 i810_ac97_get(struct ac97_codec *dev, u8 reg)
{
	struct i810_card *card = dev->private_data;
	int count = 100;
	u8 reg_set = ((dev->id)?((reg&0x7f)|0x80):(reg&0x7f));

	while(count-- && (inb(card->iobase + CAS) & 1)) 
		udelay(1);
	
	return inw(card->ac97base + reg_set);
}

static void i810_ac97_set(struct ac97_codec *dev, u8 reg, u16 data)
{
	struct i810_card *card = dev->private_data;
	int count = 100;
	u8 reg_set = ((dev->id)?((reg&0x7f)|0x80):(reg&0x7f));

	while(count-- && (inb(card->iobase + CAS) & 1)) 
		udelay(1);
	outw(data, card->ac97base + reg_set);
}


/* OSS /dev/mixer file operation methods */

static int i810_open_mixdev(struct inode *inode, struct file *file)
{
	int i;
	int minor = MINOR(inode->i_rdev);
	struct i810_card *card = devs;

	for (card = devs; card != NULL; card = card->next)
		for (i = 0; i < NR_AC97; i++)
			if (card->ac97_codec[i] != NULL &&
			    card->ac97_codec[i]->dev_mixer == minor) {
				file->private_data = card->ac97_codec[i];
				return 0;
			}
	return -ENODEV;
}

static int i810_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct ac97_codec *codec = (struct ac97_codec *)file->private_data;

	return codec->mixer_ioctl(codec, cmd, arg);
}

static /*const*/ struct file_operations i810_mixer_fops = {
	owner:		THIS_MODULE,
	llseek:		no_llseek,
	ioctl:		i810_ioctl_mixdev,
	open:		i810_open_mixdev,
};

/* AC97 codec initialisation.  These small functions exist so we don't
   duplicate code between module init and apm resume */

static inline int i810_ac97_exists(struct i810_card *card,int ac97_number)
{
	u32 reg = inl(card->iobase + GLOB_STA);
	return (reg & (0x100 << ac97_number));
}

static inline int i810_ac97_enable_variable_rate(struct ac97_codec *codec)
{
	i810_ac97_set(codec, AC97_EXTENDED_STATUS, 9);
	i810_ac97_set(codec,AC97_EXTENDED_STATUS,
		      i810_ac97_get(codec, AC97_EXTENDED_STATUS)|0xE800);
	
	return (i810_ac97_get(codec, AC97_EXTENDED_STATUS)&1);
}


static int i810_ac97_probe_and_powerup(struct i810_card *card,struct ac97_codec *codec)
{
	/* Returns 0 on failure */
	int i;

	if (ac97_probe_codec(codec) == 0) return 0;
	
	/* power it all up */
	i810_ac97_set(codec, AC97_POWER_CONTROL,
		      i810_ac97_get(codec, AC97_POWER_CONTROL) & ~0x7f00);
	/* wait for analog ready */
	for (i=10;
	     i && ((i810_ac97_get(codec, AC97_POWER_CONTROL) & 0xf) != 0xf);
	     i--)
	{
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ/20);
	} 
	return i;
}

/* if I knew what this did, I'd give it a better name */
static int i810_ac97_random_init_stuff(struct i810_card *card)
{	
	u32 reg = inl(card->iobase + GLOB_CNT);
	int i;

	if((reg&2)==0)	/* Cold required */
		reg|=2;
	else
		reg|=4;	/* Warm */
		
	reg&=~8;	/* ACLink on */
	outl(reg , card->iobase + GLOB_CNT);
	
	for(i=0;i<10;i++)
	{
		if((inl(card->iobase+GLOB_CNT)&4)==0)
			break;

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ/20);
	}
	if(i==10)
	{
		printk(KERN_ERR "i810_audio: AC'97 reset failed.\n");
		return 0;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ/2);
	reg = inl(card->iobase + GLOB_STA);
	inw(card->ac97base);
	return 1;
}

static int __init i810_ac97_init(struct i810_card *card)
{
	int num_ac97 = 0;
	int total_channels = 0;
	struct ac97_codec *codec;
	u16 eid;
	u32 reg;

	if(!i810_ac97_random_init_stuff(card)) return 0;

	/* Number of channels supported */
	/* What about the codec?  Just because the ICH supports */
	/* multiple channels doesn't mean the codec does.       */
	/* we'll have to modify this in the codec section below */
	/* to reflect what the codec has.                       */
	/* ICH and ICH0 only support 2 channels so don't bother */
	/* to check....                                         */

	card->channels = 2;
	reg = inl(card->iobase + GLOB_STA);
	if ( reg & 0x0200000 )
		card->channels = 6;
	else if ( reg & 0x0100000 )
		card->channels = 4;
	printk("i810_audio: Audio Controller supports %d channels.\n", card->channels);
		
	inw(card->ac97base);

	for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {

		/* Assume codec isn't available until we go through the
		 * gauntlet below */
		card->ac97_codec[num_ac97] = NULL;

		/* The ICH programmer's reference says you should   */
		/* check the ready status before probing. So we chk */
		/*   What do we do if it's not ready?  Wait and try */
		/*   again, or abort?                               */
		if (!i810_ac97_exists(card,num_ac97)) {
			if(num_ac97 == 0)
				printk(KERN_ERR "i810_audio: Primary codec not ready.\n");
			break; /* I think this works, if not ready stop */
		}

		if ((codec = kmalloc(sizeof(struct ac97_codec), GFP_KERNEL)) == NULL)
			return -ENOMEM;
		memset(codec, 0, sizeof(struct ac97_codec));

		/* initialize some basic codec information, other fields will be filled
		   in ac97_probe_codec */
		codec->private_data = card;
		codec->id = num_ac97;

		codec->codec_read = i810_ac97_get;
		codec->codec_write = i810_ac97_set;
	
		if(!i810_ac97_probe_and_powerup(card,codec)) {
			printk("i810_audio: timed out waiting for codec %d analog ready", num_ac97);
			kfree(codec);
			break;	/* it didn't work */
		}
		/* Store state information about S/PDIF transmitter */
		card->ac97_status = 0;
		
		/* Don't attempt to get eid until powerup is complete */
		eid = i810_ac97_get(codec, AC97_EXTENDED_ID);
		
		if(eid==0xFFFFFF)
		{
			printk(KERN_WARNING "i810_audio: no codec attached ?\n");
			kfree(codec);
			break;
		}
		
		card->ac97_features = eid;
				
		/* Now check the codec for useful features to make up for
		   the dumbness of the 810 hardware engine */

		if(!(eid&0x0001))
			printk(KERN_WARNING "i810_audio: only 48Khz playback available.\n");
		else
		{
			if(!i810_ac97_enable_variable_rate(codec)) {
				printk(KERN_WARNING "i810_audio: Codec refused to allow VRA, using 48Khz only.\n");
				card->ac97_features&=~1;
			}			
		}
   		
		/* Determine how many channels the codec(s) support   */
		/*   - The primary codec always supports 2            */
		/*   - If the codec supports AMAP, surround DACs will */
		/*     automaticlly get assigned to slots.            */
		/*     * Check for surround DACs and increment if     */
		/*       found.                                       */
		/*   - Else check if the codec is revision 2.2        */
		/*     * If surround DACs exist, assign them to slots */
		/*       and increment channel count.                 */

		/* All of this only applies to ICH2 and above. ICH    */
		/* and ICH0 only support 2 channels.  ICH2 will only  */
		/* support multiple codecs in a "split audio" config. */
		/* as described above.                                */

		/* TODO: Remove all the debugging messages!           */

		if((eid & 0xc000) == 0) /* primary codec */
			total_channels += 2; 

		if(eid & 0x200) { /* GOOD, AMAP support */
			if (eid & 0x0080) /* L/R Surround channels */
				total_channels += 2;
			if (eid & 0x0140) /* LFE and Center channels */
				total_channels += 2;
			printk("i810_audio: AC'97 codec %d supports AMAP, total channels = %d\n", num_ac97, total_channels);
		} else if (eid & 0x0400) {  /* this only works on 2.2 compliant codecs */
			eid &= 0xffcf;
			if((eid & 0xc000) != 0)	{
				switch ( total_channels ) {
					case 2:
						/* Set dsa1, dsa0 to 01 */
						eid |= 0x0010;
						break;
					case 4:
						/* Set dsa1, dsa0 to 10 */
						eid |= 0x0020;
						break;
					case 6:
						/* Set dsa1, dsa0 to 11 */
						eid |= 0x0030;
						break;
				}
				total_channels += 2;
			}
			i810_ac97_set(codec, AC97_EXTENDED_ID, eid);
			eid = i810_ac97_get(codec, AC97_EXTENDED_ID);
			printk("i810_audio: AC'97 codec %d, new EID value = 0x%04x\n", num_ac97, eid);
			if (eid & 0x0080) /* L/R Surround channels */
				total_channels += 2;
			if (eid & 0x0140) /* LFE and Center channels */
				total_channels += 2;
			printk("i810_audio: AC'97 codec %d, DAC map configured, total channels = %d\n", num_ac97, total_channels);
		} else {
			printk("i810_audio: AC'97 codec %d Unable to map surround DAC's (or DAC's not present), total channels = %d\n", num_ac97, total_channels);
		}

		if ((codec->dev_mixer = register_sound_mixer(&i810_mixer_fops, -1)) < 0) {
			printk(KERN_ERR "i810_audio: couldn't register mixer!\n");
			kfree(codec);
			break;
		}

		card->ac97_codec[num_ac97] = codec;
	}

	/* pick the minimum of channels supported by ICHx or codec(s) */
	card->channels = (card->channels > total_channels)?total_channels:card->channels;

	return num_ac97;
}

static void __init i810_configure_clocking (void)
{
	struct i810_card *card;
	struct i810_state *state;
	struct dmabuf *dmabuf;
	unsigned int i, offset, new_offset;
	unsigned long flags;

	card = devs;
	/* We could try to set the clocking for multiple cards, but can you even have
	 * more than one i810 in a machine?  Besides, clocking is global, so unless
	 * someone actually thinks more than one i810 in a machine is possible and
	 * decides to rewrite that little bit, setting the rate for more than one card
	 * is a waste of time.
	 */
	if(card != NULL) {
		state = card->states[0] = (struct i810_state *)
					kmalloc(sizeof(struct i810_state), GFP_KERNEL);
		if (state == NULL)
			return;
		memset(state, 0, sizeof(struct i810_state));
		dmabuf = &state->dmabuf;

		dmabuf->write_channel = card->alloc_pcm_channel(card);
		state->virt = 0;
		state->card = card;
		state->magic = I810_STATE_MAGIC;
		init_waitqueue_head(&dmabuf->wait);
		init_MUTEX(&state->open_sem);
		dmabuf->fmt = I810_FMT_STEREO | I810_FMT_16BIT;
		dmabuf->trigger = PCM_ENABLE_OUTPUT;
		i810_set_dac_rate(state, 48000);
		if(prog_dmabuf(state, 0) != 0) {
			goto config_out_nodmabuf;
		}
		if(dmabuf->dmasize < 16384) {
			goto config_out;
		}
		dmabuf->count = dmabuf->dmasize;
		outb(31,card->iobase+dmabuf->write_channel->port+OFF_LVI);
		save_flags(flags);
		cli();
		start_dac(state);
		offset = i810_get_dma_addr(state, 0);
		mdelay(50);
		new_offset = i810_get_dma_addr(state, 0);
		stop_dac(state);
		outb(2,card->iobase+dmabuf->write_channel->port+OFF_CR);
		restore_flags(flags);
		i = new_offset - offset;
#ifdef DEBUG
		printk("i810_audio: %d bytes in 50 milliseconds\n", i);
#endif
		if(i == 0)
			goto config_out;
		i = i / 4 * 20;
		if (i > 48500 || i < 47500) {
			clocking = clocking * clocking / i;
			printk("i810_audio: setting clocking to %d\n", clocking);
		}
config_out:
		dealloc_dmabuf(state);
config_out_nodmabuf:
		state->card->free_pcm_channel(state->card,state->dmabuf.write_channel->num);
		kfree(state);
		card->states[0] = NULL;
	}
}

/* install the driver, we do not allocate hardware channel nor DMA buffer now, they are defered 
   until "ACCESS" time (in prog_dmabuf called by open/read/write/ioctl/mmap) */
   
static int __init i810_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
	struct i810_card *card;

	if (pci_enable_device(pci_dev))
		return -EIO;

	if (pci_set_dma_mask(pci_dev, I810_DMA_MASK)) {
		printk(KERN_ERR "intel810: architecture does not support"
		       " 32bit PCI busmaster DMA\n");
		return -ENODEV;
	}

	if ((card = kmalloc(sizeof(struct i810_card), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "i810_audio: out of memory\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(*card));

	card->iobase = pci_resource_start (pci_dev, 1);
	card->ac97base = pci_resource_start (pci_dev, 0);
	card->pci_dev = pci_dev;
	card->pci_id = pci_id->device;
	card->irq = pci_dev->irq;
	card->next = devs;
	card->magic = I810_CARD_MAGIC;
#ifdef CONFIG_PM
	card->pm_suspended=0;
#endif
	spin_lock_init(&card->lock);
	devs = card;

	pci_set_master(pci_dev);

	printk(KERN_INFO "i810: %s found at IO 0x%04lx and 0x%04lx, IRQ %d\n",
	       card_names[pci_id->driver_data], card->iobase, card->ac97base, 
	       card->irq);

	card->alloc_pcm_channel = i810_alloc_pcm_channel;
	card->alloc_rec_pcm_channel = i810_alloc_rec_pcm_channel;
	card->alloc_rec_mic_channel = i810_alloc_rec_mic_channel;
	card->free_pcm_channel = i810_free_pcm_channel;
	card->channel[0].offset = 0;
	card->channel[0].port = 0x00;
	card->channel[0].num=0;
	card->channel[1].offset = 0;
	card->channel[1].port = 0x10;
	card->channel[1].num=1;
	card->channel[2].offset = 0;
	card->channel[2].port = 0x20;
	card->channel[2].num=2;

	/* claim our iospace and irq */
	request_region(card->iobase, 64, card_names[pci_id->driver_data]);
	request_region(card->ac97base, 256, card_names[pci_id->driver_data]);

	if (request_irq(card->irq, &i810_interrupt, SA_SHIRQ,
			card_names[pci_id->driver_data], card)) {
		printk(KERN_ERR "i810_audio: unable to allocate irq %d\n", card->irq);
		release_region(card->iobase, 64);
		release_region(card->ac97base, 256);
		kfree(card);
		return -ENODEV;
	}

	/* initialize AC97 codec and register /dev/mixer */
	if (i810_ac97_init(card) <= 0) {
		release_region(card->iobase, 64);
		release_region(card->ac97base, 256);
		free_irq(card->irq, card);
		kfree(card);
		return -ENODEV;
	}
	pci_set_drvdata(pci_dev, card);

	if(clocking == 48000) {
		i810_configure_clocking();
	}

	/* register /dev/dsp */
	if ((card->dev_audio = register_sound_dsp(&i810_audio_fops, -1)) < 0) {
		int i;
		printk(KERN_ERR "i810_audio: couldn't register DSP device!\n");
		release_region(card->iobase, 64);
		release_region(card->ac97base, 256);
		free_irq(card->irq, card);
		for (i = 0; i < NR_AC97; i++)
		if (card->ac97_codec[i] != NULL) {
			unregister_sound_mixer(card->ac97_codec[i]->dev_mixer);
			kfree (card->ac97_codec[i]);
		}
		kfree(card);
		return -ENODEV;
	}

	return 0;
}

static void __exit i810_remove(struct pci_dev *pci_dev)
{
	int i;
	struct i810_card *card = pci_get_drvdata(pci_dev);
	/* free hardware resources */
	free_irq(card->irq, devs);
	release_region(card->iobase, 64);
	release_region(card->ac97base, 256);

	/* unregister audio devices */
	for (i = 0; i < NR_AC97; i++)
		if (card->ac97_codec[i] != NULL) {
			unregister_sound_mixer(card->ac97_codec[i]->dev_mixer);
			kfree (card->ac97_codec[i]);
		}
	unregister_sound_dsp(card->dev_audio);
	kfree(card);
}

#ifdef CONFIG_PM
static int i810_pm_suspend(struct pci_dev *dev, u32 pm_state)
{
        struct i810_card *card = pci_get_drvdata(dev);
        struct i810_state *state;
	unsigned long flags;
	struct dmabuf *dmabuf;
	int i,num_ac97;
#ifdef DEBUG
	printk("i810_audio: i810_pm_suspend called\n");
#endif
	if(!card) return 0;
	spin_lock_irqsave(&card->lock, flags);
	card->pm_suspended=1;
	for(i=0;i<NR_HW_CH;i++) {
		state = card->states[i];
		if(!state) continue;
		/* this happens only if there are open files */
		dmabuf = &state->dmabuf;
		if(dmabuf->enable & DAC_RUNNING ||
		   (dmabuf->count && (dmabuf->trigger & PCM_ENABLE_OUTPUT))) {
			state->pm_saved_dac_rate=dmabuf->rate;
			stop_dac(state);
		} else {
			state->pm_saved_dac_rate=0;
		}
		if(dmabuf->enable & ADC_RUNNING) {
			state->pm_saved_adc_rate=dmabuf->rate;	
			stop_adc(state);
		} else {
			state->pm_saved_adc_rate=0;
		}
		dmabuf->ready = 0;
		dmabuf->swptr = dmabuf->hwptr = 0;
		dmabuf->count = dmabuf->total_bytes = 0;
	}

	spin_unlock_irqrestore(&card->lock, flags);

	/* save mixer settings */
	for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {
		struct ac97_codec *codec = card->ac97_codec[num_ac97];
		if(!codec) continue;
		for(i=0;i< SOUND_MIXER_NRDEVICES ;i++) {
			if((supported_mixer(codec,i)) &&
			   (codec->read_mixer)) {
				card->pm_saved_mixer_settings[i][num_ac97]=
					codec->read_mixer(codec,i);
			}
		}
	}
	pci_save_state(dev,card->pm_save_state); /* XXX do we need this? */
	pci_disable_device(dev); /* disable busmastering */
	pci_set_power_state(dev,3); /* Zzz. */

	return 0;
}


static int i810_pm_resume(struct pci_dev *dev)
{
	int num_ac97,i=0;
	struct i810_card *card=pci_get_drvdata(dev);
	pci_enable_device(dev);
	pci_restore_state (dev,card->pm_save_state);

	/* observation of a toshiba portege 3440ct suggests that the 
	   hardware has to be more or less completely reinitialized from
	   scratch after an apm suspend.  Works For Me.   -dan */

	i810_ac97_random_init_stuff(card);

	for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {
		struct ac97_codec *codec = card->ac97_codec[num_ac97];
		/* check they haven't stolen the hardware while we were
		   away */
		if(!codec || !i810_ac97_exists(card,num_ac97)) {
			if(num_ac97) continue;
			else BUG();
		}
		if(!i810_ac97_probe_and_powerup(card,codec)) BUG();
		
		if((card->ac97_features&0x0001)) {
			/* at probe time we found we could do variable
			   rates, but APM suspend has made it forget
			   its magical powers */
			if(!i810_ac97_enable_variable_rate(codec)) BUG();
		}
		/* we lost our mixer settings, so restore them */
		for(i=0;i< SOUND_MIXER_NRDEVICES ;i++) {
			if(supported_mixer(codec,i)){
				int val=card->
					pm_saved_mixer_settings[i][num_ac97];
				codec->mixer_state[i]=val;
				codec->write_mixer(codec,i,
						   (val  & 0xff) ,
						   ((val >> 8)  & 0xff) );
			}
		}
	}

	/* we need to restore the sample rate from whatever it was */
	for(i=0;i<NR_HW_CH;i++) {
		struct i810_state * state=card->states[i];
		if(state) {
			if(state->pm_saved_adc_rate)
				i810_set_adc_rate(state,state->pm_saved_adc_rate);
			if(state->pm_saved_dac_rate)
				i810_set_dac_rate(state,state->pm_saved_dac_rate);
		}
	}

	
        card->pm_suspended = 0;

	/* any processes that were reading/writing during the suspend
	   probably ended up here */
	for(i=0;i<NR_HW_CH;i++) {
		struct i810_state *state = card->states[i];
		if(state) wake_up(&state->dmabuf.wait);
        }

	return 0;
}	
#endif /* CONFIG_PM */

MODULE_AUTHOR("");
MODULE_DESCRIPTION("Intel 810 audio support");
MODULE_LICENSE("GPL");
MODULE_PARM(ftsodell, "i");
MODULE_PARM(clocking, "i");
MODULE_PARM(strict_clocking, "i");
MODULE_PARM(spdif_locked, "i");

#define I810_MODULE_NAME "intel810_audio"

static struct pci_driver i810_pci_driver = {
	name:		I810_MODULE_NAME,
	id_table:	i810_pci_tbl,
	probe:		i810_probe,
	remove:		i810_remove,
#ifdef CONFIG_PM
	suspend:	i810_pm_suspend,
	resume:		i810_pm_resume,
#endif /* CONFIG_PM */
};


static int __init i810_init_module (void)
{
	if (!pci_present())   /* No PCI bus in this machine! */
		return -ENODEV;

	printk(KERN_INFO "Intel 810 + AC97 Audio, version "
	       DRIVER_VERSION ", " __TIME__ " " __DATE__ "\n");

	if (!pci_register_driver(&i810_pci_driver)) {
		pci_unregister_driver(&i810_pci_driver);
                return -ENODEV;
	}
	if(ftsodell != 0) {
		printk("i810_audio: ftsodell is now a deprecated option.\n");
	}
	if(clocking == 48000) {
		i810_configure_clocking();
	}
	if(spdif_locked > 0 ) {
		if(spdif_locked == 32000 || spdif_locked == 44100 || spdif_locked == 48000) {
			printk("i810_audio: Enabling S/PDIF at sample rate %dHz.\n", spdif_locked);
		} else {
			printk("i810_audio: S/PDIF can only be locked to 32000, 441000, or 48000Hz.\n");
			spdif_locked = 0;
		}
	}
	
	return 0;
}

static void __exit i810_cleanup_module (void)
{
	pci_unregister_driver(&i810_pci_driver);
}

module_init(i810_init_module);
module_exit(i810_cleanup_module);

/*
Local Variables:
c-basic-offset: 8
End:
*/
