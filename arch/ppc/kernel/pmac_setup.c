/*
 *  linux/arch/ppc/kernel/setup.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  Derived from "arch/alpha/kernel/setup.c"
 *    Copyright (C) 1995 Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/major.h>
#ifdef CONFIG_ABSTRACT_CONSOLE
#include <linux/console.h>
#endif
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/ide.h>
#include <asm/pci-bridge.h>
#include <asm/adb.h>
#include "time.h"

/*
 * A magic address and value to put into it on machines with the
 * "ohare" I/O controller.  This makes the IDE CD work on Starmaxes.
 * Contributed by Harry Eaton.
 */
#define OMAGICPLACE	((volatile unsigned *) 0xf3000038)
#define OMAGICCONT	0xbeff7a

extern int root_mountflags;

unsigned char drive_info;

#define DEFAULT_ROOT_DEVICE 0x0801	/* sda1 - slightly silly choice */

static void gc_init(const char *, int);

void
pmac_setup_arch(unsigned long *memory_start_p, unsigned long *memory_end_p)
{
	struct device_node *cpu;
	int *fp;

	/* Set loops_per_sec to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	cpu = find_type_devices("cpu");
	if (cpu != 0) {
		fp = (int *) get_property(cpu, "clock-frequency", NULL);
		if (fp != 0) {
			switch (_get_PVR() >> 16) {
			case 4:		/* 604 */
			case 9:		/* 604e */
			case 10:	/* mach V (604ev5) */
			case 20:	/* 620 */
				loops_per_sec = *fp;
				break;
			default:	/* 601, 603, etc. */
				loops_per_sec = *fp / 2;
			}
		} else
			loops_per_sec = 50000000;
	}

	*memory_start_p = pmac_find_bridges(*memory_start_p, *memory_end_p);
	gc_init("gc", 0);
	gc_init("ohare", 1);

#ifdef CONFIG_ABSTRACT_CONSOLE
	/* Frame buffer device based console */
	conswitchp = &fb_con;
#endif
}

static void gc_init(const char *name, int isohare)
{
	struct device_node *np;

	for (np = find_devices(name); np != NULL; np = np->next) {
		if (np->n_addrs > 0)
			ioremap(np->addrs[0].address, np->addrs[0].size);
		if (isohare) {
			printk(KERN_INFO "Twiddling the magic ohare bits\n");
			out_le32(OMAGICPLACE, OMAGICCONT);
		}
	}
}

extern char *bootpath;
extern char *bootdevice;
void *boot_host;
int boot_target;
int boot_part;
kdev_t boot_dev;

unsigned long
powermac_init(unsigned long mem_start, unsigned long mem_end)
{
	pmac_nvram_init();
	adb_init();
	if (_machine == _MACH_Pmac) {
		pmac_read_rtc_time();
	}
#ifdef CONFIG_PMAC_CONSOLE
	pmac_find_display();
#endif

	return mem_start;
}

void
note_scsi_host(struct device_node *node, void *host)
{
	int l;
	char *p;

	l = strlen(node->full_name);
	if (bootpath != NULL && bootdevice != NULL
	    && strncmp(node->full_name, bootdevice, l) == 0
	    && (bootdevice[l] == '/' || bootdevice[l] == 0)) {
		boot_host = host;
		/*
		 * There's a bug in OF 1.0.5.  (Why am I not surprised.)
		 * If you pass a path like scsi/sd@1:0 to canon, it returns
		 * something like /bandit@F2000000/gc@10/53c94@10000/sd@0,0
		 * That is, the scsi target number doesn't get preserved.
		 * So we pick the target number out of bootpath and use that.
		 */
		p = strstr(bootpath, "/sd@");
		if (p != NULL) {
			p += 4;
			boot_target = simple_strtoul(p, NULL, 10);
			p = strchr(p, ':');
			if (p != NULL)
				boot_part = simple_strtoul(p + 1, NULL, 10);
		}
	}
}

#ifdef CONFIG_SCSI
/* Find the device number for the disk (if any) at target tgt
   on host adaptor host.
   XXX this really really should be in drivers/scsi/sd.c. */
#include <linux/blkdev.h>
#include "../../../drivers/scsi/scsi.h"
#include "../../../drivers/scsi/sd.h"
#include "../../../drivers/scsi/hosts.h"

int sd_find_target(void *host, int tgt)
{
    Scsi_Disk *dp;
    int i;

    for (dp = rscsi_disks, i = 0; i < sd_template.dev_max; ++i, ++dp)
        if (dp->device != NULL && dp->device->host == host
            && dp->device->id == tgt)
            return MKDEV(SCSI_DISK_MAJOR, i << 4);
    return 0;
}
#endif

void find_boot_device(void)
{
	int dev;

	if (kdev_t_to_nr(ROOT_DEV) != 0)
		return;
	ROOT_DEV = to_kdev_t(DEFAULT_ROOT_DEVICE);
	if (boot_host == NULL)
		return;
#ifdef CONFIG_SCSI
	dev = sd_find_target(boot_host, boot_target);
	if (dev == 0)
		return;
	boot_dev = to_kdev_t(dev + boot_part);
#endif
	/* XXX should cope with booting from IDE also */
}

void note_bootable_part(kdev_t dev, int part)
{
	static int found_boot = 0;

	if (!found_boot) {
		find_boot_device();
		found_boot = 1;
	}
	if (dev == boot_dev) {
		ROOT_DEV = MKDEV(MAJOR(dev), MINOR(dev) + part);
		boot_dev = NODEV;
		printk(" (root)");
	}
}

void pmac_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq)
{
	struct device_node *np;
	int i;
	static struct device_node *atas;
	static int atas_valid;

	*p = 0;
	*irq = 0;
	if (!atas_valid) {
		atas = find_devices("ATA");
		atas_valid = 1;
	}
	for (i = (int)base, np = atas; i > 0 && np != NULL; --i, np = np->next)
		;
	if (np == NULL)
		return;
	if (np->n_addrs == 0) {
		printk("ide: no addresses for device %s\n", np->full_name);
		return;
	}
	if (np->n_intrs == 0) {
		printk("ide: no intrs for device %s, using 13\n",
		       np->full_name);
		*irq = 13;
	} else {
		*irq = np->intrs[0];
	}
	base = (unsigned long) ioremap(np->addrs[0].address, 0x200);
	for (i = 0; i < 8; ++i)
		*p++ = base + i * 0x10;
	*p = base + 0x160;
}

int
pmac_get_cpuinfo(char *buffer)
{
	int len;
	/* should find motherboard type here as well */
	len = sprintf(buffer,"machine\t\t: PowerMac\n");
	return len;
}
