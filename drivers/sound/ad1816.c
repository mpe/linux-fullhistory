/*

AD1816 lowlevel sound driver for Linux 2.2.0 and above

Copyright (C) 1998 by Thorsten Knabe <tek@rbg.informatik.tu-darmstadt.de>
Based on the CS4232/AD1848 driver Copyright (C) by Hannu Savolainen 1993-1996

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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA

-------------------------------------------------------------------------------
NOTE!   NOTE!   NOTE!   NOTE!   NOTE!   NOTE!   NOTE!   NOTE!   NOTE!

This software is still under development. New versions of the driver 
are available from:
  http://www.student.informatik.tu-darmstadt.de/~tek/projects/linux.html
or
  http://www.tu-darmstadt.de/~tek01/projects/linux.html

Please report any bugs to: tek@rbg.informatik.tu-darmstadt.de

-------------------------------------------------------------------------------

version: 1.2
cvs: $Header: /home/tek/tmp/CVSROOT/sound21/ad1816.c,v 1.28 1999/01/16 19:01:36 tek Exp $
status: experimental
date: 1999/01/16

Changes:
	Oleg Drokin: Some cleanup of load/unload functions.    1998/11/24
	
	Thorsten Knabe: attach and unload rewritten, 
	some argument checks added                             1998/11/30

	Thorsten Knabe: Buggy isa bridge workaround added      1999/01/16
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include "soundmodule.h"
#include "sound_config.h"

#ifdef CONFIG_AD1816

#define DEBUGNOISE(x)
#define DEBUGINFO(x) 
#define DEBUGLOG(x) x
#define DEBUGWARN(x) x

#define CHECK_FOR_POWER { int timeout=100; \
  while (timeout > 0 && (inb(devc->base)&0x80)!= 0x80) {\
          timeout--; \
  } \
  if (timeout==0) {\
          printk("ad1816: Check for power failed in %s line: %d\n",__FILE__,__LINE__); \
  } \
}

/* structure to hold device specific information */
typedef struct
{
        int            base;          /* set in attach */
	int            irq;
	int            dma_playback;
        int            dma_capture;
  
        int            speed;         /* open */
	int            channels;
	int            audio_format;
	unsigned char  format_bits;
        int            audio_mode; 
	int            opened;
  
        int            recmask;        /* setup */
	int            supported_devices;
	int            supported_rec_devices;
	unsigned short levels[SOUND_MIXER_NRDEVICES];
        int            dev_no;   /* this is the # in audio_devs and NOT 
				    in ad1816_info */
	int            irq_ok;
	int            *osp;
  
}

ad1816_info;

static int  nr_ad1816_devs = 0;

static int  ad1816_clockfreq=33000;

/* for backward mapping of irq to sound device */

static volatile char irq2dev[17] = {-1, -1, -1, -1, -1, -1, -1, -1,
				    -1, -1, -1, -1, -1, -1, -1, -1, -1};


/* supported audio formats */
static int  ad_format_mask =
AFMT_U8 | AFMT_S16_LE | AFMT_S16_BE | AFMT_MU_LAW | AFMT_A_LAW;

/* array of device info structures */
static ad1816_info dev_info[MAX_AUDIO_DEV];


/* ------------------------------------------------------------------- */

/* functions for easier access to inderect registers */

static int ad_read (ad1816_info * devc, int reg)
{
	unsigned long   flags;
	int result;
	
	CHECK_FOR_POWER;

	save_flags (flags); /* make register access atomic */
	cli ();
	outb ((unsigned char) (reg & 0x3f), devc->base+0);
	result = inb(devc->base+2);
	result+= inb(devc->base+3)<<8;
	restore_flags (flags);
	
	return (result);
}


static void ad_write (ad1816_info * devc, int reg, int data)
{
	unsigned long flags;
	
	CHECK_FOR_POWER;
	
	save_flags (flags); /* make register access atomic */
	cli ();
	outb ((unsigned char) (reg & 0xff), devc->base+0);
	outb ((unsigned char) (data & 0xff),devc->base+2);
	outb ((unsigned char) ((data>>8)&0xff),devc->base+3);
	restore_flags (flags);

}

/* ------------------------------------------------------------------- */

/* function interface required by struct audio_driver */

static void ad1816_halt_input (int dev)
{
	unsigned long flags;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	unsigned char buffer;
	
	DEBUGINFO (printk("ad1816: halt_input called\n"));
	
	save_flags (flags); 
	cli ();
	
	if(!isa_dma_bridge_buggy) {
	        disable_dma(audio_devs[dev]->dmap_in->dma);
	}
	
	buffer=inb(devc->base+9);
	if (buffer & 0x01) {
		/* disable capture */
		outb(buffer & ~0x01,devc->base+9); 
	}

	if(!isa_dma_bridge_buggy) {
	        enable_dma(audio_devs[dev]->dmap_in->dma);
	}

	/* Clear interrupt status */
	outb (~0x40, devc->base+1);	
	
	devc->audio_mode &= ~PCM_ENABLE_INPUT;
	restore_flags (flags);
}

