/*
 *
 *	Trident 4D-Wave/SiS 7018 OSS driver for Linux 2.2.x
 *
 *	Driver: Alan Cox <alan@redhat.com>
 *
 *  Built from:
 *	Low level code: <audio@tridentmicro.com> from ALSA
 *	Framework: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *	Extended by: Zach Brown <zab@redhat.com>  
 *
 *  Hacked up by:
 *	Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *	Ollie Lho <ollie@sis.com.tw> SiS 7018 Audio Core Support
 *
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
 *  History
 *  v0.11 Jan 27 2000 Ollie Lho
 *	DMA bug, scheduler latency, second try
 *  v0.10 Jan 24 2000 Ollie Lho
 *	DMA bug fixed, found kernel scheduling problem
 *  v0.09 Jan 20 2000 Ollie Lho
 *	Clean up of channel register access routine (prepare for channel binding)
 *  v0.08 Jan 14 2000 Ollie Lho
 *	Isolation of AC97 codec code
 *  v0.07 Jan 13 2000 Ollie Lho
 *	Get rid of ugly old low level access routines (e.g. CHRegs.lp****)
 *  v0.06 Jan 11 2000 Ollie Lho
 *	Preliminary support for dual (more ?) AC97 codecs
 *  v0.05 Jan 08 2000 Luca Montecchiani <m.luca@iname.com>
 *	adapt to 2.3.x new __setup/__init call
 *  v0.04 Dec 31 1999 Ollie Lho
 *	Multiple Open, using Middle Loop Interrupt to smooth playback
 *  v0.03 Dec 24 1999 Ollie Lho
 *	mem leak in prog_dmabuf and dealloc_dmabuf removed
 *  v0.02 Dec 15 1999 Ollie Lho
 *	SiS 7018 support added, playback O.K.
 *  v0.01 Alan Cox et. al.
 *	Initial Release in kernel 2.3.30, does not work
 * 
 *  ToDo
 *	Clean up of low level channel register access code. (done)
 *        Fix the bug on dma buffer management in update_ptr, read/write, drain_dac (done)
 *	Dual AC97 codecs support (done partially, need channel binding to test)
 *	Recording support
 *	Mmap support
 *	"Channel Binding" ioctl extension
 */
      
#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/malloc.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif

#include "trident.h"
#include "ac97_codec.h"

#undef DEBUG

#define DRIVER_VERSION "0.11"

/* magic numbers to protect our data structures */
#define TRIDENT_CARD_MAGIC	0x5072696E /* "Prin" */
#define TRIDENT_STATE_MAGIC	0x63657373 /* "cess" */

/* The first 32 channels are called Bank A. They are (should be) reserved
   for MIDI synthesizer. But since that is not supported yet, we can (ab)use
   them to play PCM samples */
#undef ABUSE_BANK_A

/* maxinum number of instances of opening /dev/dspN, can your CPU handle this ?
   NOTE: If /dev/dsp is opened O_RDWR (i.e. full duplex) it will consume 2 HW
   channels */
#ifdef ABUSE_BANK_A
#define NR_HW_CH		64
#else
#define NR_HW_CH		32
#endif

/* maxinum nuber of AC97 codecs connected, AC97 2.0 defined 4, but 7018 and 4D-NX only
   have 2 SDATA_IN lines (currently) */
#define NR_AC97		2

/* minor number of /dev/dspW */
#define SND_DEV_DSP16	5 

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

static const char invalid_magic[] = KERN_CRIT "trident: invalid magic value in %s\n";

struct pci_audio_info {
	u16 vendor;
	u16 device;
	char *name;
};

static struct pci_audio_info pci_audio_devices[] = {
	{PCI_VENDOR_ID_TRIDENT, PCI_DEVICE_ID_TRIDENT_4DWAVE_DX, "Trident 4DWave DX"},
	{PCI_VENDOR_ID_TRIDENT, PCI_DEVICE_ID_TRIDENT_4DWAVE_NX, "Trident 4DWave NX"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_7018, "SiS 7018 PCI Audio"}
};

/* "software" or virtual channel, an instance of opened /dev/dsp */
struct trident_state {
	unsigned int magic;
	struct trident_card *card;	/* Card info */

	/* single open lock mechanism, only used for recording */
	struct semaphore open_sem;
	wait_queue_head_t open_wait;

	/* file mode */
	mode_t open_mode;

	/* virtual channel number */
	int virt;

	struct dmabuf {
		/* wave sample stuff */
		unsigned int rate;
		unsigned char fmt, enable;

		/* hardware channel */
		struct trident_channel *channel;

		/* OSS buffer manangemeent stuff */
		void *rawbuf;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;

		/* our buffer acts like a circular ring */
		unsigned hwptr;		/* where dma last started, update by update_ptr */
		unsigned swptr;		/* where driver last clear/filled, updated by read/write */
		int count;		/* bytes to be comsumed by dma machine */
		unsigned total_bytes;	/* total bytes dmaed by hardware */

		unsigned error;		/* number of over/underruns */
		wait_queue_head_t wait;	/* put process on wait queue when no more space in buffer */

		/* redundant, but makes calculations easier */
		unsigned fragsize;
		unsigned dmasize;
		unsigned fragsamples;

		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned endcleared:1;
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;
	} dma_dac, dma_adc;
};

/* hardware channels */
struct trident_channel {
	int  num;	/* channel number */
	u32 lba;	/* reg 0xe4 */
	u32 eso;	/* reg 0xe8 */
	u32 delta;
	u16 attribute;	/* reg 0xec */
	u16 fm_vol;
	u32 control;	/* reg 0xf0 */
};

struct trident_pcm_bank_address {
	u32 start;
	u32 stop;
	u32 aint;
	u32 aint_en;
};
static struct trident_pcm_bank_address bank_a_addrs =
{
	T4D_START_A,
	T4D_STOP_A,
	T4D_AINT_A,
	T4D_AINTEN_A
};
static struct trident_pcm_bank_address bank_b_addrs =
{
	T4D_START_B,
	T4D_STOP_B,
	T4D_AINT_B,
	T4D_AINTEN_B
};
struct trident_pcm_bank {
	/* register addresses to control bank operations */
	struct trident_pcm_bank_address *addresses;
	/* each bank has 32 channels */
	u32 bitmap; /* channel allocation bitmap */
	struct trident_channel channels[32];
};

struct trident_card {
	unsigned int magic;

	/* We keep trident cards in a linked list */
	struct trident_card *next;

	/* The trident has a certain amount of cross channel interaction
	   so we use a single per card lock */
	spinlock_t lock;

	/* PCI device stuff */
	struct pci_audio_info *pci_info;
	struct pci_dev * pci_dev;
	u16 pci_id;

	/* soundcore stuff */
	int dev_audio;

	/* structures for abstraction of hardware facilities, codecs, banks and channels*/
	struct ac97_codec *ac97_codec[NR_AC97];
	struct trident_pcm_bank banks[NR_BANKS];
	struct trident_state *states[NR_HW_CH];

	/* hardware resources */
	unsigned long iobase;
	u32 irq;
};

static struct trident_card *devs = NULL;

static void trident_ac97_set(struct ac97_codec *codec, u8 reg, u16 val);
static u16 trident_ac97_get(struct ac97_codec *codec, u8 reg);

static int trident_open_mixdev(struct inode *inode, struct file *file);
static int trident_release_mixdev(struct inode *inode, struct file *file);
static int trident_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd,
				unsigned long arg);
static loff_t trident_llseek(struct file *file, loff_t offset, int origin);

static int trident_enable_loop_interrupts(struct trident_card * card)
{
	u32 global_control;

	global_control = inl(TRID_REG(card, T4D_LFO_GC_CIR));

	switch (card->pci_id)
	{
	case PCI_DEVICE_ID_SI_7018:
		global_control |= (ENDLP_IE | MIDLP_IE| BANK_B_EN);
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		global_control |= (ENDLP_IE | MIDLP_IE);
		break;
	default:
		return FALSE;
	}

	outl(global_control, TRID_REG(card, T4D_LFO_GC_CIR));

#ifdef DEBUG
	printk("trident: Enable Loop Interrupts, globctl = 0x%08X\n",
	       global_control);
#endif
	return (TRUE);
}

