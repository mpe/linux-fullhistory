/*
 * sound/sb_dsp.c
 * 
 * The low level driver for the SoundBlaster DS chips.
 * 
 * Copyright by Hannu Savolainen 1993
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
 */

#include "sound_config.h"

#if defined(CONFIGURE_SOUNDCARD) && !defined(EXCLUDE_SB) && !defined(EXCLUDE_MIDI)

#include "sb.h"
#undef SB_TEST_IRQ

/*
 * The DSP channel can be used either for input or output. Variable
 * 'sb_irq_mode' will be set when the program calls read or write first time
 * after open. Current version doesn't support mode changes without closing
 * and reopening the device. Support for this feature may be implemented in a
 * future version of this driver.
 */

extern int      sb_dsp_ok;	/* Set to 1 after successful initialization */

extern int      sb_midi_mode;
extern int      sb_midi_busy;	/* 1 if the process has output to MIDI */
extern int      sb_dsp_busy;
extern int      sb_dsp_highspeed;

extern volatile int sb_irq_mode;	/* IMODE_INPUT, IMODE_OUTPUT
					 * or IMODE_NONE */
extern int      sb_dsp_model;	/* 1=SB, 2=SB Pro */
extern int      sb_duplex_midi;
extern int      sb_intr_active;

static int
sb_midi_open (int dev, int mode,
	      void            (*input) (int dev, unsigned char data),
	      void            (*output) (int dev)
)
{
  int             ret;

  if (!sb_dsp_ok)
    {
      printk ("SB Error: MIDI hardware not installed\n");
      return RET_ERROR (ENXIO);
    }

  if (mode != OPEN_WRITE && !sb_duplex_midi)
    {
      if (num_midis == 1)
	printk ("SoundBlaster: Midi input not currently supported\n");
      return RET_ERROR (EPERM);
    }

  sb_midi_mode = NORMAL_MIDI;
  if (mode != OPEN_WRITE)
    {
      if (sb_dsp_busy || sb_intr_active)
	return RET_ERROR (EBUSY);
      sb_midi_mode = UART_MIDI;
    }

  if (sb_dsp_highspeed)
    {
      printk ("SB Error: Midi output not possible during stereo or high speed audio\n");
      return RET_ERROR (EBUSY);
    }

  if (sb_midi_mode == UART_MIDI)
    {
      sb_irq_mode = IMODE_MIDI;

      sb_reset_dsp ();

      if (!sb_dsp_command (0x35))
	return RET_ERROR (EIO);	/* Enter the UART mode */
      sb_intr_active = 1;

      if ((ret = sb_get_irq ()) < 0)
	{
	  sb_reset_dsp ();
	  return 0;		/* IRQ not free */
	}
    }

  sb_midi_busy = 1;

  return 0;
}

static void
sb_midi_close (int dev)
{
  if (sb_midi_mode == UART_MIDI)
    {
      sb_reset_dsp ();		/* The only way to kill the UART mode */
      sb_free_irq ();
    }
  sb_intr_active = 0;
  sb_midi_busy = 0;
}

static int
sb_midi_out (int dev, unsigned char midi_byte)
{
  unsigned long   flags;

  sb_midi_busy = 1;		/* Kill all notes after close */

  if (sb_midi_mode == NORMAL_MIDI)
    {
      DISABLE_INTR (flags);
      if (sb_dsp_command (0x38))
	sb_dsp_command (midi_byte);
      else
	printk ("SB Error: Unable to send a MIDI byte\n");
      RESTORE_INTR (flags);
    }
  else
    sb_dsp_command (midi_byte);	/* UART write */

  return 1;
}

static int
sb_midi_start_read (int dev)
{
  if (sb_midi_mode != UART_MIDI)
    {
      printk ("SoundBlaster: MIDI input not implemented.\n");
      return RET_ERROR (EPERM);
    }
  return 0;
}

static int
sb_midi_end_read (int dev)
{
  if (sb_midi_mode == UART_MIDI)
    {
      sb_reset_dsp ();
      sb_intr_active = 0;
    }
  return 0;
}

static int
sb_midi_ioctl (int dev, unsigned cmd, unsigned arg)
{
  return RET_ERROR (EPERM);
}

static struct midi_operations sb_midi_operations =
{
  {"SoundBlaster", 0, 0, SNDCARD_SB},
  sb_midi_open,
  sb_midi_close,
  sb_midi_ioctl,
  sb_midi_out,
  sb_midi_start_read,
  sb_midi_end_read,
  NULL,				/* Kick */
  NULL,				/* command */
  NULL				/* buffer_status */
};

void
sb_midi_init(int model)
{
  midi_devs[num_midis++] = &sb_midi_operations;
}

#endif
