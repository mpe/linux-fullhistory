/* $Id: probe.c,v 1.42 1995/12/26 01:38:08 davem Exp $
 * probe.c: Preliminary device tree probing routines...
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/string.h>

#include <asm/oplib.h>
#include <asm/vac-ops.h>
#include <asm/idprom.h>
#include <asm/machines.h>
#include <asm/io.h>
#include <asm/vaddrs.h>
#include <asm/param.h>
#include <asm/timer.h>
#include <asm/mostek.h>
#include <asm/auxio.h>
#include <asm/system.h>
#include <asm/mp.h>
#include <asm/mbus.h>

/* #define DEBUG_PROBING */

/* XXX Grrr, this stuff should have it's own file, only generic stuff goes
 * XXX here.  Possibly clock.c and timer.c?  This file should get smaller
 * XXX and smaller as time goes on...
 */
enum sparc_clock_type sp_clock_typ;
struct mostek48t02 *mstk48t02_regs = 0;
struct mostek48t08 *mstk48t08_regs = 0;
volatile unsigned int *master_l10_limit = 0;
volatile unsigned int *master_l10_counter = 0;
struct sun4m_timer_regs *sun4m_timers;

static char node_str[128];

/* Cpu-type information and manufacturer strings */

struct cpu_iu_info {
  int psr_impl;
  int psr_vers;
  char* cpu_name;   /* should be enough I hope... */
};

struct cpu_fp_info {
  int psr_impl;
  int fp_vers;
  char* fp_name;
};

/* In order to get the fpu type correct, you need to take the IDPROM's
 * machine type value into consideration too.  I will fix this.
 */
struct cpu_fp_info linux_sparc_fpu[] = {
  { 0, 0, "Fujitsu MB86910 or Weitek WTL1164/5"},
  { 0, 1, "Fujitsu MB86911 or Weitek WTL1164/5 or LSI L64831"},
  { 0, 2, "LSI Logic L64802 or Texas Instruments ACT8847"},
  /* SparcStation SLC, SparcStation1 */
  { 0, 3, "Weitek WTL3170/2"},
  /* SPARCstation-5 */
  { 0, 4, "Lsi Logic/Meiko L64804 or compatible"},
  { 0, 5, "reserved"},
  { 0, 6, "reserved"},
  { 0, 7, "No FPU"},
  { 1, 0, "ROSS HyperSparc combined IU/FPU"},
  { 1, 1, "Lsi Logic L64814"},
  { 1, 2, "Texas Instruments TMS390-C602A"},
  { 1, 3, "Cypress CY7C602 FPU"},
  { 1, 4, "reserved"},
  { 1, 5, "reserved"},
  { 1, 6, "reserved"},
  { 1, 7, "No FPU"},
  { 2, 0, "BIT B5010 or B5110/20 or B5210"},
  { 2, 1, "reserved"},
  { 2, 2, "reserved"},
  { 2, 3, "reserved"},
  { 2, 4, "reserved"},
  { 2, 5, "reserved"},
  { 2, 6, "reserved"},
  { 2, 7, "No FPU"},
  /* SuperSparc 50 module */
  { 4, 0, "SuperSparc on-chip FPU"},
  /* SparcClassic */
  { 4, 4, "TI MicroSparc on chip FPU"},
  { 5, 0, "Matsushita MN10501"},
  { 5, 1, "reserved"},
  { 5, 2, "reserved"},
  { 5, 3, "reserved"},
  { 5, 4, "reserved"},
  { 5, 5, "reserved"},
  { 5, 6, "reserved"},
  { 5, 7, "No FPU"},
};

#define NSPARCFPU  (sizeof(linux_sparc_fpu)/sizeof(struct cpu_fp_info))

