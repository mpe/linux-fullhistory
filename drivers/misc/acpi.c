/*
 *  acpi.c - Linux ACPI driver
 *
 *  Copyright (C) 1999 Andrew Henroid
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/acpi.h>

/*
 * Defines for 2.2.x
 */
#ifndef __exit
#define __exit
#endif
#ifndef module_init
#define module_init(x) int init_module(void) {return x();}
#endif
#ifndef module_exit
#define module_exit(x) void cleanup_module(void) {x();}
#endif
#ifndef DECLARE_WAIT_QUEUE_HEAD
#define DECLARE_WAIT_QUEUE_HEAD(x) struct wait_queue * x = NULL
#endif

static struct acpi_facp *acpi_facp = NULL;
static unsigned long acpi_facp_addr = 0;
static unsigned long acpi_dsdt_addr = 0;
static DECLARE_WAIT_QUEUE_HEAD(acpi_wait_event);

/*
 * Get the value of the fixed event status register
 */
static u32 acpi_read_pm1_status(struct acpi_facp *facp)
{
	u32 value = inw(facp->pm1a_evt);

	if (facp->pm1b_evt) {
		value |= inw(facp->pm1b_evt);
	}
	return value;
}

/*
 * Set the value of the fixed event status register (clear events)
 */
static void acpi_write_pm1_status(struct acpi_facp *facp, u32 value)
{
	outw(value, facp->pm1a_evt);
	if (facp->pm1b_evt) {
		outw(value, facp->pm1b_evt);
	}
}

/*
 * Set the value of the fixed event enable register (enable events)
 */
static void acpi_write_pm1_enable(struct acpi_facp *facp, u32 value)
{
	int offset = facp->pm1_evt_len >> 1;

	outw(value, facp->pm1a_evt + offset);
	if (facp->pm1b_evt) {
		outw(value, facp->pm1b_evt + offset);
	}
}

/*
 * Get the value of the general-purpose event status register
 */
static u32 acpi_read_gpe_status(struct acpi_facp *facp)
{
	u32 value = 0;
	int i, size;

	if (facp->gpe1) {
		size = facp->gpe1_len >> 1;
		for (i = size - 1; i >= 0; i--) {
			value = (value << 8) | inb(facp->gpe1 + i);
		}
	}
	size = facp->gpe0_len >> 1;
	for (i = size - 1; i >= 0; i--) {
		value = (value << 8) | inb(facp->gpe0 + i);
	}
	return value;
}

/*
 * Set the value of the general-purpose event status register (clear events)
 */
static void acpi_write_gpe_status(struct acpi_facp *facp, u32 value)
{
	int i, size;

	size = facp->gpe0_len >> 1;
	for (i = 0; i < size; i++) {
		outb(value & 0xff, facp->gpe0 + i);
		value >>= 8;
	}
	if (facp->gpe1) {
		size = facp->gpe1_len >> 1;
		for (i = 0; i < size; i++) {
			outb(value & 0xff, facp->gpe1 + i);
			value >>= 8;
		}
	}
}

/*
 * Set the value of the general-purpose event enable register (enable events)
 */
static void acpi_write_gpe_enable(struct acpi_facp *facp, u32 value)
{
	int i, offset;

	offset = facp->gpe0_len >> 1;
	for (i = 0; i < offset; i++) {
		outb(value & 0xff, facp->gpe0 + offset + i);
		value >>= 8;
	}
	if (facp->gpe1) {
		offset = facp->gpe1_len >> 1;
		for (i = 0; i < offset; i++) {
			outb(value & 0xff, facp->gpe1 + offset + i);
			value >>= 8;
		}
	}
}

/*
 * Map an ACPI table into virtual memory
 */
static struct acpi_table *__init acpi_map_table(u32 addr)
{
	struct acpi_table *table = NULL;
	if (addr) {
		// map table header to determine size
		table = (struct acpi_table *)
		    ioremap_nocache((unsigned long) addr, sizeof(struct acpi_table));
		if (table) {
			unsigned long table_size = table->length;
			iounmap(table);
			// remap entire table
			table = (struct acpi_table *)
			    ioremap_nocache((unsigned long) addr, table_size);
		}
	}
	return table;
}

/*
 * Unmap an ACPI table from virtual memory
 */
static void acpi_unmap_table(struct acpi_table *table)
{
	if (table) {
		iounmap(table);
	}
}

/*
 * Locate and map ACPI tables (FACP, DSDT, ...)
 */
