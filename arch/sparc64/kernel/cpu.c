/* cpu.c: Dinky routines to look for the kind of Sparc cpu
 *        we are on.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <asm/asi.h>
#include <asm/system.h>
#include <asm/fpumacro.h>

struct cpu_iu_info {
  short manuf;
  short impl;
  char* cpu_name;   /* should be enough I hope... */
};

struct cpu_fp_info {
  short manuf;
  short impl;
  char fpu_vers;
  char* fp_name;
};

/* In order to get the fpu type correct, you need to take the IDPROM's
 * machine type value into consideration too.  I will fix this.
 */
struct cpu_fp_info linux_sparc_fpu[] = {
  { 0x17, 0x10, 0, "UltraSparc I integrated FPU"},
  { 0x22, 0x10, 0, "UltraSparc II integrated FPU"},
  { 0x17, 0x11, 0, "UltraSparc II integrated FPU"},
  { 0x17, 0x12, 0, "UltraSparc IIi integrated FPU"},
  { 0x17, 0x13, 0, "UltraSparc III integrated FPU"},
};

#define NSPARCFPU  (sizeof(linux_sparc_fpu)/sizeof(struct cpu_fp_info))

struct cpu_iu_info linux_sparc_chips[] = {
  { 0x17, 0x10, "TI UltraSparc I   (SpitFire)"},
  { 0x22, 0x10, "TI UltraSparc II  (BlackBird)"},
  { 0x17, 0x11, "TI UltraSparc II  (BlackBird)"},
  { 0x17, 0x12, "TI UltraSparc IIi"},
  { 0x17, 0x13, "TI UltraSparc III (Cheetah)"},  /* A guess... */
};

#define NSPARCCHIPS  (sizeof(linux_sparc_chips)/sizeof(struct cpu_iu_info))

#ifdef __SMP__
char *sparc_cpu_type[NR_CPUS] = { "cpu-oops", "cpu-oops1", "cpu-oops2", "cpu-oops3" };
char *sparc_fpu_type[NR_CPUS] = { "fpu-oops", "fpu-oops1", "fpu-oops2", "fpu-oops3" };
#else
char *sparc_cpu_type[NR_CPUS] = { "cpu-oops", };
char *sparc_fpu_type[NR_CPUS] = { "fpu-oops", };
#endif

unsigned int fsr_storage;

__initfunc(void cpu_probe(void))
{
	int manuf, impl;
	unsigned i, cpuid;
	long ver, fpu_vers;
	long fprs;
	
	cpuid = smp_processor_id();

	fprs = fprs_read ();
	fprs_write (FPRS_FEF);
	__asm__ __volatile__ ("rdpr %%ver, %0; stx %%fsr, [%1]" : "=r" (ver) : "r" (&fpu_vers));
	fprs_write (fprs);
	
	manuf = ((ver >> 48)&0xffff);
	impl = ((ver >> 32)&0xffff);

	fpu_vers = ((fpu_vers>>17)&0x7);

	for(i = 0; i<NSPARCCHIPS; i++) {
		if(linux_sparc_chips[i].manuf == manuf)
			if(linux_sparc_chips[i].impl == impl) {
				sparc_cpu_type[cpuid] = linux_sparc_chips[i].cpu_name;
				break;
			}
	}

	if(i==NSPARCCHIPS) {
		printk("DEBUG: manuf = 0x%x   impl = 0x%x\n", manuf, 
			    impl);
		sparc_cpu_type[cpuid] = "Unknow CPU";
	}

	for(i = 0; i<NSPARCFPU; i++) {
		if(linux_sparc_fpu[i].manuf == manuf && linux_sparc_fpu[i].impl == impl)
			if(linux_sparc_fpu[i].fpu_vers == fpu_vers) {
				sparc_fpu_type[cpuid] = linux_sparc_fpu[i].fp_name;
				break;
			}
	}

	if(i == NSPARCFPU) {
		printk("DEBUG: manuf = 0x%x  impl = 0x%x fsr.vers = 0x%x\n", manuf, impl,
			    (unsigned)fpu_vers);
		sparc_fpu_type[cpuid] = "Unknown FPU";
	}
}
