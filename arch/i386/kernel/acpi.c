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

/*
 * See http://www.geocities.com/SiliconValley/Hardware/3165/
 * for the user-level ACPI stuff
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/sysctl.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/acpi.h>

/*
 * Yes, it's unfortunate that we are relying on get_cmos_time
 * because it is slow (> 1 sec.) and i386 only.	 It might be better
 * to use some of the code from drivers/char/rtc.c in the near future
 */
extern unsigned long get_cmos_time(void);

static int acpi_do_ulong(ctl_table *ctl,
			 int write,
			 struct file *file,
			 void *buffer,
			 size_t *len);
static int acpi_do_event_reg(ctl_table *ctl,
			     int write,
			     struct file *file,
			     void *buffer,
			     size_t *len);
static int acpi_do_event(ctl_table *ctl,
			 int write,
			 struct file *file,
			 void *buffer,
			 size_t *len);
static int acpi_do_sleep(ctl_table *ctl,
			 int write,
			 struct file *file,
			 void *buffer,
			 size_t *len);

static struct ctl_table_header *acpi_sysctl = NULL;

static struct acpi_facp *acpi_facp = NULL;
static int acpi_fake_facp = 0;
static struct acpi_facs *acpi_facs = NULL;
static unsigned long acpi_facp_addr = 0;
static unsigned long acpi_dsdt_addr = 0;

// current system sleep state (S0 - S4)
static acpi_sstate_t acpi_sleep_state = ACPI_S0;
// time sleep began
static unsigned long acpi_sleep_start = 0;

static spinlock_t acpi_event_lock = SPIN_LOCK_UNLOCKED;
static volatile u32 acpi_pm1_status = 0;
static volatile u32 acpi_gpe_status = 0;
static volatile u32 acpi_gpe_level = 0;
static volatile acpi_sstate_t acpi_event_state = ACPI_S0;
static DECLARE_WAIT_QUEUE_HEAD(acpi_event_wait);

/* Make it impossible to enter C2/C3 until after we've initialized */
static unsigned long acpi_enter_lvl2_lat = ACPI_INFINITE_LAT;
static unsigned long acpi_enter_lvl3_lat = ACPI_INFINITE_LAT;
static unsigned long acpi_p_lvl2_lat = ACPI_INFINITE_LAT;
static unsigned long acpi_p_lvl3_lat = ACPI_INFINITE_LAT;

static unsigned long acpi_p_blk = 0;

static int acpi_p_lvl2_tested = 0;
static int acpi_p_lvl3_tested = 0;

enum
{
	ACPI_ENABLED,
	ACPI_TABLES_ONLY,
	ACPI_CHIPSET_ONLY,
	ACPI_DISABLED,
};

static int acpi_enabled = ACPI_ENABLED;

// bits 8-15 are SLP_TYPa, bits 0-7 are SLP_TYPb
static unsigned long acpi_slp_typ[] = 
{
	ACPI_SLP_TYP_DISABLED, /* S0 */
	ACPI_SLP_TYP_DISABLED, /* S1 */
	ACPI_SLP_TYP_DISABLED, /* S2 */
	ACPI_SLP_TYP_DISABLED, /* S3 */
	ACPI_SLP_TYP_DISABLED, /* S4 */
	ACPI_SLP_TYP_DISABLED  /* S5 */
};

static struct ctl_table acpi_table[] =
{
	{ACPI_FACP, "facp",
	 &acpi_facp_addr, sizeof(acpi_facp_addr),
	 0400, NULL, &acpi_do_ulong},

	{ACPI_DSDT, "dsdt",
	 &acpi_dsdt_addr, sizeof(acpi_dsdt_addr),
	 0400, NULL, &acpi_do_ulong},

	{ACPI_PM1_ENABLE, "pm1_enable",
	 NULL, 0,
	 0600, NULL, &acpi_do_event_reg},

	{ACPI_GPE_ENABLE, "gpe_enable",
	 NULL, 0,
	 0600, NULL, &acpi_do_event_reg},

	{ACPI_GPE_LEVEL, "gpe_level",
	 NULL, 0,
	 0600, NULL, &acpi_do_event_reg},

	{ACPI_EVENT, "event", NULL, 0, 0400, NULL, &acpi_do_event},

	{ACPI_P_BLK, "p_blk",
	 &acpi_p_blk, sizeof(acpi_p_blk),
	 0600, NULL, &acpi_do_ulong},

	{ACPI_P_LVL2_LAT, "p_lvl2_lat",
	 &acpi_p_lvl2_lat, sizeof(acpi_p_lvl2_lat),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_P_LVL3_LAT, "p_lvl3_lat",
	 &acpi_p_lvl3_lat, sizeof(acpi_p_lvl3_lat),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_P_LVL2_LAT, "enter_lvl2_lat",
	 &acpi_enter_lvl2_lat, sizeof(acpi_enter_lvl2_lat),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_ENTER_LVL3_LAT, "enter_lvl3_lat",
	 &acpi_enter_lvl3_lat, sizeof(acpi_enter_lvl3_lat),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_S0_SLP_TYP, "s0_slp_typ",
	 &acpi_slp_typ[ACPI_S0], sizeof(acpi_slp_typ[ACPI_S0]),
	 0600, NULL, &acpi_do_ulong},

	{ACPI_S1_SLP_TYP, "s1_slp_typ",
	 &acpi_slp_typ[ACPI_S1], sizeof(acpi_slp_typ[ACPI_S1]),
	 0600, NULL, &acpi_do_ulong},

