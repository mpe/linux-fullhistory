/*
 * sound/midibuf.c
 *
 * Device file manager for /dev/midi#
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

#if defined(CONFIG_MIDI)

/*
 * Don't make MAX_QUEUE_SIZE larger than 4000
 */

#define MAX_QUEUE_SIZE	4000

static wait_handle *midi_sleeper[MAX_MIDI_DEV] =
{NULL};
static volatile struct snd_wait midi_sleep_flag[MAX_MIDI_DEV] =
{
  {0}};
static wait_handle *input_sleeper[MAX_MIDI_DEV] =
{NULL};
static volatile struct snd_wait input_sleep_flag[MAX_MIDI_DEV] =
{
  {0}};

struct midi_buf
  {
    int             len, head, tail;
    unsigned char   queue[MAX_QUEUE_SIZE];
  };

struct midi_parms
  {
    int             prech_timeout;	/*
					 * Timeout before the first ch
					 */
  };

static struct midi_buf *midi_out_buf[MAX_MIDI_DEV] =
{NULL};
static struct midi_buf *midi_in_buf[MAX_MIDI_DEV] =
{NULL};
static struct midi_parms parms[MAX_MIDI_DEV];

static void     midi_poll (unsigned long dummy);


static struct timer_list poll_timer =
{NULL, NULL, 0, 0, midi_poll};
static volatile int open_devs = 0;

#define DATA_AVAIL(q) (q->len)
#define SPACE_AVAIL(q) (MAX_QUEUE_SIZE - q->len)

#define QUEUE_BYTE(q, data) \
	if (SPACE_AVAIL(q)) \
	{ \
	  unsigned long flags; \
	  save_flags(flags);cli(); \
	  q->queue[q->tail] = (data); \
	  q->len++; q->tail = (q->tail+1) % MAX_QUEUE_SIZE; \
	  restore_flags(flags); \
	}

#define REMOVE_BYTE(q, data) \
	if (DATA_AVAIL(q)) \
	{ \
	  unsigned long flags; \
	  save_flags(flags);cli(); \
	  data = q->queue[q->head]; \
	  q->len--; q->head = (q->head+1) % MAX_QUEUE_SIZE; \
	  restore_flags(flags); \
	}

void
drain_midi_queue (int dev)
{

  /*
   * Give the Midi driver time to drain its output queues
   */

  if (midi_devs[dev]->buffer_status != NULL)
    while (!current_got_fatal_signal () &&
	   midi_devs[dev]->buffer_status (dev))

      {
	unsigned long   tlimit;

	if (HZ / 10)
	  current_set_timeout (tlimit = jiffies + (HZ / 10));
	else
	  tlimit = (unsigned long) -1;
	midi_sleep_flag[dev].flags = WK_SLEEP;
	module_interruptible_sleep_on (&midi_sleeper[dev]);
	if (!(midi_sleep_flag[dev].flags & WK_WAKEUP))
	  {
	    if (jiffies >= tlimit)
	      midi_sleep_flag[dev].flags |= WK_TIMEOUT;
	  }
	midi_sleep_flag[dev].flags &= ~WK_SLEEP;
      };
}

static void
midi_input_intr (int dev, unsigned char data)
{
  if (midi_in_buf[dev] == NULL)
    return;

  if (data == 0xfe)		/*
				 * Active sensing
				 */
    return;			/*
				 * Ignore
				 */

  if (SPACE_AVAIL (midi_in_buf[dev]))
    {
      QUEUE_BYTE (midi_in_buf[dev], data);
      if ((input_sleep_flag[dev].flags & WK_SLEEP))
	{
	  input_sleep_flag[dev].flags = WK_WAKEUP;
	  module_wake_up (&input_sleeper[dev]);
	};
    }

}

static void
midi_output_intr (int dev)
{
  /*
   * Currently NOP
   */
}

static void
midi_poll (unsigned long dummy)
{
  unsigned long   flags;
  int             dev;

  save_flags (flags);
  cli ();
  if (open_devs)
    {
      for (dev = 0; dev < num_midis; dev++)
	if (midi_out_buf[dev] != NULL)
	  {
	    while (DATA_AVAIL (midi_out_buf[dev]) &&
		   midi_devs[dev]->putc (dev,
			 midi_out_buf[dev]->queue[midi_out_buf[dev]->head]))
	      {
		midi_out_buf[dev]->head = (midi_out_buf[dev]->head + 1) % MAX_QUEUE_SIZE;
		midi_out_buf[dev]->len--;
	      }

	    if (DATA_AVAIL (midi_out_buf[dev]) < 100 &&
		(midi_sleep_flag[dev].flags & WK_SLEEP))
	      {
		midi_sleep_flag[dev].flags = WK_WAKEUP;
		module_wake_up (&midi_sleeper[dev]);
	      };
	  }

      {
	poll_timer.expires = (1) + jiffies;
	add_timer (&poll_timer);
      };			/*
				   * Come back later
				 */
    }
  restore_flags (flags);
}

