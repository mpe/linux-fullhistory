/*
 * sound/patmgr.c
 *
 * The patch manager interface for the /dev/sequencer
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
    return -ENXIO;

  if (pmgr_opened[dev])
    return -EBUSY;
  pmgr_opened[dev] = 1;

  server_wait_flag[dev].mode = WK_NONE;

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
      mbox[dev]->parm1 = -EIO;

      if ((appl_wait_flag.mode & WK_SLEEP))
	{
	  appl_wait_flag.mode = WK_WAKEUP;
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
      return -EIO;
    }

  while (!ok && !current_got_fatal_signal ())
    {
      save_flags (flags);
      cli ();

      while (!(mbox[dev] && msg_direction[dev] == A_TO_S) &&
	     !current_got_fatal_signal ())
	{

	  server_wait_flag[dev].mode = WK_SLEEP;
	  module_interruptible_sleep_on (&server_procs[dev]);
	  server_wait_flag[dev].mode &= ~WK_SLEEP;;
	}

      if (mbox[dev] && msg_direction[dev] == A_TO_S)
	{
	  memcpy_tofs (&((buf)[0]), (char *) mbox[dev], count);
	  msg_direction[dev] = 0;
	  ok = 1;
	}

      restore_flags (flags);

    }

  if (!ok)
    return -EINTR;
  return count;
}

int
pmgr_write (int dev, struct fileinfo *file, const char *buf, int count)
{
  unsigned long   flags;

  if (count < 4)
    {
      printk ("PATMGR%d: Write count < 4\n", dev);
      return -EIO;
    }

  memcpy_fromfs ((char *) mbox[dev], &((buf)[0]), 4);

  if (*(unsigned char *) mbox[dev] == SEQ_FULLSIZE)
    {
      int             tmp_dev;

      tmp_dev = ((unsigned short *) mbox[dev])[2];
      if (tmp_dev != dev)
	return -ENXIO;

      return synth_devs[dev]->load_patch (dev, *(unsigned short *) mbox[dev],
					  buf, 4, count, 1);
    }

  if (count != sizeof (struct patmgr_info))
    {
      printk ("PATMGR%d: Invalid write count\n", dev);
      return -EIO;
    }

  /*
   * If everything went OK, there should be a preallocated buffer in the
   * mailbox and a client waiting.
   */

  save_flags (flags);
  cli ();

  if (mbox[dev] && !msg_direction[dev])
    {
      memcpy_fromfs (&((char *) mbox[dev])[4], &((buf)[4]), count - 4);
      msg_direction[dev] = S_TO_A;

      if ((appl_wait_flag.mode & WK_SLEEP))
	{
	  {
	    appl_wait_flag.mode = WK_WAKEUP;
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

      if ((server_wait_flag[dev].mode & WK_SLEEP))
	{
	  {
	    server_wait_flag[dev].mode = WK_WAKEUP;
	    module_wake_up (&server_procs[dev]);
	  };
	}


      appl_wait_flag.mode = WK_SLEEP;
      module_interruptible_sleep_on (&appl_proc);
      appl_wait_flag.mode &= ~WK_SLEEP;;

      if (msg_direction[dev] != S_TO_A)
	{
	  rec->key = PM_ERROR;
	  rec->parm1 = -EIO;
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

  tmp_mbox = (struct patmgr_info *) kmalloc (sizeof (struct patmgr_info), GFP_KERNEL);

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

      if ((server_wait_flag[dev].mode & WK_SLEEP))
	{
	  {
	    server_wait_flag[dev].mode = WK_WAKEUP;
	    module_wake_up (&server_procs[dev]);
	  };
	}


      appl_wait_flag.mode = WK_SLEEP;
      module_interruptible_sleep_on (&appl_proc);
      appl_wait_flag.mode &= ~WK_SLEEP;;
      mbox[dev] = NULL;
      msg_direction[dev] = 0;
    }

  restore_flags (flags);
  kfree (tmp_mbox);

  return err;
}

#endif
