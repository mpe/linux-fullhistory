/*
 * sound/sb_audio.c
 *
 * Audio routines for SoundBlaster compatible cards.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>


#include "sound_config.h"

#if defined(CONFIG_SBDSP)

#include "sb_mixer.h"
#include "sb.h"

static int
sb_audio_open (int dev, int mode)
{
  sb_devc        *devc = audio_devs[dev]->devc;
  unsigned long   flags;

  if (devc == NULL)
    {
      printk ("SB: Incomplete initialization\n");
      return -(ENXIO);
    }

  if (devc->caps & SB_NO_RECORDING && mode & OPEN_READ)
    {
      printk ("SB: Recording is not possible with this device\n");
      return -(EPERM);
    }

  save_flags (flags);
  cli ();
  if (devc->opened)
    {
      restore_flags (flags);
      return -(EBUSY);
    }

  if (devc->dma16 != -1 && devc->dma16 != devc->dma8)
    {
      if (sound_open_dma (devc->dma16, "Sound Blaster 16 bit"))
	{
	  return -(EBUSY);
	}
    }
  devc->opened = mode;
  restore_flags (flags);

  devc->irq_mode = IMODE_NONE;
  sb_dsp_reset (devc);

  return 0;
}

static void
sb_audio_close (int dev)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  audio_devs[dev]->dmachan1 =
    audio_devs[dev]->dmachan2 =
    devc->dma8;

  if (devc->dma16 != -1 && devc->dma16 != devc->dma8)
    sound_close_dma (devc->dma16);

  devc->opened = 0;
}

static void
sb_set_output_parms (int dev, unsigned long buf, int nr_bytes,
		     int intrflag, int restart_dma)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  devc->trg_buf = buf;
  devc->trg_bytes = nr_bytes;
  devc->trg_intrflag = intrflag;
  devc->trg_restart = restart_dma;
  devc->irq_mode = IMODE_OUTPUT;
}

static void
sb_set_input_parms (int dev, unsigned long buf, int count, int intrflag,
		    int restart_dma)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  devc->trg_buf = buf;
  devc->trg_bytes = count;
  devc->trg_intrflag = intrflag;
  devc->trg_restart = restart_dma;
  devc->irq_mode = IMODE_INPUT;
}

/*
 * SB1.x compatible routines 
 */

static void
sb1_audio_output_block (int dev, unsigned long buf, int nr_bytes,
			int intrflag, int restart_dma)
{
  unsigned long   flags;
  int             count = nr_bytes;
  sb_devc        *devc = audio_devs[dev]->devc;

  DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE);

  if (audio_devs[dev]->dmachan1 > 3)
    count >>= 1;
  count--;

  devc->irq_mode = IMODE_OUTPUT;

  save_flags (flags);
  cli ();
  if (sb_dsp_command (devc, 0x14))	/* 8 bit DAC using DMA */
    {
      sb_dsp_command (devc, (unsigned char) (count & 0xff));
      sb_dsp_command (devc, (unsigned char) ((count >> 8) & 0xff));
    }
  else
    printk ("SB: Unable to start DAC\n");
  restore_flags (flags);
  devc->intr_active = 1;
}

static void
sb1_audio_start_input (int dev, unsigned long buf, int nr_bytes, int intrflag,
		       int restart_dma)
{
  unsigned long   flags;
  int             count = nr_bytes;
  sb_devc        *devc = audio_devs[dev]->devc;

  /*
   * Start a DMA input to the buffer pointed by dmaqtail
   */

  DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ);

  if (audio_devs[dev]->dmachan1 > 3)
    count >>= 1;
  count--;

  devc->irq_mode = IMODE_INPUT;

  save_flags (flags);
  cli ();
  if (sb_dsp_command (devc, 0x24))	/* 8 bit ADC using DMA */
    {
      sb_dsp_command (devc, (unsigned char) (count & 0xff));
      sb_dsp_command (devc, (unsigned char) ((count >> 8) & 0xff));
    }
  else
    printk ("SB Error: Unable to start ADC\n");
  restore_flags (flags);

  devc->intr_active = 1;
}