static int trident_disable_loop_interrupts(struct trident_card * card)
{
	u32 global_control;

	global_control = inl(TRID_REG(card, T4D_LFO_GC_CIR));
	global_control &= ~(ENDLP_IE | MIDLP_IE);
	outl(global_control, TRID_REG(card, T4D_LFO_GC_CIR));

#ifdef DEBUG
	printk("trident: Disabled Loop Interrupts, globctl = 0x%08X\n",
	       global_control);
#endif
	return (TRUE);
}

static void trident_enable_voice_irq(struct trident_card * card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 reg, addr = bank->addresses->aint_en;

	reg = inl(TRID_REG(card, addr));
	reg |= mask;
	outl(reg, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, T4D_AINTEN_B));
	printk("trident: enabled IRQ on channel %d, AINTEN_B = 0x%08x\n",
	       channel, reg);
#endif
}

static void trident_disable_voice_irq(struct trident_card * card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 reg, addr = bank->addresses->aint_en;
	
	reg = inl(TRID_REG(card, addr));
	reg &= ~mask;
	outl(reg, TRID_REG(card, addr));
	
	/* Ack the channel in case the interrupt was set before we disable it. */
	outl(mask, TRID_REG(card, bank->addresses->aint));

#ifdef DEBUG
	reg = inl(TRID_REG(card, T4D_AINTEN_B));
	printk("trident: disabled IRQ on channel %d, AINTEN_B = 0x%08x\n",
	       channel, reg);
#endif
}

static void trident_start_voice(struct trident_card * card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 addr = bank->addresses->start;

#ifdef DEBUG
	u32 reg;
#endif

	outl(mask, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, T4D_START_B));
	printk("trident: start voice on channel %d, START_B  = 0x%08x\n",
	       channel, reg);
#endif
}

static void trident_stop_voice(struct trident_card * card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 addr = bank->addresses->stop;

#ifdef DEBUG
	u32 reg;
#endif

	outl(mask, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, T4D_STOP_B));
	printk("trident: stop voice on channel %d,  STOP_B  = 0x%08x\n",
	       channel, reg);
#endif
}

static int trident_check_channel_interrupt(struct trident_card * card, int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 reg, addr = bank->addresses->aint;

	reg = inl(TRID_REG(card, addr));

#ifdef DEBUG
	if (reg & mask)
		printk("trident: channel %d has interrupt, AINT_B = 0x%08x\n",
		       channel, reg);
#endif
	return (reg & mask) ? TRUE : FALSE;
}

static void trident_ack_channel_interrupt(struct trident_card * card, int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 reg, addr = bank->addresses->aint;

	reg = inl(TRID_REG(card, addr));
	reg &= mask;
	outl(reg, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, T4D_AINT_B));
	printk("trident: Ack channel %d interrupt, AINT_B = 0x%08x\n",
	       channel, reg);
#endif
}

static struct trident_channel * trident_alloc_pcm_channel(struct trident_card *card)
{
	struct trident_pcm_bank *bank;
	int idx;

	bank = &card->banks[BANK_B];
	if (bank->bitmap == ~0UL) {
		/* no more free channels avaliable */
		printk(KERN_ERR "trident: no more channels available on Bank B.\n");
#ifdef ABUSE_BANK_A
		goto bank_a;
#endif
		return NULL;
	}
	for (idx = 31; idx >= 0; idx--) {
		if (!(bank->bitmap & (1 << idx))) {
			struct trident_channel *channel = &bank->channels[idx];
			bank->bitmap |= 1 << idx;
			channel->num = idx + 32;
			return channel;
		}
	}

#ifdef ABUSE_BANK_A
	/* channels in Bank A should be reserved for synthesizer 
	   not for normal use (channels in Bank A can't record) */
 bank_a:
	bank = &card->banks[BANK_A];
	if (bank->bitmap == ~0UL) {
		/* no more free channels avaliable */
		printk(KERN_ERR "trident: no more channels available on Bank A.\n");
		return NULL;
	}
	for (idx = 31; idx >= 0; idx--) {
		if (!(bank->bitmap & (1 << idx))) {
			struct trident_channel *channel = &bank->channels[idx];
			banks->bitmap |= 1 << idx;
			channel->num = idx;
			return channels;
		}
	}
#endif
	return NULL;
}

static void trident_free_pcm_channel(struct trident_card *card, int channel)
{
	int bank;

#ifdef ABUSE_BANK_A
	if (channel < 0 || channel > 63)
		return;
#else
	if (channel < 31 || channel > 63)
		return;
#endif

	bank = channel >> 5;
	channel = channel & 0x1f;

	if (card->banks[bank].bitmap & (1 << (channel))) {
		card->banks[bank].bitmap &= ~(1 << (channel));
	}
}

/* called with spin lock held */
static int trident_load_channel_registers(struct trident_card *card, u32 *data, unsigned int channel)
{
	int i;

	if (channel > 63)
		return FALSE;

	/* select hardware channel to write */
	outb(channel, TRID_REG(card, T4D_LFO_GC_CIR));
	/* output the channel registers */
	for (i = 0; i < CHANNEL_REGS; i++) {
		outl(data[i], TRID_REG(card, CHANNEL_START + 4*i));
	}

	return TRUE;
}

/* called with spin lock held */
static int trident_write_voice_regs(struct trident_state *state, unsigned int rec)
{
	unsigned int data[CHANNEL_REGS + 1];
	struct trident_channel *channel;

	if (rec)
		channel = state->dma_adc.channel;
	else
		channel = state->dma_dac.channel;

	data[1] = channel->lba;
	data[4] = channel->control;

	switch (state->card->pci_id)
	{
	case PCI_DEVICE_ID_SI_7018:
		data[0] = 0; /* Current Sample Offset */
		data[2] = (channel->eso << 16) | (channel->delta & 0xffff);
		data[3] = (channel->attribute << 16) | (channel->fm_vol & 0xffff);
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		data[0] = 0; /* Current Sample Offset */
		data[2] = (channel->eso << 16) | (channel->delta & 0xffff);
		data[3] = channel->fm_vol & 0xffff;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		data[0] = (channel->delta << 24);
		data[2] = ((channel->delta << 24) & 0xff000000) | (channel->eso & 0x00ffffff);
		data[3] = channel->fm_vol & 0xffff;
		break;
	default:
		return FALSE;
	}

	return trident_load_channel_registers(state->card, data, channel->num);
}

static int compute_rate(u32 rate)
{
	int delta;
	/* We special case 44100 and 8000 since rounding with the equation
	   does not give us an accurate enough value. For 11025 and 22050
	   the equation gives us the best answer. All other frequencies will
	   also use the equation. JDW */
	if (rate == 44100)
		delta = 0xeb3;
	else if (rate == 8000)
		delta = 0x2ab;
	else if (rate == 48000)
		delta = 0x1000;
	else
		delta = (((rate << 12) + rate) / 48000) & 0x0000ffff;
	return delta;
}

/* set playback sample rate */
static unsigned int trident_set_dac_rate(struct trident_state * state, unsigned int rate)
{	
	struct dmabuf *dmabuf = &state->dma_dac;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	dmabuf->rate = rate;
	dmabuf->channel->delta = compute_rate(rate);

	trident_write_voice_regs(state, 0);

#ifdef DEBUG
	printk("trident: called trident_set_dac_rate : rate = %d\n", rate);
#endif

	return rate;
}

/* set recording sample rate */
static unsigned int trident_set_adc_rate(struct trident_state * state, unsigned int rate)
{
	struct dmabuf *dmabuf = &state->dma_adc;
	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	dmabuf->rate = rate;
	dmabuf->channel->delta = compute_rate(rate);

	trident_write_voice_regs(state, 1);

#ifdef DEBUG
	printk("trident: called trident_set_adc_rate : rate = %d\n", rate);
#endif
	return rate;
}

