#define _PAS2_PCM_C_
/*
 * sound/pas2_pcm.c
 *
 * The low level driver for the Pro Audio Spectrum ADC/DAC.
 */
/*
 * Copyright by Hannu Savolainen 1993-1996
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <linux/config.h>


#include "sound_config.h"

#include "pas.h"

#if defined(CONFIG_PAS) && defined(CONFIG_AUDIO)

#define TRACE(WHAT)		/*
				   * * * (WHAT)   */

#define PAS_PCM_INTRBITS (0x08)
/*
 * Sample buffer timer interrupt enable
 */

#define PCM_NON	0
#define PCM_DAC	1
#define PCM_ADC	2

static unsigned long pcm_speed = 0;	/* sampling rate */
static unsigned char pcm_channels = 1;	/* channels (1 or 2) */
static unsigned char pcm_bits = 8;	/* bits/sample (8 or 16) */
static unsigned char pcm_filter = 0;	/* filter FLAG */
static unsigned char pcm_mode = PCM_NON;
static unsigned long pcm_count = 0;
static unsigned short pcm_bitsok = 8;	/* mask of OK bits */
static int      pcm_busy = 0;
static int      my_devnum = 0;
static int      open_mode = 0;

int
pcm_set_speed (int arg)
{
  int             foo, tmp;
  unsigned long   flags;

  if (arg > 44100)
    arg = 44100;
  if (arg < 5000)
    arg = 5000;

  if (pcm_channels & 2)
    {
      foo = (596590 + (arg / 2)) / arg;
      arg = 596590 / foo;
    }
  else
    {
      foo = (1193180 + (arg / 2)) / arg;
      arg = 1193180 / foo;
    }

  pcm_speed = arg;

  tmp = pas_read (FILTER_FREQUENCY);

  /*
     * Set anti-aliasing filters according to sample rate. You really *NEED*
     * to enable this feature for all normal recording unless you want to
     * experiment with aliasing effects.
     * These filters apply to the selected "recording" source.
     * I (pfw) don't know the encoding of these 5 bits. The values shown
     * come from the SDK found on ftp.uwp.edu:/pub/msdos/proaudio/.
   */
#if !defined NO_AUTO_FILTER_SET
  tmp &= 0xe0;
  if (pcm_speed >= 2 * 17897)
    tmp |= 0x21;
  else if (pcm_speed >= 2 * 15909)
    tmp |= 0x22;
  else if (pcm_speed >= 2 * 11931)
    tmp |= 0x29;
  else if (pcm_speed >= 2 * 8948)
    tmp |= 0x31;
  else if (pcm_speed >= 2 * 5965)
    tmp |= 0x39;
  else if (pcm_speed >= 2 * 2982)
    tmp |= 0x24;
  pcm_filter = tmp;
#endif

  save_flags (flags);
  cli ();

  pas_write (tmp & ~(F_F_PCM_RATE_COUNTER | F_F_PCM_BUFFER_COUNTER), FILTER_FREQUENCY);
  pas_write (S_C_C_SAMPLE_RATE | S_C_C_LSB_THEN_MSB | S_C_C_SQUARE_WAVE, SAMPLE_COUNTER_CONTROL);
  pas_write (foo & 0xff, SAMPLE_RATE_TIMER);
  pas_write ((foo >> 8) & 0xff, SAMPLE_RATE_TIMER);
  pas_write (tmp, FILTER_FREQUENCY);

  restore_flags (flags);

  return pcm_speed;
}

int
pcm_set_channels (int arg)
{

  if ((arg != 1) && (arg != 2))
    return pcm_channels;

  if (arg != pcm_channels)
    {
      pas_write (pas_read (PCM_CONTROL) ^ P_C_PCM_MONO, PCM_CONTROL);

      pcm_channels = arg;
      pcm_set_speed (pcm_speed);	/*
					   * The speed must be reinitialized
					 */
    }

  return pcm_channels;
}

int
pcm_set_bits (int arg)
{
  if ((arg & pcm_bitsok) != arg)
    return pcm_bits;

  if (arg != pcm_bits)
    {
      pas_write (pas_read (SYSTEM_CONFIGURATION_2) ^ S_C_2_PCM_16_BIT, SYSTEM_CONFIGURATION_2);

      pcm_bits = arg;
    }

  return pcm_bits;
}

