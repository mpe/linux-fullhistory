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
#include <linux/init.h>
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
#include <linux/blk.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/adb.h>
#include <asm/cuda.h>
#include <asm/pmu.h>
#include <asm/ohare.h>
#include <asm/mediabay.h>
#include <asm/feature.h>
#include "time.h"

unsigned char drive_info;

extern char saved_command_line[];

#define DEFAULT_ROOT_DEVICE 0x0801	/* sda1 - slightly silly choice */

extern void zs_kgdb_hook(int tty_num);
static void ohare_init(void);

__pmac
int
pmac_get_cpuinfo(char *buffer)
{
	int len;
	struct device_node *np;
	char *pp;
	int plen;

	/* find motherboard type */
	len = sprintf(buffer, "machine\t\t: ");
	np = find_devices("device-tree");
	if (np != NULL) {
		pp = (char *) get_property(np, "model", NULL);
		if (pp != NULL)
			len += sprintf(buffer+len, "%s\n", pp);
		else
			len += sprintf(buffer+len, "PowerMac\n");
		pp = (char *) get_property(np, "compatible", &plen);
		if (pp != NULL) {
			len += sprintf(buffer+len, "motherboard\t:");
			while (plen > 0) {
				int l = strlen(pp) + 1;
				len += sprintf(buffer+len, " %s", pp);
				plen -= l;
				pp += l;
			}
			buffer[len++] = '\n';
		}
	} else
		len += sprintf(buffer+len, "PowerMac\n");

	/* find l2 cache info */
	np = find_devices("l2-cache");
	if (np == 0)
		np = find_type_devices("cache");
	if (np != 0) {
		unsigned int *ic = (unsigned int *)
			get_property(np, "i-cache-size", NULL);
		unsigned int *dc = (unsigned int *)
			get_property(np, "d-cache-size", NULL);
		len += sprintf(buffer+len, "L2 cache\t:");
		if (get_property(np, "cache-unified", NULL) != 0 && dc) {
			len += sprintf(buffer+len, " %dK unified", *dc / 1024);
		} else {
			if (ic)
				len += sprintf(buffer+len, " %dK instruction",
					       *ic / 1024);
			if (dc)
				len += sprintf(buffer+len, "%s %dK data",
					       (ic? " +": ""), *dc / 1024);
		}
		pp = get_property(np, "ram-type", NULL);
		if (pp)
			len += sprintf(buffer+len, " %s", pp);
		buffer[len++] = '\n';
	}

	/* find ram info */
	np = find_devices("memory");
	if (np != 0) {
		struct reg_property *reg = (struct reg_property *)
			get_property(np, "reg", NULL);
		if (reg != 0) {
			len += sprintf(buffer+len, "memory\t\t: %dMB\n",
				       reg->size >> 20);
		}
	}

	return len;
}

#ifdef CONFIG_SCSI
/* Find the device number for the disk (if any) at target tgt
   on host adaptor host.
   XXX this really really should be in drivers/scsi/sd.c. */
#include <linux/blkdev.h>
#include "../../../drivers/scsi/scsi.h"
#include "../../../drivers/scsi/sd.h"
#include "../../../drivers/scsi/hosts.h"

#define SD_MAJOR(i)		(!(i) ? SCSI_DISK0_MAJOR : SCSI_DISK1_MAJOR-1+(i))
#define SD_MAJOR_NUMBER(i)	SD_MAJOR((i) >> 8)
#define SD_MINOR_NUMBER(i)	((i) & 255)
#define MKDEV_SD_PARTITION(i)	MKDEV(SD_MAJOR_NUMBER(i), SD_MINOR_NUMBER(i))
#define MKDEV_SD(index)		MKDEV_SD_PARTITION((index) << 4)

__init
kdev_t sd_find_target(void *host, int tgt)
{
    Scsi_Disk *dp;
    int i;

    for (dp = rscsi_disks, i = 0; i < sd_template.dev_max; ++i, ++dp)
        if (dp->device != NULL && dp->device->host == host
            && dp->device->id == tgt)
            return MKDEV_SD(i);
    return 0;
}
#endif

