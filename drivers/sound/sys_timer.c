/*
 * sound/sys_timer.c
 *
 * The default timer for the Level 2 sequencer interface
 * Uses the (1/HZ sec) timer of kernel.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>


#define SEQUENCER_C
#include "sound_config.h"

#ifdef CONFIG_SEQUENCER

static volatile int opened = 0, tmr_running = 0;
static volatile time_t tmr_offs, tmr_ctr;
static volatile unsigned long ticks_offs;
static volatile int curr_tempo, curr_timebase;
static volatile unsigned long curr_ticks;
static volatile unsigned long next_event_time;
static unsigned long prev_event_time;

static void     poll_def_tmr (unsigned long dummy);


static struct timer_list def_tmr =
{NULL, NULL, 0, 0, poll_def_tmr};

static unsigned long
tmr2ticks (int tmr_value)
{
  /*
   *    Convert system timer ticks (HZ) to MIDI ticks
   *    (divide # of MIDI ticks/minute by # of system ticks/minute).
   */

  return ((tmr_value * curr_tempo * curr_timebase) + (30 * 100)) / (60 * HZ);
}

static void
poll_def_tmr (unsigned long dummy)
{

  if (opened)
    {

      {
	def_tmr.expires = (1) + jiffies;
	add_timer (&def_tmr);
      };

      if (tmr_running)
	{
	  tmr_ctr++;
	  curr_ticks = ticks_offs + tmr2ticks (tmr_ctr);

	  if (curr_ticks >= next_event_time)
	    {
	      next_event_time = (unsigned long) -1;
	      sequencer_timer (0);
	    }
	}
    }
}

static void
tmr_reset (void)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();
  tmr_offs = 0;
  ticks_offs = 0;
  tmr_ctr = 0;
  next_event_time = (unsigned long) -1;
  prev_event_time = 0;
  curr_ticks = 0;
  restore_flags (flags);
}

static int
def_tmr_open (int dev, int mode)
{
  if (opened)
    return -(EBUSY);

  tmr_reset ();
  curr_tempo = 60;
  curr_timebase = 100;
  opened = 1;

  ;

  {
    def_tmr.expires = (1) + jiffies;
    add_timer (&def_tmr);
  };

  return 0;
}

static void
def_tmr_close (int dev)
{
  opened = tmr_running = 0;
  del_timer (&def_tmr);;
}

static int
def_tmr_event (int dev, unsigned char *event)
{
  unsigned char   cmd = event[1];
  unsigned long   parm = *(int *) &event[4];

  switch (cmd)
    {
    case TMR_WAIT_REL:
      parm += prev_event_time;
    case TMR_WAIT_ABS:
      if (parm > 0)
	{
	  long            time;

	  if (parm <= curr_ticks)	/* It's the time */
	    return TIMER_NOT_ARMED;

	  time = parm;
	  next_event_time = prev_event_time = time;

	  return TIMER_ARMED;
	}
      break;

    case TMR_START:
      tmr_reset ();
      tmr_running = 1;
      break;

    case TMR_STOP:
      tmr_running = 0;
      break;

    case TMR_CONTINUE:
      tmr_running = 1;
      break;

    case TMR_TEMPO:
      if (parm)
	{
	  if (parm < 8)
	    parm = 8;
	  if (parm > 360)
	    parm = 360;
	  tmr_offs = tmr_ctr;
	  ticks_offs += tmr2ticks (tmr_ctr);
	  tmr_ctr = 0;
	  curr_tempo = parm;
	}
      break;

    case TMR_ECHO:
      seq_copy_to_input (event, 8);
      break;

    default:;
    }

  return TIMER_NOT_ARMED;
}

static unsigned long
def_tmr_get_time (int dev)
{
  if (!opened)
    return 0;

  return curr_ticks;
}

static int
def_tmr_ioctl (int dev,
	       unsigned int cmd, caddr_t arg)
{
  switch (cmd)
    {
    case SNDCTL_TMR_SOURCE:
      return snd_ioctl_return ((int *) arg, TMR_INTERNAL);
      break;

    case SNDCTL_TMR_START:
      tmr_reset ();
      tmr_running = 1;
      return 0;
      break;

    case SNDCTL_TMR_STOP:
      tmr_running = 0;
      return 0;
      break;

    case SNDCTL_TMR_CONTINUE:
      tmr_running = 1;
      return 0;
      break;

    case SNDCTL_TMR_TIMEBASE:
      {
	int             val = get_user ((int *) arg);

	if (val)
	  {
	    if (val < 1)
	      val = 1;
	    if (val > 1000)
	      val = 1000;
	    curr_timebase = val;
	  }

	return snd_ioctl_return ((int *) arg, curr_timebase);
      }
      break;

    case SNDCTL_TMR_TEMPO:
      {
	int             val = get_user ((int *) arg);

	if (val)
	  {
	    if (val < 8)
	      val = 8;
	    if (val > 250)
	      val = 250;
	    tmr_offs = tmr_ctr;
	    ticks_offs += tmr2ticks (tmr_ctr);
	    tmr_ctr = 0;
	    curr_tempo = val;
	  }

	return snd_ioctl_return ((int *) arg, curr_tempo);
      }
      break;

    case SNDCTL_SEQ_CTRLRATE:
      if (get_user ((int *) arg) != 0)	/* Can't change */
	return -(EINVAL);

      return snd_ioctl_return ((int *) arg, ((curr_tempo * curr_timebase) + 30) / 60);
      break;

    case SNDCTL_TMR_METRONOME:
      /* NOP */
      break;

    default:;
    }

  return -(EINVAL);
}

static void
def_tmr_arm (int dev, long time)
{
  if (time < 0)
    time = curr_ticks + 1;
  else if (time <= curr_ticks)	/* It's the time */
    return;

  next_event_time = prev_event_time = time;

  return;
}

struct sound_timer_operations default_sound_timer =
{
  {"System clock", 0},
  0,				/* Priority */
  0,				/* Local device link */
  def_tmr_open,
  def_tmr_close,
  def_tmr_event,
  def_tmr_get_time,
  def_tmr_ioctl,
  def_tmr_arm
};

#endif
