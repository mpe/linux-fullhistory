
/*
 * split into mid and low-level for better support of different hardware
 * by Joerg Dorchain (dorchain@mpi-sb.mpg.de)
 *
 * Amiga printer device by Michael Rausch (linux@uni-koblenz.de);
 * Atari support added by Andreas Schwab (schwab@ls5.informatik.uni-dortmund.de);
 * based upon work from
 *
 * Copyright (C) 1992 by Jim Weigand and Linus Torvalds
 * Copyright (C) 1992,1993 by Michael K. Johnson
 * - Thanks much to Gunter Windau for pointing out to me where the error
 *   checking ought to be.
 * Copyright (C) 1993 by Nigel Gamble (added interrupt code)
 */

#include <linux/config.h>
#include <linux/lp_intern.h>
#include <linux/kernel.h>
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#endif
#ifdef CONFIG_ATARI
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/atarihw.h>
#endif


static void lp_int_out(int, int);
static int lp_int_busy(int);
static int lp_int_pout(int);
static int lp_int_online(int);
static int lp_int_interrupt(int);

int lp_internal_init(struct lp_struct *, int, int, int);


static void
lp_int_out (int c, int dev)
{
  switch (boot_info.machtype)
    {
#ifdef CONFIG_AMIGA
    case MACH_AMIGA:
       {
	int wait = 0;
	while (wait != lp_table[dev].wait) wait++;
	ciaa.prb = c;
       }
      break;
#endif
#ifdef CONFIG_ATARI
    case MACH_ATARI:
       {
	 int wait = 0;
	 sound_ym.rd_data_reg_sel = 15;
	 sound_ym.wd_data = c;
	 sound_ym.rd_data_reg_sel = 14;
	 while (wait != lp_table[dev].wait) wait++;
	 sound_ym.wd_data = sound_ym.rd_data_reg_sel & ~(1 << 5);
	 while (wait) wait--;
	 sound_ym.wd_data = sound_ym.rd_data_reg_sel | (1 << 5);
	 break;
       }
#endif
    }
}

static int
lp_int_busy (int dev)
{
  switch (boot_info.machtype)
    {
#ifdef CONFIG_AMIGA
    case MACH_AMIGA:
      return ciab.pra & 1;
#endif
#ifdef CONFIG_ATARI
    case MACH_ATARI:
      return mfp.par_dt_reg & 1;
#endif
    default:
      return 0;
    }
}

static int
lp_int_pout (int dev)
{
  switch (boot_info.machtype)
    {
#ifdef CONFIG_AMIGA
    case MACH_AMIGA:
      return ciab.pra & 2;
#endif
#ifdef CONFIG_ATARI
    case MACH_ATARI:
#endif
    default:
      return 0;
    }
}

static int
lp_int_online (int dev)
{
  switch (boot_info.machtype)
    {
#ifdef CONFIG_AMIGA
    case MACH_AMIGA:
      return ciab.pra & 4;
#endif
#ifdef CONFIG_ATARI
    case MACH_ATARI:
      return !(mfp.par_dt_reg & 1);
#endif
    default:
      return 0;
    }
}

static int lp_int_interrupt(int dev)
{
  return 1;
}

int lp_internal_init(struct lp_struct *lp_table, int entry,
		     int max_lp, int irq)
{
  if (max_lp-entry < 1)
    return 0;
#ifdef CONFIG_AMIGA
  if (MACH_IS_AMIGA)
    {
      ciaa.ddrb = 0xff;
      ciab.ddra &= 0xf8;
    }
#endif
#ifdef CONFIG_ATARI
  if (MACH_IS_ATARI)
    {
      unsigned long flags;

      save_flags(flags);
      cli();
      sound_ym.rd_data_reg_sel = 7;
      sound_ym.wd_data = (sound_ym.rd_data_reg_sel & 0x3f) | 0xc0;
      restore_flags(flags);
    }
#endif
  lp_table[entry].name = "Builtin LP";
  lp_table[entry].lp_out = lp_int_out;
  lp_table[entry].lp_is_busy = lp_int_busy;
  lp_table[entry].lp_has_pout = lp_int_pout;
  lp_table[entry].lp_is_online = lp_int_online;
  lp_table[entry].lp_my_interrupt = lp_int_interrupt;
  lp_table[entry].flags = LP_EXIST;
  lp_table[entry].chars = LP_INIT_CHAR;
  lp_table[entry].time = LP_INIT_TIME;
  lp_table[entry].wait = LP_INIT_WAIT;
  lp_table[entry].lp_wait_q = NULL;

  printk("lp%d: internal port\n", entry);

  return 1;
}

