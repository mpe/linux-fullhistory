/*
 *  arch/ppc/kernel/openpic.c -- OpenPIC Interrupt Handling
 *
 *  Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */


/*
 *  Note: Interprocessor Interrupt (IPI) and Timer support is incomplete
 */


#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/openpic.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/io.h>
#include <asm/irq.h>


#define REGISTER_DEBUG
#undef REGISTER_DEBUG


volatile struct OpenPIC *OpenPIC = NULL;
u_int OpenPIC_NumInitSenses __initdata = 0;
u_char *OpenPIC_InitSenses __initdata = NULL;

static u_int NumProcessors;
static u_int NumSources;


    /*
     *  Accesses to the current processor's registers
     */

#ifndef __powerpc__
#define THIS_CPU		Private
#define CHECK_THIS_CPU		do {} while (0)
#else
#define THIS_CPU		Processor[cpu]
#define CHECK_THIS_CPU		check_arg_cpu(cpu)
#endif


    /*
     *  Sanity checks
     */

#if 1
#define check_arg_ipi(ipi) \
    if (ipi < 0 || ipi >= OPENPIC_NUM_IPI) \
	printk("openpic.c:%d: illegal ipi %d\n", __LINE__, ipi);
#define check_arg_timer(timer) \
    if (timer < 0 || timer >= OPENPIC_NUM_TIMERS) \
	printk("openpic.c:%d: illegal timer %d\n", __LINE__, timer);
#define check_arg_vec(vec) \
    if (vec < 0 || vec >= OPENPIC_NUM_VECTORS) \
	printk("openpic.c:%d: illegal vector %d\n", __LINE__, vec);
#define check_arg_pri(pri) \
    if (pri < 0 || pri >= OPENPIC_NUM_PRI) \
	printk("openpic.c:%d: illegal priority %d\n", __LINE__, pri);
#define check_arg_irq(irq) \
    if (irq < 0 || irq >= NumSources) \
	printk("openpic.c:%d: illegal irq %d\n", __LINE__, irq);
#define check_arg_cpu(cpu) \
    if (cpu < 0 || cpu >= NumProcessors) \
	printk("openpic.c:%d: illegal cpu %d\n", __LINE__, cpu);
#else
#define check_arg_ipi(ipi)	do {} while (0)
#define check_arg_timer(timer)	do {} while (0)
#define check_arg_vec(vec)	do {} while (0)
#define check_arg_pri(pri)	do {} while (0)
#define check_arg_irq(irq)	do {} while (0)
#define check_arg_cpu(cpu)	do {} while (0)
#endif


    /*
     *  Dummy interrupt handler
     */

static void no_action(int ir1, void *dev, struct pt_regs *regs)
{}


    /*
     *  I/O functions
     */

#ifdef __i386__
static inline u_int ld_le32(volatile u_int *addr)
{
    return *addr;
}

static inline void out_le32(volatile u_int *addr, u_int val)
{
    *addr = val;
}
#endif

static inline u_int openpic_read(volatile u_int *addr)
{
    u_int val;

    val = ld_le32(addr);
#ifdef REGISTER_DEBUG
    printk("openpic_read(0x%08x) = 0x%08x\n", (u_int)addr, val);
#endif
    return val;
}

static inline void openpic_write(volatile u_int *addr, u_int val)
{
#ifdef REGISTER_DEBUG
    printk("openpic_write(0x%08x, 0x%08x)\n", (u_int)addr, val);
#endif
    out_le32(addr, val);
}


static inline u_int openpic_readfield(volatile u_int *addr, u_int mask)
{
    u_int val = openpic_read(addr);
    return val & mask;
}

inline void openpic_writefield(volatile u_int *addr, u_int mask,
				      u_int field)
{
    u_int val = openpic_read(addr);
    openpic_write(addr, (val & ~mask) | (field & mask));
}

static inline void openpic_clearfield(volatile u_int *addr, u_int mask)
{
    openpic_writefield(addr, mask, 0);
}

