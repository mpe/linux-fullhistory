/*
 *  tables.c - ACPI tables, chipset, and errata handling
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
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include "acpi.h"
#include "driver.h"

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("tables")

struct acpi_facp acpi_facp;

#define ACPI_DUMMY_CHECKSUM 9
#define ACPI_DUMMY_PBLK 51

static u8 acpi_dummy_dsdt[] =
{
	0x44, 0x53, 0x44, 0x54,                         // "DSDT"
	0x38, 0x00, 0x00, 0x00,                         // length
	0x01,                                           // revision
	0x00,                                           // checksum
	0x4c, 0x49, 0x4e, 0x55, 0x58, 0x00,             // "LINUX"
	0x44, 0x55, 0x4d, 0x4d, 0x59, 0x00, 0x00, 0x00, // "DUMMY"
	0x01, 0x00, 0x00, 0x00,                         // OEM rev
	0x4c, 0x4e, 0x55, 0x58,                         // "LNUX"
	0x01, 0x00, 0x00, 0x00,                         // creator rev
	0x10,                                           // Scope
	0x13,                                           //   PkgLength
	0x5c, 0x5f, 0x50, 0x52, 0x5f,                   //   \_PR_
	0x5b, 0x83,                                     //   Processor
	0x0b,                                           //     PkgLength
	0x43, 0x50, 0x55, 0x30,                         //     CPU0
	0x00,                                           //     ID
	0x00, 0x00, 0x00, 0x00,                         //     PBLK
	0x06                                            //     PBLK size
};

/*
 * Calculate and set ACPI table checksum
 */
static void
acpi_set_checksum(u8 *table, int size)
{
	int i, sum = 0;
	for (i = 0; i < size; i++)
		sum += (int) table[i];
	sum = (0x100 - ((sum - table[ACPI_DUMMY_CHECKSUM]) & 0xff));
	table[ACPI_DUMMY_CHECKSUM] = sum;
}

/*
 * Init PIIX4 device, create a fake FACP
 */
static int
acpi_init_piix4(struct pci_dev *dev)
{
	u32 base, pblk;
	u16 cmd;
	u8 pmregmisc;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_IO))
		return -ENODEV;
	
	pci_read_config_byte(dev, ACPI_PIIX4_PMREGMISC, &pmregmisc);
	if (!(pmregmisc & ACPI_PIIX4_PMIOSE))
		return -ENODEV;
	
	base = pci_resource_start (dev, PCI_BRIDGE_RESOURCES);
	if (!base)
		return -ENODEV;

	printk(KERN_INFO "ACPI: found \"%s\" at 0x%04x\n", dev->name, base);

	memset(&acpi_facp, 0, sizeof(acpi_facp));
	acpi_facp.hdr.signature = ACPI_FACP_SIG;
	acpi_facp.hdr.length = sizeof(acpi_facp);
	acpi_facp.int_model = ACPI_PIIX4_INT_MODEL;
	acpi_facp.sci_int = ACPI_PIIX4_SCI_INT;
	acpi_facp.smi_cmd = ACPI_PIIX4_SMI_CMD;
	acpi_facp.acpi_enable = ACPI_PIIX4_ACPI_ENABLE;
	acpi_facp.acpi_disable = ACPI_PIIX4_ACPI_DISABLE;
	acpi_facp.s4bios_req = ACPI_PIIX4_S4BIOS_REQ;
	acpi_facp.pm1a_evt = base + ACPI_PIIX4_PM1_EVT;
	acpi_facp.pm1a_cnt = base + ACPI_PIIX4_PM1_CNT;
	acpi_facp.pm2_cnt = ACPI_PIIX4_PM2_CNT;
	acpi_facp.pm_tmr = base + ACPI_PIIX4_PM_TMR;
	acpi_facp.gpe0 = base + ACPI_PIIX4_GPE0;
	acpi_facp.pm1_evt_len = ACPI_PIIX4_PM1_EVT_LEN;
	acpi_facp.pm1_cnt_len = ACPI_PIIX4_PM1_CNT_LEN;
	acpi_facp.pm2_cnt_len = ACPI_PIIX4_PM2_CNT_LEN;
	acpi_facp.pm_tm_len = ACPI_PIIX4_PM_TM_LEN;
	acpi_facp.gpe0_len = ACPI_PIIX4_GPE0_LEN;
	acpi_facp.p_lvl2_lat = (__u16) ACPI_INFINITE_LAT;
	acpi_facp.p_lvl3_lat = (__u16) ACPI_INFINITE_LAT;

	acpi_set_checksum((u8*) &acpi_facp, sizeof(acpi_facp));
	acpi_load_table((ACPI_TABLE_HEADER*) &acpi_facp);

	pblk = base + ACPI_PIIX4_P_BLK;
	memcpy(acpi_dummy_dsdt + ACPI_DUMMY_PBLK, &pblk, sizeof(pblk));
	acpi_set_checksum(acpi_dummy_dsdt, sizeof(acpi_dummy_dsdt));
	acpi_load_table((ACPI_TABLE_HEADER*) acpi_dummy_dsdt);

	return 0;
}

/*
 * Init VIA ACPI device and create a fake FACP
 */
