/*
 * linux/arch/ia64/kernel/irq.c
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1999-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 *  6/10/99: Updated to bring in sync with x86 version to facilitate
 *	     support for SMP and different interrupt controllers.
 */

#include <linux/config.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel_stat.h>
#include <linux/malloc.h>
#include <linux/ptrace.h>
#include <linux/random.h>	/* for rand_initialize_irq() */
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/threads.h>

#ifdef CONFIG_KDB
# include <linux/kdb.h>
#endif

#include <asm/bitops.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/machvec.h>
#include <asm/pgtable.h>
#include <asm/system.h>

/* This is used to detect bad usage of probe_irq_on()/probe_irq_off().  */
#define PROBE_IRQ_COOKIE	0xfeedC0FFEE

struct irq_desc irq_desc[NR_IRQS];

/*
 * Micro-access to controllers is serialized over the whole
 * system. We never hold this lock when we call the actual
 * IRQ handler.
 */
spinlock_t irq_controller_lock;

#ifdef CONFIG_ITANIUM_ASTEP_SPECIFIC
spinlock_t ivr_read_lock;
#endif

unsigned int local_bh_count[NR_CPUS];
/*
 * used in irq_enter()/irq_exit()
 */
unsigned int local_irq_count[NR_CPUS];

static struct irqaction timer_action = { NULL, 0, 0, NULL, NULL, NULL};

#ifdef CONFIG_SMP
static struct irqaction ipi_action = { NULL, 0, 0, NULL, NULL, NULL};
#endif

/*
 * Legacy IRQ to IA-64 vector translation table.  Any vector not in
 * this table maps to itself (ie: irq 0x30 => IA64 vector 0x30)
 */
__u8 irq_to_vector_map[IA64_MIN_VECTORED_IRQ] = {
	/* 8259 IRQ translation, first 16 entries */
	TIMER_IRQ, 0x50, 0x0f, 0x51, 0x52, 0x53, 0x43, 0x54,
	0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x40, 0x41,
};

/*
 * Reverse of the above table.
 */
static __u8 vector_to_legacy_map[256];

/*
 * used by proc fs (/proc/interrupts)
 */
int
get_irq_list (char *buf)
{
	int i;
	struct irqaction * action;
	char *p = buf;

#ifdef CONFIG_SMP
	p += sprintf(p, "           ");
	for (i = 0; i < smp_num_cpus; i++)
		p += sprintf(p, "CPU%d       ", i);
	*p++ = '\n';
#endif
	/*
	 * Simply scans the external vectored interrupts
	 */
	for (i = 0; i < NR_IRQS; i++) {
		action = irq_desc[i].action;
		if (!action) 
			continue;
		p += sprintf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		p += sprintf(p, "%10u ", kstat_irqs(i));
#else
		{
			int j;
			for (j = 0; j < smp_num_cpus; j++)
				p += sprintf(p, "%10u ",
					     kstat.irqs[cpu_logical_map(j)][i]);
		}
#endif
		p += sprintf(p, " %14s", irq_desc[i].handler->typename);
		p += sprintf(p, "  %c%s", (action->flags & SA_INTERRUPT) ? '+' : ' ',
			     action->name);

		for (action = action->next; action; action = action->next) {
			p += sprintf(p, ", %c%s",
				     (action->flags & SA_INTERRUPT)?'+':' ',
				     action->name);
		}
		*p++ = '\n';
	}
	return p - buf;
}

int usbfix;

static int __init
usbfix_option (char *str)
{
	printk("irq: enabling USB workaround\n");
	usbfix = 1;
	return 1;
}

__setup("usbfix", usbfix_option);

/*
 * That's where the IVT branches when we get an external
 * interrupt. This branches to the correct hardware IRQ handler via
 * function ptr.
 */