static inline void openpic_setfield(volatile u_int *addr, u_int mask)
{
    openpic_writefield(addr, mask, mask);
}


    /*
     *  Update a Vector/Priority register in a safe manner. The interrupt will
     *  be disabled.
     */

static void openpic_safe_writefield(volatile u_int *addr, u_int mask,
				    u_int field)
{
    openpic_setfield(addr, OPENPIC_MASK);
    /* wait until it's not in use */
    while (openpic_read(addr) & OPENPIC_ACTIVITY);
    openpic_writefield(addr, mask | OPENPIC_MASK, field | OPENPIC_MASK);
}


/* -------- Global Operations ---------------------------------------------- */


    /*
     *  Initialize the OpenPIC
     */

__initfunc(void openpic_init(int main_pic))
{
    u_int t, i;
    u_int vendorid, devid, stepping, timerfreq;
    const char *version, *vendor, *device;

    if (!OpenPIC)
	panic("No OpenPIC found");

    t = openpic_read(&OpenPIC->Global.Feature_Reporting0);
    switch (t & OPENPIC_FEATURE_VERSION_MASK) {
	case 1:
	    version = "1.0";
	    break;
	case 2:
	    version = "1.2";
	    break;
	default:
	    version = "?";
	    break;
    }
    NumProcessors = ((t & OPENPIC_FEATURE_LAST_PROCESSOR_MASK) >>
		     OPENPIC_FEATURE_LAST_PROCESSOR_SHIFT) + 1;
    NumSources = ((t & OPENPIC_FEATURE_LAST_SOURCE_MASK) >>
		  OPENPIC_FEATURE_LAST_SOURCE_SHIFT) + 1;
    printk("OpenPIC Version %s (%d CPUs and %d IRQ sources) at %p\n", version,
	   NumProcessors, NumSources, OpenPIC);

    t = openpic_read(&OpenPIC->Global.Vendor_Identification);
    vendorid = t & OPENPIC_VENDOR_ID_VENDOR_ID_MASK;
    devid = (t & OPENPIC_VENDOR_ID_DEVICE_ID_MASK) >>
	    OPENPIC_VENDOR_ID_DEVICE_ID_SHIFT;
    stepping = (t & OPENPIC_VENDOR_ID_STEPPING_MASK) >>
	       OPENPIC_VENDOR_ID_STEPPING_SHIFT;
    switch (vendorid) {
	case OPENPIC_VENDOR_ID_APPLE:
	    vendor = "Apple";
	    break;
	default:
	    vendor = "Unknown";
	    break;
    }
    switch (devid) {
	case OPENPIC_DEVICE_ID_APPLE_HYDRA:
	    device = "Hydra";
	    break;
	default:
	    device = "Unknown";
	    break;
    }
    printk("OpenPIC Vendor %d (%s), Device %d (%s), Stepping %d\n", vendorid,
	   vendor, devid, device, stepping);

    timerfreq = openpic_read(&OpenPIC->Global.Timer_Frequency);
    printk("OpenPIC timer frequency is ");
    if (timerfreq)
	printk("%d Hz\n", timerfreq);
    else
	printk("not set\n");

    if ( main_pic )
    {
	    /* Initialize timer interrupts */
	    for (i = 0; i < OPENPIC_NUM_TIMERS; i++) {
		    /* Disabled, Priority 0 */
		    openpic_inittimer(i, 0, OPENPIC_VEC_TIMER+i);
		    /* No processor */
		    openpic_maptimer(i, 0);
	    }
	    
	    /* Initialize IPI interrupts */
	    for (i = 0; i < OPENPIC_NUM_IPI; i++) {
		    /* Disabled, Priority 0 */
		    openpic_initipi(i, 0, OPENPIC_VEC_IPI+i);
	    }
	    
	    /* Initialize external interrupts */
	    /* SIOint (8259 cascade) is special */
	    openpic_initirq(0, 8, OPENPIC_VEC_SOURCE, 1, 1);
	    /* Processor 0 */
	    openpic_mapirq(0, 1<<0);
	    for (i = 1; i < NumSources; i++) {
		    /* Enabled, Priority 8 */
		    openpic_initirq(i, 8, OPENPIC_VEC_SOURCE+i, 0,
				    i < OpenPIC_NumInitSenses ? OpenPIC_InitSenses[i] : 1);
		    /* Processor 0 */
		    openpic_mapirq(i, 1<<0);
	    }
	    
	    /* Initialize the spurious interrupt */
	    openpic_set_spurious(OPENPIC_VEC_SPURIOUS);
	    
	    if (request_irq(IRQ_8259_CASCADE, no_action, SA_INTERRUPT,
			    "82c59 cascade", NULL))
		    printk("Unable to get OpenPIC IRQ 0 for cascade\n");
	    openpic_set_priority(0, 0);
	    openpic_disable_8259_pass_through();
    }
}


    /*
     *  Reset the OpenPIC
     */