static int
pas_pcm_ioctl (int dev, unsigned int cmd, caddr_t arg, int local)
{
  TRACE (printk ("pas2_pcm.c: static int pas_pcm_ioctl(unsigned int cmd = %X, unsigned int arg = %X)\n", cmd, arg));

  switch (cmd)
    {
    case SOUND_PCM_WRITE_RATE:
      if (local)
	return pcm_set_speed ((int) arg);
      return snd_ioctl_return ((int *) arg, pcm_set_speed (get_fs_long ((long *) arg)));
      break;

    case SOUND_PCM_READ_RATE:
      if (local)
	return pcm_speed;
      return snd_ioctl_return ((int *) arg, pcm_speed);
      break;

    case SNDCTL_DSP_STEREO:
      if (local)
	return pcm_set_channels ((int) arg + 1) - 1;
      return snd_ioctl_return ((int *) arg, pcm_set_channels (get_fs_long ((long *) arg) + 1) - 1);
      break;

    case SOUND_PCM_WRITE_CHANNELS:
      if (local)
	return pcm_set_channels ((int) arg);
      return snd_ioctl_return ((int *) arg, pcm_set_channels (get_fs_long ((long *) arg)));
      break;

    case SOUND_PCM_READ_CHANNELS:
      if (local)
	return pcm_channels;
      return snd_ioctl_return ((int *) arg, pcm_channels);
      break;

    case SNDCTL_DSP_SETFMT:
      if (local)
	return pcm_set_bits ((int) arg);
      return snd_ioctl_return ((int *) arg, pcm_set_bits (get_fs_long ((long *) arg)));
      break;

    case SOUND_PCM_READ_BITS:
      if (local)
	return pcm_bits;
      return snd_ioctl_return ((int *) arg, pcm_bits);

    case SOUND_PCM_WRITE_FILTER:	/*
					 * NOT YET IMPLEMENTED
					 */
      if (get_fs_long ((long *) arg) > 1)
	return -EINVAL;
      pcm_filter = get_fs_long ((long *) arg);
      break;

    case SOUND_PCM_READ_FILTER:
      return snd_ioctl_return ((int *) arg, pcm_filter);
      break;

    default:
      return -EINVAL;
    }

  return -EINVAL;
}

static void
pas_pcm_reset (int dev)
{
  TRACE (printk ("pas2_pcm.c: static void pas_pcm_reset(void)\n"));

  pas_write (pas_read (PCM_CONTROL) & ~P_C_PCM_ENABLE, PCM_CONTROL);
}

static int
pas_pcm_open (int dev, int mode)
{
  int             err;
  unsigned long   flags;

  TRACE (printk ("pas2_pcm.c: static int pas_pcm_open(int mode = %X)\n", mode));

  save_flags (flags);
  cli ();
  if (pcm_busy)
    {
      restore_flags (flags);
      return -EBUSY;
    }

  pcm_busy = 1;
  restore_flags (flags);

  if ((err = pas_set_intr (PAS_PCM_INTRBITS)) < 0)
    return err;


  pcm_count = 0;
  open_mode = mode;

  return 0;
}

static void
pas_pcm_close (int dev)
{
  unsigned long   flags;

  TRACE (printk ("pas2_pcm.c: static void pas_pcm_close(void)\n"));

  save_flags (flags);
  cli ();

  pas_pcm_reset (dev);
  pas_remove_intr (PAS_PCM_INTRBITS);
  pcm_mode = PCM_NON;

  pcm_busy = 0;
  restore_flags (flags);
}

static void
pas_pcm_output_block (int dev, unsigned long buf, int count,
		      int intrflag, int restart_dma)
{
  unsigned long   flags, cnt;

  TRACE (printk ("pas2_pcm.c: static void pas_pcm_output_block(char *buf = %P, int count = %X)\n", buf, count));

  cnt = count;
  if (audio_devs[dev]->dmachan1 > 3)
    cnt >>= 1;

  if (audio_devs[dev]->flags & DMA_AUTOMODE &&
      intrflag &&
      cnt == pcm_count)
    return;			/*
				 * Auto mode on. No need to react
				 */

  save_flags (flags);
  cli ();

  pas_write (pas_read (PCM_CONTROL) & ~P_C_PCM_ENABLE,
	     PCM_CONTROL);

  if (restart_dma)
    DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE);

  if (audio_devs[dev]->dmachan1 > 3)
    count >>= 1;

  if (count != pcm_count)
    {
      pas_write (pas_read (FILTER_FREQUENCY) & ~F_F_PCM_BUFFER_COUNTER, FILTER_FREQUENCY);
      pas_write (S_C_C_SAMPLE_BUFFER | S_C_C_LSB_THEN_MSB | S_C_C_SQUARE_WAVE, SAMPLE_COUNTER_CONTROL);
      pas_write (count & 0xff, SAMPLE_BUFFER_COUNTER);
      pas_write ((count >> 8) & 0xff, SAMPLE_BUFFER_COUNTER);
      pas_write (pas_read (FILTER_FREQUENCY) | F_F_PCM_BUFFER_COUNTER, FILTER_FREQUENCY);

      pcm_count = count;
    }
  pas_write (pas_read (FILTER_FREQUENCY) | F_F_PCM_BUFFER_COUNTER | F_F_PCM_RATE_COUNTER, FILTER_FREQUENCY);
#ifdef NO_TRIGGER
  pas_write (pas_read (PCM_CONTROL) | P_C_PCM_ENABLE | P_C_PCM_DAC_MODE, PCM_CONTROL);
#endif

  pcm_mode = PCM_DAC;

  restore_flags (flags);
}