static int __init acpi_map_tables(void)
{
	struct acpi_rsdp *rsdp;
	struct acpi_table *rsdt;
	u32 *rsdt_entry;
	int rsdt_entry_count;
	u8 *i;

	// search BIOS memory for RSDP
	for (i = ACPI_BIOS_ROM_BASE; i < ACPI_BIOS_ROM_END; i += 16) {
		rsdp = (struct acpi_rsdp *) i;
		if (readl(rsdp->signature) == ACPI_RSDP1_SIG
		    && readl(rsdp->signature + 1) == ACPI_RSDP2_SIG) {
			char oem[7];
			int j;

			// strip trailing space and print OEM identifier
			memcpy_fromio(oem, rsdp->oem, 6);
			oem[6] = '\0';
			for (j = 5; j > 0 && (oem[j] == '\0' || oem[j] == ' '); j--) {
				oem[j] = '\0';
			}
			printk(KERN_INFO "ACPI: \"%s\" found at 0x%p\n", oem, (void *) i);

			break;
		}
	}
	if (i >= ACPI_BIOS_ROM_END) {
		printk(KERN_ERR "ACPI: no RSDP found\n");
		return -ENODEV;
	}
	// fetch RSDT from RSDP
	rsdt = acpi_map_table(readl(&rsdp->rsdt));
	if (!rsdt || rsdt->signature != ACPI_RSDT_SIG) {
		printk(KERN_ERR "ACPI: no RSDT found\n");
		acpi_unmap_table(rsdt);
		return -ENODEV;
	}
	// search RSDT for FACP
	acpi_facp = NULL;
	rsdt_entry = (u32 *) (rsdt + 1);
	rsdt_entry_count = (int) ((rsdt->length - sizeof(*rsdt)) >> 2);
	while (rsdt_entry_count) {
		struct acpi_table *dt = acpi_map_table(*rsdt_entry);
		if (dt && dt->signature == ACPI_FACP_SIG) {
			acpi_facp = (struct acpi_facp *) dt;
			acpi_facp_addr = *rsdt_entry;
			acpi_dsdt_addr = acpi_facp->dsdt;
			break;
		} else {
			acpi_unmap_table(dt);
		}
		rsdt_entry++;
		rsdt_entry_count--;
	}

	acpi_unmap_table(rsdt);

	if (!acpi_facp) {
		printk(KERN_ERR "ACPI: no FACP found\n");
		return -ENODEV;
	}
	return 0;
}

/*
 * Unmap ACPI tables (FACP, DSDT, ...)
 */
static void acpi_unmap_tables(void)
{
	acpi_dsdt_addr = 0;
	acpi_facp_addr = 0;
	acpi_unmap_table((struct acpi_table *) acpi_facp);
	acpi_facp = NULL;
}

/*
 * Handle an ACPI SCI (fixed or general purpose event)
 */
static void acpi_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	u32 status;

	// detect and disable any fixed events
	status = acpi_read_pm1_status(acpi_facp);
	acpi_write_pm1_enable(acpi_facp, ~status);

	// detect and disable any general-purpose events
	status = acpi_read_gpe_status(acpi_facp);
	acpi_write_gpe_enable(acpi_facp, ~status);

	// notify process reading /dev/acpi
	wake_up_interruptible(&acpi_wait_event);
}

/*
 * Handle open of /dev/acpi
 */
static int acpi_open(struct inode *inode, struct file *file)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * Handle close of /dev/acpi
 */
static int acpi_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * Handle command to /dev/acpi
 */
static int acpi_ioctl(struct inode *inode,
		      struct file *file,
		      unsigned cmd,
		      unsigned long arg)
{
	int status = -EINVAL;

	switch (cmd) {
	case ACPI_FIND_TABLES:
		status = verify_area(VERIFY_WRITE,
				     (void *) arg,
				     sizeof(struct acpi_find_tables));
		if (!status) {
			struct acpi_find_tables *rqst = (struct acpi_find_tables *) arg;
			put_user(acpi_facp_addr, &rqst->facp);
			put_user(acpi_dsdt_addr, &rqst->dsdt);
			status = 0;
		}
		break;
	case ACPI_WAIT_EVENT:
		interruptible_sleep_on(&acpi_wait_event);
		if (signal_pending(current))
			return -ERESTARTSYS;
		status = 0;
		break;
	}
	return status;
}

static struct file_operations acpi_fops =
{
	NULL,			/* llseek */
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* poll */
	acpi_ioctl,		/* ioctl */
	NULL,			/* mmap */
	acpi_open,		/* open */
	NULL,			/* flush */
	acpi_release,		/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
	NULL,			/* lock */
};

static struct miscdevice acpi_device =
{
	ACPI_MINOR_DEV,
	"acpi",
	&acpi_fops,
	NULL,
	NULL
};

/*
 * Initialize and enable ACPI
 */
static int __init acpi_init(void)
{
	if (acpi_map_tables()) {
		return -ENODEV;
	}
	if (request_irq(acpi_facp->sci_int,
			acpi_irq,
			SA_INTERRUPT | SA_SHIRQ,
			"acpi",
			NULL)) {
		printk(KERN_ERR "ACPI: SCI (IRQ%d) allocation failed\n",
		       acpi_facp->sci_int);
		acpi_unmap_tables();
		return -ENODEV;
	}
	if (misc_register(&acpi_device)) {
		printk(KERN_ERR "ACPI: misc. register failed\n");
	}
	return 0;
}

/*
 * Disable and deinitialize ACPI
 */
static void __exit acpi_exit(void)
{
	misc_deregister(&acpi_device);

	// disable and clear any pending events
	acpi_write_gpe_enable(acpi_facp, 0);
	while (acpi_read_gpe_status(acpi_facp)) {
		acpi_write_gpe_status(acpi_facp, acpi_read_gpe_status(acpi_facp));
	}
	acpi_write_pm1_enable(acpi_facp, 0);
	acpi_write_pm1_status(acpi_facp, acpi_read_pm1_status(acpi_facp));

	// disable SCI and free interrupt
	outb(acpi_facp->acpi_disable, acpi_facp->smi_cmd);
	free_irq(acpi_facp->sci_int, NULL);

	acpi_unmap_tables();
}

#ifdef MODULE

module_init(acpi_init)
module_exit(acpi_exit)
#else

__initcall(acpi_init);

#endif
