/*
 * sound/uart401.c
 *
 * MPU-401 UART driver (formerly uart401_midi.c)
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

#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)

typedef struct uart401_devc
  {
    int             base;
    int             irq;
    int            *osp;
    void            (*midi_input_intr) (int dev, unsigned char data);
    int             opened;
    volatile unsigned char input_byte;
    int             my_dev;
    int             share_irq;
  }
uart401_devc;

static uart401_devc *detected_devc = NULL;
static uart401_devc *irq2devc[16] =
{NULL};

#define	DATAPORT   (devc->base)
#define	COMDPORT   (devc->base+1)
#define	STATPORT   (devc->base+1)

static int 
uart401_status (uart401_devc * devc)
{
  return inb (STATPORT);
}
#define input_avail(devc) (!(uart401_status(devc)&INPUT_AVAIL))
#define output_ready(devc)	(!(uart401_status(devc)&OUTPUT_READY))
static void 
uart401_cmd (uart401_devc * devc, unsigned char cmd)
{
  outb (cmd, COMDPORT);
}
static int 
uart401_read (uart401_devc * devc)
{
  return inb (DATAPORT);
}
static void 
uart401_write (uart401_devc * devc, unsigned char byte)
{
  outb (byte, DATAPORT);
}

#define	OUTPUT_READY	0x40
#define	INPUT_AVAIL	0x80
#define	MPU_ACK		0xFE
#define	MPU_RESET	0xFF
#define	UART_MODE_ON	0x3F

static int      reset_uart401 (uart401_devc * devc);

static void
uart401_input_loop (uart401_devc * devc)
{
  while (input_avail (devc))
    {
      unsigned char   c = uart401_read (devc);

      if (c == MPU_ACK)
	devc->input_byte = c;
      else if (devc->opened & OPEN_READ && devc->midi_input_intr)
	devc->midi_input_intr (devc->my_dev, c);
    }
}

void
uart401intr (int irq, void *dev_id, struct pt_regs *dummy)
{
  uart401_devc   *devc;

  if (irq < 1 || irq > 15)
    return;

  devc = irq2devc[irq];

  if (devc == NULL)
    return;

  if (input_avail (devc))
    uart401_input_loop (devc);
}

static int
uart401_open (int dev, int mode,
	      void            (*input) (int dev, unsigned char data),
	      void            (*output) (int dev)
)
{
  uart401_devc   *devc = (uart401_devc *) midi_devs[dev]->devc;

  if (devc->opened)
    {
      return -(EBUSY);
    }

  while (input_avail (devc))
    uart401_read (devc);

  devc->midi_input_intr = input;
  devc->opened = mode;

  return 0;
}

static void
uart401_close (int dev)
{
  uart401_devc   *devc = (uart401_devc *) midi_devs[dev]->devc;

  devc->opened = 0;
}

static int
uart401_out (int dev, unsigned char midi_byte)
{
  int             timeout;
  unsigned long   flags;
  uart401_devc   *devc = (uart401_devc *) midi_devs[dev]->devc;

  /*
   * Test for input since pending input seems to block the output.
   */

  save_flags (flags);
  cli ();

  if (input_avail (devc))
    uart401_input_loop (devc);

  restore_flags (flags);

  /*
   * Sometimes it takes about 13000 loops before the output becomes ready
   * (After reset). Normally it takes just about 10 loops.
   */

  for (timeout = 30000; timeout > 0 && !output_ready (devc); timeout--);

  if (!output_ready (devc))
    {
      printk ("MPU-401: Timeout\n");
      return 0;
    }

  uart401_write (devc, midi_byte);
  return 1;
}

static int
uart401_start_read (int dev)
{
  return 0;
}

static int
uart401_end_read (int dev)
{
  return 0;
}

static int
uart401_ioctl (int dev, unsigned cmd, caddr_t arg)
{
  return -(EINVAL);
}

static void
uart401_kick (int dev)
{
}

static int
uart401_buffer_status (int dev)
{
  return 0;
}

#define MIDI_SYNTH_NAME	"MPU-401 UART"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

static struct midi_operations uart401_operations =
{
  {"MPU-401 (UART) MIDI", 0, 0, SNDCARD_MPU401},
  &std_midi_synth,
  {0},
  uart401_open,
  uart401_close,
  uart401_ioctl,
  uart401_out,
  uart401_start_read,
  uart401_end_read,
  uart401_kick,
  NULL,
  uart401_buffer_status,
  NULL
};

static void
enter_uart_mode (uart401_devc * devc)
{
  int             ok, timeout;
  unsigned long   flags;

  save_flags (flags);
  cli ();
  for (timeout = 30000; timeout < 0 && !output_ready (devc); timeout--);

  devc->input_byte = 0;
  uart401_cmd (devc, UART_MODE_ON);

  ok = 0;
  for (timeout = 50000; timeout > 0 && !ok; timeout--)
    if (devc->input_byte == MPU_ACK)
      ok = 1;
    else if (input_avail (devc))
      if (uart401_read (devc) == MPU_ACK)
	ok = 1;

  restore_flags (flags);
}

