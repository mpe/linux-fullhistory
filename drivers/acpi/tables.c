/*
 *  acpi_tables.c - ACPI Boot-Time Table Parsing
 *
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/acpi.h>

#define PREFIX			"ACPI: "

#define ACPI_MAX_TABLES		ACPI_TABLE_COUNT

static char *acpi_table_signatures[ACPI_TABLE_COUNT] = {
	[ACPI_TABLE_UNKNOWN]	= "????",
	[ACPI_APIC]		= "APIC",
	[ACPI_BOOT]		= "BOOT",
	[ACPI_DBGP]		= "DBGP",
	[ACPI_DSDT]		= "DSDT",
	[ACPI_ECDT]		= "ECDT",
	[ACPI_ETDT]		= "ETDT",
	[ACPI_FACP]		= "FACP",
	[ACPI_FACS]		= "FACS",
	[ACPI_OEMX]		= "OEM",
	[ACPI_PSDT]		= "PSDT",
	[ACPI_SBST]		= "SBST",
	[ACPI_SLIT]		= "SLIT",
	[ACPI_SPCR]		= "SPCR",
	[ACPI_SRAT]		= "SRAT",
	[ACPI_SSDT]		= "SSDT",
	[ACPI_SPMI]		= "SPMI"
};

/* System Description Table (RSDT/XSDT) */
struct acpi_table_sdt {
	unsigned long		pa;		/* Physical Address */
	unsigned long		count;		/* Table count */
	struct {
		unsigned long		pa;
		enum acpi_table_id	id;
		unsigned long		size;
	}			entry[ACPI_MAX_TABLES];
} __attribute__ ((packed));

static struct acpi_table_sdt	sdt;

acpi_madt_entry_handler		madt_handlers[ACPI_MADT_ENTRY_COUNT];


void
acpi_table_print (
	struct acpi_table_header *header,
	unsigned long		phys_addr)
{
	char			*name = NULL;

	if (!header)
		return;

	/* Some table signatures aren't good table names */

	if (!strncmp((char *) &header->signature,
		acpi_table_signatures[ACPI_APIC],
		sizeof(header->signature))) {
		name = "MADT";
	}
	else if (!strncmp((char *) &header->signature,
		acpi_table_signatures[ACPI_FACP],
		sizeof(header->signature))) {
		name = "FADT";
	}
	else
		name = header->signature;

	printk(KERN_INFO PREFIX "%.4s (v%3.3d %6.6s %8.8s %5.5d.%5.5d) @ 0x%p\n",
		name, header->revision, header->oem_id,
		header->oem_table_id, header->oem_revision >> 16,
		header->oem_revision & 0xffff, (void *) phys_addr);
}


void
acpi_table_print_madt_entry (
	acpi_table_entry_header	*header)
{
	if (!header)
		return;

	switch (header->type) {

	case ACPI_MADT_LAPIC:
	{
		struct acpi_table_lapic *p =
			(struct acpi_table_lapic*) header;
		printk(KERN_INFO PREFIX "LAPIC (acpi_id[0x%02x] lapic_id[0x%02x] %s)\n",
			p->acpi_id, p->id, p->flags.enabled?"enabled":"disabled");
	}
		break;

	case ACPI_MADT_IOAPIC:
	{
		struct acpi_table_ioapic *p =
			(struct acpi_table_ioapic*) header;
		printk(KERN_INFO PREFIX "IOAPIC (id[0x%02x] address[0x%08x] global_irq_base[0x%x])\n",
			p->id, p->address, p->global_irq_base);
	}
		break;

	case ACPI_MADT_INT_SRC_OVR:
	{
		struct acpi_table_int_src_ovr *p =
			(struct acpi_table_int_src_ovr*) header;
		printk(KERN_INFO PREFIX "INT_SRC_OVR (bus[%d] irq[0x%x] global_irq[0x%x] polarity[0x%x] trigger[0x%x])\n",
			p->bus, p->bus_irq, p->global_irq, p->flags.polarity, p->flags.trigger);
	}
		break;

	case ACPI_MADT_NMI_SRC:
	{
		struct acpi_table_nmi_src *p =
			(struct acpi_table_nmi_src*) header;
		printk(KERN_INFO PREFIX "NMI_SRC (polarity[0x%x] trigger[0x%x] global_irq[0x%x])\n",
			p->flags.polarity, p->flags.trigger, p->global_irq);
	}
		break;

	case ACPI_MADT_LAPIC_NMI:
	{
		struct acpi_table_lapic_nmi *p =
			(struct acpi_table_lapic_nmi*) header;
		printk(KERN_INFO PREFIX "LAPIC_NMI (acpi_id[0x%02x] polarity[0x%x] trigger[0x%x] lint[0x%x])\n",
			p->acpi_id, p->flags.polarity, p->flags.trigger, p->lint);
	}
		break;

	case ACPI_MADT_LAPIC_ADDR_OVR:
	{
		struct acpi_table_lapic_addr_ovr *p =
			(struct acpi_table_lapic_addr_ovr*) header;
		printk(KERN_INFO PREFIX "LAPIC_ADDR_OVR (address[%p])\n",
			(void *) (unsigned long) p->address);
	}
		break;

	case ACPI_MADT_IOSAPIC:
	{
		struct acpi_table_iosapic *p =
			(struct acpi_table_iosapic*) header;
		printk(KERN_INFO PREFIX "IOSAPIC (id[0x%x] global_irq_base[0x%x] address[%p])\n",
			p->id, p->global_irq_base, (void *) (unsigned long) p->address);
	}
		break;

	case ACPI_MADT_LSAPIC:
	{
		struct acpi_table_lsapic *p =
			(struct acpi_table_lsapic*) header;
		printk(KERN_INFO PREFIX "LSAPIC (acpi_id[0x%02x] lsapic_id[0x%02x] lsapic_eid[0x%02x] %s)\n",
			p->acpi_id, p->id, p->eid, p->flags.enabled?"enabled":"disabled");
	}
		break;

	case ACPI_MADT_PLAT_INT_SRC:
	{
		struct acpi_table_plat_int_src *p =
			(struct acpi_table_plat_int_src*) header;
		printk(KERN_INFO PREFIX "PLAT_INT_SRC (polarity[0x%x] trigger[0x%x] type[0x%x] id[0x%04x] eid[0x%x] iosapic_vector[0x%x] global_irq[0x%x]\n",
			p->flags.polarity, p->flags.trigger, p->type, p->id, p->eid, p->iosapic_vector, p->global_irq);
	}
		break;

	default:
		printk(KERN_WARNING PREFIX "Found unsupported MADT entry (type = 0x%x)\n",
			header->type);
		break;
	}
}