struct cpu_iu_info linux_sparc_chips[] = {
  /* Sun4/100, 4/200, SLC */
  { 0, 0, "Fujitsu  MB86900/1A or LSI L64831 SparcKIT-40"},
  /* borned STP1012PGA */
  { 0, 4, "Fujitsu  MB86904"},
  /* SparcStation2, SparcServer 490 & 690 */
  { 1, 0, "LSI Logic Corporation - L64811"},
  /* SparcStation2 */
  { 1, 1, "Cypress/ROSS CY7C601"},
  /* Embedded controller */
  { 1, 3, "Cypress/ROSS CY7C611"},
  /* Ross Technologies HyperSparc */
  { 1, 0xf, "ROSS HyperSparc RT620"},
  { 1, 0xe, "ROSS HyperSparc RT625"},
  /* ECL Implementation, CRAY S-MP Supercomputer... AIEEE! */
  /* Someone please write the code to support this beast! ;) */
  { 2, 0, "Bipolar Integrated Technology - B5010"},
  { 3, 0, "LSI Logic Corporation - unknown-type"},
  { 4, 0, "Texas Instruments, Inc. - SuperSparc 50"},
  /* SparcClassic  --  borned STP1010TAB-50*/
  { 4, 1, "Texas Instruments, Inc. - MicroSparc"},
  { 4, 2, "Texas Instruments, Inc. - MicroSparc II"},
  { 4, 3, "Texas Instruments, Inc. - SuperSparc 51"},
  { 4, 4, "Texas Instruments, Inc. - SuperSparc 61"},
  { 4, 5, "Texas Instruments, Inc. - unknown"},
  { 5, 0, "Matsushita - MN10501"},
  { 6, 0, "Philips Corporation - unknown"},
  { 7, 0, "Harvest VLSI Design Center, Inc. - unknown"},
  /* Gallium arsenide 200MHz, BOOOOGOOOOMIPS!!! */
  { 8, 0, "Systems and Processes Engineering Corporation (SPEC)"},
  { 9, 0, "Fujitsu #3"},
  { 0xa, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xb, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xc, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xd, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xe, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xf, 0, "UNKNOWN CPU-VENDOR/TYPE"},
};

#define NSPARCCHIPS  (sizeof(linux_sparc_chips)/sizeof(struct cpu_iu_info))

char *sparc_cpu_type[NCPUS] = { "cpu-oops", "cpu-oops1", "cpu-oops2", "cpu-oops3" };
char *sparc_fpu_type[NCPUS] = { "fpu-oops", "fpu-oops1", "fpu-oops2", "fpu-oops3" };

static inline int find_mmu_num_contexts(int cpu)
{
	return prom_getintdefault(cpu, "mmu-nctx", 0x8);
}

unsigned int fsr_storage;

void
probe_cpu(void)
{
	int psr_impl, psr_vers, fpu_vers;
	int i, cpuid;

	cpuid = get_cpuid();

	psr_impl = ((get_psr()>>28)&0xf);
	psr_vers = ((get_psr()>>24)&0xf);

	fpu_vers = ((get_fsr()>>17)&0x7);

	for(i = 0; i<NSPARCCHIPS; i++) {
		if(linux_sparc_chips[i].psr_impl == psr_impl)
			if(linux_sparc_chips[i].psr_vers == psr_vers) {
				sparc_cpu_type[cpuid] = linux_sparc_chips[i].cpu_name;
				break;
			}
	}

	if(i==NSPARCCHIPS)
		printk("DEBUG: psr.impl = 0x%x   psr.vers = 0x%x\n", psr_impl, 
			    psr_vers);

	for(i = 0; i<NSPARCFPU; i++) {
		if(linux_sparc_fpu[i].psr_impl == psr_impl)
			if(linux_sparc_fpu[i].fp_vers == fpu_vers) {
				sparc_fpu_type[cpuid] = linux_sparc_fpu[i].fp_name;
				break;
			}
	}

	if(i == NSPARCFPU) {
		printk("DEBUG: psr.impl = 0x%x  fsr.vers = 0x%x\n", psr_impl,
			    fpu_vers);
		sparc_fpu_type[cpuid] = linux_sparc_fpu[31].fp_name;
	}
}

void
probe_vac(void)
{
	int propval;

	sun4c_disable_vac();
	sun4c_vacinfo.num_bytes = prom_getintdefault(prom_root_node,
						     "vac-size", 65536);
	sun4c_vacinfo.linesize = prom_getintdefault(prom_root_node,
						    "vac-linesize", 16);
	sun4c_vacinfo.num_lines =
		(sun4c_vacinfo.num_bytes / sun4c_vacinfo.linesize);
	switch(sun4c_vacinfo.linesize) {
	case 16:
		sun4c_vacinfo.log2lsize = 4;
		break;
	case 32:
		sun4c_vacinfo.log2lsize = 5;
		break;
	default:
		prom_printf("probe_vac: Didn't expect vac-linesize of %d, halting\n",
			    sun4c_vacinfo.linesize);
		prom_halt();
	};

	propval = prom_getintdefault(prom_root_node, "vac_hwflush", -1);
	sun4c_vacinfo.do_hwflushes = (propval == -1 ?
				      prom_getintdefault(prom_root_node,
							 "vac-hwflush", 0) :
				      propval);

	if(sun4c_vacinfo.num_bytes != 65536) {
		prom_printf("WEIRD Sun4C VAC cache size, tell davem");
		prom_halt();
	}

	sun4c_flush_all();
	sun4c_enable_vac();
}

