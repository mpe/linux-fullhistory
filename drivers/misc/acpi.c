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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/sysctl.h>
#include <linux/delay.h>
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

static int acpi_idle_thread(void *context);
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

DECLARE_WAIT_QUEUE_HEAD(acpi_idle_wait);

static struct ctl_table_header *acpi_sysctl = NULL;

static struct acpi_facp *acpi_facp = NULL;
static int acpi_fake_facp = 0;
static struct acpi_facs *acpi_facs = NULL;
static unsigned long acpi_facp_addr = 0;
static unsigned long acpi_dsdt_addr = 0;

static spinlock_t acpi_event_lock = SPIN_LOCK_UNLOCKED;
static volatile u32 acpi_pm1_status = 0;
static volatile u32 acpi_gpe_status = 0;
static volatile u32 acpi_gpe_level = 0;
static DECLARE_WAIT_QUEUE_HEAD(acpi_event_wait);

static spinlock_t acpi_devs_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(acpi_devs);

/* Make it impossible to enter L2/L3 until after we've initialized */
static unsigned long acpi_p_lvl2_lat = ~0UL;
static unsigned long acpi_p_lvl3_lat = ~0UL;

/* Initialize to guaranteed harmless port read */
static unsigned long acpi_p_lvl2 = ACPI_P_LVL_DISABLED;
static unsigned long acpi_p_lvl3 = ACPI_P_LVL_DISABLED;

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

	{ACPI_P_LVL2, "p_lvl2",
	 &acpi_p_lvl2, sizeof(acpi_p_lvl2),
	 0600, NULL, &acpi_do_ulong},

	{ACPI_P_LVL3, "p_lvl3",
	 &acpi_p_lvl3, sizeof(acpi_p_lvl3),
	 0600, NULL, &acpi_do_ulong},

	{ACPI_P_LVL2_LAT, "p_lvl2_lat",
	 &acpi_p_lvl2_lat, sizeof(acpi_p_lvl2_lat),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_P_LVL3_LAT, "p_lvl3_lat",
	 &acpi_p_lvl3_lat, sizeof(acpi_p_lvl3_lat),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_S5_SLP_TYP, "s5_slp_typ",
	 &acpi_slp_typ[5], sizeof(acpi_slp_typ[5]),
	 0600, NULL, &acpi_do_ulong},

	{0}
};

static struct ctl_table acpi_dir_table[] =
{
	{CTL_ACPI, "acpi", NULL, 0, 0555, acpi_table},
	{0}
};


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
			ioremap_nocache((unsigned long) addr,
					sizeof(struct acpi_table));
		if (table) {
			unsigned long table_size = table->length;
			iounmap(table);
			// remap entire table
			table = (struct acpi_table *)
				ioremap_nocache((unsigned long) addr,
						table_size);
		}
	}
	return table;
}

/*
 * Unmap an ACPI table from virtual memory
 */
