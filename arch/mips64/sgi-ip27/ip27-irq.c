/* $Id: ip27-irq.c,v 1.9 2000/03/14 01:39:27 ralf Exp $
 *
 * ip27-irq.c: Highlevel interrupt handling for IP27 architecture.
 *
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/init.h>

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/smp_lock.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>

#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/pci/bridge.h>
#include <asm/sn/sn0/hub.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/sn/arch.h>
#include <asm/sn/intr.h>
#include <asm/sn/intr_public.h>

#undef DEBUG_IRQ
#ifdef DEBUG_IRQ
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif


/*
 * Linux has a controller-independent x86 interrupt architecture.
 * every controller has a 'controller-template', that is used
 * by the main code to do the right thing. Each driver-visible
 * interrupt source is transparently wired to the apropriate
 * controller. Thus drivers need not be aware of the
 * interrupt-controller.
 *
 * Various interrupt controllers we handle: 8259 PIC, SMP IO-APIC,
 * PIIX4's internal 8259 PIC and SGI's Visual Workstation Cobalt (IO-)APIC.
 * (IO-APICs assumed to be messaging to Pentium local-APICs)
 *
 * the code is designed to be easily extended with new/different
 * interrupt controllers, without having to do assembly magic.
 */

irq_cpustat_t irq_stat [NR_CPUS];

extern asmlinkage void ip27_irq(void);
int (*irq_cannonicalize)(int irq);
int intr_connect_level(cpuid_t cpu, int bit);
int intr_disconnect_level(cpuid_t cpu, int bit);

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];
unsigned long spurious_count = 0;

/*
 * we need to map irq's up to at least bit 7 of the INT_MASK0_A register
 * since bits 0-6 are pre-allocated for other purposes.
 */
#define IRQ_TO_SWLEVEL(i)	i + 7
#define SWLEVEL_TO_IRQ(s)	s - 7
/*
 * use these macros to get the encoded nasid, widget id, and real irq
 * from the irq value
 */
#define NASID_FROM_IRQ(i)       ((i >> 16)&(0xff))
#define WID_FROM_IRQ(i)          ((i >> 8)&(0xff))
#define IRQ_FROM_IRQ(i)               ((i)&(0xff))

void disable_irq(unsigned int irq_nr)
{
	panic("disable_irq() called ...");
}

void enable_irq(unsigned int irq_nr)
{
	panic("enable_irq() called ...");
}

/* This is stupid for an Origin which can have thousands of IRQs ...  */
static struct irqaction *irq_action[NR_IRQS];

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action;

	for (i = 0 ; i < 32 ; i++) {
		action = irq_action[i];
		if (!action) 
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s", i, kstat.irqs[0][i],
		               (action->flags & SA_INTERRUPT) ? '+' : ' ',
		               action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
			               (action->flags & SA_INTERRUPT)
			                ? " +" : "",
			                action->name);
		}
		len += sprintf(buf+len, "\n");
	}
	return len;
}

/*
 * do_IRQ handles all normal device IRQ's (the special SMP cross-CPU interrupts
 * have their own specific handlers).
 */
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqaction *action;
	int do_random, cpu;

	cpu = smp_processor_id();
	irq_enter(cpu, irq);
	kstat.irqs[cpu][irq]++;

	action = *(irq + irq_action);
	if (action) {
		if (!(action->flags & SA_INTERRUPT))
			__sti();
		action = *(irq + irq_action);
		do_random = 0;
        	do {
			do_random |= action->flags;
			action->handler(irq, action->dev_id, regs);
			action = action->next;
        	} while (action);
		if (do_random & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		__cli();
	}
	irq_exit(cpu, irq);

	/* unmasking and bottom half handling is done magically for us. */
}

/*
 * Find first bit set
 */
static int ms1bit(unsigned long x)
{
	int	b;

	if (x >> 32) 	b = 32, x >>= 32;
	else		b  =  0;
	if (x >> 16)	b += 16, x >>= 16;
	if (x >>  8)	b +=  8, x >>=  8;
	if (x >>  4)	b +=  4, x >>=  4;
	if (x >>  2)	b +=  2, x >>=  2;

	return b + (int) (x >> 1);
}
	