static void
pas_pcm_start_input (int dev, unsigned long buf, int count,
		     int intrflag, int restart_dma)
{
  unsigned long   flags;
  int             cnt;

  TRACE (printk ("pas2_pcm.c: static void pas_pcm_start_input(char *buf = %P, int count = %X)\n", buf, count));

  cnt = count;
  if (audio_devs[dev]->dmachan1 > 3)
    cnt >>= 1;

  if (audio_devs[my_devnum]->flags & DMA_AUTOMODE &&
      intrflag &&
      cnt == pcm_count)
    return;			/*
				 * Auto mode on. No need to react
				 */

  save_flags (flags);
  cli ();

  if (restart_dma)
    DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ);

  if (audio_devs[dev]->dmachan1 > 3)
    count >>= 1;

  if (count != pcm_count)
    {
      pas_write (pas_read (FILTER_FREQUENCY) & ~F_F_PCM_BUFFER_COUNTER, FILTER_FREQUENCY);
      pas_write (S_C_C_SAMPLE_BUFFER | S_C_C_LSB_THEN_MSB | S_C_C_SQUARE_WAVE, SAMPLE_COUNTER_CONTROL);
      pas_write (count & 0xff, SAMPLE_BUFFER_COUNTER);
      pas_write ((count >> 8) & 0xff, SAMPLE_BUFFER_COUNTER);
      pas_write (pas_read (FILTER_FREQUENCY) | F_F_PCM_BUFFER_COUNTER, FILTER_FREQUENCY);

      pcm_count = count;
    }
  pas_write (pas_read (FILTER_FREQUENCY) | F_F_PCM_BUFFER_COUNTER | F_F_PCM_RATE_COUNTER, FILTER_FREQUENCY);
#ifdef NO_TRIGGER
  pas_write ((pas_read (PCM_CONTROL) | P_C_PCM_ENABLE) & ~P_C_PCM_DAC_MODE, PCM_CONTROL);
#endif

  pcm_mode = PCM_ADC;

  restore_flags (flags);
}

#ifndef NO_TRIGGER
static void
pas_pcm_trigger (int dev, int state)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();
  state &= open_mode;

  if (state & PCM_ENABLE_OUTPUT)
    pas_write (pas_read (PCM_CONTROL) | P_C_PCM_ENABLE | P_C_PCM_DAC_MODE, PCM_CONTROL);
  else if (state & PCM_ENABLE_INPUT)
    pas_write ((pas_read (PCM_CONTROL) | P_C_PCM_ENABLE) & ~P_C_PCM_DAC_MODE, PCM_CONTROL);
  else
    pas_write (pas_read (PCM_CONTROL) & ~P_C_PCM_ENABLE, PCM_CONTROL);

  restore_flags (flags);
}
#endif

static int
pas_pcm_prepare_for_input (int dev, int bsize, int bcount)
{
  return 0;
}

static int
pas_pcm_prepare_for_output (int dev, int bsize, int bcount)
{
  return 0;
}

static struct audio_operations pas_pcm_operations =
{
  "Pro Audio Spectrum",
  DMA_AUTOMODE,
  AFMT_U8 | AFMT_S16_LE,
  NULL,
  pas_pcm_open,
  pas_pcm_close,
  pas_pcm_output_block,
  pas_pcm_start_input,
  pas_pcm_ioctl,
  pas_pcm_prepare_for_input,
  pas_pcm_prepare_for_output,
  pas_pcm_reset,
  pas_pcm_reset,
  NULL,
  NULL,
  NULL,
  NULL,
#ifndef NO_TRIGGER
  pas_pcm_trigger
#else
  NULL
#endif
};

long
pas_pcm_init (long mem_start, struct address_info *hw_config)
{
  TRACE (printk ("pas2_pcm.c: long pas_pcm_init(long mem_start = %X)\n", mem_start));

  pcm_bitsok = 8;
  if (pas_read (OPERATION_MODE_1) & O_M_1_PCM_TYPE)
    pcm_bitsok |= 16;

  pcm_set_speed (DSP_DEFAULT_SPEED);

  if (num_audiodevs < MAX_AUDIO_DEV)
    {
      audio_devs[my_devnum = num_audiodevs++] = &pas_pcm_operations;
      audio_devs[my_devnum]->dmachan1 = hw_config->dma;
      audio_devs[my_devnum]->buffsize = DSP_BUFFSIZE;
    }
  else
    printk ("PAS2: Too many PCM devices available\n");

  return mem_start;
}

void
pas_pcm_interrupt (unsigned char status, int cause)
{
  if (cause == 1)		/*
				 * PCM buffer done
				 */
    {
      /*
       * Halt the PCM first. Otherwise we don't have time to start a new
       * block before the PCM chip proceeds to the next sample
       */

      if (!(audio_devs[my_devnum]->flags & DMA_AUTOMODE))
	{
	  pas_write (pas_read (PCM_CONTROL) & ~P_C_PCM_ENABLE,
		     PCM_CONTROL);
	}

      switch (pcm_mode)
	{

	case PCM_DAC:
	  DMAbuf_outputintr (my_devnum, 1);
	  break;

	case PCM_ADC:
	  DMAbuf_inputintr (my_devnum);
	  break;

	default:
	  printk ("PAS: Unexpected PCM interrupt\n");
	}
    }
}

#endif
