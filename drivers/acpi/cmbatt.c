/*
 *  cmbatt.c - Control Method Battery driver
 *
 *  Copyright (C) 2000 Andrew Grover
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
	MODULE_NAME	("cmbatt")

#define ACPI_CMBATT_HID		"PNP0C0A"

#define ACPI_BATT_PRESENT	0x10

#define ACPI_MAX_BATTERIES	0x8

struct cmbatt_context
{
	char			UID[9];
	u8			is_present;
	ACPI_HANDLE		handle;
};

struct cmbatt_status
{
	u32			state;
	u32			present_rate;
	u32			remaining_capacity;
	u32			present_voltage;
};

static u32 batt_count = 0;

static struct cmbatt_context batt_list[ACPI_MAX_BATTERIES];

/*
 * We found a device with the correct HID
 */
static ACPI_STATUS
acpi_found_cmbatt(ACPI_HANDLE handle, u32 level, void *ctx, void **value)
{
	ACPI_DEVICE_INFO	info;
	
	if (!ACPI_SUCCESS(acpi_get_object_info(handle, &info))) {
		printk(KERN_ERR "Could not get battery object info\n");
		return (AE_OK);
	}

	if (info.valid & ACPI_VALID_UID) {
		strncpy(batt_list[batt_count].UID, info.unique_id, 9);
	}
	else if (batt_count > 1) {
		printk(KERN_WARNING "ACPI: No UID but more than 1 battery\n");
	}

	if ((info.valid & ACPI_VALID_STA) &&
	    (info.current_status & ACPI_BATT_PRESENT)) {

		ACPI_BUFFER buf;

		printk("ACPI: Found a battery\n");
		batt_list[batt_count].is_present = TRUE;

		buf.length = 0;
		buf.pointer = NULL;

		/* determine buffer length needed */
		if (acpi_evaluate_object(handle, "_BST", NULL, &buf) != AE_BUFFER_OVERFLOW)
			return AE_OK;

		buf.pointer = kmalloc(buf.length, GFP_KERNEL);
		
		if (!buf.pointer)
			return AE_NO_MEMORY;

		/* get the data */
		if (!ACPI_SUCCESS(acpi_evaluate_object(handle, "_BST", NULL, &buf))) {
			printk(KERN_ERR "Could not get battery status\n");
			kfree (buf.pointer);
			return AE_OK;
		}

		kfree(buf.pointer);

		/* TODO: parse the battery data */
		/* TODO: add proc interface */
	}
	else {
		printk("ACPI: Found an empty battery socket\n");
		batt_list[batt_count].is_present = FALSE;
	}

	batt_list[batt_count].handle = handle;

	batt_count++;

	return (AE_OK);
}

int
acpi_cmbatt_init(void)
{
	acpi_get_devices(ACPI_CMBATT_HID, 
			acpi_found_cmbatt,
			NULL,
			NULL);

	return 0;
}

int
acpi_cmbatt_terminate(void)
{
	/* TODO */	
	/* walk list of batteries */
	/* free their context and release resources */
	return 0;
}
