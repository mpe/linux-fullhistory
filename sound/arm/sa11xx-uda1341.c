/*
 *  Driver for Philips UDA1341TS on Compaq iPAQ H3600 soundcard
 *  Copyright (C) 2002 Tomas Kasparek <tomas.kasparek@seznam.cz>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License.
 * 
 * History:
 *
 * 2002-03-13	Tomas Kasparek	Initial release - based on h3600-uda1341.c from OSS
 * 2002-03-20   Tomas Kasparek  playback over ALSA is working
 * 2002-03-28   Tomas Kasparek  playback over OSS emulation is working
 * 2002-03-29   Tomas Kasparek  basic capture is working (native ALSA)
 * 2002-03-29   Tomas Kasparek  capture is working (OSS emulation)
 * 2002-04-04   Tomas Kasparek  better rates handling (allow non-standard rates)
 */

/* $Id: sa11xx-uda1341.c,v 1.3 2002/05/25 10:26:06 perex Exp $ */

#include <sound/driver.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <asm/hardware.h>
#include <asm/dma.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include <linux/l3/l3.h>

#undef DEBUG_MODE
#undef DEBUG_FUNCTION_NAMES
#include <sound/uda1341.h>

/* {{{ Type definitions */

MODULE_AUTHOR("Tomas Kasparek <tomas.kasparek@seznam.cz>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SA1100/SA1111 + UDA1341TS driver for ALSA");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{UDA1341,iPAQ H3600 UDA1341TS}}");

static char *snd_id = NULL;	/* ID for this card */

MODULE_PARM(snd_id, "s");
MODULE_PARM_DESC(snd_id, "ID string for SA1100/SA1111 + UDA1341TS soundcard.");

#define chip_t sa11xx_uda1341_t

#define SHIFT_16_STEREO         2

typedef enum stream_id_t{
	PLAYBACK=0,
	CAPTURE,
	MAX_STREAMS,
}stream_id_t;

typedef struct audio_stream {
	char *id;		/* identification string */
	dma_device_t dma_dev;	/* device identifier for DMA */
	dma_regs_t *dma_regs;	/* points to our DMA registers */

	int active:1;		/* we are using this stream for transfer now */

	int sent_periods;       /* # of sent periods from actual DMA buffer */
	int sent_total;         /* # of sent periods total (just for info & debug) */

	int sync;               /* are we recoding - flag used to do DMA trans. for sync */
        
	snd_pcm_substream_t *stream;
}audio_stream_t;

/* I do not want to have substream = NULL when syncing - ALSA does not like it */
#define SYNC_SUBSTREAM ((void *) -1)

typedef struct snd_card_sa11xx_uda1341 {
	struct pm_dev *pm_dev;        
	snd_card_t *card;
	struct l3_client *uda1341;

	long samplerate;

	audio_stream_t *s[MAX_STREAMS];
	snd_info_entry_t *proc_entry;
}sa11xx_uda1341_t;

static struct snd_card_sa11xx_uda1341 *sa11xx_uda1341 = NULL;

static unsigned int rates[] = {
	8000,  10666, 10985, 14647,
	16000, 21970, 22050, 24000,
	29400, 32000, 44100, 48000,
};

#define RATES sizeof(rates) / sizeof(rates[0])

static snd_pcm_hw_constraint_list_t hw_constraints_rates = {
	.count	= RATES,
	.list	= rates,
	.mask	= 0,
};

/* }}} */

/* {{{ Clock and sample rate stuff */

/*
 * Stop-gap solution until rest of hh.org HAL stuff is merged.
 */
#define GPIO_H3600_CLK_SET0		GPIO_GPIO (12)
#define GPIO_H3600_CLK_SET1		GPIO_GPIO (13)

#ifdef CONFIG_SA1100_H3XXX
#define	clr_sa11xx_uda1341_egpio(x)	clr_h3600_egpio(x)
#define set_sa11xx_uda1341_egpio(x)	set_h3600_egpio(x)
#else
#error This driver could serve H3x00 handhelds only!
#endif

