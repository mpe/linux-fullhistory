/*****************************************************************************
 *
 *      ESS Maestro/Maestro-2/Maestro-2E driver for Linux 2.2.x
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	(c) Copyright 1999	 Alan Cox <alan.cox@linux.org>
 *
 *	Based heavily on SonicVibes.c:
 *      Copyright (C) 1998-1999  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	Heavily modified by Zach Brown <zab@redhat.com> based on lunch
 *	with ESS engineers.  Many thanks to Howard Kim for providing 
 *	contacts and hardware.  Honorable mention goes to Eric 
 *	Brombaugh for the BOB routines and nice hacking in general.
 *
 *  Supported devices:
 *  /dev/dsp0-7    standard /dev/dsp device, (mostly) OSS compatible
 *  /dev/mixer  standard /dev/mixer device, (mostly) OSS compatible
 *
 *  Hardware Description
 *
 *	A working Maestro setup contains the Maestro chip wired to a 
 *	codec or 2.  In the Maestro we have the APUs, the ASP, and the
 *	Wavecache.  The APUs can be though of as virtual audio routing
 *	channels.  They can take data from a number of sources and perform
 *	basic encodings of the data.  The wavecache is a storehouse for
 *	PCM data.  Typically it deals with PCI and interracts with the
 *	APUs.  The ASP is a wacky DSP like device that ESS is loathe
 *	to release docs on.  Thankfully it isn't required on the Maestro
 *	until you start doing insane things like FM emulation and surround
 *	encoding.  The codecs are almost always AC-97 compliant codecs, 
 *	but it appears that early Maestros may have had PT101 (an ESS
 *	part?) wired to them.  The only real difference in the Maestro
 *	families is external goop like docking capability, memory for
 *	the ASP, and trivial initialization differences.
 *
 *  Driver Operation
 *
 *	We only drive the APU/Wavecache as typical DACs and drive the
 *	mixers in the codecs.  There are 64 APUs.  We assign 4 to each
 *	/dev/dsp? device.  2 channels for both in and out.
 *
 *	For output we maintain a ring buffer of data that we are dmaing
 *	to the card.  In mono operation this is nice and easy.  When
 *	we receive data we tack it onto the ring buffer and make sure
 *	the APU assigned to it is playing over the data.  When we fill
 *	the ring buffer we put the client to sleep until there is
 *	room again.  Easy.
 *
 *	However, this starts to stink when we use stereo.  The APUs
 *	supposedly can decode LRLR packed stereo data, but it
 *	doesn't work.  So we're forced to use dual mono APUs walking over
 *	mono encoded data.  This requires us to split the input from
 *	the client and complicates the buffer maths tremendously.  Ick.
 *
 *	Once input is actually written, it will be worth pointing out
 *	that only 44/16 input actually works.
 *
 * TODO
 *	Leaks memory?
 *	recording is horribly broken
 *	apus or dmas get out sync
 *	bob can be started twice
 *	anyone have a pt101 codec?
 *	ess's ac97 codec (es1921) doesn't work
 *	generally test across codecs..
 *	mmap(), but beware stereo encoding nastiness.
 */

/*****************************************************************************/

      
#ifdef MODULE
#include <linux/module.h>
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif
#endif

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
#include <asm/spinlock.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>

#include "maestro.h"
#include "maestro_tables.h"

/* --------------------------------------------------------------------- */

#define M_DEBUG 1

#ifdef M_DEBUG
static int debug=0;
#define M_printk(args...) {if (debug) printk(args);}
#else
#define M_printk(x)
#endif

/* --------------------------------------------------------------------- */

#define DRIVER_VERSION "0.03"

#ifndef PCI_VENDOR_ESS
#define PCI_VENDOR_ESS			0x125D
#define PCI_DEVICE_ID_ESS_ESS1968	0x1968		/* Maestro 2	*/
#define PCI_DEVICE_ID_ESS_ESS1978      	0x1978		/* Maestro 2E	*/

#define PCI_VENDOR_ESS_OLD		0x1285		/* vendor id for maestro 1 */
#define PCI_DEVICE_ID_ESS_ESS0100	0x0100		/* maestro 1 */
#endif /* PCI_VENDOR_ESS */

#define ESS_CHAN_HARD		0x100

#define ESS_CFMT_STEREO     0x01
#define ESS_CFMT_16BIT      0x02
#define ESS_CFMT_MASK       0x03
#define ESS_CFMT_ASHIFT     0   
#define ESS_CFMT_CSHIFT     4

#define ESS_ENABLE_PE		1
#define ESS_ENABLE_RE		2

#define ESS_STATE_MAGIC		0x125D1968
#define ESS_CARD_MAGIC		0x19283746

#define DAC_RUNNING		1
#define ADC_RUNNING		2

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

enum card_types_t {
	TYPE_MAESTRO,
	TYPE_MAESTRO2,
	TYPE_MAESTRO2E
};

static const char *card_names[]={
	[TYPE_MAESTRO] = "ESS Maestro",
	[TYPE_MAESTRO2] = "ESS Maestro 2",
	[TYPE_MAESTRO2E] = "ESS Maestro 2E"
};

#define SND_DEV_DSP16   5 

/* --------------------------------------------------------------------- */

struct ess_state {
	unsigned int magic;
	/* FIXME: we probably want submixers in here, but only one record pair */
	u8 apu[4];		/* Left, Right, Left In, Right In */
	u8 apu_mode[4];		/* Running mode for this APU */
	u8 apu_pan[4];		/* Panning setup for this APU */
	struct ess_card *card;	/* Card info */
	/* wave stuff */
	unsigned int rateadc, ratedac;
	unsigned char fmt, enable;

	spinlock_t lock;
	struct semaphore open_sem;
	mode_t open_mode;
	wait_queue_head_t open_wait;

	/* soundcore stuff */
	int dev_audio;

	struct dmabuf {
		void *rawbuf;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;
		unsigned hwptr, swptr;
		unsigned total_bytes;
		int count;
		unsigned error; /* over/underrun */
		wait_queue_head_t wait;
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
		u16 base;		/* Offset for ptr */
	} dma_dac, dma_adc;
};
	
struct ess_card {
	unsigned int magic;

	/* We keep maestro cards in a linked list */
	struct ess_card *next;

	int dev_mixer;

	int card_type;

	/* as most of this is static,
		perhaps it should be a pointer to a global struct */
	struct mixer_goo {
		int modcnt;
		int supported_mixers;
		int stereo_mixers;
		int record_sources;
		/* the caller must guarantee arg sanity before calling these */
		int (*read_mixer)(struct ess_card *card, int index);
		void (*write_mixer)(struct ess_card *card,int mixer, int vol);
		int (*recmask_io)(struct ess_card *card,int rw,int mask);
	} mix;
	
	struct ess_state channels[8];
	u16 maestro_map[32];	/* Register map */

	/* hardware resources */
	u32 iobase;
	u32 irq;

};

extern __inline__ unsigned ld2(unsigned int x)
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


/* --------------------------------------------------------------------- */

static struct ess_card *devs = NULL;

/* --------------------------------------------------------------------- */


/*
 *	ESS Maestro AC97 codec programming interface.
 */
	 
static void maestro_ac97_set(int io, u8 cmd, u16 val)
{
	int i;
	/*
	 *	Wait for the codec bus to be free 
	 */
	 
	for(i=0;i<10000;i++)
	{
		if(!(inb(io+ESS_AC97_INDEX)&1))
			break;
	}
	/*
	 *	Write the bus
	 */ 
	outw(val, io+ESS_AC97_DATA);
	udelay(1);
	/* should actually be delaying 10 milliseconds? */
	outb(cmd, io+ESS_AC97_INDEX);
	udelay(1);
}

static u16 maestro_ac97_get(int io, u8 cmd)
{
	int sanity=100000;
	u16 data;
	int i;
	
	/*
	 *	Wait for the codec bus to be free 
	 */
	 
	for(i=0;i<10000;i++)
	{
		if(!(inb(io+ESS_AC97_INDEX)&1))
			break;
	}

	outb(cmd|0x80, io+ESS_AC97_INDEX);
	udelay(1);
	
	while(inb(io+ESS_AC97_INDEX)&1)
	{
		sanity--;
		if(!sanity)
		{
			printk(KERN_ERR "maestro: ac97 codec read timeout.\n");
			return 0;
		}
	}
	data=inw(io+ESS_AC97_DATA);
	udelay(1);
	return data;
}

/*
 *	The Maestro can be wired to a standard AC97 compliant codec
 *	(see www.intel.com for the pdf's on this), or to a PT101 codec
 *	which appears to be the ES1918 (data sheet on the esstech.com.tw site)
 *
 *	The PT101 setup is untested.
 */
 
static u16 maestro_ac97_init(int iobase)
{

	int val, seid, caps;
	u16 vend1, vend2;

#if 0 /* an experiment for another time */
		/* aim at the second codec */
		outw(0x21, iobase+0x38);
		outw(0x5555, iobase+0x3a);
		outw(0x5555, iobase+0x3c);
		udelay(1);
		vend1 = maestro_ac97_get(iobase, 0x7c);
		vend2 = maestro_ac97_get(iobase, 0x7e);
		if(vend1 != 0xffff || vend2 != 0xffff) {
			printk("maestro: It seems you have a second codec: %x %x, please report this.\n",
				vend1,vend2);
		}
		/* back to the first */
		outw(0x0, iobase+0x38);
		outw(0x0, iobase+0x3a);
		outw(0x0, iobase+0x3c);
#endif

	/* should make sure we're ac97 2.1? */
	vend1 = maestro_ac97_get(iobase, 0x7c);
	vend2 = maestro_ac97_get(iobase, 0x7e);

	val = maestro_ac97_get(iobase, 0x00);
	seid = val >> 8;
	caps = val & 255;

	printk(KERN_INFO "maestro: AC97 Codec detected: v: 0x%2x%2x 3d: 0x%x caps: 0x%x\n",
		vend1,vend2,seid, caps);

	switch ((long)(vend1 << 16) | vend2) {
		/* magic vendor specifc init code, _no_ idea what these do */
#if 0
	case 0x83847609:	/* ESS 1921 */
		maestro_ac97_set(iobase,0x76,0xABBA); /* o/~ Take a chance on me o/~ */
		udelay(20);
		maestro_ac97_set(iobase,0x78,0x3002);
		udelay(20);
		maestro_ac97_set(iobase,0x78,0x3802);
		udelay(20);
		break;
#endif
	default: break;
	}

	/* set master, headphone, master mono */
        maestro_ac97_set(iobase, 0x02, 0x0000);
	/* always set headphones to max unmuted, OSS won't 
		let us change it :( */
        maestro_ac97_set(iobase, 0x04, 0x0000);
        maestro_ac97_set(iobase, 0x06, 0x0000);
        maestro_ac97_set(iobase, 0x08, 0x0606);
	/* beep, phone, mic, line, cd video, aux */
	maestro_ac97_set(iobase, 0x0A, 0x1F1F);
	maestro_ac97_set(iobase, 0x0C, 0x1F1F);
	maestro_ac97_set(iobase, 0x0E, 0x1F1F);
	maestro_ac97_set(iobase, 0x10, 0x1F1F);
	maestro_ac97_set(iobase, 0x12, 0x1F1F);
	maestro_ac97_set(iobase, 0x14, 0x1F1F);
	maestro_ac97_set(iobase, 0x16, 0x1F1F);
	/* unmute, but set pcm out to 1/2 */
	maestro_ac97_set(iobase, 0x18, 0x0808);
	/* null record select */
	maestro_ac97_set(iobase, 0x1A, 0x0000);
	/* record gain, record gain mic.. */
	maestro_ac97_set(iobase, 0x1C, 0x0404);
	maestro_ac97_set(iobase, 0x1E, 0x0404);
	/* null misc stuff */
	maestro_ac97_set(iobase, 0x20, 0x0000);
	/* power up various units? */
	maestro_ac97_set(iobase, 0x26, 0x000F);

	return 0;
}