void
ia64_handle_irq (unsigned long irq, struct pt_regs *regs)
{
	unsigned long bsp, sp, saved_tpr;

#ifdef CONFIG_ITANIUM_ASTEP_SPECIFIC
# ifndef CONFIG_SMP
	static unsigned int max_prio = 0;
# endif
	unsigned int prev_prio;
	unsigned long eoi_ptr;
 
# ifdef CONFIG_USB
	if (usbfix)
		disable_usb();
# endif
	/*
	 * Stop IPIs by getting the ivr_read_lock
	 */
	spin_lock(&ivr_read_lock);

	/*
	 * Disable PCI writes
	 */
	outl(0x80ff81c0, 0xcf8);
	outl(0x73002188, 0xcfc);
	eoi_ptr = inl(0xcfc);

	irq = ia64_get_ivr();

	/*
	 * Enable PCI writes
	 */
	outl(0x73182188, 0xcfc);

	spin_unlock(&ivr_read_lock);

# ifdef CONFIG_USB
	if (usbfix)
		reenable_usb();
# endif

# ifndef CONFIG_SMP
	prev_prio = max_prio;
	if (irq < max_prio) {
		printk ("ia64_handle_irq: got irq %lu while %u was in progress!\n",
			irq, max_prio);
		
	} else
		max_prio = irq;
# endif /* !CONFIG_SMP */
#endif /* CONFIG_ITANIUM_ASTEP_SPECIFIC */

	/* Always set TPR to limit maximum interrupt nesting depth to
	 * 16 (without this, it would be ~240, which could easily lead
	 * to kernel stack overflows.
	 */
	saved_tpr = ia64_get_tpr();
	ia64_srlz_d();
	ia64_set_tpr(irq);
	ia64_srlz_d();

	asm ("mov %0=ar.bsp" : "=r"(bsp));
	asm ("mov %0=sp" : "=r"(sp));

	if ((sp - bsp) < 1024) {
		static long last_time;
		static unsigned char count;

		if (count > 5 && jiffies - last_time > 5*HZ)
			count = 0;
		if (++count < 5) {
			last_time = jiffies;
			printk("ia64_handle_irq: DANGER: less than 1KB of free stack space!!\n"
			       "(bsp=0x%lx, sp=%lx)\n", bsp, sp);
		}
#ifdef CONFIG_KDB
		kdb(KDB_REASON_PANIC, 0, regs);
#endif		
	}

	/*
	 * The interrupt is now said to be in service
	 */
	if (irq >= NR_IRQS) {
		printk("handle_irq: invalid irq=%lu\n", irq);
		goto out;
	}

	++kstat.irqs[smp_processor_id()][irq];

	if (irq == IA64_SPURIOUS_INT) {
		printk("handle_irq: spurious interrupt\n");
		goto out;
	}

	/* 
	 * Handle the interrupt by calling the hardware specific handler (IOSAPIC, Internal, etc).
	 */
	(*irq_desc[irq].handler->handle)(irq, regs);
  out:
#ifdef CONFIG_ITANIUM_ASTEP_SPECIFIC
	{
		long pEOI;

		asm ("mov %0=0;; (p1) mov %0=1" : "=r"(pEOI));
		if (!pEOI) {
			printk("Yikes: ia64_handle_irq() without pEOI!!\n");
			asm volatile ("cmp.eq p1,p0=r0,r0" : "=r"(pEOI));
# ifdef CONFIG_KDB
			kdb(KDB_REASON_PANIC, 0, regs);
# endif
		}
	}

	local_irq_disable();
# ifndef CONFIG_SMP
	if (max_prio == irq)
		max_prio = prev_prio;
# endif /* !CONFIG_SMP */
#endif /* CONFIG_ITANIUM_ASTEP_SPECIFIC */

	ia64_srlz_d();
	ia64_set_tpr(saved_tpr);
	ia64_srlz_d();
}


/*
 * This should really return information about whether we should do
 * bottom half handling etc. Right now we end up _always_ checking the
 * bottom half, which is a waste of time and is not what some drivers
 * would prefer.
 */
int
invoke_irq_handlers (unsigned int irq, struct pt_regs *regs, struct irqaction *action)
{
	void (*handler)(int, void *, struct pt_regs *);
	unsigned long flags, flags_union = 0;
	int cpu = smp_processor_id();
	unsigned int requested_irq;
	void *dev_id;

	irq_enter(cpu, irq);

	if ((action->flags & SA_INTERRUPT) == 0)
		__sti();

	do {
		flags = action->flags;
		requested_irq = irq;
		if ((flags & SA_LEGACY) != 0)
			requested_irq = vector_to_legacy_map[irq];
		flags_union |= flags;
		handler = action->handler;
		dev_id = action->dev_id;
		action = action->next;
		(*handler)(requested_irq, dev_id, regs);
	} while (action);
	if ((flags_union & SA_SAMPLE_RANDOM) != 0)
		add_interrupt_randomness(irq);
	__cli();

	irq_exit(cpu, irq);
	return flags_union | 1;	/* force the "do bottom halves" bit */
}