static void sa11xx_uda1341_set_audio_clock(long val)
{
	switch (val) {
	case 24000: case 32000: case 48000:	/* 00: 12.288 MHz */
		GPCR = GPIO_H3600_CLK_SET0 | GPIO_H3600_CLK_SET1;
		break;

	case 22050: case 29400: case 44100:	/* 01: 11.2896 MHz */
		GPSR = GPIO_H3600_CLK_SET0;
		GPCR = GPIO_H3600_CLK_SET1;
		break;

	case 8000: case 10666: case 16000:	/* 10: 4.096 MHz */
		GPCR = GPIO_H3600_CLK_SET0;
		GPSR = GPIO_H3600_CLK_SET1;
		break;

	case 10985: case 14647: case 21970:	/* 11: 5.6245 MHz */
		GPSR = GPIO_H3600_CLK_SET0 | GPIO_H3600_CLK_SET1;
		break;
	}
}

static void sa11xx_uda1341_set_samplerate(sa11xx_uda1341_t *sa11xx_uda1341, long rate)
{
	int clk_div = 0;
	int clk=0;

	DEBUG(KERN_DEBUG "set_samplerate rate: %ld\n", rate);
        
	/* We don't want to mess with clocks when frames are in flight */
	Ser4SSCR0 &= ~SSCR0_SSE;
	/* wait for any frame to complete */
	udelay(125);

	/*
	 * We have the following clock sources:
	 * 4.096 MHz, 5.6245 MHz, 11.2896 MHz, 12.288 MHz
	 * Those can be divided either by 256, 384 or 512.
	 * This makes up 12 combinations for the following samplerates...
	 */
	if (rate >= 48000)
		rate = 48000;
	else if (rate >= 44100)
		rate = 44100;
	else if (rate >= 32000)
		rate = 32000;
	else if (rate >= 29400)
		rate = 29400;
	else if (rate >= 24000)
		rate = 24000;
	else if (rate >= 22050)
		rate = 22050;
	else if (rate >= 21970)
		rate = 21970;
	else if (rate >= 16000)
		rate = 16000;
	else if (rate >= 14647)
		rate = 14647;
	else if (rate >= 10985)
		rate = 10985;
	else if (rate >= 10666)
		rate = 10666;
	else
		rate = 8000;

	/* Set the external clock generator */
	sa11xx_uda1341_set_audio_clock(rate);

	/* Select the clock divisor */
	switch (rate) {
	case 8000:
	case 10985:
	case 22050:
	case 24000:
		clk = F512;
		clk_div = SSCR0_SerClkDiv(16);
		break;
	case 16000:
	case 21970:
	case 44100:
	case 48000:
		clk = F256;
		clk_div = SSCR0_SerClkDiv(8);
		break;
	case 10666:
	case 14647:
	case 29400:
	case 32000:
		clk = F384;
		clk_div = SSCR0_SerClkDiv(12);
		break;
	}

	l3_command(sa11xx_uda1341->uda1341, CMD_FORMAT, (void *)LSB16);
	l3_command(sa11xx_uda1341->uda1341, CMD_FS, (void *)clk);        
	Ser4SSCR0 = (Ser4SSCR0 & ~0xff00) + clk_div + SSCR0_SSE;
	DEBUG(KERN_DEBUG "set_samplerate done (new rate: %ld)\n", rate);
	sa11xx_uda1341->samplerate = rate;
}

/* }}} */

/* {{{ HW init and shutdown */