static int
acpi_init_via(struct pci_dev *dev)
{
	u32 base, pblk;
	u8 tmp, irq;

	pci_read_config_byte(dev, 0x41, &tmp);
	if (!(tmp & 0x80))
		return -ENODEV;

	base = pci_resource_start(dev, PCI_BRIDGE_RESOURCES);
	if (!base) {
		base = pci_resource_start(dev, PCI_BASE_ADDRESS_4);
		if (!base)
			return -ENODEV;
	}

	pci_read_config_byte(dev, 0x42, &irq);

	printk(KERN_INFO "ACPI: found \"%s\" at 0x%04x\n", dev->name, base);

	memset(&acpi_facp, 0, sizeof(acpi_facp));
	acpi_facp.hdr.signature = ACPI_FACP_SIG;
	acpi_facp.hdr.length = sizeof(acpi_facp);
	acpi_facp.int_model = ACPI_VIA_INT_MODEL;
	acpi_facp.sci_int = irq;
	acpi_facp.smi_cmd = base + ACPI_VIA_SMI_CMD;
	acpi_facp.acpi_enable = ACPI_VIA_ACPI_ENABLE;
	acpi_facp.acpi_disable = ACPI_VIA_ACPI_DISABLE;
	acpi_facp.pm1a_evt = base + ACPI_VIA_PM1_EVT;
	acpi_facp.pm1a_cnt = base + ACPI_VIA_PM1_CNT;
	acpi_facp.pm_tmr = base + ACPI_VIA_PM_TMR;
	acpi_facp.gpe0 = base + ACPI_VIA_GPE0;

	acpi_facp.pm1_evt_len = ACPI_VIA_PM1_EVT_LEN;
	acpi_facp.pm1_cnt_len = ACPI_VIA_PM1_CNT_LEN;
	acpi_facp.pm_tm_len = ACPI_VIA_PM_TM_LEN;
	acpi_facp.gpe0_len = ACPI_VIA_GPE0_LEN;
	acpi_facp.p_lvl2_lat = (__u16) ACPI_INFINITE_LAT;
	acpi_facp.p_lvl3_lat = (__u16) ACPI_INFINITE_LAT;

	acpi_facp.duty_offset = ACPI_VIA_DUTY_OFFSET;
	acpi_facp.duty_width = ACPI_VIA_DUTY_WIDTH;

	acpi_facp.day_alarm = ACPI_VIA_DAY_ALARM;
	acpi_facp.mon_alarm = ACPI_VIA_MON_ALARM;
	acpi_facp.century = ACPI_VIA_CENTURY;

	acpi_set_checksum((u8*) &acpi_facp, sizeof(acpi_facp));
	acpi_load_table((ACPI_TABLE_HEADER*) &acpi_facp);

	pblk = base + ACPI_VIA_P_BLK;
	memcpy(acpi_dummy_dsdt + ACPI_DUMMY_PBLK, &pblk, sizeof(pblk));
	acpi_set_checksum(acpi_dummy_dsdt, sizeof(acpi_dummy_dsdt));
	acpi_load_table((ACPI_TABLE_HEADER*) acpi_dummy_dsdt);

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

static struct pci_device_id acpi_pci_tbl[] =
{
        {0x8086, 0x7113, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_INTEL_PIIX4},
        {0x1106, 0x3040, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_VIA_586},
        {0x1106, 0x3057, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_VIA_686A},
        {0,} /* terminate list */
};

static int
acpi_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
        return acpi_chip_info[id->driver_data].chip_init(dev);
}

static struct pci_driver acpi_driver =
{
        name:           "acpi",
        id_table:       acpi_pci_tbl,
        probe:          acpi_probe,
};
static int acpi_driver_registered = 0;

/*
 * Locate a known ACPI chipset
 */
static int
acpi_find_chipset(void)
{
        if (pci_register_driver(&acpi_driver) < 1)
                return -ENODEV;
        acpi_driver_registered = 1;
        return 0;
}

/*
 * Fetch the FACP information
 */
static int
acpi_fetch_facp(void)
{
	ACPI_BUFFER buffer;

	memset(&acpi_facp, 0, sizeof(acpi_facp));
	buffer.pointer = &acpi_facp;
	buffer.length = sizeof(acpi_facp);
	if (!ACPI_SUCCESS(acpi_get_table(ACPI_TABLE_FACP, 1, &buffer))) {
		printk(KERN_ERR "ACPI: missing FACP\n");
		return -ENODEV;
	}

	if (acpi_facp.p_lvl2_lat
	    && acpi_facp.p_lvl2_lat <= ACPI_MAX_P_LVL2_LAT) {
		acpi_c2_exit_latency
			= ACPI_uS_TO_TMR_TICKS(acpi_facp.p_lvl2_lat);
		acpi_c2_enter_latency
			= ACPI_uS_TO_TMR_TICKS(ACPI_TMR_HZ / 1000);
	}
	if (acpi_facp.p_lvl3_lat
	    && acpi_facp.p_lvl3_lat <= ACPI_MAX_P_LVL3_LAT) {
		acpi_c3_exit_latency
			= ACPI_uS_TO_TMR_TICKS(acpi_facp.p_lvl3_lat);
		acpi_c3_enter_latency
			= ACPI_uS_TO_TMR_TICKS(acpi_facp.p_lvl3_lat * 5);
	}

	return 0;
}

/*
 * Find and load ACPI tables
 */
int
acpi_load_tables(void)
{
	if (ACPI_SUCCESS(acpi_load_firmware_tables()))
	{
		printk(KERN_INFO "ACPI: support found\n");
	}
	else if (acpi_find_chipset()) {
		acpi_terminate();
		return -1;
	}

	if (acpi_fetch_facp()) {
		acpi_terminate();
		return -1;
	}

	if (!ACPI_SUCCESS(acpi_load_namespace())) {
		printk(KERN_ERR "ACPI: namespace load failed\n");
		acpi_terminate();
		return -1;
	}

	return 0;
}
