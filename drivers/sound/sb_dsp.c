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
 *      Hunyue Yau      Jan 6 1994
 *      Added code to support Sound Galaxy NX Pro
 *
 *      JRA Gibson      April 1995
 *      Code added for MV ProSonic/Jazz 16 in 16 bit mode
 */

#include "sound_config.h"

#if defined(CONFIG_SB)

#include "sb.h"
#include "sb_mixer.h"
#undef SB_TEST_IRQ

int             sbc_base = 0;
static int      sbc_irq = 0, sbc_dma;
static int      open_mode = 0;	/* Read, write or both */
int             Jazz16_detected = 0;
int             AudioDrive = 0;	/* 1=ES1688 detected */
static int      ess_mpu_irq = 0;
int             sb_no_recording = 0;
static int      dsp_count = 0;
static int      trigger_bits;
static int      mpu_base = 0, mpu_irq = 0;

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
int             sbc_major = 1, sbc_minor = 0;
static int      dsp_stereo = 0;
static int      dsp_current_speed = DSP_DEFAULT_SPEED;
static int      sb16 = 0;
static int      irq_verified = 0;

int             sb_midi_mode = NORMAL_MIDI;
int             sb_midi_busy = 0;
int             sb_dsp_busy = 0;

volatile int    sb_irq_mode = IMODE_NONE;
static volatile int irq_ok = 0;

static int      dma8 = 1;
static int      dsp_16bit = 0;

/* 16 bit support
 */

static int      dma16 = 1;

static int      dsp_set_bits (int arg);
static int      initialize_ProSonic16 (void);

/* end of 16 bit support
 */

int             sb_duplex_midi = 0;
static int      my_dev = 0;

volatile int    sb_intr_active = 0;

static int      dsp_speed (int);
static int      dsp_set_stereo (int mode);
static void     sb_dsp_reset (int dev);
sound_os_info  *sb_osp = NULL;

static void     ess_init (void);

#if defined(CONFIG_MIDI) || defined(CONFIG_AUDIO)

/*
 * Common code for the midi and pcm functions
 */

int
sb_dsp_command (unsigned char val)
{
  int             i;
  unsigned long   limit;

  limit = jiffies + HZ / 10;	/*
				   * The timeout is 0.1 secods
				 */

  /*
   * Note! the i<500000 is an emergency exit. The sb_dsp_command() is sometimes
   * called while interrupts are disabled. This means that the timer is
   * disabled also. However the timeout situation is a abnormal condition.
   * Normally the DSP should be ready to accept commands after just couple of
   * loops.
   */

  for (i = 0; i < 500000 && jiffies < limit; i++)
    {
      if ((inb (DSP_STATUS) & 0x80) == 0)
	{
	  outb (val, DSP_COMMAND);
	  return 1;
	}
    }

  printk ("SoundBlaster: DSP Command(%x) Timeout.\n", val);
  return 0;
}

static int
ess_write (unsigned char reg, unsigned char data)
{
  /* Write a byte to an extended mode register of ES1688 */

  if (!sb_dsp_command (reg))
    return 0;

  return sb_dsp_command (data);
}

static int
ess_read (unsigned char reg)
{
/* Read a byte from an extended mode register of ES1688 */

  int             i;

  if (!sb_dsp_command (0xc0))	/* Read register command */
    return -1;

  if (!sb_dsp_command (reg))
    return -1;

  for (i = 1000; i; i--)
    {
      if (inb (DSP_DATA_AVAIL) & 0x80)
	return inb (DSP_READ);
    }

  return -1;
}

void
sbintr (int irq, struct pt_regs *dummy)
{
  int             status;

  if (sb16 && !AudioDrive)
    {
      unsigned char   src = sb_getmixer (IRQ_STAT);	/* Interrupt source register */

      if (src & 3)
	sb16_dsp_interrupt (irq);

#ifdef CONFIG_MIDI
      if (src & 4)
	sb16midiintr (irq);	/*
				 * SB MPU401 interrupt
				 */
#endif

      if (!(src & 1))
	return;			/*
				 * Not a DSP interupt
				 */
    }

  status = inb (DSP_DATA_AVAIL);	/*
					   * Clear interrupt
					 */
  if (sb_intr_active)
    switch (sb_irq_mode)
      {
      case IMODE_OUTPUT:
	if (!AudioDrive)
	  sb_intr_active = 0;
	DMAbuf_outputintr (my_dev, 1);
	break;

      case IMODE_INPUT:
	if (!AudioDrive)
	  sb_intr_active = 0;
	DMAbuf_inputintr (my_dev);
	break;

      case IMODE_INIT:
	sb_intr_active = 0;
	irq_ok = 1;
	break;

      case IMODE_MIDI:
#ifdef CONFIG_MIDI
	sb_midi_interrupt (irq);
#endif
	break;

      default:
	printk ("SoundBlaster: Unexpected interrupt\n");
      }
}

