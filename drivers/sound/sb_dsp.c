/*
 * sound/sb_dsp.c
 *
 * The low level driver for the SoundBlaster DSP chip (SB1.0 to 2.1, SB Pro).
 *
 * Copyright by Hannu Savolainen 1994
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
 *
 * Modified:
 *	Hunyue Yau	Jan 6 1994
 *	Added code to support Sound Galaxy NX Pro
 *
 */

#include "sound_config.h"

#if defined(CONFIGURE_SOUNDCARD) && !defined(EXCLUDE_SB)

#include "sb.h"
#include "sb_mixer.h"
#undef SB_TEST_IRQ

int             sbc_base = 0;
static int      sbc_irq = 0;
static int      open_mode = 0;	/* Read, write or both */

/*
 * The DSP channel can be used either for input or output. Variable
 * 'sb_irq_mode' will be set when the program calls read or write first time
 * after open. Current version doesn't support mode changes without closing
 * and reopening the device. Support for this feature may be implemented in a
 * future version of this driver.
 */

int             sb_dsp_ok = 0;	/*


				 * *  * * Set to 1 after successful
				 * initialization  *  */
static int      midi_disabled = 0;
int             sb_dsp_highspeed = 0;
int             sbc_major = 1, sbc_minor = 0;	/*


						   * *  * * DSP version   */
static int      dsp_stereo = 0;
static int      dsp_current_speed = DSP_DEFAULT_SPEED;
static int      sb16 = 0;
static int      irq_verified = 0;

int             sb_midi_mode = NORMAL_MIDI;
int             sb_midi_busy = 0;	/*


					 * *  * * 1 if the process has output
					 * to *  * MIDI   */
int             sb_dsp_busy = 0;

volatile int    sb_irq_mode = IMODE_NONE;	/*


						 * *  * * IMODE_INPUT, *
						 * IMODE_OUTPUT * * or *
						 * IMODE_NONE   */
static volatile int irq_ok = 0;

int             sb_duplex_midi = 0;
static int      my_dev = 0;

volatile int    sb_intr_active = 0;

static int      dsp_speed (int);
static int      dsp_set_stereo (int mode);
int             sb_dsp_command (unsigned char val);

#if !defined(EXCLUDE_MIDI) || !defined(EXCLUDE_AUDIO)

/*
 * Common code for the midi and pcm functions
 */

int
sb_dsp_command (unsigned char val)
{
  int             i;
  unsigned long   limit;

  limit = GET_TIME () + HZ / 10;/*
					   * The timeout is 0.1 secods
					 */

  /*
   * Note! the i<500000 is an emergency exit. The sb_dsp_command() is sometimes
   * called while interrupts are disabled. This means that the timer is
   * disabled also. However the timeout situation is a abnormal condition.
   * Normally the DSP should be ready to accept commands after just couple of
   * loops.
   */

  for (i = 0; i < 500000 && GET_TIME () < limit; i++)
    {
      if ((INB (DSP_STATUS) & 0x80) == 0)
	{
	  OUTB (val, DSP_COMMAND);
	  return 1;
	}
    }

  printk ("SoundBlaster: DSP Command(%x) Timeout.\n", val);
  printk ("IRQ conflict???\n");
  return 0;
}

void
sbintr (int unit)
{
  int             status;

#ifndef EXCLUDE_SBPRO
  if (sb16)
    {
      unsigned char   src = sb_getmixer (IRQ_STAT);	/*


							 * *  * * Interrupt
							 * source * *
							 * register   */

#ifndef EXCLUDE_SB16
      if (src & 3)
	sb16_dsp_interrupt (unit);

#ifndef EXCLUDE_MIDI
      if (src & 4)
	sb16midiintr (unit);	/*
				 * SB MPU401 interrupt
				 */
#endif

#endif

      if (!(src & 1))
	return;			/*
				 * Not a DSP interupt
				 */
    }
#endif

  status = INB (DSP_DATA_AVAIL);/*
					 * Clear interrupt
					 */

  if (sb_intr_active)
    switch (sb_irq_mode)
      {
      case IMODE_OUTPUT:
	sb_intr_active = 0;
	DMAbuf_outputintr (my_dev, 1);
	break;

      case IMODE_INPUT:
	sb_intr_active = 0;
	DMAbuf_inputintr (my_dev);
	/*
	 * A complete buffer has been input. Let's start new one
	 */
	break;

      case IMODE_INIT:
	sb_intr_active = 0;
	irq_ok = 1;
	break;

      case IMODE_MIDI:
#ifndef EXCLUDE_MIDI
	sb_midi_interrupt (unit);
#endif
	break;

      default:
	printk ("SoundBlaster: Unexpected interrupt\n");
      }
}