/* prepare channel attributes for playback */ 
static void trident_play_setup(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dma_dac;
	struct trident_channel *channel = dmabuf->channel;

	channel->lba = virt_to_bus(dmabuf->rawbuf);
	channel->delta = compute_rate(dmabuf->rate);

	channel->eso = dmabuf->dmasize >> sample_shift[dmabuf->fmt];
	channel->eso -= 1;

	if (state->card->pci_id == PCI_DEVICE_ID_SI_7018) {
		/* FIXME: channel attributes are configured by ioctls, but it is not implemented
		   so just set to ZERO for the moment */
		channel->attribute = 0;
	} else {
		channel->attribute = 0;
	}

	channel->fm_vol = 0x0;
	
	channel->control = CHANNEL_LOOP;
	if (dmabuf->fmt & TRIDENT_FMT_16BIT) {
		/* 16-bits */
		channel->control |= CHANNEL_16BITS;
		/* signed */
		channel->control |= CHANNEL_SIGNED;
	}
	if (dmabuf->fmt & TRIDENT_FMT_STEREO)
		/* stereo */
		channel->control |= CHANNEL_STEREO;
#ifdef DEBUG
	printk("trident: trident_play_setup, LBA = 0x%08x, "
	       "Delat = 0x%08x, ESO = 0x%08x, Control = 0x%08x\n",
	       channel->lba, channel->delta, channel->eso, channel->control);
#endif
	trident_write_voice_regs(state, 0);
}

/* prepare channel attributes for recording */
static void trident_rec_setup(struct trident_state *state)
{
	u16 w;
	struct trident_card *card = state->card;
	struct dmabuf *dmabuf = &state->dma_adc;
	struct trident_channel *channel = dmabuf->channel;

	/* Enable AC-97 ADC (capture) */
	switch (card->pci_id) 
	{
	case PCI_DEVICE_ID_SI_7018:
		/* for 7018, the ac97 is always in playback/record (duplex) mode */
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		w = inb(TRID_REG(card, DX_ACR2_AC97_COM_STAT));
		outb(w | 0x48, TRID_REG(card, DX_ACR2_AC97_COM_STAT));
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		w = inw(TRID_REG(card, T4D_MISCINT));
		outw(w | 0x1000, TRID_REG(card, T4D_MISCINT));
		break;
	default:
		return;
	}

	channel->lba = virt_to_bus(dmabuf->rawbuf);
	channel->delta = compute_rate(dmabuf->rate);

	channel->eso = dmabuf->dmasize >> sample_shift[dmabuf->fmt];
	channel->eso -= 1;

	if (state->card->pci_id == PCI_DEVICE_ID_SI_7018) {
		/* FIXME: channel attributes are configured by ioctls, but it is not implemented
		   so just set to ZERO for the moment */
		channel->attribute = 0;
	} else {
		channel->attribute = 0;
	}

	channel->fm_vol = 0x0;
	
	channel->control = CHANNEL_LOOP;
	if (dmabuf->fmt & TRIDENT_FMT_16BIT) {
		/* 16-bits */
		channel->control |= CHANNEL_16BITS;
		/* signed */
		channel->control |= CHANNEL_SIGNED;
	}
	if (dmabuf->fmt & TRIDENT_FMT_STEREO)
		/* stereo */
		channel->control |= CHANNEL_STEREO;
#ifdef DEBUG
	printk("trident: trident_rec_setup, LBA = 0x%08x, "
	       "Delat = 0x%08x, ESO = 0x%08x, Control = 0x%08x\n",
	       channel->lba, channel->delta, channel->eso, channel->control);
#endif
	trident_write_voice_regs(state, 1);
}

/* get current playback/recording dma buffer pointer (byte offset from LBA),
   called with spinlock held! */
extern __inline__ unsigned trident_get_dma_addr(struct trident_state *state, unsigned rec)
{
	struct dmabuf *dmabuf;
	u32 cso;

	if (rec)
		dmabuf = &state->dma_adc;
	else
		dmabuf = &state->dma_dac;

	if (!dmabuf->enable)
		return 0;

	outb(dmabuf->channel->num, TRID_REG(state->card, T4D_LFO_GC_CIR));

	switch (state->card->pci_id) 
	{
	case PCI_DEVICE_ID_SI_7018:
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		/* 16 bits ESO, CSO for 7018 and DX */
		cso = inw(TRID_REG(state->card, CH_DX_CSO_ALPHA_FMS + 2));
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		/* 24 bits ESO, CSO for NX */
		cso = inl(TRID_REG(state->card, CH_NX_DELTA_CSO)) & 0x00ffffff;
		break;
	default:
		return 0;
	}

#ifdef DEBUG
	printk("trident: trident_get_dma_addr: chip reported channel: %d, cso = %d\n",
	       dmabuf->channel->num, cso);
#endif
	/* ESO and CSO are in units of Samples, convert to byte offset */
	cso <<= sample_shift[dmabuf->fmt];

	return (cso % dmabuf->dmasize);
}

/* Stop recording (lock held) */
extern __inline__ void __stop_adc(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dma_adc;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;

	dmabuf->enable &= ~DMA_RUNNING;
	trident_stop_voice(card, chan_num);
	trident_disable_voice_irq(card, chan_num);
}

static void stop_adc(struct trident_state *state)
{
	struct trident_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	__stop_adc(state);
	spin_unlock_irqrestore(&card->lock, flags);
}

static void start_adc(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dma_adc;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	if ((dmabuf->mapped || 
	     dmabuf->count < (signed)(dmabuf->dmasize - 2*dmabuf->fragsize))
	    && dmabuf->ready) {
		dmabuf->enable |= DMA_RUNNING;
		trident_enable_voice_irq(card, chan_num);
		trident_start_voice(card, chan_num);
	}
	spin_unlock_irqrestore(&card->lock, flags);
}

/* stop playback (lock held) */
extern __inline__ void __stop_dac(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dma_dac;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;

	dmabuf->enable &= ~DMA_RUNNING;
	trident_stop_voice(card, chan_num);
	trident_disable_voice_irq(card, chan_num);
}

static void stop_dac(struct trident_state *state)
{
	struct trident_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	__stop_dac(state);
	spin_unlock_irqrestore(&card->lock, flags);
}	

static void start_dac(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dma_dac;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	if ((dmabuf->mapped || dmabuf->count > 0) && dmabuf->ready) {
		dmabuf->enable |= DMA_RUNNING;
		trident_enable_voice_irq(card, chan_num);
		trident_start_voice(card, chan_num);
	}
	spin_unlock_irqrestore(&card->lock, flags);
}

#define DMABUF_DEFAULTORDER (15-PAGE_SHIFT)
#define DMABUF_MINORDER 1

/* allocate DMA buffer, playback and recording buffer should be allocated seperately */
static int alloc_dmabuf(struct trident_state *state, unsigned rec)
{
	struct dmabuf *dmabuf;
	void *rawbuf;
	int order;
	unsigned long map, mapend;

	if (rec)
		dmabuf  = &state->dma_adc;
	else
		dmabuf  = &state->dma_dac;

	/* alloc as big a chunk as we can, FIXME: is this necessary ?? */
	for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER; order--)
		if ((rawbuf = (void *)__get_free_pages(GFP_KERNEL, order)))
			break;
	if (!rawbuf)
		return -ENOMEM;
#ifdef DEBUG
	printk("trident: allocated %ld (order = %d) bytes at %p\n",
	       PAGE_SIZE << order, order, rawbuf);
#endif

	/* for 4DWave and 7018, there are only 30 (31) siginifcan bits for Loop Begin Address
	   (LBA) which limits the address space to 1 (2) GB, bad T^2 design */
	if ((virt_to_bus(rawbuf) + (PAGE_SIZE << order) - 1) & ~0x3fffffff) {
		printk(KERN_ERR "trident: DMA buffer beyond 1 GB; "
		       "bus address = 0x%lx, size = %ld\n",
		       virt_to_bus(rawbuf), PAGE_SIZE << order);
		free_pages((unsigned long)rawbuf, order);
		return -ENOMEM;
	}

	dmabuf->ready  = dmabuf->mapped = 0;
	dmabuf->rawbuf = rawbuf;
	dmabuf->buforder = order;
	
	/* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
	mapend = MAP_NR(rawbuf + (PAGE_SIZE << order) - 1);
	for (map = MAP_NR(rawbuf); map <= mapend; map++)
		set_bit(PG_reserved, &mem_map[map].flags);

	return 0;
}

/* free DMA buffer */
static void dealloc_dmabuf(struct dmabuf *dmabuf)
{
	unsigned long map, mapend;

	if (dmabuf->rawbuf) {
		/* undo marking the pages as reserved */
		mapend = MAP_NR(dmabuf->rawbuf + (PAGE_SIZE << dmabuf->buforder) - 1);
		for (map = MAP_NR(dmabuf->rawbuf); map <= mapend; map++)
			clear_bit(PG_reserved, &mem_map[map].flags);	
		free_pages((unsigned long)dmabuf->rawbuf, dmabuf->buforder);
	}
	dmabuf->rawbuf = NULL;
	dmabuf->mapped = dmabuf->ready = 0;
}

