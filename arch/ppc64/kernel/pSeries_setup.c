/*
 *  linux/arch/ppc/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified by PPC64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * bootup setup stuff..
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/adb.h>
#include <linux/module.h>
#include <linux/delay.h>

#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/naca.h>
#include <asm/time.h>
#include <asm/nvram.h>

#include "i8259.h"
#include "open_pic.h"
#include <asm/xics.h>
#include <asm/ppcdebug.h>
#include <asm/cputable.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

extern void pSeries_init_openpic(void);

extern void find_and_init_phbs(void);
extern void pSeries_final_fixup(void);

extern void pSeries_get_boot_time(struct rtc_time *rtc_time);
extern void pSeries_get_rtc_time(struct rtc_time *rtc_time);
extern int  pSeries_set_rtc_time(struct rtc_time *rtc_time);
extern void find_udbg_vterm(void);
extern void SystemReset_FWNMI(void), MachineCheck_FWNMI(void);	/* from head.S */
int fwnmi_active;  /* TRUE if an FWNMI handler is present */

unsigned long  virtPython0Facilities = 0;  // python0 facility area (memory mapped io) (64-bit format) VIRTUAL address.

extern unsigned long loops_per_jiffy;

extern unsigned long ppc_proc_freq;
extern unsigned long ppc_tb_freq;

void pSeries_get_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);
	of_node_put(root);
}

/* Initialize firmware assisted non-maskable interrupts if
 * the firmware supports this feature.
 *
 */
static void __init fwnmi_init(void)
{
	int ret;
	int ibm_nmi_register = rtas_token("ibm,nmi-register");
	if (ibm_nmi_register == RTAS_UNKNOWN_SERVICE)
		return;
	ret = rtas_call(ibm_nmi_register, 2, 1, NULL,
			__pa((unsigned long)SystemReset_FWNMI),
			__pa((unsigned long)MachineCheck_FWNMI));
	if (ret == 0)
		fwnmi_active = 1;
}

static void __init pSeries_setup_arch(void)
{
	struct device_node *root;
	unsigned int *opprop;

	/* Fixup ppc_md depending on the type of interrupt controller */
	if (naca->interrupt_controller == IC_OPEN_PIC) {
		ppc_md.init_IRQ       = pSeries_init_openpic; 
		ppc_md.get_irq        = openpic_get_irq;
	} else {
		ppc_md.init_IRQ       = xics_init_IRQ;
		ppc_md.get_irq        = xics_get_irq;
	}

#ifdef CONFIG_SMP
	smp_init_pSeries();
#endif
	/* openpic global configuration register (64-bit format). */
	/* openpic Interrupt Source Unit pointer (64-bit format). */
	/* python0 facility area (mmio) (64-bit format) REAL address. */

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	if (ROOT_DEV == 0) {
		printk("No ramdisk, default root is /dev/sda2\n");
		ROOT_DEV = Root_SDA2;
	}

	fwnmi_init();

	/* Find and initialize PCI host bridges */
	/* iSeries needs to be done much later. */
	eeh_init();
	find_and_init_phbs();

	/* Find the Open PIC if present */
	root = of_find_node_by_path("/");
	opprop = (unsigned int *) get_property(root,
				"platform-open-pic", NULL);
	if (opprop != 0) {
		int n = prom_n_addr_cells(root);
		unsigned long openpic;

		for (openpic = 0; n > 0; --n)
			openpic = (openpic << 32) + *opprop++;
		printk(KERN_DEBUG "OpenPIC addr: %lx\n", openpic);
		OpenPIC_Addr = __ioremap(openpic, 0x40000, _PAGE_NO_CACHE);
	}
	of_node_put(root);

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	pSeries_nvram_init();
}

static int __init pSeries_init_panel(void)
{
	/* Manually leave the kernel version on the panel. */
	ppc_md.progress("Linux ppc64\n", 0);
	ppc_md.progress(UTS_RELEASE, 0);

	return 0;
}
arch_initcall(pSeries_init_panel);



