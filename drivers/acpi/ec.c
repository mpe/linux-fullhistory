/*
 *  ec.c - Embedded controller support
 *
 *  Copyright (C) 2000 Andrew Henroid
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <asm/io.h>
#include "acpi.h"
#include "driver.h"

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("ec")

#define ACPI_EC_HID	"PNP0C09"

enum
{
	ACPI_EC_SMI = 0x40,
	ACPI_EC_SCI = 0x20,
	ACPI_EC_BURST = 0x10,
	ACPI_EC_CMD = 0x08,
	ACPI_EC_IBF = 0x02,
	ACPI_EC_OBF = 0x01
};

enum
{
	ACPI_EC_READ = 0x80,
	ACPI_EC_WRITE = 0x81,
	ACPI_EC_BURST_ENABLE = 0x82,
	ACPI_EC_BURST_DISABLE = 0x83,
	ACPI_EC_QUERY = 0x84,
};

struct ec_context
{
	u32			gpe_bit;
	ACPI_IO_ADDRESS 	status_port;
	ACPI_IO_ADDRESS 	data_port;
	u32			need_global_lock;
};


static DECLARE_WAIT_QUEUE_HEAD(acpi_ec_wait);

/*
 * handle GPE
 */
static void
acpi_ec_gpe(void *context)
{
	printk(KERN_INFO "ACPI: EC GPE\n");
	/* TODO fix this to use per-device sem */
	if (waitqueue_active(&acpi_ec_wait))
		wake_up_interruptible(&acpi_ec_wait);
}

/*
 * wait for read/write status to clear
 */
static void
acpi_ec_wait_control(struct ec_context *ec_cxt)
{
	udelay(1);
	while(inb(ec_cxt->status_port) & ACPI_EC_IBF)
		udelay(10);
}

/*
 * read a byte from the EC
 */
int
acpi_ec_read(struct ec_context *ec_cxt,
		int addr,
		int *value)
{
	if (!ec_cxt->data_port || !ec_cxt->status_port)
		return -1;

	if (ec_cxt->need_global_lock)
		acpi_acquire_global_lock();

	outb(ACPI_EC_READ, ec_cxt->status_port);
	acpi_ec_wait_control(ec_cxt);
	outb(addr, ec_cxt->data_port);
	acpi_ec_wait_control(ec_cxt);
	/*interruptible_sleep_on(&acpi_ec_wait);*/
	*value = inb(ec_cxt->data_port);

	if (ec_cxt->need_global_lock)
		acpi_release_global_lock();

	return 0;
}

/*
 * write a byte to the EC
 */
int
acpi_ec_write(struct ec_context *ec_cxt,
		int addr,
		int value)
{
	if (!ec_cxt->data_port || !ec_cxt->status_port)
		return -1;

	if (ec_cxt->need_global_lock)
		acpi_acquire_global_lock();

	outb(ACPI_EC_WRITE, ec_cxt->status_port);
	acpi_ec_wait_control(ec_cxt);
	outb(addr, ec_cxt->data_port);
	acpi_ec_wait_control(ec_cxt);
	outb(value, ec_cxt->data_port);
	acpi_ec_wait_control(ec_cxt);
	/*interruptible_sleep_on(&acpi_ec_wait);*/

	if (ec_cxt->need_global_lock)
		acpi_release_global_lock();

	return 0;
}

static ACPI_STATUS
acpi_ec_region_setup (
    ACPI_HANDLE handle,
    u32 function,
    void *handler_context,
    void **region_context)
{
	FUNCTION_TRACE("acpi_ec_region_setup");

	printk("acpi_ec_region_setup\n");

	if (function == ACPI_REGION_DEACTIVATE)
	{
		if (*region_context)
		{
			acpi_cm_free (*region_context);
			*region_context = NULL;
		}

		return_ACPI_STATUS (AE_OK);
	}

	*region_context = NULL;

	return_ACPI_STATUS (AE_OK);
}