static void ad1816_halt_output (int dev)
{
	unsigned long  flags;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	
	unsigned char buffer;

	DEBUGINFO (printk("ad1816: halt_output called!\n"));

	save_flags (flags); 
	cli ();
	/* Mute pcm output */
	ad_write(devc, 4, ad_read(devc,4)|0x8080);

	if(!isa_dma_bridge_buggy) {
	        disable_dma(audio_devs[dev]->dmap_out->dma);
	}

	buffer=inb(devc->base+8);
	if (buffer & 0x01) {
		/* disable capture */
		outb(buffer & ~0x01,devc->base+8); 
	}

	if(!isa_dma_bridge_buggy) {
	        enable_dma(audio_devs[dev]->dmap_out->dma);
	}

	/* Clear interrupt status */
	outb ((unsigned char)~0x80, devc->base+1);	

	devc->audio_mode &= ~PCM_ENABLE_OUTPUT;
	restore_flags (flags);
}

static void ad1816_output_block (int dev, unsigned long buf, 
				 int count, int intrflag)
{
	unsigned long flags;
	unsigned long cnt;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	
	DEBUGINFO(printk("ad1816: output_block called buf=%ld count=%d flags=%d\n",buf,count,intrflag));
  
	cnt = count/4 - 1;
  
	save_flags (flags);
	cli ();
	
	/* set transfer count */
	ad_write (devc, 8, cnt & 0xffff); 
	
	devc->audio_mode |= PCM_ENABLE_OUTPUT; 
	restore_flags (flags);
}


static void ad1816_start_input (int dev, unsigned long buf, int count,
				int intrflag)
{
	unsigned long flags;
	unsigned long  cnt;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	
	DEBUGINFO(printk("ad1816: start_input called buf=%ld count=%d flags=%d\n",buf,count,intrflag));

	cnt = count/4 - 1;

	save_flags (flags); /* make register access atomic */
	cli ();

	/* set transfer count */
	ad_write (devc, 10, cnt & 0xffff); 

	devc->audio_mode |= PCM_ENABLE_INPUT;
	restore_flags (flags);
}


static int ad1816_ioctl (int dev, unsigned int cmd, caddr_t arg)
{
	return -(EINVAL);
}


static int ad1816_prepare_for_input (int dev, int bsize, int bcount)
{
	unsigned long flags;
	unsigned int freq;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	unsigned char fmt_bits;
	
	DEBUGINFO (printk ("ad1816: prepare_for_input called: bsize=%d bcount=%d\n",bsize,bcount));

	save_flags (flags); 
	cli ();
	
	fmt_bits= (devc->format_bits&0x7)<<3;
	
	/* set mono/stereo mode */
	if (devc->channels > 1) {
		fmt_bits |=0x4;
	}

	/* set Mono/Stereo in playback/capture register */
	outb( (inb(devc->base+8) & ~0x3C)|fmt_bits, devc->base+8); 
	outb( (inb(devc->base+9) & ~0x3C)|fmt_bits, devc->base+9);
  
	/* If compiled into kernel, AD1816_CLOCK is defined, so use it */
#ifdef AD1816_CLOCK 
	ad1816_clockfreq=AD1816_CLOCK;
#endif

	/* capture/playback frequency correction for soundcards 
	   with clock chips != 33MHz (allowed range 5 - 100 kHz) */

	if (ad1816_clockfreq<5000 || ad1816_clockfreq>100000) {
		ad1816_clockfreq=33000;
	}
	
	freq=((unsigned int)devc->speed*33000)/ad1816_clockfreq; 

	/* write playback/capture speeds */
	ad_write (devc, 2, freq & 0xffff);	
	ad_write (devc, 3, freq & 0xffff);	

	restore_flags (flags);

	ad1816_halt_input(dev);
	return 0;
}

static int ad1816_prepare_for_output (int dev, int bsize, int bcount)
{
	unsigned long flags;
	unsigned int freq;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	unsigned char fmt_bits;

	DEBUGINFO (printk ("ad1816: prepare_for_output called: bsize=%d bcount=%d\n",bsize,bcount));

	save_flags (flags); /* make register access atomic */
	cli ();

	fmt_bits= (devc->format_bits&0x7)<<3;
	/* set mono/stereo mode */
	if (devc->channels > 1) {
		fmt_bits |=0x4;
	}

	/* write format bits to playback/capture registers */
	outb( (inb(devc->base+8) & ~0x3C)|fmt_bits, devc->base+8); 
	outb( (inb(devc->base+9) & ~0x3C)|fmt_bits, devc->base+9);
  
#ifdef AD1816_CLOCK 
	ad1816_clockfreq=AD1816_CLOCK;
#endif

	/* capture/playback frequency correction for soundcards 
	   with clock chips != 33MHz (allowed range 5 - 100 kHz)*/

	if (ad1816_clockfreq<5000 || ad1816_clockfreq>100000) {
		ad1816_clockfreq=33000;
	}
  
	freq=((unsigned int)devc->speed*33000)/ad1816_clockfreq; 
	
	/* write playback/capture speeds */
	ad_write (devc, 2, freq & 0xffff);
	ad_write (devc, 3, freq & 0xffff);

	restore_flags (flags);
	
	ad1816_halt_output(dev);
	return 0;

}