static int
acpi_table_compute_checksum (
	void			*table_pointer,
	unsigned long		length)
{
	u8			*p = (u8 *) table_pointer;
	unsigned long		remains = length;
	unsigned long		sum = 0;

	if (!p || !length)
		return -EINVAL;

	while (remains--)
		sum += *p++;

	return (sum & 0xFF);
}


int __init
acpi_table_parse_madt (
	enum acpi_table_id	id,
	acpi_madt_entry_handler	handler)
{
	struct acpi_table_madt	*madt = NULL;
	acpi_table_entry_header	*entry = NULL;
	unsigned long		count = 0;
	unsigned long		madt_end = 0;
	int			i = 0;

	if (!handler)
		return -EINVAL;

	/* Locate the MADT (if exists). There should only be one. */

	for (i = 0; i < sdt.count; i++) {
		if (sdt.entry[i].id != ACPI_APIC)
			continue;
		madt = (struct acpi_table_madt *)
			__acpi_map_table(sdt.entry[i].pa, sdt.entry[i].size);
		if (!madt) {
			printk(KERN_WARNING PREFIX "Unable to map MADT\n");
			return -ENODEV;
		}
		break;
	}

	if (!madt) {
		printk(KERN_WARNING PREFIX "MADT not present\n");
		return -ENODEV;
	}

	madt_end = (unsigned long) madt + sdt.entry[i].size;

	/* Parse all entries looking for a match. */

	entry = (acpi_table_entry_header *)
		((unsigned long) madt + sizeof(struct acpi_table_madt));

	while (((unsigned long) entry) < madt_end) {
		if (entry->type == id) {
			count++;
			handler(entry);
		}
		entry = (acpi_table_entry_header *)
			((unsigned long) entry += entry->length);
	}

	return count;
}


int __init
acpi_table_parse (
	enum acpi_table_id	id,
	acpi_table_handler	handler)
{
	int			count = 0;
	int			i = 0;

	if (!handler)
		return -EINVAL;

	for (i = 0; i < sdt.count; i++) {
		if (sdt.entry[i].id != id)
			continue;
		handler(sdt.entry[i].pa, sdt.entry[i].size);
		count++;
	}

	return count;
}


static int __init
acpi_table_get_sdt (
	struct acpi_table_rsdp	*rsdp)
{
	struct acpi_table_header *header = NULL;
	int			i, id = 0;

	if (!rsdp)
		return -EINVAL;

	/* First check XSDT (but only on ACPI 2.0-compatible systems) */