/* For now ...  */
void ip27_do_irq(struct pt_regs *regs)
{
	int irq, swlevel;
	hubreg_t pend0, mask0;
	int pi_int_mask0 = ((cputoslice(smp_processor_id()) == 0) ?
					PI_INT_MASK0_A : PI_INT_MASK0_B);

	/* copied from Irix intpend0() */
	while (((pend0 = LOCAL_HUB_L(PI_INT_PEND0)) & 
				(mask0 = LOCAL_HUB_L(pi_int_mask0))) != 0) {
		pend0 &= mask0;
		do {
			swlevel = ms1bit(pend0);
			LOCAL_HUB_S(pi_int_mask0, mask0 & ~(1 << swlevel));
			LOCAL_HUB_CLR_INTR(swlevel);
			/* "map" swlevel to irq */
			irq = SWLEVEL_TO_IRQ(swlevel);
			do_IRQ(irq, regs);
			/* reset INT_MASK0 register */
			LOCAL_HUB_S(pi_int_mask0, mask0);
			/* clear bit in pend0 */
			pend0 ^= 1ULL << swlevel;
		} while (pend0);
	}
}


/* Startup one of the (PCI ...) IRQs routes over a bridge.  */
static unsigned int bridge_startup(unsigned int irq)
{
        bridge_t *bridge;
        int pin, swlevel;
        int real_irq = IRQ_FROM_IRQ(irq);

	DBG("bridge_startup(): irq= 0x%x  real_irq= %d\n", irq, real_irq);
        bridge = (bridge_t *) NODE_SWIN_BASE(NASID_FROM_IRQ(irq), WID_FROM_IRQ(irq));

        /* FIIIIIXME ...  Temporary kludge.  This knows how interrupts are
           setup in _my_ Origin.  */

        if (irq != real_irq)            /* pci device interrupt */
                switch (real_irq) {
                        case IRQ_FROM_IRQ(IOC3_ETH_INT):        pin = 2; break;
			default:		  pin = real_irq; break;
                }
        else
                switch (real_irq) {
                        case CPU_RESCHED_A_IRQ:
                        case CPU_RESCHED_B_IRQ:
                        case CPU_CALL_A_IRQ:
                        case CPU_CALL_B_IRQ:
                                                return 0;
                        default:                panic("bridge_startup: whoops? %d\n", irq);
                }

        /*
         * "map" irq to a swlevel greater than 6 since the first 6 bits
         * of INT_PEND0 are taken
         */
        swlevel = IRQ_TO_SWLEVEL(real_irq);
        intr_connect_level(smp_processor_id(), swlevel);

        bridge->b_int_addr[pin].addr = 0x20000 | swlevel;
        bridge->b_int_enable |= (1 << pin);
	/* set more stuff in int_enable reg */
	bridge->b_int_enable |= 0x7ffffe00;

        if (real_irq < 2 || real_irq==4 || real_irq==5) {
                bridgereg_t device;
#if 0
                /*
                 * Allocate enough RRBs on the bridge for the DMAs.
                 * Right now allocating 2 RRBs on the normal channel
                 * and 2 on the virtual channel for slot 0 on the bus.
                 * And same for slot 1, to get ioc3 eth working.
                 */
                Not touching b_even_resp          /* boot doesn't go far */
                bridge->b_even_resp = 0xdd99cc88; /* boot doesn't go far */
                bridge->b_even_resp = 0xcccc8888; /* breaks eth0 */
                bridge->b_even_resp = 0xcc88;     /* breaks eth0 */
#endif
                /* Turn on bridge swapping */
                device = bridge->b_device[real_irq].reg;
                device |= BRIDGE_DEV_SWAP_DIR;
                bridge->b_device[real_irq].reg = device;
                /*
                 * Associate interrupt pin with device
                 * XXX This only works if b_int_device is initialized to 0!
                 */
                device = bridge->b_int_device;
                device |= (pin << (real_irq*3));
                bridge->b_int_device = device;
        }
        bridge->b_widget.w_tflush;                      /* Flush */

        return 0;       /* Never anything pending.  */
}