static void sa11xx_uda1341_audio_init(sa11xx_uda1341_t *sa11xx_uda1341)
{
	unsigned long flags;

	DEBUG_NAME(KERN_DEBUG "audio_init\n");

	/* Setup DMA stuff */
	if (sa11xx_uda1341->s[PLAYBACK]) {
		sa11xx_uda1341->s[PLAYBACK]->id = "UDA1341 out";
		sa11xx_uda1341->s[PLAYBACK]->dma_dev = DMA_Ser4SSPWr;
	}

	if (sa11xx_uda1341->s[CAPTURE]) {
		sa11xx_uda1341->s[CAPTURE]->id = "UDA1341 in";
		sa11xx_uda1341->s[CAPTURE]->dma_dev = DMA_Ser4SSPRd;
	}

	/* Initialize the UDA1341 internal state */
       
	/* Setup the uarts */
	local_irq_save(flags);
	GAFR |= (GPIO_SSP_CLK);
	GPDR &= ~(GPIO_SSP_CLK);
	Ser4SSCR0 = 0;
	Ser4SSCR0 = SSCR0_DataSize(16) + SSCR0_TI + SSCR0_SerClkDiv(8);
	Ser4SSCR1 = SSCR1_SClkIactL + SSCR1_SClk1P + SSCR1_ExtClk;
	Ser4SSCR0 |= SSCR0_SSE;

	/* Enable the audio power */
	clr_sa11xx_uda1341_egpio(IPAQ_EGPIO_CODEC_NRESET);
	set_sa11xx_uda1341_egpio(IPAQ_EGPIO_AUDIO_ON);
	set_sa11xx_uda1341_egpio(IPAQ_EGPIO_QMUTE);
	local_irq_restore(flags);
        
	/* Initialize the UDA1341 internal state */
	l3_open(sa11xx_uda1341->uda1341);

	/* external clock configuration */
	sa11xx_uda1341_set_samplerate(sa11xx_uda1341, 44100); /* default sample rate */                

	/* Wait for the UDA1341 to wake up */
	set_sa11xx_uda1341_egpio(IPAQ_EGPIO_CODEC_NRESET);
	mdelay(1);

	/* make the left and right channels unswapped (flip the WS latch ) */
	Ser4SSDR = 0;
       
	clr_sa11xx_uda1341_egpio(IPAQ_EGPIO_QMUTE);        
}

static void sa11xx_uda1341_audio_shutdown(sa11xx_uda1341_t *sa11xx_uda1341)
{
	/* disable the audio power and all signals leading to the audio chip */
	l3_close(sa11xx_uda1341->uda1341);
	Ser4SSCR0 = 0;
	clr_sa11xx_uda1341_egpio(IPAQ_EGPIO_CODEC_NRESET);
	clr_sa11xx_uda1341_egpio(IPAQ_EGPIO_AUDIO_ON);
	clr_sa11xx_uda1341_egpio(IPAQ_EGPIO_QMUTE);
}

/* }}} */

/* {{{ DMA staff */

#define SYNC_ADDR		(dma_addr_t)FLUSH_BASE_PHYS
#define SYNC_SIZE		4096 // was 2048

#define DMA_REQUEST(s, cb)	sa1100_request_dma((s)->dma_dev, (s)->id, cb, s, \
                                                   &((s)->dma_regs))
#define DMA_FREE(s)		{sa1100_free_dma((s)->dma_regs); (s)->dma_regs = 0;}
#define DMA_START(s, d, l)	sa1100_start_dma((s)->dma_regs, d, l)
#define DMA_STOP(s)		sa1100_stop_dma((s)->dma_regs)
#define DMA_CLEAR(s)		sa1100_clear_dma((s)->dma_regs)
#define DMA_RESET(s)		sa1100_reset_dma((s)->dma_regs)
#define DMA_POS(s)              sa1100_get_dma_pos((s)->dma_regs)

static void audio_dma_request(audio_stream_t *s, void (*callback)(void *))
{
	DMA_REQUEST(s, callback);
}

static void audio_dma_free(audio_stream_t *s)
{
	DMA_FREE(s);
}

static u_int audio_get_dma_pos(audio_stream_t *s)
{
	snd_pcm_substream_t * substream = s->stream;
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int offset;
        
	DEBUG_NAME(KERN_DEBUG "get_dma_pos");
        
	offset = DMA_POS(s) - substream->runtime->dma_addr;
	DEBUG(" %d ->", offset);
	offset >>= SHIFT_16_STEREO;                
	DEBUG(" %d [fr]\n", offset);
        
	if (offset >= runtime->buffer_size){
		offset = runtime->buffer_size;
	}

	DEBUG(KERN_DEBUG "  hw_ptr_interrupt: %lX\n",
	      (unsigned long)runtime->hw_ptr_interrupt);
	DEBUG(KERN_DEBUG "  updated pos [fr]: %ld\n",
	      offset - (offset % runtime->min_align));
        
	return offset;
}