static int prog_dmabuf(struct trident_state *state, unsigned rec)
{
	struct dmabuf *dmabuf;
	unsigned bytepersec;
	unsigned bufsize;
	unsigned long flags;
	int ret;

	if (rec)
		dmabuf = &state->dma_adc;
	else
		dmabuf = &state->dma_dac;

	spin_lock_irqsave(&state->card->lock, flags);
	dmabuf->hwptr  = dmabuf->swptr = dmabuf->total_bytes = 0;
	dmabuf->count  = dmabuf->error = dmabuf->endcleared  = 0;
	spin_unlock_irqrestore(&state->card->lock, flags);

	/* allocate DMA buffer if not allocated yet */
	if (!dmabuf->rawbuf)
		if ((ret = alloc_dmabuf(state, rec)))
			return ret;

	/* FIXME: figure out all this OSS fragment stuff */
	bytepersec = dmabuf->rate << sample_shift[dmabuf->fmt];
	bufsize = PAGE_SIZE << dmabuf->buforder;
	if (dmabuf->ossfragshift) {
		if ((1000 << dmabuf->ossfragshift) < bytepersec)
			dmabuf->fragshift = ld2(bytepersec/1000);
		else
			dmabuf->fragshift = dmabuf->ossfragshift;
	} else {
		/* lets hand out reasonable big ass buffers by default */
		dmabuf->fragshift = (dmabuf->buforder + PAGE_SHIFT -2);
	}
	dmabuf->numfrag = bufsize >> dmabuf->fragshift;
	while (dmabuf->numfrag < 4 && dmabuf->fragshift > 3) {
		dmabuf->fragshift--;
		dmabuf->numfrag = bufsize >> dmabuf->fragshift;
	}
	dmabuf->fragsize = 1 << dmabuf->fragshift;
	if (dmabuf->ossmaxfrags >= 4 && dmabuf->ossmaxfrags < dmabuf->numfrag)
		dmabuf->numfrag = dmabuf->ossmaxfrags;
	dmabuf->fragsamples = dmabuf->fragsize >> sample_shift[dmabuf->fmt];
	dmabuf->dmasize = dmabuf->numfrag << dmabuf->fragshift;

	memset(dmabuf->rawbuf, (dmabuf->fmt & TRIDENT_FMT_16BIT) ? 0 : 0x80,
	       dmabuf->dmasize);

	spin_lock_irqsave(&state->card->lock, flags);
	if (rec) {
		trident_rec_setup(state);
	} else {
		trident_play_setup(state);
	}
	spin_unlock_irqrestore(&state->card->lock, flags);

	/* set the ready flag for the dma buffer */
	dmabuf->ready = 1;

#ifdef DEBUG
	printk("trident: prog_dmabuf, sample rate = %d, format = %d, numfrag = %d, "
	       "fragsize = %d dmasize = %d\n",
	       dmabuf->rate, dmabuf->fmt, dmabuf->numfrag,
	       dmabuf->fragsize, dmabuf->dmasize);
#endif

	return 0;
}

/* we are doing quantum mechanics here, the buffer can only be empty, half or full filled i.e.
   |------------|------------|   or   |xxxxxxxxxxxx|------------|   or   |xxxxxxxxxxxx|xxxxxxxxxxxx|
   but we almost always get this
   |xxxxxx------|------------|   or   |xxxxxxxxxxxx|xxxxx-------|
   so we have to clear the tail space to "silence"
   |xxxxxx000000|------------|   or   |xxxxxxxxxxxx|xxxxxx000000|
*/
static void trident_clear_tail(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dma_dac;
	unsigned swptr;
	unsigned char silence = (dmabuf->fmt & TRIDENT_FMT_16BIT) ? 0 : 0x80;
	unsigned int len;
	unsigned long flags;

	spin_lock_irqsave(&state->card->lock, flags);
	swptr = dmabuf->swptr;
	spin_unlock_irqrestore(&state->card->lock, flags);

	if (swptr == 0 || swptr == dmabuf->dmasize / 2 || swptr == dmabuf->dmasize)
		return;


	if (swptr < dmabuf->dmasize/2)
		len = dmabuf->dmasize/2 - swptr;
	else
		len = dmabuf->dmasize - swptr;

	memset(dmabuf->rawbuf + swptr, silence, len);

	spin_lock_irqsave(&state->card->lock, flags);	
	dmabuf->swptr += len;
	dmabuf->count += len;
	spin_unlock_irqrestore(&state->card->lock, flags);

	/* restart the dma machine in case it is halted */
	start_dac(state);
}

static int drain_dac(struct trident_state *state, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	struct dmabuf *dmabuf = &state->dma_dac;
	unsigned long flags;
	unsigned long tmo;
	int count;

	if (dmabuf->mapped || !dmabuf->ready)
		return 0;

	add_wait_queue(&dmabuf->wait, &wait);
	for (;;) {
		/* It seems that we have to set the current state to TASK_INTERRUPTIBLE
		   every time to make the process really go to sleep */
		current->state = TASK_INTERRUPTIBLE;

		spin_lock_irqsave(&state->card->lock, flags);
		count = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (count <= 0)
			break;

		if (signal_pending(current))
			break;

		if (nonblock) {
			remove_wait_queue(&dmabuf->wait, &wait);
			current->state = TASK_RUNNING;
			return -EBUSY;
		}

		/* No matter how much data left in the buffer, we have to wait untill
		   CSO == ESO/2 or CSO == ESO when address engine interrupts */
		tmo = (dmabuf->dmasize * HZ) / dmabuf->rate;
		tmo >>= sample_shift[dmabuf->fmt];
		if (!schedule_timeout(tmo ? tmo : 1) && tmo){
			printk(KERN_ERR "trident: drain_dac, dma timeout?\n");
			break;
		}
	}
	remove_wait_queue(&dmabuf->wait, &wait);
	current->state = TASK_RUNNING;
	if (signal_pending(current))
		return -ERESTARTSYS;

	return 0;
}

/* call with spinlock held! */
static void trident_update_ptr(struct trident_state *state)
{
	struct dmabuf *dmabuf;
	unsigned hwptr;
	int diff;

	/* update ADC pointer */
	if (state->dma_adc.ready) {
		dmabuf = &state->dma_adc;
		hwptr = trident_get_dma_addr(state, 1);
		diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;

		dmabuf->hwptr = hwptr;
		dmabuf->total_bytes += diff;
		dmabuf->count += diff;

		if (dmabuf->count >= (signed)dmabuf->fragsize)
			wake_up(&dmabuf->wait);
		if (!dmabuf->mapped) {
			if (dmabuf->count > (signed)(dmabuf->dmasize - ((3 * dmabuf->fragsize) >> 1))) {
				__stop_adc(state); 
				dmabuf->error++;
			}
		}
	}

	/* update DAC pointer */
	if (state->dma_dac.ready) {
		dmabuf = &state->dma_dac;
		hwptr = trident_get_dma_addr(state, 0);
		diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;

		dmabuf->hwptr = hwptr;
		dmabuf->total_bytes += diff;

		if (dmabuf->mapped) {
			dmabuf->count += diff;
			if (dmabuf->count >= (signed)dmabuf->fragsize) 
				wake_up(&dmabuf->wait);
		}
		else {
			dmabuf->count -= diff;
			if (dmabuf->count < 0 || dmabuf->count > dmabuf->dmasize) {
				/* buffer underrun or buffer overrun, we have no way to recover
				   it here, just stop the machine and let the process force hwptr
				   and swptr to sync */
				__stop_dac(state);
				dmabuf->error++;
			}
			/* since dma machine only interrupts at ESO and ESO/2, we sure have at 
			   least half of dma buffer free, so wake up the process unconditionally */
			wake_up(&dmabuf->wait);
		}
	}
}

static void trident_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct trident_state *state;
	struct trident_card *card = (struct trident_card *)dev_id;
	int i;
	u32 event;

	spin_lock(&card->lock);
	event = inl(TRID_REG(card, T4D_MISCINT));