int
sb_get_irq (void)
{
  return 0;
}

void
sb_free_irq (void)
{
}

int
sb_reset_dsp (void)
{
  int             loopc;

  if (AudioDrive)
    outb (3, DSP_RESET);	/* Reset FIFO too */
  else
    outb (1, DSP_RESET);

  tenmicrosec (sb_osp);
  outb (0, DSP_RESET);
  tenmicrosec (sb_osp);
  tenmicrosec (sb_osp);
  tenmicrosec (sb_osp);

  for (loopc = 0; loopc < 1000 && !(inb (DSP_DATA_AVAIL) & 0x80); loopc++);

  if (inb (DSP_READ) != 0xAA)
    return 0;			/* Sorry */

  if (AudioDrive)
    sb_dsp_command (0xc6);	/* Enable extended mode */

  return 1;
}

#endif

#ifdef CONFIG_AUDIO

static void
dsp_speaker (char state)
{
  if (state)
    sb_dsp_command (DSP_CMD_SPKON);
  else
    sb_dsp_command (DSP_CMD_SPKOFF);
}

static int
ess_speed (int speed)
{
  int             rate;
  unsigned char   bits = 0;

  if (speed < 4000)
    speed = 4000;
  else if (speed > 48000)
    speed = 48000;

  if (speed > 22000)
    {
      bits = 0x80;
      rate = 256 - (795500 + speed / 2) / speed;
      speed = 795500 / (256 - rate);
    }
  else
    {
      rate = 128 - (397700 + speed / 2) / speed;
      speed = 397700 / (128 - rate);
    }

  bits |= (unsigned char) rate;
  ess_write (0xa1, bits);

  dsp_current_speed = speed;
  return speed;
}

static int
dsp_speed (int speed)
{
  unsigned char   tconst;
  unsigned long   flags;
  int             max_speed = 44100;

  if (AudioDrive)
    return ess_speed (speed);

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

  /* Logitech SoundMan Games and Jazz16 cards can support 44.1kHz stereo */
#if !defined (SM_GAMES)
  /*
   * Max. stereo speed is 22050
   */
  if (dsp_stereo && speed > 22050 && Jazz16_detected == 0 && AudioDrive == 0)
    speed = 22050;
#endif

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

      save_flags (flags);
      cli ();
      if (sb_dsp_command (0x40))
	sb_dsp_command (tconst);
      restore_flags (flags);

      tmp = 65536 - (tconst << 8);
      speed = (256000000 + tmp / 2) / tmp;
    }
  else
    {
      int             tmp;

      sb_dsp_highspeed = 0;
      tconst = (256 - ((1000000 + speed / 2) / speed)) & 0xff;

      save_flags (flags);
      cli ();
      if (sb_dsp_command (0x40))	/*
					 * Set time constant
					 */
	sb_dsp_command (tconst);
      restore_flags (flags);

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
}

static unsigned long trg_buf;
static int      trg_bytes;
static int      trg_intrflag;
static int      trg_restart;

static void
sb_dsp_output_block (int dev, unsigned long buf, int nr_bytes,
		     int intrflag, int restart_dma)
{
  trg_buf = buf;
  trg_bytes = nr_bytes;
  trg_intrflag = intrflag;
  trg_restart = restart_dma;
  sb_irq_mode = IMODE_OUTPUT;
}

static void
actually_output_block (int dev, unsigned long buf, int nr_bytes,
		       int intrflag, int restart_dma)
{
  unsigned long   flags;
  int             count = nr_bytes;

  if (!sb_irq_mode)
    dsp_speaker (ON);

  DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE);

  sb_irq_mode = 0;

  if (audio_devs[dev]->dmachan1 > 3)
    count >>= 1;
  count--;
  dsp_count = count;

  sb_irq_mode = IMODE_OUTPUT;

  if (AudioDrive)
    {
      int             c = 0x10000 - count;	/* ES1688 increments the count */

      ess_write (0xa4, (unsigned char) (c & 0xff));
      ess_write (0xa5, (unsigned char) ((c >> 8) & 0xff));

      ess_write (0xb8, ess_read (0xb8) | 0x01);		/* Go */
    }
  else if (sb_dsp_highspeed)
    {
      save_flags (flags);
      cli ();
      if (sb_dsp_command (0x48))	/*
					   * High speed size
					 */
	{
	  sb_dsp_command ((unsigned char) (dsp_count & 0xff));
	  sb_dsp_command ((unsigned char) ((dsp_count >> 8) & 0xff));
	  sb_dsp_command (0x91);	/*
					   * High speed 8 bit DAC
					 */
	}
      else
	printk ("SB Error: Unable to start (high speed) DAC\n");
      restore_flags (flags);
    }
  else
    {
      save_flags (flags);
      cli ();
      if (sb_dsp_command (0x14))	/*
					   * 8-bit DAC (DMA)
					 */
	{
	  sb_dsp_command ((unsigned char) (dsp_count & 0xff));
	  sb_dsp_command ((unsigned char) ((dsp_count >> 8) & 0xff));
	}
      else
	printk ("SB Error: Unable to start DAC\n");
      restore_flags (flags);
    }
  sb_intr_active = 1;
}