/* Shutdown one of the (PCI ...) IRQs routes over a bridge.  */
static unsigned int bridge_shutdown(unsigned int irq)
{
        bridge_t *bridge;
        int pin, swlevel;
        int real_irq = IRQ_FROM_IRQ(irq);
        struct irqaction **p;

        bridge = (bridge_t *) NODE_SWIN_BASE(NASID_FROM_IRQ(irq), WID_FROM_IRQ(irq));
	DBG("bridge_shutdown: irq 0x%x\n", irq);
        /* FIIIIIXME ...  Temporary kludge.  This knows how interrupts are
           setup in _my_ Origin.  */

        if (irq != real_irq)            /* pci device interrupt */
                switch (real_irq) {
                        case IRQ_FROM_IRQ(IOC3_ETH_INT):        pin = 2; break;
			default:		  pin = real_irq; break;
                }
        else
                switch (real_irq) {
                        case CPU_RESCHED_A_IRQ:
                        case CPU_RESCHED_B_IRQ:
                        case CPU_CALL_A_IRQ:
                        case CPU_CALL_B_IRQ:
                                                return 0;
                        default:                panic("bridge_startup: whoops?");
                }

        /*
         * map irq to a swlevel greater than 6 since the first 6 bits
         * of INT_PEND0 are taken
         */
        swlevel = IRQ_TO_SWLEVEL(real_irq);
        intr_disconnect_level(smp_processor_id(), swlevel);

        bridge->b_int_enable &= ~(1 << pin);
        bridge->b_widget.w_tflush;                      /* Flush */

        return 0;       /* Never anything pending.  */
}

static void bridge_init(void)
{
	bridge_t *bridge;
	nasid_t   nasid;
        char    wid;
        int     bus;

        nasid = get_nasid();

        for (bus=0; bus<num_bridges; bus++) {
          bridge = (bridge_t *) NODE_SWIN_BASE(bus_to_nid[bus],bus_to_wid[bus]);          
          /* Hmm...  IRIX sets additional bits in the address which are
             documented as reserved in the bridge docs ...  */
          bridge->b_int_mode = 0x0;                     /* Don't clear ints */
#if 0
          bridge->b_wid_int_upper = 0x000a8000;           /* Ints to node 0 */
          bridge->b_wid_int_lower = 0x01000090;
          bridge->b_dir_map = 0xa00000;                 /* DMA */
#endif /* shouldn't lower= 0x01800090 ??? */
          bridge->b_wid_int_upper = 0x000a8000;           /* Ints to widget A */
          bridge->b_wid_int_lower = 0x01800090;
          bridge->b_dir_map = 0xa00000;                   /* DMA */

          bridge->b_int_enable = 0;
          bridge->b_widget.w_tflush;                    /* Flush */
        }
}

void irq_debug(void)
{
	bridge_t *bridge = (bridge_t *) 0x9200000008000000;

	printk("bridge->b_int_status = 0x%x\n", bridge->b_int_status);
	printk("bridge->b_int_enable = 0x%x\n", bridge->b_int_enable);
	printk("PI_INT_PEND0   = 0x%x\n", LOCAL_HUB_L(PI_INT_PEND0));
	printk("PI_INT_MASK0_A = 0x%x\n", LOCAL_HUB_L(PI_INT_MASK0_A));
}

