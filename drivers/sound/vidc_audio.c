/*
 * drivers/sound/vidc_audio.c
 *
 * Audio routines for the VIDC
 *
 * Copyright (C) 1997 Russell King <rmk@arm.uk.linux.org>
 */

#include <asm/hardware.h>
#include <asm/io.h>
#include "sound_config.h"
#include "vidc.h"

/*
 * VIDC sound
 *
 * When using SERIAL SOUND mode (external DAC), the number of physical
 * channels is fixed at 2.  Therefore, the sample rate = vidc sample rate.
 */

static int      vidc_adev;

static int      vidc_audio_volume;
static int      vidc_audio_rate;
static char     vidc_audio_bits;
static char     vidc_audio_channels;

extern void     vidc_update_filler(int bits, int channels);

int  vidc_audio_get_volume(void)
{
	return vidc_audio_volume;
}

int vidc_audio_set_volume(int newvol)
{
	vidc_audio_volume = newvol;
	return vidc_audio_volume;
}

static int vidc_audio_set_bits(int bits)
{
	switch (bits)
	{
		case AFMT_QUERY:
			break;
		case AFMT_U8:
		case AFMT_S16_LE:
			vidc_audio_bits = bits;
			vidc_update_filler(vidc_audio_bits, vidc_audio_channels);
			break;
		default:
			vidc_audio_bits = 16;
			vidc_update_filler(vidc_audio_bits, vidc_audio_channels);
			break;
	}
	return vidc_audio_bits;
}

static int vidc_audio_set_rate(int rate)
{
	if (rate)
	{
		int newsize, new2size;

		vidc_audio_rate = ((500000 / rate) + 1) >> 1;
		if (vidc_audio_rate < 3)
			vidc_audio_rate = 3;
		if (vidc_audio_rate > 255)
			vidc_audio_rate = 255;
		outl((vidc_audio_rate - 2) | 0xb0000000, VIDC_BASE);
		outl(0xb1000003, VIDC_BASE);
		newsize = (10000 / vidc_audio_rate) & ~3;
		if (newsize < 208)
			newsize = 208;
		if (newsize > 4096)
			newsize = 4096;
		for (new2size = 128; new2size < newsize; new2size <<= 1);
			if (new2size - newsize > newsize - (new2size >> 1))
				new2size >>= 1;
		dma_bufsize = new2size;
	}
	return 250000 / vidc_audio_rate;
}

static int vidc_audio_set_channels(int channels)
{
	switch (channels)
	{
		case 0:
			break;
		case 1:
		case 2:
			vidc_audio_channels = channels;
			vidc_update_filler(vidc_audio_bits, vidc_audio_channels);
			break;
		default:
			vidc_audio_channels = 2;
			vidc_update_filler(vidc_audio_bits, vidc_audio_channels);
			break;
	}
	return vidc_audio_channels;
}

/*
 * Open the device
 *
 * dev  - device
 * mode - mode to open device (logical OR of OPEN_READ and OPEN_WRITE)
 *
 * Called when opening the DMAbuf               (dmabuf.c:259)
 */

static int vidc_audio_open(int dev, int mode)
{
	if (vidc_busy)
		return -EBUSY;

	if ((mode & OPEN_READ) && (!mode & OPEN_WRITE))
	{
		/* This audio device doesn't have recording capability */
		return -EIO;
	}
	vidc_busy = 1;
	return 0;
}

/*
 * Close the device
 *
 * dev  - device
 *
 * Called when closing the DMAbuf               (dmabuf.c:477)
 *      after halt_xfer
 */

static void vidc_audio_close(int dev)
{
	vidc_busy = 0;
}

static int vidc_audio_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	int ret;

	switch (cmd)
	{
		case SOUND_PCM_WRITE_RATE:
			if (get_user(ret, (int *) arg))
				return -EFAULT;
			ret = vidc_audio_set_rate(ret);
			break;

		case SOUND_PCM_READ_RATE:
			ret = vidc_audio_set_rate(0);
			break;

		case SNDCTL_DSP_STEREO:
			if (get_user(ret, (int *) arg))
				return -EFAULT;
			ret = vidc_audio_set_channels(ret + 1) - 1;
			break;

		case SOUND_PCM_WRITE_CHANNELS:
			if (get_user(ret, (int *) arg))
				return -EFAULT;
			ret = vidc_audio_set_channels(ret);
			break;

		case SOUND_PCM_READ_CHANNELS:
			ret = vidc_audio_set_channels(0);
			break;

		case SNDCTL_DSP_SETFMT:
			if (get_user(ret, (int *) arg))
				return -EFAULT;
			ret = vidc_audio_set_bits(ret);
			break;

		case SOUND_PCM_READ_BITS:
			ret = vidc_audio_set_bits(0);
			break;

		case SOUND_PCM_WRITE_FILTER:
		case SOUND_PCM_READ_FILTER:
			return -EINVAL;

		default:
			return -EINVAL;
	}
	return put_user(ret, (int *) arg);
}

