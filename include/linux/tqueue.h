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
#include <linux/list.h>
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
 * Notes:
 * - Bottom halfs are called in the reverse order that they were linked into
 *   the list.
 */

struct tq_struct {
	struct list_head list;		/* linked list of active bh's */
	unsigned long sync;		/* must be initialized to zero */
	void (*routine)(void *);	/* function to call */
	void *data;			/* argument to function */
};

typedef struct list_head task_queue;

#define DECLARE_TASK_QUEUE(q)	LIST_HEAD(q)
#define TQ_ACTIVE(q)		(!list_empty(&q))

extern task_queue tq_timer, tq_immediate, tq_disk;

/*
 * To implement your own list of active bottom halfs, use the following
 * two definitions:
 *
 * DECLARE_TASK_QUEUE(my_bh);
 * struct tq_struct run_my_bh = {
 * 	routine: (void (*)(void *)) run_task_queue,
 *	data: &my_bh
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
 * Queue a task on a tq.  Return non-zero if it was successfully
 * added.
 */
static inline int queue_task(struct tq_struct *bh_pointer,
			   task_queue *bh_list)
{
	int ret = 0;
	if (!test_and_set_bit(0,&bh_pointer->sync)) {
		unsigned long flags;
		spin_lock_irqsave(&tqueue_lock, flags);
		list_add_tail(&bh_pointer->list, bh_list);
		spin_unlock_irqrestore(&tqueue_lock, flags);
		ret = 1;
	}
	return ret;
}

/*
 * Call all "bottom halfs" on a given list.
 */
static inline void run_task_queue(task_queue *list)
{
	while (!list_empty(list)) {
		unsigned long flags;
		struct list_head *next;

		spin_lock_irqsave(&tqueue_lock, flags);
		next = list->next;
		if (next != list) {
			void *arg;
			void (*f) (void *);
			struct tq_struct *p;

			list_del(next);
			p = list_entry(next, struct tq_struct, list);
			arg = p->data;
			f = p->routine;
			p->sync = 0;
			spin_unlock_irqrestore(&tqueue_lock, flags);

			if (f)
				f(arg);
			continue;
		}
		spin_unlock_irqrestore(&tqueue_lock, flags);
	}
}

#endif /* _LINUX_TQUEUE_H */