	if ((rsdp->revision >= 2) &&
		(((struct acpi20_table_rsdp*)rsdp)->xsdt_address)) {
			
		struct acpi_table_xsdt	*mapped_xsdt = NULL;

		sdt.pa = ((struct acpi20_table_rsdp*)rsdp)->xsdt_address;

		header = (struct acpi_table_header *)
			__acpi_map_table(sdt.pa, sizeof(struct acpi_table_header));

		if (!header) {
			printk(KERN_WARNING PREFIX "Unable to map XSDT header\n");
			return -ENODEV;
		}

		if (strncmp(header->signature, "XSDT", 4)) {
			printk(KERN_WARNING PREFIX "XSDT signature incorrect\n");
			return -ENODEV;
		}

		sdt.count = (header->length - sizeof(struct acpi_table_header)) >> 3;
		if (sdt.count > ACPI_MAX_TABLES) {
			printk(KERN_WARNING PREFIX "Truncated %lu XSDT entries\n",
				(ACPI_MAX_TABLES - sdt.count));
			sdt.count = ACPI_MAX_TABLES;
		}

		mapped_xsdt = (struct acpi_table_xsdt *)
			__acpi_map_table(sdt.pa, header->length);
		if (!mapped_xsdt) {
			printk(KERN_WARNING PREFIX "Unable to map XSDT\n");
			return -ENODEV;
		}

		header = &mapped_xsdt->header;

		for (i = 0; i < sdt.count; i++)
			sdt.entry[i].pa = (unsigned long) mapped_xsdt->entry[i];
	}

	/* Then check RSDT */

	else if (rsdp->rsdt_address) {

		struct acpi_table_rsdt	*mapped_rsdt = NULL;

		sdt.pa = rsdp->rsdt_address;

		header = (struct acpi_table_header *)
			__acpi_map_table(sdt.pa, sizeof(struct acpi_table_header));
		if (!header) {
			printk(KERN_WARNING PREFIX "Unable to map RSDT header\n");
			return -ENODEV;
		}

		if (strncmp(header->signature, "RSDT", 4)) {
			printk(KERN_WARNING PREFIX "RSDT signature incorrect\n");
			return -ENODEV;
		}

		sdt.count = (header->length - sizeof(struct acpi_table_header)) >> 2;
		if (sdt.count > ACPI_MAX_TABLES) {
			printk(KERN_WARNING PREFIX "Truncated %lu RSDT entries\n",
				(ACPI_TABLE_COUNT - sdt.count));
			sdt.count = ACPI_MAX_TABLES;
		}

		mapped_rsdt = (struct acpi_table_rsdt *)
			__acpi_map_table(sdt.pa, header->length);
		if (!mapped_rsdt) {
			printk(KERN_WARNING PREFIX "Unable to map RSDT\n");
			return -ENODEV;
		}

		header = &mapped_rsdt->header;

		for (i = 0; i < sdt.count; i++)
			sdt.entry[i].pa = (unsigned long) mapped_rsdt->entry[i];
	}

	else {
		printk(KERN_WARNING PREFIX "No System Description Table (RSDT/XSDT) specified in RSDP\n");
		return -ENODEV;
	}

	acpi_table_print(header, sdt.pa);

	for (i = 0; i < sdt.count; i++) {

		header = (struct acpi_table_header *)
			__acpi_map_table(sdt.entry[i].pa,
				sizeof(struct acpi_table_header));
		if (!header)
			continue;

		acpi_table_print(header, sdt.entry[i].pa);

		if (acpi_table_compute_checksum(header, header->length)) {
			printk(KERN_WARNING "  >>> ERROR: Invalid checksum\n");
			continue;
		}

		sdt.entry[i].size = header->length;

		for (id = 0; id < ACPI_TABLE_COUNT; id++) {
			if (!strncmp((char *) &header->signature,
				acpi_table_signatures[id],
				sizeof(header->signature))) {
				sdt.entry[i].id = id;
			}
		}
	}

	return 0;
}


int __init
acpi_table_init (
	char			*cmdline)
{
	struct acpi_table_rsdp	*rsdp = NULL;
	unsigned long		rsdp_phys = 0;
	int			result = 0;

	memset(&sdt, 0, sizeof(struct acpi_table_sdt));
	memset(&madt_handlers, 0, sizeof(madt_handlers));

	/* Locate and map the Root System Description Table (RSDP) */

	rsdp_phys = acpi_find_rsdp();
	if (!rsdp_phys) {
		printk(KERN_ERR PREFIX "Unable to locate RSDP\n");
		return -ENODEV;
	}

	rsdp = (struct acpi_table_rsdp *) __va(rsdp_phys);
	if (!rsdp) {
		printk(KERN_WARNING PREFIX "Unable to map RSDP\n");
		return -ENODEV;
	}

	printk(KERN_INFO PREFIX "RSDP (v%3.3d %6.6s                     ) @ 0x%p\n",
		rsdp->revision, rsdp->oem_id, (void *) rsdp_phys);

	if (rsdp->revision < 2)
		result = acpi_table_compute_checksum(rsdp, sizeof(struct acpi_table_rsdp));
	else
		result = acpi_table_compute_checksum(rsdp, ((struct acpi20_table_rsdp *)rsdp)->length);

	if (result) {
		printk(KERN_WARNING "  >>> ERROR: Invalid checksum\n");
		return -ENODEV;
	}

	/* Locate and map the System Description table (RSDT/XSDT) */

	if (acpi_table_get_sdt(rsdp))
		return -ENODEV;

	return 0;
}