/*
 * Output a block via DMA to sound device
 *
 * dev          - device number
 * buf          - physical address of buffer
 * total_count  - total byte count in buffer
 * intrflag     - set if this has been called from an interrupt (via DMAbuf_outputintr)
 * restart_dma  - set if DMA needs to be re-initialised
 *
 * Called when:
 *  1. Starting output                                  (dmabuf.c:1327)
 *  2.                                                  (dmabuf.c:1504)
 *  3. A new buffer needs to be sent to the device      (dmabuf.c:1579)
 */
 
static void vidc_audio_dma_interrupt(void)
{
	DMAbuf_outputintr(vidc_adev, 1);
}

static void vidc_audio_output_block(int dev, unsigned long buf, int total_count,
			int intrflag)
{
	dma_start = buf;
	dma_count = total_count;

	if (!intrflag)
	{
		dma_interrupt = vidc_audio_dma_interrupt;
		vidc_sound_dma_irq(0, NULL, NULL);
		outb(DMA_CR_D | DMA_CR_E, IOMD_SD0CR);
	}
}

static void vidc_audio_start_input(int dev, unsigned long buf, int count,
		       int intrflag)
{
}

static int vidc_audio_prepare_for_input(int dev, int bsize, int bcount)
{
	return -EINVAL;
}

/*
 * Prepare for outputting samples to `dev'
 *
 * Each buffer that will be passed will be `bsize' bytes long,
 * with a total of `bcount' buffers.
 *
 * Called when:
 *  1. A trigger enables audio output                   (dmabuf.c:978)
 *  2. We get a write buffer without dma_mode setup     (dmabuf.c:1152)
 *  3. We restart a transfer                            (dmabuf.c:1324)
 */

static int vidc_audio_prepare_for_output(int dev, int bsize, int bcount)
{
	return 0;
}

static void vidc_audio_reset(int dev)
{
}

/*
 * Halt a DMA transfer to `dev'
 *
 * Called when:
 *  1. We close the DMAbuf                                      (dmabuf.c:476)
 *  2. We run out of output buffers to output to the device.    (dmabuf.c:1456)
 *  3. We run out of output buffers and we're closing down.     (dmabuf.c:1546)
 *  4. We run out of input buffers in AUTOMODE.                 (dmabuf.c:1651)
 */
 
static void vidc_audio_halt_xfer(int dev)
{
	dma_count = 0;
}

static int vidc_audio_local_qlen(int dev)
{
	return dma_count != 0;
}

static struct audio_driver vidc_audio_driver =
{
	vidc_audio_open,		/* open                 */
	vidc_audio_close,		/* close                */
	vidc_audio_output_block,	/* output_block         */
	vidc_audio_start_input,		/* start_input          */
	vidc_audio_ioctl,		/* ioctl                */
	vidc_audio_prepare_for_input,	/* prepare_for_input    */
	vidc_audio_prepare_for_output,	/* prepare_for_output   */
	vidc_audio_reset,		/* reset                */
	vidc_audio_halt_xfer,		/* halt_xfer            */
	vidc_audio_local_qlen,		/*+local_qlen           */
	NULL,				/*+copy_from_user       */
	NULL,				/*+halt_input           */
	NULL,				/*+halt_output          */
	NULL,				/*+trigger              */
	NULL,				/*+set_speed            */
	NULL,				/*+set_bits             */
	NULL,				/*+set_channels         */
};

static struct audio_operations vidc_audio_operations =
{
	"VIDCsound",
	0,
	AFMT_U8 | AFMT_S16_LE,
	NULL,
	&vidc_audio_driver
};

void vidc_audio_init(struct address_info *hw_config)
{
	vidc_audio_volume = 100 | (100 << 8);
	if ((vidc_adev = sound_alloc_audiodev())!=-1)
	{
		audio_devs[vidc_adev] = &vidc_audio_operations;
		audio_devs[vidc_adev]->min_fragment = 10;	/* 1024 bytes => 64 buffers */
		audio_devs[vidc_adev]->mixer_dev = num_mixers;
		audio_devs[vidc_adev]->flags |= 0;
	}
	else printk(KERN_ERR "VIDCsound: Too many PCM devices available\n");
}
