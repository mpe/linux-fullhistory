/*
 *	scsi_module.c Copyright (1994, 1995) Eric Youngdale.
 *
 * Support for loading low-level scsi drivers using the linux kernel loadable
 * module interface.
 *
 * To use, the host adapter should first define and initialize the variable
 * driver_template (datatype Scsi_Host_Template), and then include this file.
 * This should also be wrapped in a #ifdef MODULE/#endif.
 *
 * The low -level driver must also define a release function which will
 * free any irq assignments, release any dma channels, release any I/O
 * address space that might be reserved, and otherwise clean up after itself.
 * The idea is that the same driver should be able to be reloaded without
 * any difficulty.  This makes debugging new drivers easier, as you should
 * be able to load the driver, test it, unload, modify and reload.
 *
 * One *very* important caveat.  If the driver may need to do DMA on the
 * ISA bus, you must have unchecked_isa_dma set in the device template,
 * even if this might be changed during the detect routine.  This is
 * because the shpnt structure will be allocated in a special way so that
 * it will be below the appropriate DMA limit - thus if your driver uses
 * the hostdata field of shpnt, and the board must be able to access this
 * via DMA, the shpnt structure must be in a DMA accessible region of
 * memory.  This comment would be relevant for something like the buslogic
 * driver where there are many boards, only some of which do DMA onto the
 * ISA bus.  There is no convenient way of specifying whether the host
 * needs to be in a ISA DMA accessible region of memory when you call
 * scsi_register.
 */

#include <linux/module.h>
#include <linux/version.h>

char kernel_version[] = UTS_RELEASE;

int init_module(void) {
	driver_template.usage_count = &mod_use_count_;
	scsi_register_module(MODULE_SCSI_HA, &driver_template);
	return (driver_template.present == 0);
}

void cleanup_module( void) {
	if (MOD_IN_USE) {
		printk(KERN_INFO __FILE__ ": module is in use, remove rejected\n");
	      }
	scsi_unregister_module(MODULE_SCSI_HA, &driver_template);
}

