
#include <linux/autoconf.h>

/*
 * We should not even be trying to compile this if we are not doing
 * a module.
 */
#ifndef MODULE
#error Go away.
#endif

/*
 * Even though we are building a module, we need to undef this, since
 * we are building a symbol table to be used by other modules.  For
 * the symbol table to build properly, we need to undefine this.
 */
#undef MODULE

#include <linux/module.h>
#include <linux/version.h>

#include <asm/system.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <linux/ioport.h>
#include <linux/kernel.h>

#include "../block/blk.h"
#include "scsi.h"
#include "hosts.h"
#include "constants.h"

#include "sd.h"
/*
 * This source file contains the symbol table used by scsi loadable
 * modules.
 */

extern void print_command (unsigned char *command);
extern void print_sense(char * devclass, Scsi_Cmnd * SCpnt);

struct symbol_table scsi_symbol_table = {
#include <linux/symtab_begin.h>
#ifdef CONFIG_MODVERSIONS
    { (void *)1 /* Version version :-) */, 
	SYMBOL_NAME_STR("Using_Versions") },
#endif
    X(scsi_register_module),
    X(scsi_unregister_module),
    X(scsi_free),
    X(scsi_malloc),
    X(scsi_register),
    X(scsi_unregister),
    X(scsicam_bios_param),
    X(allocate_device),
    X(scsi_do_cmd),
    X(scsi_command_size),
    X(scsi_init_malloc),
    X(scsi_init_free),
    X(scsi_ioctl),
    X(print_command),
    X(print_sense),
    X(dma_free_sectors),
    X(kernel_scsi_ioctl),
    X(need_isa_buffer),
    X(request_queueable),
/*
 * These are here only while I debug the rest of the scsi stuff.
 */
    X(scsi_hostlist),
    X(scsi_hosts),
    X(scsi_devicelist),
    X(scsi_devices),

    /********************************************************
     * Do not add anything below this line,
     * as the stacked modules depend on this!
     */
#include <linux/symtab_end.h>
};