static void
sb_dsp_start_input (int dev, unsigned long buf, int count, int intrflag,
		    int restart_dma)
{
  trg_buf = buf;
  trg_bytes = count;
  trg_intrflag = intrflag;
  trg_restart = restart_dma;
  sb_irq_mode = IMODE_INPUT;
}

static void
actually_start_input (int dev, unsigned long buf, int count, int intrflag,
		      int restart_dma)
{
  unsigned long   flags;

  if (sb_no_recording)
    {
      printk ("SB Error: This device doesn't support recording\n");
      return;
    }

  /*
   * Start a DMA input to the buffer pointed by dmaqtail
   */

  if (!sb_irq_mode)
    dsp_speaker (OFF);

  DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ);
  sb_irq_mode = 0;

  if (audio_devs[dev]->dmachan1 > 3)
    count >>= 1;
  count--;
  dsp_count = count;

  sb_irq_mode = IMODE_INPUT;

  if (AudioDrive)
    {
      int             c = 0x10000 - count;	/* ES1688 increments the count */

      ess_write (0xa4, (unsigned char) (c & 0xff));
      ess_write (0xa5, (unsigned char) ((c >> 8) & 0xff));

      ess_write (0xb8, ess_read (0xb8) | 0x01);		/* Go */
    }
  else if (sb_dsp_highspeed)
    {
      save_flags (flags);
      cli ();
      if (sb_dsp_command (0x48))	/*
					   * High speed size
					 */
	{
	  sb_dsp_command ((unsigned char) (dsp_count & 0xff));
	  sb_dsp_command ((unsigned char) ((dsp_count >> 8) & 0xff));
	  sb_dsp_command (0x99);	/*
					   * High speed 8 bit ADC
					 */
	}
      else
	printk ("SB Error: Unable to start (high speed) ADC\n");
      restore_flags (flags);
    }
  else
    {
      save_flags (flags);
      cli ();
      if (sb_dsp_command (0x24))	/*
					   * 8-bit ADC (DMA)
					 */
	{
	  sb_dsp_command ((unsigned char) (dsp_count & 0xff));
	  sb_dsp_command ((unsigned char) ((dsp_count >> 8) & 0xff));
	}
      else
	printk ("SB Error: Unable to start ADC\n");
      restore_flags (flags);
    }

  sb_intr_active = 1;
}

static void
sb_dsp_trigger (int dev, int bits)
{

  if (!bits)
    sb_dsp_command (0xd0);	/* Halt DMA */
  else if (bits & sb_irq_mode)
    {
      switch (sb_irq_mode)
	{
	case IMODE_INPUT:
	  actually_start_input (my_dev, trg_buf, trg_bytes,
				trg_intrflag, trg_restart);
	  break;

	case IMODE_OUTPUT:
	  actually_output_block (my_dev, trg_buf, trg_bytes,
				 trg_intrflag, trg_restart);
	  break;
	}
    }

  trigger_bits = bits;
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
      if (AudioDrive)
	{

	  /* ess_init(); */
	  ess_write (0xb8, 0x0e);	/* Auto init DMA mode */
	  ess_write (0xa8, (ess_read (0xa8) & ~0x04) |
		     (2 - dsp_stereo));		/* Mono/stereo */
	  ess_write (0xb9, 2);	/* Demand mode (2 bytes/xfer) */

	  if (!dsp_stereo)
	    {
	      if (dsp_16bit == 0)
		{		/* 8 bit mono */
		  ess_write (0xb7, 0x51);
		  ess_write (0xb7, 0xd0);
		}
	      else
		{		/* 16 bit mono */
		  ess_write (0xb7, 0x71);
		  ess_write (0xb7, 0xf4);
		}
	    }
	  else
	    {			/* Stereo */
	      if (!dsp_16bit)
		{		/* 8 bit stereo */
		  ess_write (0xb7, 0x51);
		  ess_write (0xb7, 0x98);
		}
	      else
		{		/* 16 bit stereo */
		  ess_write (0xb7, 0x71);
		  ess_write (0xb7, 0xbc);
		}
	    }

	  ess_write (0xb1, (ess_read (0xb1) & 0x0f) | 0x50);
	  ess_write (0xb2, (ess_read (0xb2) & 0x0f) | 0x50);
	}
      else
	{			/* !AudioDrive */

	  /* Select correct dma channel
	     * for 16/8 bit acccess
	   */
	  audio_devs[my_dev]->dmachan1 = dsp_16bit ? dma16 : dma8;
	  if (dsp_stereo)
	    sb_dsp_command (dsp_16bit ? 0xac : 0xa8);
	  else
	    sb_dsp_command (dsp_16bit ? 0xa4 : 0xa0);

	  dsp_speed (dsp_current_speed);
	}			/* !AudioDrive */
    }
  trigger_bits = 0;
  return 0;
}