static ACPI_STATUS
acpi_ec_region_handler (u32 function,
			ACPI_PHYSICAL_ADDRESS address,
			u32 bitwidth,
			u32 *value,
			void *handler_context,
			void *region_context)
{
	struct ec_context	*ec_cxt;

	FUNCTION_TRACE("acpi_ec_region_handler");

	ec_cxt = handler_context;

	if (function == ADDRESS_SPACE_READ) {
		*value = 0;
		acpi_ec_read(ec_cxt, address, value);
		/*printk("EC read %x from %x\n", *value, address);*/
	}
	else {
		acpi_ec_write(ec_cxt, address, *value);
		/*printk("EC write value %x to %x\n", *value, address);*/
	}
	
	return_ACPI_STATUS (AE_OK);
}

/*
 * Get Embedded Controller information
 */
static ACPI_STATUS
acpi_found_ec(ACPI_HANDLE handle, u32 level, void *ctx, void **value)
{
	ACPI_STATUS status;
	ACPI_OBJECT obj;
	ACPI_BUFFER buf;
	RESOURCE *res;
	struct ec_context *ec_cxt;

	buf.length = 0;
	buf.pointer = NULL;
	if (acpi_get_current_resources(handle, &buf) != AE_BUFFER_OVERFLOW)
		return AE_OK;

	buf.pointer = kmalloc(buf.length, GFP_KERNEL);
	if (!buf.pointer)
		return AE_NO_MEMORY;

	if (!ACPI_SUCCESS(acpi_get_current_resources(handle, &buf))) {
		kfree(buf.pointer);
		return AE_OK;
	}

	ec_cxt = kmalloc(sizeof(struct ec_context), GFP_KERNEL);
	if (!ec_cxt) {
		kfree(buf.pointer);
		return AE_NO_MEMORY;
	}

	res = (RESOURCE*) buf.pointer;
	ec_cxt->data_port = res->data.io.min_base_address;
	res = NEXT_RESOURCE(res);
	ec_cxt->status_port = (int) res->data.io.min_base_address;

	kfree(buf.pointer);

	/* determine GPE bit */
	/* BUG: in acpi 2.0 this could return a package */
	buf.length = sizeof(obj);
	buf.pointer = &obj;
	if (!ACPI_SUCCESS(acpi_evaluate_object(handle, "_GPE", NULL, &buf))
		|| obj.type != ACPI_TYPE_NUMBER)
		return AE_OK;

	ec_cxt->gpe_bit = obj.number.value;

	/* determine if we need the Global Lock when accessing */
	buf.length = sizeof(obj);
	buf.pointer = &obj;

	status = acpi_evaluate_object(handle, "_GLK", NULL, &buf);
	if (status == AE_NOT_FOUND)
		ec_cxt->need_global_lock = 0;
	else if (!ACPI_SUCCESS(status) || obj.type != ACPI_TYPE_NUMBER) {
		DEBUG_PRINT(ACPI_ERROR, ("_GLK failed\n"));
		return AE_OK;
	}

	ec_cxt->need_global_lock = obj.number.value;

	printk(KERN_INFO "ACPI: found EC @ (0x%02x,0x%02x,gpe %d GL %d)\n",
		ec_cxt->data_port, ec_cxt->status_port, ec_cxt->gpe_bit,
		ec_cxt->need_global_lock);

	if (!ACPI_SUCCESS(acpi_install_gpe_handler(
		ec_cxt->gpe_bit,
		(ACPI_EVENT_LEVEL_TRIGGERED
		 | ACPI_EVENT_EDGE_TRIGGERED),
		acpi_ec_gpe,
		NULL))) {
		
		REPORT_ERROR(("Could not install GPE handler for EC.\n"));
		return AE_OK;
	}

	status = acpi_install_address_space_handler (handle, ADDRESS_SPACE_EC, 
		    acpi_ec_region_handler, acpi_ec_region_setup, ec_cxt);

	if (!ACPI_SUCCESS(status)) {
		REPORT_ERROR(("Could not install EC address "
			"space handler, error %s\n", acpi_cm_format_exception (status)));
	}
	
	return AE_OK;
}

int
acpi_ec_init(void)
{
	acpi_get_devices(ACPI_EC_HID, 
			acpi_found_ec,
			NULL,
			NULL);

	return 0;
}

int
acpi_ec_terminate(void)
{
	/* TODO */	
	/* walk list of EC's */
	/* free their context and release resources */
	return 0;
}