static void
sb1_audio_trigger (int dev, int bits)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  bits &= devc->irq_mode;

  if (!bits)
    sb_dsp_command (devc, 0xd0);	/* Halt DMA */
  else
    {
      switch (devc->irq_mode)
	{
	case IMODE_INPUT:
	  sb1_audio_start_input (dev, devc->trg_buf, devc->trg_bytes,
				 devc->trg_intrflag, devc->trg_restart);
	  break;

	case IMODE_OUTPUT:
	  sb1_audio_output_block (dev, devc->trg_buf, devc->trg_bytes,
				  devc->trg_intrflag, devc->trg_restart);
	  break;
	}
    }

  devc->trigger_bits = bits;
}

static int
sb1_audio_prepare_for_input (int dev, int bsize, int bcount)
{
  sb_devc        *devc = audio_devs[dev]->devc;
  unsigned long   flags;

  save_flags (flags);
  cli ();
  if (sb_dsp_command (devc, 0x40))
    sb_dsp_command (devc, devc->tconst);
  sb_dsp_command (devc, DSP_CMD_SPKOFF);
  restore_flags (flags);

  devc->trigger_bits = 0;
  return 0;
}

static int
sb1_audio_prepare_for_output (int dev, int bsize, int bcount)
{
  sb_devc        *devc = audio_devs[dev]->devc;
  unsigned long   flags;

  save_flags (flags);
  cli ();
  if (sb_dsp_command (devc, 0x40))
    sb_dsp_command (devc, devc->tconst);
  sb_dsp_command (devc, DSP_CMD_SPKON);
  restore_flags (flags);
  devc->trigger_bits = 0;
  return 0;
}

static int
sb1_audio_set_speed (int dev, int speed)
{
  int             max_speed = 23000;
  sb_devc        *devc = audio_devs[dev]->devc;
  int             tmp;

  if (devc->opened & OPEN_READ)
    max_speed = 13000;

  if (speed > 0)
    {
      if (speed < 4000)
	speed = 4000;

      if (speed > max_speed)
	speed = max_speed;

      devc->tconst = (256 - ((1000000 + speed / 2) / speed)) & 0xff;

      tmp = 256 - devc->tconst;
      speed = (1000000 + tmp / 2) / tmp;

      devc->speed = speed;
    }

  return devc->speed;
}

static short
sb1_audio_set_channels (int dev, short channels)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  return devc->channels = 1;
}

static unsigned int
sb1_audio_set_bits (int dev, unsigned int bits)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  return devc->bits = 8;
}

static void
sb1_audio_halt_xfer (int dev)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  sb_dsp_reset (devc);
}

/*
 * SB 2.0 and SB 2.01 compatible routines
 */

static void
sb20_audio_output_block (int dev, unsigned long buf, int nr_bytes,
			 int intrflag, int restart_dma)
{
  unsigned long   flags;
  int             count = nr_bytes;
  sb_devc        *devc = audio_devs[dev]->devc;
  unsigned char   cmd;

  DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE);

  if (audio_devs[dev]->dmachan1 > 3)
    count >>= 1;
  count--;

  devc->irq_mode = IMODE_OUTPUT;

  save_flags (flags);
  cli ();
  if (sb_dsp_command (devc, 0x48))	/* DSP Block size */
    {
      sb_dsp_command (devc, (unsigned char) (count & 0xff));
      sb_dsp_command (devc, (unsigned char) ((count >> 8) & 0xff));

      if (devc->speed * devc->channels <= 23000)
	cmd = 0x1c;		/* 8 bit PCM output */
      else
	cmd = 0x90;		/* 8 bit high speed PCM output (SB2.01/Pro) */

      if (!sb_dsp_command (devc, cmd))
	printk ("SB: Unable to start DAC\n");

    }
  else
    printk ("SB: Unable to start DAC\n");
  restore_flags (flags);
  devc->intr_active = 1;
}

