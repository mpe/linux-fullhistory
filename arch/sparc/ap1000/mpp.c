  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * simple mpp functions for the AP+
 */

#include <asm/ap1000/apreg.h>
#include <asm/ap1000/apservice.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/pgtable.h>

extern int cap_cid0;
extern int cap_ncel0;
extern struct cap_init cap_init;

static volatile int mpp_current_task = 0;
static int gang_factor = DEF_GANG_FACTOR;
static int last_task = 0;


void mpp_schedule(struct cap_request *req)
{
	mpp_current_task = req->data[0];
	current->need_resched = 1;
	mark_bh(TQUEUE_BH);
}


void mpp_notify_schedule(struct task_struct *tsk)
{
	last_task = tsk->taskid;

	msc_switch_check(tsk);

	if (gang_factor == 0) return;

	if (cap_cid0 == cap_init.bootcid && 
	    mpp_current_task != tsk->taskid) {
		struct cap_request req;

		mpp_current_task = tsk->taskid;
	
		req.cid = mpp_cid();
		req.type = REQ_SCHEDULE;
		req.size = sizeof(req);
 		req.header = MAKE_HEADER(-1);
		req.data[0] = mpp_current_task;
		
		bif_queue(&req,NULL,0);
	}
}


int mpp_weight(struct task_struct *tsk)
{
	extern int block_parallel_tasks;

	if (!MPP_IS_PAR_TASK(tsk->taskid)) return 0;

	if (block_parallel_tasks) return -1000;

	/* XXX task[] fixme */
	if (last_task && last_task != tsk->taskid && task[last_task] &&
	    !msc_switch_ok()) return -1000;

	if (cap_cid0 != cap_init.bootcid &&
	    tsk->taskid != mpp_current_task) {
		return -gang_factor;
	}
	return 0;
}

void mpp_set_gang_factor(int factor)
{
	gang_factor = factor;
}