static u16 maestro_pt101_init(int iobase)
{
	printk(KERN_INFO "maestro: PT101 Codec detected, initializing but _not_ installing mixer device.\n");
	/* who knows.. */
	maestro_ac97_set(iobase, 0x2A, 0x0001);
	maestro_ac97_set(iobase, 0x2C, 0x0000);
	maestro_ac97_set(iobase, 0x2C, 0xFFFF);
	maestro_ac97_set(iobase, 0x10, 0x9F1F);
	maestro_ac97_set(iobase, 0x12, 0x0808);
	maestro_ac97_set(iobase, 0x14, 0x9F1F);
	maestro_ac97_set(iobase, 0x16, 0x9F1F);
	maestro_ac97_set(iobase, 0x18, 0x0404);
	maestro_ac97_set(iobase, 0x1A, 0x0000);
	maestro_ac97_set(iobase, 0x1C, 0x0000);
	maestro_ac97_set(iobase, 0x02, 0x0404);
	maestro_ac97_set(iobase, 0x04, 0x0808);
	maestro_ac97_set(iobase, 0x0C, 0x801F);
	maestro_ac97_set(iobase, 0x0E, 0x801F);
	return 0;
}

static void maestro_ac97_reset(int ioaddr)
{
	outw(0x2000,  ioaddr+0x36);
	udelay(20);
	outw(0x0000,  ioaddr+0x36);
	udelay(200);
}

/*
 *	Indirect register access. Not all registers are readable so we
 *	need to keep register state ourselves
 */
 
#define WRITEABLE_MAP	0xEFFFFF
#define READABLE_MAP	0x64003F

/*
 *	The Maestro engineers were a little indirection happy. These indirected
 *	registers themselves include indirect registers at another layer
 */
 
static void maestro_write(struct ess_state *ess, u16 reg, u16 data)
{
	long ioaddr = ess->card->iobase;
	unsigned long flags;
	save_flags(flags);
	cli();
	outw(reg, ioaddr+0x02);
	outw(data, ioaddr+0x00);
	ess->card->maestro_map[reg]=data;
	restore_flags(flags);
}

static u16 maestro_read(struct ess_state *ess, u16 reg)
{
	long ioaddr = ess->card->iobase;
	if(READABLE_MAP & (1<<reg))
	{
		unsigned long flags;
		save_flags(flags);
		cli();
		outw(reg, ioaddr+0x02);
		ess->card->maestro_map[reg]=inw(ioaddr+0x00);
		restore_flags(flags);
	}
	return ess->card->maestro_map[reg];
}

/*
 *	These routines handle accessing the second level indirections to the
 *	wave ram.
 */

/*
 *	The register names are the ones ESS uses (see 104T31.ZIP)
 */
 
#define IDR0_DATA_PORT		0x00
#define IDR1_CRAM_POINTER	0x01
#define IDR2_CRAM_DATA		0x02
#define IDR3_WAVE_DATA		0x03
#define IDR4_WAVE_PTR_LOW	0x04
#define IDR5_WAVE_PTR_HI	0x05
#define IDR6_TIMER_CTRL		0x06
#define IDR7_WAVE_ROMRAM	0x07

static void apu_index_set(struct ess_state *ess, u16 index)
{
	int i;
	maestro_write(ess, IDR1_CRAM_POINTER, index);
	for(i=0;i<1000;i++)
		if(maestro_read(ess, IDR1_CRAM_POINTER)==index)
			return;
	printk(KERN_WARNING "maestro: APU register select failed.\n");
}

static void apu_data_set(struct ess_state *ess, u16 data)
{
	int i;
	for(i=0;i<1000;i++)
	{
		if(maestro_read(ess, IDR0_DATA_PORT)==data)
			return;
		maestro_write(ess, IDR0_DATA_PORT, data);
	}
}

/*
 *	This is the public interface for APU manipulation. It handles the
 *	interlock to avoid two APU writes in parallel etc. Don't diddle
 *	directly with the stuff above.
 */

static void apu_set_register(struct ess_state *ess, u16 channel, u8 reg, u16 data)
{
	unsigned long flags;
	
	if(channel&ESS_CHAN_HARD)
		channel&=~ESS_CHAN_HARD;
	else
	{
		if(channel>3)
			printk("BAD CHANNEL %d.\n",channel);
		else
			channel = ess->apu[channel];
	}
	
	reg|=(channel<<4);
	
	save_flags(flags);
	cli();
	apu_index_set(ess, reg);
	apu_data_set(ess, data);
	restore_flags(flags);
}

static u16 apu_get_register(struct ess_state *ess, u16 channel, u8 reg)
{
	unsigned long flags;
	u16 v;
	
	if(channel&ESS_CHAN_HARD)
		channel&=~ESS_CHAN_HARD;
	else
		channel = ess->apu[channel];

	reg|=(channel<<4);
	
	save_flags(flags);
	cli();
	apu_index_set(ess, reg);
	v=maestro_read(ess, IDR0_DATA_PORT);
	restore_flags(flags);
	return v;
}


/*
 *	The wavecache does pci fetches for us and feeds
 *	them to the APUs..  
 *	XXX describe interface
 */
 
static void wave_set_register(struct ess_state *ess, u16 reg, u16 value)
{
	long ioaddr = ess->card->iobase;
	unsigned long flags;
	
	save_flags(flags);
	cli();
	outw(reg, ioaddr+0x10);
	outw(value, ioaddr+0x12);
	restore_flags(flags);
}

static u16 wave_get_register(struct ess_state *ess, u16 reg)
{
	long ioaddr = ess->card->iobase;
	unsigned long flags;
	u16 value;
	
	save_flags(flags);
	cli();
	outw(reg, ioaddr+0x10);
	value=inw(ioaddr+0x12);
	restore_flags(flags);
	
	return value;
}

static void sound_reset(int ioaddr)
{
	outw(0x2000, 0x18+ioaddr);
	udelay(10);
	outw(0x0000, 0x18+ioaddr);
	udelay(10);
}

static void set_apu_fmt(struct ess_state *s, int apu, int mode)
{
	if(mode&ESS_CFMT_16BIT) { 
		s->apu_mode[apu] = 0x10;
		s->apu_mode[apu+1] = 0x10;
	} else {
		s->apu_mode[apu] = 0x30;
		s->apu_mode[apu+1] = 0x30;
	}
}

static void set_fmt(struct ess_state *s, unsigned char mask, unsigned char data)
{
	s->fmt = (s->fmt & mask) | data;
	set_apu_fmt(s, 0, s->fmt & ESS_CFMT_MASK);
	set_apu_fmt(s, 2, (s->fmt >> ESS_CFMT_CSHIFT) & ESS_CFMT_MASK);
}

static u16 compute_rate(u32 freq)
{
	if(freq==48000)
		return 0xFFFF;
	freq<<=16;
	freq/=48000;
	return freq;
}

static void set_dac_rate(struct ess_state *s, unsigned rate)
{
	u32 freq;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	s->ratedac = rate;

	if(!(s->fmt & ESS_CFMT_16BIT))
		rate >>= 1; /* who knows */

/*	M_printk("computing dac rate %d with mode %d\n",rate,s->fmt);*/

	freq = compute_rate(rate);
	
	/* Load the frequency, turn on 6dB, turn off the effects */
	apu_set_register(s, 0, 2, (freq&0xFF)<<8|0x10);
	apu_set_register(s, 0, 3, freq>>8);
	apu_set_register(s, 1, 2, (freq&0xFF)<<8|0x10);
	apu_set_register(s, 1, 3, freq>>8);
}

static void set_adc_rate(struct ess_state *s, unsigned rate)
{
	u32 freq;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	s->rateadc = rate;

	freq = compute_rate(rate);
	
	/* Load the frequency, turn on 6dB, turn off the effects */
	apu_set_register(s, 2, 2, (freq&0xFF)<<8|0x10);
	apu_set_register(s, 2, 3, freq>>8);
	apu_set_register(s, 3, 2, (freq&0xFF)<<8|0x10);
	apu_set_register(s, 3, 3, freq>>8);
}


/*
 *	Native play back driver 
 */

