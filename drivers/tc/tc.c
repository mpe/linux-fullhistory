/*
 * tc-init: We assume the TURBOchannel to be up and running so
 * just probe for Modules and fill in the global data structure
 * tc_bus.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) Harald Koerfgen, 1998
 * Copyright (c) 2001, 2003  Maciej W. Rozycki
 */
#include <linux/string.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/addrspace.h>
#include <asm/errno.h>
#include <asm/dec/machtype.h>
#include <asm/dec/prom.h>
#include <asm/dec/tcinfo.h>
#include <asm/dec/tcmodule.h>
#include <asm/dec/interrupts.h>
#include <asm/paccess.h>
#include <asm/ptrace.h>

#define TC_DEBUG

MODULE_LICENSE("GPL");
slot_info tc_bus[MAX_SLOT];
static int num_tcslots;
static tcinfo *info;

unsigned long system_base;

/*
 * Interface to the world. Read comment in include/asm-mips/tc.h.
 */

int search_tc_card(const char *name)
{
	int slot;
	slot_info *sip;

	for (slot = 0; slot < num_tcslots; slot++) {
		sip = &tc_bus[slot];
		if ((sip->flags & FREE) &&
		    (strncmp(sip->name, name, strlen(name)) == 0)) {
			return slot;
		}
	}

	return -ENODEV;
}

void claim_tc_card(int slot)
{
	if (tc_bus[slot].flags & IN_USE) {
		printk("claim_tc_card: attempting to claim a card already in use\n");
		return;
	}
	tc_bus[slot].flags &= ~FREE;
	tc_bus[slot].flags |= IN_USE;
}

void release_tc_card(int slot)
{
	if (tc_bus[slot].flags & FREE) {
		printk("release_tc_card: "
		       "attempting to release a card already free\n");
		return;
	}
	tc_bus[slot].flags &= ~IN_USE;
	tc_bus[slot].flags |= FREE;
}

unsigned long get_tc_base_addr(int slot)
{
	return tc_bus[slot].base_addr;
}

unsigned long get_tc_irq_nr(int slot)
{
	return tc_bus[slot].interrupt;
}

unsigned long get_tc_speed(void)
{
	return 100000 * (10000 / (unsigned long)info->clk_period);
}

/*
 * Probing for TURBOchannel modules
 */
static void __init tc_probe(unsigned long startaddr, unsigned long size,
			    int slots)
{
	int i, slot, err;
	long offset;
	unsigned char pattern[4];
	unsigned char *module;

	for (slot = 0; slot < slots; slot++) {
		module = (char *)(startaddr + slot * size);

		offset = OLDCARD;

		err = 0;
		err |= get_dbe(pattern[0], module + OLDCARD + TC_PATTERN0);
		err |= get_dbe(pattern[1], module + OLDCARD + TC_PATTERN1);
		err |= get_dbe(pattern[2], module + OLDCARD + TC_PATTERN2);
		err |= get_dbe(pattern[3], module + OLDCARD + TC_PATTERN3);
		if (err)
			continue;

		if (pattern[0] != 0x55 || pattern[1] != 0x00 ||
		    pattern[2] != 0xaa || pattern[3] != 0xff) {
			offset = NEWCARD;

			err = 0;
			err |= get_dbe(pattern[0], module + TC_PATTERN0);
			err |= get_dbe(pattern[1], module + TC_PATTERN1);
			err |= get_dbe(pattern[2], module + TC_PATTERN2);
			err |= get_dbe(pattern[3], module + TC_PATTERN3);
			if (err)
				continue;
		}

		if (pattern[0] != 0x55 || pattern[1] != 0x00 ||
		    pattern[2] != 0xaa || pattern[3] != 0xff)
			continue;

		tc_bus[slot].base_addr = (unsigned long)module;
		for(i = 0; i < 8; i++) {
			tc_bus[slot].firmware[i] =
				module[TC_FIRM_VER + offset + 4 * i];
			tc_bus[slot].vendor[i] =
				module[TC_VENDOR + offset + 4 * i];
			tc_bus[slot].name[i] =
				module[TC_MODULE + offset + 4 * i];
		}
		tc_bus[slot].firmware[8] = 0;
		tc_bus[slot].vendor[8] = 0;
		tc_bus[slot].name[8] = 0;
		/*
		 * Looks unneccesary, but we may change
		 * TC? in the future
		 */
		switch (slot) {
		case 0:
			tc_bus[slot].interrupt = dec_interrupt[DEC_IRQ_TC0];
			break;
		case 1:
			tc_bus[slot].interrupt = dec_interrupt[DEC_IRQ_TC1];
			break;
		case 2:
			tc_bus[slot].interrupt = dec_interrupt[DEC_IRQ_TC2];
			break;
		/*
		 * Yuck! DS5000/200 onboard devices
		 */
		case 5:
			tc_bus[slot].interrupt = dec_interrupt[DEC_IRQ_TC5];
			break;
		case 6:
			tc_bus[slot].interrupt = dec_interrupt[DEC_IRQ_TC6];
			break;
		default:
			tc_bus[slot].interrupt = -1;
			break;
		}
	}
}