static void ad1816_trigger (int dev, int state) 
{
	unsigned long flags;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;

	DEBUGINFO (printk("ad1816: trigger called! (devc=%d,devc->base=%d\n",devc,devc->base));

	/* mode may have changed */

	save_flags (flags); /* make register access atomic */
	cli ();

	/* mask out modes not specified on open call */
	state &= devc->audio_mode; 
				
	/* setup soundchip to new io-mode */
	if (state & PCM_ENABLE_INPUT) {
		/* enable capture */
		outb(inb(devc->base+9)|0x01, devc->base+9);
	} else {
		/* disable capture */
		outb(inb(devc->base+9)&~0x01, devc->base+9);
	}

	if (state & PCM_ENABLE_OUTPUT) {
		/* enable playback */
		outb(inb(devc->base+8)|0x01, devc->base+8);
		/* unmute pcm output */
		ad_write(devc, 4, ad_read(devc,4)&~0x8080);
	} else {
		/* mute pcm output */
		ad_write(devc, 4, ad_read(devc,4)|0x8080);
		/* disable capture */
		outb(inb(devc->base+8)&~0x01, devc->base+8);
	}
	restore_flags (flags);
}


/* halt input & output */
static void ad1816_halt (int dev)
{
	ad1816_halt_input(dev);
	ad1816_halt_output(dev);
}

static void ad1816_reset (int dev)
{
	ad1816_halt (dev);
}

/* set playback speed */
static int ad1816_set_speed (int dev, int arg)
{
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	
	if (arg == 0) {
		return devc->speed;
	}
	/* range checking */
	if (arg < 4000) {
		arg = 4000;
	}
	if (arg > 55000) {
		arg = 55000;
	}

	devc->speed = arg;
	return devc->speed;

}

static unsigned int ad1816_set_bits (int dev, unsigned int arg)
{
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	
	static struct format_tbl
	{
		int             format;
		unsigned char   bits;
	}
	format2bits[] =
	{
		{
			0, 0
		}
		,
		{
			AFMT_MU_LAW, 1
		}
		,
		{
			AFMT_A_LAW, 3
		}
		,
		{
			AFMT_IMA_ADPCM, 0
		}
		,
		{
			AFMT_U8, 0
		}
		,
		{
			AFMT_S16_LE, 2
		}
		,
		{
			AFMT_S16_BE, 6
		}
		,
		{
			AFMT_S8, 0
		}
		,
		{
			AFMT_U16_LE, 0
		}
		,
		{
			AFMT_U16_BE, 0
		}
  };
	int  i, n = sizeof (format2bits) / sizeof (struct format_tbl);

	/* return current format */
	if (arg == 0) {     
		return devc->audio_format;
	}
	
	devc->audio_format = arg;

	/* search matching format bits */
	for (i = 0; i < n; i++) {
		if (format2bits[i].format == arg) {
			devc->format_bits = format2bits[i].bits;
			devc->audio_format = arg;
			return arg;
		}
	}
	/* Still hanging here. Something must be terribly wrong */
	devc->format_bits = 0;
	return devc->audio_format = AFMT_U8;
}

static short ad1816_set_channels (int dev, short arg)
{
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;

	if (arg != 1 && arg != 2) {
		return devc->channels;
	}

	devc->channels = arg;
	return arg;
}

/* open device */
static int ad1816_open (int dev, int mode) 
{
	ad1816_info    *devc = NULL;
	unsigned long   flags;

	/* is device number valid ? */
	if (dev < 0 || dev >= num_audiodevs) { 
		return -(ENXIO);
	}

	/* get device info of this dev */
	devc = (ad1816_info *) audio_devs[dev]->devc; 

	/* make check if device already open atomic */
	save_flags (flags); 
	cli ();

	if (devc->opened) {
		restore_flags (flags);
		return -(EBUSY);
	}

	/* mark device as open */
	devc->opened = 1; 

	devc->audio_mode = 0;
	devc->speed = 8000;
	devc->audio_format=AFMT_U8;
	devc->channels=1;

	ad1816_reset(devc->dev_no); /* halt all pending output */
	restore_flags (flags);
	return 0;
}

static void ad1816_close (int dev) /* close device */
{
	unsigned long flags;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;

	save_flags (flags); 
	cli ();

	/* halt all pending output */
	ad1816_reset(devc->dev_no); 
	
	devc->opened = 0;
	devc->audio_mode = 0;
	devc->speed = 8000;
	devc->audio_format=AFMT_U8;
	devc->format_bits = 0;


	restore_flags (flags);
}


/* ------------------------------------------------------------------- */

/* Audio driver structure */

static struct audio_driver ad1816_audio_driver =
{
	ad1816_open,
	ad1816_close,
	ad1816_output_block,
	ad1816_start_input,
	ad1816_ioctl,
	ad1816_prepare_for_input,
	ad1816_prepare_for_output,
	ad1816_halt,
	NULL,
	NULL,
	ad1816_halt_input,
	ad1816_halt_output,
	ad1816_trigger,
	ad1816_set_speed,
	ad1816_set_bits,
	ad1816_set_channels,
	NULL,
	NULL
};


