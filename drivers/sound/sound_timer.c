/*
 * sound/sound_timer.c
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

#if defined(CONFIG_SEQUENCER)

static volatile int initialized = 0, opened = 0, tmr_running = 0;
static volatile time_t tmr_offs, tmr_ctr;
static volatile unsigned long ticks_offs;
static volatile int curr_tempo, curr_timebase;
static volatile unsigned long curr_ticks;
static volatile unsigned long next_event_time;
static unsigned long prev_event_time;
static volatile unsigned long usecs_per_tmr;	/* Length of the current interval */

static struct sound_lowlev_timer *tmr = NULL;

static unsigned long
tmr2ticks (int tmr_value)
{
  /*
     *    Convert timer ticks to MIDI ticks
   */

  unsigned long   tmp;
  unsigned long   scale;

  tmp = tmr_value * usecs_per_tmr;	/* Convert to usecs */

  scale = (60 * 1000000) / (curr_tempo * curr_timebase);	/* usecs per MIDI tick */

  return (tmp + (scale / 2)) / scale;
}

static void
reprogram_timer (void)
{
  unsigned long   usecs_per_tick;

  usecs_per_tick = (60 * 1000000) / (curr_tempo * curr_timebase);

  /*
     * Don't kill the system by setting too high timer rate
   */
  if (usecs_per_tick < 2000)
    usecs_per_tick = 2000;

  usecs_per_tmr = tmr->tmr_start (tmr->dev, usecs_per_tick);
}

void
sound_timer_syncinterval (unsigned int new_usecs)
{
/*
 *    This routine is called by the hardware level if
 *      the clock frequency has changed for some reason.
 */
  tmr_offs = tmr_ctr;
  ticks_offs += tmr2ticks (tmr_ctr);
  tmr_ctr = 0;

  usecs_per_tmr = new_usecs;
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
timer_open (int dev, int mode)
{
  if (opened)
    return -(EBUSY);

  tmr_reset ();
  curr_tempo = 60;
  curr_timebase = 100;
  opened = 1;
  reprogram_timer ();

  return 0;
}

static void
timer_close (int dev)
{
  opened = tmr_running = 0;
  tmr->tmr_disable (tmr->dev);
}

static int
timer_event (int dev, unsigned char *event)
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
      reprogram_timer ();
      break;

    case TMR_STOP:
      tmr_running = 0;
      break;

    case TMR_CONTINUE:
      tmr_running = 1;
      reprogram_timer ();
      break;

    case TMR_TEMPO:
      if (parm)
	{
	  if (parm < 8)
	    parm = 8;
	  if (parm > 250)
	    parm = 250;
	  tmr_offs = tmr_ctr;
	  ticks_offs += tmr2ticks (tmr_ctr);
	  tmr_ctr = 0;
	  curr_tempo = parm;
	  reprogram_timer ();
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
timer_get_time (int dev)
{
  if (!opened)
    return 0;

  return curr_ticks;
}

static int
timer_ioctl (int dev,
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
	int             val = get_fs_long ((long *) arg);

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
	int             val = get_fs_long ((long *) arg);

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
	    reprogram_timer ();
	  }

	return snd_ioctl_return ((int *) arg, curr_tempo);
      }
      break;

    case SNDCTL_SEQ_CTRLRATE:
      if (get_fs_long ((long *) arg) != 0)	/* Can't change */
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
timer_arm (int dev, long time)
{
  if (time < 0)
    time = curr_ticks + 1;
  else if (time <= curr_ticks)	/* It's the time */
    return;

  next_event_time = prev_event_time = time;

  return;
}

static struct sound_timer_operations sound_timer =
{
  {"GUS Timer", 0},
  1,				/* Priority */
  0,				/* Local device link */
  timer_open,
  timer_close,
  timer_event,
  timer_get_time,
  timer_ioctl,
  timer_arm
};

void
sound_timer_interrupt (void)
{
  if (!opened)
    return;

  tmr->tmr_restart (tmr->dev);

  if (!tmr_running)
    return;

  tmr_ctr++;
  curr_ticks = ticks_offs + tmr2ticks (tmr_ctr);

  if (curr_ticks >= next_event_time)
    {
      next_event_time = (unsigned long) -1;
      sequencer_timer (0);
    }
}

void
sound_timer_init (struct sound_lowlev_timer *t, char *name)
{
  int             n;

  if (initialized || t == NULL)
    return;			/* There is already a similar timer */

  initialized = 1;
  tmr = t;

  if (num_sound_timers >= MAX_TIMER_DEV)
    n = 0;			/* Overwrite the system timer */
  else
    n = num_sound_timers++;

  strcpy (sound_timer.info.name, name);

  sound_timer_devs[n] = &sound_timer;
}

#endif