static int      sb_irq_usecount = 0;

int
sb_get_irq (void)
{
  int             ok;

  if (!sb_irq_usecount)
    if ((ok = snd_set_irq_handler (sbc_irq, sbintr)) < 0)
      return ok;

  sb_irq_usecount++;

  return 0;
}

void
sb_free_irq (void)
{
  if (!sb_irq_usecount)
    return;

  sb_irq_usecount--;

  if (!sb_irq_usecount)
    snd_release_irq (sbc_irq);
}

int
sb_reset_dsp (void)
{
  int             loopc;

  OUTB (1, DSP_RESET);
  tenmicrosec ();
  OUTB (0, DSP_RESET);
  tenmicrosec ();
  tenmicrosec ();
  tenmicrosec ();

  for (loopc = 0; loopc < 1000 && !(INB (DSP_DATA_AVAIL) & 0x80); loopc++);	/*
										 * Wait
										 * for
										 * data
										 * *
										 * available
										 * status
										 */

  if (INB (DSP_READ) != 0xAA)
    return 0;			/*
				 * Sorry
				 */

  return 1;
}

#endif

#ifndef EXCLUDE_AUDIO

static void
dsp_speaker (char state)
{
  if (state)
    sb_dsp_command (DSP_CMD_SPKON);
  else
    sb_dsp_command (DSP_CMD_SPKOFF);
}

static int
dsp_speed (int speed)
{
  unsigned char   tconst;
  unsigned long   flags;
  int             max_speed = 44100;

  if (speed < 4000)
    speed = 4000;

  /*
 * Older SB models don't support higher speeds than 22050.
 */

  if (sbc_major < 2 ||
      (sbc_major == 2 && sbc_minor == 0))
    max_speed = 22050;

  /*
 * SB models earlier than SB Pro have low limit for the input speed.
 */
  if (open_mode != OPEN_WRITE)	/* Recording is possible */
    if (sbc_major < 3)		/* Limited input speed with these cards */
      if (sbc_major == 2 && sbc_minor > 0)
	max_speed = 15000;
      else
	max_speed = 13000;

  if (speed > max_speed)
    speed = max_speed;		/*
				 * Invalid speed
				 */

  if (dsp_stereo && speed > 22050)
    speed = 22050;
  /*
   * Max. stereo speed is 22050
   */

  if ((speed > 22050) && sb_midi_busy)
    {
      printk ("SB Warning: High speed DSP not possible simultaneously with MIDI output\n");
      speed = 22050;
    }

  if (dsp_stereo)
    speed *= 2;

  /*
   * Now the speed should be valid
   */

  if (speed > 22050)
    {				/*
				 * High speed mode
				 */
      int             tmp;

      tconst = (unsigned char) ((65536 -
				 ((256000000 + speed / 2) / speed)) >> 8);
      sb_dsp_highspeed = 1;

      DISABLE_INTR (flags);
      if (sb_dsp_command (0x40))
	sb_dsp_command (tconst);
      RESTORE_INTR (flags);

      tmp = 65536 - (tconst << 8);
      speed = (256000000 + tmp / 2) / tmp;
    }
  else
    {
      int             tmp;

      sb_dsp_highspeed = 0;
      tconst = (256 - ((1000000 + speed / 2) / speed)) & 0xff;

      DISABLE_INTR (flags);
      if (sb_dsp_command (0x40))/*
					 * Set time constant
					 */
	sb_dsp_command (tconst);
      RESTORE_INTR (flags);

      tmp = 256 - tconst;
      speed = (1000000 + tmp / 2) / tmp;
    }

  if (dsp_stereo)
    speed /= 2;

  dsp_current_speed = speed;
  return speed;
}