#ifdef DEBUG
	printk("trident: trident_interrupt called, MISCINT = 0x%08x\n", event);
#endif

	if (event & ADDRESS_IRQ) {
		/* Update the pointers for all channels we are running. */
		/* FIXME: should read interrupt status only once */
		for (i = 0; i < NR_HW_CH; i++) {
			if (trident_check_channel_interrupt(card, 63 - i)) {
				trident_ack_channel_interrupt(card, 63 - i);
				if ((state = card->states[i]) != NULL) {
					trident_update_ptr(state);
				} else {
					printk("trident: spurious channel irq %d.\n",
					       63 - i);
					trident_stop_voice(card, i);
					trident_disable_voice_irq(card, i);	
				}
			}
		}
	}

	if (event & SB_IRQ){
		/* Midi - TODO */
	}

	/* manually clear interrupt status, bad hardware design, balme T^2 */
	outl((ST_TARGET_REACHED | MIXER_OVERFLOW | MIXER_UNDERFLOW),
	     TRID_REG(card, T4D_MISCINT));
	spin_unlock(&card->lock);
}

static loff_t trident_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

/* in this loop, dma_adc.count signifies the amount of data thats waiting
   to be copied to the user's buffer.  it is filled by the interrupt
   handler and drained by this loop. */
static ssize_t trident_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct trident_state *state = (struct trident_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dma_dac;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(state);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (dmabuf->mapped)
		return -ENXIO;
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 1)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;

	while (count > 0) {
		spin_lock_irqsave(&state->card->lock, flags);
		swptr = dmabuf->swptr;
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count < cnt)
			cnt = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (cnt > count)
			cnt = count;

		if (cnt <= 0) {
			start_adc(state);
			if (file->f_flags & O_NONBLOCK) {
				ret = ret ? ret : -EAGAIN;
				return ret;
			}
			if (!interruptible_sleep_on_timeout(&dmabuf->wait, HZ)) {
				printk(KERN_ERR
				       "(trident) read: chip lockup? "
				       "dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       dmabuf->dmasize, dmabuf->fragsize, dmabuf->count,
				       dmabuf->hwptr, dmabuf->swptr);
				stop_adc(state);
				spin_lock_irqsave(&state->card->lock, flags);
				dmabuf->count = 0;
				dmabuf->hwptr = 0;
				dmabuf->swptr = 0;
				spin_unlock_irqrestore(&state->card->lock, flags);
			}
			if (signal_pending(current)) {
				ret = ret ? ret : -ERESTARTSYS;
				return ret;
			}
			continue;
		}

		if (copy_to_user(buffer, dmabuf->rawbuf + swptr, cnt)) {
			ret = ret ? ret : -EFAULT;
			return ret;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;

		spin_lock_irqsave(&state->card->lock, flags);
		dmabuf->swptr = swptr;
		dmabuf->count -= cnt;
		spin_unlock_irqrestore(&state->card->lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_adc(state);
	}

	return ret;
}

static ssize_t trident_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct trident_state *state = (struct trident_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dma_dac;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

#ifdef DEBUG
	printk("trident: trident_write called, count = %d\n", count);
#endif	

	VALIDATE_STATE(state);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (dmabuf->mapped)
		return -ENXIO;
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 0)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;

	while (count > 0) {
		spin_lock_irqsave(&state->card->lock, flags);
		if (dmabuf->count < 0) {
			/* buffer underrun, we are recovering from sleep_on_timeout,
			   resync hwptr and swptr */
			dmabuf->count = 0;
			dmabuf->swptr = dmabuf->hwptr;
		}
		swptr = dmabuf->swptr;
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count + cnt > dmabuf->dmasize)
			cnt = dmabuf->dmasize - dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			unsigned long tmo;
			/* buffer is full, start the dma machine and wait for data to be played */
			start_dac(state);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret) ret = -EAGAIN;
				return ret;
			}
			/* No matter how much data left in the buffer, we have to wait untill
			   CSO == ESO/2 or CSO == ESO when address engine interrupts */
			tmo = (dmabuf->dmasize * HZ) / (dmabuf->rate * 2);
			tmo >>= sample_shift[dmabuf->fmt];
			/* There are two situations when sleep_on_timeout returns, one is when the
			   interrupt is serviced correctly and the process is waked up by ISR ON TIME.
			   Another is when timeout is expired, which means that either interrupt is NOT
			   serviced correctly (pending interrupt) or it is TOO LATE for the process to 
			   be scheduled to run (scheduler latency) which results in a (potential) buffer
			   underrun. And worse, there is NOTHING we can do to prevent it. */
			if (!interruptible_sleep_on_timeout(&dmabuf->wait, tmo)) {
#ifdef DEBUG
				printk(KERN_ERR "trident: schedule timeout, "
				       "dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       dmabuf->dmasize, dmabuf->fragsize, dmabuf->count,
				       dmabuf->hwptr, dmabuf->swptr);
#endif
				/* a buffer underrun, we delay the recovery untill next time the
				   while loop begin and we REALLY have data to play */
			}
			if (signal_pending(current)) {
				if (!ret) ret = -ERESTARTSYS;
				return ret;
			}
			continue;
		}
		if (copy_from_user(dmabuf->rawbuf + swptr, buffer, cnt)) {
			if (!ret) ret = -EFAULT;
			return ret;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;

		spin_lock_irqsave(&state->card->lock, flags);
		dmabuf->swptr = swptr;
		dmabuf->count += cnt;
		dmabuf->endcleared = 0;
		spin_unlock_irqrestore(&state->card->lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac(state);
	}
	return ret;
}

static unsigned int trident_poll(struct file *file, struct poll_table_struct *wait)
{
	struct trident_state *s = (struct trident_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &s->dma_dac.wait, wait);
	if (file->f_mode & FMODE_READ)
		poll_wait(file, &s->dma_adc.wait, wait);

	spin_lock_irqsave(&s->card->lock, flags);
	trident_update_ptr(s);
	if (file->f_mode & FMODE_READ) {
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->dma_dac.mapped) {
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) 
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)s->dma_dac.dmasize >= s->dma_dac.count + (signed)s->dma_dac.fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&s->card->lock, flags);

	return mask;
}

static int trident_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct trident_state *state = (struct trident_state *)file->private_data;
	struct dmabuf *dmabuf;
	int ret;
	unsigned long size;

	VALIDATE_STATE(state);
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf(state, 0)) != 0)
			return ret;
		dmabuf = &state->dma_dac;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf(state, 1)) != 0)
			return ret;
		dmabuf = &state->dma_adc;
	} else 
		return -EINVAL;

	if (vma->vm_pgoff != 0)
		return -EINVAL;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << dmabuf->buforder))
		return -EINVAL;
	if (remap_page_range(vma->vm_start, virt_to_phys(dmabuf->rawbuf),
			     size, vma->vm_page_prot))
		return -EAGAIN;
	dmabuf->mapped = 1;

	return 0;
}

static int trident_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct trident_state *state = (struct trident_state *)file->private_data;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	int val, mapped, ret;

	VALIDATE_STATE(state);
	mapped = ((file->f_mode & FMODE_WRITE) && state->dma_dac.mapped) ||
		((file->f_mode & FMODE_READ) && state->dma_adc.mapped);
#ifdef DEBUG
	printk("trident: trident_ioctl, command = %2d, arg = 0x%08x\n",
	       _IOC_NR(cmd), arg ? *(int *)arg : 0);
