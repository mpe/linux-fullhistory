/*
 *  linux/arch/x86_64/kernel/head64.c -- prepare to run common code
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *
 *  $Id: head64.c,v 1.22 2001/07/06 14:28:20 ak Exp $
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/processor.h>

/* Don't add a printk in there. printk relies on the PDA which is not initialized 
   yet. */
static void __init clear_bss(void)
{
	extern char __bss_start[], __bss_end[];
	memset(__bss_start, 0,
	       (unsigned long) __bss_end - (unsigned long) __bss_start);
}

extern char x86_boot_params[2048];

#define NEW_CL_POINTER		0x228	/* Relative to real mode data */
#define OLD_CL_MAGIC_ADDR	0x90020
#define OLD_CL_MAGIC            0xA33F
#define OLD_CL_BASE_ADDR        0x90000
#define OLD_CL_OFFSET           0x90022

extern char saved_command_line[];

static void __init copy_bootdata(char *real_mode_data)
{
	int new_data;
	char * command_line;

	memcpy(x86_boot_params, real_mode_data, 2048); 
	new_data = *(int *) (x86_boot_params + NEW_CL_POINTER);
	if (!new_data) {
		if (OLD_CL_MAGIC != * (u16 *) OLD_CL_MAGIC_ADDR) {
			printk("so old bootloader that it does not support commandline?!\n");
			return;
		}
		new_data = OLD_CL_BASE_ADDR + * (u16 *) OLD_CL_OFFSET;
		printk("old bootloader convention, maybe loadlin?\n");
	}
	command_line = (char *) ((u64)(new_data));
	memcpy(saved_command_line, command_line, 2048);
	printk("Bootdata ok (command line is %s)\n", saved_command_line);	
}

static void __init setup_boot_cpu_data(void)
{
	int dummy, eax;

	/* get vendor info */
	cpuid(0, &boot_cpu_data.cpuid_level,
	      (int *)&boot_cpu_data.x86_vendor_id[0],
	      (int *)&boot_cpu_data.x86_vendor_id[8],
	      (int *)&boot_cpu_data.x86_vendor_id[4]);

	/* get cpu type */
	cpuid(1, &eax, &dummy, &dummy, (int *) &boot_cpu_data.x86_capability);
	boot_cpu_data.x86 = (eax >> 8) & 0xf;
	boot_cpu_data.x86_model = (eax >> 4) & 0xf;
	boot_cpu_data.x86_mask = eax & 0xf;
}

extern void start_kernel(void), pda_init(int); 

void __init x86_64_start_kernel(char * real_mode_data)
{
	clear_bss();
	pda_init(0);

	copy_bootdata(real_mode_data);
	setup_boot_cpu_data();


	start_kernel();
}
