/*
 * tqueue.h --- task queue handling for Linux.
 *
 * Mostly based on a proposed bottom-half replacement code written by
 * Kai Petzke, wpp@marie.physik.tu-berlin.de.
 *
 * Modified for use in the Linux kernel by Theodore Ts'o,
 * tytso@mit.edu.  Any bugs are my fault, not Kai's.
 *
 * The original comment follows below.
 */

#ifndef _LINUX_TQUEUE_H
#define _LINUX_TQUEUE_H

#include <linux/spinlock.h>
#include <asm/bitops.h>
#include <asm/system.h>

/*
 * New proposed "bottom half" handlers:
 * (C) 1994 Kai Petzke, wpp@marie.physik.tu-berlin.de
 *
 * Advantages:
 * - Bottom halfs are implemented as a linked list.  You can have as many
 *   of them, as you want.
 * - No more scanning of a bit field is required upon call of a bottom half.
 * - Support for chained bottom half lists.  The run_task_queue() function can be
 *   used as a bottom half handler.  This is for example useful for bottom
 *   halfs, which want to be delayed until the next clock tick.
 *
 * Problems:
 * - The queue_task_irq() inline function is only atomic with respect to itself.
 *   Problems can occur, when queue_task_irq() is called from a normal system
 *   call, and an interrupt comes in.  No problems occur, when queue_task_irq()
 *   is called from an interrupt or bottom half, and interrupted, as run_task_queue()
 *   will not be executed/continued before the last interrupt returns.  If in
 *   doubt, use queue_task(), not queue_task_irq().
 * - Bottom halfs are called in the reverse order that they were linked into
 *   the list.
 */

struct tq_struct {
	struct tq_struct *next;		/* linked list of active bh's */
	unsigned long sync;		/* must be initialized to zero */
	void (*routine)(void *);	/* function to call */
	void *data;			/* argument to function */
};

typedef struct tq_struct * task_queue;

#define DECLARE_TASK_QUEUE(q)  task_queue q = NULL

extern task_queue tq_timer, tq_immediate, tq_scheduler, tq_disk;

/*
 * To implement your own list of active bottom halfs, use the following
 * two definitions:
 *
 * struct tq_struct *my_bh = NULL;
 * struct tq_struct run_my_bh = {
 *	0, 0, (void (*)(void *)) run_task_queue, &my_bh
 * };
 *
 * To activate a bottom half on your list, use:
 *
 *     queue_task(tq_pointer, &my_bh);
 *
 * To run the bottom halfs on your list put them on the immediate list by:
 *
 *     queue_task(&run_my_bh, &tq_immediate);
 *
 * This allows you to do deferred procession.  For example, you could
 * have a bottom half list tq_timer, which is marked active by the timer
 * interrupt.
 */

extern spinlock_t tqueue_lock;

/*
 * queue_task
 */
extern __inline__ void queue_task(struct tq_struct *bh_pointer,
			   task_queue *bh_list)
{
	if (!test_and_set_bit(0,&bh_pointer->sync)) {
		unsigned long flags;
		spin_lock_irqsave(&tqueue_lock, flags);
		bh_pointer->next = *bh_list;
		*bh_list = bh_pointer;
		spin_unlock_irqrestore(&tqueue_lock, flags);
	}
}

/*
 * Call all "bottom halfs" on a given list.
 */
extern __inline__ void run_task_queue(task_queue *list)
{
	if (*list) {
		unsigned long flags;
		struct tq_struct *p;

		spin_lock_irqsave(&tqueue_lock, flags);
		p = *list;
		*list = NULL;
		spin_unlock_irqrestore(&tqueue_lock, flags);
		
		while (p) {
			void *arg;
			void (*f) (void *);
			struct tq_struct *save_p;
			arg    = p -> data;
			f      = p -> routine;
			save_p = p;
			p      = p -> next;
			mb();
			save_p -> sync = 0;
			if (f)
				(*f)(arg);
		}
	}
}

#endif /* _LINUX_TQUEUE_H */