/*
 * the main entry
 */
void __init tc_init(void)
{
	int tc_clock;
	int i;
	unsigned long slot0addr;
	unsigned long slot_size;

	if (!TURBOCHANNEL)
		return;

	for (i = 0; i < MAX_SLOT; i++) {
		tc_bus[i].base_addr = 0;
		tc_bus[i].name[0] = 0;
		tc_bus[i].vendor[0] = 0;
		tc_bus[i].firmware[0] = 0;
		tc_bus[i].interrupt = -1;
		tc_bus[i].flags = FREE;
	}

	info = (tcinfo *) rex_gettcinfo();
	slot0addr = (unsigned long)KSEG1ADDR(rex_slot_address(0));

	switch (mips_machtype) {
	case MACH_DS5000_200:
		num_tcslots = 7;
		break;
	case MACH_DS5000_1XX:
	case MACH_DS5000_2X0:
	case MACH_DS5900:
		num_tcslots = 3;
		break;
	case MACH_DS5000_XX:
	default:
		num_tcslots = 2;
		break;
	}

	tc_clock = 10000 / info->clk_period;

	if (TURBOCHANNEL && info->slot_size && slot0addr) {
		printk("TURBOchannel rev. %1d at %2d.%1d MHz ", info->revision,
			tc_clock / 10, tc_clock % 10);
		printk("(with%s parity)\n", info->parity ? "" : "out");

		slot_size = info->slot_size << 20;

		tc_probe(slot0addr, slot_size, num_tcslots);

  		/*
  		 * All TURBOchannel DECstations have the onboard devices
 		 * where the (num_tcslots + 0 or 1 on DS5k/xx) Option Module
 		 * would be.
 		 */
 		if(mips_machtype == MACH_DS5000_XX)
 			i = 1;
		else
 			i = 0;

 	        system_base = slot0addr + slot_size * (num_tcslots + i);

#ifdef TC_DEBUG
		for (i = 0; i < num_tcslots; i++)
			if (tc_bus[i].base_addr) {
				printk("    slot %d: ", i);
				printk("%s %s %s\n", tc_bus[i].vendor,
					tc_bus[i].name, tc_bus[i].firmware);
			}
#endif
		ioport_resource.end = KSEG2 - 1;
	}
}

subsys_initcall(tc_init);

EXPORT_SYMBOL(search_tc_card);
EXPORT_SYMBOL(claim_tc_card);
EXPORT_SYMBOL(release_tc_card);
EXPORT_SYMBOL(get_tc_base_addr);
EXPORT_SYMBOL(get_tc_irq_nr);
EXPORT_SYMBOL(get_tc_speed);
EXPORT_SYMBOL(system_base);