static int
dsp_set_stereo (int mode)
{
  dsp_stereo = 0;

#ifdef EXCLUDE_SBPRO
  return 0;
#else
  if (sbc_major < 3 || sb16)
    return 0;			/*
				 * Sorry no stereo
				 */

  if (mode && sb_midi_busy)
    {
      printk ("SB Warning: Stereo DSP not possible simultaneously with MIDI output\n");
      return 0;
    }

  dsp_stereo = !!mode;
  return dsp_stereo;
#endif
}

static void
sb_dsp_output_block (int dev, unsigned long buf, int count,
		     int intrflag, int restart_dma)
{
  unsigned long   flags;

  if (!sb_irq_mode)
    dsp_speaker (ON);

  sb_irq_mode = IMODE_OUTPUT;
  DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE);

  if (audio_devs[dev]->dmachan > 3)
    count >>= 1;
  count--;

  if (sb_dsp_highspeed)
    {
      DISABLE_INTR (flags);
      if (sb_dsp_command (0x48))/*
					 * High speed size
					 */
	{
	  sb_dsp_command ((unsigned char) (count & 0xff));
	  sb_dsp_command ((unsigned char) ((count >> 8) & 0xff));
	  sb_dsp_command (0x91);/*
					 * High speed 8 bit DAC
					 */
	}
      else
	printk ("SB Error: Unable to start (high speed) DAC\n");
      RESTORE_INTR (flags);
    }
  else
    {
      DISABLE_INTR (flags);
      if (sb_dsp_command (0x14))/*
					 * 8-bit DAC (DMA)
					 */
	{
	  sb_dsp_command ((unsigned char) (count & 0xff));
	  sb_dsp_command ((unsigned char) ((count >> 8) & 0xff));
	}
      else
	printk ("SB Error: Unable to start DAC\n");
      RESTORE_INTR (flags);
    }
  sb_intr_active = 1;
}

static void
sb_dsp_start_input (int dev, unsigned long buf, int count, int intrflag,
		    int restart_dma)
{
  /*
   * Start a DMA input to the buffer pointed by dmaqtail
   */

  unsigned long   flags;

  if (!sb_irq_mode)
    dsp_speaker (OFF);

  sb_irq_mode = IMODE_INPUT;
  DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ);

  if (audio_devs[dev]->dmachan > 3)
    count >>= 1;
  count--;

  if (sb_dsp_highspeed)
    {
      DISABLE_INTR (flags);
      if (sb_dsp_command (0x48))/*
					 * High speed size
					 */
	{
	  sb_dsp_command ((unsigned char) (count & 0xff));
	  sb_dsp_command ((unsigned char) ((count >> 8) & 0xff));
	  sb_dsp_command (0x99);/*
					 * High speed 8 bit ADC
					 */
	}
      else
	printk ("SB Error: Unable to start (high speed) ADC\n");
      RESTORE_INTR (flags);
    }
  else
    {
      DISABLE_INTR (flags);
      if (sb_dsp_command (0x24))/*
					 * 8-bit ADC (DMA)
					 */
	{
	  sb_dsp_command ((unsigned char) (count & 0xff));
	  sb_dsp_command ((unsigned char) ((count >> 8) & 0xff));
	}
      else
	printk ("SB Error: Unable to start ADC\n");
      RESTORE_INTR (flags);
    }

  sb_intr_active = 1;
}

static void
dsp_cleanup (void)
{
  sb_intr_active = 0;
}

static int
sb_dsp_prepare_for_input (int dev, int bsize, int bcount)
{
  dsp_cleanup ();
  dsp_speaker (OFF);

  if (sbc_major == 3)		/*
				 * SB Pro
				 */
    {
      if (dsp_stereo)
	sb_dsp_command (0xa8);
      else
	sb_dsp_command (0xa0);

      dsp_speed (dsp_current_speed);	/*
					 * Speed must be recalculated if
					 * #channels * changes
					 */
    }
  return 0;
}

static int
sb_dsp_prepare_for_output (int dev, int bsize, int bcount)
{
  dsp_cleanup ();
  dsp_speaker (ON);

#ifndef EXCLUDE_SBPRO
  if (sbc_major == 3)		/*
				 * SB Pro
				 */
    {
      sb_mixer_set_stereo (dsp_stereo);
      dsp_speed (dsp_current_speed);	/*
					 * Speed must be recalculated if
					 * #channels * changes
					 */
    }
#endif
  return 0;
}

