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
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/ide.h>

extern int root_mountflags;

extern char command_line[];
char saved_command_line[256];

unsigned char aux_device_present;	/* XXX */
unsigned char kbd_read_mask;
unsigned char drive_info;

#define DEFAULT_ROOT_DEVICE 0x0801	/* sda1 - slightly silly choice */

extern unsigned long find_available_memory(void);

unsigned long bios32_init(unsigned long memory_start, unsigned long memory_end)
{
	return memory_start;
}

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	extern unsigned long *end_of_DRAM;
	struct device_node *cpu;
	int *fp;

	strcpy(saved_command_line, command_line);
	*cmdline_p = command_line;

	*memory_start_p = find_available_memory();
	*memory_end_p = (unsigned long) end_of_DRAM;

	set_prom_callback();

	*memory_start_p = copy_device_tree(*memory_start_p, *memory_end_p);

	/* Set loops_per_sec to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	cpu = find_type_devices("cpu");
	if (cpu != 0) {
		fp = (int *) get_property(cpu, "clock-frequency", NULL);
		if (fp != 0) {
			switch (_get_PVR() >> 16) {
			case 4:		/* 604 */
				loops_per_sec = *fp;
				break;
			default:	/* 601, 603, etc. */
				loops_per_sec = *fp / 2;
			}
		} else
			loops_per_sec = 50000000;
	}
}

char *bootpath;
char bootdevice[256];
void *boot_host;
int boot_target;
int boot_part;
kdev_t boot_dev;

unsigned long
pmac_find_devices(unsigned long mem_start, unsigned long mem_end)
{
	struct device_node *chosen_np;

	nvram_init();
	via_cuda_init();
	read_rtc_time();
	pmac_find_display();
	bootpath = NULL;
	chosen_np = find_devices("chosen");
	if (chosen_np != NULL)
		bootpath = (char *) get_property(chosen_np, "bootpath", NULL);
	if (bootpath != NULL) {
		/*
		 * There's a bug in the prom.  (Why am I not surprised.)
		 * If you pass a path like scsi/sd@1:0 to canon, it returns
		 * something like /bandit@F2000000/gc@10/53c94@10000/sd@0,0
		 * That is, the scsi target number doesn't get preserved.
		 */
		call_prom("canon", 3, 1, bootpath, bootdevice, sizeof(bootdevice));
	}
	return mem_start;
}

void
note_scsi_host(struct device_node *node, void *host)
{
	int l;
	char *p;

	l = strlen(node->full_name);
	if (strncmp(node->full_name, bootdevice, l) == 0
	    && (bootdevice[l] == '/' || bootdevice[l] == 0)) {
		boot_host = host;
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

void find_scsi_boot()
{
	int dev;

	if (kdev_t_to_nr(ROOT_DEV) != 0)
		return;
	ROOT_DEV = to_kdev_t(DEFAULT_ROOT_DEVICE);
	if (boot_host == NULL)
		return;
	dev = sd_find_target(boot_host, boot_target);
	if (dev == 0)
		return;
	boot_dev = to_kdev_t(dev + boot_part);
}

void ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq)
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
		np->intrs[0] = 13;
	}
	base = (unsigned long) ioremap(np->addrs[0].address, 0x200);
	for (i = 0; i < 8; ++i)
		*p++ = base + i * 0x10;
	*p = base + 0x160;
	*irq = np->intrs[0];
}

int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -EIO;
}

#if 0
extern char builtin_ramdisk_image;
extern long builtin_ramdisk_size;
#endif

void
builtin_ramdisk_init(void)
{
#if 0
	if ((ROOT_DEV == to_kdev_t(DEFAULT_ROOT_DEVICE)) && (builtin_ramdisk_size != 0))
	{
		rd_preloaded_init(&builtin_ramdisk_image, builtin_ramdisk_size);
	} else
#endif
	{  /* Not ramdisk - assume root needs to be mounted read only */
		root_mountflags |= MS_RDONLY;
	}
}

int
get_cpuinfo(char *buffer)
{
	int pvr = _get_PVR();
	char *model;
	struct device_node *cpu;
	int l, *fp;

	l = 0;
	cpu = find_type_devices("cpu");
	if (cpu != 0) {
		fp = (int *) get_property(cpu, "clock-frequency", NULL);
		if (fp != 0)
			l += sprintf(buffer, "%dMHz ", *fp / 1000000);
	}

	switch (pvr>>16) {
	case 1:
		model = "601";
		break;
	case 3:
		model = "603";
		break;
	case 4:
		model = "604";
		break;
	case 6:
		model = "603e";
		break;
	case 7:
		model = "603ev";
		break;
	default:
		model = "unknown";
		break;
	}
	return l + sprintf(buffer+l, "PowerPC %s rev %d.%d\n", model,
			   (pvr & 0xff) >> 8, pvr & 0xff);
}
