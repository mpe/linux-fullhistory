/*
 * sound/sb16_midi.c
 * 
 * The low level driver for the MPU-401 UART emulation of the SB16.
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

#ifdef CONFIGURE_SOUNDCARD

#if !defined(EXCLUDE_SB) && !defined(EXCLUDE_SB16) && !defined(EXCLUDE_MIDI)

#define	DATAPORT   (sb16midi_base)	/* MPU-401 Data I/O Port on IBM */
#define	COMDPORT   (sb16midi_base+1)	/* MPU-401 Command Port on IBM */
#define	STATPORT   (sb16midi_base+1)	/* MPU-401 Status Port on IBM */

#define sb16midi_status()		INB(STATPORT)
#define input_avail()		(!(sb16midi_status()&INPUT_AVAIL))
#define output_ready()		(!(sb16midi_status()&OUTPUT_READY))
#define sb16midi_cmd(cmd)		OUTB(cmd, COMDPORT)
#define sb16midi_read()		INB(DATAPORT)
#define sb16midi_write(byte)	OUTB(byte, DATAPORT)

#define	OUTPUT_READY	0x40	/* Mask for Data Read Redy Bit */
#define	INPUT_AVAIL	0x80	/* Mask for Data Send Ready Bit */
#define	MPU_ACK		0xFE	/* MPU-401 Acknowledge Response */
#define	MPU_RESET	0xFF	/* MPU-401 Total Reset Command */
#define	UART_MODE_ON	0x3F	/* MPU-401 "Dumb UART Mode" */

static int      sb16midi_opened = 0;
static int      sb16midi_base = 0x330;
static int      sb16midi_detected = 0;
static int      my_dev;

static int      reset_sb16midi (void);
static void     (*midi_input_intr) (int dev, unsigned char data);

static void
sb16midi_input_loop (void)
{
  int             count;

  count = 10;

  while (count)			/* Not timed out */
    if (input_avail ())
      {
	unsigned char   c = sb16midi_read ();

	count = 100;

	if (sb16midi_opened & OPEN_READ)
	  midi_input_intr (my_dev, c);
      }
    else
      while (!input_avail () && count)
	count--;
}

void
sb16midiintr (int unit)
{
  if (input_avail ())
    sb16midi_input_loop ();
}

/*
 * It looks like there is no input interrupts in the UART mode. Let's try
 * polling.
 */

static void
poll_sb16midi (unsigned long dummy)
{
  unsigned long   flags;

  DEFINE_TIMER(sb16midi_timer, poll_sb16midi);

  if (!(sb16midi_opened & OPEN_READ))
    return;			/* No longer required */

  DISABLE_INTR (flags);

  if (input_avail ())
    sb16midi_input_loop ();

  ACTIVATE_TIMER(sb16midi_timer, poll_sb16midi, 1); /* Come back later */

  RESTORE_INTR (flags);
}

static int
sb16midi_open (int dev, int mode,
	     void            (*input) (int dev, unsigned char data),
	     void            (*output) (int dev)
)
{
  if (sb16midi_opened)
    {
      return RET_ERROR (EBUSY);
    }

  sb16midi_input_loop ();

  midi_input_intr = input;
  sb16midi_opened = mode;
  poll_sb16midi (0);		/* Enable input polling */

  return 0;
}

static void
sb16midi_close (int dev)
{
  sb16midi_opened = 0;
}

static int
sb16midi_out (int dev, unsigned char midi_byte)
{
  int             timeout;
  unsigned long   flags;

  /*
   * Test for input since pending input seems to block the output.
   */

  DISABLE_INTR (flags);

  if (input_avail ())
    sb16midi_input_loop ();

  RESTORE_INTR (flags);

  /*
   * Sometimes it takes about 13000 loops before the output becomes ready
   * (After reset). Normally it takes just about 10 loops.
   */

  for (timeout = 30000; timeout > 0 && !output_ready (); timeout--);	/* Wait */

  if (!output_ready ())
    {
      printk ("MPU-401: Timeout\n");
      return 0;
    }

  sb16midi_write (midi_byte);
  return 1;
}

static int
sb16midi_command (int dev, unsigned char midi_byte)
{
  return 1;
}

static int
sb16midi_start_read (int dev)
{
  return 0;
}

static int
sb16midi_end_read (int dev)
{
  return 0;
}

static int
sb16midi_ioctl (int dev, unsigned cmd, unsigned arg)
{
  return RET_ERROR (EINVAL);
}

static void
sb16midi_kick (int dev)
{
}

static int
sb16midi_buffer_status (int dev)
{
  return 0;			/* No data in buffers */
}

static struct midi_operations sb16midi_operations =
{
  {"SoundBlaster MPU-401", 0, 0, SNDCARD_SB16MIDI},
  sb16midi_open,
  sb16midi_close,
  sb16midi_ioctl,
  sb16midi_out,
  sb16midi_start_read,
  sb16midi_end_read,
  sb16midi_kick,
  sb16midi_command,
  sb16midi_buffer_status
};


long
attach_sb16midi (long mem_start, struct address_info *hw_config)
{
  int             ok, timeout;
  unsigned long   flags;

  sb16midi_base = hw_config->io_base;

  if (!sb16midi_detected)
    return RET_ERROR (EIO);

  DISABLE_INTR (flags);
  for (timeout = 30000; timeout < 0 && !output_ready (); timeout--);	/* Wait */
  sb16midi_cmd (UART_MODE_ON);

  ok = 0;
  for (timeout = 50000; timeout > 0 && !ok; timeout--)
    if (input_avail ())
      if (sb16midi_read () == MPU_ACK)
	ok = 1;

  RESTORE_INTR (flags);

  printk (" <SoundBlaster MPU-401>");

  my_dev = num_midis;
  midi_devs[num_midis++] = &sb16midi_operations;
  return mem_start;
}

static int
reset_sb16midi (void)
{
  unsigned long   flags;
  int             ok, timeout, n;

  /*
   * Send the RESET command. Try again if no success at the first time.
   */

  ok = 0;

  DISABLE_INTR (flags);

  for (n = 0; n < 2 && !ok; n++)
    {
      for (timeout = 30000; timeout < 0 && !output_ready (); timeout--);	/* Wait */
      sb16midi_cmd (MPU_RESET);	/* Send MPU-401 RESET Command */

      /*
       * Wait at least 25 msec. This method is not accurate so let's make the
       * loop bit longer. Cannot sleep since this is called during boot.
       */

      for (timeout = 50000; timeout > 0 && !ok; timeout--)
	if (input_avail ())
	  if (sb16midi_read () == MPU_ACK)
	    ok = 1;

    }

  sb16midi_opened = 0;
  if (ok)
    sb16midi_input_loop ();	/* Flush input before enabling interrupts */

  RESTORE_INTR (flags);

  return ok;
}


int
probe_sb16midi (struct address_info *hw_config)
{
  int             ok = 0;

  sb16midi_base = hw_config->io_base;

  if (sb_get_irq () < 0)
    return 0;

  ok = reset_sb16midi ();

  sb16midi_detected = ok;
  return ok;
}

#endif

#endif