static void
sb_dsp_halt_xfer (int dev)
{
}

static int
verify_irq (void)
{
#if 0
  DEFINE_WAIT_QUEUE (testq, testf);

  irq_ok = 0;

  if (sb_get_irq () == -1)
    {
      printk ("*** SB Error: Irq %d already in use\n", sbc_irq);
      return 0;
    }


  sb_irq_mode = IMODE_INIT;

  sb_dsp_command (0xf2);	/*
				 * This should cause immediate interrupt
				 */

  DO_SLEEP (testq, testf, HZ / 5);

  sb_free_irq ();

  if (!irq_ok)
    {
      printk ("SB Warning: IRQ%d test not passed!", sbc_irq);
      irq_ok = 1;
    }
#else
  irq_ok = 1;
#endif
  return irq_ok;
}

static int
sb_dsp_open (int dev, int mode)
{
  int             retval;

  if (!sb_dsp_ok)
    {
      printk ("SB Error: SoundBlaster board not installed\n");
      return RET_ERROR (ENXIO);
    }

  if (sb_intr_active || (sb_midi_busy && sb_midi_mode == UART_MIDI))
    {
      printk ("SB: PCM not possible during MIDI input\n");
      return RET_ERROR (EBUSY);
    }

  if (!irq_verified)
    {
      verify_irq ();
      irq_verified = 1;
    }
  else if (!irq_ok)
    printk ("SB Warning: Incorrect IRQ setting %d\n",
	    sbc_irq);

  retval = sb_get_irq ();
  if (retval)
    return retval;

  if (DMAbuf_open_dma (dev) < 0)
    {
      sb_free_irq ();
      printk ("SB: DMA Busy\n");
      return RET_ERROR (EBUSY);
    }

  sb_irq_mode = IMODE_NONE;

  sb_dsp_busy = 1;
  open_mode = mode;

  return 0;
}

static void
sb_dsp_close (int dev)
{
  DMAbuf_close_dma (dev);
  sb_free_irq ();
  dsp_cleanup ();
  dsp_speaker (OFF);
  sb_dsp_busy = 0;
  sb_dsp_highspeed = 0;
  open_mode = 0;
}

static int
sb_dsp_ioctl (int dev, unsigned int cmd, unsigned int arg, int local)
{
  switch (cmd)
    {
    case SOUND_PCM_WRITE_RATE:
      if (local)
	return dsp_speed (arg);
      return IOCTL_OUT (arg, dsp_speed (IOCTL_IN (arg)));
      break;

    case SOUND_PCM_READ_RATE:
      if (local)
	return dsp_current_speed;
      return IOCTL_OUT (arg, dsp_current_speed);
      break;

    case SOUND_PCM_WRITE_CHANNELS:
      if (local)
	return dsp_set_stereo (arg - 1) + 1;
      return IOCTL_OUT (arg, dsp_set_stereo (IOCTL_IN (arg) - 1) + 1);
      break;

    case SOUND_PCM_READ_CHANNELS:
      if (local)
	return dsp_stereo + 1;
      return IOCTL_OUT (arg, dsp_stereo + 1);
      break;

    case SNDCTL_DSP_STEREO:
      if (local)
	return dsp_set_stereo (arg);
      return IOCTL_OUT (arg, dsp_set_stereo (IOCTL_IN (arg)));
      break;

    case SOUND_PCM_WRITE_BITS:
    case SOUND_PCM_READ_BITS:
      if (local)
	return 8;
      return IOCTL_OUT (arg, 8);/*
					 * Only 8 bits/sample supported
					 */
      break;

    case SOUND_PCM_WRITE_FILTER:
    case SOUND_PCM_READ_FILTER:
      return RET_ERROR (EINVAL);
      break;

    default:
      return RET_ERROR (EINVAL);
    }

  return RET_ERROR (EINVAL);
}