static void ess_play_setup(struct ess_state *ess, int mode, u32 rate, void *buffer, int size)
{
	u32 pa;
	u32 tmpval;
	int high_apu = 0;
	int channel;

	M_printk("mode=%d rate=%d buf=%p len=%d.\n",
		mode, rate, buffer, size);
		
	/* all maestro sizes are in 16bit words */
	size >>=1;

	/* we're given the full size of the buffer, but
	in stereo each channel will only play its half */
	if(mode&ESS_CFMT_STEREO) {
		size >>=1; 
		high_apu++;
	}
	
	for(channel=0; channel <= high_apu; channel++)
	{
		int i;
		
		/*
		 *	To understand this it helps to know how the
		 *	wave cache works. There are 4 DSP wavecache 
		 *	blocks which are 0x1FC->0x1FF. They are selected
		 *	by setting bits  22,21,20 of the address to
		 *	1 X Y   where X Y select the block.
		 *
		 *	In addition stereo pairing is supported. This is
		 *	set in the wave cache control for the channel as is
		 *	8bit unsigned.
		 *
		 *	Note that this causes a problem. With our limit of
		 *	about 12 full duplex pairs (48 channels active) we
		 *	will need to do a lot of juggling to get all the
		 *	memory we want sufficiently close together.
		 *	
		 *	Even with 64K blocks that means
		 *		24 channel pairs
		 *		6 pairs/block
		 *	
		 *	10K per channel pair = 5000 samples.
		 */
		
		if(!channel) 
			pa = virt_to_bus(buffer);
		else
		/* right channel plays its split half.
			*2 accomodates for rampant shifting earlier */
			pa = virt_to_bus(buffer + size*2);

		M_printk("sending pa %x to %d\n",pa,channel);
		
		wave_set_register(ess, 0x01FC, (pa&0xFFE00000)>>12);

		/* set the wavecache control reg */
		tmpval = (pa - 0x10) & 0xFFF8;
#if 0
		if(mode & 1) tmpval |= 2; /* stereo */
#endif
		if(!(mode & 2)) tmpval |= 4; /* 8bit */ 
		wave_set_register(ess, ess->apu[channel]<<3, tmpval);
		
		pa&=0x1FFFFF;			/* Low 21 bits */	
		pa>>=1; /* words */
		
		/* base offset of dma calcs when reading the pointer
			on this left one */
		if(!channel) ess->dma_dac.base = pa&0xFFFF;
		
#if 0
		if(mode&ESS_CFMT_STEREO)			/* Enable stereo */
			pa|=0x00800000;
#endif
		pa|=0x00400000;			/* System RAM */
		
		/* Begin loading the APU */		
		for(i=0;i<15;i++)		/* clear all PBRs */
			apu_set_register(ess, channel, i, 0x0000);
			
		/* Load the frequency, turn on 6dB, turn off the effects */
/*		apu_set_register(ess, channel, 2, (rate&0xFF)<<8|0x10);
		apu_set_register(ess, channel, 3, rate>>8);*/
		
		/* Load the buffer into the wave engine */
		apu_set_register(ess, channel, 4, ((pa>>16)&0xFF)<<8);
		/* XXX reg is little endian.. */
		apu_set_register(ess, channel, 5, pa&0xFFFF);
		apu_set_register(ess, channel, 6, (pa+size)&0xFFFF);
		/* setting loop == sample len */
		apu_set_register(ess, channel, 7, size);
		
		/* clear effects/env.. */
		apu_set_register(ess, channel, 8, 0x0000);
		/* aplitudeNow to 0xd0? */
		apu_set_register(ess, channel, 9, 0xD000);

		/* set the panning reg of the apu to left/right/mid.. */

		/* clear routing stuff */
		apu_set_register(ess, channel, 11, 0x0000);
		/* mark dma and turn on filter stuff? */
		apu_set_register(ess, channel, 0, 0x400F);
		
		if(mode&ESS_CFMT_STEREO)
			/* set panning: left or right */
			apu_set_register(ess, channel, 10, 0x8F00 | (channel ? 0x10 : 0));
		else
			apu_set_register(ess, channel, 10, 0x8F08);

	}
	
	/* clear WP interupts */
	outw(1, ess->card->iobase+0x04);
	/* enable WP ints */
	outw(inw(ess->card->iobase+0x18)|4, ess->card->iobase+0x18);

	set_dac_rate(ess,rate);

	for(channel=0; channel<=high_apu; channel++)
	{
		/* Turn on the DMA */
		if(mode&ESS_CFMT_16BIT)
		{
			apu_set_register(ess, channel, 0,
				(apu_get_register(ess, channel, 0)&0xFF0F)|0x10);
			ess->apu_mode[channel]=0x10;
		}
		else
		{
			apu_set_register(ess, channel, 0,
				(apu_get_register(ess, channel, 0)&0xFF0F)|0x30);
			ess->apu_mode[channel]=0x30;
		}
	}
}

/* --------------------------------------------------------------------- */

static void set_dmaa(struct ess_state *s, unsigned int addr, unsigned int count)
{
}

static void set_dmac(struct ess_state *s, unsigned int addr, unsigned int count)
{
}

/* Playback pointer */
extern __inline__ unsigned get_dmaa(struct ess_state *s)
{
	long ioport = s->card->iobase;
	int offset;
	
	outw(1, ioport+2);
	outw(s->apu[0]<<4|5, ioport);
	outw(0, ioport+2);
	offset=inw(ioport);

/*	M_printk("dmaa: offset: %d, base: %d\n",offset,s->dma_dac.base);*/
	
	offset-=s->dma_dac.base;

	return (offset&0xFFFE)/*<<1*/; /* XXX printk didn't have it */
}

/* Record pointer */
extern __inline__ unsigned get_dmac(struct ess_state *s)
{
	long ioport = s->card->iobase;
	int offset;
	
	outw(1, ioport+2);
	outw(s->apu[2]<<4|5, ioport);
	outw(0, ioport+2);
	offset=inw(ioport);
	
	/* The offset is an address not a position relative to base */
	
	return (offset&0xFFFE)<<1; /* hardware is in words */
}

/*
 *	Meet Bob, the timer...
 */

static void ess_interrupt(int irq, void *dev_id, struct pt_regs *regs);

#define ESS_HW_TIMER

#ifndef ESS_HW_TIMER

/* old kernel timer based timer ints, should BOB prove flakey */

static struct timer_list tmp_timer;

static int bob_stopped;

static void ess_interrupt_fake(unsigned long v)
{
	ess_interrupt(5, (void *)v, NULL);
	del_timer(&tmp_timer);
	if(!bob_stopped)
	{
		tmp_timer.expires=jiffies+1;
		add_timer(&tmp_timer);
	}
	else
		M_printk("Stopping bob (SW)\n");
}

static void stop_bob(struct ess_state *s)
{
	bob_stopped=1;
}

static void kill_bob(struct ess_state *s)
{
	del_timer(&tmp_timer);
	M_printk("Killing bob (SW)\n");
}

static void start_bob(struct ess_state *s)
{
	static int init=1;
	if(init)
	{
		init=0;
		init_timer(&tmp_timer);
		tmp_timer.function = ess_interrupt_fake;
	}
	bob_stopped = 0;
	if(!timer_pending(&tmp_timer))
	{
		del_timer(&tmp_timer);
		tmp_timer.expires = jiffies+1;
		tmp_timer.data = (unsigned long)s->card;
		add_timer(&tmp_timer);
		M_printk("Starting bob (SW)\n");
	}
}

#else

/* nice HW BOB implementation.  cheers, eric. */

static void stop_bob(struct ess_state *s)
{
	/* Mask IDR 11,17 */
	maestro_write(s,  0x11, maestro_read(s, 0x11)&~1);
	maestro_write(s,  0x17, maestro_read(s, 0x17)&~1);
}

/* eventually we could be clever and limit bob ints
	to the frequency at which our smallest duration
	chunks may expire */
static void start_bob(struct ess_state *s)
{
	stop_bob(s);	// make sure bob's not already running
	
	maestro_write(s, 6, 0x8000 |(1<<12) | (5<<5) | 11);		// (50MHz/2^14)/12 = 254 Hz = 40 mS
	
	/* Now set IDR 11/17 */
	maestro_write(s, 0x11, maestro_read(s, 0x11)|1);
	maestro_write(s, 0x17, maestro_read(s, 0x17)|1);
}
#endif // ESS_HW_TIMER
/* --------------------------------------------------------------------- */

static int adc_active = 0;

extern inline void stop_adc(struct ess_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	/* Stop left and right recording APU */
	s->enable &= ~ADC_RUNNING;
	apu_set_register(s, 2, 0, apu_get_register(s, 2, 0)&0xFF0F);
	apu_set_register(s, 3, 0, apu_get_register(s, 3, 0)&0xFF0F);
	adc_active&=~1;
//	if(!adc_active)
//		stop_bob(s);
	spin_unlock_irqrestore(&s->lock, flags);
}	