/* ------------------------------------------------------------------- */

/* Interrupt handler */

void ad1816_interrupt (int irq, void *dev_id, struct pt_regs *dummy)
{
	unsigned char   status;
	ad1816_info    *devc;
	int             dev;
	unsigned long flags;

	
	if (irq < 0 || irq > 15) {
	        printk ("ad1816: Got bogus interrupt %d\n", irq);
		return;
	}

	dev = irq2dev[irq];
	
	if (dev < 0 || dev >= num_audiodevs) {
	        printk ("ad1816: IRQ2AD1816-mapping failed for irq %d device %d\n", irq,dev);
		return;	        
	}

	devc = (ad1816_info *) audio_devs[dev]->devc;
	
	save_flags(flags);
	cli();

	/* read interrupt register */
	status = inb (devc->base+1); 
	/* Clear all interrupt  */
	outb (~status, devc->base+1);	

	DEBUGNOISE (printk("ad1816: Got interrupt subclass %d\n",status));
	
	devc->irq_ok=1;

	if (status == 0) {
		DEBUGWARN(printk ("ad1816: interrupt: Got interrupt, but no reason?\n"));
	}
	if (devc->opened && (devc->audio_mode & PCM_ENABLE_INPUT) 
	    && (status&64)){
		DMAbuf_inputintr (dev);
	}

	if (devc->opened && (devc->audio_mode & PCM_ENABLE_OUTPUT) &&
	    (status & 128)) {
		DMAbuf_outputintr (dev, 1);
	}
	restore_flags(flags);
}

/* ------------------------------------------------------------------- */

/* Mixer stuff */

struct mixer_def {
	unsigned int regno: 7;
	unsigned int polarity:1;	/* 0=normal, 1=reversed */
	unsigned int bitpos:4;
	unsigned int nbits:4;
};

static char mix_cvt[101] = {
	0, 0,3,7,10,13,16,19,21,23,26,28,30,32,34,35,37,39,40,42,
	43,45,46,47,49,50,51,52,53,55,56,57,58,59,60,61,62,63,64,65,
	65,66,67,68,69,70,70,71,72,73,73,74,75,75,76,77,77,78,79,79,
	80,81,81,82,82,83,84,84,85,85,86,86,87,87,88,88,89,89,90,90,
	91,91,92,92,93,93,94,94,95,95,96,96,96,97,97,98,98,98,99,99,
	100
};

typedef struct mixer_def mixer_ent;

/*
 * Most of the mixer entries work in backwards. Setting the polarity field
 * makes them to work correctly.
 *
 * The channel numbering used by individual soundcards is not fixed. Some
 * cards have assigned different meanings for the AUX1, AUX2 and LINE inputs.
 * The current version doesn't try to compensate this.
 */

#define MIX_ENT(name, reg_l, pola_l, pos_l, len_l, reg_r, pola_r, pos_r, len_r)	\
  {{reg_l, pola_l, pos_l, len_l}, {reg_r, pola_r, pos_r, len_r}}


mixer_ent mix_devices[SOUND_MIXER_NRDEVICES][2] = {
MIX_ENT(SOUND_MIXER_VOLUME,	14, 1, 8, 5,	14, 1, 0, 5),
MIX_ENT(SOUND_MIXER_BASS,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,	 5, 1, 8, 6,	 5, 1, 0, 6),
MIX_ENT(SOUND_MIXER_PCM,	 4, 1, 8, 6,	 4, 1, 0, 6),
MIX_ENT(SOUND_MIXER_SPEAKER,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	18, 1, 8, 5,	18, 1, 0, 5),
MIX_ENT(SOUND_MIXER_MIC,	19, 1, 8, 5,	19, 1, 0, 5),
MIX_ENT(SOUND_MIXER_CD,	 	15, 1, 8, 5,	15, 1, 0, 5),
MIX_ENT(SOUND_MIXER_IMIX,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	20, 0, 8, 4,	20, 0, 0, 4),
MIX_ENT(SOUND_MIXER_IGAIN,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_LINE1, 	17, 1, 8, 5,	17, 1, 0, 5),
MIX_ENT(SOUND_MIXER_LINE2,	16, 1, 8, 5,	16, 1, 0, 5),
MIX_ENT(SOUND_MIXER_LINE3,      39, 0, 9, 4,    39, 1, 0, 5)
};


static unsigned short default_mixer_levels[SOUND_MIXER_NRDEVICES] =
{
	0x4343,			/* Master Volume */
	0x3232,			/* Bass */
	0x3232,			/* Treble */
	0x0000,			/* FM */
	0x4343,			/* PCM */
	0x0000,			/* PC Speaker */
	0x0000,			/* Ext Line */
	0x0000,			/* Mic */
	0x0000,			/* CD */
	0x0000,			/* Recording monitor */
	0x0000,			/* SB PCM */
	0x0000,			/* Recording level */
	0x0000,			/* Input gain */
	0x0000,			/* Output gain */
	0x0000,			/* Line1 */
	0x0000,			/* Line2 */
	0x0000			/* Line3 (usually line in)*/
};