void openpic_reset(void)
{
    openpic_setfield(&OpenPIC->Global.Global_Configuration0,
    		       OPENPIC_CONFIG_RESET);
}


    /*
     *  Enable/disable 8259 Pass Through Mode
     */

void openpic_enable_8259_pass_through(void)
{
    openpic_clearfield(&OpenPIC->Global.Global_Configuration0,
    		       OPENPIC_CONFIG_8259_PASSTHROUGH_DISABLE);
}

void openpic_disable_8259_pass_through(void)
{
    openpic_setfield(&OpenPIC->Global.Global_Configuration0,
    		     OPENPIC_CONFIG_8259_PASSTHROUGH_DISABLE);
}


#ifndef __i386__
    /*
     *  Find out the current interrupt
     */

u_int openpic_irq(u_int cpu)
{
    u_int vec;

    check_arg_cpu(cpu);
    vec = openpic_readfield(&OpenPIC->THIS_CPU.Interrupt_Acknowledge,
			    OPENPIC_VECTOR_MASK);
#if 0
if (vec != 22 /* SCSI */)
printk("++openpic_irq: %d\n", vec);
#endif
    return vec;
}
#endif


    /*
     *  Signal end of interrupt (EOI) processing
     */

#ifndef __powerpc__
void openpic_eoi(void)
#else
void openpic_eoi(u_int cpu)
#endif
{
    check_arg_cpu(cpu);
    openpic_write(&OpenPIC->THIS_CPU.EOI, 0);
}


    /*
     *  Get/set the current task priority
     */

#ifndef __powerpc__
u_int openpic_get_priority(void)
#else
u_int openpic_get_priority(u_int cpu)
#endif
{
    CHECK_THIS_CPU;
    return openpic_readfield(&OpenPIC->THIS_CPU.Current_Task_Priority,
			     OPENPIC_CURRENT_TASK_PRIORITY_MASK);
}

#ifndef __powerpc__
void openpic_set_priority(u_int pri)
#else
void openpic_set_priority(u_int cpu, u_int pri)
#endif
{
    CHECK_THIS_CPU;
    check_arg_pri(pri);
    openpic_writefield(&OpenPIC->THIS_CPU.Current_Task_Priority,
    		       OPENPIC_CURRENT_TASK_PRIORITY_MASK, pri);
}

    /*
     *  Get/set the spurious vector
     */

u_int openpic_get_spurious(void)
{
    return openpic_readfield(&OpenPIC->Global.Spurious_Vector,
    			     OPENPIC_VECTOR_MASK);
}

void openpic_set_spurious(u_int vec)
{
    check_arg_vec(vec);
    openpic_writefield(&OpenPIC->Global.Spurious_Vector, OPENPIC_VECTOR_MASK,
    		       vec);
}


    /*
     *  Initialize one or more CPUs
     */

void openpic_init_processor(u_int cpumask)
{
    openpic_write(&OpenPIC->Global.Processor_Initialization, cpumask);
}


/* -------- Interprocessor Interrupts -------------------------------------- */


    /*
     *  Initialize an interprocessor interrupt (and disable it)
     *
     *  ipi: OpenPIC interprocessor interrupt number
     *  pri: interrupt source priority
     *  vec: the vector it will produce
     */