int
MIDIbuf_open (int dev, struct fileinfo *file)
{
  int             mode, err;

  dev = dev >> 4;
  mode = file->mode & O_ACCMODE;

  if (num_midis > MAX_MIDI_DEV)
    {
      printk ("Sound: FATAL ERROR: Too many midi interfaces\n");
      num_midis = MAX_MIDI_DEV;
    }

  if (dev < 0 || dev >= num_midis)
    {
      printk ("Sound: Nonexistent MIDI interface %d\n", dev);
      return -(ENXIO);
    }

  /*
     *    Interrupts disabled. Be careful
   */

  if ((err = midi_devs[dev]->open (dev, mode,
				   midi_input_intr, midi_output_intr)) < 0)
    {
      return err;
    }

  parms[dev].prech_timeout = 0;

  midi_in_buf[dev] = (struct midi_buf *) vmalloc (sizeof (struct midi_buf));

  if (midi_in_buf[dev] == NULL)
    {
      printk ("midi: Can't allocate buffer\n");
      midi_devs[dev]->close (dev);
      return -(EIO);
    }
  midi_in_buf[dev]->len = midi_in_buf[dev]->head = midi_in_buf[dev]->tail = 0;

  midi_out_buf[dev] = (struct midi_buf *) vmalloc (sizeof (struct midi_buf));

  if (midi_out_buf[dev] == NULL)
    {
      printk ("midi: Can't allocate buffer\n");
      midi_devs[dev]->close (dev);
      vfree (midi_in_buf[dev]);
      midi_in_buf[dev] = NULL;
      return -(EIO);
    }
  midi_out_buf[dev]->len = midi_out_buf[dev]->head = midi_out_buf[dev]->tail = 0;
  open_devs++;

  midi_sleep_flag[dev].flags = WK_NONE;
  input_sleep_flag[dev].flags = WK_NONE;

  if (open_devs < 2)		/* This was first open */
    {
      ;

      {
	poll_timer.expires = (1) + jiffies;
	add_timer (&poll_timer);
      };			/* Start polling */
    }

  return err;
}

void
MIDIbuf_release (int dev, struct fileinfo *file)
{
  int             mode;
  unsigned long   flags;

  dev = dev >> 4;
  mode = file->mode & O_ACCMODE;

  if (dev < 0 || dev >= num_midis)
    return;

  save_flags (flags);
  cli ();

  /*
     * Wait until the queue is empty
   */

  if (mode != OPEN_READ)
    {
      midi_devs[dev]->putc (dev, 0xfe);		/*
						   * Active sensing to shut the
						   * devices
						 */

      while (!current_got_fatal_signal () &&
	     DATA_AVAIL (midi_out_buf[dev]))

	{
	  unsigned long   tlimit;

	  if (0)
	    current_set_timeout (tlimit = jiffies + (0));
	  else
	    tlimit = (unsigned long) -1;
	  midi_sleep_flag[dev].flags = WK_SLEEP;
	  module_interruptible_sleep_on (&midi_sleeper[dev]);
	  if (!(midi_sleep_flag[dev].flags & WK_WAKEUP))
	    {
	      if (jiffies >= tlimit)
		midi_sleep_flag[dev].flags |= WK_TIMEOUT;
	    }
	  midi_sleep_flag[dev].flags &= ~WK_SLEEP;
	};			/*
				   * Sync
				 */

      drain_midi_queue (dev);	/*
				 * Ensure the output queues are empty
				 */
    }

  restore_flags (flags);

  midi_devs[dev]->close (dev);

  vfree (midi_in_buf[dev]);
  vfree (midi_out_buf[dev]);
  midi_in_buf[dev] = NULL;
  midi_out_buf[dev] = NULL;
  if (open_devs < 2)
    del_timer (&poll_timer);;
  open_devs--;
}