#define LEFT_CHN	0
#define RIGHT_CHN	1



static int
ad1816_set_recmask (ad1816_info * devc, int mask)
{
	unsigned char   recdev;
	int             i, n;
	
	mask &= devc->supported_rec_devices;
	
	n = 0;
	/* Count selected device bits */
	for (i = 0; i < 32; i++) {
		if (mask & (1 << i)) {
			n++;
		}
	}
	
	if (n == 0) {
		mask = SOUND_MASK_MIC;
	} else if (n != 1) { /* Too many devices selected */
		/* Filter out active settings */
		mask &= ~devc->recmask;	
		
		n = 0;
		/* Count selected device bits */
		for (i = 0; i < 32; i++) {	
			if (mask & (1 << i)) {
				n++;
			}
		}
		
		if (n != 1) {
			mask = SOUND_MASK_MIC;
		}
	}
	
	switch (mask) {
	case SOUND_MASK_MIC:
		recdev = 5;
		break;
		
	case SOUND_MASK_LINE:
		recdev = 0;
		break;
		
	case SOUND_MASK_CD:
		recdev = 2;
		break;
		
	case SOUND_MASK_LINE1:
		recdev = 4;
		break;
		
	case SOUND_MASK_LINE2:
		recdev = 3;
		break;
		
	case SOUND_MASK_VOLUME:
		recdev = 1;
		break;
		
	default:
		mask = SOUND_MASK_MIC;
		recdev = 5;
	}
	
	recdev <<= 4;
	ad_write (devc, 20, 
		  (ad_read (devc, 20) & 0x8f8f) | recdev | (recdev<<8));

	devc->recmask = mask;
	return mask;
}

static void
change_bits (int *regval, int dev, int chn, int newval)
{
	unsigned char   mask;
	int             shift;
  
	/* Reverse polarity*/

	if (mix_devices[dev][chn].polarity == 1) {	
		newval = 100 - newval;
	}

	mask = (1 << mix_devices[dev][chn].nbits) - 1;
	shift = mix_devices[dev][chn].bitpos;
	/* Scale it */
	newval = (int) ((newval * mask) + 50) / 100;	
	/* Clear bits */
	*regval &= ~(mask << shift);	
	/* Set new value */
	*regval |= (newval & mask) << shift;	
}

static int
ad1816_mixer_get (ad1816_info * devc, int dev)
{
	DEBUGINFO(printk("ad1816: mixer_get called!\n"));
	
	/* range check + supported mixer check */
	if (dev < 0 || dev >= SOUND_MIXER_NRDEVICES ) {
	        return (-(EINVAL));
	}
	if (!((1 << dev) & devc->supported_devices)) {
		return -(EINVAL);
	}
	
	return devc->levels[dev];
}

static int
ad1816_mixer_set (ad1816_info * devc, int dev, int value)
{
	int   left = value & 0x000000ff;
	int   right = (value & 0x0000ff00) >> 8;
	int   retvol;

	int   regoffs;
	int   val;
	int   valmute;

	DEBUGINFO(printk("ad1816: mixer_set called!\n"));
	
	if (dev < 0 || dev >= SOUND_MIXER_NRDEVICES ) {
		return -(EINVAL);
	}

	if (left > 100) {
		left = 100;
	}
	if (left < 0) {
		left = 0;
	}
	if (right > 100) {
		right = 100;
	}
	if (right < 0) {
		right = 0;
	}
	
	/* Mono control */
	if (mix_devices[dev][RIGHT_CHN].nbits == 0) {	
		right = left;
	}
	retvol = left | (right << 8);
	
	/* Scale it */
	
	left = mix_cvt[left];
	right = mix_cvt[right];

	/* reject all mixers that are not supported */
	if (!(devc->supported_devices & (1 << dev))) {
		return -(EINVAL);
	}
	
	/* sanity check */
	if (mix_devices[dev][LEFT_CHN].nbits == 0) {
		return -(EINVAL);
	}

	/* keep precise volume internal */
	devc->levels[dev] = retvol;

	/* Set the left channel */
	regoffs = mix_devices[dev][LEFT_CHN].regno;
	val = ad_read (devc, regoffs);
	change_bits (&val, dev, LEFT_CHN, left);

	valmute=val;

	/* Mute bit masking on some registers */
	if ( regoffs==5 || regoffs==14 || regoffs==15 ||
	     regoffs==16 || regoffs==17 || regoffs==18 || 
	     regoffs==19 || regoffs==39) {
		if (left==0) {
			valmute |= 0x8000;
		} else {
			valmute &= ~0x8000;
		}
	}
	ad_write (devc, regoffs, valmute); /* mute */

	/*
	 * Set the right channel
	 */
 
	/* Was just a mono channel */
	if (mix_devices[dev][RIGHT_CHN].nbits == 0) {
		return retvol;		
	}
	regoffs = mix_devices[dev][RIGHT_CHN].regno;
	val = ad_read (devc, regoffs);
	change_bits (&val, dev, RIGHT_CHN, right);

	valmute=val;
	if ( regoffs==5 || regoffs==14 || regoffs==15 ||
	     regoffs==16 || regoffs==17 || regoffs==18 || 
	     regoffs==19 || regoffs==39) {
		if (right==0) {
			valmute |= 0x80;
		} else {
			valmute &= ~0x80;
		}
	}
	ad_write (devc, regoffs, valmute); /* mute */
	
       	return retvol;
}