static void
sb20_audio_start_input (int dev, unsigned long buf, int nr_bytes, int intrflag,
			int restart_dma)
{
  unsigned long   flags;
  int             count = nr_bytes;
  sb_devc        *devc = audio_devs[dev]->devc;
  unsigned char   cmd;

  /*
   * Start a DMA input to the buffer pointed by dmaqtail
   */

  DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ);

  if (audio_devs[dev]->dmachan1 > 3)
    count >>= 1;
  count--;

  devc->irq_mode = IMODE_INPUT;

  save_flags (flags);
  cli ();
  if (sb_dsp_command (devc, 0x48))	/* DSP Block size */
    {
      sb_dsp_command (devc, (unsigned char) (count & 0xff));
      sb_dsp_command (devc, (unsigned char) ((count >> 8) & 0xff));

      if (devc->speed * devc->channels <= (devc->major == 3 ? 23000 : 13000))
	cmd = 0x2c;		/* 8 bit PCM input */
      else
	cmd = 0x98;		/* 8 bit high speed PCM input (SB2.01/Pro) */

      if (!sb_dsp_command (devc, cmd))
	printk ("SB: Unable to start ADC\n");
    }
  else
    printk ("SB Error: Unable to start ADC\n");
  restore_flags (flags);

  devc->intr_active = 1;
}

static void
sb20_audio_trigger (int dev, int bits)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  bits &= devc->irq_mode;

  if (!bits)
    sb_dsp_command (devc, 0xd0);	/* Halt DMA */
  else
    {
      switch (devc->irq_mode)
	{
	case IMODE_INPUT:
	  sb20_audio_start_input (dev, devc->trg_buf, devc->trg_bytes,
				  devc->trg_intrflag, devc->trg_restart);
	  break;

	case IMODE_OUTPUT:
	  sb20_audio_output_block (dev, devc->trg_buf, devc->trg_bytes,
				   devc->trg_intrflag, devc->trg_restart);
	  break;
	}
    }

  devc->trigger_bits = bits;
}

/*
 * SB2.01 spesific speed setup
 */

static int
sb201_audio_set_speed (int dev, int speed)
{
  sb_devc        *devc = audio_devs[dev]->devc;
  int             tmp;
  int             s = speed * devc->channels;

  if (speed > 0)
    {
      if (speed < 4000)
	speed = 4000;

      if (speed > 44100)
	speed = 44100;

      if (devc->opened & OPEN_READ && speed > 15000)
	speed = 15000;

      devc->tconst = ((65536 - ((256000000 + s / 2) /
				s)) >> 8) & 0xff;

      tmp = 256 - devc->tconst;
      speed = ((1000000 + tmp / 2) / tmp) / devc->channels;

      devc->speed = speed;
    }

  return devc->speed;
}

/*
 * SB Pro spesific routines
 */

static int
sbpro_audio_prepare_for_input (int dev, int bsize, int bcount)
{				/* For SB Pro and Jazz16 */
  sb_devc        *devc = audio_devs[dev]->devc;
  unsigned long   flags;
  unsigned char   bits = 0;

  if (devc->dma16 >= 0 && devc->dma16 != devc->dma8)
    audio_devs[dev]->dmachan1 =
      audio_devs[dev]->dmachan2 =
      devc->bits == 16 ? devc->dma16 : devc->dma8;

  if (devc->model == MDL_JAZZ || devc->model == MDL_SMW)
    if (devc->bits == AFMT_S16_LE)
      bits = 0x04;		/* 16 bit mode */

  save_flags (flags);
  cli ();
  if (sb_dsp_command (devc, 0x40))
    sb_dsp_command (devc, devc->tconst);
  sb_dsp_command (devc, DSP_CMD_SPKOFF);
  if (devc->channels == 1)
    sb_dsp_command (devc, 0xa0 | bits);		/* Mono input */
  else
    sb_dsp_command (devc, 0xa8 | bits);		/* Stereo input */
  restore_flags (flags);

  devc->trigger_bits = 0;
  return 0;
}