#endif

	switch (cmd) 
	{
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(state);
			synchronize_irq();
			state->dma_dac.ready = 0;
			state->dma_dac.swptr = state->dma_dac.hwptr = 0;
			state->dma_dac.count = state->dma_dac.total_bytes = 0;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(state);
			synchronize_irq();
			state->dma_adc.ready = 0;
			state->dma_adc.swptr = state->dma_adc.hwptr = 0;
			state->dma_adc.count = state->dma_adc.total_bytes = 0;
		}
		return 0;

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_dac(state, file->f_flags & O_NONBLOCK);
		return 0;

	case SNDCTL_DSP_SPEED: /* set smaple rate */
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val >= 0) {
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(state);
				state->dma_dac.ready = 0;
				spin_lock_irqsave(&state->card->lock, flags);
				trident_set_dac_rate(state, val);
				spin_unlock_irqrestore(&state->card->lock, flags);
			}
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
				state->dma_adc.ready = 0;
				spin_lock_irqsave(&state->card->lock, flags);
				trident_set_adc_rate(state, val);
				spin_unlock_irqrestore(&state->card->lock, flags);
			}
		}
		return put_user((file->f_mode & FMODE_READ) ? state->dma_adc.rate :
				state->dma_dac.rate,
				(int *)arg);

	case SNDCTL_DSP_STEREO: /* set stereo or mono channel */
		get_user_ret(val, (int *)arg, -EFAULT);
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(state);
			state->dma_dac.ready = 0;
			if (val)
				state->dma_dac.fmt |= TRIDENT_FMT_STEREO;
			else
				state->dma_dac.fmt &= ~TRIDENT_FMT_STEREO;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(state);
			state->dma_adc.ready = 0;
			if (val)
				state->dma_adc.fmt |= TRIDENT_FMT_STEREO;
			else
				state->dma_adc.fmt &= ~TRIDENT_FMT_STEREO;
		}
		return 0;

	case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf(state, 0)))
				return val;
			return put_user(state->dma_dac.fragsize, (int *)arg);
		}
		if (file->f_mode & FMODE_READ) {
			if ((val = prog_dmabuf(state, 1)))
				return val;
			return put_user(state->dma_adc.fragsize, (int *)arg);
		}

	case SNDCTL_DSP_GETFMTS: /* Returns a mask of supported sample format*/
		return put_user(AFMT_S16_LE|AFMT_U16_LE|AFMT_S8|AFMT_U8, (int *)arg);

	case SNDCTL_DSP_SETFMT: /* Select sample format */
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val != AFMT_QUERY) {
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(state);
				state->dma_dac.ready = 0;
				if (val == AFMT_S16_LE)
					state->dma_dac.fmt |= TRIDENT_FMT_16BIT;
				else
					state->dma_dac.fmt &= ~TRIDENT_FMT_16BIT;
			}
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
				state->dma_adc.ready = 0;
				if (val == AFMT_S16_LE)
					state->dma_adc.fmt |= TRIDENT_FMT_16BIT;
				else
					state->dma_adc.fmt &= ~TRIDENT_FMT_16BIT;
			}
		}
		if (file->f_mode & FMODE_WRITE)
			return put_user((state->dma_dac.fmt & TRIDENT_FMT_16BIT) ?
					AFMT_S16_LE : AFMT_U8, (int *)arg);
		else
			return put_user((state->dma_adc.fmt & TRIDENT_FMT_16BIT) ?
					AFMT_S16_LE : AFMT_U8, (int *)arg);

	case SNDCTL_DSP_CHANNELS:
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val != 0) {
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(state);
				state->dma_dac.ready = 0;
				if (val >= 2)
					state->dma_dac.fmt |= TRIDENT_FMT_STEREO;
				else
					state->dma_dac.fmt &= ~TRIDENT_FMT_STEREO;
			}
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
				state->dma_adc.ready = 0;
				if (val >= 2)
					state->dma_adc.fmt |= TRIDENT_FMT_STEREO;
				else
					state->dma_adc.fmt &= ~TRIDENT_FMT_STEREO;
			}
		}
		if (file->f_mode & FMODE_WRITE)
			return put_user((state->dma_dac.fmt & TRIDENT_FMT_STEREO) ? 2 : 1,
					(int *)arg);
		else
			return put_user((state->dma_adc.fmt & TRIDENT_FMT_STEREO) ? 2 : 1,
					(int *)arg);
	case SNDCTL_DSP_POST:
		/* FIXME: the same as RESET ?? */
		return 0;

	case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && state->dma_adc.subdivision) ||
		    (file->f_mode & FMODE_WRITE && state->dma_dac.subdivision))
			return -EINVAL;
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		if (file->f_mode & FMODE_READ)
			state->dma_adc.subdivision = val;
		if (file->f_mode & FMODE_WRITE)
			state->dma_dac.subdivision = val;
		return 0;

	case SNDCTL_DSP_SETFRAGMENT:
		get_user_ret(val, (int *)arg, -EFAULT);
		if (file->f_mode & FMODE_READ) {
			state->dma_adc.ossfragshift = val & 0xffff;
			state->dma_adc.ossmaxfrags = (val >> 16) & 0xffff;
			if (state->dma_adc.ossfragshift < 4)
				state->dma_adc.ossfragshift = 4;
			if (state->dma_adc.ossfragshift > 15)
				state->dma_adc.ossfragshift = 15;
			if (state->dma_adc.ossmaxfrags < 4)
				state->dma_adc.ossmaxfrags = 4;
		}
		if (file->f_mode & FMODE_WRITE) {
			state->dma_dac.ossfragshift = val & 0xffff;
			state->dma_dac.ossmaxfrags = (val >> 16) & 0xffff;
			if (state->dma_dac.ossfragshift < 4)
				state->dma_dac.ossfragshift = 4;
			if (state->dma_dac.ossfragshift > 15)
				state->dma_dac.ossfragshift = 15;
			if (state->dma_dac.ossmaxfrags < 4)
				state->dma_dac.ossmaxfrags = 4;
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!state->dma_dac.enable && (val = prog_dmabuf(state, 0)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		abinfo.fragsize = state->dma_dac.fragsize;
		abinfo.bytes = state->dma_dac.dmasize - state->dma_dac.count;
		abinfo.fragstotal = state->dma_dac.numfrag;
		abinfo.fragments = abinfo.bytes >> state->dma_dac.fragshift;      
		spin_unlock_irqrestore(&state->card->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!state->dma_adc.enable && (val = prog_dmabuf(state, 1)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		abinfo.fragsize = state->dma_adc.fragsize;
		abinfo.bytes = state->dma_adc.count;
		abinfo.fragstotal = state->dma_adc.numfrag;
		abinfo.fragments = abinfo.bytes >> state->dma_adc.fragshift;      
		spin_unlock_irqrestore(&state->card->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETCAPS:
	    return put_user(/* DSP_CAP_DUPLEX|*/DSP_CAP_REALTIME|DSP_CAP_TRIGGER|DSP_CAP_MMAP,
			    (int *)arg);

	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if (file->f_mode & FMODE_READ && state->dma_adc.enable) 
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & FMODE_WRITE && state->dma_dac.enable) 
			val |= PCM_ENABLE_OUTPUT;
		return put_user(val, (int *)arg);

	case SNDCTL_DSP_SETTRIGGER:
		get_user_ret(val, (int *)arg, -EFAULT);
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!state->dma_adc.ready && (ret = prog_dmabuf(state, 1)))
					return ret;
				start_adc(state);
			} 
			else
				stop_adc(state);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!state->dma_dac.ready && (ret = prog_dmabuf(state, 0)))
					return ret;
				start_dac(state);
			} 
			else
				stop_dac(state);
		}
		return 0;

	case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		cinfo.bytes = state->dma_adc.total_bytes;
		cinfo.blocks = state->dma_adc.count >> state->dma_adc.fragshift;
		cinfo.ptr = state->dma_adc.hwptr;
		if (state->dma_adc.mapped)
			state->dma_adc.count &= state->dma_adc.fragsize-1;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));
		
	case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		cinfo.bytes = state->dma_dac.total_bytes;
		cinfo.blocks = state->dma_dac.count >> state->dma_dac.fragshift;
		cinfo.ptr = state->dma_dac.hwptr;
		if (state->dma_dac.mapped)
			state->dma_dac.count &= state->dma_dac.fragsize-1;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

	case SNDCTL_DSP_SETDUPLEX:
		/* XXX fix */
		return 0;

	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		val = state->dma_dac.count;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return put_user(val, (int *)arg);

	case SOUND_PCM_READ_RATE:
		return put_user((file->f_mode & FMODE_READ) ? state->dma_adc.rate :
				state->dma_dac.rate, (int *)arg);

	case SOUND_PCM_READ_CHANNELS:
		if (file->f_mode & FMODE_WRITE)
			return put_user((state->dma_dac.fmt & TRIDENT_FMT_STEREO) ? 2 : 1,
					(int *)arg);
		else
			return put_user((state->dma_adc.fmt & TRIDENT_FMT_STEREO) ? 2 : 1,
					(int *)arg);
		
	case SOUND_PCM_READ_BITS:
		if (file->f_mode & FMODE_WRITE)
			return put_user((state->dma_dac.fmt & TRIDENT_FMT_16BIT) ?
					AFMT_S16_LE : AFMT_U8, (int *)arg);
		else
			return put_user((state->dma_adc.fmt & TRIDENT_FMT_16BIT) ?
					AFMT_S16_LE : AFMT_U8, (int *)arg);
	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_WRITE_FILTER:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;
		
	}
	return -EINVAL;
}