void
attach_uart401 (struct address_info *hw_config)
{
  uart401_devc   *devc;
  char           *name = "MPU-401 (UART) MIDI";

  if (hw_config->name)
    name = hw_config->name;

  if (detected_devc == NULL)
    return;


  devc = (uart401_devc *) (sound_mem_blocks[sound_nblocks] = vmalloc (sizeof (uart401_devc)));
  if (sound_nblocks < 1024)
    sound_nblocks++;;
  if (devc == NULL)
    {
      printk ("uart401: Can't allocate memory\n");
      return;
    }

  memcpy ((char *) devc, (char *) detected_devc, sizeof (uart401_devc));
  detected_devc = NULL;

  devc->irq = hw_config->irq;
  if (devc->irq < 0)
    {
      devc->share_irq = 1;
      devc->irq *= -1;
    }
  else
    devc->share_irq = 0;

  if (devc->irq < 1 || devc->irq > 15)
    return;

  if (!devc->share_irq)
    if (snd_set_irq_handler (devc->irq, uart401intr, "uart401", devc->osp) < 0)
      {
	printk ("uart401: Failed to allocate IRQ%d\n", devc->irq);
	return;
      }

  irq2devc[devc->irq] = devc;
  devc->my_dev = num_midis;

  request_region (hw_config->io_base, 4, "SB MIDI");
  enter_uart_mode (devc);

  if (num_midis >= MAX_MIDI_DEV)
    {
      printk ("Sound: Too many midi devices detected\n");
      return;
    }

  conf_printf (name, hw_config);

  std_midi_synth.midi_dev = devc->my_dev = num_midis;


  midi_devs[num_midis] = (struct midi_operations *) (sound_mem_blocks[sound_nblocks] = vmalloc (sizeof (struct midi_operations)));

  if (sound_nblocks < 1024)
    sound_nblocks++;;
  if (midi_devs[num_midis] == NULL)
    {
      printk ("uart401: Failed to allocate memory\n");
      return;
    }

  memcpy ((char *) midi_devs[num_midis], (char *) &uart401_operations,
	  sizeof (struct midi_operations));

  midi_devs[num_midis]->devc = devc;


  midi_devs[num_midis]->converter = (struct synth_operations *) (sound_mem_blocks[sound_nblocks] = vmalloc (sizeof (struct synth_operations)));

  if (sound_nblocks < 1024)
    sound_nblocks++;;

  if (midi_devs[num_midis]->converter == NULL)
    {
      printk ("uart401: Failed to allocate memory\n");
      return;
    }

  memcpy ((char *) midi_devs[num_midis]->converter, (char *) &std_midi_synth,
	  sizeof (struct synth_operations));

  strcpy (midi_devs[num_midis]->info.name, name);
  num_midis++;
  devc->opened = 0;
}

static int
reset_uart401 (uart401_devc * devc)
{
  int             ok, timeout, n;

  /*
   * Send the RESET command. Try again if no success at the first time.
   */

  ok = 0;

  /* save_flags(flags);cli(); */

  for (n = 0; n < 2 && !ok; n++)
    {
      for (timeout = 30000; timeout < 0 && !output_ready (devc); timeout--);

      devc->input_byte = 0;
      uart401_cmd (devc, MPU_RESET);

      /*
       * Wait at least 25 msec. This method is not accurate so let's make the
       * loop bit longer. Cannot sleep since this is called during boot.
       */

      for (timeout = 50000; timeout > 0 && !ok; timeout--)
	if (devc->input_byte == MPU_ACK)	/* Interrupt */
	  ok = 1;
	else if (input_avail (devc))
	  if (uart401_read (devc) == MPU_ACK)
	    ok = 1;

    }

  if (ok)
    uart401_input_loop (devc);	/*
				 * Flush input before enabling interrupts
				 */

  /* restore_flags(flags); */

  return ok;
}

int
probe_uart401 (struct address_info *hw_config)
{
  int             ok = 0;

  static uart401_devc hw_info;
  uart401_devc   *devc = &hw_info;

  detected_devc = NULL;

  if (check_region (hw_config->io_base, 4))
    return 0;

  devc->base = hw_config->io_base;
  devc->irq = hw_config->irq;
  devc->osp = hw_config->osp;
  devc->midi_input_intr = NULL;
  devc->opened = 0;
  devc->input_byte = 0;
  devc->my_dev = 0;
  devc->share_irq = 0;

  ok = reset_uart401 (devc);

  if (ok)
    detected_devc = devc;

  return ok;
}

void
unload_uart401 (struct address_info *hw_config)
{
  uart401_devc   *devc;

  int             irq = hw_config->irq;

  if (irq < 0)
    irq *= -1;

  if (irq < 1 || irq > 15)
    return;

  devc = irq2devc[irq];
  if (devc == NULL)
    return;

  release_region (hw_config->io_base, 4);

  if (!devc->share_irq)
    snd_release_irq (devc->irq);
}


#endif