static int
sbpro_audio_prepare_for_output (int dev, int bsize, int bcount)
{				/* For SB Pro and Jazz16 */
  sb_devc        *devc = audio_devs[dev]->devc;
  unsigned long   flags;
  unsigned char   tmp;
  unsigned char   bits = 0;

  if (devc->dma16 >= 0 && devc->dma16 != devc->dma8)
    audio_devs[dev]->dmachan1 =
      audio_devs[dev]->dmachan2 =
      devc->bits == 16 ? devc->dma16 : devc->dma8;

  save_flags (flags);
  cli ();
  if (sb_dsp_command (devc, 0x40))
    sb_dsp_command (devc, devc->tconst);
  sb_dsp_command (devc, DSP_CMD_SPKON);

  if (devc->model == MDL_JAZZ || devc->model == MDL_SMW)
    {
      if (devc->bits == AFMT_S16_LE)
	bits = 0x04;		/* 16 bit mode */

      if (devc->channels == 1)
	sb_dsp_command (devc, 0xa0 | bits);	/* Mono output */
      else
	sb_dsp_command (devc, 0xa8 | bits);	/* Stereo output */
    }
  else
    {
      tmp = sb_getmixer (devc, 0x0e);
      if (devc->channels == 1)
	tmp &= ~0x20;
      else
	tmp |= 0x20;
      sb_setmixer (devc, 0x0e, tmp);
    }
  restore_flags (flags);
  devc->trigger_bits = 0;
  return 0;
}

static int
sbpro_audio_set_speed (int dev, int speed)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  if (speed > 0)
    {
      if (speed < 4000)
	speed = 4000;

      if (speed > 44100)
	speed = 44100;

      if (devc->channels > 1 && speed > 22050)
	speed = 22050;

      sb201_audio_set_speed (dev, speed);
    }

  return devc->speed;
}

static short
sbpro_audio_set_channels (int dev, short channels)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  if (channels == 1 || channels == 2)
    if (channels != devc->channels)
      {
	devc->channels = channels;
	sbpro_audio_set_speed (dev, devc->speed);
      }
  return devc->channels;
}

static int
jazz16_audio_set_speed (int dev, int speed)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  if (speed > 0)
    {
      int             tmp;
      int             s = speed * devc->channels;

      if (speed < 5000)
	speed = 4000;

      if (speed > 44100)
	speed = 44100;

      devc->tconst = ((65536 - ((256000000 + s / 2) /
				s)) >> 8) & 0xff;

      tmp = 256 - devc->tconst;
      speed = ((1000000 + tmp / 2) / tmp) / devc->channels;

      devc->speed = speed;
    }

  return devc->speed;
}

/*
 * ESS spesific routines
 */

static void
ess_speed (sb_devc * devc)
{
  int             divider;
  unsigned char   bits = 0;
  int             speed = devc->speed;

  if (speed < 4000)
    speed = 4000;
  else if (speed > 48000)
    speed = 48000;

  if (speed > 22000)
    {
      bits = 0x80;
      divider = 256 - (795500 + speed / 2) / speed;
    }
  else
    {
      divider = 128 - (397700 + speed / 2) / speed;
    }

  bits |= (unsigned char) divider;
  ess_write (devc, 0xa1, bits);

/*
 * Set filter divider register
 */

  speed = (speed * 9) / 20;	/* Set filter rolloff to 90% of speed/2 */
  divider = 256 - 7160000 / (speed * 82);
  ess_write (devc, 0xa2, divider);

  return;
}

static int
ess_audio_prepare_for_input (int dev, int bsize, int bcount)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  ess_speed (devc);
  sb_dsp_command (devc, DSP_CMD_SPKOFF);

  ess_write (devc, 0xb8, 0x0e);	/* Auto init DMA mode */
  ess_write (devc, 0xa8, (ess_read (devc, 0xa8) & ~0x03) |
	     (3 - devc->channels));	/* Mono/stereo */
  ess_write (devc, 0xb9, 2);	/* Demand mode (4 bytes/DMA request) */

  if (devc->channels == 1)
    {
      if (devc->bits == AFMT_U8 == 0)
	{			/* 8 bit mono */
	  ess_write (devc, 0xb7, 0x51);
	  ess_write (devc, 0xb7, 0xd0);
	}
      else
	{			/* 16 bit mono */
	  ess_write (devc, 0xb7, 0x71);
	  ess_write (devc, 0xb7, 0xf4);
	}
    }
  else
    {				/* Stereo */
      if (devc->bits == AFMT_U8)
	{			/* 8 bit stereo */
	  ess_write (devc, 0xb7, 0x51);
	  ess_write (devc, 0xb7, 0x98);
	}
      else
	{			/* 16 bit stereo */
	  ess_write (devc, 0xb7, 0x71);
	  ess_write (devc, 0xb7, 0xbc);
	}
    }

  ess_write (devc, 0xb1, (ess_read (devc, 0xb1) & 0x0f) | 0x50);
  ess_write (devc, 0xb2, (ess_read (devc, 0xb2) & 0x0f) | 0x50);

  devc->trigger_bits = 0;
  return 0;
}