static void acpi_unmap_table(struct acpi_table *table)
{
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
	if (!rsdt || rsdt->signature != ACPI_RSDT_SIG) {
		printk(KERN_ERR "ACPI: missing RSDT\n");
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

			if (acpi_facp->facs) {
				acpi_facs = (struct acpi_facs*)
					acpi_map_table(acpi_facp->facs);
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
 * Locate PIIX4 device and create a fake FACP
 */
static int __init acpi_find_piix4(void)
{
	struct pci_dev *dev;
	u32 base;
	u16 cmd;
	u8 pmregmisc;

	dev = pci_find_device(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82371AB_3,
			      NULL);
	if (!dev)
		return -ENODEV;
	
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
	acpi_facp->p_lvl2_lat = ~0;
	acpi_facp->p_lvl3_lat = ~0;

	acpi_facp_addr = virt_to_phys(acpi_facp);
	acpi_dsdt_addr = 0;

	acpi_p_lvl2 = base + ACPI_PIIX4_P_LVL2;
	acpi_p_lvl3 = base + ACPI_PIIX4_P_LVL3;

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

	if (facp->smi_cmd)
		outb(facp->acpi_disable, facp->smi_cmd);
	return (acpi_is_enabled(facp) ? -1:0);
}

/*
 * Idle loop
 */
static void acpi_idle_handler(void)
{
	static int sleep_level = 1;
	u32 timer, pm2_cnt;
	unsigned long time;

	// get current time (fallback to CPU cycles if no PM timer)
	timer = acpi_facp->pm_tmr;
	if (timer)
		time = inl(timer);
	else
		time = get_cycles();

	// sleep
	switch (sleep_level) {
	case 1:
		__asm__ __volatile__("sti ; hlt": : :"memory");
		break;
	case 2:
		inb(acpi_p_lvl2);
		break;
	case 3:
		pm2_cnt = acpi_facp->pm2_cnt;
		if (pm2_cnt) {
				/* Disable PCI arbitration while sleeping,
				   to avoid DMA corruption? */
			outb(inb(pm2_cnt) | ACPI_ARB_DIS, pm2_cnt);
			inb(acpi_p_lvl3);
			outb(inb(pm2_cnt) & ~ACPI_ARB_DIS, pm2_cnt);
		}
		else {
			inb(acpi_p_lvl3);
		}
		break;
	}

	// calculate time spent sleeping (fallback to CPU cycles)
	if (timer)
		time = (inl(timer) - time) & ACPI_TMR_MASK;
	else
		time = ACPI_CPU_TO_TMR_TICKS(get_cycles() - time);

	if (time > acpi_p_lvl3_lat)
		sleep_level = 3;
	else if (time > acpi_p_lvl2_lat)
		sleep_level = 2;
	else
		sleep_level = 1;
}

/*
 * Enter system sleep state
 */
static void acpi_enter_sx(int state)
{
	unsigned long slp_typ = acpi_slp_typ[state];
	if (slp_typ != ACPI_SLP_TYP_DISABLED) {
		u16 typa, typb, value;

		// bits 8-15 are SLP_TYPa, bits 0-7 are SLP_TYPb
		typa = (slp_typ >> 8) & 0xff;
		typb = slp_typ & 0xff;

		typa = ((typa << ACPI_SLP_TYP_SHIFT) & ACPI_SLP_TYP_MASK);
		typb = ((typb << ACPI_SLP_TYP_SHIFT) & ACPI_SLP_TYP_MASK);

		// set SLP_TYPa/b and SLP_EN
		if (acpi_facp->pm1a_cnt) {
			value = inw(acpi_facp->pm1a_cnt) & ~ACPI_SLP_TYP_MASK;
			outw(value | typa | ACPI_SLP_EN, acpi_facp->pm1a_cnt);
		}
		if (acpi_facp->pm1b_cnt) {
			value = inw(acpi_facp->pm1b_cnt) & ~ACPI_SLP_TYP_MASK;
			outw(value | typb | ACPI_SLP_EN, acpi_facp->pm1b_cnt);
		}
	}
}

/*
 * Enter soft-off (S5)
 */
static void acpi_power_off_handler(void)
{
	acpi_enter_sx(5);
}

/*
 * Claim ACPI I/O ports
 */
static int acpi_claim_ioports(struct acpi_facp *facp)
{
	// we don't get a guarantee of contiguity for any of the ACPI registers
	if (facp->pm1a_evt)
		request_region(facp->pm1a_evt, facp->pm1_evt_len, "acpi");
	if (facp->pm1b_evt)
		request_region(facp->pm1b_evt, facp->pm1_evt_len, "acpi");
	if (facp->pm1a_cnt)
		request_region(facp->pm1a_cnt, facp->pm1_cnt_len, "acpi");
	if (facp->pm1b_cnt)
		request_region(facp->pm1b_cnt, facp->pm1_cnt_len, "acpi");
	if (facp->pm_tmr)
		request_region(facp->pm_tmr, facp->pm_tm_len, "acpi");
	if (facp->gpe0)
		request_region(facp->gpe0, facp->gpe0_len, "acpi");
	if (facp->gpe1)
		request_region(facp->gpe1, facp->gpe1_len, "acpi");

	return 0;
}

/*
 * Free ACPI I/O ports
 */
static int acpi_release_ioports(struct acpi_facp *facp)
{
	// we don't get a guarantee of contiguity for any of the ACPI registers
	if (facp->pm1a_evt)
		release_region(facp->pm1a_evt, facp->pm1_evt_len);
	if (facp->pm1b_evt)
		release_region(facp->pm1b_evt, facp->pm1_evt_len);
	if (facp->pm1a_cnt)
		release_region(facp->pm1a_cnt, facp->pm1_cnt_len);
	if (facp->pm1b_cnt)
		release_region(facp->pm1b_cnt, facp->pm1_cnt_len);
	if (facp->pm_tmr)
		release_region(facp->pm_tmr, facp->pm_tm_len);
	if (facp->gpe0)
		release_region(facp->gpe0, facp->gpe0_len);
	if (facp->gpe1)
		release_region(facp->gpe1, facp->gpe1_len);

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
	char str[4 * sizeof(u32) + 7];
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
		
		if (pm1_status || gpe_status)
			break;
		
		// wait for an event to arrive
		interruptible_sleep_on(&acpi_event_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	size = sprintf(str, "0x%08x 0x%08x\n", pm1_status, gpe_status);
	copy_to_user(buffer, str, size);
	*len = size;
	file->f_pos += size;

	return 0;
}

/*
 * Initialize and enable ACPI
 */
static int __init acpi_init(void)
{
	int pid;

	if (acpi_find_tables() && acpi_find_piix4()) {
		// no ACPI tables and not PIIX4
		return -ENODEV;
	}

	if (acpi_facp->sci_int
	    && request_irq(acpi_facp->sci_int,
			   acpi_irq,
			   SA_INTERRUPT | SA_SHIRQ,
			   "acpi",
			   acpi_facp)) {
		printk(KERN_ERR "ACPI: SCI (IRQ%d) allocation failed\n",
		       acpi_facp->sci_int);
		acpi_destroy_tables();
		return -ENODEV;
	}

	acpi_claim_ioports(acpi_facp);
	acpi_sysctl = register_sysctl_table(acpi_dir_table, 1);

	pid = kernel_thread(acpi_idle_thread,
			    NULL,
			    CLONE_FS | CLONE_FILES | CLONE_SIGHAND);

	/*
	 * Set up the ACPI idle function. Note that we can't really
	 * do this with multiple CPU's, we'd need a per-CPU ACPI
	 * device..
	 */
#ifdef __SMP__
	if (smp_num_cpus > 1)
		return 0;
#endif

	acpi_power_off = acpi_power_off_handler;
	acpi_idle = acpi_idle_handler;

	return 0;
}

/*
 * Disable and deinitialize ACPI
 */
static void __exit acpi_exit(void)
{
	acpi_idle = NULL;
	acpi_power_off = NULL;

	unregister_sysctl_table(acpi_sysctl);
	acpi_disable(acpi_facp);
	acpi_release_ioports(acpi_facp);

	if (acpi_facp->sci_int)
		free_irq(acpi_facp->sci_int, acpi_facp);

	acpi_destroy_tables();
}

/*
 * Register a device with the ACPI subsystem
 */
struct acpi_dev* acpi_register(acpi_dev_t type,
			       unsigned long adr,
			       acpi_hid_t hid,
			       acpi_transition trans)
{
	struct acpi_dev *dev = kmalloc(sizeof(struct acpi_dev), GFP_KERNEL);
	if (dev) {
		unsigned long flags;

		memset(dev, 0, sizeof(*dev));
		dev->type = type;
		dev->adr = adr;
		dev->hid = hid;
		dev->transition = trans;

		spin_lock_irqsave(&acpi_devs_lock, flags);
		list_add(&dev->entry, &acpi_devs);
		spin_unlock_irqrestore(&acpi_devs_lock, flags);
	}
	return dev;
}

/*
 * Unregister a device with ACPI
 */
void acpi_unregister(struct acpi_dev *dev)
{
	if (dev) {
		unsigned long flags;

		spin_lock_irqsave(&acpi_devs_lock, flags);
		list_del(&dev->entry);
		spin_unlock_irqrestore(&acpi_devs_lock, flags);

		kfree(dev);
	}
}

/*
 * Wake up a device
 */
void acpi_wakeup(struct acpi_dev *dev)
{
	// run _PS0 or tell parent bus to wake device up
}

/*
 * Manage idle devices
 */
static int acpi_idle_thread(void *context)
{
	exit_mm(current);
	exit_files(current);
	strcpy(current->comm, "acpi");
	
	for(;;) {
		interruptible_sleep_on(&acpi_idle_wait);
		if (signal_pending(current))
			break;

		// find all idle devices and set idle timer based on policy
	}

	return 0;
}

__initcall(acpi_init);

/*
 * Module visible symbols
 */
EXPORT_SYMBOL(acpi_idle_wait);
EXPORT_SYMBOL(acpi_register);
EXPORT_SYMBOL(acpi_unregister);
EXPORT_SYMBOL(acpi_wakeup);
