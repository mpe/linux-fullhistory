/*
 * linux/drivers/s390/scsi/zfcp_ccw.c
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * CCW driver related routines
 *
 * Copyright (C) 2003 IBM Entwicklung GmbH, IBM Corporation
 * Authors:
 *      Martin Peschke <mpeschke@de.ibm.com>
 *	Heiko Carstens <heiko.carstens@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define ZFCP_CCW_C_REVISION "$Revision: 1.33 $"

#include <linux/init.h>
#include <linux/module.h>
#include <asm/ccwdev.h>
#include "zfcp_ext.h"
#include "zfcp_def.h"

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_CONFIG

static int zfcp_ccw_probe(struct ccw_device *);
static int zfcp_ccw_remove(struct ccw_device *);
static int zfcp_ccw_set_online(struct ccw_device *);
static int zfcp_ccw_set_offline(struct ccw_device *);

static struct ccw_device_id zfcp_ccw_device_id[] = {
	{CCW_DEVICE_DEVTYPE(ZFCP_CONTROL_UNIT_TYPE,
			    ZFCP_CONTROL_UNIT_MODEL,
			    ZFCP_DEVICE_TYPE,
			    ZFCP_DEVICE_MODEL)},
	{},
};

static struct ccw_driver zfcp_ccw_driver = {
	.name        = ZFCP_NAME,
	.ids         = zfcp_ccw_device_id,
	.probe       = zfcp_ccw_probe,
	.remove      = zfcp_ccw_remove,
	.set_online  = zfcp_ccw_set_online,
	.set_offline = zfcp_ccw_set_offline,
};

MODULE_DEVICE_TABLE(ccw, zfcp_ccw_device_id);

/**
 * zfcp_ccw_probe - probe function of zfcp driver
 * @ccw_device: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer and sets up the initial
 * data structures for each fcp adapter, which was detected by the system.
 * Also the sysfs files for this adapter will be created by this function.
 * In addition the nameserver port will be added to the ports of the adapter
 * and its sysfs representation will be created too.
 */
static int
zfcp_ccw_probe(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;
	int retval = 0;

	down(&zfcp_data.config_sema);
	adapter = zfcp_adapter_enqueue(ccw_device);
	if (!adapter)
		retval = -EINVAL;
	up(&zfcp_data.config_sema);
	return retval;
}

/**
 * zfcp_ccw_remove - remove function of zfcp driver
 * @ccw_device: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer and removes an adapter
 * from the system. Task of this function is to get rid of all units and
 * ports that belong to this adapter. And addition all resources of this
 * adapter will be freed too.
 */
static int
zfcp_ccw_remove(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;
	struct zfcp_port *port, *p;
	struct zfcp_unit *unit, *u;

	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(&ccw_device->dev);

	write_lock_irq(&zfcp_data.config_lock);
	list_for_each_entry_safe(port, p, &adapter->port_list_head, list) {
		list_for_each_entry_safe(unit, u, &port->unit_list_head, list) {
			list_move(&unit->list, &port->unit_remove_lh);
			atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE,
					&unit->status);
		}
		list_move(&port->list, &adapter->port_remove_lh);
		atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);
	}
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);
	write_unlock_irq(&zfcp_data.config_lock);

	list_for_each_entry_safe(port, p, &adapter->port_remove_lh, list) {
		list_for_each_entry_safe(unit, u, &port->unit_remove_lh, list) {
			zfcp_unit_wait(unit);
			device_unregister(&unit->sysfs_device);
		}
		zfcp_port_wait(port);
		device_unregister(&port->sysfs_device);
	}
	zfcp_adapter_wait(adapter);
	zfcp_adapter_dequeue(adapter);

	up(&zfcp_data.config_sema);
	return 0;
}

/**
 * zfcp_ccw_set_online - set_online function of zfcp driver
 * @ccw_device: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer and sets an adapter
 * into state online. Setting an fcp device online means that it will be
 * registered with the SCSI stack, that the QDIO queues will be set up
 * and that the adapter will be opened (asynchronously).
 */
static int
zfcp_ccw_set_online(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;
	int retval;

	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(&ccw_device->dev);

	retval = zfcp_adapter_scsi_register(adapter);
	if (retval)
		goto out;
	zfcp_erp_modify_adapter_status(adapter, ZFCP_STATUS_COMMON_RUNNING,
				       ZFCP_SET);
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED);
 out:
	up(&zfcp_data.config_sema);
	return retval;
}

/**
 * zfcp_ccw_set_offline - set_offline function of zfcp driver
 * @ccw_device: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer and sets an adapter
 * into state offline. Setting an fcp device offline means that it will be
 * unregistered from the SCSI stack and that the adapter will be shut down
 * asynchronously.
 */
static int
zfcp_ccw_set_offline(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;

	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(&ccw_device->dev);
	zfcp_erp_adapter_shutdown(adapter, 0);
	zfcp_erp_wait(adapter);
	zfcp_adapter_scsi_unregister(adapter);
	up(&zfcp_data.config_sema);
	return 0;
}

/**
 * zfcp_ccw_register - ccw register function
 *
 * Registers the driver at the common i/o layer. This function will be called
 * at module load time/system start.
 */
int __init
zfcp_ccw_register(void)
{
	int retval;

	retval = ccw_driver_register(&zfcp_ccw_driver);
	if (retval)
		goto out;
	retval = zfcp_sysfs_driver_create_files(&zfcp_ccw_driver.driver);
	if (retval)
		ccw_driver_unregister(&zfcp_ccw_driver);
 out:
	return retval;
}

/**
 * zfcp_ccw_unregister - ccw unregister function
 *
 * Unregisters the driver from common i/o layer. Function will be called at
 * module unload/system shutdown.
 */
void __exit
zfcp_ccw_unregister(void)
{
	zfcp_sysfs_driver_remove_files(&zfcp_ccw_driver.driver);
	ccw_driver_unregister(&zfcp_ccw_driver);
}

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
