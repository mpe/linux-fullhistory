/*
 *  linux/arch/m68k/hp300/config.c
 *
 *  Copyright (C) 1998 Philip Blundell <philb@gnu.org>
 *
 *  This file contains the HP300-specific initialisation code.  It gets
 *  called by setup.c.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <asm/machdep.h>
#include <asm/blinken.h>
#include <asm/io.h>                               /* readb() and writeb() */
#include <asm/hwtest.h>                           /* hwreg_present() */

#include "ints.h"
#include "time.h"

extern void hp300_reset(void);
extern void hp300_hil_init(void);

#ifdef CONFIG_HEARTBEAT
static void hp300_pulse(int x)
{
   if (x)
      blinken_leds(0xfe);
   else
      blinken_leds(0xff);
}
#endif

static int hp300_kbdrate(struct kbd_repeat *k)
{
  return 0;
}

static void hp300_kbd_leds(unsigned int leds)
{
}

__initfunc(void config_hp300(void))
{
  mach_sched_init      = hp300_sched_init;
  mach_keyb_init       = hp300_hil_init;
  mach_kbdrate         = hp300_kbdrate;
  mach_kbd_leds        = hp300_kbd_leds;
  mach_init_IRQ        = hp300_init_IRQ;
  mach_request_irq     = hp300_request_irq;
  mach_free_irq        = hp300_free_irq;
#if 0
  mach_get_model       = hp300_get_model;
  mach_get_irq_list    = hp300_get_irq_list;
#endif
  mach_gettimeoffset   = hp300_gettimeoffset;
#if 0
  mach_gettod          = hp300_gettod;
#endif
  mach_reset           = hp300_reset;
#ifdef CONFIG_HEARTBEAT
  mach_heartbeat       = hp300_pulse;
#endif
#ifdef CONFIG_DUMMY_CONSOLE
  conswitchp	       = &dummy_con;
#endif
  mach_max_dma_address = 0xffffffff;
}

/* for "kbd-reset" cmdline param */
__initfunc(void kbd_reset_setup(char *str, int *ints))
{
}