void __init pSeries_find_serial_port(void)
{
	struct device_node *np;
	unsigned long encode_phys_size = 32;
	u32 *sizeprop;

	struct isa_reg_property {
		u32 space;
		u32 address;
		u32 size;
	};
	struct pci_reg_property {
		struct pci_address addr;
		u32 size_hi;
		u32 size_lo;
	};                                                                        

	DBG(" -> pSeries_find_serial_port()\n");

	naca->serialPortAddr = 0;

	np = of_find_node_by_path("/");
	if (!np)
		return;
	sizeprop = (u32 *)get_property(np, "#size-cells", NULL);
	if (sizeprop != NULL)
		encode_phys_size = (*sizeprop) << 5;
	
	for (np = NULL; (np = of_find_node_by_type(np, "serial"));) {
		struct device_node *isa, *pci;
		struct isa_reg_property *reg;
		union pci_range *rangesp;
		char *typep;

	 	typep = (char *)get_property(np, "ibm,aix-loc", NULL);
		if ((typep == NULL) || (typep && strcmp(typep, "S1")))
			continue;

		reg = (struct isa_reg_property *)get_property(np, "reg", NULL);	

		isa = of_get_parent(np);
		if (!isa) {
			DBG("no isa parent found\n");
			break;
		}
		pci = of_get_parent(isa);
		if (!pci) {
			DBG("no pci parent found\n");
			break;
		}

		rangesp = (union pci_range *)get_property(pci, "ranges", NULL);

		if ( encode_phys_size == 32 )
			naca->serialPortAddr = rangesp->pci32.phys+reg->address;
		else {
			naca->serialPortAddr =
				((((unsigned long)rangesp->pci64.phys_hi) << 32)
				|
			(rangesp->pci64.phys_lo)) + reg->address;
		}
		break;
	}

	DBG(" <- pSeries_find_serial_port()\n");
}


/* Build up the firmware_features bitmask field
 * using contents of device-tree/ibm,hypertas-functions.
 * Ultimately this functionality may be moved into prom.c prom_init().
 */
void __init fw_feature_init(void)
{
	struct device_node * dn;
	char * hypertas;
	unsigned int len;

	DBG(" -> fw_feature_init()\n");

	cur_cpu_spec->firmware_features = 0;
	dn = of_find_node_by_path("/rtas");
	if (dn == NULL) {
		printk(KERN_ERR "WARNING ! Cannot find RTAS in device-tree !\n");
		goto no_rtas;
	}

	hypertas = get_property(dn, "ibm,hypertas-functions", &len);
	if (hypertas) {
		while (len > 0){
			int i, hypertas_len;
			/* check value against table of strings */
			for(i=0; i < FIRMWARE_MAX_FEATURES ;i++) {
				if ((firmware_features_table[i].name) &&
				    (strcmp(firmware_features_table[i].name,hypertas))==0) {
					/* we have a match */
					cur_cpu_spec->firmware_features |= 
						(firmware_features_table[i].val);
					break;
				} 
			}
			hypertas_len = strlen(hypertas);
			len -= hypertas_len +1;
			hypertas+= hypertas_len +1;
		}
	}

	of_node_put(dn);
 no_rtas:
	printk(KERN_INFO "firmware_features = 0x%lx\n", 
	       cur_cpu_spec->firmware_features);

	DBG(" <- fw_feature_init()\n");
}


static  void __init pSeries_discover_pic(void)
{
	struct device_node *np;
	char *typep;

	/*
	 * Setup interrupt mapping options that are needed for finish_device_tree
	 * to properly parse the OF interrupt tree & do the virtual irq mapping
	 */
	__irq_offset_value = NUM_ISA_INTERRUPTS;
	naca->interrupt_controller = IC_INVALID;
	for (np = NULL; (np = of_find_node_by_name(np, "interrupt-controller"));) {
		typep = (char *)get_property(np, "compatible", NULL);
		if (strstr(typep, "open-pic"))
			naca->interrupt_controller = IC_OPEN_PIC;
		else if (strstr(typep, "ppc-xicp"))
			naca->interrupt_controller = IC_PPC_XIC;
		else
			printk("initialize_naca: failed to recognize"
			       " interrupt-controller\n");
		break;
	}
}

