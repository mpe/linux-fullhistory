/*
 * We should not even be trying to compile this if we are not doing
 * a module.
 */
#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>

#ifdef CONFIG_MODULES

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>

#include "fcp_scsi.h"

EXPORT_SYMBOL(fcp_init);
EXPORT_SYMBOL(fcp_release);
EXPORT_SYMBOL(fcp_queue_empty);
EXPORT_SYMBOL(fcp_receive_solicited);
EXPORT_SYMBOL(fc_channels);
EXPORT_SYMBOL(fcp_state_change);

/* SCSI stuff */
EXPORT_SYMBOL(fcp_scsi_queuecommand);
EXPORT_SYMBOL(fcp_old_abort);
EXPORT_SYMBOL(fcp_scsi_abort);
EXPORT_SYMBOL(fcp_scsi_dev_reset);
EXPORT_SYMBOL(fcp_scsi_bus_reset);
EXPORT_SYMBOL(fcp_scsi_host_reset);

#endif /* CONFIG_MODULES */