#define MIXER_DEVICES ( SOUND_MASK_VOLUME | \
			SOUND_MASK_SYNTH | \
			SOUND_MASK_PCM | \
			SOUND_MASK_LINE | \
			SOUND_MASK_LINE1 | \
			SOUND_MASK_LINE2 | \
			SOUND_MASK_LINE3 | \
			SOUND_MASK_MIC | \
			SOUND_MASK_CD | \
			SOUND_MASK_RECLEV  \
			)
#define REC_DEVICES ( SOUND_MASK_LINE2 |\
		      SOUND_MASK_LINE |\
		      SOUND_MASK_LINE1 |\
		      SOUND_MASK_MIC |\
		      SOUND_MASK_CD |\
		      SOUND_MASK_VOLUME \
		      )
     
static void
ad1816_mixer_reset (ad1816_info * devc)
{
	int  i;

	devc->supported_devices = MIXER_DEVICES;
	
	devc->supported_rec_devices = REC_DEVICES;

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (devc->supported_devices & (1 << i)) {
			ad1816_mixer_set (devc, i, default_mixer_levels[i]);
		}
	}
	ad1816_set_recmask (devc, SOUND_MASK_MIC);
}

static int
ad1816_mixer_ioctl (int dev, unsigned int cmd, caddr_t arg)
{
	ad1816_info    *devc = mixer_devs[dev]->devc;
	int val;
  
	DEBUGINFO(printk("ad1816: mixer_ioctl called!\n"));
  
	/* Mixer ioctl */
	if (((cmd >> 8) & 0xff) == 'M') { 
		
		/* set ioctl */
		if (_IOC_DIR (cmd) & _IOC_WRITE) { 
			switch (cmd & 0xff){
			case SOUND_MIXER_RECSRC:
				
				if (get_user(val, (int *)arg)) {
					return -EFAULT;
				}
				val=ad1816_set_recmask (devc, val);
				        return put_user(val, (int *)arg);
				break;
				
			default:
				if (get_user(val, (int *)arg)){
					return -EFAULT;
				}
				if ((val=ad1816_mixer_set (devc, cmd & 0xff, val))<0) {
				        return val;
				} else {
				        return put_user(val, (int *)arg);
				}
			}
		} else { 
			/* read ioctl */
			switch (cmd & 0xff) {
				
			case SOUND_MIXER_RECSRC:
				val=devc->recmask;
				return put_user(val, (int *)arg);
				break;
				
			case SOUND_MIXER_DEVMASK:
				val=devc->supported_devices;
				return put_user(val, (int *)arg);
				break;

			case SOUND_MIXER_STEREODEVS:
				val=devc->supported_devices & ~(SOUND_MASK_SPEAKER | SOUND_MASK_IMIX);
				return put_user(val, (int *)arg);
				break;
				
			case SOUND_MIXER_RECMASK:
				val=devc->supported_rec_devices;
				return put_user(val, (int *)arg);
				break;
				
			case SOUND_MIXER_CAPS:
				val=SOUND_CAP_EXCL_INPUT;
				return put_user(val, (int *)arg);
				break;
				
			default:
			        if ((val=ad1816_mixer_get (devc, cmd & 0xff))<0) {
				        return val;
				} else {
				        return put_user(val, (int *)arg);
				}
			}
		}
	} else {
		/* not for mixer */
		return -(EINVAL);
	}
}

/* ------------------------------------------------------------------- */

/* Mixer structure */

static struct mixer_operations ad1816_mixer_operations =
{
	"AD1816",
	"AD1816 Mixer",
	ad1816_mixer_ioctl
};


/* ------------------------------------------------------------------- */

/* stuff for card recognition, init and unloading */