int setup_irq(unsigned int irq, struct irqaction *new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	DBG("setup_irq: 0x%x\n", irq);
	if (new->flags & SA_SAMPLE_RANDOM)
		rand_initialize_irq(irq);

	save_and_cli(flags);
	p = irq_action + IRQ_FROM_IRQ(irq);
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			restore_flags(flags);
			return -EBUSY;
		}

		/* Add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	*p = new;

	if (!shared) {
		bridge_startup(irq);
	}
	restore_flags(flags);

	return 0;
}

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char * devname, void *dev_id)
{
	int retval;
	struct irqaction *action;

	DBG("request_irq(): irq= 0x%x\n", irq);
	if (IRQ_FROM_IRQ(irq) > 9)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(*action), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	DBG("request_irq(): %s  devid= 0x%x\n", devname, dev_id);
	retval = setup_irq(irq, action);
	DBG("request_irq(): retval= %d\n", retval);
	if (retval)
		kfree(action);
	return retval;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (IRQ_FROM_IRQ(irq) > 9) {
		printk("Trying to free IRQ%d\n", irq);
		return;
	}
	for (p = IRQ_FROM_IRQ(irq) + irq_action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		save_and_cli(flags);
		*p = action->next;
		if (!irq[irq_action])
			bridge_shutdown(irq);
		restore_flags(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

/* Useless ISA nonsense.  */
unsigned long probe_irq_on (void)
{
	panic("probe_irq_on called!\n");
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

static int indy_irq_cannonicalize(int irq)
{
	return irq;	/* Sane hardware, sane code ... */
}

void __init init_IRQ(void)
{
	irq_cannonicalize = indy_irq_cannonicalize;

	bridge_init();
	set_except_vector(0, ip27_irq);
}


#ifdef CONFIG_SMP

/*
 * This following are the global intr on off routines, copied almost
 * entirely from i386 code.
 */

int global_irq_holder = NO_PROC_ID;
spinlock_t global_irq_lock = SPIN_LOCK_UNLOCKED;

extern void show_stack(unsigned long* esp);

static void show(char * str)
{
	int i;
	int cpu = smp_processor_id();

	printk("\n%s, CPU %d:\n", str, cpu);
	printk("irq:  %d [",irqs_running());
	for(i=0;i < smp_num_cpus;i++)
		printk(" %d",local_irq_count(i));
	printk(" ]\nbh:   %d [",spin_is_locked(&global_bh_lock) ? 1 : 0);
	for(i=0;i < smp_num_cpus;i++)
		printk(" %d",local_bh_count(i));

	printk(" ]\nStack dumps:");
	for(i = 0; i < smp_num_cpus; i++) {
		unsigned long esp;
		if (i == cpu)
			continue;
		printk("\nCPU %d:",i);
		printk("Code not developed yet\n");
		/* show_stack(0); */
	}
	printk("\nCPU %d:",cpu);
	printk("Code not developed yet\n");
	/* show_stack(NULL); */
	printk("\n");
}

#define MAXCOUNT 		100000000
#define SYNC_OTHER_CORES(x)	udelay(x+1)

static inline void wait_on_irq(int cpu)
{
	int count = MAXCOUNT;

	for (;;) {

		/*
		 * Wait until all interrupts are gone. Wait
		 * for bottom half handlers unless we're
		 * already executing in one..
		 */
		if (!irqs_running())
			if (local_bh_count(cpu) || !spin_is_locked(&global_bh_lock))
				break;

		/* Duh, we have to loop. Release the lock to avoid deadlocks */
		spin_unlock(&global_irq_lock);

		for (;;) {
			if (!--count) {
				show("wait_on_irq");
				count = ~0;
			}
			__sti();
			SYNC_OTHER_CORES(cpu);
			__cli();
			if (irqs_running())
				continue;
			if (spin_is_locked(&global_irq_lock))
				continue;
			if (!local_bh_count(cpu) && spin_is_locked(&global_bh_lock))
				continue;
			if (spin_trylock(&global_irq_lock))
				break;
		}
	}
}

void synchronize_irq(void)
{
	if (irqs_running()) {
		/* Stupid approach */
		cli();
		sti();
	}
}

static inline void get_irqlock(int cpu)
{
	if (!spin_trylock(&global_irq_lock)) {
		/* do we already hold the lock? */
		if ((unsigned char) cpu == global_irq_holder)
			return;
		/* Uhhuh.. Somebody else got it. Wait.. */
		spin_lock(&global_irq_lock);
	}
	/*
	 * We also to make sure that nobody else is running
	 * in an interrupt context.
	 */
	wait_on_irq(cpu);

	/*
	 * Ok, finally..
	 */
	global_irq_holder = cpu;
}

void __global_cli(void)
{
	unsigned int flags;

	__save_flags(flags);
	if (flags & ST0_IE) {
		int cpu = smp_processor_id();
		__cli();
		if (!local_irq_count(cpu))
			get_irqlock(cpu);
	}
}

void __global_sti(void)
{
	int cpu = smp_processor_id();

	if (!local_irq_count(cpu))
		release_irqlock(cpu);
	__sti();
}

/*
 * SMP flags value to restore to:
 * 0 - global cli
 * 1 - global sti
 * 2 - local cli
 * 3 - local sti
 */
unsigned long __global_save_flags(void)
{
	int retval;
	int local_enabled;
	unsigned long flags;
	int cpu = smp_processor_id();

	__save_flags(flags);
	local_enabled = (flags & ST0_IE);
	/* default to local */
	retval = 2 + local_enabled;

	/* check for global flags if we're not in an interrupt */
	if (!local_irq_count(cpu)) {
		if (local_enabled)
			retval = 1;
		if (global_irq_holder == cpu)
			retval = 0;
	}
	return retval;
}

void __global_restore_flags(unsigned long flags)
{
	switch (flags) {
		case 0:
			__global_cli();
			break;
		case 1:
			__global_sti();
			break;
		case 2:
			__cli();
			break;
		case 3:
			__sti();
			break;
		default:
			printk("global_restore_flags: %08lx\n", flags);
	}
}

#endif /* CONFIG_SMP */

/*
 * Get values that vary depending on which CPU and bit we're operating on.
 */
static hub_intmasks_t *intr_get_ptrs(cpuid_t cpu, int bit, int *new_bit,
				hubreg_t **intpend_masks, int *ip)
{
	hub_intmasks_t *hub_intmasks;

	hub_intmasks = &cpu_data[cpu].p_intmasks;
	if (bit < N_INTPEND_BITS) {
		*intpend_masks = hub_intmasks->intpend0_masks;
		*ip = 0;
		*new_bit = bit;
	} else {
		*intpend_masks = hub_intmasks->intpend1_masks;
		*ip = 1;
		*new_bit = bit - N_INTPEND_BITS;
	}
	return hub_intmasks;
}

int intr_connect_level(cpuid_t cpu, int bit)
{
	int ip;
	int slice = cputoslice(cpu);
	volatile hubreg_t *mask_reg;
	hubreg_t *intpend_masks;
	nasid_t nasid = COMPACT_TO_NASID_NODEID(cputocnode(cpu));

	(void)intr_get_ptrs(cpu, bit, &bit, &intpend_masks, &ip);

	/* Make sure it's not already pending when we connect it. */
	REMOTE_HUB_CLR_INTR(nasid, bit + ip * N_INTPEND_BITS);

	intpend_masks[0] |= (1ULL << (u64)bit);

	if (ip == 0) {
		mask_reg = REMOTE_HUB_ADDR(nasid, PI_INT_MASK0_A + 
				PI_INT_MASK_OFFSET * slice);
	} else {
		mask_reg = REMOTE_HUB_ADDR(nasid, PI_INT_MASK1_A + 
				PI_INT_MASK_OFFSET * slice);
	}
	HUB_S(mask_reg, intpend_masks[0]);
	return(0);
}

int intr_disconnect_level(cpuid_t cpu, int bit)
{
	int ip;
	int slice = cputoslice(cpu);
	volatile hubreg_t *mask_reg;
	hubreg_t *intpend_masks;
	nasid_t nasid = COMPACT_TO_NASID_NODEID(cputocnode(cpu));

	(void)intr_get_ptrs(cpu, bit, &bit, &intpend_masks, &ip);
	intpend_masks[0] &= ~(1ULL << (u64)bit);
	if (ip == 0) {
		mask_reg = REMOTE_HUB_ADDR(nasid, PI_INT_MASK0_A + 
				PI_INT_MASK_OFFSET * slice);
	} else {
		mask_reg = REMOTE_HUB_ADDR(nasid, PI_INT_MASK1_A + 
				PI_INT_MASK_OFFSET * slice);
	}
	HUB_S(mask_reg, intpend_masks[0]);
	return(0);
}


void handle_resched_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	/* Nothing, the return from intr will work for us */
}