extern inline void stop_dac(struct ess_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->enable &= ~DAC_RUNNING;
	apu_set_register(s, 0, 0, apu_get_register(s, 0, 0)&0xFF0F);
	apu_set_register(s, 1, 0, apu_get_register(s, 1, 0)&0xFF0F);
	adc_active&=~2;
//	if(!adc_active)
//		stop_bob(s);
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_dac(struct ess_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if ((s->dma_dac.mapped || s->dma_dac.count > 0) && s->dma_dac.ready) {
		s->enable |= DAC_RUNNING;

		apu_set_register(s, 0, 0, 
			(apu_get_register(s, 0, 0)&0xFF0F)|s->apu_mode[0]);

		if(s->fmt & ESS_CFMT_STEREO) 
			apu_set_register(s, 1, 0, 
				(apu_get_register(s, 1, 0)&0xFF0F)|s->apu_mode[1]);
	}
//	if(!adc_active)
//		start_bob(s);
	adc_active|=2;
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_adc(struct ess_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if ((s->dma_adc.mapped || s->dma_adc.count < (signed)(s->dma_adc.dmasize - 2*s->dma_adc.fragsize)) 
	    && s->dma_adc.ready) {
		s->enable |= ADC_RUNNING;
		apu_set_register(s, 2, 0, 
			(apu_get_register(s, 2, 0)&0xFF0F)|s->apu_mode[2]);
		apu_set_register(s, 3, 0, 
			(apu_get_register(s, 3, 0)&0xFF0F)|s->apu_mode[3]);
	}
//	if(!adc_active)
//		start_bob(s);
	adc_active|=1;
	spin_unlock_irqrestore(&s->lock, flags);
}	

/* --------------------------------------------------------------------- */

#define DMABUF_DEFAULTORDER (15-PAGE_SHIFT)
#define DMABUF_MINORDER 1

static void dealloc_dmabuf(struct dmabuf *db)
{
	unsigned long map, mapend;

	if (db->rawbuf) {
		/* undo marking the pages as reserved */
		mapend = MAP_NR(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
		for (map = MAP_NR(db->rawbuf); map <= mapend; map++)
			clear_bit(PG_reserved, &mem_map[map].flags);	
		free_pages((unsigned long)db->rawbuf, db->buforder);
	}
	db->rawbuf = NULL;
	db->mapped = db->ready = 0;
}


static int prog_dmabuf(struct ess_state *s, unsigned rec)
{
	struct dmabuf *db = rec ? &s->dma_adc : &s->dma_dac;
	unsigned rate = rec ? s->rateadc : s->ratedac;
	int order;
	unsigned bytepersec;
	unsigned bufs;
	unsigned long map, mapend;
	unsigned char fmt;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	fmt = s->fmt;
	if (rec) {
		s->enable &= ~ESS_ENABLE_RE;
		fmt >>= ESS_CFMT_CSHIFT;
	} else {
		s->enable &= ~ESS_ENABLE_PE;
		fmt >>= ESS_CFMT_ASHIFT;
	}
	spin_unlock_irqrestore(&s->lock, flags);

	db->hwptr = db->swptr = db->total_bytes = db->count = db->error = db->endcleared = 0;

	if (!db->rawbuf) {
		db->ready = db->mapped = 0;

		/* alloc as big a chunk as we can */
		for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER && !db->rawbuf; order--)
			db->rawbuf = (void *)__get_free_pages(GFP_KERNEL|GFP_DMA, order);

		if (!db->rawbuf)
			return -ENOMEM;

		db->buforder = order;

		if ((virt_to_bus(db->rawbuf) ^ (virt_to_bus(db->rawbuf) + (PAGE_SIZE << db->buforder) - 1)) & ~0xffff)
			printk(KERN_DEBUG "maestro: DMA buffer crosses 64k boundary: busaddr 0x%lx  size %ld\n", 
			       virt_to_bus(db->rawbuf), PAGE_SIZE << db->buforder);

		if ((virt_to_bus(db->rawbuf) + (PAGE_SIZE << db->buforder) - 1) & ~0xffffff)
			printk(KERN_DEBUG "maestro: DMA buffer beyond 16MB: busaddr 0x%lx  size %ld\n", 
			       virt_to_bus(db->rawbuf), PAGE_SIZE << db->buforder);

		/* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
		mapend = MAP_NR(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
		for (map = MAP_NR(db->rawbuf); map <= mapend; map++)
			set_bit(PG_reserved, &mem_map[map].flags);
	}
	bytepersec = rate << sample_shift[fmt];
	bufs = PAGE_SIZE << db->buforder;
	if (db->ossfragshift) {
		if ((1000 << db->ossfragshift) < bytepersec)
			db->fragshift = ld2(bytepersec/1000);
		else
			db->fragshift = db->ossfragshift;
	} else {
		db->fragshift = ld2(bytepersec/100/(db->subdivision ? db->subdivision : 1));
		if (db->fragshift < 3)
			db->fragshift = 3;
	}
	db->numfrag = bufs >> db->fragshift;
	while (db->numfrag < 4 && db->fragshift > 3) {
		db->fragshift--;
		db->numfrag = bufs >> db->fragshift;
	}
	db->fragsize = 1 << db->fragshift;
	if (db->ossmaxfrags >= 4 && db->ossmaxfrags < db->numfrag)
		db->numfrag = db->ossmaxfrags;
	db->fragsamples = db->fragsize >> sample_shift[fmt];
	db->dmasize = db->numfrag << db->fragshift;
	memset(db->rawbuf, (fmt & ESS_CFMT_16BIT) ? 0 : 0x80, db->dmasize);
	spin_lock_irqsave(&s->lock, flags);
	if (rec) {
		set_dmac(s, virt_to_bus(db->rawbuf), db->numfrag << db->fragshift);
		/* program enhanced mode registers */
		/* FILL */
	} else {
		//set_dmaa(s, virt_to_bus(db->rawbuf), db->numfrag << db->fragshift);
		/* program enhanced mode registers */
		/* FILL */
		//set_dac_rate(s, s->ratedac);	// redundant
		ess_play_setup(s, fmt, s->ratedac, 
			db->rawbuf, db->numfrag << db->fragshift);
	}
	spin_unlock_irqrestore(&s->lock, flags);
	db->ready = 1;
	return 0;
}

extern __inline__ void clear_advance(struct ess_state *s)
{
	unsigned char c = (s->fmt & (ESS_CFMT_16BIT << ESS_CFMT_ASHIFT)) ? 0 : 0x80;
	unsigned char *buf = s->dma_dac.rawbuf;
	unsigned bsize = s->dma_dac.dmasize;
	unsigned bptr = s->dma_dac.swptr;
	unsigned len = s->dma_dac.fragsize;

	if (bptr + len > bsize) {
		unsigned x = bsize - bptr;
		memset(buf + bptr, c, x);
		/* account for wrapping? */
		bptr = 0;
		len -= x;
	}
	memset(buf + bptr, c, len);
}

/* call with spinlock held! */
static void ess_update_ptr(struct ess_state *s)
{
	unsigned hwptr;
	int diff;

	/* update ADC pointer */
	if (s->dma_adc.ready) {
		M_printk("adc ready.. \n");
		hwptr = (/*s->dma_adc.dmasize - */get_dmac(s)) % s->dma_adc.dmasize;
		diff = (s->dma_adc.dmasize + hwptr - s->dma_adc.hwptr) % s->dma_adc.dmasize;
		s->dma_adc.hwptr = hwptr;
		s->dma_adc.total_bytes += diff;
		s->dma_adc.count += diff;
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize) 
			wake_up(&s->dma_adc.wait);
		if (!s->dma_adc.mapped) {
			if (s->dma_adc.count > (signed)(s->dma_adc.dmasize - ((3 * s->dma_adc.fragsize) >> 1))) {
				s->enable &= ~ESS_ENABLE_RE;
				/* FILL ME */
//				wrindir(s, SV_CIENABLE, s->enable);
				stop_adc(s);
				s->dma_adc.error++;
			}
		}
	}
	/* update DAC pointer */
	if (s->dma_dac.ready) {
		hwptr = (/*s->dma_dac.dmasize -*/ get_dmaa(s)) % s->dma_dac.dmasize;
		diff = (s->dma_dac.dmasize + hwptr - s->dma_dac.hwptr) % s->dma_dac.dmasize;
/*		M_printk("updating dac: hwptr: %d diff: %d\n",hwptr,diff);*/
		s->dma_dac.hwptr = hwptr;
		s->dma_dac.total_bytes += diff;
		if (s->dma_dac.mapped) {
			s->dma_dac.count += diff;
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize)
				wake_up(&s->dma_dac.wait);
		} else {
			s->dma_dac.count -= diff;
			if (s->dma_dac.count <= 0) {
				s->enable &= ~ESS_ENABLE_PE;
/* FILL ME */
//				wrindir(s, SV_CIENABLE, s->enable);

				stop_dac(s);
				s->dma_dac.error++;
			} else if (s->dma_dac.count <= (signed)s->dma_dac.fragsize && !s->dma_dac.endcleared) {
				clear_advance(s);
				s->dma_dac.endcleared = 1;
			}
			if (s->dma_dac.count + (signed)s->dma_dac.fragsize <= (signed)s->dma_dac.dmasize)
				wake_up(&s->dma_dac.wait);
		}
	}
}

static void ess_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        struct ess_state *s;
        struct ess_card *c = (struct ess_card *)dev_id;
	int i;
	u32 event;

	event = inb(c->iobase+0x1A);
	
	outw(inw(c->iobase+4)&1, c->iobase+4);

/*	M_printk("maestro int: %x\n",event);*/

	if(event&(1<<6))
	{
		/* XXX if we have a hw volume control int enable
			all the ints?  doesn't make sense.. */
		event = inw(c->iobase+0x18);
		outb(0xFF, c->iobase+0x1A);
	}
	else
	{
		/* else ack 'em all, i imagine */
		outb(0xFF, c->iobase+0x1A);
	}
		
	/*
	 *	Update the pointers for all APU's we are running.
	 */
	for(i=0;i<8;i++)
	{
		s=&c->channels[i];
		if(s->dev_audio == -1)
			break;
		spin_lock(&s->lock);
		ess_update_ptr(s);
		spin_unlock(&s->lock);
	}
}


/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT "maestro: invalid magic value in %s\n";

#define VALIDATE_MAGIC(FOO,MAG)                         \
({                                                \
	if (!(FOO) || (FOO)->magic != MAG) { \
		printk(invalid_magic,__FUNCTION__);            \
		return -ENXIO;                    \
	}                                         \
})

#define VALIDATE_STATE(a) VALIDATE_MAGIC(a,ESS_STATE_MAGIC)
#define VALIDATE_CARD(a) VALIDATE_MAGIC(a,ESS_CARD_MAGIC)

/* --------------------------------------------------------------------- */

/* ac97 mixer routines.  */

#define AC97_STEREO_MASK (SOUND_MASK_VOLUME|\
	SOUND_MASK_PCM|SOUND_MASK_LINE|SOUND_MASK_CD|\
	SOUND_MASK_VIDEO|SOUND_MASK_LINE1|SOUND_MASK_IGAIN)

#define AC97_SUPPORTED_MASK (AC97_STEREO_MASK | \
	SOUND_MASK_BASS|SOUND_MASK_TREBLE|SOUND_MASK_MIC|\
	SOUND_MASK_SPEAKER)

#define AC97_RECORD_MASK (SOUND_MASK_MIC|\
	SOUND_MASK_CD| SOUND_MASK_VIDEO| SOUND_MASK_LINE1| SOUND_MASK_LINE|\
	SOUND_MASK_PHONEIN)

#define supported_mixer(CARD,FOO) ( CARD->mix.supported_mixers & (1<<FOO) )

static struct ac97_mixer_hw {
	unsigned char offset;
	int scale;
} ac97_hw[SOUND_MIXER_NRDEVICES]= {
	[SOUND_MIXER_VOLUME]	=	{0x02,63},
	[SOUND_MIXER_BASS]	=	{0x08,15},
	[SOUND_MIXER_TREBLE]	=	{0x08,15},
	[SOUND_MIXER_SPEAKER]	=	{0x0a,15},
	[SOUND_MIXER_MIC]	=	{0x0e,31},
	[SOUND_MIXER_LINE]	=	{0x10,31},
	[SOUND_MIXER_CD]	=	{0x12,31},
	[SOUND_MIXER_VIDEO]	=	{0x14,31},
	[SOUND_MIXER_LINE1]	=	{0x16,31},
	[SOUND_MIXER_PCM]	=	{0x18,31},
	[SOUND_MIXER_IGAIN]	=	{0x1c,31}
};

/* reads the given OSS mixer from the ac97
	the caller must have insured that the ac97 knows
	about that given mixer, and should be holding a
	spinlock for the card */
static int ac97_read_mixer(struct ess_card *card, int mixer) 
{
	u16 val;
	int ret=0;
	struct ac97_mixer_hw *mh = &ac97_hw[mixer];

	val = maestro_ac97_get(card->iobase , mh->offset);

	if(AC97_STEREO_MASK & (1<<mixer)) {
		/* nice stereo mixers .. */
		int left,right;

		left = (val >> 8)  & 0x7f;
		right = val  & 0x7f;

		right = 100 - ((right * 100) / mh->scale);
		left = 100 - ((left * 100) / mh->scale);
		ret = left | (right << 8);
	} else if (mixer == SOUND_MIXER_SPEAKER) {
		ret = 100 - ((((val & 0x1e)>>1) * 100) / mh->scale);
	} else if (mixer == SOUND_MIXER_MIC) {
		ret = 100 - (((val & 0x1f) * 100) / mh->scale);
	/*  the low bit is optional in the tone sliders and masking
		it lets is avoid the 0xf 'bypass'.. */
	} else if (mixer == SOUND_MIXER_BASS) {
		ret = 100 - ((((val >> 8) & 0xe) * 100) / mh->scale);
	} else if (mixer == SOUND_MIXER_TREBLE) {
		ret = 100 - (((val & 0xe) * 100) / mh->scale);
	}

	M_printk("read mixer %d (0x%x) %x -> %x\n",mixer,mh->offset,val,ret);

	return ret;
}

/* write the OSS encoded volume to the given OSS encoded mixer,
	again caller's job to make sure all is well in arg land,
	call with spinlock held */
static void ac97_write_mixer(struct ess_card *card,int mixer, int vol) 
{
	u16 val=0;
	unsigned left, right;
	struct ac97_mixer_hw *mh = &ac97_hw[mixer];

	/* cleanse input a little */
	right = ((vol >> 8)  & 0x7f) ;
	left = (vol  & 0x7f) ;

	if(right > 100) right = 100;
	if(left > 100) left = 100;

	M_printk("wrote mixer %d (0x%x) %d,%d",mixer,mh->offset,left,right);

	if(AC97_STEREO_MASK & (1<<mixer)) {
		/* stereo mixers */

		right = ((100 - right) * mh->scale) / 100;
		left = ((100 - left) * mh->scale) / 100;

		val = (left << 8) | right;
	} else if (mixer == SOUND_MIXER_SPEAKER) {
		val = (((100 - left) * mh->scale) / 100) << 1;
	} else if (mixer == SOUND_MIXER_MIC) {
		val = maestro_ac97_get(card->iobase , mh->offset) & ~0x001f;
		val |= (((100 - left) * mh->scale) / 100);
	/*  the low bit is optional in the tone sliders and masking
		it lets is avoid the 0xf 'bypass'.. */
	} else if (mixer == SOUND_MIXER_BASS) {
		val = maestro_ac97_get(card->iobase , mh->offset) & ~0x0f00;
		val |= ((((100 - left) * mh->scale) / 100) << 8) & 0xe0;
	} else if (mixer == SOUND_MIXER_TREBLE)  {
		val = maestro_ac97_get(card->iobase , mh->offset) & ~0x000f;
		val |= (((100 - left) * mh->scale) / 100) & 0xe;
	}

	maestro_ac97_set(card->iobase , mh->offset, val);
	
	M_printk(" -> %x\n",val);
}