static int
ess_audio_prepare_for_output (int dev, int bsize, int bcount)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  sb_dsp_reset (devc);
  ess_speed (devc);

  ess_write (devc, 0xb8, 4);	/* Auto init DMA mode */
  ess_write (devc, 0xa8, (ess_read (devc, 0xa8) & ~0x03) |
	     (3 - devc->channels));	/* Mono/stereo */
  ess_write (devc, 0xb9, 2);	/* Demand mode (4 bytes/request) */

  if (devc->channels == 1)
    {
      if (devc->bits == AFMT_U8)
	{			/* 8 bit mono */
	  ess_write (devc, 0xb6, 0x80);
	  ess_write (devc, 0xb7, 0x51);
	  ess_write (devc, 0xb7, 0xd0);
	}
      else
	{			/* 16 bit mono */
	  ess_write (devc, 0xb6, 0x00);
	  ess_write (devc, 0xb7, 0x71);
	  ess_write (devc, 0xb7, 0xf4);
	}
    }
  else
    {				/* Stereo */
      if (devc->bits == AFMT_U8)
	{			/* 8 bit stereo */
	  ess_write (devc, 0xb6, 0x80);
	  ess_write (devc, 0xb7, 0x51);
	  ess_write (devc, 0xb7, 0x98);
	}
      else
	{			/* 16 bit stereo */
	  ess_write (devc, 0xb6, 0x00);
	  ess_write (devc, 0xb7, 0x71);
	  ess_write (devc, 0xb7, 0xbc);
	}
    }

  ess_write (devc, 0xb1, (ess_read (devc, 0xb1) & 0x0f) | 0x50);
  ess_write (devc, 0xb2, (ess_read (devc, 0xb2) & 0x0f) | 0x50);
  sb_dsp_command (devc, DSP_CMD_SPKON);

  devc->trigger_bits = 0;
  return 0;
}

static void
ess_audio_output_block (int dev, unsigned long buf, int nr_bytes,
			int intrflag, int restart_dma)
{
  int             count = nr_bytes;
  sb_devc        *devc = audio_devs[dev]->devc;
  short           c = -nr_bytes;

  if (!restart_dma)
    return;

  DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE);

  if (audio_devs[dev]->dmachan1 > 3)
    count >>= 1;
  count--;

  devc->irq_mode = IMODE_OUTPUT;

  ess_write (devc, 0xa4, (unsigned char) ((unsigned short) c & 0xff));
  ess_write (devc, 0xa5, (unsigned char) (((unsigned short) c >> 8) & 0xff));

  ess_write (devc, 0xb8, ess_read (devc, 0xb8) | 0x05);		/* Go */
  devc->intr_active = 1;
}

static void
ess_audio_start_input (int dev, unsigned long buf, int nr_bytes, int intrflag,
		       int restart_dma)
{
  int             count = nr_bytes;
  sb_devc        *devc = audio_devs[dev]->devc;
  short           c = -nr_bytes;

  if (!restart_dma)
    return;

  /*
   * Start a DMA input to the buffer pointed by dmaqtail
   */

  DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ);

  if (audio_devs[dev]->dmachan1 > 3)
    count >>= 1;
  count--;

  devc->irq_mode = IMODE_INPUT;

  ess_write (devc, 0xa4, (unsigned char) ((unsigned short) c & 0xff));
  ess_write (devc, 0xa5, (unsigned char) (((unsigned short) c >> 8) & 0xff));

  ess_write (devc, 0xb8, ess_read (devc, 0xb8) | 0x0f);		/* Go */
  devc->intr_active = 1;
}