/* replace with probe routine */
int probe_ad1816 ( struct address_info *hw_config )
{
	ad1816_info    *devc = &dev_info[nr_ad1816_devs];
	int io_base=hw_config->io_base;
	int *osp=hw_config->osp;
	int tmp;
	
	printk("ad1816: AD1816 sounddriver Copyright (C) 1998 by Thorsten Knabe\n");
	printk("ad1816: $Header: /home/tek/tmp/CVSROOT/sound21/ad1816.c,v 1.28 1999/01/16 19:01:36 tek Exp $\n");
	printk("ad1816: io=0x%x, irq=%d, dma=%d, dma2=%d, isadmabug=%d\n",
	       hw_config->io_base,
	       hw_config->irq,
	       hw_config->dma,
	       hw_config->dma2,
	       isa_dma_bridge_buggy);

	if (check_region (io_base, 16)) {
		printk ("ad1816: I/O port 0x%03x not free\n", io_base);
		return 0;
	}

	DEBUGLOG(printk ("ad1816: detect(%x)\n", io_base));
	
	if (nr_ad1816_devs >= MAX_AUDIO_DEV) {
		printk ("ad1816: detect error - step 0\n");
		return 0;
	}
  
	devc->base = io_base;
	devc->irq_ok = 0;
	devc->irq = 0;
	devc->opened = 0;
	devc->osp = osp;

	/* base+0: bit 1 must be set but not 255 */
	tmp=inb(devc->base);
	if ( (tmp&0x80)==0 || tmp==255 ) {
		DEBUGLOG (printk ("ad1816: Chip is not an AD1816 or chip is not active (Test 0)\n"));
		return(0);
	}


	/* writes to ireg 8 are copied to ireg 9 */
	ad_write(devc,8,12345); 
	if (ad_read(devc,9)!=12345) {
		DEBUGLOG (printk ("ad1816: Chip is not an AD1816 (Test 1)\n"));
		return(0);
	}
  
	/* writes to ireg 8 are copied to ireg 9 */
	ad_write(devc,8,54321); 
	if (ad_read(devc,9)!=54321) {
		DEBUGLOG (printk ("ad1816: Chip is not an AD1816 (Test 2)\n"));
		return(0);
	}
  
	/* writes to ireg 10 are copied to ireg 11 */
	ad_write(devc,10,54321); 
	if (ad_read(devc,11)!=54321) {
		DEBUGLOG (printk ("ad1816: Chip is not an AD1816 (Test 3)\n"));
		return(0);
	}

	/* writes to ireg 10 are copied to ireg 11 */
	ad_write(devc,10,12345); 
	if (ad_read(devc,11)!=12345) {
		DEBUGLOG (printk ("ad1816: Chip is not an AD1816 (Test 4)\n"));
		return(0);
	}
	
	/* bit in base +1 cannot be set to 1 */
	tmp=inb(devc->base+1);
	outb(0xff,devc->base+1); 
	if (inb(devc->base+1)!=tmp) {
		DEBUGLOG (printk ("ad1816: Chip is not an AD1816 (Test 5)\n"));
		return(0);
	}

  
	DEBUGLOG (printk ("ad1816: detect() - Detected OK\n"));
	DEBUGLOG (printk ("ad1816: AD1816 Version: %d\n",ad_read(devc,45)));
	
	/*  detection was successful */
	return 1; 
}


/* allocate resources from the kernel. If any allocation fails, free
   all allocated resources and exit attach.
  
 */

void attach_ad1816 (struct address_info *hw_config)
{
	int             my_dev;
	char            dev_name[100];
	ad1816_info    *devc = &dev_info[nr_ad1816_devs];
	

	/* allocate i/o ports */
       	request_region (hw_config->io_base, 16, "AD1816 Sound");
	devc->base = hw_config->io_base;	

	/* disable all interrupts */
	ad_write(devc,1,0);     

	/* Clear pending interrupts */
	outb (0, devc->base+1);	

	/* allocate irq */
	if (hw_config->irq < 0 || hw_config->irq > 15) {
		release_region(hw_config->io_base, 16);
		return;	  
	}
	if (request_irq(hw_config->irq, ad1816_interrupt,0,
			"SoundPort",
			hw_config->osp) < 0)	{
	        printk ("ad1816: IRQ in use\n");
		release_region(hw_config->io_base, 16);
		return;
	}
	devc->irq=hw_config->irq;

	/* DMA stuff */
	if (sound_alloc_dma (hw_config->dma, "Sound System")) {
		printk ("ad1816: Can't allocate DMA%d\n", hw_config->dma);
		free_irq(hw_config->irq,hw_config->osp);
		release_region(hw_config->io_base, 16);
		return;
	}
	devc->dma_playback=hw_config->dma;
	
	if ( hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma) {
		if (sound_alloc_dma (hw_config->dma2, "Sound System (capture)")) {
			printk ("ad1816: Can't allocate DMA%d\n", hw_config->dma2);
			sound_free_dma(hw_config->dma);
			free_irq(hw_config->irq,hw_config->osp);
			release_region(hw_config->io_base, 16);
			return;
		}
		devc->dma_capture=hw_config->dma2;
		devc->audio_mode=DMA_AUTOMODE|DMA_DUPLEX;
	} else {
	        devc->dma_capture=-1;
		devc->audio_mode=DMA_AUTOMODE;
	}

	sprintf (dev_name,"AD1816 audio driver");
  
	conf_printf2 (dev_name,
		      devc->base, devc->irq, hw_config->dma, hw_config->dma2);

	/* register device */
	if ((my_dev = sound_install_audiodrv (AUDIO_DRIVER_VERSION,
					      dev_name,
					      &ad1816_audio_driver,
					      sizeof (struct audio_driver),
					      devc->audio_mode,
					      ad_format_mask,
					      devc,
					      hw_config->dma, 
					      hw_config->dma2)) < 0) {
		printk ("ad1816: Can't install sound driver\n");
		if (devc->dma_capture>=0) {
		        sound_free_dma(hw_config->dma2);
		}
		sound_free_dma(hw_config->dma);
		free_irq(hw_config->irq,hw_config->osp);
		release_region(hw_config->io_base, 16);
		return;

	}

	/* fill rest of structure with reasonable default values */
	irq2dev[hw_config->irq] = devc->dev_no = my_dev;
	devc->opened = 0;
	devc->irq_ok = 0;
	devc->osp = hw_config->osp;  
	nr_ad1816_devs++;

	ad_write(devc,32,0x80f0); /* sound system mode */
	ad_write(devc,33,0x03f8); /* enable all audiosources for dsp */
	ad_write(devc,4,0x8080);  /* default values for volumes (muted)*/
	ad_write(devc,5,0x8080);
	ad_write(devc,6,0x8080);
	ad_write(devc,7,0x8080);
	ad_write(devc,15,0x8888);
	ad_write(devc,16,0x8888);
	ad_write(devc,17,0x8888);
	ad_write(devc,18,0x8888);
	ad_write(devc,19,0xc888); /* +20db mic active */
	ad_write(devc,14,0x0000); /* Master volume unmuted full power */
	ad_write(devc,39,0x009f); /* 3D effect on 0% phone out muted */
	ad_write(devc,44,0x0080); /* everything on power, 3d enabled for d/a */
	outb(0x10,devc->base+8); /* set dma mode */
	outb(0x10,devc->base+9);
  
	/* enable capture + playback interrupt */
	ad_write(devc,1,0xc000); 
	
	/* set mixer defaults */
	ad1816_mixer_reset (devc); 
  
	/* register mixer */
	if ((audio_devs[my_dev]->mixer_dev=sound_install_mixer(
				       MIXER_DRIVER_VERSION,
				       dev_name,
				       &ad1816_mixer_operations,
				       sizeof (struct mixer_operations),
				       devc)) >= 0) {
		audio_devs[my_dev]->min_fragment = 0;
	}
}

