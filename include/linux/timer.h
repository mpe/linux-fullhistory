#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

#include <linux/config.h>
#include <linux/list.h>

/*
 * This is completely separate from the above, and is the
 * "new and improved" way of handling timers more dynamically.
 * Hopefully efficient and general enough for most things.
 *
 * The "hardcoded" timers above are still useful for well-
 * defined problems, but the timer-list is probably better
 * when you need multiple outstanding timers or similar.
 *
 * The "data" field is in case you want to use the same
 * timeout function for several timeouts. You can use this
 * to distinguish between the different invocations.
 */
struct timer_list {
	struct list_head list;
	unsigned long expires;
	unsigned long data;
	void (*function)(unsigned long);
};

extern volatile struct timer_list *running_timer;
extern void add_timer(struct timer_list * timer);
extern int del_timer(struct timer_list * timer);

/*
 * mod_timer is a more efficient way to update the expire field of an
 * active timer (if the timer is inactive it will be activated)
 * mod_timer(a,b) is equivalent to del_timer(a); a->expires = b; add_timer(a)
 */
int mod_timer(struct timer_list *timer, unsigned long expires);

extern void it_real_fn(unsigned long);

static inline void init_timer(struct timer_list * timer)
{
	timer->list.next = timer->list.prev = NULL;
}

static inline int timer_pending (const struct timer_list * timer)
{
	return timer->list.next != NULL;
}

#ifdef CONFIG_SMP
#define timer_enter(t) do { running_timer = t; mb(); } while (0)
#define timer_exit() do { running_timer = NULL; } while (0)
#define timer_is_running(t) (running_timer == t)
#define timer_synchronize(t) while (timer_is_running(t)) barrier()
extern int del_timer_sync(struct timer_list * timer);
#else
#define timer_enter(t)		do { } while (0)
#define timer_exit()		do { } while (0)
#define timer_is_running(t)	(0)
#define timer_synchronize(t)	do { (void)(t); barrier(); } while(0)
#define del_timer_sync(t)	del_timer(t)
#endif

/*
 *	These inlines deal with timer wrapping correctly. You are 
 *	strongly encouraged to use them
 *	1. Because people otherwise forget
 *	2. Because if the timer wrap changes in future you wont have to
 *	   alter your driver code.
 *
 * Do this with "<0" and ">=0" to only test the sign of the result. A
 * good compiler would generate better code (and a really good compiler
 * wouldn't care). Gcc is currently neither.
 */
#define time_after(a,b)		((long)(b) - (long)(a) < 0)
#define time_before(a,b)	time_after(b,a)

#define time_after_eq(a,b)	((long)(a) - (long)(b) >= 0)
#define time_before_eq(a,b)	time_after_eq(b,a)

#endif
