/*
 * SMP IRQ Lock support
 *
 * Global interrupt locks for SMP. Allow interrupts to come in on any
 * CPU, yet make cli/sti act globally to protect critical regions..
 * These function usually appear in irq.c, but I think it's cleaner this way.
 * 
 * Copyright (C) 1999 VA Linux Systems 
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 */ 

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/delay.h>

int global_irq_holder = NO_PROC_ID;
spinlock_t global_irq_lock;
atomic_t global_irq_count;
atomic_t global_bh_count;
spinlock_t global_bh_lock;

#define INIT_STUCK (1<<26)

void
irq_enter(int cpu, int irq)
{
        int stuck = INIT_STUCK;

        hardirq_enter(cpu, irq);
        barrier();
        while (global_irq_lock.lock) {
                if (cpu == global_irq_holder) {
			break;
                }

		if (!--stuck) {
			printk("irq_enter stuck (irq=%d, cpu=%d, global=%d)\n",
			       irq, cpu,global_irq_holder);
			stuck = INIT_STUCK;
		}
                barrier();
        }
}

void
irq_exit(int cpu, int irq)
{
        hardirq_exit(cpu, irq);
        release_irqlock(cpu);
}

static void
show(char * str)
{
        int i;
        unsigned long *stack;
        int cpu = smp_processor_id();

        printk("\n%s, CPU %d:\n", str, cpu);
        printk("irq:  %d [%d %d]\n",
                atomic_read(&global_irq_count), local_irq_count[0], local_irq_count[1]);
        printk("bh:   %d [%d %d]\n",
                atomic_read(&global_bh_count), local_bh_count[0], local_bh_count[1]);

        stack = (unsigned long *) &stack;
        for (i = 40; i ; i--) {
                unsigned long x = *++stack;
                if (x > (unsigned long) &get_options && x < (unsigned long) &vsprintf) {
                        printk("<[%08lx]> ", x);
                }
        }
}
        
#define MAXCOUNT 100000000

static inline void 
wait_on_bh(void)
{
        int count = MAXCOUNT;
        do {
                if (!--count) {
                        show("wait_on_bh");
                        count = ~0;
                }
                /* nothing .. wait for the other bh's to go away */
        } while (atomic_read(&global_bh_count) != 0);
}

static inline void 
wait_on_irq(int cpu)
{
        int count = MAXCOUNT;

        for (;;) {

                /*
                 * Wait until all interrupts are gone. Wait
                 * for bottom half handlers unless we're
                 * already executing in one..
                 */
                if (!atomic_read(&global_irq_count)) {
                        if (local_bh_count[cpu] || !atomic_read(&global_bh_count))
                                break;
                }

                /* Duh, we have to loop. Release the lock to avoid deadlocks */
		spin_unlock(&global_irq_lock);
		mb();

                for (;;) {
                        if (!--count) {
                                show("wait_on_irq");
                                count = ~0;
                        }
                        __sti();
                        udelay(cpu + 1);
                        __cli();
                        if (atomic_read(&global_irq_count))
                                continue;
                        if (global_irq_lock.lock)
                                continue;
                        if (!local_bh_count[cpu] && atomic_read(&global_bh_count))
                                continue;
                        if (spin_trylock(&global_irq_lock))
                                break;
                }
        }
}

/*
 * This is called when we want to synchronize with
 * bottom half handlers. We need to wait until
 * no other CPU is executing any bottom half handler.
 *
 * Don't wait if we're already running in an interrupt
 * context or are inside a bh handler. 
 */
void 
synchronize_bh(void)
{
        if (atomic_read(&global_bh_count)) {
                int cpu = smp_processor_id();
                if (!local_irq_count[cpu] && !local_bh_count[cpu]) {
                        wait_on_bh();
                }
        }
}


/*
 * This is called when we want to synchronize with
 * interrupts. We may for example tell a device to
 * stop sending interrupts: but to make sure there
 * are no interrupts that are executing on another
 * CPU we need to call this function.
 */
void 
synchronize_irq(void)
{
        int cpu = smp_processor_id();
        int local_count;
        int global_count;

        mb();
        do {
                local_count = local_irq_count[cpu];
                global_count = atomic_read(&global_irq_count);
        } while (global_count != local_count);
}

static inline void 
get_irqlock(int cpu)
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

/*
 * A global "cli()" while in an interrupt context
 * turns into just a local cli(). Interrupts
 * should use spinlocks for the (very unlikely)
 * case that they ever want to protect against
 * each other.
 *
 * If we already have local interrupts disabled,
 * this will not turn a local disable into a
 * global one (problems with spinlocks: this makes
 * save_flags+cli+sti usable inside a spinlock).
 */
void 
__global_cli(void)
{
	unsigned long flags;

	__save_flags(flags);
	if (flags & IA64_PSR_I) {
		int cpu = smp_processor_id();
		__cli();
		if (!local_irq_count[cpu])
			get_irqlock(cpu);
	}
}

void 
__global_sti(void)
{
        int cpu = smp_processor_id();

        if (!local_irq_count[cpu])
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
unsigned long 
__global_save_flags(void)
{
        int retval;
        int local_enabled;
        unsigned long flags;

        __save_flags(flags);
        local_enabled = flags & IA64_PSR_I;
        /* default to local */
        retval = 2 + local_enabled;

        /* check for global flags if we're not in an interrupt */
        if (!local_irq_count[smp_processor_id()]) {
                if (local_enabled)
                        retval = 1;
                if (global_irq_holder == (unsigned char) smp_processor_id())
                        retval = 0;
        }
        return retval;
}

void 
__global_restore_flags(unsigned long flags)
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
                printk("global_restore_flags: %08lx (%08lx)\n",
                        flags, (&flags)[-1]);
        }
}
