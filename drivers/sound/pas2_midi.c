/*
 * sound/pas2_midi.c
 *
 * The low level driver for the PAS Midi Interface.
 */

#include <linux/config.h>

#include "sound_config.h"

#if defined(CONFIG_PAS) && defined(CONFIG_MIDI)

static int      midi_busy = 0, input_opened = 0;
static int      my_dev;
static volatile int ofifo_bytes = 0;

static unsigned char tmp_queue[256];
static volatile int qlen;
static volatile unsigned char qhead, qtail;

static void     (*midi_input_intr) (int dev, unsigned char data);

static int
pas_midi_open (int dev, int mode,
	       void            (*input) (int dev, unsigned char data),
	       void            (*output) (int dev)
)
{
  int             err;
  unsigned long   flags;
  unsigned char   ctrl;


  if (midi_busy)
    {
      printk ("PAS2: Midi busy\n");
      return -(EBUSY);
    }

  /*
   * Reset input and output FIFO pointers
   */
  pas_write (0x20 | 0x40,
	     0x178b);

  save_flags (flags);
  cli ();

  if ((err = pas_set_intr (0x10)) < 0)
    return err;

  /*
   * Enable input available and output FIFO empty interrupts
   */

  ctrl = 0;
  input_opened = 0;
  midi_input_intr = input;

  if (mode == OPEN_READ || mode == OPEN_READWRITE)
    {
      ctrl |= 0x04;		/*
				   * Enable input
				 */
      input_opened = 1;
    }

  if (mode == OPEN_WRITE || mode == OPEN_READWRITE)
    {
      ctrl |= 0x08 |		/*
				   * Enable output
				 */
	0x10;
    }

  pas_write (ctrl,
	     0x178b);

  /*
   * Acknowledge any pending interrupts
   */

  pas_write (0xff, 0x1B88);
  ofifo_bytes = 0;

  restore_flags (flags);

  midi_busy = 1;
  qlen = qhead = qtail = 0;
  return 0;
}

static void
pas_midi_close (int dev)
{

  /*
   * Reset FIFO pointers, disable intrs
   */
  pas_write (0x20 | 0x40, 0x178b);

  pas_remove_intr (0x10);
  midi_busy = 0;
}

static int
dump_to_midi (unsigned char midi_byte)
{
  int             fifo_space, x;

  fifo_space = ((x = pas_read (0x1B89)) >> 4) & 0x0f;

  if (fifo_space == 15 || (fifo_space < 2 && ofifo_bytes > 13))		/*
									   * Fifo
									   * full
									 */
    {
      return 0;			/*
				 * Upper layer will call again
				 */
    }

  ofifo_bytes++;

  pas_write (midi_byte, 0x178A);

  return 1;
}

static int
pas_midi_out (int dev, unsigned char midi_byte)
{

  unsigned long   flags;

  /*
   * Drain the local queue first
   */

  save_flags (flags);
  cli ();

  while (qlen && dump_to_midi (tmp_queue[qhead]))
    {
      qlen--;
      qhead++;
    }

  restore_flags (flags);

  /*
   * Output the byte if the local queue is empty.
   */

  if (!qlen)
    if (dump_to_midi (midi_byte))
      return 1;			/*
				 * OK
				 */

  /*
   * Put to the local queue
   */

  if (qlen >= 256)
    return 0;			/*
				 * Local queue full
				 */

  save_flags (flags);
  cli ();

  tmp_queue[qtail] = midi_byte;
  qlen++;
  qtail++;

  restore_flags (flags);

  return 1;
}

static int
pas_midi_start_read (int dev)
{
  return 0;
}

static int
pas_midi_end_read (int dev)
{
  return 0;
}

static int
pas_midi_ioctl (int dev, unsigned cmd, caddr_t arg)
{
  return -(EINVAL);
}

static void
pas_midi_kick (int dev)
{
  ofifo_bytes = 0;
}

static int
pas_buffer_status (int dev)
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
  pas_midi_ioctl,
  pas_midi_out,
  pas_midi_start_read,
  pas_midi_end_read,
  pas_midi_kick,
  NULL,				/*
				 * command
				 */
  pas_buffer_status,
  NULL
};

void
pas_midi_init (void)
{
  if (num_midis >= MAX_MIDI_DEV)
    {
      printk ("Sound: Too many midi devices detected\n");
      return;
    }

  std_midi_synth.midi_dev = my_dev = num_midis;
  midi_devs[num_midis++] = &pas_midi_operations;
}

void
pas_midi_interrupt (void)
{
  unsigned char   stat;
  int             i, incount;
  unsigned long   flags;

  stat = pas_read (0x1B88);

  if (stat & 0x04)		/*
				   * Input byte available
				 */
    {
      incount = pas_read (0x1B89) & 0x0f;	/*
						   * Input FIFO count
						 */
      if (!incount)
	incount = 16;

      for (i = 0; i < incount; i++)
	if (input_opened)
	  {
	    midi_input_intr (my_dev, pas_read (0x178A));
	  }
	else
	  pas_read (0x178A);	/*
				 * Flush
				 */
    }

  if (stat & (0x08 | 0x10))
    {
      if (!(stat & 0x08))
	{
	  ofifo_bytes = 8;
	}
      else
	{
	  ofifo_bytes = 0;
	}

      save_flags (flags);
      cli ();

      while (qlen && dump_to_midi (tmp_queue[qhead]))
	{
	  qlen--;
	  qhead++;
	}

      restore_flags (flags);
    }


  if (stat & 0x40)
    {
      printk ("MIDI output overrun %x,%x,%d \n", pas_read (0x1B89), stat, ofifo_bytes);
      ofifo_bytes = 100;
    }

  pas_write (stat, 0x1B88);	/*
				   * Acknowledge interrupts
				 */
}

#endif
