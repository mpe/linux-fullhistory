/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: oak_setup.c
 *
 *    Description:
 *      Architecture- / platform-specific boot-time initialization code for
 *      the IBM PowerPC 403GCX "Oak" evaluation board. Adapted from original
 *      code by Gary Thomas, Cort Dougan <cort@cs.nmt.edu>, and Dan Malek
 *      <dmalek@jlc.net>.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/string.h>

#include <asm/machdep.h>
#include <asm/page.h>

#include "oak_setup.h"


void __init
oak_init(unsigned long r3, unsigned long r4, unsigned long r5, 
	 unsigned long r6, unsigned long r7)
{
#if 0
#if defined(CONFIG_BLK_DEV_INITRD)
	/*
	 * If the init RAM disk has been configured in, and there's a valid
	 * starting address for it, set it up.
	 */
	if (r4) {
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	/* Copy the kernel command line arguments to a safe place. */

	if (r6) {
 		*(char *)(r7 + KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6 + KERNELBASE));
	}
#endif /* 0 */

	ppc_md.setup_arch	 = oak_setup_arch;
	ppc_md.setup_residual	 = NULL;
	ppc_md.get_cpuinfo	 = NULL;
	ppc_md.irq_cannonicalize = NULL;
	ppc_md.init_IRQ		 = NULL;
	ppc_md.get_irq		 = NULL;
	ppc_md.init		 = NULL;

	ppc_md.restart		 = NULL;
	ppc_md.power_off	 = NULL;
	ppc_md.halt		 = NULL;

	ppc_md.time_init	 = NULL;
	ppc_md.set_rtc_time	 = NULL;
	ppc_md.get_rtc_time	 = NULL;
	ppc_md.calibrate_decr	 = NULL;

	ppc_md.kbd_setkeycode    = NULL;
	ppc_md.kbd_getkeycode    = NULL;
	ppc_md.kbd_translate     = NULL;
	ppc_md.kbd_unexpected_up = NULL;
	ppc_md.kbd_leds          = NULL;
	ppc_md.kbd_init_hw       = NULL;

#if defined(CONFIG_MAGIC_SYSRQ)
	ppc_md.kbd_sysrq_xlate	 = NULL;
#endif

	return;
}

void __init
oak_setup_arch(void)
{

}