enum ac97_recsettings {
	AC97_REC_MIC=0,
	AC97_REC_CD,
	AC97_REC_VIDEO,
	AC97_REC_AUX,
	AC97_REC_LINE,
	AC97_REC_STEREO, /* combination of all enabled outputs..  */
	AC97_REC_MONO,        /*.. or the mono equivalent */
	AC97_REC_PHONE        
};
static unsigned int ac97_rm2oss[] = {
	[AC97_REC_MIC] = SOUND_MASK_MIC, 
	[AC97_REC_CD] = SOUND_MASK_CD, 
	[AC97_REC_VIDEO] = SOUND_MASK_VIDEO, 
	[AC97_REC_AUX] = SOUND_MASK_LINE1, 
	[AC97_REC_LINE] = SOUND_MASK_LINE, 
	[AC97_REC_PHONE] = SOUND_MASK_PHONEIN
};

/* indexed by bit position, XXX dependant on OSS header internals */
static unsigned int ac97_oss_rm[] = {
	[SOUND_MIXER_MIC] = AC97_REC_MIC,
	[SOUND_MIXER_CD] = AC97_REC_CD,
	[SOUND_MIXER_VIDEO] = AC97_REC_VIDEO,
	[SOUND_MIXER_LINE1] = AC97_REC_AUX,
	[SOUND_MIXER_LINE] = AC97_REC_LINE,
	[SOUND_MIXER_PHONEIN] = AC97_REC_PHONE
};
	
/* read or write the recmask 
	the ac97 can really have left and right recording
	inputs independantly set, but OSS doesn't seem to 
	want us to express that to the user. 
	the caller guarantees that we have a supported bit set,
	and they must be holding the card's spinlock */
static int ac97_recmask_io(struct ess_card *card, int rw, int mask) 
{
	unsigned int val;

	if (rw) {
		/* read it from the card */
		val = maestro_ac97_get(card->iobase, 0x1a) & 0x7;
		return ac97_rm2oss[val];
	}

	/* else, write the first set in the mask as the
		output */	

	val = ffs(mask); 
	val = ac97_oss_rm[val-1];
	val |= val << 8;  /* set both channels */

	maestro_ac97_set(card->iobase,0x1a,val);

	return 0;
};

static int ac97_mixer_ioctl(struct ess_card *card, unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	int i, val=0;
	struct ess_state *s = &card->channels[0];

	VALIDATE_CARD(card);
        if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		strncpy(info.id, card_names[card->card_type], sizeof(info.id));
		strncpy(info.name,card_names[card->card_type],sizeof(info.name));
		info.modify_counter = card->mix.modcnt;
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		strncpy(info.id, card_names[card->card_type], sizeof(info.id));
		strncpy(info.name,card_names[card->card_type],sizeof(info.name));
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, (int *)arg);

	if (_IOC_TYPE(cmd) != 'M' || _IOC_SIZE(cmd) != sizeof(int))
                return -EINVAL;

        if (_IOC_DIR(cmd) == _IOC_READ) {
                switch (_IOC_NR(cmd)) {
                case SOUND_MIXER_RECSRC: /* give them the current record source */
			spin_lock_irqsave(&s->lock, flags);
			val = card->mix.recmask_io(card,1,0);
			spin_unlock_irqrestore(&s->lock, flags);
			break;
			
                case SOUND_MIXER_DEVMASK: /* give them the supported mixers */
			val = card->mix.supported_mixers;
			break;

                case SOUND_MIXER_RECMASK: /* Arg contains a bit for each supported recording source */
			val = card->mix.record_sources;
			break;
			
                case SOUND_MIXER_STEREODEVS: /* Mixer channels supporting stereo */
			val = card->mix.stereo_mixers;
			break;
			
                case SOUND_MIXER_CAPS:
			val = SOUND_CAP_EXCL_INPUT;
			break;

		default: /* read a specific mixer */
			i = _IOC_NR(cmd);

			if ( ! supported_mixer(card,i)) 
				return -EINVAL;

			spin_lock_irqsave(&s->lock, flags);
			val = card->mix.read_mixer(card,i);
			spin_unlock_irqrestore(&s->lock, flags);

			break;
		}
		return put_user(val,(int *)arg);
	}
	
        if (_IOC_DIR(cmd) != (_IOC_WRITE|_IOC_READ))
		return -EINVAL;
	
	card->mix.modcnt++;

	get_user_ret(val, (int *)arg, -EFAULT);

	switch (_IOC_NR(cmd)) {
	case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */

		if(! (val &= card->mix.record_sources)) return -EINVAL;

		spin_lock_irqsave(&s->lock, flags);
		card->mix.recmask_io(card,0,val);
		spin_unlock_irqrestore(&s->lock, flags);
		return 0;

	default:
		i = _IOC_NR(cmd);

		if ( ! supported_mixer(card,i)) 
			return -EINVAL;

		spin_lock_irqsave(&s->lock, flags);
		card->mix.write_mixer(card,i,val);
		spin_unlock_irqrestore(&s->lock, flags);

		return 0;
	}
}

/* --------------------------------------------------------------------- */

static loff_t ess_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

/* --------------------------------------------------------------------- */

static int ess_open_mixdev(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct ess_card *card = devs;

	while (card && card->dev_mixer != minor)
		card = card->next;
	if (!card)
		return -ENODEV;

	file->private_data = card;
	MOD_INC_USE_COUNT;
	return 0;
}

static int ess_release_mixdev(struct inode *inode, struct file *file)
{
	struct ess_card *card = (struct ess_card *)file->private_data;

	VALIDATE_CARD(card);
	
	MOD_DEC_USE_COUNT;
	return 0;
}

static int ess_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ess_card *card = (struct ess_card *)file->private_data;

	VALIDATE_CARD(card);

	return ac97_mixer_ioctl(card, cmd, arg);
}

static /*const*/ struct file_operations ess_mixer_fops = {
	&ess_llseek,
	NULL,  /* read */
	NULL,  /* write */
	NULL,  /* readdir */
	NULL,  /* poll */
	&ess_ioctl_mixdev,
	NULL,  /* mmap */
	&ess_open_mixdev,
	NULL,	/* flush */
	&ess_release_mixdev,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL,  /* lock */
};

/* --------------------------------------------------------------------- */

static int drain_dac(struct ess_state *s, int nonblock)
{
        DECLARE_WAITQUEUE(wait,current);
	unsigned long flags;
	int count, tmo;

	if (s->dma_dac.mapped || !s->dma_dac.ready)
		return 0;
        current->state = TASK_INTERRUPTIBLE;
        add_wait_queue(&s->dma_dac.wait, &wait);
        for (;;) {
                spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
                spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
                        break;
                if (nonblock) {
                        remove_wait_queue(&s->dma_dac.wait, &wait);
                        current->state = TASK_RUNNING;
                        return -EBUSY;
                }
		tmo = (count * HZ) / s->ratedac;
		tmo >>= sample_shift[(s->fmt >> ESS_CFMT_ASHIFT) & ESS_CFMT_MASK];
		if (!schedule_timeout(tmo ? : 1) && tmo)
			printk(KERN_DEBUG "maestro: dma timed out??\n");
        }
        remove_wait_queue(&s->dma_dac.wait, &wait);
        current->state = TASK_RUNNING;
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

/* --------------------------------------------------------------------- */

static ssize_t ess_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct ess_state *s = (struct ess_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	/* for damned dual players */
	int cnt;

	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (s->dma_adc.mapped)
		return -ENXIO;
	if (!s->dma_adc.ready && (ret = prog_dmabuf(s, 1)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;
#if 0
   spin_lock_irqsave(&s->lock, flags);
   ess_update_ptr(s);
   spin_unlock_irqrestore(&s->lock, flags);
#endif
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		swptr = s->dma_adc.swptr;
		cnt = s->dma_adc.dmasize-swptr;
		if (s->dma_adc.count < cnt)
			cnt = s->dma_adc.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			start_adc(s);
			if (file->f_flags & O_NONBLOCK) 
				return ret ? ret : -EAGAIN;
			if (!interruptible_sleep_on_timeout(&s->dma_adc.wait, HZ)) {
				printk(KERN_DEBUG "maestro: read: chip lockup? dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       s->dma_adc.dmasize, s->dma_adc.fragsize, s->dma_adc.count, 
				       s->dma_adc.hwptr, s->dma_adc.swptr);
				stop_adc(s);
				spin_lock_irqsave(&s->lock, flags);
				set_dmac(s, virt_to_bus(s->dma_adc.rawbuf), s->dma_adc.numfrag << s->dma_adc.fragshift);
				/* program enhanced mode registers */
				/* FILL ME */
//				wrindir(s, SV_CIDMACBASECOUNT1, (s->dma_adc.fragsamples-1) >> 8);
//				wrindir(s, SV_CIDMACBASECOUNT0, s->dma_adc.fragsamples-1);
				s->dma_adc.count = s->dma_adc.hwptr = s->dma_adc.swptr = 0;
				spin_unlock_irqrestore(&s->lock, flags);
			}
			if (signal_pending(current)) 
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_to_user(buffer, s->dma_adc.rawbuf + swptr, cnt)) 
			return ret ? ret : -EFAULT;
		swptr = (swptr + cnt) % s->dma_adc.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_adc.swptr = swptr;
		s->dma_adc.count -= cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_adc(s);
	}

	return ret;
}

/* god this is gross..*/
int split_stereo(unsigned char *real_buffer,unsigned char  *tmp_buffer, int offset, 
	int count, int bufsize, int mode)
{  
	/* oh, bother.	stereo decoding APU's don't work in 16bit so we
	use dual linear decoders.  which means we have to hack up stereo
	buffer's we're given.  yuck. 

	and we have to be able to work a byte at a time..*/

	unsigned char *so,*left,*right;
	int i;

	so = tmp_buffer;
	left = real_buffer + offset;
	right = real_buffer + bufsize/2 + offset;

	M_printk("writing %d to %p and %p from %p:%d bufs: %d\n",count/2, left,right,real_buffer,offset,bufsize);

	if(mode & ESS_CFMT_16BIT) {
		for(i=count/4; i ; i--) {
			*(right++) = (*(so+2));
			*(right++) = (*(so+3));
			*(left++) = (*so);
			*(left++) = (*(so+1));
			so+=4;
		}
	} else {
		for(i=count/2; i ; i--) {
			*(right++) = (*(so+1));
			*(left++) = (*so);
			so+=2;
		}
	}

	return 0;
}

static ssize_t ess_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct ess_state *s = (struct ess_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	unsigned char *splitbuf = NULL;
	int cnt;

	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (s->dma_dac.mapped)
		return -ENXIO;
	if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	/* I wish we could be more clever than this */
	if (!(splitbuf = kmalloc(count,GFP_KERNEL)))
		return -ENOMEM; 
	ret = 0;
#if 0
   spin_lock_irqsave(&s->lock, flags);
   ess_update_ptr(s);
   spin_unlock_irqrestore(&s->lock, flags);
#endif
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);

		if (s->dma_dac.count < 0) {
			s->dma_dac.count = 0;
			s->dma_dac.swptr = s->dma_dac.hwptr;
		}
		swptr = s->dma_dac.swptr;

		if(s->fmt & ESS_CFMT_STEREO) {
			/* in stereo we have the 'dual' buffers.. */
			cnt = ((s->dma_dac.dmasize/2)-swptr)*2;
		} else {
			cnt = s->dma_dac.dmasize-swptr;
		}
		if (s->dma_dac.count + cnt > s->dma_dac.dmasize)
			cnt = s->dma_dac.dmasize - s->dma_dac.count;

		spin_unlock_irqrestore(&s->lock, flags);

		if (cnt > count)
			cnt = count;

		/* our goofball stereo splitter can only deal in mults of 4 */
		if (cnt > 0) 
			cnt &= ~3;

		if (cnt <= 0) {
			/* buffer is full, wait for it to be played */
			start_dac(s);
			if (file->f_flags & O_NONBLOCK) {
				if(!ret) ret = -EAGAIN;
				goto return_free;
			}
			if (!interruptible_sleep_on_timeout(&s->dma_dac.wait, HZ)) {
				printk(KERN_DEBUG "maestro: write: chip lockup? dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       s->dma_dac.dmasize, s->dma_dac.fragsize, s->dma_dac.count, 
				       s->dma_dac.hwptr, s->dma_dac.swptr);
				stop_dac(s);
				spin_lock_irqsave(&s->lock, flags);
				set_dmaa(s, virt_to_bus(s->dma_dac.rawbuf), s->dma_dac.numfrag << s->dma_dac.fragshift);
				/* program enhanced mode registers */
//				wrindir(s, SV_CIDMAABASECOUNT1, (s->dma_dac.fragsamples-1) >> 8);
//				wrindir(s, SV_CIDMAABASECOUNT0, s->dma_dac.fragsamples-1);
				/* FILL ME */
				s->dma_dac.count = s->dma_dac.hwptr = s->dma_dac.swptr = 0;
				spin_unlock_irqrestore(&s->lock, flags);
			}
			if (signal_pending(current)) {
				if (!ret) ret = -ERESTARTSYS;
				goto return_free;
			}
			continue;
		}
		if(s->fmt & ESS_CFMT_STEREO) {
			if (copy_from_user(splitbuf, buffer, cnt)) {
				if (!ret) ret = -EFAULT;
				goto return_free;
			}
			split_stereo(s->dma_dac.rawbuf,splitbuf,swptr,cnt,s->dma_dac.dmasize,s->fmt);
		} else {
			if (copy_from_user(s->dma_dac.rawbuf + swptr, buffer, cnt)) {
				if (!ret) ret = -EFAULT;
				goto return_free;
			}
		}

		if(s->fmt & ESS_CFMT_STEREO) {
			/* again with the weird pointer magic*/
			swptr = (swptr + (cnt/2)) % (s->dma_dac.dmasize/2);
		} else {
			swptr = (swptr + cnt) % s->dma_dac.dmasize;
		}
		spin_lock_irqsave(&s->lock, flags);
		s->dma_dac.swptr = swptr;
		s->dma_dac.count += cnt;
		s->dma_dac.endcleared = 0;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac(s);
	}