/*
 * Dummy mksound function that does nothing.
 * The real one is in the dmasound driver.
 */
__pmac
static void
pmac_mksound(unsigned int hz, unsigned int ticks)
{
}

static volatile u32 *sysctrl_regs;

__initfunc(void
pmac_setup_arch(unsigned long *memory_start_p, unsigned long *memory_end_p))
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

	/* this area has the CPU identification register
	   and some registers used by smp boards */
	sysctrl_regs = (volatile u32 *) ioremap(0xf8000000, 0x1000);
	__ioremap(0xffc00000, 0x400000, pgprot_val(PAGE_READONLY));
	ohare_init();

	*memory_start_p = pmac_find_bridges(*memory_start_p, *memory_end_p);

	feature_init();

#ifdef CONFIG_KGDB
	zs_kgdb_hook(0);
#endif

	find_via_cuda();
	find_via_pmu();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	kd_mksound = pmac_mksound;

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
	else
#endif
		ROOT_DEV = to_kdev_t(DEFAULT_ROOT_DEVICE);
}

__initfunc(static void ohare_init(void))
{
	/*
	 * Turn on the L2 cache.
	 * We assume that we have a PSX memory controller iff
	 * we have an ohare I/O controller.
	 */
	if (find_devices("ohare") != NULL) {
		if (((sysctrl_regs[2] >> 24) & 0xf) >= 3) {
			if (sysctrl_regs[4] & 0x10)
				sysctrl_regs[4] |= 0x04000020;
			else
				sysctrl_regs[4] |= 0x04000000;
			printk(KERN_INFO "Level 2 cache enabled\n");
		}
	}
}

extern char *bootpath;
extern char *bootdevice;
void *boot_host;
int boot_target;
int boot_part;
kdev_t boot_dev;

void __init powermac_init(void)
{
  	if ( (_machine != _MACH_chrp) && (_machine != _MACH_Pmac) )
		return;
	adb_init();
	pmac_nvram_init();
	if (_machine == _MACH_Pmac) {
		media_bay_init();
	}
}

#ifdef CONFIG_SCSI
__initfunc(void
note_scsi_host(struct device_node *node, void *host))
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
#endif

#ifdef CONFIG_BLK_DEV_IDE_PMAC
extern int pmac_ide_count;
extern struct device_node *pmac_ide_node[];
static int ide_majors[] = { 3, 22, 33, 34, 56, 57 };

__initfunc(kdev_t find_ide_boot(void))
{
	char *p;
	int i, n;

	if (bootdevice == NULL)
		return 0;
	p = strrchr(bootdevice, '/');
	if (p == NULL)
		return 0;
	n = p - bootdevice;

	/*
	 * Look through the list of IDE interfaces for this one.
	 */
	for (i = 0; i < pmac_ide_count; ++i) {
		char *name = pmac_ide_node[i]->full_name;
		if (memcmp(name, bootdevice, n) == 0 && name[n] == 0) {
			/* XXX should cope with the 2nd drive as well... */
			return MKDEV(ide_majors[i], 0);
		}
	}

	return 0;
}
#endif /* CONFIG_BLK_DEV_IDE_PMAC */

__initfunc(void find_boot_device(void))
{
#ifdef CONFIG_SCSI
	if (boot_host != NULL) {
		boot_dev = sd_find_target(boot_host, boot_target);
		if (boot_dev != 0)
			return;
	}
#endif
#ifdef CONFIG_BLK_DEV_IDE_PMAC
	boot_dev = find_ide_boot();
#endif
}

/* can't be initfunc - can be called whenever a disk is first accessed */
__pmac
void note_bootable_part(kdev_t dev, int part)
{
	static int found_boot = 0;
	char *p;

	/* Do nothing if the root has been set already. */
	if (ROOT_DEV != to_kdev_t(DEFAULT_ROOT_DEVICE))
		return;
	p = strstr(saved_command_line, "root=");
	if (p != NULL && (p == saved_command_line || p[-1] == ' '))
		return;

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