static void
ess_audio_trigger (int dev, int bits)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  bits &= devc->irq_mode;

  if (!bits)
    sb_dsp_command (devc, 0xd0);	/* Halt DMA */
  else
    {
      switch (devc->irq_mode)
	{
	case IMODE_INPUT:
	  ess_audio_start_input (dev, devc->trg_buf, devc->trg_bytes,
				 devc->trg_intrflag, devc->trg_restart);
	  break;

	case IMODE_OUTPUT:
	  ess_audio_output_block (dev, devc->trg_buf, devc->trg_bytes,
				  devc->trg_intrflag, devc->trg_restart);
	  break;
	}
    }

  devc->trigger_bits = bits;
}

/*
 * SB16 spesific routines
 */

static int
sb16_audio_set_speed (int dev, int speed)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  if (speed > 0)
    {
      if (speed < 5000)
	speed = 4000;

      if (speed > 44100)
	speed = 44100;

      devc->speed = speed;
    }

  return devc->speed;
}

static unsigned int
sb16_audio_set_bits (int dev, unsigned int bits)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  if (bits != 0)
    if (devc->bits == AFMT_U8 || bits == AFMT_S16_LE)
      devc->bits = bits;
    else
      devc->bits = AFMT_U8;

  return devc->bits;
}

static int
sb16_audio_prepare_for_input (int dev, int bsize, int bcount)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  audio_devs[dev]->dmachan1 =
    audio_devs[dev]->dmachan2 =
    devc->bits == AFMT_S16_LE ? devc->dma16 : devc->dma8;

  devc->trigger_bits = 0;
  return 0;
}

static int
sb16_audio_prepare_for_output (int dev, int bsize, int bcount)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  audio_devs[dev]->dmachan1 =
    audio_devs[dev]->dmachan2 =
    devc->bits == AFMT_S16_LE ? devc->dma16 : devc->dma8;

  devc->trigger_bits = 0;
  return 0;
}

static void
sb16_audio_output_block (int dev, unsigned long buf, int count,
			 int intrflag, int restart_dma)
{
  unsigned long   flags, cnt;
  sb_devc        *devc = audio_devs[dev]->devc;

  devc->irq_mode = IMODE_OUTPUT;
  devc->intr_active = 1;

  if (!restart_dma)
    return;

  cnt = count;
  if (devc->bits == AFMT_S16_LE)
    cnt >>= 1;
  cnt--;

  save_flags (flags);
  cli ();

  if (restart_dma)
    DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE);

  sb_dsp_command (devc, 0x41);
  sb_dsp_command (devc, (unsigned char) ((devc->speed >> 8) & 0xff));
  sb_dsp_command (devc, (unsigned char) (devc->speed & 0xff));

  sb_dsp_command (devc, (devc->bits == AFMT_S16_LE ? 0xb6 : 0xc6));
  sb_dsp_command (devc, ((devc->channels == 2 ? 0x20 : 0) +
			 (devc->bits == AFMT_S16_LE ? 0x10 : 0)));
  sb_dsp_command (devc, (unsigned char) (cnt & 0xff));
  sb_dsp_command (devc, (unsigned char) (cnt >> 8));

  restore_flags (flags);
}

static void
sb16_audio_start_input (int dev, unsigned long buf, int count, int intrflag,
			int restart_dma)
{
  unsigned long   flags, cnt;
  sb_devc        *devc = audio_devs[dev]->devc;

  devc->irq_mode = IMODE_INPUT;
  devc->intr_active = 1;

  if (!restart_dma)
    return;

  cnt = count;
  if (devc->bits == AFMT_S16_LE)
    cnt >>= 1;
  cnt--;

  save_flags (flags);
  cli ();

  if (restart_dma)
    DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ);

  sb_dsp_command (devc, 0x42);
  sb_dsp_command (devc, (unsigned char) ((devc->speed >> 8) & 0xff));
  sb_dsp_command (devc, (unsigned char) (devc->speed & 0xff));

  sb_dsp_command (devc, (devc->bits == AFMT_S16_LE ? 0xbe : 0xce));
  sb_dsp_command (devc, ((devc->channels == 2 ? 0x20 : 0) +
			 (devc->bits == AFMT_S16_LE ? 0x10 : 0)));
  sb_dsp_command (devc, (unsigned char) (cnt & 0xff));
  sb_dsp_command (devc, (unsigned char) (cnt >> 8));

  restore_flags (flags);
}

