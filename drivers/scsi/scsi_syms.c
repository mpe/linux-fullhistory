/*
 * We should not even be trying to compile this if we are not doing
 * a module.
 */
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/config.h>
#ifdef CONFIG_MODULES

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/blk.h>
#include <linux/fs.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>

#include "scsi.h"
#include <scsi/scsi_ioctl.h>
#include "hosts.h"
#include "constants.h"

#include "sd.h"
/*
 * This source file contains the symbol table used by scsi loadable
 * modules.
 */
extern int scsicam_bios_param (Disk * disk,
                               int dev,	int *ip	); 


extern void print_command (unsigned char *command);
extern void print_sense(const char * devclass, Scsi_Cmnd * SCpnt);

struct symbol_table scsi_symbol_table = {
#include <linux/symtab_begin.h>
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
    X(print_msg),
    X(print_status),
    X(dma_free_sectors),
    X(kernel_scsi_ioctl),
    X(need_isa_buffer),
    X(request_queueable),
    X(print_Scsi_Cmnd),
    X(scsi_mark_host_reset),
    X(scsi_mark_bus_reset),
#if defined(CONFIG_PROC_FS)
    X(proc_print_scsidevice),
#endif
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
#endif