static void audio_stop_dma(audio_stream_t *s)
{
	long flags;

	DEBUG_NAME(KERN_DEBUG "stop_dma\n");
        
	if (!s->stream)
		return;

	local_irq_save(flags);
	s->active = 0;
	s->sent_periods = 0;
	s->sent_total = 0;        

	DMA_STOP(s);
	DMA_CLEAR(s);
	local_irq_restore(flags);
}


static void audio_reset(audio_stream_t *s)
{
	DEBUG_NAME(KERN_DEBUG "dma_reset\n");
        
	if (s->stream) {
		audio_stop_dma(s);
	}
	s->active = 0;
}


static void audio_process_dma(audio_stream_t *s)
{
	snd_pcm_substream_t * substream = s->stream;
	snd_pcm_runtime_t *runtime;
	int ret,i;
                
	DEBUG_NAME(KERN_DEBUG "process_dma\n");

	if(!s->active){
		DEBUG("!!!want to process DMA when stopped!!!\n");
		return;
	}

	/* we are requested to process synchronization DMA transfer */
	if (s->sync) {
		while (1) {
			DEBUG(KERN_DEBUG "sent sync period (dma_size[B]: %d)\n", SYNC_SIZE);
			ret = DMA_START(s, SYNC_ADDR, SYNC_SIZE);
			if (ret)
				return;   
		}
	}

	/* must be set here - for sync there is no runtime struct */
	runtime = substream->runtime;
        
	while(1) {       
		unsigned int  dma_size = runtime->period_size << SHIFT_16_STEREO;
		unsigned int offset = dma_size * s->sent_periods;
                
		if (dma_size > MAX_DMA_SIZE){
			/* this should not happen! */
			DEBUG(KERN_DEBUG "-----> cut dma_size: %d -> ", dma_size);
			dma_size = CUT_DMA_SIZE;
			DEBUG("%d <-----\n", dma_size);
		}

		ret = DMA_START(s, runtime->dma_addr + offset, dma_size);
		if (ret)
			return;

		DEBUG(KERN_DEBUG "sent period %d (%d total)(dma_size[B]: %d"
		      "offset[B]: %06d)\n",
		      s->sent_periods, s->sent_total, dma_size, offset);

#ifdef DEBUG_MODE
		printk(KERN_DEBUG "  dma_area:");
		for (i=0; i < 32; i++) {
			printk(" %02x", *(char *)(runtime->dma_addr+offset+i));
		}
		printk("\n");
#endif                
		s->sent_total++;
		s->sent_periods++;
		s->sent_periods %= runtime->periods;
	}
}


static void audio_dma_callback(void *data)
{
	audio_stream_t *s = data;
	char *buf;
	int i;
        
	DEBUG_NAME(KERN_DEBUG "dma_callback\n");

	/* when syncing we do not have any real stream from ALSA! */
	if (!s->sync) {
		snd_pcm_period_elapsed(s->stream);
		DEBUG(KERN_DEBUG "----> period done <----\n");
#ifdef DEBUG_MODE
		printk(KERN_DEBUG "  dma_area:");
		buf = s->stream->runtime->dma_addr +
			((s->sent_periods - 1 ) *
			 (s->stream->runtime->period_size << SHIFT_16_STEREO));
		for (i=0; i < 32; i++) {
			printk(" %02x", *(char *)(buf + i));
		}
		printk("\n");
#endif                      
	}
        
	if (s->active)
		audio_process_dma(s);
}

/* }}} */

/* {{{ PCM setting */

/* {{{ trigger & timer */