return_free:
	if (splitbuf) kfree(splitbuf);
	return ret;
}

static unsigned int ess_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ess_state *s = (struct ess_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &s->dma_dac.wait, wait);
	if (file->f_mode & FMODE_READ)
		poll_wait(file, &s->dma_adc.wait, wait);
	spin_lock_irqsave(&s->lock, flags);
	ess_update_ptr(s);
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
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

/* this needs to be fixed to deal with the dualing apus/buffers */
#if 0
static int ess_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ess_state *s = (struct ess_state *)file->private_data;
	struct dmabuf *db;
	int ret;
	unsigned long size;

	VALIDATE_STATE(s);
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf(s, 1)) != 0)
			return ret;
		db = &s->dma_dac;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf(s, 0)) != 0)
			return ret;
		db = &s->dma_adc;
	} else 
		return -EINVAL;
	if (vma->vm_offset != 0)
		return -EINVAL;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << db->buforder))
		return -EINVAL;
	if (remap_page_range(vma->vm_start, virt_to_phys(db->rawbuf), size, vma->vm_page_prot))
		return -EAGAIN;
	db->mapped = 1;
	return 0;
}
#endif

static int ess_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ess_state *s = (struct ess_state *)file->private_data;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int val, mapped, ret;
	unsigned char fmtm, fmtd;

	VALIDATE_STATE(s);
        mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
		((file->f_mode & FMODE_READ) && s->dma_adc.mapped);
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_dac(s, 0/*file->f_flags & O_NONBLOCK*/);
		return 0;
		
	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(0/*DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP*/, (int *)arg);
		
        case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			synchronize_irq();
			s->dma_dac.swptr = s->dma_dac.hwptr = s->dma_dac.count = s->dma_dac.total_bytes = 0;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			synchronize_irq();
			s->dma_adc.swptr = s->dma_adc.hwptr = s->dma_adc.count = s->dma_adc.total_bytes = 0;
		}
		return 0;

        case SNDCTL_DSP_SPEED:
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val >= 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				set_adc_rate(s, val);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				set_dac_rate(s, val);
			}
		}
		return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, (int *)arg);
		
        case SNDCTL_DSP_STEREO:
		get_user_ret(val, (int *)arg, -EFAULT);
		fmtd = 0;
		fmtm = ~0;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.ready = 0;
			if (val)
				fmtd |= ESS_CFMT_STEREO << ESS_CFMT_CSHIFT;
			else
				fmtm &= ~(ESS_CFMT_STEREO << ESS_CFMT_CSHIFT);
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			s->dma_dac.ready = 0;
			if (val)
				fmtd |= ESS_CFMT_STEREO << ESS_CFMT_ASHIFT;
			else
				fmtm &= ~(ESS_CFMT_STEREO << ESS_CFMT_ASHIFT);
		}
		set_fmt(s, fmtm, fmtd);
		return 0;

        case SNDCTL_DSP_CHANNELS:
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val != 0) {
			fmtd = 0;
			fmtm = ~0;
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val >= 2)
					fmtd |= ESS_CFMT_STEREO << ESS_CFMT_CSHIFT;
				else
					fmtm &= ~(ESS_CFMT_STEREO << ESS_CFMT_CSHIFT);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val >= 2)
					fmtd |= ESS_CFMT_STEREO << ESS_CFMT_ASHIFT;
				else
					fmtm &= ~(ESS_CFMT_STEREO << ESS_CFMT_ASHIFT);
			}
			set_fmt(s, fmtm, fmtd);
		}
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_CFMT_STEREO << ESS_CFMT_CSHIFT) 
					   : (ESS_CFMT_STEREO << ESS_CFMT_ASHIFT))) ? 2 : 1, (int *)arg);
		
	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(AFMT_S8|AFMT_S16_LE, (int *)arg);
		
	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val != AFMT_QUERY) {
			fmtd = 0;
			fmtm = ~0;
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val == AFMT_S16_LE)
					fmtd |= ESS_CFMT_16BIT << ESS_CFMT_CSHIFT;
				else
					fmtm &= ~(ESS_CFMT_16BIT << ESS_CFMT_CSHIFT);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val == AFMT_S16_LE)
					fmtd |= ESS_CFMT_16BIT << ESS_CFMT_ASHIFT;
				else
					fmtm &= ~(ESS_CFMT_16BIT << ESS_CFMT_ASHIFT);
			}
			set_fmt(s, fmtm, fmtd);
		}
 		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? 
			(ESS_CFMT_16BIT << ESS_CFMT_CSHIFT) 
			: (ESS_CFMT_16BIT << ESS_CFMT_ASHIFT))) ? 
				AFMT_S16_LE : 
				AFMT_S8, 
			(int *)arg);
		
	case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if (file->f_mode & FMODE_READ && s->enable & ESS_ENABLE_RE) 
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & FMODE_WRITE && s->enable & ESS_ENABLE_PE) 
			val |= PCM_ENABLE_OUTPUT;
		return put_user(val, (int *)arg);
		
	case SNDCTL_DSP_SETTRIGGER:
		get_user_ret(val, (int *)arg, -EFAULT);
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!s->dma_adc.ready && (ret =  prog_dmabuf(s, 1)))
					return ret;
				start_adc(s);
			} else
				stop_adc(s);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
					return ret;
				start_dac(s);
			} else
				stop_dac(s);
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!(s->enable & ESS_ENABLE_PE) && (val = prog_dmabuf(s, 0)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		ess_update_ptr(s);
		abinfo.fragsize = s->dma_dac.fragsize;
                abinfo.bytes = s->dma_dac.dmasize - s->dma_dac.count;
                abinfo.fragstotal = s->dma_dac.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_dac.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!(s->enable & ESS_ENABLE_RE) && (val = prog_dmabuf(s, 1)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		ess_update_ptr(s);
		abinfo.fragsize = s->dma_adc.fragsize;
                abinfo.bytes = s->dma_adc.count;
                abinfo.fragstotal = s->dma_adc.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_adc.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
		
        case SNDCTL_DSP_NONBLOCK:
                file->f_flags |= O_NONBLOCK;
                return 0;

        case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		ess_update_ptr(s);
                val = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		return put_user(val, (int *)arg);

        case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		ess_update_ptr(s);
                cinfo.bytes = s->dma_adc.total_bytes;
                cinfo.blocks = s->dma_adc.count >> s->dma_adc.fragshift;
                cinfo.ptr = s->dma_adc.hwptr;
		if (s->dma_adc.mapped)
			s->dma_adc.count &= s->dma_adc.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

        case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		ess_update_ptr(s);
                cinfo.bytes = s->dma_dac.total_bytes;
                cinfo.blocks = s->dma_dac.count >> s->dma_dac.fragshift;
                cinfo.ptr = s->dma_dac.hwptr;
		if (s->dma_dac.mapped)
			s->dma_dac.count &= s->dma_dac.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

        case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf(s, 0)))
				return val;
			return put_user(s->dma_dac.fragsize, (int *)arg);
		}
		if ((val = prog_dmabuf(s, 1)))
			return val;
		return put_user(s->dma_adc.fragsize, (int *)arg);

        case SNDCTL_DSP_SETFRAGMENT:
                get_user_ret(val, (int *)arg, -EFAULT);
		if (file->f_mode & FMODE_READ) {
			s->dma_adc.ossfragshift = val & 0xffff;
			s->dma_adc.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_adc.ossfragshift < 4)
				s->dma_adc.ossfragshift = 4;
			if (s->dma_adc.ossfragshift > 15)
				s->dma_adc.ossfragshift = 15;
			if (s->dma_adc.ossmaxfrags < 4)
				s->dma_adc.ossmaxfrags = 4;
		}
		if (file->f_mode & FMODE_WRITE) {
			s->dma_dac.ossfragshift = val & 0xffff;
			s->dma_dac.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_dac.ossfragshift < 4)
				s->dma_dac.ossfragshift = 4;
			if (s->dma_dac.ossfragshift > 15)
				s->dma_dac.ossfragshift = 15;
			if (s->dma_dac.ossmaxfrags < 4)
				s->dma_dac.ossmaxfrags = 4;
		}
		return 0;

        case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision) ||
		    (file->f_mode & FMODE_WRITE && s->dma_dac.subdivision))
			return -EINVAL;
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		if (file->f_mode & FMODE_READ)
			s->dma_adc.subdivision = val;
		if (file->f_mode & FMODE_WRITE)
			s->dma_dac.subdivision = val;
		return 0;

        case SOUND_PCM_READ_RATE:
		return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, (int *)arg);

        case SOUND_PCM_READ_CHANNELS:
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_CFMT_STEREO << ESS_CFMT_CSHIFT) 
					   : (ESS_CFMT_STEREO << ESS_CFMT_ASHIFT))) ? 2 : 1, (int *)arg);

        case SOUND_PCM_READ_BITS:
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_CFMT_16BIT << ESS_CFMT_CSHIFT) 
					   : (ESS_CFMT_16BIT << ESS_CFMT_ASHIFT))) ? 16 : 8, (int *)arg);

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
		
	}
