/*
 * sound/mad16_sb_midi.c
 *
 * The low level driver for MAD16 SoundBlaster-DS-chip-based MIDI.
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

/*
 * Modifications by Aaron Ucko 1995
 */

#include "sound_config.h"

#if defined(CONFIG_MAD16) && defined(CONFIG_MIDI)

#define sbc_base mad16_sb_base
#include "sb.h"

static int      input_opened = 0;
static int      my_dev;
static int      mad16_sb_base = 0x220;
static int      mad16_sb_irq = 0;
static int      mad16_sb_dsp_ok = 0;
static int      mad16_sb_dsp_attached = 0;
static int     *midi_osp;

int             mad16_sb_midi_mode = NORMAL_MIDI;
int             mad16_sb_midi_busy = 0;

int             mad16_sb_duplex_midi = 0;
volatile int    mad16_sb_intr_active = 0;

void            (*midi_input_intr) (int dev, unsigned char data);

static void     mad16_sb_midi_init (int model);

static int
mad16_sb_dsp_command (unsigned char val)
{
  int             i;
  unsigned long   limit;

  limit = jiffies + HZ / 10;	/*
				   * The timeout is 0.1 seconds
				 */

  /*
   * Note! the i<500000 is an emergency exit. The mad16_sb_dsp_command() is sometimes
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

  printk ("MAD16 (SBP mode): DSP Command(%x) Timeout.\n", val);
  printk ("IRQ conflict???\n");
  return 0;
}

void
mad16_sbintr (int irq, void *dev_id, struct pt_regs *dummy)
{
  int             status;

  unsigned long   flags;
  unsigned char   data;

  status = inb (DSP_DATA_AVAIL);	/*
					   * Clear interrupt
					 */

  save_flags (flags);
  cli ();

  data = inb (DSP_READ);
  if (input_opened)
    midi_input_intr (my_dev, data);

  restore_flags (flags);
}

static int
mad16_sb_reset_dsp (void)
{
  int             loopc;

  outb (1, DSP_RESET);
  tenmicrosec (midi_osp);
  outb (0, DSP_RESET);
  tenmicrosec (midi_osp);
  tenmicrosec (midi_osp);
  tenmicrosec (midi_osp);

  for (loopc = 0; loopc < 1000 && !(inb (DSP_DATA_AVAIL) & 0x80); loopc++);	/*
										   * Wait
										   * for
										   * data
										   * *
										   * available
										   * status
										 */

  if (inb (DSP_READ) != 0xAA)
    return 0;			/*
				 * Sorry
				 */

  return 1;
}

int
mad16_sb_dsp_detect (struct address_info *hw_config)
{
  mad16_sb_base = hw_config->io_base;
  mad16_sb_irq = hw_config->irq;
  midi_osp = hw_config->osp;

  if (check_region (hw_config->io_base, 16))
    {
      printk ("MAD16 SB MIDI: I/O base %x not free\n", hw_config->io_base);
      return 0;
    }

  if (mad16_sb_dsp_ok)
    return 0;			/*
				 * Already initialized
				 */
  if (!mad16_sb_reset_dsp ())
    return 0;

  return 1;			/*
				 * Detected
				 */
}

long
mad16_sb_dsp_init (long mem_start, struct address_info *hw_config)
/* this function now just verifies the reported version and calls
 * mad16_sb_midi_init -- everything else is done elsewhere */
{

  mad16_sb_dsp_attached = 1;
  midi_osp = hw_config->osp;
  if (snd_set_irq_handler (mad16_sb_irq, mad16_sbintr, "MAD16 SB MIDI", midi_osp) < 0)
    {
      printk ("MAD16 SB MIDI: IRQ not free\n");
      return mem_start;
    }

  request_region (hw_config->io_base, 16, "mad16/Mozart MIDI");

  conf_printf ("MAD16 MIDI (SB mode)", hw_config);
  mad16_sb_midi_init (2);

  mad16_sb_dsp_ok = 1;
  return mem_start;
}