void
disable_irq_nosync (unsigned int irq)
{
	unsigned long flags;

	irq = map_legacy_irq(irq);

	spin_lock_irqsave(&irq_controller_lock, flags);
	if (irq_desc[irq].depth++ > 0) {
		irq_desc[irq].status &= ~IRQ_ENABLED;
		irq_desc[irq].handler->disable(irq);
	}
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

/*
 * Synchronous version of the above, making sure the IRQ is
 * no longer running on any other IRQ..
 */
void
disable_irq (unsigned int irq)
{
	disable_irq_nosync(irq);

	irq = map_legacy_irq(irq);

	if (!local_irq_count[smp_processor_id()]) {
		do {
			barrier();
		} while ((irq_desc[irq].status & IRQ_INPROGRESS) != 0);
	}
}

void
enable_irq (unsigned int irq)
{
	unsigned long flags;

	irq = map_legacy_irq(irq);

	spin_lock_irqsave(&irq_controller_lock, flags);
	switch (irq_desc[irq].depth) {
	      case 1:
		irq_desc[irq].status |= IRQ_ENABLED;
		(*irq_desc[irq].handler->enable)(irq);
		/* fall through */
	      default:
		--irq_desc[irq].depth;
		break;

	      case 0:
		printk("enable_irq: unbalanced from %p\n", __builtin_return_address(0));
	}
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

/*
 * This function encapsulates the initialization that needs to be
 * performed under the protection of lock irq_controller_lock.  The
 * lock must have been acquired by the time this is called.
 */
static inline int
setup_irq (unsigned int irq, struct irqaction *new)
{
	int shared = 0;
	struct irqaction *old, **p;

	p = &irq_desc[irq].action;
	old = *p;
	if (old) {
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			return -EBUSY;
		}
		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}
	*p = new;

	/* when sharing do not unmask */
	if (!shared) {
		irq_desc[irq].depth = 0;
		irq_desc[irq].status |= IRQ_ENABLED;
		(*irq_desc[irq].handler->startup)(irq);
	}
	return 0;
}

int
request_irq (unsigned int requested_irq, void (*handler)(int, void *, struct pt_regs *),
	     unsigned long irqflags, const char * devname, void *dev_id)
{
	int retval, need_kfree = 0;
	struct irqaction *action;
	unsigned long flags;
	unsigned int irq;

#ifdef IA64_DEBUG
	printk("request_irq(0x%x) called\n", requested_irq);
#endif
	/*
	 * Sanity-check: shared interrupts should REALLY pass in
	 * a real dev-ID, otherwise we'll have trouble later trying
	 * to figure out which interrupt is which (messes up the
	 * interrupt freeing logic etc).
	 */
	if ((irqflags & SA_SHIRQ) && !dev_id)
		printk("Bad boy: %s (at %p) called us without a dev_id!\n",
		       devname, current_text_addr());

	irq = map_legacy_irq(requested_irq);
	if (irq != requested_irq)
		irqflags |= SA_LEGACY;

	if (irq >= NR_IRQS)
		return -EINVAL;

	if (!handler)
		return -EINVAL;

	/*
	 * The timer_action and ipi_action cannot be allocated
	 * dynamically because its initialization happens really early
	 * on in init/main.c at this point the memory allocator has
	 * not yet been initialized.  So we use a statically reserved
	 * buffer for it. In some sense that's no big deal because we
	 * need one no matter what.
	 */
	if (irq == TIMER_IRQ)
		action = &timer_action;
#ifdef CONFIG_SMP
	else if (irq == IPI_IRQ)
		action = &ipi_action;
#endif
	else {
		action = kmalloc(sizeof(struct irqaction), GFP_KERNEL);
		need_kfree = 1;
	}

	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	if ((irqflags & SA_SAMPLE_RANDOM) != 0)
		rand_initialize_irq(irq);

	spin_lock_irqsave(&irq_controller_lock, flags);
	retval = setup_irq(irq, action);
	spin_unlock_irqrestore(&irq_controller_lock, flags);

	if (need_kfree && retval)
		kfree(action);

	return retval;
}
		
void
free_irq (unsigned int irq, void *dev_id)
{
	struct irqaction *action, **p;
	unsigned long flags;

	/*
	 * some sanity checks first
	 */
	if (irq >= NR_IRQS) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}

	irq = map_legacy_irq(irq);

	/*
	 * Find the corresponding irqaction
	 */
	spin_lock_irqsave(&irq_controller_lock, flags);
	for (p = &irq_desc[irq].action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now remove it from the list of entries */
		*p = action->next;
		if (!irq_desc[irq].action) {
			irq_desc[irq].status &= ~IRQ_ENABLED;
			(*irq_desc[irq].handler->shutdown)(irq);
		}

		spin_unlock_irqrestore(&irq_controller_lock, flags);

#ifdef CONFIG_SMP
		/* Wait to make sure it's not being used on another CPU */
		while (irq_desc[irq].status & IRQ_INPROGRESS)
			barrier();
#endif

		if (action != &timer_action
#ifdef CONFIG_SMP
		    && action != &ipi_action
#endif
		   )
			kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n", irq);
}

/*
 * IRQ autodetection code.  Note that the return value of
 * probe_irq_on() is no longer being used (it's role has been replaced
 * by the IRQ_AUTODETECT flag).
 */
unsigned long
probe_irq_on (void)
{
	struct irq_desc *id;
	unsigned long delay;

#ifdef IA64_DEBUG
	printk("probe_irq_on() called\n");
#endif

	spin_lock_irq(&irq_controller_lock);
	for (id = irq_desc; id < irq_desc + NR_IRQS; ++id) {
		if (!id->action) {
			id->status |= IRQ_AUTODETECT | IRQ_WAITING;
			(*id->handler->startup)(id - irq_desc);
		}
	}
	spin_unlock_irq(&irq_controller_lock);

	/* wait for spurious interrupts to trigger: */

	for (delay = jiffies + HZ/10; time_after(delay, jiffies); )
		/* about 100ms delay */
		synchronize_irq();

	/* filter out obviously spurious interrupts: */
	spin_lock_irq(&irq_controller_lock);
	for (id = irq_desc; id < irq_desc + NR_IRQS; ++id) {
		unsigned int status = id->status;

		if (!(status & IRQ_AUTODETECT))
			continue;

		if (!(status & IRQ_WAITING)) {
			id->status = status & ~IRQ_AUTODETECT;
			(*id->handler->shutdown)(id - irq_desc);
		}
	}
	spin_unlock_irq(&irq_controller_lock);
	return PROBE_IRQ_COOKIE;		/* return meaningless return value  */
}

int
probe_irq_off (unsigned long cookie)
{
	int irq_found, nr_irqs;
	struct irq_desc *id;

#ifdef IA64_DEBUG
	printk("probe_irq_off(cookie=0x%lx) -> ", cookie);
#endif

	if (cookie != PROBE_IRQ_COOKIE)
		printk("bad irq probe from %p\n", __builtin_return_address(0));

	nr_irqs = 0;
	irq_found = 0;
	spin_lock_irq(&irq_controller_lock);
	for (id = irq_desc + IA64_MIN_VECTORED_IRQ; id < irq_desc + NR_IRQS; ++id) {
		unsigned int status = id->status;

		if (!(status & IRQ_AUTODETECT))
			continue;

		if (!(status & IRQ_WAITING)) {
			if (!nr_irqs)
				irq_found = (id - irq_desc);
			++nr_irqs;
		}
		id->status = status & ~IRQ_AUTODETECT;
		(*id->handler->shutdown)(id - irq_desc);
	}
	spin_unlock_irq(&irq_controller_lock);

	if (nr_irqs > 1)
		irq_found = -irq_found;

#ifdef IA64_DEBUG
	printk("%d\n", irq_found);
#endif
	return irq_found;
}

#ifdef CONFIG_SMP

void __init
init_IRQ_SMP (void)
{
	if (request_irq(IPI_IRQ, handle_IPI, 0, "IPI", NULL))
		panic("Could not allocate IPI Interrupt Handler!");
}

#endif

void __init
init_IRQ (void)
{
	int i;

	for (i = 0; i < IA64_MIN_VECTORED_IRQ; ++i)
		vector_to_legacy_map[irq_to_vector_map[i]] = i;

	for (i = 0; i < NR_IRQS; ++i) {
		irq_desc[i].handler = &irq_type_default;
	}

	irq_desc[TIMER_IRQ].handler = &irq_type_ia64_internal;
#ifdef CONFIG_SMP
	/* 
	 * Configure the IPI vector and handler
	 */
	irq_desc[IPI_IRQ].handler = &irq_type_ia64_internal;
	init_IRQ_SMP();
#endif

	ia64_set_pmv(1 << 16);
	ia64_set_cmcv(CMC_IRQ);			/* XXX fix me */

	platform_irq_init(irq_desc);

	/* clear TPR to enable all interrupt classes: */
	ia64_set_tpr(0);
}

/* TBD:
 * 	Certain IA64 platforms can have inter-processor interrupt support.
 * 	This interface is supposed to default to the IA64 IPI block-based
 * 	mechanism if the platform doesn't provide a separate mechanism
 *	for IPIs.
 *	Choices : (1) Extend hw_interrupt_type interfaces 
 *		  (2) Use machine vector mechanism
 *	For now defining the following interface as a place holder.
 */
void
ipi_send (int cpu, int vector, int delivery_mode)
{
}
