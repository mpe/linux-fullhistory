#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#ifdef __SMP__
#ifndef ASSEMBLY

#include <asm/i82489.h>
#include <asm/bitops.h>
#include <linux/tasks.h>
#include <linux/ptrace.h>

/*
 *	Support definitions for SMP machines following the intel multiprocessing
 *	specification
 */

/*
 *	This tag identifies where the SMP configuration
 *	information is. 
 */
 
#define SMP_MAGIC_IDENT	(('_'<<24)|('P'<<16)|('M'<<8)|'_')

struct intel_mp_floating
{
	char mpf_signature[4];		/* "_MP_" 			*/
	unsigned long mpf_physptr;	/* Configuration table address	*/
	unsigned char mpf_length;	/* Our length (paragraphs)	*/
	unsigned char mpf_specification;/* Specification version	*/
	unsigned char mpf_checksum;	/* Checksum (makes sum 0)	*/
	unsigned char mpf_feature1;	/* Standard or configuration ? 	*/
	unsigned char mpf_feature2;	/* Bit7 set for IMCR|PIC	*/
	unsigned char mpf_feature3;	/* Unused (0)			*/
	unsigned char mpf_feature4;	/* Unused (0)			*/
	unsigned char mpf_feature5;	/* Unused (0)			*/
};

struct mp_config_table
{
	char mpc_signature[4];
#define MPC_SIGNATURE "PCMP"
	unsigned short mpc_length;	/* Size of table */
	char  mpc_spec;			/* 0x01 */
	char  mpc_checksum;
	char  mpc_oem[8];
	char  mpc_productid[12];
	unsigned long mpc_oemptr;	/* 0 if not present */
	unsigned short mpc_oemsize;	/* 0 if not present */
	unsigned short mpc_oemcount;
	unsigned long mpc_lapic;	/* APIC address */
	unsigned long reserved;
};

/* Followed by entries */

#define	MP_PROCESSOR	0
#define	MP_BUS		1
#define	MP_IOAPIC	2
#define	MP_INTSRC	3
#define	MP_LINTSRC	4

struct mpc_config_processor
{
	unsigned char mpc_type;
	unsigned char mpc_apicid;	/* Local APIC number */
	unsigned char mpc_apicver;	/* Its versions */
	unsigned char mpc_cpuflag;
#define CPU_ENABLED		1	/* Processor is available */
#define CPU_BOOTPROCESSOR	2	/* Processor is the BP */
	unsigned long mpc_cpufeature;		
#define CPU_STEPPING_MASK 0x0F
#define CPU_MODEL_MASK	0xF0
#define CPU_FAMILY_MASK	0xF00
	unsigned long mpc_featureflag;	/* CPUID feature value */
	unsigned long mpc_reserved[2];
};

struct mpc_config_bus
{
	unsigned char mpc_type;
	unsigned char mpc_busid;
	unsigned char mpc_bustype[6] __attribute((packed));
};

#define BUSTYPE_EISA	"EISA"
#define BUSTYPE_ISA	"ISA"
#define BUSTYPE_INTERN	"INTERN"	/* Internal BUS */
#define BUSTYPE_MCA	"MCA"
#define BUSTYPE_VL	"VL"		/* Local bus */
#define BUSTYPE_PCI	"PCI"
#define BUSTYPE_PCMCIA	"PCMCIA"

/* We don't understand the others */

struct mpc_config_ioapic
{
	unsigned char mpc_type;
	unsigned char mpc_apicid;
	unsigned char mpc_apicver;
	unsigned char mpc_flags;
#define MPC_APIC_USABLE		0x01
	unsigned long mpc_apicaddr;
};

struct mpc_config_intsrc
{
	unsigned char mpc_type;
	unsigned char mpc_irqtype;
	unsigned short mpc_irqflag;
	unsigned char mpc_srcbus;
	unsigned char mpc_srcbusirq;
	unsigned char mpc_dstapic;
	unsigned char mpc_dstirq;
};

#define MP_INT_VECTORED		0
#define MP_INT_NMI		1
#define MP_INT_SMI		2
#define MP_INT_EXTINT		3

#define MP_IRQDIR_DEFAULT	0
#define MP_IRQDIR_HIGH		1
#define MP_IRQDIR_LOW		3