void openpic_initipi(u_int ipi, u_int pri, u_int vec)
{
    check_arg_timer(ipi);
    check_arg_pri(pri);
    check_arg_vec(vec);
    openpic_safe_writefield(&OpenPIC->Global.IPI_Vector_Priority(ipi),
    			    OPENPIC_PRIORITY_MASK | OPENPIC_VECTOR_MASK,
    			    (pri << OPENPIC_PRIORITY_SHIFT) | vec);
}


    /*
     *  Send an IPI to one or more CPUs
     */

#ifndef __powerpc__
void openpic_cause_IPI(u_int ipi, u_int cpumask)
#else
void openpic_cause_IPI(u_int cpu, u_int ipi, u_int cpumask)
#endif
{
    CHECK_THIS_CPU;
    check_arg_ipi(ipi);
    openpic_write(&OpenPIC->THIS_CPU.IPI_Dispatch(ipi), cpumask);
}


/* -------- Timer Interrupts ----------------------------------------------- */


    /*
     *  Initialize a timer interrupt (and disable it)
     *
     *  timer: OpenPIC timer number
     *  pri: interrupt source priority
     *  vec: the vector it will produce
     */

void openpic_inittimer(u_int timer, u_int pri, u_int vec)
{
    check_arg_timer(timer);
    check_arg_pri(pri);
    check_arg_vec(vec);
    openpic_safe_writefield(&OpenPIC->Global.Timer[timer].Vector_Priority,
    			    OPENPIC_PRIORITY_MASK | OPENPIC_VECTOR_MASK,
    			    (pri << OPENPIC_PRIORITY_SHIFT) | vec);
}


    /*
     *  Map a timer interrupt to one or more CPUs
     */

void openpic_maptimer(u_int timer, u_int cpumask)
{
    check_arg_timer(timer);
    openpic_write(&OpenPIC->Global.Timer[timer].Destination, cpumask);
}


/* -------- Interrupt Sources ---------------------------------------------- */


    /*
     *  Enable/disable an interrupt source
     */

void openpic_enable_irq(u_int irq)
{
    check_arg_irq(irq);
    openpic_clearfield(&OpenPIC->Source[irq].Vector_Priority, OPENPIC_MASK);
}

void openpic_disable_irq(u_int irq)
{
    check_arg_irq(irq);
    openpic_setfield(&OpenPIC->Source[irq].Vector_Priority, OPENPIC_MASK);
}


    /*
     *  Initialize an interrupt source (and disable it!)
     *
     *  irq: OpenPIC interrupt number
     *  pri: interrupt source priority
     *  vec: the vector it will produce
     *  pol: polarity (1 for positive, 0 for negative)
     *  sense: 1 for level, 0 for edge
     */

void openpic_initirq(u_int irq, u_int pri, u_int vec, int pol, int sense)
{
    check_arg_irq(irq);
    check_arg_pri(pri);
    check_arg_vec(vec);
    openpic_safe_writefield(&OpenPIC->Source[irq].Vector_Priority,
    			    OPENPIC_PRIORITY_MASK | OPENPIC_VECTOR_MASK |
    			    OPENPIC_SENSE_POLARITY | OPENPIC_SENSE_LEVEL,
    			    (pri << OPENPIC_PRIORITY_SHIFT) | vec |
			    (pol ? OPENPIC_SENSE_POLARITY : 0) |
			    (sense ? OPENPIC_SENSE_LEVEL : 0));
}


    /*
     *  Map an interrupt source to one or more CPUs
     */

void openpic_mapirq(u_int irq, u_int cpumask)
{
    check_arg_irq(irq);
    openpic_write(&OpenPIC->Source[irq].Destination, cpumask);
}


    /*
     *  Set the sense for an interrupt source (and disable it!)
     *
     *  sense: 1 for level, 0 for edge
     */

void openpic_set_sense(u_int irq, int sense)
{
    check_arg_irq(irq);
    openpic_safe_writefield(&OpenPIC->Source[irq].Vector_Priority,
    			    OPENPIC_SENSE_LEVEL,
			    (sense ? OPENPIC_SENSE_LEVEL : 0));
}