static int snd_card_sa11xx_uda1341_pcm_trigger(stream_id_t stream_id,
				      snd_pcm_substream_t * substream, int cmd)
{
	sa11xx_uda1341_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int i;

	DEBUG_NAME(KERN_DEBUG "pcm_trigger id: %d cmd: %d\n", stream_id, cmd);

	DEBUG(KERN_DEBUG "  sound: %d x %d [Hz]\n", runtime->channels, runtime->rate);
	DEBUG(KERN_DEBUG "  periods: %ld x %ld [fr]\n", (unsigned long)runtime->periods,
	      (unsigned long) runtime->period_size);
	DEBUG(KERN_DEBUG "  buffer_size: %ld [fr]\n", (unsigned long)runtime->buffer_size);
	DEBUG(KERN_DEBUG "  dma_addr %p\n", (char *)runtime->dma_addr);

#ifdef DEBUG_MODE
	printk(KERN_DEBUG "  dma_area:");
	for (i=0; i < 32; i++) {
		printk(" %02x", *(char *)(runtime->dma_addr+i));
	}
	printk("\n");
#endif
        
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		/* want to capture and have no playback - run DMA syncing */
		if (stream_id == CAPTURE && !chip->s[PLAYBACK]->active) {
			/* we need synchronization DMA transfer (zeros) */
			DEBUG(KERN_DEBUG "starting synchronization DMA transfer\n");
			chip->s[PLAYBACK]->sync = 1;
			chip->s[PLAYBACK]->active = 1;
			chip->s[PLAYBACK]->stream = SYNC_SUBSTREAM; /* not really used! */
			audio_process_dma(chip->s[PLAYBACK]);
		}
		/* want to playback and have capture - stop syncing */
		if(stream_id == PLAYBACK && chip->s[PLAYBACK]->sync) {
			chip->s[PLAYBACK]->sync = 0;
		}

		/* requested stream startup */
		chip->s[stream_id]->active = 1;
		audio_process_dma(chip->s[stream_id]);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/* want to stop capture and use syncing - stop DMA syncing */
		if (stream_id == CAPTURE && chip->s[PLAYBACK]->sync) {
			/* we do not need synchronization DMA transfer now */
			DEBUG(KERN_DEBUG "stopping synchronization DMA transfer\n");
			chip->s[PLAYBACK]->sync = 0;
			chip->s[PLAYBACK]->active = 0;
			audio_stop_dma(chip->s[PLAYBACK]);
		}
		/* want to stop playback and have capture - run DMA syncing */
		if(stream_id == PLAYBACK && chip->s[CAPTURE]->active) {
			/* we need synchronization DMA transfer (zeros) */
			DEBUG(KERN_DEBUG "starting synchronization DMA transfer\n");
			chip->s[PLAYBACK]->sync = 1;
			chip->s[PLAYBACK]->active = 1;
			chip->s[PLAYBACK]->stream = SYNC_SUBSTREAM; /* not really used! */
			audio_process_dma(chip->s[PLAYBACK]);
		}

		/* requested stream shutdown */
		chip->s[stream_id]->active = 0;
		audio_stop_dma(chip->s[stream_id]);
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
}

/* }}} */