extern int num_segmaps, num_contexts;

/* XXX Only called on sun4c XXX */
void
probe_mmu(void)
{
	int cpuid;

	/* who are we? */
	cpuid = get_cpuid();
	switch(sparc_cpu_model) {
	case sun4:
	case sun4c:
	case sun4e:
		/* A sun4, sun4c or sun4e. */
		num_segmaps = prom_getintdefault(prom_root_node, "mmu-npmg", 128);
		num_contexts = find_mmu_num_contexts(prom_root_node);
		break;
	default:
		printk("cpu%d probe_mmu: sparc_cpu_model botch\n", cpuid);
		break;
	};
}

/* Clock probing, we probe the timers here also. */
volatile unsigned int foo_limit;

void
probe_clock(int fchild)
{
	register int node, type;
	struct linux_prom_registers clk_reg[2];

	/* This will basically traverse the node-tree of the prom to see
	 * which timer chip is on this machine.
	 */

	node = 0;
	if(sparc_cpu_model == sun4) {
		printk("probe_clock: No SUN4 Clock/Timer support yet...\n");
		return;
	}
	if(sparc_cpu_model == sun4c) node=prom_getchild(prom_root_node);
	else
		if(sparc_cpu_model == sun4m)
			node=prom_getchild(prom_searchsiblings(prom_getchild(prom_root_node), "obio"));
	type = 0;
	sp_clock_typ = MSTK_INVALID;
	for(;;) {
		prom_getstring(node, "model", node_str, sizeof(node_str));
		if(strcmp(node_str, "mk48t02") == 0) {
			sp_clock_typ = MSTK48T02;
			if(prom_getproperty(node, "reg", (char *) clk_reg, sizeof(clk_reg)) == -1) {
				printk("probe_clock: FAILED!\n");
				halt();
			}
			prom_apply_obio_ranges(clk_reg, 1);
			/* Map the clock register io area read-only */
			mstk48t02_regs = (struct mostek48t02 *) 
				sparc_alloc_io((void *) clk_reg[0].phys_addr,
					       (void *) 0, sizeof(*mstk48t02_regs),
					       "clock", 0x0, 0x0);
			mstk48t08_regs = 0;  /* To catch weirdness */
			break;
		}

		if(strcmp(node_str, "mk48t08") == 0) {
			sp_clock_typ = MSTK48T08;
			if(prom_getproperty(node, "reg", (char *) clk_reg,
					    sizeof(clk_reg)) == -1) {
				printk("probe_clock: FAILED!\n");
				halt();
			}
			prom_apply_obio_ranges(clk_reg, 1);
			/* Map the clock register io area read-only */
			mstk48t08_regs = (struct mostek48t08 *)
				sparc_alloc_io((void *) clk_reg[0].phys_addr,
					       (void *) 0, sizeof(*mstk48t08_regs),
					       "clock", 0x0, 0x0);

			mstk48t02_regs = &mstk48t08_regs->regs;
			break;
		}

		node = prom_getsibling(node);
		if(node == 0) {
			printk("Aieee, could not find timer chip type\n");
			return;
		}
	}

	if(sparc_cpu_model == sun4c) {
		/* Map the Timer chip, this is implemented in hardware inside
		 * the cache chip on the sun4c.
		 */
		sparc_alloc_io ((void *) SUN4C_TIMER_PHYSADDR, (void *) TIMER_VADDR,
				sizeof (*SUN4C_TIMER_STRUCT), "timer", 0x0, 0x0);

		/* Have the level 10 timer tick at 100HZ.  We don't touch the
		 * level 14 timer limit since we are letting the prom handle
		 * them until we have a real console driver so L1-A works.
		 */
		SUN4C_TIMER_STRUCT->timer_limit10 = (((1000000/HZ) + 1) << 10);
		master_l10_limit = &(SUN4C_TIMER_STRUCT->timer_limit10);
		master_l10_counter = &(SUN4C_TIMER_STRUCT->cur_count10);
	} else {
		int reg_count;
		struct linux_prom_registers cnt_regs[PROMREG_MAX];
		int obio_node, cnt_node;

		cnt_node = 0;
		if((obio_node =
		    prom_searchsiblings (prom_getchild(prom_root_node), "obio")) == 0 ||
		   (obio_node = prom_getchild (obio_node)) == 0 ||
		   (cnt_node = prom_searchsiblings (obio_node, "counter")) == 0) {
			printk ("Cannot find /obio/counter node\n");
			prom_halt ();
		}
		reg_count = prom_getproperty(cnt_node, "reg",
					     (void *) cnt_regs, sizeof(cnt_regs));
		reg_count = (reg_count/sizeof(struct linux_prom_registers));

		/* Apply the obio ranges to the timer registers. */
		prom_apply_obio_ranges(cnt_regs, reg_count);

		/* Map the per-cpu Counter registers. */
		sparc_alloc_io(cnt_regs[0].phys_addr, (void *) TIMER_VADDR,
			       PAGE_SIZE*NCPUS, "counters_percpu",
			       cnt_regs[0].which_io, 0x0);

		/* Map the system Counter register. */
		sparc_alloc_io(cnt_regs[reg_count-1].phys_addr,
			       (void *) TIMER_VADDR+(NCPUS*PAGE_SIZE),
			       cnt_regs[reg_count-1].reg_size,
			       "counters_system", cnt_regs[reg_count-1].which_io, 0x0);
		sun4m_timers = (struct sun4m_timer_regs *) TIMER_VADDR;

		/* Avoid interrupt bombs... */
		foo_limit = (volatile) sun4m_timers->l10_timer_limit;

		/* Must set the master pointer first or we will lose badly. */
		master_l10_limit =
			&(((struct sun4m_timer_regs *)TIMER_VADDR)->l10_timer_limit);
		master_l10_counter =
			&(((struct sun4m_timer_regs *)TIMER_VADDR)->l10_cur_count);

		((struct sun4m_timer_regs *)TIMER_VADDR)->l10_timer_limit =
			(((1000000/HZ) + 1) << 10);
	}
}

