/* 
        pseudo.h    (c) 1997-8  Grant R. Guenther <grant@torque.net>
                                Under the terms of the GNU public license.

	This is the "pseudo-interrupt" logic for parallel port drivers.

        This module is #included into each driver.  It makes one
        function available:

		ps_set_intr( void (*continuation)(void),
			     int  (*ready)(void),
			     int timeout,
			     int nice )

	Which will arrange for ready() to be evaluated frequently and
	when either it returns true, or timeout jiffies have passed,
	continuation() will be invoked.

	If nice is true, the test will done approximately once a
	jiffy.  If nice is 0, the test will also be done whenever
	the scheduler runs (by adding it to a task queue).

*/

/* Changes:

	1.01	1998.05.03	Switched from cli()/sti() to spinlocks

*/
	
#define PS_VERSION	"1.01"

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/tqueue.h>

static void ps_timer_int( unsigned long data);
static void ps_tq_int( void *data);

static int ps_use_tq = 1;
static void (* ps_continuation)(void);
static int (* ps_ready)(void);
static int ps_then;
static int ps_timeout;
static int ps_timer_active = 0;
static int ps_tq_active = 0;

static spinlock_t ps_spinlock = SPIN_LOCK_UNLOCKED;

static struct timer_list ps_timer = {0,0,0,0,ps_timer_int};
static struct tq_struct ps_tq = {0,0,ps_tq_int,NULL};

static void ps_set_intr( void (*continuation)(void), 
			 int (*ready)(void),
			 int timeout, int nice )

{       long	flags;

	spin_lock_irqsave(&ps_spinlock,flags);

	ps_continuation = continuation;
	ps_ready = ready;
        ps_then = jiffies;
	ps_timeout = jiffies + timeout;
	ps_use_tq = !nice;

        if (ps_use_tq && !ps_tq_active) {
#ifdef HAVE_DISABLE_HLT
                disable_hlt();
#endif
		ps_tq_active = 1;
                queue_task(&ps_tq,&tq_scheduler);
	}

        if (!ps_timer_active) {
		ps_timer_active = 1;
                ps_timer.expires = jiffies;
                add_timer(&ps_timer);
        }

	spin_unlock_irqrestore(&ps_spinlock,flags);
}

static void ps_tq_int( void *data )

{       void (*con)(void);
	long flags;

	spin_lock_irqsave(&ps_spinlock,flags);

        con = ps_continuation;

#ifdef HAVE_DISABLE_HLT
        enable_hlt();
#endif

        ps_tq_active = 0;

        if (!con) {
		spin_unlock_irqrestore(&ps_spinlock,flags);
		return;
	}
        if (!ps_ready || ps_ready() || (jiffies >= ps_timeout)) {
                ps_continuation = NULL;
        	spin_unlock_irqrestore(&ps_spinlock,flags);
                con();
                return;
                }

#ifdef HAVE_DISABLE_HLT
        disable_hlt();
#endif

        ps_tq_active = 1;
	queue_task(&ps_tq,&tq_scheduler);
        spin_unlock_irqrestore(&ps_spinlock,flags);
}

static void ps_timer_int( unsigned long data)

{       void (*con)(void);
	long	flags;

	spin_lock_irqsave(&ps_spinlock,flags);

	con = ps_continuation;
	ps_timer_active = 0;
	if (!con) {
	        spin_unlock_irqrestore(&ps_spinlock,flags);
		return;
	}
        if (!ps_ready || ps_ready() || (jiffies >= ps_timeout)) {
                ps_continuation = NULL;
	        spin_unlock_irqrestore(&ps_spinlock,flags);
                con();
		return;
		}
	ps_timer_active = 1;
        ps_timer.expires = jiffies;
        add_timer(&ps_timer);
        spin_unlock_irqrestore(&ps_spinlock,flags);
}

/* end of pseudo.h */