static void
sb_dsp_reset (int dev)
{
  unsigned long   flags;

  DISABLE_INTR (flags);

  sb_reset_dsp ();
  dsp_speed (dsp_current_speed);
  dsp_cleanup ();

  RESTORE_INTR (flags);
}

#endif

int
sb_dsp_detect (struct address_info *hw_config)
{
  sbc_base = hw_config->io_base;
  sbc_irq = hw_config->irq;

  if (sb_dsp_ok)
    return 0;			/*
				 * Already initialized
				 */

  if (!sb_reset_dsp ())
    return 0;

  return 1;			/*
				 * Detected
				 */
}

#ifndef EXCLUDE_AUDIO
static struct audio_operations sb_dsp_operations =
{
  "SoundBlaster",
  NOTHING_SPECIAL,
  AFMT_U8,			/* Just 8 bits. Poor old SB */
  NULL,
  sb_dsp_open,
  sb_dsp_close,
  sb_dsp_output_block,
  sb_dsp_start_input,
  sb_dsp_ioctl,
  sb_dsp_prepare_for_input,
  sb_dsp_prepare_for_output,
  sb_dsp_reset,
  sb_dsp_halt_xfer,
  NULL,				/* local_qlen */
  NULL				/* copy_from_user */
};

#endif

long
sb_dsp_init (long mem_start, struct address_info *hw_config)
{
  int             i;
  int             mixer_type = 0;

  sbc_major = sbc_minor = 0;
  sb_dsp_command (0xe1);	/*
				 * Get version
				 */

  for (i = 1000; i; i--)
    {
      if (INB (DSP_DATA_AVAIL) & 0x80)
	{			/*
				 * wait for Data Ready
				 */
	  if (sbc_major == 0)
	    sbc_major = INB (DSP_READ);
	  else
	    {
	      sbc_minor = INB (DSP_READ);
	      break;
	    }
	}
    }

  if (sbc_major == 2 || sbc_major == 3)
    sb_duplex_midi = 1;

  if (sbc_major == 4)
    sb16 = 1;

#ifndef EXCLUDE_SBPRO
  if (sbc_major >= 3)
    mixer_type = sb_mixer_init (sbc_major);
#endif

#ifndef EXCLUDE_YM8312

  if (sbc_major > 3 ||
      (sbc_major == 3 && INB (0x388) == 0x00))	/* Should be 0x06 if not OPL-3 */
    enable_opl3_mode (OPL3_LEFT, OPL3_RIGHT, OPL3_BOTH);
#endif

  if (sbc_major >= 3)
    {
#ifndef SCO
#  ifdef __SGNXPRO__
      if (mixer_type == 2)
	{
	  sprintf (sb_dsp_operations.name, "Sound Galaxy NX Pro %d.%d", sbc_major, sbc_minor);
	}
      else
#  endif
	{
	  sprintf (sb_dsp_operations.name, "SoundBlaster Pro %d.%d", sbc_major, sbc_minor);
	}
#endif
    }
  else
    {
#ifndef SCO
      sprintf (sb_dsp_operations.name, "SoundBlaster %d.%d", sbc_major, sbc_minor);
#endif
    }

  printk (" <%s>", sb_dsp_operations.name);

#ifndef EXCLUDE_AUDIO
#if !defined(EXCLUDE_SB16) && !defined(EXCLUDE_SBPRO)
  if (!sb16)			/*
				 * There is a better driver for SB16
				 */
#endif
    if (num_audiodevs < MAX_AUDIO_DEV)
      {
	audio_devs[my_dev = num_audiodevs++] = &sb_dsp_operations;
	audio_devs[my_dev]->buffcount = DSP_BUFFCOUNT;
	audio_devs[my_dev]->buffsize = DSP_BUFFSIZE;
	audio_devs[my_dev]->dmachan = hw_config->dma;
      }
    else
      printk ("SB: Too many DSP devices available\n");
#endif

#ifndef EXCLUDE_MIDI
  if (!midi_disabled && !sb16)	/*
				 * Midi don't work in the SB emulation mode *
				 * of PAS, SB16 has better midi interface
				 */
    sb_midi_init (sbc_major);
#endif

  sb_dsp_ok = 1;
  return mem_start;
}

void
sb_dsp_disable_midi (void)
{
  midi_disabled = 1;
}

#endif
