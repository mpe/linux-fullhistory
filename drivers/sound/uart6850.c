/*
 * sound/uart6850.c
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>

/* Mon Nov 22 22:38:35 MET 1993 marco@driq.home.usn.nl:
 *      added 6850 support, used with COVOX SoundMaster II and custom cards.
 */

#include "sound_config.h"

#if defined(CONFIG_UART6850) && defined(CONFIG_MIDI)

static int      uart6850_base = 0x330;

static int     *uart6850_osp;

#define	DATAPORT   (uart6850_base)
#define	COMDPORT   (uart6850_base+1)
#define	STATPORT   (uart6850_base+1)

static int 
uart6850_status (void)
{
  return inb (STATPORT);
}
#define input_avail()		(uart6850_status()&INPUT_AVAIL)
#define output_ready()		(uart6850_status()&OUTPUT_READY)
static void 
uart6850_cmd (unsigned char cmd)
{
  outb (cmd, COMDPORT);
}
static int 
uart6850_read (void)
{
  return inb (DATAPORT);
}
static void 
uart6850_write (unsigned char byte)
{
  outb (byte, DATAPORT);
}

#define	OUTPUT_READY	0x02	/* Mask for data ready Bit */
#define	INPUT_AVAIL	0x01	/* Mask for Data Send Ready Bit */

#define	UART_RESET	0x95
#define	UART_MODE_ON	0x03

static int      uart6850_opened = 0;
static int      uart6850_irq;
static int      uart6850_detected = 0;
static int      my_dev;

static int      reset_uart6850 (void);
static void     (*midi_input_intr) (int dev, unsigned char data);
static void     poll_uart6850 (unsigned long dummy);


static struct timer_list uart6850_timer =
{NULL, NULL, 0, 0, poll_uart6850};

static void
uart6850_input_loop (void)
{
  int             count;

  count = 10;

  while (count)			/*
				 * Not timed out
				 */
    if (input_avail ())
      {
	unsigned char   c = uart6850_read ();

	count = 100;

	if (uart6850_opened & OPEN_READ)
	  midi_input_intr (my_dev, c);
      }
    else
      while (!input_avail () && count)
	count--;
}

void
m6850intr (int irq, void *dev_id, struct pt_regs *dummy)
{
  if (input_avail ())
    uart6850_input_loop ();
}

/*
 * It looks like there is no input interrupts in the UART mode. Let's try
 * polling.
 */

static void
poll_uart6850 (unsigned long dummy)
{
  unsigned long   flags;

  if (!(uart6850_opened & OPEN_READ))
    return;			/* Device has been closed */

  save_flags (flags);
  cli ();

  if (input_avail ())
    uart6850_input_loop ();


  {
    uart6850_timer.expires = (1) + jiffies;
    add_timer (&uart6850_timer);
  };				/*
				   * Come back later
				 */

  restore_flags (flags);
}

static int
uart6850_open (int dev, int mode,
	       void            (*input) (int dev, unsigned char data),
	       void            (*output) (int dev)
)
{
  if (uart6850_opened)
    {
      printk ("Midi6850: Midi busy\n");
      return -(EBUSY);
    }

  ;
  uart6850_cmd (UART_RESET);

  uart6850_input_loop ();

  midi_input_intr = input;
  uart6850_opened = mode;
  poll_uart6850 (0);		/*
				 * Enable input polling
				 */

  return 0;
}

static void
uart6850_close (int dev)
{
  uart6850_cmd (UART_MODE_ON);

  del_timer (&uart6850_timer);;
  uart6850_opened = 0;
}

static int
uart6850_out (int dev, unsigned char midi_byte)
{
  int             timeout;
  unsigned long   flags;

  /*
   * Test for input since pending input seems to block the output.
   */

  save_flags (flags);
  cli ();

  if (input_avail ())
    uart6850_input_loop ();

  restore_flags (flags);

  /*
   * Sometimes it takes about 13000 loops before the output becomes ready
   * (After reset). Normally it takes just about 10 loops.
   */

  for (timeout = 30000; timeout > 0 && !output_ready (); timeout--);	/*
									 * Wait
									 */

  if (!output_ready ())
    {
      printk ("Midi6850: Timeout\n");
      return 0;
    }

  uart6850_write (midi_byte);
  return 1;
}

static int
uart6850_command (int dev, unsigned char *midi_byte)
{
  return 1;
}

static int
uart6850_start_read (int dev)
{
  return 0;
}

static int
uart6850_end_read (int dev)
{
  return 0;
}

static int
uart6850_ioctl (int dev, unsigned cmd, caddr_t arg)
{
  return -(EINVAL);
}

static void
uart6850_kick (int dev)
{
}

static int
uart6850_buffer_status (int dev)
{
  return 0;			/*
				 * No data in buffers
				 */
}

#define MIDI_SYNTH_NAME	"6850 UART Midi"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

static struct midi_operations uart6850_operations =
{
  {"6850 UART", 0, 0, SNDCARD_UART6850},
  &std_midi_synth,
  {0},
  uart6850_open,
  uart6850_close,
  uart6850_ioctl,
  uart6850_out,
  uart6850_start_read,
  uart6850_end_read,
  uart6850_kick,
  uart6850_command,
  uart6850_buffer_status
};


void
attach_uart6850 (struct address_info *hw_config)
{
  int             ok, timeout;
  unsigned long   flags;

  if (num_midis >= MAX_MIDI_DEV)
    {
      printk ("Sound: Too many midi devices detected\n");
      return;
    }

  uart6850_base = hw_config->io_base;
  uart6850_osp = hw_config->osp;
  uart6850_irq = hw_config->irq;

  if (!uart6850_detected)
    return;

  save_flags (flags);
  cli ();

  for (timeout = 30000; timeout < 0 && !output_ready (); timeout--);	/*
									 * Wait
									 */
  uart6850_cmd (UART_MODE_ON);

  ok = 1;

  restore_flags (flags);

  conf_printf ("6850 Midi Interface", hw_config);

  std_midi_synth.midi_dev = my_dev = num_midis;
  midi_devs[num_midis++] = &uart6850_operations;
}

static int
reset_uart6850 (void)
{
  uart6850_read ();
  return 1;			/*
				 * OK
				 */
}


int
probe_uart6850 (struct address_info *hw_config)
{
  int             ok = 0;

  uart6850_osp = hw_config->osp;
  uart6850_base = hw_config->io_base;
  uart6850_irq = hw_config->irq;

  if (snd_set_irq_handler (uart6850_irq, m6850intr, "MIDI6850", uart6850_osp) < 0)
    return 0;

  ok = reset_uart6850 ();

  uart6850_detected = ok;
  return ok;
}

void
unload_uart6850 (struct address_info *hw_config)
{
  snd_release_irq (hw_config->irq);
}

#endif