/* Probe and map in the Auxiliaary I/O register */
void
probe_auxio(void)
{
	int node, auxio_nd;
	struct linux_prom_registers auxregs[1];

	node = prom_getchild(prom_root_node);
	auxio_nd = prom_searchsiblings(node, "auxiliary-io");
	if(!auxio_nd) {
		node = prom_searchsiblings(node, "obio");
		node = prom_getchild(node);
		auxio_nd = prom_searchsiblings(node, "auxio");
		if(!auxio_nd) {
			printk("Cannot find auxio node, cannot continue...\n");
			prom_halt();
		}
	}
	prom_getproperty(auxio_nd, "reg", (char *) auxregs, sizeof(auxregs));
	prom_apply_obio_ranges(auxregs, 0x1);
	/* Map the register both read and write */
	sparc_alloc_io(auxregs[0].phys_addr, (void *) AUXIO_VADDR,
		       auxregs[0].reg_size, "auxilliaryIO", auxregs[0].which_io, 0x0);
}

extern unsigned long probe_memory(void);
extern struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];
unsigned int phys_bytes_of_ram, end_of_phys_memory;
extern void probe_mbus(void);

/* #define DEBUG_PROBE_DEVICES */
struct prom_cpuinfo linux_cpus[NCPUS];
int linux_num_cpus;

unsigned long
probe_devices(unsigned long mem_start)
{
	int nd, i, prom_node_cpu, thismid;
	int cpu_nds[NCPUS];  /* One node for each cpu */
	int cpu_ctr = 0;

	prom_getstring(prom_root_node, "device_type", node_str, sizeof(node_str));
	if(strcmp(node_str, "cpu") == 0) {
		cpu_nds[0] = prom_root_node;
		cpu_ctr++;
	} else {
		int scan;
		scan = prom_getchild(prom_root_node);
		nd = 0;
		while((scan = prom_getsibling(scan)) != 0) {
			prom_getstring(scan, "device_type", node_str, sizeof(node_str));
			if(strcmp(node_str, "cpu") == 0) {
				cpu_nds[cpu_ctr] = scan;
				linux_cpus[cpu_ctr].prom_node = scan;
				prom_getproperty(scan, "mid", (char *) &thismid, sizeof(thismid));
				linux_cpus[cpu_ctr].mid = thismid;
				cpu_ctr++;
			}
		};
		if(cpu_ctr == 0) {
			printk("No CPU nodes found, cannot continue.\n");
			/* Probably a sun4d or sun4e, Sun is trying to trick us ;-) */
			halt();
		}
		printk("Found %d CPU prom device tree node(s).\n", cpu_ctr);
	};
	prom_node_cpu = cpu_nds[0];

	linux_num_cpus = cpu_ctr;
	for(i=0; i<cpu_ctr; i++) {
		prom_getstring(cpu_nds[i], "name", node_str, sizeof(node_str));
#if 0
		printk("cpu%d: %s \n", i, node_str);
#endif
	}

	probe_cpu();
	probe_auxio();
	if(sparc_cpu_model != sun4c) probe_mbus();

	return mem_start;
}