static snd_pcm_hardware_t snd_sa11xx_uda1341_capture =
{
	.info			= (SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.rates			= (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
				   SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 |\
				   SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
				   SNDRV_PCM_RATE_KNOT),
	.rate_min		= 8000,
	.rate_max		= 48000,
	.channels_min:		= 2,
	.channels_max:		= 2,
	.buffer_bytes_max:	= 16380,
	.period_bytes_min:	= 64,
	.period_bytes_max:	= 8190, /* <= MAX_DMA_SIZE from ams/arch-sa1100/dma.h */
	.periods_min		= 2,
	.periods_max		= 255,
	.fifo_size		= 0,
};

static snd_pcm_hardware_t snd_sa11xx_uda1341_playback =
{
	.info			= (SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.rates			= (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
                                   SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 |\
				   SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
				   SNDRV_PCM_RATE_KNOT),
	.rate_min		= 8000,
	.rate_max		= 48000,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= 16380,
	.period_bytes_min	= 64,
	.period_bytes_max	= 8190, /* <= MAX_DMA_SIZE from ams/arch-sa1100/dma.h */
	.periods_min		= 2,
	.periods_max		= 255,
	.fifo_size		= 0,
};

/* {{{ snd_card_sa11xx_uda1341_playback functions */

static int snd_card_sa11xx_uda1341_playback_open(snd_pcm_substream_t * substream)
{
	sa11xx_uda1341_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;
        
	DEBUG_NAME(KERN_DEBUG "playback_open\n");
        
	chip->s[PLAYBACK]->stream = substream;
	chip->s[PLAYBACK]->sent_periods = 0;
	chip->s[PLAYBACK]->sent_total = 0;
        
	audio_reset(chip->s[PLAYBACK]);
 
	runtime->hw = snd_sa11xx_uda1341_playback;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	if ((err = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					      &hw_constraints_rates)) < 0)
		return err;
        
	return 0;
}

static int snd_card_sa11xx_uda1341_playback_close(snd_pcm_substream_t * substream)
{
	sa11xx_uda1341_t *chip = snd_pcm_substream_chip(substream);

	DEBUG_NAME(KERN_DEBUG "playback_close\n");

	chip->s[PLAYBACK]->stream = NULL;
      
	return 0;
}

static int snd_card_sa11xx_uda1341_playback_ioctl(snd_pcm_substream_t * substream,
				         unsigned int cmd, void *arg)
{
	DEBUG_NAME(KERN_DEBUG "playback_ioctl cmd: %d\n", cmd);
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_card_sa11xx_uda1341_playback_prepare(snd_pcm_substream_t * substream)
{
	sa11xx_uda1341_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
        
	DEBUG_NAME(KERN_DEBUG "playback_prepare\n");
                
	/* set requested samplerate */
	sa11xx_uda1341_set_samplerate(chip, runtime->rate);
        
	return 0;
}

static int snd_card_sa11xx_uda1341_playback_trigger(snd_pcm_substream_t * substream, int cmd)
{
	DEBUG_NAME(KERN_DEBUG "playback_trigger\n");
	return snd_card_sa11xx_uda1341_pcm_trigger(PLAYBACK, substream, cmd);
}

static snd_pcm_uframes_t snd_card_sa11xx_uda1341_playback_pointer(snd_pcm_substream_t * substream)
{
	sa11xx_uda1341_t *chip = snd_pcm_substream_chip(substream);

	DEBUG_NAME(KERN_DEBUG "playback_pointer\n");        
	return audio_get_dma_pos(chip->s[PLAYBACK]);
}

/* }}} */

/* {{{ snd_card_sa11xx_uda1341_record functions */

static int snd_card_sa11xx_uda1341_capture_open(snd_pcm_substream_t * substream)
{
	sa11xx_uda1341_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	DEBUG_NAME(KERN_DEBUG "record_open\n");

	chip->s[CAPTURE]->stream = substream;
	chip->s[CAPTURE]->sent_periods = 0;
	chip->s[CAPTURE]->sent_total = 0;
        
	audio_reset(chip->s[PLAYBACK]);        

	runtime->hw = snd_sa11xx_uda1341_capture;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	if ((err = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					      &hw_constraints_rates)) < 0)
		return err;
        
	return 0;
}

static int snd_card_sa11xx_uda1341_capture_close(snd_pcm_substream_t * substream)
{
	sa11xx_uda1341_t *chip = snd_pcm_substream_chip(substream);

	DEBUG_NAME(KERN_DEBUG "record_close\n");
        
	chip->s[CAPTURE]->stream = NULL;

	return 0;
}

static int snd_card_sa11xx_uda1341_capture_ioctl(snd_pcm_substream_t * substream,
					unsigned int cmd, void *arg)
{
	DEBUG_NAME(KERN_DEBUG "record_ioctl cmd: %d\n", cmd);
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_card_sa11xx_uda1341_capture_prepare(snd_pcm_substream_t * substream)
{
	sa11xx_uda1341_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
        
	DEBUG_NAME(KERN_DEBUG "record_prepare\n");

	/* set requested samplerate */
	sa11xx_uda1341_set_samplerate(chip, runtime->rate);
        
	return 0;
}

static int snd_card_sa11xx_uda1341_capture_trigger(snd_pcm_substream_t * substream, int cmd)
{
	DEBUG_NAME(KERN_DEBUG "record_trigger\n");
	return snd_card_sa11xx_uda1341_pcm_trigger(CAPTURE, substream, cmd);
}

static snd_pcm_uframes_t snd_card_sa11xx_uda1341_capture_pointer(snd_pcm_substream_t * substream)
{
	sa11xx_uda1341_t *chip = snd_pcm_substream_chip(substream);

	DEBUG_NAME(KERN_DEBUG "record_pointer\n");        
	return audio_get_dma_pos(chip->s[CAPTURE]);
}

/* }}} */

/* {{{ HW params & free */

static int snd_sa11xx_uda1341_hw_params(snd_pcm_substream_t * substream,
			       snd_pcm_hw_params_t * hw_params)
{
        
	DEBUG_NAME(KERN_DEBUG "hw_params\n");
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_sa11xx_uda1341_hw_free(snd_pcm_substream_t * substream)
{
	DEBUG_NAME(KERN_DEBUG "hw_free\n");        
	return snd_pcm_lib_free_pages(substream);
}

/* }}} */

static snd_pcm_ops_t snd_card_sa11xx_uda1341_playback_ops = {
	.open			= snd_card_sa11xx_uda1341_playback_open,
	.close			= snd_card_sa11xx_uda1341_playback_close,
	.ioctl			= snd_card_sa11xx_uda1341_playback_ioctl,
	.hw_params	        = snd_sa11xx_uda1341_hw_params,
	.hw_free	        = snd_sa11xx_uda1341_hw_free,
	.prepare		= snd_card_sa11xx_uda1341_playback_prepare,
	.trigger		= snd_card_sa11xx_uda1341_playback_trigger,
	.pointer		= snd_card_sa11xx_uda1341_playback_pointer,
};

static snd_pcm_ops_t snd_card_sa11xx_uda1341_capture_ops = {
	.open			= snd_card_sa11xx_uda1341_capture_open,
	.close			= snd_card_sa11xx_uda1341_capture_close,
	.ioctl			= snd_card_sa11xx_uda1341_capture_ioctl,
	.hw_params	        = snd_sa11xx_uda1341_hw_params,
	.hw_free	        = snd_sa11xx_uda1341_hw_free,
	.prepare		= snd_card_sa11xx_uda1341_capture_prepare,
	.trigger		= snd_card_sa11xx_uda1341_capture_trigger,
	.pointer		= snd_card_sa11xx_uda1341_capture_pointer,
};

static int __init snd_card_sa11xx_uda1341_pcm(sa11xx_uda1341_t *sa11xx_uda1341, int device, int substreams)
{
	snd_pcm_t *pcm;
	int err;

	DEBUG_NAME(KERN_DEBUG "sa11xx_uda1341_pcm\n");

	if ((err = snd_pcm_new(sa11xx_uda1341->card, "UDA1341 PCM", device,
			       substreams, substreams, &pcm)) < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_card_sa11xx_uda1341_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_card_sa11xx_uda1341_capture_ops);
	pcm->private_data = sa11xx_uda1341;
	pcm->info_flags = 0;
	strcpy(pcm->name, "UDA1341 PCM");

	sa11xx_uda1341->s[PLAYBACK] = snd_kcalloc(sizeof(audio_stream_t), GFP_KERNEL);
	sa11xx_uda1341->s[CAPTURE] = snd_kcalloc(sizeof(audio_stream_t), GFP_KERNEL);

	sa11xx_uda1341_audio_init(sa11xx_uda1341);

	/* setup DMA controller */
	audio_dma_request(sa11xx_uda1341->s[PLAYBACK], audio_dma_callback);
	audio_dma_request(sa11xx_uda1341->s[CAPTURE], audio_dma_callback);

	return 0;
}

/* }}} */

/* {{{ module init & exit */

#ifdef CONFIG_PM

static int sa11xx_uda1341_pm_callback(struct pm_dev *pm_dev, pm_request_t req, void *data)
{
	sa11xx_uda1341_t *sa11xx_uda1341 = pm_dev->data;
	audio_stream_t *is, *os;
	int stopstate;

	DEBUG_NAME(KERN_DEBUG "pm_callback\n");

	is = sa11xx_uda1341->s[PLAYBACK];
	os = sa11xx_uda1341->s[CAPTURE];
        
	switch (req) {
	case PM_SUSPEND: /* enter D1-D3 */
		if (is && is->dma_regs) {
			stopstate = is->active;
			audio_stop_dma(is);
			DMA_CLEAR(is);
			is->active = stopstate;
		}
		if (os && os->dma_regs) {
			stopstate = os->active;
			audio_stop_dma(os);
			DMA_CLEAR(os);
			os->active = stopstate;
		}
		if (is->stream || os->stream)
			sa11xx_uda1341_audio_shutdown(sa11xx_uda1341);
		break;
	case PM_RESUME:  /* enter D0 */
		if (is->stream || os->stream)
			sa11xx_uda1341_audio_init(sa11xx_uda1341);
		if (os && os->dma_regs) {
			DMA_RESET(os);
			audio_process_dma(os);
		}
		if (is && is->dma_regs) {
			DMA_RESET(is);
			audio_process_dma(is);
		}
		break;
	}
	return 0;
}

#endif

void snd_sa11xx_uda1341_free(snd_card_t *card)
{
	sa11xx_uda1341_t *chip = snd_magic_cast(sa11xx_uda1341_t, card->private_data, return);

	DEBUG_NAME(KERN_DEBUG "snd_sa11xx_uda1341_free\n");

	audio_dma_free(chip->s[PLAYBACK]);
	audio_dma_free(chip->s[CAPTURE]);

	kfree(chip->s[PLAYBACK]);
	kfree(chip->s[CAPTURE]);

	chip->s[PLAYBACK] = NULL;
	chip->s[CAPTURE] = NULL;

	snd_magic_kfree(chip);
	card->private_data = NULL;
}

static int __init sa11xx_uda1341_init(void)
{
	int err;
	snd_card_t *card;

	DEBUG_NAME(KERN_DEBUG "sa11xx_uda1341_uda1341_init\n");
        
	if (!machine_is_h3xxx())
		return -ENODEV;

	/* register the soundcard */
	card = snd_card_new(-1, snd_id, THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	sa11xx_uda1341 = snd_magic_kcalloc(sa11xx_uda1341_t, 0, GFP_KERNEL);
	if (sa11xx_uda1341 == NULL)
		return -ENOMEM;
         
	card->private_data = (void *)sa11xx_uda1341;
	card->private_free = snd_sa11xx_uda1341_free;
        
	sa11xx_uda1341->card = card;

	// mixer
	if ((err = snd_chip_uda1341_mixer_new(sa11xx_uda1341->card, &sa11xx_uda1341->uda1341)))
		goto nodev;

	// PCM
	if ((err = snd_card_sa11xx_uda1341_pcm(sa11xx_uda1341, 0, 2)) < 0)
		goto nodev;
        
       
#ifdef CONFIG_PM
	sa11xx_uda1341->pm_dev = pm_register(PM_SYS_DEV, 0, sa11xx_uda1341_pm_callback);
	if (sa11xx_uda1341->pm_dev)
		sa11xx_uda1341->pm_dev->data = sa11xx_uda1341;
#endif
        
	strcpy(card->driver, "UDA1341");
	strcpy(card->shortname, "H3600 UDA1341TS");
	sprintf(card->longname, "Compaq iPAQ H3600 with Philips UDA1341TS");
        
	if ((err = snd_card_register(card)) == 0) {
		printk( KERN_INFO "iPAQ audio support initialized\n" );
		return 0;
	}
        
 nodev:
	snd_card_free(card);
	return err;
}

static void __exit sa11xx_uda1341_exit(void)
{
	snd_chip_uda1341_mixer_del(sa11xx_uda1341->card);
	snd_card_free(sa11xx_uda1341->card);
	sa11xx_uda1341 = NULL;
}

module_init(sa11xx_uda1341_init);
module_exit(sa11xx_uda1341_exit);

/* }}} */

/*
 * Local variables:
 * indent-tabs-mode: t
 * End:
 */