static int
sb_dsp_prepare_for_output (int dev, int bsize, int bcount)
{
  dsp_cleanup ();
  dsp_speaker (OFF);

  if (sbc_major == 3)		/* SB Pro (at least ) */
    {

      if (AudioDrive)
	{

	  /* ess_init(); */
	  ess_write (0xb8, 4);	/* Auto init DMA mode */
	  ess_write (0xa8, ess_read (0xa8) |
		     (2 - dsp_stereo));		/* Mono/stereo */
	  ess_write (0xb9, 2);	/* Demand mode (2 bytes/xfer) */

	  if (!dsp_stereo)
	    {
	      if (dsp_16bit == 0)
		{		/* 8 bit mono */
		  ess_write (0xb6, 0x80);
		  ess_write (0xb7, 0x51);
		  ess_write (0xb7, 0xd0);
		}
	      else
		{		/* 16 bit mono */
		  ess_write (0xb6, 0x00);
		  ess_write (0xb7, 0x71);
		  ess_write (0xb7, 0xf4);
		}
	    }
	  else
	    {			/* Stereo */
	      if (!dsp_16bit)
		{		/* 8 bit stereo */
		  ess_write (0xb6, 0x80);
		  ess_write (0xb7, 0x51);
		  ess_write (0xb7, 0x98);
		}
	      else
		{		/* 16 bit stereo */
		  ess_write (0xb6, 0x00);
		  ess_write (0xb7, 0x71);
		  ess_write (0xb7, 0xbc);
		}
	    }

	  ess_write (0xb1, (ess_read (0xb1) & 0x0f) | 0x50);
	  ess_write (0xb2, (ess_read (0xb2) & 0x0f) | 0x50);
	}
      else
	{			/* !AudioDrive */

	  /* 16 bit specific instructions (Jazz16)
	   */
	  audio_devs[my_dev]->dmachan1 = dsp_16bit ? dma16 : dma8;
	  if (Jazz16_detected != 2)	/* SM Wave */
	    sb_mixer_set_stereo (dsp_stereo);
	  if (dsp_stereo)
	    sb_dsp_command (dsp_16bit ? 0xac : 0xa8);
	  else
	    sb_dsp_command (dsp_16bit ? 0xa4 : 0xa0);
	}			/* !AudioDrive */

    }

  trigger_bits = 0;
  dsp_speaker (ON);
  return 0;
}

static void
sb_dsp_halt_xfer (int dev)
{
}

static int
verify_irq (void)
{
  irq_ok = 1;
  return irq_ok;
}