	{ACPI_S5_SLP_TYP, "s5_slp_typ",
	 &acpi_slp_typ[ACPI_S5], sizeof(acpi_slp_typ[ACPI_S5]),
	 0600, NULL, &acpi_do_ulong},

	{ACPI_SLEEP, "sleep", NULL, 0, 0600, NULL, &acpi_do_sleep},

	{0}
};

static struct ctl_table acpi_dir_table[] =
{
	{CTL_ACPI, "acpi", NULL, 0, 0555, acpi_table},
	{0}
};

static u32 FASTCALL(acpi_read_pm1_control(struct acpi_facp *));
static u32 FASTCALL(acpi_read_pm1_status(struct acpi_facp *));
static u32 FASTCALL(acpi_read_pm1_enable(struct acpi_facp *));
static u32 FASTCALL(acpi_read_gpe_status(struct acpi_facp *));
static u32 FASTCALL(acpi_read_gpe_enable(struct acpi_facp *));

static void FASTCALL(acpi_write_pm1_control(struct acpi_facp *, u32));
static void FASTCALL(acpi_write_pm1_status(struct acpi_facp *, u32));
static void FASTCALL(acpi_write_pm1_enable(struct acpi_facp *, u32));
static void FASTCALL(acpi_write_gpe_status(struct acpi_facp *, u32));
static void FASTCALL(acpi_write_gpe_enable(struct acpi_facp *, u32));

/*
 * Get the value of the PM1 control register (SCI_EN, ...)
 */
static u32 acpi_read_pm1_control(struct acpi_facp *facp)
{
	u32 value = 0;
	if (facp->pm1a_cnt)
		value = inw(facp->pm1a_cnt);
	if (facp->pm1b_cnt)
		value |= inw(facp->pm1b_cnt);
	return value;
}

/*
 * Set the value of the PM1 control register (BM_RLD, ...)
 */
static void acpi_write_pm1_control(struct acpi_facp *facp, u32 value)
{
	if (facp->pm1a_cnt)
		outw(value, facp->pm1a_cnt);
	if (facp->pm1b_cnt)
		outw(value, facp->pm1b_cnt);
}

/*
 * Get the value of the fixed event status register
 */
static u32 acpi_read_pm1_status(struct acpi_facp *facp)
{
	u32 value = 0;
	if (facp->pm1a_evt)
		value = inw(facp->pm1a_evt);
	if (facp->pm1b_evt)
		value |= inw(facp->pm1b_evt);
	return value;
}

/*
 * Set the value of the fixed event status register (clear events)
 */
static void acpi_write_pm1_status(struct acpi_facp *facp, u32 value)
{
	if (facp->pm1a_evt)
		outw(value, facp->pm1a_evt);
	if (facp->pm1b_evt)
		outw(value, facp->pm1b_evt);
}

/*
 * Get the value of the fixed event enable register
 */
static u32 acpi_read_pm1_enable(struct acpi_facp *facp)
{
	int offset = facp->pm1_evt_len >> 1;
	u32 value = 0;
	if (facp->pm1a_evt)
		value = inw(facp->pm1a_evt + offset);
	if (facp->pm1b_evt)
		value |= inw(facp->pm1b_evt + offset);
	return value;
}

/*
 * Set the value of the fixed event enable register (enable events)
 */
