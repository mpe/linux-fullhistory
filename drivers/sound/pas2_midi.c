/*
 * sound/pas2_midi.c
 *
 * The low level driver for the PAS Midi Interface.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>


#include "sound_config.h"

#ifdef CONFIG_PAS
#ifdef CONFIG_MIDI

static int      midi_busy = 0, input_opened = 0;
static int      my_dev;

static unsigned char tmp_queue[256];
static volatile int qlen;
static volatile unsigned char qhead, qtail;

static void     (*midi_input_intr) (int dev, unsigned char data);

static int
pas_midi_open(int dev, int mode,
	      void            (*input) (int dev, unsigned char data),
	      void            (*output) (int dev)
)
{
	int             err;
	unsigned long   flags;
	unsigned char   ctrl;


	if (midi_busy)
	  {
		  printk("PAS16: Midi busy\n");
		  return -EBUSY;
	  }
	/*
	 * Reset input and output FIFO pointers
	 */
	pas_write(0x20 | 0x40,
		  0x178b);

	save_flags(flags);
	cli();

	if ((err = pas_set_intr(0x10)) < 0)
	  {
		  restore_flags(flags);
		  return err;
	  }
	/*
	 * Enable input available and output FIFO empty interrupts
	 */

	ctrl = 0;
	input_opened = 0;
	midi_input_intr = input;

	if (mode == OPEN_READ || mode == OPEN_READWRITE)
	  {
		  ctrl |= 0x04;	/* Enable input */
		  input_opened = 1;
	  }
	if (mode == OPEN_WRITE || mode == OPEN_READWRITE)
	  {
		  ctrl |= 0x08 | 0x10;	/* Enable output */
	  }
	pas_write(ctrl, 0x178b);

	/*
	 * Acknowledge any pending interrupts
	 */

	pas_write(0xff, 0x1B88);

	restore_flags(flags);

	midi_busy = 1;
	qlen = qhead = qtail = 0;
	return 0;
}

static void
pas_midi_close(int dev)
{

	/*
	 * Reset FIFO pointers, disable intrs
	 */
	pas_write(0x20 | 0x40, 0x178b);

	pas_remove_intr(0x10);
	midi_busy = 0;
}

static int
dump_to_midi(unsigned char midi_byte)
{
	int             fifo_space, x;

	fifo_space = ((x = pas_read(0x1B89)) >> 4) & 0x0f;

/*
 * The MIDI FIFO space register and it's documentation is nonunderstandable.
 * There seem to be no way to differentiate between buffer full and buffer
 * empty situations. For this reason we don't never write the buffer
 * completely full. In this way we can assume that 0 (or is it 15)
 * means that the buffer is empty.
 */

	if (fifo_space < 2 && fifo_space != 0)	/* Full (almost) */
	  {
		  return 0;	/* Ask upper layers to retry after some time */
	  }
	pas_write(midi_byte, 0x178A);

	return 1;
}

static int
pas_midi_out(int dev, unsigned char midi_byte)
{

	unsigned long   flags;

	/*
	 * Drain the local queue first
	 */

	save_flags(flags);
	cli();

	while (qlen && dump_to_midi(tmp_queue[qhead]))
	  {
		  qlen--;
		  qhead++;
	  }

	restore_flags(flags);

	/*
	 * Output the byte if the local queue is empty.
	 */

	if (!qlen)
		if (dump_to_midi(midi_byte))
			return 1;

	/*
	 * Put to the local queue
	 */

	if (qlen >= 256)
		return 0;	/* Local queue full */

	save_flags(flags);
	cli();

	tmp_queue[qtail] = midi_byte;
	qlen++;
	qtail++;

	restore_flags(flags);

	return 1;
}

static int
pas_midi_start_read(int dev)
{
	return 0;
}

static int
pas_midi_end_read(int dev)
{
	return 0;
}

static void
pas_midi_kick(int dev)
{
}

static int
pas_buffer_status(int dev)
{
	return qlen;
}

#define MIDI_SYNTH_NAME	"Pro Audio Spectrum Midi"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

static struct midi_operations pas_midi_operations =
{
	{"Pro Audio Spectrum", 0, 0, SNDCARD_PAS},
	&std_midi_synth,
	{0},
	pas_midi_open,
	pas_midi_close,
	NULL,
	pas_midi_out,
	pas_midi_start_read,
	pas_midi_end_read,
	pas_midi_kick,
	NULL,
	pas_buffer_status,
	NULL
};

void
pas_midi_init(void)
{
	int             dev = sound_alloc_mididev();

	if (dev == -1)
	  {
		  printk(KERN_WARNING "pas_midi_init: Too many midi devices detected\n");
		  return;
	  }
	std_midi_synth.midi_dev = my_dev = dev;
	midi_devs[dev] = &pas_midi_operations;
	sequencer_init();
}

void
pas_midi_interrupt(void)
{
	unsigned char   stat;
	int             i, incount;
	unsigned long   flags;

	stat = pas_read(0x1B88);

	if (stat & 0x04)	/* Input data available */
	  {
		  incount = pas_read(0x1B89) & 0x0f;	/* Input FIFO size */
		  if (!incount)
			  incount = 16;

		  for (i = 0; i < incount; i++)
			  if (input_opened)
			    {
				    midi_input_intr(my_dev, pas_read(0x178A));
			  } else
				  pas_read(0x178A);	/* Flush */
	  }
	if (stat & (0x08 | 0x10))
	  {
		  save_flags(flags);
		  cli();

		  while (qlen && dump_to_midi(tmp_queue[qhead]))
		    {
			    qlen--;
			    qhead++;
		    }

		  restore_flags(flags);
	  }
	if (stat & 0x40)
	  {
		  printk("MIDI output overrun %x,%x\n", pas_read(0x1B89), stat);
	  }
	pas_write(stat, 0x1B88);	/* Acknowledge interrupts */
}

#endif
#endif