static int
sb_dsp_open (int dev, int mode)
{
  int             retval;

  if (!sb_dsp_ok)
    {
      printk ("SB Error: SoundBlaster board not installed\n");
      return -ENXIO;
    }

  if (sb_no_recording && mode & OPEN_READ)
    {
      printk ("SB Warning: Recording not supported by this device\n");
    }

  if (sb_intr_active || (sb_midi_busy && sb_midi_mode == UART_MIDI))
    {
      printk ("SB: PCM not possible during MIDI input\n");
      return -EBUSY;
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

  /* Allocate 8 bit dma
   */
  audio_devs[my_dev]->dmachan1 = dma8;

  /* Allocate 16 bit dma (jazz16)
   */
  if (Jazz16_detected != 0)
    if (dma16 != dma8)
      {
	if (sound_open_dma (dma16, "Jazz16 16 bit"))
	  {
	    sb_free_irq ();
	    /* DMAbuf_close_dma (dev); */
	    return -EBUSY;
	  }
      }

  sb_irq_mode = IMODE_NONE;

  sb_dsp_busy = 1;
  open_mode = mode;

  return 0;
}

static void
sb_dsp_close (int dev)
{
  /* Release 16 bit dma channel
   */
  if (Jazz16_detected)
    {
      audio_devs[my_dev]->dmachan1 = dma8;

      if (dma16 != dma8)
	sound_close_dma (dma16);
    }

  /* DMAbuf_close_dma (dev); */
  sb_free_irq ();
  /* sb_dsp_command (0xd4); */
  dsp_cleanup ();
  dsp_speaker (OFF);
  sb_dsp_busy = 0;
  sb_dsp_highspeed = 0;
  open_mode = 0;
}

static int
dsp_set_bits (int arg)
{
  if (arg)
    if (Jazz16_detected == 0 && AudioDrive == 0)
      dsp_16bit = 0;
    else
      switch (arg)
	{
	case 8:
	  dsp_16bit = 0;
	  break;
	case 16:
	  dsp_16bit = 1;
	  break;
	default:
	  dsp_16bit = 0;
	}

  return dsp_16bit ? 16 : 8;
}

static int
sb_dsp_ioctl (int dev, unsigned int cmd, ioctl_arg arg, int local)
{
  switch (cmd)
    {
    case SOUND_PCM_WRITE_RATE:
      if (local)
	return dsp_speed ((int) arg);
      return snd_ioctl_return ((int *) arg, dsp_speed (get_fs_long ((long *) arg)));
      break;

    case SOUND_PCM_READ_RATE:
      if (local)
	return dsp_current_speed;
      return snd_ioctl_return ((int *) arg, dsp_current_speed);
      break;

    case SOUND_PCM_WRITE_CHANNELS:
      if (local)
	return dsp_set_stereo ((int) arg - 1) + 1;
      return snd_ioctl_return ((int *) arg, dsp_set_stereo (get_fs_long ((long *) arg) - 1) + 1);
      break;

    case SOUND_PCM_READ_CHANNELS:
      if (local)
	return dsp_stereo + 1;
      return snd_ioctl_return ((int *) arg, dsp_stereo + 1);
      break;

    case SNDCTL_DSP_STEREO:
      if (local)
	return dsp_set_stereo ((int) arg);
      return snd_ioctl_return ((int *) arg, dsp_set_stereo (get_fs_long ((long *) arg)));
      break;

      /* Word size specific cases here.
         * SNDCTL_DSP_SETFMT=SOUND_PCM_WRITE_BITS
       */
    case SNDCTL_DSP_SETFMT:
      if (local)
	return dsp_set_bits ((int) arg);
      return snd_ioctl_return ((int *) arg, dsp_set_bits (get_fs_long ((long *) arg)));
      break;

    case SOUND_PCM_READ_BITS:
      if (local)
	return dsp_16bit ? 16 : 8;
      return snd_ioctl_return ((int *) arg, dsp_16bit ? 16 : 8);
      break;

    case SOUND_PCM_WRITE_FILTER:
    case SOUND_PCM_READ_FILTER:
      return -EINVAL;
      break;

    default:;
    }

  return -EINVAL;
}

static void
sb_dsp_reset (int dev)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();

  sb_reset_dsp ();
  dsp_speed (dsp_current_speed);
  dsp_cleanup ();

  restore_flags (flags);
}

#endif


/*
 * Initialization of a Media Vision ProSonic 16 Soundcard.
 * The function initializes a ProSonic 16 like PROS.EXE does for DOS. It sets
 * the base address, the DMA-channels, interrupts and enables the joystickport.
 *
 * Also used by Jazz 16 (same card, different name)
 *
 * written 1994 by Rainer Vranken
 * E-Mail: rvranken@polaris.informatik.uni-essen.de
 */

unsigned int
get_sb_byte (void)
{
  int             i;

  for (i = 1000; i; i--)
    if (inb (DSP_DATA_AVAIL) & 0x80)
      {
	return inb (DSP_READ);
      }

  return 0xffff;
}

/*
 * Logitech Soundman Wave detection and initialization by Hannu Savolainen.
 *
 * There is a microcontroller (8031) in the SM Wave card for MIDI emulation.
 * it's located at address MPU_BASE+4.  MPU_BASE+7 is a SM Wave specific
 * control register for MC reset, SCSI, OPL4 and DSP (future expansion)
 * address decoding. Otherwise the SM Wave is just a ordinary MV Jazz16
 * based soundcard.
 */

static void
smw_putmem (int base, int addr, unsigned char val)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();

  outb (addr & 0xff, base + 1);	/* Low address bits */
  outb (addr >> 8, base + 2);	/* High address bits */
  outb (val, base);		/* Data */

  restore_flags (flags);
}

static unsigned char
smw_getmem (int base, int addr)
{
  unsigned long   flags;
  unsigned char   val;

  save_flags (flags);
  cli ();

  outb (addr & 0xff, base + 1);	/* Low address bits */
  outb (addr >> 8, base + 2);	/* High address bits */
  val = inb (base);		/* Data */

  restore_flags (flags);
  return val;
}

#ifdef SMW_MIDI0001_INCLUDED
#include "smw-midi0001.h"
#else
unsigned char  *smw_ucode = NULL;
int             smw_ucodeLen = 0;