static void acpi_write_pm1_enable(struct acpi_facp *facp, u32 value)
{
	int offset = facp->pm1_evt_len >> 1;
	if (facp->pm1a_evt)
		outw(value, facp->pm1a_evt + offset);
	if (facp->pm1b_evt)
		outw(value, facp->pm1b_evt + offset);
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
		for (i = size - 1; i >= 0; i--)
			value = (value << 8) | inb(facp->gpe1 + i);
	}
	if (facp->gpe0) {
		size = facp->gpe0_len >> 1;
		for (i = size - 1; i >= 0; i--)
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

	if (facp->gpe0) {
		size = facp->gpe0_len >> 1;
		for (i = 0; i < size; i++) {
			outb(value & 0xff, facp->gpe0 + i);
			value >>= 8;
		}
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
 * Get the value of the general-purpose event enable register
 */
static u32 acpi_read_gpe_enable(struct acpi_facp *facp)
{
	u32 value = 0;
	int i, size, offset;
	
	offset = facp->gpe0_len >> 1;
	if (facp->gpe1) {
		size = facp->gpe1_len >> 1;
		for (i = size - 1; i >= 0; i--) {
			value = (value << 8) | inb(facp->gpe1 + offset + i);
		}
	}
	if (facp->gpe0) {
		size = facp->gpe0_len >> 1;
		for (i = size - 1; i >= 0; i--)
			value = (value << 8) | inb(facp->gpe0 + offset + i);
	}
	return value;
}

/*
 * Set the value of the general-purpose event enable register (enable events)
 */
static void acpi_write_gpe_enable(struct acpi_facp *facp, u32 value)
{
	int i, offset;

	offset = facp->gpe0_len >> 1;
	if (facp->gpe0) {
		for (i = 0; i < offset; i++) {
			outb(value & 0xff, facp->gpe0 + offset + i);
			value >>= 8;
		}
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
			ioremap((unsigned long) addr,
				sizeof(struct acpi_table));
		if (table) {
			unsigned long table_size = table->length;
			iounmap(table);
			// remap entire table
			table = (struct acpi_table *)
				ioremap((unsigned long) addr, table_size);
		}

		if (!table && addr < virt_to_phys(high_memory)) {
			/* sometimes we see ACPI tables in low memory
			 * and not reserved by the memory map (E820) code,
			 * who is at fault for this?  BIOS?
			 */
			printk(KERN_ERR
			       "ACPI: unreserved table memory @ 0x%p!\n",
			       (void*) addr);
		}
	}
	return table;
}

/*
 * Unmap an ACPI table from virtual memory
 */
static void acpi_unmap_table(struct acpi_table *table)
{
	// iounmap ignores addresses within physical memory
	if (table)
		iounmap(table);
}

/*
 * Locate and map ACPI tables
 */
static int __init acpi_find_tables(void)
{
	struct acpi_rsdp *rsdp;
	struct acpi_table *rsdt;
	u32 *rsdt_entry;
	int rsdt_entry_count;
	unsigned long i;

	// search BIOS memory for RSDP
	for (i = ACPI_BIOS_ROM_BASE; i < ACPI_BIOS_ROM_END; i += 16) {
		rsdp = (struct acpi_rsdp *) phys_to_virt(i);
		if (rsdp->signature[0] == ACPI_RSDP1_SIG
		    && rsdp->signature[1] == ACPI_RSDP2_SIG) {
			char oem[7];
			int j;

			// strip trailing space and print OEM identifier
			memcpy(oem, rsdp->oem, 6);
			oem[6] = '\0';
			for (j = 5;
			     j > 0 && (oem[j] == '\0' || oem[j] == ' ');
			     j--) {
				oem[j] = '\0';
			}
			printk(KERN_INFO "ACPI: \"%s\" found at 0x%p\n",
			       oem, (void *) i);

			break;
		}
	}
	if (i >= ACPI_BIOS_ROM_END)
		return -ENODEV;

	// fetch RSDT from RSDP
	rsdt = acpi_map_table(rsdp->rsdt);
	if (!rsdt) {
		printk(KERN_ERR "ACPI: missing RSDT at 0x%p\n",
		       (void*) rsdp->rsdt);
		return -ENODEV;
	}
	else if (rsdt->signature != ACPI_RSDT_SIG) {
		printk(KERN_ERR "ACPI: bad RSDT at 0x%p (%08x)\n",
		       (void*) rsdp->rsdt, (unsigned) rsdt->signature);
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
			acpi_facp = (struct acpi_facp*) dt;
			acpi_facp_addr = *rsdt_entry;
			acpi_dsdt_addr = acpi_facp->dsdt;

			// map FACS if it exists
			if (acpi_facp->facs) {
				dt = acpi_map_table(acpi_facp->facs);
				if (dt && dt->signature == ACPI_FACS_SIG) {
					acpi_facs = (struct acpi_facs*) dt;
				}
				else {
					acpi_unmap_table(dt);
				}
			}
		}
		else {
			acpi_unmap_table(dt);
		}
		rsdt_entry++;
		rsdt_entry_count--;
	}

	acpi_unmap_table(rsdt);

	if (!acpi_facp) {
		printk(KERN_ERR "ACPI: missing FACP\n");
		return -ENODEV;
	}
	return 0;
}

/*
 * Unmap or destroy ACPI tables
 */
static void acpi_destroy_tables(void)
{
	if (!acpi_fake_facp)
		acpi_unmap_table((struct acpi_table*) acpi_facp);
	else
		kfree(acpi_facp);
	acpi_unmap_table((struct acpi_table*) acpi_facs);
}

/*
 * Init PIIX4 device, create a fake FACP
 */
static int __init acpi_init_piix4(struct pci_dev *dev)
{
	u32 base;
	u16 cmd;
	u8 pmregmisc;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_IO))
		return -ENODEV;
	
	pci_read_config_byte(dev, ACPI_PIIX4_PMREGMISC, &pmregmisc);
	if (!(pmregmisc & ACPI_PIIX4_PMIOSE))
		return -ENODEV;
	
	pci_read_config_dword(dev, 0x40, &base);
	if (!(base & PCI_BASE_ADDRESS_SPACE_IO))
		return -ENODEV;
	
	base &= PCI_BASE_ADDRESS_IO_MASK;
	if (!base)
		return -ENODEV;

	printk(KERN_INFO "ACPI: found PIIX4 at 0x%04x\n", base);

	acpi_facp = kmalloc(sizeof(struct acpi_facp), GFP_KERNEL);
	if (!acpi_facp)
		return -ENOMEM;

	acpi_fake_facp = 1;
	memset(acpi_facp, 0, sizeof(struct acpi_facp));
	acpi_facp->int_model = ACPI_PIIX4_INT_MODEL;
	acpi_facp->sci_int = ACPI_PIIX4_SCI_INT;
	acpi_facp->smi_cmd = ACPI_PIIX4_SMI_CMD;
	acpi_facp->acpi_enable = ACPI_PIIX4_ACPI_ENABLE;
	acpi_facp->acpi_disable = ACPI_PIIX4_ACPI_DISABLE;
	acpi_facp->s4bios_req = ACPI_PIIX4_S4BIOS_REQ;
	acpi_facp->pm1a_evt = base + ACPI_PIIX4_PM1_EVT;
	acpi_facp->pm1a_cnt = base + ACPI_PIIX4_PM1_CNT;
	acpi_facp->pm2_cnt = ACPI_PIIX4_PM2_CNT;
	acpi_facp->pm_tmr = base + ACPI_PIIX4_PM_TMR;
	acpi_facp->gpe0 = base + ACPI_PIIX4_GPE0;
	acpi_facp->pm1_evt_len = ACPI_PIIX4_PM1_EVT_LEN;
	acpi_facp->pm1_cnt_len = ACPI_PIIX4_PM1_CNT_LEN;
	acpi_facp->pm2_cnt_len = ACPI_PIIX4_PM2_CNT_LEN;
	acpi_facp->pm_tm_len = ACPI_PIIX4_PM_TM_LEN;
	acpi_facp->gpe0_len = ACPI_PIIX4_GPE0_LEN;
	acpi_facp->p_lvl2_lat = (__u16) ACPI_INFINITE_LAT;
	acpi_facp->p_lvl3_lat = (__u16) ACPI_INFINITE_LAT;

	acpi_facp_addr = virt_to_phys(acpi_facp);
	acpi_dsdt_addr = 0;

	acpi_p_blk = base + ACPI_PIIX4_P_BLK;

	return 0;
}