static void
sb16_audio_trigger (int dev, int bits)
{
  sb_devc        *devc = audio_devs[dev]->devc;

  bits &= devc->irq_mode;

  if (!bits)
    sb_dsp_command (devc, 0xd0);	/* Halt DMA */
  else
    {
      switch (devc->irq_mode)
	{
	case IMODE_INPUT:
	  sb16_audio_start_input (dev, devc->trg_buf, devc->trg_bytes,
				  devc->trg_intrflag, devc->trg_restart);
	  break;

	case IMODE_OUTPUT:
	  sb16_audio_output_block (dev, devc->trg_buf, devc->trg_bytes,
				   devc->trg_intrflag, devc->trg_restart);
	  break;
	}
    }

  devc->trigger_bits = bits;
}

static int
sb_audio_ioctl (int dev, unsigned int cmd, caddr_t arg, int local)
{
  return -(EINVAL);
}

static void
sb_audio_reset (int dev)
{
  unsigned long   flags;
  sb_devc        *devc = audio_devs[dev]->devc;

  save_flags (flags);
  cli ();
  sb_dsp_reset (devc);
  restore_flags (flags);
}

static struct audio_driver sb1_audio_driver =	/* SB1.x */
{
  sb_audio_open,
  sb_audio_close,
  sb_set_output_parms,
  sb_set_input_parms,
  sb_audio_ioctl,
  sb1_audio_prepare_for_input,
  sb1_audio_prepare_for_output,
  sb_audio_reset,
  sb1_audio_halt_xfer,
  NULL,				/* local_qlen */
  NULL,				/* copy_from_user */
  NULL,
  NULL,
  sb1_audio_trigger,
  sb1_audio_set_speed,
  sb1_audio_set_bits,
  sb1_audio_set_channels
};

static struct audio_driver sb20_audio_driver =	/* SB2.0 */
{
  sb_audio_open,
  sb_audio_close,
  sb_set_output_parms,
  sb_set_input_parms,
  sb_audio_ioctl,
  sb1_audio_prepare_for_input,
  sb1_audio_prepare_for_output,
  sb_audio_reset,
  sb1_audio_halt_xfer,
  NULL,				/* local_qlen */
  NULL,				/* copy_from_user */
  NULL,
  NULL,
  sb20_audio_trigger,
  sb1_audio_set_speed,
  sb1_audio_set_bits,
  sb1_audio_set_channels
};

static struct audio_driver sb201_audio_driver =		/* SB2.01 */
{
  sb_audio_open,
  sb_audio_close,
  sb_set_output_parms,
  sb_set_input_parms,
  sb_audio_ioctl,
  sb1_audio_prepare_for_input,
  sb1_audio_prepare_for_output,
  sb_audio_reset,
  sb1_audio_halt_xfer,
  NULL,				/* local_qlen */
  NULL,				/* copy_from_user */
  NULL,
  NULL,
  sb20_audio_trigger,
  sb201_audio_set_speed,
  sb1_audio_set_bits,
  sb1_audio_set_channels
};

static struct audio_driver sbpro_audio_driver =		/* SB Pro */
{
  sb_audio_open,
  sb_audio_close,
  sb_set_output_parms,
  sb_set_input_parms,
  sb_audio_ioctl,
  sbpro_audio_prepare_for_input,
  sbpro_audio_prepare_for_output,
  sb_audio_reset,
  sb1_audio_halt_xfer,
  NULL,				/* local_qlen */
  NULL,				/* copy_from_user */
  NULL,
  NULL,
  sb20_audio_trigger,
  sbpro_audio_set_speed,
  sb1_audio_set_bits,
  sbpro_audio_set_channels
};