int
MIDIbuf_write (int dev, struct fileinfo *file, const char *buf, int count)
{
  unsigned long   flags;
  int             c, n, i;
  unsigned char   tmp_data;

  dev = dev >> 4;

  if (!count)
    return 0;

  save_flags (flags);
  cli ();

  c = 0;

  while (c < count)
    {
      n = SPACE_AVAIL (midi_out_buf[dev]);

      if (n == 0)		/*
				 * No space just now. We have to sleep
				 */
	{

	  {
	    unsigned long   tlimit;

	    if (0)
	      current_set_timeout (tlimit = jiffies + (0));
	    else
	      tlimit = (unsigned long) -1;
	    midi_sleep_flag[dev].flags = WK_SLEEP;
	    module_interruptible_sleep_on (&midi_sleeper[dev]);
	    if (!(midi_sleep_flag[dev].flags & WK_WAKEUP))
	      {
		if (jiffies >= tlimit)
		  midi_sleep_flag[dev].flags |= WK_TIMEOUT;
	      }
	    midi_sleep_flag[dev].flags &= ~WK_SLEEP;
	  };
	  if (current_got_fatal_signal ())
	    {
	      restore_flags (flags);
	      return -(EINTR);
	    }

	  n = SPACE_AVAIL (midi_out_buf[dev]);
	}

      if (n > (count - c))
	n = count - c;

      for (i = 0; i < n; i++)
	{
	  memcpy_fromfs ((char *) &tmp_data, &(buf)[c], 1);
	  QUEUE_BYTE (midi_out_buf[dev], tmp_data);
	  c++;
	}
    }

  restore_flags (flags);

  return c;
}


int
MIDIbuf_read (int dev, struct fileinfo *file, char *buf, int count)
{
  int             n, c = 0;
  unsigned long   flags;
  unsigned char   tmp_data;

  dev = dev >> 4;

  save_flags (flags);
  cli ();

  if (!DATA_AVAIL (midi_in_buf[dev]))	/*
					 * No data yet, wait
					 */
    {

      {
	unsigned long   tlimit;

	if (parms[dev].prech_timeout)
	  current_set_timeout (tlimit = jiffies + (parms[dev].prech_timeout));
	else
	  tlimit = (unsigned long) -1;
	input_sleep_flag[dev].flags = WK_SLEEP;
	module_interruptible_sleep_on (&input_sleeper[dev]);
	if (!(input_sleep_flag[dev].flags & WK_WAKEUP))
	  {
	    if (jiffies >= tlimit)
	      input_sleep_flag[dev].flags |= WK_TIMEOUT;
	  }
	input_sleep_flag[dev].flags &= ~WK_SLEEP;
      };
      if (current_got_fatal_signal ())
	c = -(EINTR);		/*
				   * The user is getting restless
				 */
    }

  if (c == 0 && DATA_AVAIL (midi_in_buf[dev]))	/*
						 * Got some bytes
						 */
    {
      n = DATA_AVAIL (midi_in_buf[dev]);
      if (n > count)
	n = count;
      c = 0;

      while (c < n)
	{
	  REMOVE_BYTE (midi_in_buf[dev], tmp_data);
	  memcpy_tofs (&(buf)[c], (char *) &tmp_data, 1);
	  c++;
	}
    }

  restore_flags (flags);

  return c;
}

int
MIDIbuf_ioctl (int dev, struct fileinfo *file,
	       unsigned int cmd, caddr_t arg)
{
  int             val;

  dev = dev >> 4;

  if (((cmd >> 8) & 0xff) == 'C')
    {
      if (midi_devs[dev]->coproc)	/* Coprocessor ioctl */
	return midi_devs[dev]->coproc->ioctl (midi_devs[dev]->coproc->devc, cmd, arg, 0);
      else
	printk ("/dev/midi%d: No coprocessor for this device\n", dev);

      return -(ENXIO);
    }
  else
    switch (cmd)
      {

      case SNDCTL_MIDI_PRETIME:
	val = (int) get_fs_long ((long *) arg);
	if (val < 0)
	  val = 0;

	val = (HZ * val) / 10;
	parms[dev].prech_timeout = val;
	return snd_ioctl_return ((int *) arg, val);
	break;

      default:
	return midi_devs[dev]->ioctl (dev, cmd, arg);
      }
}

int
MIDIbuf_select (int dev, struct fileinfo *file, int sel_type, select_table_handle * wait)
{
  dev = dev >> 4;

  switch (sel_type)
    {
    case SEL_IN:
      if (!DATA_AVAIL (midi_in_buf[dev]))
	{

	  input_sleep_flag[dev].flags = WK_SLEEP;
	  module_select_wait (&input_sleeper[dev], wait);
	  return 0;
	}
      return 1;
      break;

    case SEL_OUT:
      if (SPACE_AVAIL (midi_out_buf[dev]))
	{

	  midi_sleep_flag[dev].flags = WK_SLEEP;
	  module_select_wait (&midi_sleeper[dev], wait);
	  return 0;
	}
      return 1;
      break;

    case SEL_EX:
      return 0;
    }

  return 0;
}


void
MIDIbuf_init (void)
{
}

#endif