/*
 * Init VIA ACPI device and create a fake FACP
 */
static int __init acpi_init_via(struct pci_dev *dev)
{
	u32 base;
	u8 tmp, irq;

	pci_read_config_byte(dev, 0x41, &tmp);
	if (!(tmp & 0x80))
		return -ENODEV;

	pci_read_config_byte(dev, PCI_CLASS_REVISION, &tmp);
	tmp = (tmp & 0x10 ? 0x48 : 0x20);

	pci_read_config_dword(dev, tmp, &base);
	if (!(base & PCI_BASE_ADDRESS_SPACE_IO))
		return -ENODEV;

	base &= PCI_BASE_ADDRESS_IO_MASK;
	if (!base)
		return -ENODEV;

	pci_read_config_byte(dev, 0x42, &irq);

	printk(KERN_INFO "ACPI: found %s at 0x%04x\n", dev->name, base);

	acpi_facp = kmalloc(sizeof(struct acpi_facp), GFP_KERNEL);
	if (!acpi_facp)
		return -ENOMEM;

	acpi_fake_facp = 1;
	memset(acpi_facp, 0, sizeof(struct acpi_facp));

	acpi_facp->int_model = ACPI_VIA_INT_MODEL;
	acpi_facp->sci_int = irq;
	acpi_facp->smi_cmd = base + ACPI_VIA_SMI_CMD;
	acpi_facp->acpi_enable = ACPI_VIA_ACPI_ENABLE;
	acpi_facp->acpi_disable = ACPI_VIA_ACPI_DISABLE;
	acpi_facp->pm1a_evt = base + ACPI_VIA_PM1_EVT;
	acpi_facp->pm1a_cnt = base + ACPI_VIA_PM1_CNT;
	acpi_facp->pm_tmr = base + ACPI_VIA_PM_TMR;
	acpi_facp->gpe0 = base + ACPI_VIA_GPE0;

	acpi_facp->pm1_evt_len = ACPI_VIA_PM1_EVT_LEN;
	acpi_facp->pm1_cnt_len = ACPI_VIA_PM1_CNT_LEN;
	acpi_facp->pm_tm_len = ACPI_VIA_PM_TM_LEN;
	acpi_facp->gpe0_len = ACPI_VIA_GPE0_LEN;
	acpi_facp->p_lvl2_lat = (__u16) ACPI_INFINITE_LAT;
	acpi_facp->p_lvl3_lat = (__u16) ACPI_INFINITE_LAT;

	acpi_facp->duty_offset = ACPI_VIA_DUTY_OFFSET;
	acpi_facp->duty_width = ACPI_VIA_DUTY_WIDTH;

	acpi_facp->day_alarm = ACPI_VIA_DAY_ALARM;
	acpi_facp->mon_alarm = ACPI_VIA_MON_ALARM;
	acpi_facp->century = ACPI_VIA_CENTURY;

	acpi_facp_addr = virt_to_phys(acpi_facp);
	acpi_dsdt_addr = 0;

	acpi_p_blk = base + ACPI_VIA_P_BLK;

	return 0;
}

typedef enum
{
	CH_UNKNOWN = 0,
	CH_INTEL_PIIX4,
	CH_VIA_586,
	CH_VIA_686A,
} acpi_chip_t;

/* indexed by value of each enum in acpi_chip_t */
const static struct
{
	int (*chip_init)(struct pci_dev *dev);
} acpi_chip_info[] =
{
	{NULL,},
	{acpi_init_piix4},
	{acpi_init_via},
	{acpi_init_via},
};
	
const static struct pci_device_id acpi_pci_tbl[] =
{
	{0x8086, 0x7113, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_INTEL_PIIX4},
	{0x1106, 0x3040, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_VIA_586},
	{0x1106, 0x3057, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_VIA_686A},
	{0,}, /* terminate list */
};

static int __init acpi_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	return acpi_chip_info[id->driver_data].chip_init(dev);
}

static struct pci_driver acpi_driver =
{
	name:		"acpi",
	id_table:	acpi_pci_tbl,
	probe:		acpi_probe,
};
static int pci_driver_registered = 0;

/*
 * Locate a known ACPI chipset
 */
static int __init acpi_find_chipset(void)
{
	if (pci_register_driver(&acpi_driver) < 1)
		return -ENODEV;

	pci_driver_registered = 1;

	return 0;
}

/*
 * Handle an ACPI SCI (fixed or general purpose event)
 */