static struct audio_driver jazz16_audio_driver =	/* Jazz16 and SM Wave */
{
  sb_audio_open,
  sb_audio_close,
  sb_set_output_parms,
  sb_set_input_parms,
  sb_audio_ioctl,
  sbpro_audio_prepare_for_input,
  sbpro_audio_prepare_for_output,
  sb_audio_reset,
  sb1_audio_halt_xfer,
  NULL,				/* local_qlen */
  NULL,				/* copy_from_user */
  NULL,
  NULL,
  sb20_audio_trigger,
  jazz16_audio_set_speed,
  sb16_audio_set_bits,
  sbpro_audio_set_channels
};

static struct audio_driver sb16_audio_driver =	/* SB16 */
{
  sb_audio_open,
  sb_audio_close,
  sb_set_output_parms,
  sb_set_input_parms,
  sb_audio_ioctl,
  sb16_audio_prepare_for_input,
  sb16_audio_prepare_for_output,
  sb_audio_reset,
  sb1_audio_halt_xfer,
  NULL,				/* local_qlen */
  NULL,				/* copy_from_user */
  NULL,
  NULL,
  sb16_audio_trigger,
  sb16_audio_set_speed,
  sb16_audio_set_bits,
  sbpro_audio_set_channels
};

static struct audio_driver ess_audio_driver =	/* ESS ES688/1688 */
{
  sb_audio_open,
  sb_audio_close,
  sb_set_output_parms,
  sb_set_input_parms,
  sb_audio_ioctl,
  ess_audio_prepare_for_input,
  ess_audio_prepare_for_output,
  sb_audio_reset,
  sb1_audio_halt_xfer,
  NULL,				/* local_qlen */
  NULL,				/* copy_from_user */
  NULL,
  NULL,
  ess_audio_trigger,
  sb16_audio_set_speed,
  sb16_audio_set_bits,
  sbpro_audio_set_channels
};

void
sb_audio_init (sb_devc * devc, char *name)
{
  int             audio_flags = 0;
  int             format_mask = AFMT_U8;

  struct audio_driver *driver = &sb1_audio_driver;

  switch (devc->model)
    {
    case MDL_SB1:		/* SB1.0 or SB 1.5 */
      DDB (printk ("Will use standard SB1.x driver\n"));
      audio_flags = DMA_HARDSTOP;
      break;

    case MDL_SB2:
      DDB (printk ("Will use SB2.0 driver\n"));
      audio_flags = DMA_AUTOMODE;
      driver = &sb20_audio_driver;
      break;

    case MDL_SB201:
      DDB (printk ("Will use SB2.01 (high speed) driver\n"));
      audio_flags = DMA_AUTOMODE;
      driver = &sb201_audio_driver;
      break;

    case MDL_JAZZ:
    case MDL_SMW:
      DDB (printk ("Will use Jazz16 driver\n"));
      audio_flags = DMA_AUTOMODE;
      format_mask |= AFMT_S16_LE;
      driver = &jazz16_audio_driver;
      break;

    case MDL_ESS:
      DDB (printk ("Will use ESS ES688/1688 driver\n"));
      audio_flags = DMA_AUTOMODE;
      format_mask |= AFMT_S16_LE;
      driver = &ess_audio_driver;
      break;

    case MDL_SB16:
      DDB (printk ("Will use SB16 driver\n"));
      audio_flags = DMA_AUTOMODE;
      format_mask |= AFMT_S16_LE;
      driver = &sb16_audio_driver;
      break;

    default:
      DDB (printk ("Will use SB Pro driver\n"));
      audio_flags = DMA_AUTOMODE;
      driver = &sbpro_audio_driver;
    }

  if ((devc->my_dev = sound_install_audiodrv (AUDIO_DRIVER_VERSION,
					      name,
					      driver,
					      sizeof (struct audio_driver),
					      audio_flags,
					      format_mask,
					      devc,
					      devc->dma8,
					      devc->dma8)) < 0)
    {
      return;
    }

  audio_devs[devc->my_dev]->mixer_dev = devc->my_mixerdev;
  audio_devs[devc->my_dev]->min_fragment = 5;
}

#endif