/*
 * Early initialization.  Relocation is on but do not reference unbolted pages
 */
static void __init pSeries_init_early(void)
{
	void *comport;
	int iommu_off = 0;

	DBG(" -> pSeries_init_early()\n");

	fw_feature_init();
	
	if (systemcfg->platform & PLATFORM_LPAR)
		hpte_init_lpar();
	else {
		hpte_init_native();
		iommu_off = (of_chosen &&
			     get_property(of_chosen, "linux,iommu-off", NULL));
	}

	pSeries_find_serial_port();

	if (systemcfg->platform & PLATFORM_LPAR)
		find_udbg_vterm();
	else if (naca->serialPortAddr) {
		/* Map the uart for udbg. */
		comport = (void *)__ioremap(naca->serialPortAddr, 16, _PAGE_NO_CACHE);
		udbg_init_uart(comport);

		ppc_md.udbg_putc = udbg_putc;
		ppc_md.udbg_getc = udbg_getc;
		ppc_md.udbg_getc_poll = udbg_getc_poll;
		DBG("Hello World !\n");
	}


	if (iommu_off)
		pci_dma_init_direct();
	else
		tce_init_pSeries();

	pSeries_discover_pic();

	DBG(" <- pSeries_init_early()\n");
}


static void pSeries_progress(char *s, unsigned short hex)
{
	struct device_node *root;
	int width, *p;
	char *os;
	static int display_character, set_indicator;
	static int max_width;
	static spinlock_t progress_lock = SPIN_LOCK_UNLOCKED;
	static int pending_newline = 0;  /* did last write end with unprinted newline? */

	if (!rtas.base)
		return;

	if (max_width == 0) {
		if ((root = find_path_device("/rtas")) &&
		     (p = (unsigned int *)get_property(root,
						       "ibm,display-line-length",
						       NULL)))
			max_width = *p;
		else
			max_width = 0x10;
		display_character = rtas_token("display-character");
		set_indicator = rtas_token("set-indicator");
	}

	if (display_character == RTAS_UNKNOWN_SERVICE) {
		/* use hex display if available */
		if (set_indicator != RTAS_UNKNOWN_SERVICE)
			rtas_call(set_indicator, 3, 1, NULL, 6, 0, hex);
		return;
	}

	spin_lock(&progress_lock);

	/*
	 * Last write ended with newline, but we didn't print it since
	 * it would just clear the bottom line of output. Print it now
	 * instead.
	 *
	 * If no newline is pending, print a CR to start output at the
	 * beginning of the line.
	 */
	if (pending_newline) {
		rtas_call(display_character, 1, 1, NULL, '\r');
		rtas_call(display_character, 1, 1, NULL, '\n');
		pending_newline = 0;
	} else {
		rtas_call(display_character, 1, 1, NULL, '\r');
	}
 
	width = max_width;
	os = s;
	while (*os) {
		if (*os == '\n' || *os == '\r') {
			/* Blank to end of line. */
			while (width-- > 0)
				rtas_call(display_character, 1, 1, NULL, ' ');
 
			/* If newline is the last character, save it
			 * until next call to avoid bumping up the
			 * display output.
			 */
			if (*os == '\n' && !os[1]) {
				pending_newline = 1;
				spin_unlock(&progress_lock);
				return;
			}
 
			/* RTAS wants CR-LF, not just LF */
 
			if (*os == '\n') {
				rtas_call(display_character, 1, 1, NULL, '\r');
				rtas_call(display_character, 1, 1, NULL, '\n');
			} else {
				/* CR might be used to re-draw a line, so we'll
				 * leave it alone and not add LF.
				 */
				rtas_call(display_character, 1, 1, NULL, *os);
			}
 
			width = max_width;
		} else {
			width--;
			rtas_call(display_character, 1, 1, NULL, *os);
		}
 
		os++;
 
		/* if we overwrite the screen length */
		if (width <= 0)
			while ((*os != 0) && (*os != '\n') && (*os != '\r'))
				os++;
	}
 
	/* Blank to end of line. */
	while (width-- > 0)
		rtas_call(display_character, 1, 1, NULL, ' ');

	spin_unlock(&progress_lock);
}

