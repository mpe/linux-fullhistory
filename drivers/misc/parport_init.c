/* Parallel-port initialisation code.
 * 
 * Authors: David Campbell <campbell@tirian.che.curtin.edu.au>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *
 * based on work by Grant Guenther <grant@torque.net>
 *              and Philip Blundell <Philip.Blundell@pobox.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/tasks.h>

#include <linux/parport.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/init.h>

#ifndef MODULE
static int io[PARPORT_MAX+1] __initdata = { 0, };
static int irq[PARPORT_MAX] __initdata = { PARPORT_IRQ_NONE, };
static int dma[PARPORT_MAX] __initdata = { PARPORT_DMA_NONE, };

extern int parport_pc_init(int *io, int *irq, int *dma);

static int parport_setup_ptr __initdata = 0;

__initfunc(void parport_setup(char *str, int *ints))
{
	if (ints[0] == 0 || ints[1] == 0) {
		/* Disable parport if "parport=" or "parport=0" in cmdline */
		io[0] = PARPORT_DISABLE; 
		return;
	}
	if (parport_setup_ptr < PARPORT_MAX) {
		io[parport_setup_ptr] = ints[1];
		if (ints[0]>1) {
			irq[parport_setup_ptr] = ints[2];
			if (ints[0]>2) dma[parport_setup_ptr] = ints[3];
		}
		parport_setup_ptr++;
	} else {
		printk(KERN_ERR "parport=0x%x", ints[1]);
		if (ints[0]>1) {
			printk(",%d", ints[2]);
			if (ints[0]>2) printk(",%d", ints[3]);
		}
		printk(" ignored, too many ports.\n");
	}
}
#endif

#ifdef MODULE
int init_module(void)
{
	parport_proc_init();
	return 0;
}

void cleanup_module(void)
{
	parport_proc_cleanup();
}
#else
__initfunc(int parport_init(void))
{
	struct parport *pb;

	if (io[0] == PARPORT_DISABLE) return 1;
	parport_proc_init();
#ifdef CONFIG_PARPORT_PC
	parport_pc_init(io, irq, dma);
#endif
	return 0;
}
#endif

/* Exported symbols for modules. */

EXPORT_SYMBOL(parport_claim);
EXPORT_SYMBOL(parport_release);
EXPORT_SYMBOL(parport_register_port);
EXPORT_SYMBOL(parport_unregister_port);
EXPORT_SYMBOL(parport_quiesce);
EXPORT_SYMBOL(parport_register_device);
EXPORT_SYMBOL(parport_unregister_device);
EXPORT_SYMBOL(parport_enumerate);
EXPORT_SYMBOL(parport_ieee1284_nibble_mode_ok);
EXPORT_SYMBOL(parport_wait_peripheral);
EXPORT_SYMBOL(parport_proc_register);
EXPORT_SYMBOL(parport_proc_unregister);

void inc_parport_count(void)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

void dec_parport_count(void)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}