//	return mixer_ioctl(s, cmd, arg);
	return -EINVAL;
}

static int ess_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct ess_card *c = devs;
	struct ess_state *s = NULL, *sp;
	int i;
	unsigned char fmtm = ~0, fmts = 0;

	/*
	 *	Scan the cards and find the channel. We only
	 *	do this at open time so it is ok
	 */
	 
	while (c!=NULL)
	{
		for(i=0;i<8;i++)
		{
			sp=&c->channels[i];
			if(sp->dev_audio < 0)
				continue;
			if((sp->dev_audio ^ minor) & ~0xf)
				continue;
			s=sp;
		}
		c=c->next;
	}
		
	if (!s)
		return -ENODEV;
		
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & file->f_mode) {
		if (file->f_flags & O_NONBLOCK) {
			up(&s->open_sem);
			return -EWOULDBLOCK;
		}
		up(&s->open_sem);
		interruptible_sleep_on(&s->open_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		down(&s->open_sem);
	}
	if (file->f_mode & FMODE_READ) {
		fmtm &= ~((ESS_CFMT_STEREO | ESS_CFMT_16BIT) << ESS_CFMT_CSHIFT);
		if ((minor & 0xf) == SND_DEV_DSP16)
			fmts |= ESS_CFMT_16BIT << ESS_CFMT_CSHIFT;
		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags = s->dma_adc.subdivision = 0;
		set_adc_rate(s, 8000);
	}
	if (file->f_mode & FMODE_WRITE) {
		fmtm &= ~((ESS_CFMT_STEREO | ESS_CFMT_16BIT) << ESS_CFMT_ASHIFT);
		if ((minor & 0xf) == SND_DEV_DSP16)
			fmts |= ESS_CFMT_16BIT << ESS_CFMT_ASHIFT;
		s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags = s->dma_dac.subdivision = 0;
		set_dac_rate(s, 8000);
	}
	set_fmt(s, fmtm, fmts);
	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	start_bob(s);
	up(&s->open_sem);
	MOD_INC_USE_COUNT;
	return 0;
}

static int ess_release(struct inode *inode, struct file *file)
{
	struct ess_state *s = (struct ess_state *)file->private_data;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		drain_dac(s, file->f_flags & O_NONBLOCK);
	down(&s->open_sem);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac(s);
		dealloc_dmabuf(&s->dma_dac);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(s);
		dealloc_dmabuf(&s->dma_adc);
	}
	s->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);
	stop_bob(s);
	up(&s->open_sem);
	wake_up(&s->open_wait);
	MOD_DEC_USE_COUNT;
	return 0;
}

static /*const*/ struct file_operations ess_audio_fops = {
	&ess_llseek,
	&ess_read,
	&ess_write,
	NULL,  /* readdir */
	&ess_poll,
	&ess_ioctl,
	NULL,	/* XXX &ess_mmap, */
	&ess_open,
	NULL,	/* flush */
	&ess_release,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL,  /* lock */
};


/* --------------------------------------------------------------------- */

/*	XXX get rid of this
 *	maximum number of devices 
 */
#define NR_DEVICE 4

/* --------------------------------------------------------------------- */

static int maestro_install(struct pci_dev *pcidev, int card_type, int index)
{
	u16 w;
	u32 n;
	int iobase;
	int i;
	struct ess_card *card;
	struct ess_state *ess;
	int apu;
	int num = 0;
			
	iobase = pcidev->resource[0].start;

	card = kmalloc(sizeof(struct ess_card), GFP_KERNEL);
	if(card == NULL)
	{
		printk(KERN_WARNING "maestro: out of memory\n");
		return 0;
	}
	
	memset(card, 0, sizeof(*card));

	card->iobase = iobase;
	card->card_type = card_type;
	card->irq = pcidev->irq;
	card->next = devs;
	card->magic = ESS_CARD_MAGIC;
	devs = card;
	
	/* init our 8 groups of 4 apus */
	for(i=0;i<8;i++)
	{
		struct ess_state *s=&card->channels[i];
		
		s->card = card;
		init_waitqueue_head(&s->dma_adc.wait);
		init_waitqueue_head(&s->dma_dac.wait);
		init_waitqueue_head(&s->open_wait);
		init_MUTEX(&s->open_sem);
		s->magic = ESS_STATE_MAGIC;
		
		s->apu[0] = 4*i;
		s->apu[1] = (4*i)+1;
		s->apu[2] = (4*i)+2;
		s->apu[3] = (4*i)+3;
		
		if(s->dma_adc.ready || s->dma_dac.ready || s->dma_adc.rawbuf)
			printk("BOTCH!\n");
		/* register devices */
		if ((s->dev_audio = register_sound_dsp(&ess_audio_fops, -1)) < 0)
			break;
	}
	
	num = i;
	
	/* clear the rest if we ran out of slots to register */
	for(;i<8;i++)
	{
		struct ess_state *s=&card->channels[i];
		s->dev_audio = -1;
	}
	
	ess = &card->channels[0];
	
	/*
	 *	Ok card ready. Begin setup proper
	 */
	 
	printk(KERN_INFO "maestro: Configuring %s at 0x%04X\n", card_names[card_type], iobase);

	/*
	 *	Disable ACPI
	 */
	 
	pci_write_config_dword(pcidev, 0x54, 0x00000000);
	pci_write_config_dword(pcidev, 0x56, 0x00000000);
	
	/*
	 *	Use TDMA for now. TDMA works on all boards, so while its
	 *	not the most efficient its the simplest.
	 */ 
	 
	pci_read_config_word(pcidev, 0x50, &w);

	/* Clear DMA bits */
	w&=~(1<<10|1<<9|1<<8);

	/* TDMA on */
	w|=(1<<8);

/* XXX do we care about these two ? */
	/*
	 *	MPU at 330
	 */
	
	w&=~((1<<4)|(1<<3));
	
	/*
	 *	SB at 0x220
	 */

	w&=~(1<<2);
	
#if 0 /* huh?  its sub decode.. */

	/*
	 *	Reserved write as 0
	 */
	 
	w&=~(1<<1);
#endif
	
	/*
	 *	Some of these are undocumented bits
	 */
	 
	w&=~(1<<13)|(1<<14);		/* PIC Snoop mode bits */
	w&=~(1<<11);			/* Safeguard off */
	w|= (1<<7);			/* Posted write */
	w|= (1<<6);			/* ISA timing on */
	w&=~(1<<1);			/* Subtractive decode off */
	/* XXX huh?  claims to be reserved.. */
	w&=~(1<<5);			/* Don't swap left/right */
	
	pci_write_config_word(pcidev, 0x50, w);
	
	pci_read_config_word(pcidev, 0x52, &w);
	w&=~(1<<15);		/* Turn off internal clock multiplier */
	/* XXX how do we know which to use? */
	w&=~(1<<14);		/* External clock */
	
	w&=~(1<<7);		/* HWV off */
	w&=~(1<<6);		/* Debounce off */
	w&=~(1<<5);		/* GPIO 4:5 */
	w&=~(1<<4);		/* Disconnect from the CHI */
	w&=~(1<<3);		/* IDMA off (undocumented) */
	w&=~(1<<2);		/* MIDI fix off (undoc) */
	w&=~(1<<1);		/* reserved, always write 0 */
	w&=~(1<<0);		/* IRQ to ISA off (undoc) */
	pci_write_config_word(pcidev, 0x52, w);

	/*
	 *	DDMA off
	 */

	pci_read_config_word(pcidev, 0x60, &w);
	w&=~(1<<0);
	pci_write_config_word(pcidev, 0x60, w);
	
	/*
	 *	Legacy mode
	 */

	pci_read_config_word(pcidev, 0x40, &w);
	w|=(1<<15);	/* legacy decode off */
	w&=~(1<<14);	/* Disable SIRQ */
	w&=~(0x1f);	/* disable mpu irq/io, game port, fm, SB */
	 
	pci_write_config_word(pcidev, 0x40, w);

	sound_reset(iobase);

	/*
	 *	Reset the CODEC
	 */
	 
	maestro_ac97_reset(iobase);
		
	/*
	 *	Ring Bus Setup
	 */
	 	 
	n=inl(iobase+0x34);
	n&=~0xF000;
	n|=12<<12;		/* Direct Sound, Stereo */

	n=inl(iobase+0x34);
	n&=~0x0F00;		/* Modem off */
	outl(n, iobase+0x34);

	n=inl(iobase+0x34);
	n&=~0x00F0;
	n|=9<<4;		/* DAC, Stereo */
	outl(n, iobase+0x34);
	
	n=inl(iobase+0x34);
	n&=~0x000F;		/* ASSP off */
	outl(n, iobase+0x34);
	
	
	n=inl(iobase+0x34);
	n|=(1<<29);		/* Enable ring bus */
	outl(n, iobase+0x34);
	
	
	n=inl(iobase+0x34);
	n|=(1<<28);		/* Enable serial bus */
	outl(n, iobase+0x34);
	
	n=inl(iobase+0x34);
	n&=~0x00F00000;		/* MIC off */
	outl(n, iobase+0x34);
	
	n=inl(iobase+0x34);
	n&=~0x000F0000;		/* I2S off */
	outl(n, iobase+0x34);
	
	w=inw(iobase+0x18);
	w&=~(1<<7);		/* ClkRun off */
	outw(w, iobase+0x18);

	w=inw(iobase+0x18);
	w&=~(1<<6);		/* Harpo off */
	outw(w, iobase+0x18);
	
	w=inw(iobase+0x18);
	w&=~(1<<4);		/* ASSP irq off */
	outw(w, iobase+0x18);
	
	w=inw(iobase+0x18);
	w&=~(1<<3);		/* ISDN irq off */
	outw(w, iobase+0x18);
	
	w=inw(iobase+0x18);
	w|=(1<<2);		/* Direct Sound IRQ on */
	outw(w, iobase+0x18);

	w=inw(iobase+0x18);
	w&=~(1<<1);		/* MPU401 IRQ off */
	outw(w, iobase+0x18);

	w=inw(iobase+0x18);
	w|=(1<<0);		/* SB IRQ on */
	outw(w, iobase+0x18);


	outb(0, iobase+0xA4);
	outb(3, iobase+0xA2);
	outb(0, iobase+0xA6);
	
	for(apu=0;apu<16;apu++)
	{
		/* Write 0 into the buffer area 0x1E0->1EF */
		outw(0x01E0+apu, 0x10+iobase);
		outw(0x0000, 0x12+iobase);
	
		/*
		 * The 1.10 test program seem to write 0 into the buffer area
		 * 0x1D0-0x1DF too.
		 */
		outw(0x01D0+apu, 0x10+iobase);
		outw(0x0000, 0x12+iobase);
	}

#if 1
	wave_set_register(ess, IDR7_WAVE_ROMRAM, 
		(wave_get_register(ess, IDR7_WAVE_ROMRAM)&0xFF00));
	wave_set_register(ess, IDR7_WAVE_ROMRAM,
		wave_get_register(ess, IDR7_WAVE_ROMRAM)|0x100);
	wave_set_register(ess, IDR7_WAVE_ROMRAM,
		wave_get_register(ess, IDR7_WAVE_ROMRAM)&~0x200);
	wave_set_register(ess, IDR7_WAVE_ROMRAM,
		wave_get_register(ess, IDR7_WAVE_ROMRAM)|~0x400);
#else		
	maestro_write(ess, IDR7_WAVE_ROMRAM, 
		(maestro_read(ess, IDR7_WAVE_ROMRAM)&0xFF00));
	maestro_write(ess, IDR7_WAVE_ROMRAM,
		maestro_read(ess, IDR7_WAVE_ROMRAM)|0x100);
	maestro_write(ess, IDR7_WAVE_ROMRAM,
		maestro_read(ess, IDR7_WAVE_ROMRAM)&~0x200);
	maestro_write(ess, IDR7_WAVE_ROMRAM,
		maestro_read(ess, IDR7_WAVE_ROMRAM)|0x400);
#endif
	
	maestro_write(ess, IDR2_CRAM_DATA, 0x0000);
	maestro_write(ess, 0x08, 0xB004);
	/* Now back to the DirectSound stuff */
	maestro_write(ess, 0x09, 0x001B);
	maestro_write(ess, 0x0A, 0x8000);
	maestro_write(ess, 0x0B, 0x3F37);
	maestro_write(ess, 0x0C, 0x0098);
	
	maestro_write(ess, 0x0C, 
		(maestro_read(ess, 0x0C)&~0xF000)|0x8000);
	maestro_write(ess, 0x0C, 
		(maestro_read(ess, 0x0C)&~0x0F00)|0x0500);

	maestro_write(ess, 0x0D, 0x7632);
			
	/* Wave cache control on - test off, sg off, 
		enable, enable extra chans 1Mb */
		
	outw(inw(0x14+iobase)|(1<<8),0x14+iobase);
	outw(inw(0x14+iobase)&0xFE03,0x14+iobase);
	outw((inw(0x14+iobase)&0xFFFC), 0x14+iobase);
	outw(inw(0x14+iobase)|(1<<7),0x14+iobase);

	outw(0xA1A0, 0x14+iobase);	/* 0300 ? */

	if(maestro_ac97_get(iobase, 0x00)==0x0080)
	{
		maestro_pt101_init(iobase);
	}
	else
	{
		maestro_ac97_init(iobase);
		card->mix.supported_mixers = AC97_SUPPORTED_MASK;
		card->mix.stereo_mixers = AC97_STEREO_MASK;
		card->mix.record_sources = AC97_RECORD_MASK;
		card->mix.read_mixer = ac97_read_mixer;
		card->mix.write_mixer = ac97_write_mixer;
		card->mix.recmask_io = ac97_recmask_io;

		if ((card->dev_mixer = register_sound_mixer(&ess_mixer_fops, -1)) < 0)
			printk("maestro: couldn't register mixer!\n");
	}
	
	/* Now clear the channel data */	
	for(apu=0;apu<64;apu++)
	{
		for(w=0;w<0x0E;w++)
			apu_set_register(ess, apu|ESS_CHAN_HARD, w, 0);
	}
	
	if(request_irq(card->irq, ess_interrupt, SA_SHIRQ, card_names[card_type], card))
	{
		printk(KERN_ERR "maestro: unable to allocate irq %d,\n", card->irq);
		return 0;
	}

//	ess_play_test(ess);
	printk("maestro: %d channels configured.\n", num);
	return 1; 
}