void install_cpuintr(cpuid_t cpu)
{
	int irq;
	extern void smp_call_function_interrupt(void);
	static int done = 0;

	/*
	 * This is a hack till we have a pernode irqlist. Currently,
	 * just have the master cpu set up the handlers for the per
	 * cpu irqs.
	 */

#ifdef CONFIG_SMP
#if (CPUS_PER_NODE == 2)
	irq = CPU_RESCHED_A_IRQ + cputoslice(cpu);
	intr_connect_level(cpu, IRQ_TO_SWLEVEL(irq));
	if (done == 0)
	if (request_irq(irq, handle_resched_intr, 0, "resched", 0))
		panic("intercpu intr unconnectible\n");
	irq = CPU_CALL_A_IRQ + cputoslice(cpu);
	intr_connect_level(cpu, IRQ_TO_SWLEVEL(irq));
	if (done == 0)
	if (request_irq(irq, smp_call_function_interrupt, 0,
						"callfunc", 0))
		panic("intercpu intr unconnectible\n");
	/* HACK STARTS */
	if (done)
		return;
	irq = CPU_RESCHED_A_IRQ + cputoslice(cpu) + 1;
	if (request_irq(irq, handle_resched_intr, 0, "resched", 0))
		panic("intercpu intr unconnectible\n");
	irq = CPU_CALL_A_IRQ + cputoslice(cpu) + 1;
	if (request_irq(irq, smp_call_function_interrupt, 0,
						"callfunc", 0))
		panic("intercpu intr unconnectible\n");
	done = 1;
	/* HACK ENDS */
#else /* CPUS_PER_NODE */
	<< Bomb!  Must redefine this for more than 2 CPUS. >>
#endif /* CPUS_PER_NODE */
#endif /* CONFIG_SMP */
}

void install_tlbintr(cpuid_t cpu)
{
	int intr_bit = N_INTPEND_BITS + TLB_INTR_A + cputoslice(cpu);

	intr_connect_level(cpu, intr_bit);
}