static int trident_open(struct inode *inode, struct file *file)
{
	int i = 0;
	int minor = MINOR(inode->i_rdev);
	struct trident_card *card = devs;
	struct trident_state *state = NULL;

	/* find an avaiable virtual channel (instance of /dev/dsp) */
	while (card != NULL) {
		for (i = 0; i < NR_HW_CH; i++) {
			if (card->states[i] == NULL) {
				state = card->states[i] = (struct trident_state *)
					kmalloc(sizeof(struct trident_state), GFP_KERNEL);
				if (state == NULL)
					return -ENOMEM;
				memset(state, 0, sizeof(struct trident_state));
				goto found_virt;
			}
		}
		card = card->next;
	}
	/* no more virtual channel avaiable */
	if (!state)
		return -ENODEV;

 found_virt:
	/* found a free virtual channel, allocate hardware channels */
	if (file->f_mode & FMODE_READ)
		if ((state->dma_adc.channel = trident_alloc_pcm_channel(card)) == NULL) {
			kfree (card->states[i]);
			card->states[i] = NULL;;
			return -ENODEV;
		}
	if (file->f_mode & FMODE_WRITE)
		if ((state->dma_dac.channel = trident_alloc_pcm_channel(card)) == NULL) {
			kfree (card->states[i]);
			card->states[i] = NULL;
			if (file->f_mode & FMODE_READ)
				/* free previously allocated hardware channel */
				trident_free_pcm_channel(card, state->dma_adc.channel->num);
			return -ENODEV;
		}

	/* initialize the virtual channel */
	state->virt = i;
	state->card = card;
	state->magic = TRIDENT_STATE_MAGIC;
	init_waitqueue_head(&state->dma_adc.wait);
	init_waitqueue_head(&state->dma_dac.wait);
	init_MUTEX(&state->open_sem);
	file->private_data = state;

	down(&state->open_sem);

	/* set default sample format, Refer to  OSS Programmer's Guide */
	if (file->f_mode & FMODE_READ) {
		/* FIXME: Trident 4d can only record in singed 16-bits stereo, 48kHz sample */
		state->dma_adc.fmt = TRIDENT_FMT_STEREO|TRIDENT_FMT_16BIT;
		state->dma_adc.ossfragshift = 0;
		state->dma_adc.ossmaxfrags  = 0;
		state->dma_adc.subdivision  = 0;
		trident_set_adc_rate(state, 48000);
	}

	/* according to OSS document, /dev/dsp should be default to unsigned 8-bits, 
	   mono, with sample rate 8kHz and /dev/dspW will accept 16-bits sample */
	if (file->f_mode & FMODE_WRITE) {
		state->dma_dac.fmt &= ~TRIDENT_FMT_MASK;
		if ((minor & 0xf) == SND_DEV_DSP16)
			 state->dma_dac.fmt |= TRIDENT_FMT_16BIT;
		state->dma_dac.ossfragshift = 0;
		state->dma_dac.ossmaxfrags  = 0;
		state->dma_dac.subdivision  = 0;
		trident_set_dac_rate(state, 8000);
	}

	state->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	up(&state->open_sem);

	//FIXME put back in
	//MOD_INC_USE_COUNT;
	return 0;
}

static int trident_release(struct inode *inode, struct file *file)
{
	struct trident_state *state = (struct trident_state *)file->private_data;

	VALIDATE_STATE(state);


	if (file->f_mode & FMODE_WRITE) {
		trident_clear_tail(state);
		drain_dac(state, file->f_flags & O_NONBLOCK);
	}

	/* stop DMA state machine and free DMA buffers/channels */
	down(&state->open_sem);

	if (file->f_mode & FMODE_WRITE) {
		stop_dac(state);
		dealloc_dmabuf(&state->dma_dac);
		trident_free_pcm_channel(state->card, state->dma_dac.channel->num);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(state);
		dealloc_dmabuf(&state->dma_adc);
		trident_free_pcm_channel(state->card, state->dma_adc.channel->num);
	}

	kfree(state->card->states[state->virt]);
	state->card->states[state->virt] = NULL;
	state->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);

	/* we're covered by the open_sem */
	up(&state->open_sem);

	//FIXME put back in
	//MOD_DEC_USE_COUNT;
	return 0;
}

static /*const*/ struct file_operations trident_audio_fops = {
	&trident_llseek,
	&trident_read,
	&trident_write,
	NULL,	/* readdir */
	&trident_poll,
	&trident_ioctl,
	&trident_mmap,
	&trident_open,
	NULL,	/* flush */
	&trident_release,
	NULL,	/* fsync */
	NULL,	/* fasync */
	NULL,	/* lock */
};

/* trident specific AC97 functions */
/* Write AC97 mixer registers */
static void trident_ac97_set(struct ac97_codec *codec, u8 reg, u16 val)
{
	struct trident_card *card = (struct trident_card *)codec->private_data;
	unsigned int address, mask, busy;
	unsigned short count  = 0xffff;
	unsigned long flags;
	u32 data;

	data = ((u32) val) << 16;

	switch (card->pci_id)
	{
	default:
	case PCI_DEVICE_ID_SI_7018:
		address = SI_AC97_WRITE;
		mask = SI_AC97_BUSY_WRITE | SI_AC97_AUDIO_BUSY;
		if (codec->id)
		    mask |= SI_AC97_SECONDARY;
		busy = SI_AC97_BUSY_WRITE;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		address = DX_ACR0_AC97_W;
		mask = busy = DX_AC97_BUSY_WRITE;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		address = NX_ACR1_AC97_W;
		mask = NX_AC97_BUSY_WRITE;
		if (codec->id)
		    mask |= NX_AC97_WRITE_SECONDARY;
		busy = NX_AC97_BUSY_WRITE;
		break;
	}

	spin_lock_irqsave(&card->lock, flags);
	do {
		if ((inw(TRID_REG(card, address)) & busy) == 0)
			break;
	} while (count--);


	data |= (mask | (reg & AC97_REG_ADDR));

	if (count == 0) {
		printk(KERN_ERR "trident: AC97 CODEC write timed out.\n");
		spin_unlock_irqrestore(&card->lock, flags);
		return;
	}

	outl(data, TRID_REG(card, address));
	spin_unlock_irqrestore(&card->lock, flags);
}

/* Read AC97 codec registers */
static u16 trident_ac97_get(struct ac97_codec *codec, u8 reg)
{
	struct trident_card *card = (struct trident_card *)codec->private_data;
	unsigned int address, mask, busy;
	unsigned short count = 0xffff;
	unsigned long flags;
	u32 data;

	switch (card->pci_id)
	{
	default:
	case PCI_DEVICE_ID_SI_7018:
		address = SI_AC97_READ;
		mask = SI_AC97_BUSY_READ | SI_AC97_AUDIO_BUSY;
		if (codec->id)
		    mask |= SI_AC97_SECONDARY;
		busy = SI_AC97_BUSY_READ;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		address = DX_ACR1_AC97_R;
		mask = busy = DX_AC97_BUSY_READ;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		if (codec->id)
			address = NX_ACR3_AC97_R_SECONDARY;
		else
			address = NX_ACR2_AC97_R_PRIMARY;
		mask = NX_AC97_BUSY_READ;
		busy = 0x0c00;
		break;
	}

	data = (mask | (reg & AC97_REG_ADDR));

	spin_lock_irqsave(&card->lock, flags);
	outl(data, TRID_REG(card, address));
	do {
		data = inl(TRID_REG(card, address));
		if ((data & busy) == 0)
			break;
	} while (count--);
	spin_unlock_irqrestore(&card->lock, flags);

	if (count == 0) {
		printk(KERN_ERR "trident: AC97 CODEC read timed out.\n");
		data = 0;
	}
	return ((u16) (data >> 16));
}

