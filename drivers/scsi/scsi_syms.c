/*
 * We should not even be trying to compile this if we are not doing
 * a module.
 */
#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>

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

extern const char *const scsi_device_types[];

EXPORT_SYMBOL(scsi_register_module);
EXPORT_SYMBOL(scsi_unregister_module);
EXPORT_SYMBOL(scsi_free);
EXPORT_SYMBOL(scsi_malloc);
EXPORT_SYMBOL(scsi_register);
EXPORT_SYMBOL(scsi_unregister);
EXPORT_SYMBOL(scsicam_bios_param);
EXPORT_SYMBOL(scsi_allocate_device);
EXPORT_SYMBOL(scsi_do_cmd);
EXPORT_SYMBOL(scsi_command_size);
EXPORT_SYMBOL(scsi_init_malloc);
EXPORT_SYMBOL(scsi_init_free);
EXPORT_SYMBOL(scsi_ioctl);
EXPORT_SYMBOL(print_command);
EXPORT_SYMBOL(print_sense);
EXPORT_SYMBOL(print_msg);
EXPORT_SYMBOL(print_status);
EXPORT_SYMBOL(scsi_dma_free_sectors);
EXPORT_SYMBOL(kernel_scsi_ioctl);
EXPORT_SYMBOL(scsi_need_isa_buffer);
EXPORT_SYMBOL(scsi_request_queueable);
EXPORT_SYMBOL(scsi_release_command);
EXPORT_SYMBOL(print_Scsi_Cmnd);
EXPORT_SYMBOL(scsi_block_when_processing_errors);
EXPORT_SYMBOL(scsi_mark_host_reset);
EXPORT_SYMBOL(scsi_ioctl_send_command);
#if defined(CONFIG_SCSI_LOGGING) /* { */
EXPORT_SYMBOL(scsi_logging_level);
#endif

EXPORT_SYMBOL(scsi_sleep);

EXPORT_SYMBOL(proc_print_scsidevice);
/*
 * These are here only while I debug the rest of the scsi stuff.
 */
EXPORT_SYMBOL(scsi_hostlist);
EXPORT_SYMBOL(scsi_hosts);
EXPORT_SYMBOL(scsi_devicelist);
EXPORT_SYMBOL(scsi_device_types);


#endif /* CONFIG_MODULES */