void unload_card(ad1816_info *devc)
{
	int  mixer, dev = 0;
	
	if (devc != NULL) {
		DEBUGLOG (printk("ad1816: Unloading card at base=%x\n",devc->base));
		
		dev = devc->dev_no;
		mixer = audio_devs[dev]->mixer_dev;

		/* unreg mixer*/
		if(mixer>=0) {
			sound_unload_mixerdev(mixer);
		}
		sound_unload_audiodev(dev);
		
		/* free dma channels */
		if (devc->dma_capture>=0) {
			sound_free_dma(devc->dma_capture);
		}

		/* card wont get added if resources could not be allocated
		   thus we need not ckeck if allocation was successful */
		sound_free_dma (devc->dma_playback);
		free_irq(devc->irq, devc->osp);
		release_region (devc->base, 16);
		
		DEBUGLOG (printk("ad1816: Unloading card at base=%x was successful\n",devc->base));
		
	} else {
		printk ("ad1816: no device/card specified\n");
	}
}

void unload_ad1816 (struct address_info *hw_config)
{
	int          i;
	ad1816_info  *devc = NULL;
  
	/* remove any soundcard */
	if (hw_config==NULL) {
		for (i = 0;  i < nr_ad1816_devs; i++) {
			devc = &dev_info[i];
			unload_card(devc);
		}     
		nr_ad1816_devs=0;
	} else { 
		/* remove specified soundcard */
		for (i = 0; i < nr_ad1816_devs; i++) {
		        int j;
			
			if (dev_info[i].base == hw_config->io_base) {
				devc = &dev_info[i];
				unload_card(devc);
				nr_ad1816_devs--;
				for ( j=i; j < nr_ad1816_devs ; j++) {
					dev_info[j] = dev_info[j+1];
				}
				i--;
			}
		}
	}
}


/* ----------------------------- 2.1.xxx module stuff ----------------- */

EXPORT_SYMBOL(ad1816_interrupt);
EXPORT_SYMBOL(probe_ad1816);
EXPORT_SYMBOL(attach_ad1816);
EXPORT_SYMBOL(unload_ad1816);


#ifdef MODULE

int             io = -1;
int             irq = -1;
int             dma = -1;
int             dma2 = -1;

MODULE_PARM(io,"i");
MODULE_PARM(irq,"i");
MODULE_PARM(dma,"i");
MODULE_PARM(dma2,"i");
MODULE_PARM(ad1816_clockfreq,"i");

struct address_info cfg;


int init_module(void)
{
        if (io == -1 || irq == -1 || dma == -1 || dma2 == -1) {
		printk("ad1816: dma, dma2, irq and io must be set.\n");
                return -EINVAL;
        }
        cfg.io_base = io;
        cfg.irq = irq;
        cfg.dma = dma;
        cfg.dma2 = dma2;
	
        if (probe_ad1816(&cfg) == 0) {
                return -ENODEV;
	}
        attach_ad1816(&cfg);
        SOUND_LOCK;
        return 0;
}


void cleanup_module(void)
{
        unload_ad1816(NULL);
        SOUND_LOCK_END;
}

#endif /* MODULE */

#endif /* CONFIG_AD1816 */