void
mad16_sb_dsp_unload (struct address_info *hw_config)
{
  if (!mad16_sb_dsp_attached)
    return;

  release_region (hw_config->io_base, 16);
  snd_release_irq (hw_config->irq);
}

static int
mad16_sb_midi_open (int dev, int mode,
		    void            (*input) (int dev, unsigned char data),
		    void            (*output) (int dev)
)
{

  if (!mad16_sb_dsp_ok)
    {
      printk ("MAD16_SB Error: MIDI hardware not installed\n");
      return -ENXIO;
    }

  if (mad16_sb_midi_busy)
    return -EBUSY;

  if (mode != OPEN_WRITE && !mad16_sb_duplex_midi)
    {
      if (num_midis == 1)
	printk ("MAD16 (SBP mode): Midi input not currently supported\n");
      return -EPERM;
    }

  mad16_sb_midi_mode = NORMAL_MIDI;
  if (mode != OPEN_WRITE)
    {
      if (mad16_sb_intr_active)
	return -EBUSY;
      mad16_sb_midi_mode = UART_MIDI;
    }

  if (mad16_sb_midi_mode == UART_MIDI)
    {
      mad16_sb_reset_dsp ();

      if (!mad16_sb_dsp_command (0x35))
	return -EIO;		/*
				   * Enter the UART mode
				 */
      mad16_sb_intr_active = 1;

      input_opened = 1;
      midi_input_intr = input;
    }

  mad16_sb_midi_busy = 1;

  return 0;
}

static void
mad16_sb_midi_close (int dev)
{
  if (mad16_sb_midi_mode == UART_MIDI)
    {
      mad16_sb_reset_dsp ();	/*
				   * The only way to kill the UART mode
				 */
    }
  mad16_sb_intr_active = 0;
  mad16_sb_midi_busy = 0;
  input_opened = 0;
}

static int
mad16_sb_midi_out (int dev, unsigned char midi_byte)
{
  unsigned long   flags;

  if (mad16_sb_midi_mode == NORMAL_MIDI)
    {
      save_flags (flags);
      cli ();
      if (mad16_sb_dsp_command (0x38))
	mad16_sb_dsp_command (midi_byte);
      else
	printk ("MAD16_SB Error: Unable to send a MIDI byte\n");
      restore_flags (flags);
    }
  else
    mad16_sb_dsp_command (midi_byte);	/*
					   * UART write
					 */

  return 1;
}

static int
mad16_sb_midi_start_read (int dev)
{
  if (mad16_sb_midi_mode != UART_MIDI)
    {
      printk ("MAD16 (SBP mode): MIDI input not implemented.\n");
      return -EPERM;
    }
  return 0;
}

static int
mad16_sb_midi_end_read (int dev)
{
  if (mad16_sb_midi_mode == UART_MIDI)
    {
      mad16_sb_reset_dsp ();
      mad16_sb_intr_active = 0;
    }
  return 0;
}

static int
mad16_sb_midi_ioctl (int dev, unsigned cmd, caddr_t arg)
{
  return -EPERM;
}

#define MIDI_SYNTH_NAME	"pseudo-SoundBlaster Midi"
#define MIDI_SYNTH_CAPS	0
#include "midi_synth.h"

static struct midi_operations mad16_sb_midi_operations =
{
  {"MAD16 (SBP mode)", 0, 0, SNDCARD_MAD16},
  &std_midi_synth,
  {0},
  mad16_sb_midi_open,
  mad16_sb_midi_close,
  mad16_sb_midi_ioctl,
  mad16_sb_midi_out,
  mad16_sb_midi_start_read,
  mad16_sb_midi_end_read,
  NULL,				/*
				 * Kick
				 */
  NULL,				/*
				 * command
				 */
  NULL,				/*
				 * buffer_status
				 */
  NULL
};

static void
mad16_sb_midi_init (int model)
{
  if (num_midis >= MAX_MIDI_DEV)
    {
      printk ("Sound: Too many midi devices detected\n");
      return;
    }

  std_midi_synth.midi_dev = num_midis;
  my_dev = num_midis;
  midi_devs[num_midis++] = &mad16_sb_midi_operations;
}

#endif
