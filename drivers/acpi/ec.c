/*
 *	ec.c - Embedded controller support
 *
 *	Copyright (C) 2000 Andrew Henroid
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

#define ACPI_EC_HID	"PNP0A09"

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


static int acpi_ec_data = 0;
static int acpi_ec_status = 0;
static DECLARE_WAIT_QUEUE_HEAD(acpi_ec_wait);

/*
 * handle GPE
 */
static void
acpi_ec_gpe(void *context)
{
	printk(KERN_INFO "ACPI: EC GPE\n");
	if (waitqueue_active(&acpi_ec_wait))
		wake_up_interruptible(&acpi_ec_wait);
}

/*
 * wait for read/write status to clear
 */
static void
acpi_ec_wait_control(void)
{
		udelay(1);
		while(inb(acpi_ec_status) & ACPI_EC_IBF)
				udelay(10);
}

/*
 * read a byte from the EC
 */
int
acpi_ec_read(int addr, int *value)
{
	if (!acpi_ec_data || !acpi_ec_status)
		return -1;

		outb(ACPI_EC_READ, acpi_ec_status);
		acpi_ec_wait_control();
		outb(addr, acpi_ec_data);
		acpi_ec_wait_control();
		interruptible_sleep_on(&acpi_ec_wait);
		*value = inb(acpi_ec_data);

	return 0;
}

/*
 * write a byte to the EC
 */
int
acpi_ec_write(int addr, int value)
{
	if (!acpi_ec_data || !acpi_ec_status)
		return -1;

		outb(ACPI_EC_WRITE, acpi_ec_status);
		acpi_ec_wait_control();
		outb(addr, acpi_ec_data);
		acpi_ec_wait_control();
		outb(value, acpi_ec_data);
		acpi_ec_wait_control();
		interruptible_sleep_on(&acpi_ec_wait);

	return 0;
}

/*
 * Get Embedded Controller information
 */
static ACPI_STATUS
acpi_find_ec(ACPI_HANDLE handle, u32 level, void *ctx, void **value)
{
	ACPI_DEVICE_INFO dev_info;
	ACPI_OBJECT obj;
	ACPI_BUFFER buf;
	RESOURCE *res;
	int gpe;

	if (!ACPI_SUCCESS(acpi_get_object_info(handle, &dev_info))
		|| !(dev_info.valid & ACPI_VALID_HID)
		|| 0 != STRCMP(dev_info.hardware_id, ACPI_EC_HID))
		return AE_OK;

	buf.length = 0;
	buf.pointer = NULL;
	if (acpi_get_current_resources(handle, &buf) != AE_BUFFER_OVERFLOW)
		return AE_OK;

	buf.pointer = kmalloc(buf.length, GFP_KERNEL);
	if (!buf.pointer)
		return AE_OK;

	if (!ACPI_SUCCESS(acpi_get_current_resources(handle, &buf))) {
		kfree(buf.pointer);
		return AE_OK;
	}

	res = (RESOURCE*) buf.pointer;
	acpi_ec_data = (int) res->data.io.min_base_address;
	res = (RESOURCE*)((u8*) buf.pointer + res->length);
	acpi_ec_status = (int) res->data.io.min_base_address;

	kfree(buf.pointer);

	buf.length = sizeof(obj);
	buf.pointer = &obj;
	if (!ACPI_SUCCESS(acpi_evaluate_object(handle, "_GPE", NULL, &buf))
		|| obj.type != ACPI_TYPE_NUMBER)
		return AE_OK;
	gpe = (int) obj.number.value;

	printk(KERN_INFO "ACPI: found EC @ (0x%02x,0x%02x,%d)\n",
		   acpi_ec_data, acpi_ec_status, gpe);

	if (!ACPI_SUCCESS(acpi_install_gpe_handler(
		gpe,
		(ACPI_EVENT_LEVEL_TRIGGERED
		 | ACPI_EVENT_EDGE_TRIGGERED),
		acpi_ec_gpe,
		NULL))) {
		
		DEBUG_PRINT(ACPI_ERROR, ("Could not install GPE handler for EC.\n"));
		return AE_OK;
	}
	
	return AE_OK;
}

int
acpi_ec_init(void)
{
	acpi_walk_namespace(ACPI_TYPE_DEVICE,
				ACPI_ROOT_OBJECT,
				ACPI_UINT32_MAX,
				acpi_find_ec,
				NULL,
				NULL);
	return 0;
}