struct mpc_config_intlocal
{
	unsigned char mpc_type;
	unsigned char mpc_irqtype;
	unsigned short mpc_irqflag;
	unsigned char mpc_srcbusid;
	unsigned char mpc_srcbusirq;
	unsigned char mpc_destapic;	
#define MP_APIC_ALL	0xFF
	unsigned char mpc_destapiclint;
};


/*
 *	Default configurations
 *
 *	1	2 CPU ISA 82489DX
 *	2	2 CPU EISA 82489DX no IRQ 8 or timer chaining
 *	3	2 CPU EISA 82489DX
 *	4	2 CPU MCA 82489DX
 *	5	2 CPU ISA+PCI
 *	6	2 CPU EISA+PCI
 *	7	2 CPU MCA+PCI
 */

/*
 *	Per process x86 parameters
 */
 
struct cpuinfo_x86
{
	char hard_math;
	char x86;
	char x86_model;
	char x86_mask;
	char x86_vendor_id[16];
	int  x86_capability;
	int  fdiv_bug;
	int  have_cpuid;
	char wp_works_ok;
	char hlt_works_ok;
	unsigned long udelay_val;
};


extern struct cpuinfo_x86 cpu_data[NR_CPUS];

/*
 *	Private routines/data
 */
 
extern int smp_found_config;
extern int smp_scan_config(unsigned long, unsigned long);
extern unsigned long smp_alloc_memory(unsigned long mem_base);
extern unsigned char *apic_reg;
extern unsigned char *kernel_stacks[NR_CPUS];
extern unsigned char boot_cpu_id;
extern unsigned long cpu_present_map;
extern volatile int cpu_number_map[NR_CPUS];
extern volatile int cpu_logical_map[NR_CPUS];
extern volatile unsigned long smp_invalidate_needed;
extern void smp_flush_tlb(void);
extern volatile unsigned long kernel_flag, kernel_counter;
extern volatile unsigned long cpu_callin_map[NR_CPUS];
extern volatile unsigned char active_kernel_processor;
extern void smp_message_irq(int cpl, void *dev_id, struct pt_regs *regs);
extern void smp_reschedule_irq(int cpl, struct pt_regs *regs);
extern unsigned long ipi_count;
extern void smp_invalidate_rcv(void);		/* Process an NMI */
extern volatile unsigned long kernel_counter;
extern volatile unsigned long syscall_count;

/*
 *	General functions that each host system must provide.
 */
 
extern void smp_callin(void);
extern void smp_boot_cpus(void);
extern void smp_store_cpu_info(int id);		/* Store per cpu info (like the initial udelay numbers */

extern volatile unsigned long smp_proc_in_lock[NR_CPUS]; /* for computing process time */
extern volatile unsigned long smp_process_available;

/*
 *	APIC handlers: Note according to the Intel specification update
 *	you should put reads between APIC writes.
 *	Intel Pentium processor specification update [11AP, pg 64]
 *		"Back to Back Assertions of HOLD May Cause Lost APIC Write Cycle"
 */

extern __inline void apic_write(unsigned long reg, unsigned long v)
{
	*((volatile unsigned long *)(apic_reg+reg))=v;
}

extern __inline unsigned long apic_read(unsigned long reg)
{
	return *((volatile unsigned long *)(apic_reg+reg));
}

/*
 *	This function is needed by all SMP systems. It must _always_ be valid from the initial
 *	startup. This may require magic on some systems (in the i86 case we dig out the boot 
 *	cpu id from the config and set up a fake apic_reg pointer so that before we activate
 *	the apic we get the right answer). Hopefully other processors are more sensible 8)
 */
 
extern __inline int smp_processor_id(void)
{
	return GET_APIC_ID(apic_read(APIC_ID));
}

#endif /* !ASSEMBLY */

#define NO_PROC_ID		0xFF		/* No processor magic marker */

/*
 *	This magic constant controls our willingness to transfer
 *	a process across CPUs. Such a transfer incurs misses on the L1
 *	cache, and on a P6 or P5 with multiple L2 caches L2 hits. My
 *	gut feeling is this will vary by board in value. For a board
 *	with separate L2 cache it probably depends also on the RSS, and
 *	for a board with shared L2 cache it ought to decay fast as other
 *	processes are run.
 */
 
#define PROC_CHANGE_PENALTY	20		/* Schedule penalty */

#define SMP_FROM_INT		1
#define SMP_FROM_SYSCALL	2

#endif
#endif