#endif

static int
initialize_smw (int mpu_base)
{

  int             mp_base = mpu_base + 4;	/* Microcontroller base */
  int             i;
  unsigned char   control;


  /*
     *  Reset the microcontroller so that the RAM can be accessed
   */

  control = inb (mpu_base + 7);
  outb (control | 3, mpu_base + 7);	/* Set last two bits to 1 (?) */
  outb ((control & 0xfe) | 2, mpu_base + 7);	/* xxxxxxx0 resets the mc */

  for (i = 0; i < 300; i++)	/* Wait at least 1ms */
    tenmicrosec (sb_osp);

  outb (control & 0xfc, mpu_base + 7);	/* xxxxxx00 enables RAM */

  /*
     *  Detect microcontroller by probing the 8k RAM area
   */
  smw_putmem (mp_base, 0, 0x00);
  smw_putmem (mp_base, 1, 0xff);
  tenmicrosec (sb_osp);

  if (smw_getmem (mp_base, 0) != 0x00 || smw_getmem (mp_base, 1) != 0xff)
    {
      printk ("\nSM Wave: No microcontroller RAM detected (%02x, %02x)\n",
	      smw_getmem (mp_base, 0), smw_getmem (mp_base, 1));
      return 0;			/* No RAM */
    }

  /*
     *  There is RAM so assume it's really a SM Wave
   */

  if (smw_ucodeLen > 0)
    {
      if (smw_ucodeLen != 8192)
	{
	  printk ("\nSM Wave: Invalid microcode (MIDI0001.BIN) length\n");
	  return 1;
	}

      /*
         *  Download microcode
       */

      for (i = 0; i < 8192; i++)
	smw_putmem (mp_base, i, smw_ucode[i]);

      /*
         *  Verify microcode
       */

      for (i = 0; i < 8192; i++)
	if (smw_getmem (mp_base, i) != smw_ucode[i])
	  {
	    printk ("SM Wave: Microcode verification failed\n");
	    return 0;
	  }
    }

  control = 0;
#ifdef SMW_SCSI_IRQ
  /*
     * Set the SCSI interrupt (IRQ2/9, IRQ3 or IRQ10). The SCSI interrupt
     * is disabled by default.
     *
     * Btw the Zilog 5380 SCSI controller is located at MPU base + 0x10.
   */
  {
    static unsigned char scsi_irq_bits[] =
    {0, 0, 3, 1, 0, 0, 0, 0, 0, 3, 2, 0, 0, 0, 0, 0};

    control |= scsi_irq_bits[SMW_SCSI_IRQ] << 6;
  }
#endif

#ifdef SMW_OPL4_ENABLE
  /*
     *  Make the OPL4 chip visible on the PC bus at 0x380.
     *
     *  There is no need to enable this feature since VoxWare
     *  doesn't support OPL4 yet. Also there is no RAM in SM Wave so
     *  enabling OPL4 is pretty useless.
   */
  control |= 0x10;		/* Uses IRQ12 if bit 0x20 == 0 */
  /* control |= 0x20;      Uncomment this if you want to use IRQ7 */
#endif

  outb (control | 0x03, mpu_base + 7);	/* xxxxxx11 restarts */
  return 1;
}

static int
initialize_ProSonic16 (void)
{
  int             x;
  static unsigned char int_translat[16] =
  {0, 0, 2, 3, 0, 1, 0, 4, 0, 2, 5, 0, 0, 0, 0, 6}, dma_translat[8] =
  {0, 1, 0, 2, 0, 3, 0, 4};

  outb (0xAF, 0x201);		/* ProSonic/Jazz16 wakeup */
  for (x = 0; x < 1000; ++x)	/* wait 10 milliseconds */
    tenmicrosec (sb_osp);
  outb (0x50, 0x201);
  outb ((sbc_base & 0x70) | ((mpu_base & 0x30) >> 4), 0x201);

  if (sb_reset_dsp ())
    {				/* OK. We have at least a SB */

      /* Check the version number of ProSonic (I guess) */

      if (!sb_dsp_command (0xFA))
	return 1;
      if (get_sb_byte () != 0x12)
	return 1;

      if (sb_dsp_command (0xFB) &&	/* set DMA-channels and Interrupts */
	  sb_dsp_command ((dma_translat[dma16] << 4) | dma_translat[dma8]) &&
      sb_dsp_command ((int_translat[mpu_irq] << 4) | int_translat[sbc_irq]))
	{
	  Jazz16_detected = 1;

	  if (mpu_base != 0)
	    if (initialize_smw (mpu_base))
	      Jazz16_detected = 2;

	  sb_dsp_disable_midi ();
	}

      return 1;			/* There was at least a SB */
    }
  return 0;			/* No SB or ProSonic16 detected */
}