static void acpi_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	u32 pm1_status, gpe_status, gpe_level, gpe_edge;
	unsigned long flags;

	// detect and clear fixed events
	pm1_status = (acpi_read_pm1_status(acpi_facp)
		      & acpi_read_pm1_enable(acpi_facp));
	acpi_write_pm1_status(acpi_facp, pm1_status);
	
	// detect and handle general-purpose events
	gpe_status = (acpi_read_gpe_status(acpi_facp)
		      & acpi_read_gpe_enable(acpi_facp));
	gpe_level = gpe_status & acpi_gpe_level;
	if (gpe_level) {
		// disable level-triggered events (re-enabled after handling)
		acpi_write_gpe_enable(
			acpi_facp,
			acpi_read_gpe_enable(acpi_facp) & ~gpe_level);
	}
	gpe_edge = gpe_status & ~gpe_level;
	if (gpe_edge) {
		// clear edge-triggered events
		while (acpi_read_gpe_status(acpi_facp) & gpe_edge)
			acpi_write_gpe_status(acpi_facp, gpe_edge);
	}

	// notify process waiting on /dev/acpi
	spin_lock_irqsave(&acpi_event_lock, flags);
	acpi_pm1_status |= pm1_status;
	acpi_gpe_status |= gpe_status;
	spin_unlock_irqrestore(&acpi_event_lock, flags);
	acpi_event_state = acpi_sleep_state;
	wake_up_interruptible(&acpi_event_wait);
}

/*
 * Is ACPI enabled or not?
 */
static inline int acpi_is_enabled(struct acpi_facp *facp)
{
	return ((acpi_read_pm1_control(facp) & ACPI_SCI_EN) ? 1:0);
}

/*
 * Enable SCI
 */
static int acpi_enable(struct acpi_facp *facp)
{
	if (facp->smi_cmd)
		outb(facp->acpi_enable, facp->smi_cmd);
	return (acpi_is_enabled(facp) ? 0:-1);
}

/*
 * Disable SCI
 */
static int acpi_disable(struct acpi_facp *facp)
{
	// disable and clear any pending events
	acpi_write_gpe_enable(facp, 0);
	while (acpi_read_gpe_status(facp))
		acpi_write_gpe_status(facp, acpi_read_gpe_status(facp));
	acpi_write_pm1_enable(facp, 0);
	acpi_write_pm1_status(facp, acpi_read_pm1_status(facp));

	/* writing acpi_disable to smi_cmd would be appropriate
	 * here but this causes a nasty crash on many systems
	 */

	return 0;
}

static inline int bm_activity(struct acpi_facp *facp)
{
	return acpi_read_pm1_status(facp) & ACPI_BM;
}

static inline void clear_bm_activity(struct acpi_facp *facp)
{
	acpi_write_pm1_status(facp, ACPI_BM);
}

static void sleep_on_busmaster(struct acpi_facp *facp)
{
	u32 pm1_cntr = acpi_read_pm1_control(facp);
	if (pm1_cntr & ACPI_BM_RLD) {
		pm1_cntr &= ~ACPI_BM_RLD;
		acpi_write_pm1_control(facp, pm1_cntr);
	}
}

static void wake_on_busmaster(struct acpi_facp *facp)
{
	u32 pm1_cntr = acpi_read_pm1_control(facp);
	if (!(pm1_cntr & ACPI_BM_RLD)) {
		pm1_cntr |= ACPI_BM_RLD;
		acpi_write_pm1_control(facp, pm1_cntr);
	}
	clear_bm_activity(facp);
}

/* The ACPI timer is just the low 24 bits */
#define TIME_BEGIN(tmr)			inl(tmr)
#define TIME_END(tmr, begin)		((inl(tmr) - (begin)) & 0x00ffffff)


/*
 * Idle loop (uniprocessor only)
 */