/* OSS /dev/mixer file operation methods */
static int trident_open_mixdev(struct inode *inode, struct file *file)
{
	int i;
	int minor = MINOR(inode->i_rdev);
	struct trident_card *card = devs;

	for (card = devs; card != NULL; card = card->next)
		for (i = 0; i < NR_AC97; i++)
			if (card->ac97_codec[i] != NULL &&
			    card->ac97_codec[i]->dev_mixer == minor)
				goto match;

	if (!card)
		return -ENODEV;

 match:
	file->private_data = card->ac97_codec[i];

	//FIXME put back in
	//MOD_INC_USE_COUNT;
	return 0;
}

static int trident_release_mixdev(struct inode *inode, struct file *file)
{
	//struct ac97_codec *codec = (struct ac97_codec *)file->private_data;

	//FIXME put back in
	//MOD_DEC_USE_COUNT;
	return 0;
}

static int trident_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct ac97_codec *codec = (struct ac97_codec *)file->private_data;

	return codec->mixer_ioctl(codec, cmd, arg);
}

static /*const*/ struct file_operations trident_mixer_fops = {
	&trident_llseek,
	NULL,  /* read */
	NULL,  /* write */
	NULL,  /* readdir */
	NULL,  /* poll */
	&trident_ioctl_mixdev,
	NULL,  /* mmap */
	&trident_open_mixdev,
	NULL,	/* flush */
	&trident_release_mixdev,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* lock */
};

/* AC97 codec initialisation. */
static int __init trident_ac97_init(struct trident_card *card)
{
	int num_ac97 = 0;
	int ready_2nd = 0;
	struct ac97_codec *codec;

	/* initialize controller side of AC link, and find out if secondary codes
	   really exist */
	switch (card->pci_id)
	{
	case PCI_DEVICE_ID_SI_7018:
		/* disable AC97 GPIO interrupt */
		outl(0x00, TRID_REG(card, SI_AC97_GPIO));
		/* stop AC97 cold reset process */
		outl(PCMOUT|SECONDARY_ID, TRID_REG(card, SI_SERIAL_INTF_CTRL));
		ready_2nd = inl(TRID_REG(card, SI_SERIAL_INTF_CTRL)); 
		ready_2nd &= SI_AC97_SECONDARY_READY;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		/* playback on */
		outl(DX_AC97_PLAYBACK, TRID_REG(card, DX_ACR2_AC97_COM_STAT));
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		/* enable AC97 Output Slot 3,4 (PCM Left/Right Playback) */
		outl(NX_AC97_PCM_OUTPUT, TRID_REG(card, NX_ACR0_AC97_COM_STAT));
		ready_2nd = inl(TRID_REG(card, NX_ACR0_AC97_COM_STAT));
		ready_2nd &= NX_AC97_SECONDARY_READY;
		break;
	}

	for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {
		if ((codec = kmalloc(sizeof(struct ac97_codec), GFP_KERNEL)) == NULL)
			return -1;
		memset(codec, 0, sizeof(struct ac97_codec));

		/* initialize some basic codec information, other fields will be filled
		   in ac97_probe_codec */
		codec->private_data = card;
		codec->id = num_ac97;
		/* controller specific low level AC97 access function */
		codec->codec_read = trident_ac97_get;
		codec->codec_write = trident_ac97_set;

		if (ac97_probe_codec(codec) == 0)
			break;

		if ((codec->dev_mixer = register_sound_mixer(&trident_mixer_fops, -1)) < 0) {
			printk(KERN_ERR "trident: couldn't register mixer!\n");
			kfree(codec);
			break;
		}

		card->ac97_codec[num_ac97] = codec;

		/* if there is no secondary codec at all, don't probe any more */
		if (!ready_2nd)
			return num_ac97+1;
	}

	return num_ac97;
}

/* install the driver, we do not allocate hardware channel nor DMA buffer now, they are defered 
   untill "ACCESS" time (in prog_dmabuf calles by open/read/write/ioctl/mmap) */
static int __init trident_install(struct pci_dev *pcidev, struct pci_audio_info *pci_info)
{
	u16 w;
	unsigned long iobase;
	struct trident_card *card;

	iobase = pcidev->resource[0].start;
	if (check_region(iobase, 256)) {
		printk(KERN_ERR "trident: can't allocate I/O space at 0x%4.4lx\n",
		       iobase);
		return 0;
	}

	/* just to be sure that IO space and bus master is on */
	pci_set_master(pcidev);	
	pci_read_config_word(pcidev, PCI_COMMAND, &w);
	w |= PCI_COMMAND_IO|PCI_COMMAND_MASTER;
	pci_write_config_word(pcidev, PCI_COMMAND, w);

	if ((card = kmalloc(sizeof(struct trident_card), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "trident: out of memory\n");
		return 0;
	}
	memset(card, 0, sizeof(*card));

	card->iobase = iobase;
	card->pci_info = pci_info;
	card->pci_id = pci_info->device;
	card->irq = pcidev->irq;
	card->next = devs;
	card->magic = TRIDENT_CARD_MAGIC;
	card->banks[BANK_A].addresses = &bank_a_addrs;
	card->banks[BANK_A].bitmap = 0UL;
	card->banks[BANK_B].addresses = &bank_b_addrs;
	card->banks[BANK_B].bitmap = 0UL;
	spin_lock_init(&card->lock);
	devs = card;	

	printk(KERN_INFO "trident: %s found at IO 0x%04lx, IRQ %d\n",
	       card->pci_info->name, card->iobase, card->irq);

	/* claim our iospace and irq */
	request_region(card->iobase, 256, card->pci_info->name);
	if (request_irq(card->irq, &trident_interrupt, SA_SHIRQ, card->pci_info->name, card)) {
		printk(KERN_ERR "trident: unable to allocate irq %d\n", card->irq);
		release_region(card->iobase, 256);
		kfree(card);
		return 0;
	}
	/* register /dev/dsp */
	if ((card->dev_audio = register_sound_dsp(&trident_audio_fops, -1)) < 0) {
		printk(KERN_ERR "trident: coundn't register DSP device!\n");
		release_region(iobase, 256);
		free_irq(card->irq, card);
		kfree(card);
		return 0;
	}
	/* initilize AC97 codec and register /dev/mixer */
	if (trident_ac97_init(card) <= 0) {
		unregister_sound_dsp(card->dev_audio);
		release_region(iobase, 256);
		free_irq(card->irq, card);
		kfree(card);
		return 0;
	}
	outl(0x00, TRID_REG(card, T4D_MUSICVOL_WAVEVOL));

	/* Enable Address Engine Interrupts */
	trident_enable_loop_interrupts(card);

	return 1; 
}

static int __init init_trident(void)
{
	struct pci_dev *pcidev = NULL;
	int foundone = 0;
	int i;

	if (!pci_present())   /* No PCI bus in this machine! */
		return -ENODEV;

	printk(KERN_INFO "Trident 4DWave/SiS 7018 PCI Audio, version "
	       DRIVER_VERSION ", " __TIME__ " " __DATE__ "\n");

	for (i = 0; i < sizeof (pci_audio_devices); i++) {
		pcidev = NULL;
		while ((pcidev = pci_find_device(pci_audio_devices[i].vendor,
						 pci_audio_devices[i].device,
						 pcidev)) != NULL) {
			foundone += trident_install(pcidev, pci_audio_devices + i);
		}
	}

	if (!foundone)
		return -ENODEV;
	return 0;
}

MODULE_AUTHOR("Alan Cox, Aaron Holtzman, Ollie Lho");
MODULE_DESCRIPTION("Trident 4DWave/SiS 7018 PCI Audio Driver");

static void __exit cleanup_trident(void)
{
	while (devs != NULL) {
		int i;
		/* Kill interrupts, and SP/DIF */
		trident_disable_loop_interrupts(devs);

		/* free hardware resources */
		free_irq(devs->irq, devs);
		release_region(devs->iobase, 256);

		/* unregister audio devices */
		for (i = 0; i < NR_AC97; i++)
			if (devs->ac97_codec[i] != NULL) {
				unregister_sound_mixer(devs->ac97_codec[i]->dev_mixer);
				kfree (devs->ac97_codec[i]);
			}
		unregister_sound_dsp(devs->dev_audio);

		kfree(devs);
		devs = devs->next;
	}
}

module_init(init_trident);
module_exit(cleanup_trident);