int
sb_dsp_detect (struct address_info *hw_config)
{
  sbc_base = hw_config->io_base;
  sbc_irq = hw_config->irq;
  sbc_dma = hw_config->dma;
  sb_osp = hw_config->osp;

  if (sb_dsp_ok)
    return 0;			/*
				 * Already initialized
				 */
  dma8 = dma16 = hw_config->dma;

  if (!initialize_ProSonic16 ())
    return 0;

  return 1;			/*
				 * Detected
				 */
}

#ifdef CONFIG_AUDIO
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
  NULL,				/* copy_from_user */
  NULL,
  NULL,
  sb_dsp_trigger
};

#endif

static void
ess_init (void)			/* ESS1688 Initialization */
{
  unsigned char   cfg, irq_bits = 0, dma_bits = 0;

  AudioDrive = 1;
  midi_disabled = 1;

  sb_reset_dsp ();		/* Turn on extended mode */

/*
 *    Set IRQ configuration register
 */

  cfg = 0x50;			/* Enable only DMA counter interrupt */

  switch (sbc_irq)
    {
    case 2:
    case 9:
      irq_bits = 0;
      break;

    case 5:
      irq_bits = 1;
      break;

    case 7:
      irq_bits = 2;
      break;

    case 10:
      irq_bits = 3;
      break;

    default:
      irq_bits = 0;
      cfg = 0x10;		/* Disable all interrupts */
      printk ("\nESS1688: Invalid IRQ %d\n", sbc_irq);
    }

  if (!ess_write (0xb1, cfg | (irq_bits << 2)))
    printk ("\nESS1688: Failed to write to IRQ config register\n");

/*
 *    Set DMA configuration register
 */

  cfg = 0x50;			/* Extended mode DMA ebable */

  if (sbc_dma > 3 || sbc_dma < 0 || sbc_dma == 2)
    {
      dma_bits = 0;
      cfg = 0x00;		/* Disable all DMA */
      printk ("\nESS1688: Invalid DMA %d\n", sbc_dma);
    }
  else
    {
      if (sbc_dma == 3)
	dma_bits = 3;
      else
	dma_bits = sbc_dma + 1;
    }

  if (!ess_write (0xb2, cfg | (dma_bits << 2)))
    printk ("\nESS1688: Failed to write to DMA config register\n");

/*
 * Enable joystick and OPL3
 */

  cfg = sb_getmixer (0x40);
  sb_setmixer (0x40, cfg | 0x03);
}

void
ess_midi_init (struct address_info *hw_config)	/* called from sb16_midi.c */
{
  unsigned char   cfg, tmp;

  cfg = sb_getmixer (0x40) & 0x03;

  tmp = (hw_config->io_base & 0x0f0) >> 4;

  if (tmp > 3)
    {
      sb_setmixer (0x40, cfg);
      return;
    }

  cfg |= tmp << 3;

  tmp = 1;			/* MPU enabled without interrupts */

  switch (hw_config->irq)
    {
    case 9:
      tmp = 0x4;
      break;
    case 5:
      tmp = 0x5;
      break;
    case 7:
      tmp = 0x6;
      break;
    case 10:
      tmp = 0x7;
      break;
    }

  cfg |= tmp << 5;

  if (tmp != 1)
    {
      ess_mpu_irq = hw_config->irq;

      if (snd_set_irq_handler (ess_mpu_irq, sbmidiintr, "ES1688 MIDI", sb_osp) < 0)
	printk ("ES1688: Can't allocate IRQ%d\n", ess_mpu_irq);
    }

  sb_setmixer (0x40, cfg);
}

void
Jazz16_midi_init (struct address_info *hw_config)
{
  mpu_base = hw_config->io_base;
  mpu_irq = hw_config->irq;

  initialize_ProSonic16 ();
}

void
Jazz16_set_dma16 (int dma)
{
  dma16 = dma;

  initialize_ProSonic16 ();
}

