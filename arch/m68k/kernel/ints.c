/*
 * ints.c -- 680x0 Linux general interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * 07/03/96: Timer initialization, and thus mach_sched_init(),
 *           removed from request_irq() and moved to init_time().
 *           We should therefore consider renaming our add_isr() and
 *           remove_isr() to request_irq() and free_irq()
 *           respectively, so they are compliant with the other
 *           architectures.                                     /Jes
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/page.h>
#include <asm/machdep.h>

/* list is accessed 0-6 for IRQs 1-7 */
static isr_node_t *isr_list[7];

/* The number of spurious interrupts */
volatile unsigned long num_spurious;
/*
unsigned long interrupt_stack[PAGE_SIZE/sizeof(long)];
*/

/*
 * void init_IRQ(void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function should be called during kernel startup to initialize
 * the IRQ handling routines.
 */

void init_IRQ(void)
{
    /* Setup interrupt stack pointer */
  /*
    asm ("movec %0,%/isp"
	 : : "r" (interrupt_stack + sizeof (interrupt_stack) / sizeof (long)));
	 */
    mach_init_INTS ();
}

void insert_isr (isr_node_t **listp, isr_node_t *node)
{
    unsigned long spl;
    isr_node_t *cur;

    save_flags(spl);
    cli();

    cur = *listp;

    while (cur && cur->pri <= node->pri)
    {
	listp = &cur->next;
	cur = cur->next;
    }

    node->next = cur;
    *listp = node;

    restore_flags(spl);
}

void delete_isr (isr_node_t **listp, isrfunc isr, void *data)
{
    unsigned long flags;
    isr_node_t *np;

    save_flags(flags);
    cli();
    for (np = *listp; np; listp = &np->next, np = *listp) {
	if (np->isr == isr && np->data == data) {
	    *listp = np->next;
	    /* Mark it as free. */
	    np->isr = NULL;
	    restore_flags(flags);
	    return;
	}
    }
    restore_flags(flags);
    printk ("delete_isr: isr %p not found on list!\n", isr);
}

#define NUM_ISR_NODES 100
static isr_node_t nodes[NUM_ISR_NODES];

isr_node_t *new_isr_node(void)
{
    isr_node_t *np;

    for (np = nodes; np < &nodes[NUM_ISR_NODES]; np++)
	if (np->isr == NULL)
	    return np;

    printk ("new_isr_node: out of nodes");
    return NULL;
}

int add_isr (unsigned long source, isrfunc isr, int pri, void *data,
	     char *name)
{
    isr_node_t *p;

    if (source & IRQ_MACHSPEC)
    {
	return mach_add_isr (source, isr, pri, data, name);
    }

    if (source < IRQ1 || source > IRQ7)
	panic ("add_isr: Incorrect IRQ source %ld from %s\n", source, name);

    p = new_isr_node();
    if (p == NULL)
	return 0;
    p->isr = isr;
    p->pri = pri;
    p->data = data;
    p->name = name;
    p->next = NULL;

    insert_isr (&isr_list[source-1], p);

    return 1;
}

int remove_isr (unsigned long source, isrfunc isr, void *data)
{
    if (source & IRQ_MACHSPEC)
	return mach_remove_isr (source, isr, data);

    if (source < IRQ1 || source > IRQ7) {
	printk ("remove_isr: Incorrect IRQ source %ld\n", source);
	return 0;
    }

    delete_isr (&isr_list[source - 1], isr, data);
    return 1;
}

void call_isr_list(int irq, isr_node_t *p, struct pt_regs *fp)
{
    while (p) {
	p->isr (irq, fp, p->data);
	p = p->next;
    }
}

asmlinkage void process_int(int vec, struct pt_regs *regs)
{
	int level;

	if (vec >= VECOFF(VEC_INT1) && vec <= VECOFF(VEC_INT7))
		level = (vec - VECOFF(VEC_SPUR)) >> 2;
	else {
		if (mach_process_int)
			mach_process_int(vec, regs);
		else
			panic("Can't process interrupt vector 0x%03x\n", vec);
		return;
	}

	kstat.interrupts[level]++;  	
	call_isr_list (level, isr_list[level-1], regs);
}

int request_irq(unsigned int irq,
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long flags, const char * devname, void *dev_id)
{
	return -EINVAL;
}

void free_irq(unsigned int irq, void *dev_id)
{
}

/*
 * Do we need these probe functions on the m68k?
 */
unsigned long probe_irq_on (void)
{
  return 0;
}

int probe_irq_off (unsigned long irqs)
{
  return 0;
}

void enable_irq(unsigned int irq_nr)
{
	if ((irq_nr & IRQ_MACHSPEC) && mach_enable_irq)
		mach_enable_irq(irq_nr);
}

void disable_irq(unsigned int irq_nr)
{
	if ((irq_nr & IRQ_MACHSPEC) && mach_disable_irq)
		mach_disable_irq(irq_nr);
}

int get_irq_list(char *buf)
{
    int i, len = 0;
    isr_node_t *p;
    
    /* autovector interrupts */
    for (i = IRQ1; i <= IRQ7; ++i) {
	if (!isr_list[i-1])
	    continue;
	len += sprintf(buf+len, "auto %2d: %8d ", i, kstat.interrupts[i]);
	for (p = isr_list[i-1]; p; p = p->next) {
	    len += sprintf(buf+len, "%s\n", p->name);
	    if (p->next)
		len += sprintf(buf+len, "                  ");
	}
    }

    len = mach_get_irq_list(buf, len);
    return len;
}
