#ifndef _M68K_IRQ_H_
#define _M68K_IRQ_H_

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#include <linux/config.h>

/*
 * This should be the same as the max(NUM_X_SOURCES) for all the
 * different m68k hosts compiled into the kernel.
 * Currently the Atari has 72 and the Amiga 24, but if both are
 * supported in the kernel it is better to make room for 72.
 */
#if defined(CONFIG_ATARI)
#define NR_IRQS 72
#else
#define NR_IRQS 24
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

#define IRQ1		 (1)    /* level 1 interrupt */
#define IRQ2		 (2)    /* level 2 interrupt */
#define IRQ3		 (3)    /* level 3 interrupt */
#define IRQ4		 (4)    /* level 4 interrupt */
#define IRQ5		 (5)    /* level 5 interrupt */
#define IRQ6		 (6)    /* level 6 interrupt */
#define IRQ7		 (7)    /* level 7 interrupt (non-maskable) */

/*
 * "Generic" interrupt sources
 */

#define IRQ_SCHED_TIMER  (8)    /* interrupt source for scheduling timer */

/*
 * Machine specific interrupt sources.
 *
 * Adding an interrupt service routine for a source with this bit
 * set indicates a special machine specific interrupt source.
 * The machine specific files define these sources.
 */

#define IRQ_MACHSPEC	 (0x10000000L)

#ifndef ISRFUNC_T
struct pt_regs;
typedef void (*isrfunc) (int irq, struct pt_regs * regs, void *data);
#define ISRFUNC_T
#endif /* ISRFUNC_T */

/*
 * This structure is used to chain together the ISRs for a particular
 * interrupt source (if it supports chaining).
 */
typedef struct isr_node {
    isrfunc	    isr;
    int 	    pri;
    void            *data;
    char	    *name;
    struct isr_node *next;
} isr_node_t;

/* count of spurious interrupts */
extern volatile unsigned long num_spurious;

/*
 * This function returns a new isr_node_t
 */
extern isr_node_t *new_isr_node(void);

/*
 * This function is used to add a specific interrupt service routine
 * for the specified interrupt source.
 *
 * If the source is machine specific, it will be passed along to the
 * machine specific routine.
 *
 * "data" is user specified data which will be passed to the isr routine.
 *
 * (isrfunc is defined in linux/config.h)
 */
extern int add_isr (unsigned long source, isrfunc isr, int pri, void
		    *data, char *name);

/*
 * This routine will remove an isr for the specified interrupt source.
 */
extern int remove_isr (unsigned long source, isrfunc isr, void *data);

/*
 * This routine will insert an isr_node_t into a chain of nodes, using
 * the priority stored in the node.
 */
extern void insert_isr (isr_node_t **listp, isr_node_t *node);

/*
 * This routine will delete the isr node for isr from a chain of nodes
 */
extern void delete_isr (isr_node_t **listp, isrfunc isr, void *data);

/*
 * This routine may be used to call the isr routines in the passed list.
 */
extern void call_isr_list (int irq, isr_node_t *p, struct pt_regs *fp);

#endif /* _M68K_IRQ_H_ */
