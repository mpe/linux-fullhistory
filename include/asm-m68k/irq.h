#ifndef _M68K_IRQ_H_
#define _M68K_IRQ_H_

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#include <linux/config.h>

/*
 * # of m68k interrupts
 */

#define SYS_IRQS 8

/*
 * This should be the same as the max(NUM_X_SOURCES) for all the
 * different m68k hosts compiled into the kernel.
 * Currently the Atari has 72 and the Amiga 24, but if both are
 * supported in the kernel it is better to make room for 72.
 */
#if defined(CONFIG_ATARI)
#define NR_IRQS (72+SYS_IRQS)
#else
#define NR_IRQS (24+SYS_IRQS)
#endif

/*
 * Interrupt source definitions
 * General interrupt sources are the level 1-7.
 * Adding an interrupt service routine for one of these sources
 * results in the addition of that routine to a chain of routines.
 * Each one is called in succession.  Each individual interrupt
 * service routine should determine if the device associated with
 * that routine requires service.
 */

#define IRQ1		(1)	/* level 1 interrupt */
#define IRQ2		(2)	/* level 2 interrupt */
#define IRQ3		(3)	/* level 3 interrupt */
#define IRQ4		(4)	/* level 4 interrupt */
#define IRQ5		(5)	/* level 5 interrupt */
#define IRQ6		(6)	/* level 6 interrupt */
#define IRQ7		(7)	/* level 7 interrupt (non-maskable) */

/*
 * "Generic" interrupt sources
 */

#define IRQ_SCHED_TIMER	(8)    /* interrupt source for scheduling timer */

/*
 * Machine specific interrupt sources.
 *
 * Adding an interrupt service routine for a source with this bit
 * set indicates a special machine specific interrupt source.
 * The machine specific files define these sources.
 */

#define IRQ_MACHSPEC	(0x10000000L)
#define IRQ_IDX(irq)	((irq) & ~IRQ_MACHSPEC)

/*
 * various flags for request_irq()
 */
#define IRQ_FLG_LOCK	(0x0001)	/* handler is not replaceable	*/
#define IRQ_FLG_REPLACE	(0x0002)	/* replace existing handler	*/
#define IRQ_FLG_FAST	(0x0004)
#define IRQ_FLG_SLOW	(0x0008)
#define IRQ_FLG_STD	(0x8000)	/* internally used		*/

/*
 * This structure is used to chain together the ISRs for a particular
 * interrupt source (if it supports chaining).
 */
typedef struct irq_node {
	void		(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
	struct irq_node *next;
} irq_node_t;

/*
 * This structure has only 4 elements for speed reasons
 */
typedef struct irq_handler {
	void		(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
} irq_handler_t;

/* count of spurious interrupts */
extern volatile unsigned int num_spurious;

/*
 * This function returns a new irq_node_t
 */
extern irq_node_t *new_irq_node(void);

#endif /* _M68K_IRQ_H_ */
