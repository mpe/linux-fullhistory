
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

#include <linux/module.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <asm/irq.h>
#include <asm/setup.h>
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#endif
#ifdef CONFIG_ATARI
#include <linux/delay.h>
#include <linux/sched.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#endif
#include <linux/lp_intern.h>

static int my_inter = 0;
static int minor = -1;

static void lp_int_out(int, int);
static int lp_int_busy(int);
static int lp_int_pout(int);
static int lp_int_online(int);


static void
lp_int_out (int c, int dev)
{
  switch (m68k_machtype)
    {
#ifdef CONFIG_AMIGA
    case MACH_AMIGA:
       {
	int wait = 0;
	while (wait != lp_table[dev]->wait) wait++;
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
	 while (wait != lp_table[dev]->wait) wait++;
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
  switch (m68k_machtype)
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
  switch (m68k_machtype)
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
  switch (m68k_machtype)
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

static int lp_int_my_interrupt(int dev)
{
  return my_inter;
}

static void lp_int_interrupt(int irq, void *data, struct pt_regs *fp)
{
  my_inter = 1;
  lp_interrupt(irq, data, fp);
  my_inter = 0;
}

static void lp_int_open(void)
{
  MOD_INC_USE_COUNT;
}

static void lp_int_release(void)
{
  MOD_DEC_USE_COUNT;
}

static struct lp_struct tab = {
	"Builtin parallel port",	/* name */
	0,				/* irq */
	lp_int_out,
	lp_int_busy,
	lp_int_pout,
	lp_int_online,
	lp_int_my_interrupt,
	NULL,				/* ioctl */
	lp_int_open,
	lp_int_release,
	LP_EXIST,
	LP_INIT_CHAR,
	LP_INIT_TIME,
	LP_INIT_WAIT,
	NULL,
	NULL,
};

int lp_internal_init(void)
{
#ifdef CONFIG_AMIGA
  if (MACH_IS_AMIGA && AMIGAHW_PRESENT(AMI_PARALLEL))
    {
      ciaa.ddrb = 0xff;
      ciab.ddra &= 0xf8;
      if (lp_irq)
        tab.irq = request_irq(IRQ_AMIGA_CIAA_FLG, lp_int_interrupt,
          0, "builtin printer port", lp_int_interrupt);
      tab.base = (void *) &ciaa.prb; /* dummy, not used */
      tab.type = LP_AMIGA;
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
      if (lp_irq)
        tab.irq = request_irq(IRQ_MFP_BUSY, lp_int_interrupt,
          IRQ_TYPE_SLOW, "builtin printer port", lp_int_interrupt);
      tab.base = (void *) &sound_ym.wd_data; /* dummy, not used */
      tab.type = LP_ATARI;
    }
#endif

  if ((minor = register_parallel(&tab, minor)) < 0) {
    printk("builtin lp init: cant get a minor\n");
    if (lp_irq) {
#ifdef CONFIG_AMIGA
      if (MACH_IS_AMIGA)
	free_irq(IRQ_AMIGA_CIAA_FLG, lp_int_interrupt);
#endif
#ifdef CONFIG_ATARI
      if (MACH_IS_ATARI)
	free_irq(IRQ_MFP_BUSY, lp_int_interrupt);
#endif
    }
    return -ENODEV;
  }
  
  return 0;
}

#ifdef MODULE
int init_module(void)
{
return lp_internal_init();
}

void cleanup_module(void)
{
if (lp_irq) {
#ifdef CONFIG_AMIGA
  if (MACH_IS_AMIGA)
    free_irq(IRQ_AMIGA_CIAA_FLG, lp_int_interrupt);
#endif
#ifdef CONFIG_ATARI
  if (MACH_IS_ATARI)
    free_irq(IRQ_MFP_BUSY, lp_int_interrupt);
#endif
}
unregister_parallel(minor);
}
#endif