static void acpi_idle(void)
{
	static int sleep_level = 1;
	struct acpi_facp *facp = acpi_facp;

	if (!facp || !facp->pm_tmr || !acpi_p_blk)
		goto not_initialized;

	/*
	 * start from the previous sleep level..
	 */
	if (sleep_level == 1)
		goto sleep1;
	if (sleep_level == 2)
		goto sleep2;
sleep3:
	sleep_level = 3;
	if (!acpi_p_lvl3_tested) {
		printk("ACPI C3 works\n");
		acpi_p_lvl3_tested = 1;
	}
	wake_on_busmaster(facp);
	if (facp->pm2_cnt)
		goto sleep3_with_arbiter;

	for (;;) {
		unsigned long time;
		unsigned int pm_tmr = facp->pm_tmr;

		__cli();
		if (current->need_resched)
			goto out;
		if (bm_activity(facp))
			goto sleep2;

		time = TIME_BEGIN(pm_tmr);
		inb(acpi_p_blk + ACPI_P_LVL3);
		inl(pm_tmr);					/* Dummy read, force synchronization with the PMU */
		time = TIME_END(pm_tmr, time);

		__sti();
		if (time < acpi_p_lvl3_lat)
			goto sleep2;
	}

sleep3_with_arbiter:
	for (;;) {
		unsigned long time;
		u8 arbiter;
		unsigned int pm2_cntr = facp->pm2_cnt;
		unsigned int pm_tmr = facp->pm_tmr;

		__cli();
		if (current->need_resched)
			goto out;
		if (bm_activity(facp))
			goto sleep2;

		time = TIME_BEGIN(pm_tmr);
		arbiter = inb(pm2_cntr) & ~ACPI_ARB_DIS;
		outb(arbiter | ACPI_ARB_DIS, pm2_cntr);		/* Disable arbiter, park on CPU */
		inb(acpi_p_blk + ACPI_P_LVL3);
		inl(pm_tmr);					/* Dummy read, force synchronization with the PMU */
		time = TIME_END(pm_tmr, time);
		outb(arbiter, pm2_cntr);			/* Enable arbiter again.. */

		__sti();
		if (time < acpi_p_lvl3_lat)
			goto sleep2;
	}

sleep2:
	sleep_level = 2;
	if (!acpi_p_lvl2_tested) {
		printk("ACPI C2 works\n");
		acpi_p_lvl2_tested = 1;
	}
	wake_on_busmaster(facp);	/* Required to track BM activity.. */
	for (;;) {
		unsigned long time;
		unsigned int pm_tmr = facp->pm_tmr;

		__cli();
		if (current->need_resched)
			goto out;

		time = TIME_BEGIN(pm_tmr);
		inb(acpi_p_blk + ACPI_P_LVL2);
		inl(pm_tmr);					/* Dummy read, force synchronization with the PMU */
		time = TIME_END(pm_tmr, time);

		__sti();
		if (time < acpi_p_lvl2_lat)
			goto sleep1;
		if (bm_activity(facp)) {
			clear_bm_activity(facp);
			continue;
		}
		if (time > acpi_enter_lvl3_lat)
			goto sleep3;
	}

sleep1:
	sleep_level = 1;
	sleep_on_busmaster(facp);
	for (;;) {
		unsigned long time;
		unsigned int pm_tmr = facp->pm_tmr;

		__cli();
		if (current->need_resched)
			goto out;
		time = TIME_BEGIN(pm_tmr);
		__asm__ __volatile__("sti ; hlt": : :"memory");
		time = TIME_END(pm_tmr, time);
		if (time > acpi_enter_lvl2_lat)
			goto sleep2;
	}

not_initialized:
	for (;;) {
		__cli();
		if (current->need_resched)
			goto out;
		__asm__ __volatile__("sti ; hlt": : :"memory");
	}

out:
	__sti();
}

/*
 * Put all devices into specified D-state
 */
static int acpi_enter_dx(acpi_dstate_t state)
{
	int status = 0;
	
	if (state == ACPI_D0)
		status = pm_send_all(PM_RESUME, (void*) state);
	else
		status = pm_send_all(PM_SUSPEND, (void*) state);

	return status;
}

/*
 * Update system time from real-time clock
 */
static void acpi_update_clock(void)
{
	if (acpi_sleep_start) {
		unsigned long delta;
		struct timeval tv;
		
		delta = get_cmos_time() - acpi_sleep_start;
		do_gettimeofday(&tv);
		tv.tv_sec += delta;
		do_settimeofday(&tv);
		
		acpi_sleep_start = 0;
	}
}


/*
 * Enter system sleep state
 */
static void acpi_enter_sx(acpi_sstate_t state)
{
	unsigned long slp_typ = acpi_slp_typ[(int) state];
	if (slp_typ != ACPI_SLP_TYP_DISABLED) {
		u16 typa, typb, value;

		// bits 8-15 are SLP_TYPa, bits 0-7 are SLP_TYPb
		typa = (slp_typ >> 8) & 0xff;
		typb = slp_typ & 0xff;

		typa = ((typa << ACPI_SLP_TYP_SHIFT) & ACPI_SLP_TYP_MASK);
		typb = ((typb << ACPI_SLP_TYP_SHIFT) & ACPI_SLP_TYP_MASK);

		acpi_sleep_start = get_cmos_time();
		acpi_enter_dx(ACPI_D3);
		acpi_sleep_state = state;

		// clear wake status
		acpi_write_pm1_status(acpi_facp, ACPI_WAK);

		// set SLP_TYPa/b and SLP_EN
		if (acpi_facp->pm1a_cnt) {
			value = inw(acpi_facp->pm1a_cnt) & ~ACPI_SLP_TYP_MASK;
			outw(value | typa | ACPI_SLP_EN, acpi_facp->pm1a_cnt);
		}
		if (acpi_facp->pm1b_cnt) {
			value = inw(acpi_facp->pm1b_cnt) & ~ACPI_SLP_TYP_MASK;
			outw(value | typb | ACPI_SLP_EN, acpi_facp->pm1b_cnt);
		}

		// wait until S1 is entered
		while (!(acpi_read_pm1_status(acpi_facp) & ACPI_WAK)) ;
		// finished sleeping, update system time
		acpi_update_clock();
		acpi_enter_dx(ACPI_D0);
	}
}

/*
 * Enter soft-off (S5)
 */
static void acpi_power_off(void)
{
	acpi_enter_sx(ACPI_S5);
}

/*
 * Claim I/O port if available
 */
static int acpi_claim(unsigned long start, unsigned long size)
{
	if (start && size) {
		if (check_region(start, size))
			return -EBUSY;
		request_region(start, size, "acpi");
	}
	return 0;
}

/*
 * Claim ACPI I/O ports
 */
static int acpi_claim_ioports(struct acpi_facp *facp)
{
	// we don't get a guarantee of contiguity for any of the ACPI registers
	if (acpi_claim(facp->pm1a_evt, facp->pm1_evt_len)
	    || acpi_claim(facp->pm1b_evt, facp->pm1_evt_len)
	    || acpi_claim(facp->pm1a_cnt, facp->pm1_cnt_len)
	    || acpi_claim(facp->pm1b_cnt, facp->pm1_cnt_len)
	    || acpi_claim(facp->pm_tmr, facp->pm_tm_len)
	    || acpi_claim(facp->gpe0, facp->gpe0_len)
	    || acpi_claim(facp->gpe1, facp->gpe1_len))
		return -EBUSY;
	return 0;
}