long
sb_dsp_init (long mem_start, struct address_info *hw_config)
{
  int             i;
  int             ess_major = 0, ess_minor = 0;

  int             mixer_type = 0;

  sb_osp = hw_config->osp;
  sbc_major = sbc_minor = 0;
  sb_dsp_command (0xe1);	/*
				 * Get version
				 */

  for (i = 1000; i; i--)
    {
      if (inb (DSP_DATA_AVAIL) & 0x80)
	{			/*
				 * wait for Data Ready
				 */
	  if (sbc_major == 0)
	    sbc_major = inb (DSP_READ);
	  else
	    {
	      sbc_minor = inb (DSP_READ);
	      break;
	    }
	}
    }

  if (sbc_major == 0)
    {
      printk ("\n\nFailed to get SB version (%x) - possible I/O conflict\n\n",
	      inb (DSP_DATA_AVAIL));
      sbc_major = 1;
    }

  if (sbc_major == 2 || sbc_major == 3)
    sb_duplex_midi = 1;

  if (sbc_major == 4)
    sb16 = 1;

  if (sbc_major == 3 && sbc_minor == 1)
    {

/*
 * Try to detect ESS chips.
 */

      sb_dsp_command (0xe7);	/*
				 * Return identification bytes.
				 */

      for (i = 1000; i; i--)
	{
	  if (inb (DSP_DATA_AVAIL) & 0x80)
	    {			/*
				 * wait for Data Ready
				 */
	      if (ess_major == 0)
		ess_major = inb (DSP_READ);
	      else
		{
		  ess_minor = inb (DSP_READ);
		  break;
		}
	    }
	}
    }

  if (snd_set_irq_handler (sbc_irq, sbintr, "SoundBlaster", sb_osp) < 0)
    printk ("sb_dsp: Can't allocate IRQ\n");;

#ifdef CONFIG_AUDIO
  if (sbc_major >= 3)
    {
      if (Jazz16_detected)
	{
	  if (Jazz16_detected == 2)
	    sprintf (sb_dsp_operations.name, "SoundMan Wave %d.%d", sbc_major, sbc_minor);
	  else
	    sprintf (sb_dsp_operations.name, "MV Jazz16 %d.%d", sbc_major, sbc_minor);
	  sb_dsp_operations.format_mask |= AFMT_S16_LE;		/* Hurrah, 16 bits          */
	}
      else
#ifdef __SGNXPRO__
      if (mixer_type == 2)
	{
	  sprintf (sb_dsp_operations.name, "Sound Galaxy NX Pro %d.%d", sbc_major, sbc_minor);
	}
      else
#endif

      if (sbc_major == 4)
	{
	  sprintf (sb_dsp_operations.name, "SoundBlaster 16 %d.%d", sbc_major, sbc_minor);
	}
      else if (ess_major != 0)
	{
	  if (ess_major == 0x48 && (ess_minor & 0xf0) == 0x80)
	    sprintf (sb_dsp_operations.name, "ESS ES488 AudioDrive (rev %d)",
		     ess_minor & 0x0f);
	  else if (ess_major == 0x68 && (ess_minor & 0xf0) == 0x80)
	    {
	      sprintf (sb_dsp_operations.name,
		       "ESS ES1688 AudioDrive (rev %d)",
		       ess_minor & 0x0f);
	      sb_dsp_operations.format_mask |= AFMT_S16_LE;
	      ess_init ();
	    }
	}
      else
	{
	  sprintf (sb_dsp_operations.name, "SoundBlaster Pro %d.%d", sbc_major, sbc_minor);
	}
    }
  else
    {
      sprintf (sb_dsp_operations.name, "SoundBlaster %d.%d", sbc_major, sbc_minor);
    }

  conf_printf (sb_dsp_operations.name, hw_config);

  if (sbc_major >= 3)
    mixer_type = sb_mixer_init (sbc_major);

  if (!sb16)
    if (num_audiodevs < MAX_AUDIO_DEV)
      {
	audio_devs[my_dev = num_audiodevs++] = &sb_dsp_operations;

	if (AudioDrive)
	  audio_devs[my_dev]->flags |= DMA_AUTOMODE;

	audio_devs[my_dev]->buffsize = DSP_BUFFSIZE;
	dma8 = audio_devs[my_dev]->dmachan1 = hw_config->dma;
	audio_devs[my_dev]->dmachan2 = -1;
	if (sound_alloc_dma (hw_config->dma, "SoundBlaster"))
	  printk ("sb_dsp.c: Can't allocate DMA channel\n");

	/* Allocate 16 bit dma (Jazz16)
	 */
	if (Jazz16_detected != 0)
	  if (dma16 != dma8)
	    {
	      if (sound_alloc_dma (dma16, "Jazz16 16 bit"))
		{
		  printk ("Jazz16: Can't allocate 16 bit DMA channel\n");
		}
	    }
      }
    else
      printk ("SB: Too many DSP devices available\n");
#else
  conf_printf ("SoundBlaster (configured without audio support)", hw_config);
#endif

#ifdef CONFIG_MIDI
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
sb_dsp_unload (void)
{
  sound_free_dma (dma8);

  /* Free 16 bit dma (Jazz16)
   */
  if (Jazz16_detected != 0)
    if (dma16 != dma8)
      {
	sound_free_dma (dma16);
      }
  snd_release_irq (sbc_irq);

  if (AudioDrive && ess_mpu_irq)
    {
      snd_release_irq (ess_mpu_irq);
    }
}

void
sb_dsp_disable_midi (void)
{
  midi_disabled = 1;
}


#endif
