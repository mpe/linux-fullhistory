/*
 * sound/patmgr.c
 *
 * The patch maneger interface for the /dev/sequencer
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>


#define PATMGR_C
#include "sound_config.h"

#if defined(CONFIG_SEQUENCER)

static wait_handle *server_procs[MAX_SYNTH_DEV] =
{NULL};
static volatile struct snd_wait server_wait_flag[MAX_SYNTH_DEV] =
{
  {0}};

static struct patmgr_info *mbox[MAX_SYNTH_DEV] =
{NULL};
static volatile int msg_direction[MAX_SYNTH_DEV] =
{0};

static int      pmgr_opened[MAX_SYNTH_DEV] =
{0};

#define A_TO_S	1
#define S_TO_A 	2

static wait_handle *appl_proc = NULL;
static volatile struct snd_wait appl_wait_flag =
{0};

int
pmgr_open (int dev)
{
  if (dev < 0 || dev >= num_synths)
    return -(ENXIO);

  if (pmgr_opened[dev])
    return -(EBUSY);
  pmgr_opened[dev] = 1;

  server_wait_flag[dev].flags = WK_NONE;

  return 0;
}

void
pmgr_release (int dev)
{

  if (mbox[dev])		/*
				 * Killed in action. Inform the client
				 */
    {

      mbox[dev]->key = PM_ERROR;
      mbox[dev]->parm1 = -(EIO);

      if ((appl_wait_flag.flags & WK_SLEEP))
	{
	  appl_wait_flag.flags = WK_WAKEUP;
	  module_wake_up (&appl_proc);
	};
    }

  pmgr_opened[dev] = 0;
}

int
pmgr_read (int dev, struct fileinfo *file, char *buf, int count)
{
  unsigned long   flags;
  int             ok = 0;

  if (count != sizeof (struct patmgr_info))
    {
      printk ("PATMGR%d: Invalid read count\n", dev);
      return -(EIO);
    }

  while (!ok && !current_got_fatal_signal ())
    {
      save_flags (flags);
      cli ();

      while (!(mbox[dev] && msg_direction[dev] == A_TO_S) &&
	     !current_got_fatal_signal ())
	{

	  server_wait_flag[dev].flags = WK_SLEEP;
	  module_interruptible_sleep_on (&server_procs[dev]);
	  server_wait_flag[dev].flags &= ~WK_SLEEP;;
	}

      if (mbox[dev] && msg_direction[dev] == A_TO_S)
	{
	  memcpy_tofs (&(buf)[0], (char *) mbox[dev], count);
	  msg_direction[dev] = 0;
	  ok = 1;
	}

      restore_flags (flags);

    }

  if (!ok)
    return -(EINTR);
  return count;
}

int
pmgr_write (int dev, struct fileinfo *file, const char *buf, int count)
{
  unsigned long   flags;

  if (count < 4)
    {
      printk ("PATMGR%d: Write count < 4\n", dev);
      return -(EIO);
    }

  memcpy_fromfs ((char *) mbox[dev], &(buf)[0], 4);

  if (*(unsigned char *) mbox[dev] == SEQ_FULLSIZE)
    {
      int             tmp_dev;

      tmp_dev = ((unsigned short *) mbox[dev])[2];
      if (tmp_dev != dev)
	return -(ENXIO);

      return synth_devs[dev]->load_patch (dev, *(unsigned short *) mbox[dev],
					  buf, 4, count, 1);
    }

  if (count != sizeof (struct patmgr_info))
    {
      printk ("PATMGR%d: Invalid write count\n", dev);
      return -(EIO);
    }

  /*
   * If everything went OK, there should be a preallocated buffer in the
   * mailbox and a client waiting.
   */

  save_flags (flags);
  cli ();

  if (mbox[dev] && !msg_direction[dev])
    {
      memcpy_fromfs (&((char *) mbox[dev])[4], &(buf)[4], count - 4);
      msg_direction[dev] = S_TO_A;

      if ((appl_wait_flag.flags & WK_SLEEP))
	{
	  {
	    appl_wait_flag.flags = WK_WAKEUP;
	    module_wake_up (&appl_proc);
	  };
	}
    }

  restore_flags (flags);

  return count;
}

int
pmgr_access (int dev, struct patmgr_info *rec)
{
  unsigned long   flags;
  int             err = 0;

  save_flags (flags);
  cli ();

  if (mbox[dev])
    printk ("  PATMGR: Server %d mbox full. Why?\n", dev);
  else
    {
      rec->key = PM_K_COMMAND;
      mbox[dev] = rec;
      msg_direction[dev] = A_TO_S;

      if ((server_wait_flag[dev].flags & WK_SLEEP))
	{
	  {
	    server_wait_flag[dev].flags = WK_WAKEUP;
	    module_wake_up (&server_procs[dev]);
	  };
	}


      appl_wait_flag.flags = WK_SLEEP;
      module_interruptible_sleep_on (&appl_proc);
      appl_wait_flag.flags &= ~WK_SLEEP;;

      if (msg_direction[dev] != S_TO_A)
	{
	  rec->key = PM_ERROR;
	  rec->parm1 = -(EIO);
	}
      else if (rec->key == PM_ERROR)
	{
	  err = rec->parm1;
	  if (err > 0)
	    err = -err;
	}

      mbox[dev] = NULL;
      msg_direction[dev] = 0;
    }

  restore_flags (flags);

  return err;
}

int
pmgr_inform (int dev, int event, unsigned long p1, unsigned long p2,
	     unsigned long p3, unsigned long p4)
{
  unsigned long   flags;
  int             err = 0;

  struct patmgr_info *tmp_mbox;

  if (!pmgr_opened[dev])
    return 0;

  tmp_mbox = (struct patmgr_info *) vmalloc (sizeof (struct patmgr_info));

  if (tmp_mbox == NULL)
    {
      printk ("pmgr: Couldn't allocate memory for a message\n");
      return 0;
    }

  save_flags (flags);
  cli ();

  if (mbox[dev])
    printk ("  PATMGR: Server %d mbox full. Why?\n", dev);
  else
    {

      mbox[dev] = tmp_mbox;
      mbox[dev]->key = PM_K_EVENT;
      mbox[dev]->command = event;
      mbox[dev]->parm1 = p1;
      mbox[dev]->parm2 = p2;
      mbox[dev]->parm3 = p3;
      msg_direction[dev] = A_TO_S;

      if ((server_wait_flag[dev].flags & WK_SLEEP))
	{
	  {
	    server_wait_flag[dev].flags = WK_WAKEUP;
	    module_wake_up (&server_procs[dev]);
	  };
	}


      appl_wait_flag.flags = WK_SLEEP;
      module_interruptible_sleep_on (&appl_proc);
      appl_wait_flag.flags &= ~WK_SLEEP;;
      mbox[dev] = NULL;
      msg_direction[dev] = 0;
    }

  restore_flags (flags);
  vfree (tmp_mbox);

  return err;
}

#endif