/*
 * Release I/O port if claimed
 */
static void acpi_release(unsigned long start, unsigned long size)
{
	if (start && size)
		release_region(start, size);
}

/*
 * Free ACPI I/O ports
 */
static int acpi_release_ioports(struct acpi_facp *facp)
{
	// we don't get a guarantee of contiguity for any of the ACPI registers
	acpi_release(facp->gpe1, facp->gpe1_len);
	acpi_release(facp->gpe0, facp->gpe0_len);
	acpi_release(facp->pm_tmr, facp->pm_tm_len);
	acpi_release(facp->pm1b_cnt, facp->pm1_cnt_len);
	acpi_release(facp->pm1a_cnt, facp->pm1_cnt_len);
	acpi_release(facp->pm1b_evt, facp->pm1_evt_len);
	acpi_release(facp->pm1a_evt, facp->pm1_evt_len);
	return 0;
}

/*
 * Examine/modify value
 */
static int acpi_do_ulong(ctl_table *ctl,
			 int write,
			 struct file *file,
			 void *buffer,
			 size_t *len)
{
	char str[2 * sizeof(unsigned long) + 4], *strend;
	unsigned long val;
	int size;

	if (!write) {
		if (file->f_pos) {
			*len = 0;
			return 0;
		}

		val = *(unsigned long*) ctl->data;
		size = sprintf(str, "0x%08lx\n", val);
		if (*len >= size) {
			copy_to_user(buffer, str, size);
			*len = size;
		}
		else
			*len = 0;
	}
	else {
		size = sizeof(str) - 1;
		if (size > *len)
			size = *len;
		copy_from_user(str, buffer, size);
		str[size] = '\0';
		val = simple_strtoul(str, &strend, 0);
		if (strend == str)
			return -EINVAL;
		*(unsigned long*) ctl->data = val;
	}

	file->f_pos += *len;
	return 0;
}

/*
 * Examine/modify event register
 */
static int acpi_do_event_reg(ctl_table *ctl,
			     int write,
			     struct file *file,
			     void *buffer,
			     size_t *len)
{
	char str[2 * sizeof(u32) + 4], *strend;
	u32 val, enabling;
	int size;

	if (!write) {
		if (file->f_pos) {
			*len = 0;
			return 0;
		}

		val = 0;
		switch (ctl->ctl_name) {
		case ACPI_PM1_ENABLE:
			val = acpi_read_pm1_enable(acpi_facp);
			break;
		case ACPI_GPE_ENABLE:
			val = acpi_read_gpe_enable(acpi_facp);
			break;
		case ACPI_GPE_LEVEL:
			val = acpi_gpe_level;
			break;
		}
		
		size = sprintf(str, "0x%08x\n", val);
		if (*len >= size) {
			copy_to_user(buffer, str, size);
			*len = size;
		}
		else
			*len = 0;
	}
	else
	{
		// fetch user value
		size = sizeof(str) - 1;
		if (size > *len)
			size = *len;
		copy_from_user(str, buffer, size);
		str[size] = '\0';
		val = (u32) simple_strtoul(str, &strend, 0);
		if (strend == str)
			return -EINVAL;

		// store value in register
		switch (ctl->ctl_name) {
		case ACPI_PM1_ENABLE:
			// clear previously disabled events
			enabling = (val
				    & ~acpi_read_pm1_enable(acpi_facp));
			acpi_write_pm1_status(acpi_facp, enabling);
			
			if (val) {
				// enable ACPI unless it is already
				if (!acpi_is_enabled(acpi_facp))
					acpi_enable(acpi_facp);
			}
			else if (!acpi_read_gpe_enable(acpi_facp)) {
				// disable ACPI unless it is already
				if (acpi_is_enabled(acpi_facp))
					acpi_disable(acpi_facp);
			}
			
			acpi_write_pm1_enable(acpi_facp, val);
			break;
		case ACPI_GPE_ENABLE:
			// clear previously disabled events
			enabling = (val
				    & ~acpi_read_gpe_enable(acpi_facp));
			while (acpi_read_gpe_status(acpi_facp) & enabling)
				acpi_write_gpe_status(acpi_facp, enabling);
			
			if (val) {
				// enable ACPI unless it is already
				if (!acpi_is_enabled(acpi_facp))
					acpi_enable(acpi_facp);
			}
			else if (!acpi_read_pm1_enable(acpi_facp)) {
				// disable ACPI unless it is already
				if (acpi_is_enabled(acpi_facp))
					acpi_disable(acpi_facp);
			}
			
			acpi_write_gpe_enable(acpi_facp, val);
			break;
		case ACPI_GPE_LEVEL:
			acpi_gpe_level = val;
			break;
		}
	}

	file->f_pos += *len;
	return 0;
}

/*
 * Wait for next event
 */