#ifdef MODULE

__initfunc(int init_module(void))
#else
__initfunc(int init_maestro(void))
#endif
{
	struct pci_dev *pcidev = NULL;
	int index = 0;

	if (!pci_present())   /* No PCI bus in this machine! */
		return -ENODEV;
	printk(KERN_INFO "maestro: version " DRIVER_VERSION " time " __TIME__ " " __DATE__ "\n");

	pcidev = NULL;

	/*
	 *	Find the ESS Maestro 2.
	 */

	while((pcidev = pci_find_device(PCI_VENDOR_ESS, PCI_DEVICE_ID_ESS_ESS1968, pcidev))!=NULL)
	{
		index+=maestro_install(pcidev, TYPE_MAESTRO2, index);
		if(index == NR_DEVICE)
			return index;
	}

	/*
	 *	Find the ESS Maestro 2E
	 */

	while((pcidev = pci_find_device(PCI_VENDOR_ESS, PCI_DEVICE_ID_ESS_ESS1978, pcidev))!=NULL)
	{
		index+=maestro_install(pcidev, TYPE_MAESTRO2E, index);
		if(index == NR_DEVICE)
			return index;
	}

	/*
	 *	ESS Maestro 1
	 */

	while((pcidev = pci_find_device(PCI_VENDOR_ESS_OLD, PCI_DEVICE_ID_ESS_ESS0100, pcidev))!=NULL)
	{
		index+=maestro_install(pcidev, TYPE_MAESTRO, index);
		if(index == NR_DEVICE)
			return index;
	}
	if(index==0)
		return -ENODEV;
	return 0;
}

/* --------------------------------------------------------------------- */

#ifdef MODULE

MODULE_AUTHOR("Alan Cox <alan@redhat.com>");
MODULE_DESCRIPTION("ESS Maestro Driver");
#ifdef M_DEBUG
MODULE_PARM(debug,"i");
#endif

void cleanup_module(void)
{
	struct ess_card *s;

	while ((s = devs)) {
		int i;
		devs = devs->next;
//		ess_play_test(&s->channels[0]);
#ifndef ESS_HW_TIMER
		kill_bob(&s->channels[0]);
#else
		stop_bob(&s->channels[0]);
#endif
//		outb(~0, s->ioenh + SV_CODEC_INTMASK);  /* disable ints */
//		synchronize_irq();
//		inb(s->ioenh + SV_CODEC_STATUS); /* ack interrupts */
//		wrindir(s, SV_CIENABLE, 0);     /* disable DMAA and DMAC */
		//outb(0, s->iodmaa + SV_DMA_RESET);
		//outb(0, s->iodmac + SV_DMA_RESET);
		free_irq(s->irq, s);
		unregister_sound_mixer(s->dev_mixer);
		for(i=0;i<8;i++)
		{
			struct ess_state *ess = &s->channels[i];
			if(ess->dev_audio != -1)
				unregister_sound_dsp(ess->dev_audio);
		}
		kfree(s);
	}
	M_printk("maestro: unloading\n");
}

#endif /* MODULE */

#if 0
/*============================================================================
 *  ex-code that we're not using anymore..
 *============================================================================
 */
/*
 *	The ASSP is fortunately not double indexed
 */
 
static void assp_set_register(int ioaddr, u32 reg, u32 value)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();
	outl(reg, ioaddr+0x80);
	outl(value, ioaddr+0x84);
	restore_flags(flags);
}

static u32 assp_get_register(int ioaddr, u32 reg)
{
	unsigned long flags;
	u32 value;
	
	save_flags(flags);
	cli();
	outl(reg, ioaddr+0x80);
	value=inl(ioaddr+0x84);
	restore_flags(flags);
	
	return value;
}

/* the ASP is basically a DSP that one can dma instructions
	into.  it can do things like surround encoding or
	fm synth in sb emul mode.  It is highly proprietary
	and the ESS dudes are none too excited about telling
	us about it.  so screw it, we'll just turn it off and
	not bother with it.  Its not needed for apu/dac work. */


static void asp_load(int ioaddr, u16 l, u16 h, u16 *data, int len)
{
	int i;
	outw(l, ioaddr+0x80);
	outw(h, ioaddr+0x82);
	for(i=0;i<len;i++)
		outw(*data++, ioaddr+0x84);
}

static void asp_memset(int ioaddr, u16 l, u16 h, u16 v, int len)
{
	int i;
	outw(l, ioaddr+0x80);
	outw(h, ioaddr+0x82);
	for(i=0;i<len;i++)
		outw(v, ioaddr+0x84);
}

/*
 *	Load a code table into the ASP.
 */

#define ASSP_LOAD_PROGRAM 0x02 
#define ASSP_LOAD_DATA 0x03 

static void load_tables(int iobase)
{
	outb(0x00, ESS_SETUP_A4+iobase); /* start ASSP programming */

	asp_load(iobase, 0x0, ASSP_LOAD_PROGRAM, asp_block_0,
		sizeof(asp_block_0)/2);

	asp_load(iobase, 0x0800, ASSP_LOAD_PROGRAM,
		asp_block_1, sizeof(asp_block_1)/2);

	asp_memset(iobase, 0x1000, ASSP_LOAD_DATA, 0, 1024);

/*
 * At page 25 of the Maestro-2E data sheet, Table 7, there is a layout of the
 * ASSP memory mapping that describe the 0x2000-0x23FF as a data area.
 * The 1.10 version of the test code load 0x3B4 words of data into this area.
 * I have grabbed the data with hexdump and inserted them into this code.
 */ 
	asp_load(iobase, 0x2000, ASSP_LOAD_DATA, asp_block_4,
		sizeof(asp_block_4)/2);

	asp_memset(iobase, 0x11BC, ASSP_LOAD_DATA, 0x18, 36);

	asp_load(iobase, 0x13DC, ASSP_LOAD_DATA, asp_block_2,
		sizeof(asp_block_2)/2);

	asp_load(iobase, 0x1300, ASSP_LOAD_DATA, asp_block_3,
		sizeof(asp_block_3)/2);

	outb(0x41, ESS_SETUP_A4+iobase); /* stop programming and run ASSP */
}
/*
 *	Do not use the main maestro_reset.  it is known
 *	to leave certain chips in an unstable state.
 *	best to just reset the direct sound (apus) and
 *	assp pieces seperately.
 */

static void maestro_reset(int ioaddr)
{
	outw(0x8000, 0x18+ioaddr);
	udelay(10);
	outw(0x0000, 0x18+ioaddr);
	udelay(10);
}
#endif