extern void setup_default_decr(void);

/* Some sane defaults: 125 MHz timebase, 1GHz processor */
#define DEFAULT_TB_FREQ		125000000UL
#define DEFAULT_PROC_FREQ	(DEFAULT_TB_FREQ * 8)

static void __init pSeries_calibrate_decr(void)
{
	struct device_node *cpu;
	struct div_result divres;
	unsigned int *fp;
	int node_found;

	/*
	 * The cpu node should have a timebase-frequency property
	 * to tell us the rate at which the decrementer counts.
	 */
	cpu = of_find_node_by_type(NULL, "cpu");

	ppc_tb_freq = DEFAULT_TB_FREQ;		/* hardcoded default */
	node_found = 0;
	if (cpu != 0) {
		fp = (unsigned int *)get_property(cpu, "timebase-frequency",
						  NULL);
		if (fp != 0) {
			node_found = 1;
			ppc_tb_freq = *fp;
		}
	}
	if (!node_found)
		printk(KERN_ERR "WARNING: Estimating decrementer frequency "
				"(not found)\n");

	ppc_proc_freq = DEFAULT_PROC_FREQ;
	node_found = 0;
	if (cpu != 0) {
		fp = (unsigned int *)get_property(cpu, "clock-frequency",
						  NULL);
		if (fp != 0) {
			node_found = 1;
			ppc_proc_freq = *fp;
		}
	}
	if (!node_found)
		printk(KERN_ERR "WARNING: Estimating processor frequency "
				"(not found)\n");

	of_node_put(cpu);

	printk(KERN_INFO "time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       ppc_tb_freq/1000000, ppc_tb_freq%1000000);
	printk(KERN_INFO "time_init: processor frequency   = %lu.%.6lu MHz\n",
	       ppc_proc_freq/1000000, ppc_proc_freq%1000000);

	tb_ticks_per_jiffy = ppc_tb_freq / HZ;
	tb_ticks_per_sec = tb_ticks_per_jiffy * HZ;
	tb_ticks_per_usec = ppc_tb_freq / 1000000;
	tb_to_us = mulhwu_scale_factor(ppc_tb_freq, 1000000);
	div128_by_32(1024*1024, 0, tb_ticks_per_sec, &divres);
	tb_to_xs = divres.result_low;

	setup_default_decr();
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
extern struct machdep_calls pSeries_md;

static int __init pSeries_probe(int platform)
{
	if (platform != PLATFORM_PSERIES &&
	    platform != PLATFORM_PSERIES_LPAR)
		return 0;

	/* if we have some ppc_md fixups for LPAR to do, do
	 * it here ...
	 */

	return 1;
}

struct machdep_calls __initdata pSeries_md = {
	.probe			= pSeries_probe,
	.setup_arch		= pSeries_setup_arch,
	.init_early		= pSeries_init_early,
	.get_cpuinfo		= pSeries_get_cpuinfo,
	.log_error		= pSeries_log_error,
	.pcibios_fixup		= pSeries_final_fixup,
	.restart		= rtas_restart,
	.power_off		= rtas_power_off,
	.halt			= rtas_halt,
	.panic			= rtas_os_term,
	.get_boot_time		= pSeries_get_boot_time,
	.get_rtc_time		= pSeries_get_rtc_time,
	.set_rtc_time		= pSeries_set_rtc_time,
	.calibrate_decr		= pSeries_calibrate_decr,
	.progress		= pSeries_progress,
};