static int acpi_do_event(ctl_table *ctl,
			 int write,
			 struct file *file,
			 void *buffer,
			 size_t *len)
{
	u32 pm1_status = 0, gpe_status = 0;
	acpi_sstate_t event_state = 0;
	char str[27];
	int size;

	if (write)
		return -EPERM;
	if (*len < sizeof(str)) {
		*len = 0;
		return 0;
	}

	for (;;) {
		unsigned long flags;
		
		// we need an atomic exchange here
		spin_lock_irqsave(&acpi_event_lock, flags);
		pm1_status = acpi_pm1_status;
		acpi_pm1_status = 0;
		gpe_status = acpi_gpe_status;
		acpi_gpe_status = 0;
		spin_unlock_irqrestore(&acpi_event_lock, flags);
		event_state = acpi_event_state;
		
		if (pm1_status || gpe_status)
			break;
		
		// wait for an event to arrive
		interruptible_sleep_on(&acpi_event_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	size = sprintf(str, "0x%08x 0x%08x 0x%01x\n",
		       pm1_status,
		       gpe_status,
		       event_state);
	copy_to_user(buffer, str, size);
	*len = size;
	file->f_pos += size;

	return 0;
}

/*
 * Enter system sleep state
 */
static int acpi_do_sleep(ctl_table *ctl,
			 int write,
			 struct file *file,
			 void *buffer,
			 size_t *len)
{
	if (!write) {
		if (file->f_pos) {
			*len = 0;
			return 0;
		}
	}
	else
	{
#ifdef CONFIG_ACPI_S1_SLEEP
		acpi_enter_sx(ACPI_S1);
#endif
	}
	file->f_pos += *len;
	return 0;
}


/*
 * Initialize and enable ACPI
 */
static int __init acpi_init(void)
{
	switch(acpi_enabled)
	{
	case ACPI_ENABLED:
		if (acpi_find_tables() && acpi_find_chipset())
			return -ENODEV;
		break;
	case ACPI_TABLES_ONLY:
		if (acpi_find_tables())
			return -ENODEV;
		break;
	case ACPI_CHIPSET_ONLY:
		if (acpi_find_chipset())
			return -ENODEV;
		break;
	case ACPI_DISABLED:
		return -ENODEV;
	}

	/*
	 * Internally we always keep latencies in timer
	 * ticks, which is simpler and more consistent (what is
	 * an uS to us?). Besides, that gives people more
	 * control in the /proc interfaces.
	 */
	if (acpi_facp->p_lvl2_lat
	    && acpi_facp->p_lvl2_lat <= ACPI_MAX_P_LVL2_LAT) {
		acpi_p_lvl2_lat = ACPI_uS_TO_TMR_TICKS(acpi_facp->p_lvl2_lat);
		acpi_enter_lvl2_lat = ACPI_uS_TO_TMR_TICKS(ACPI_TMR_HZ / 1000);
	}
	if (acpi_facp->p_lvl3_lat
	    && acpi_facp->p_lvl3_lat <= ACPI_MAX_P_LVL3_LAT) {
		acpi_p_lvl3_lat = ACPI_uS_TO_TMR_TICKS(acpi_facp->p_lvl3_lat);
		acpi_enter_lvl3_lat
			= ACPI_uS_TO_TMR_TICKS(acpi_facp->p_lvl3_lat * 5);
	}

	if (acpi_claim_ioports(acpi_facp)) {
		printk(KERN_ERR "ACPI: I/O port allocation failed\n");
		goto err_out;
	}

	if (acpi_facp->sci_int
	    && request_irq(acpi_facp->sci_int,
			   acpi_irq,
			   SA_INTERRUPT | SA_SHIRQ,
			   "acpi",
			   acpi_facp)) {
		printk(KERN_ERR "ACPI: SCI (IRQ%d) allocation failed\n",
		       acpi_facp->sci_int);
		goto err_out;
	}

	acpi_sysctl = register_sysctl_table(acpi_dir_table, 1);

	pm_power_off = acpi_power_off;

	pm_active = 1;

	/*
	 * Set up the ACPI idle function. Note that we can't really
	 * do this with multiple CPU's, we'd need a per-CPU ACPI
	 * device..
	 */
#ifdef CONFIG_SMP
	if (smp_num_cpus > 1)
		return 0;
#endif

	if (acpi_facp->pm_tmr)
		pm_idle = acpi_idle;

	return 0;

err_out:
	if (pci_driver_registered)
		pci_unregister_driver(&acpi_driver);
	acpi_destroy_tables();

	return -ENODEV;
}

/*
 * Disable and deinitialize ACPI
 */
static void __exit acpi_exit(void)
{
	pm_idle = NULL;
	pm_active = 0;
	pm_power_off = NULL;

	unregister_sysctl_table(acpi_sysctl);
	acpi_disable(acpi_facp);
	acpi_release_ioports(acpi_facp);

	if (acpi_facp->sci_int)
		free_irq(acpi_facp->sci_int, acpi_facp);

	acpi_destroy_tables();

	if (pci_driver_registered)
		pci_unregister_driver(&acpi_driver);
}

/*
 * Parse kernel command line options
 */
static int __init acpi_setup(char *str)
{
	while (str && *str) {
		if (strncmp(str, "on", 2) == 0)
			acpi_enabled = ACPI_ENABLED;
		else if (strncmp(str, "tables", 6) == 0)
			acpi_enabled = ACPI_TABLES_ONLY;
		else if (strncmp(str, "chipset", 7) == 0)
			acpi_enabled = ACPI_CHIPSET_ONLY;
		else if (strncmp(str, "off", 3) == 0)
			acpi_enabled = ACPI_DISABLED;
		str = strpbrk(str, ",");
		if (str)
			str += strspn(str, ",");
	}
	return 1;
}

__setup("acpi=", acpi_setup);

module_init(acpi_init);
module_exit(acpi_exit);
