  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* sync functions using the Tnet */

#include <asm/ap1000/apreg.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/tasks.h>

extern int cap_cid0;
extern unsigned _ncel, _ncelx, _ncely, _cid;

static volatile int sync_flags[MPP_NUM_TASKS];


int ap_sync(int numcells, int *phys_map)
{
	int basecell;
	int i,err;
	int tsk = current->taskid;
	
	if (numcells < 2) return 0;

	if (!MPP_IS_PAR_TASK(tsk)) {
		printk("nonparallel task %d called ap_sync\n",tsk);
		return 0;
	}
	tsk -= MPP_TASK_BASE;
	
	basecell = phys_map[0];
	if (cap_cid0 == basecell) {    
		if ((err=wait_on_int(&sync_flags[tsk],numcells-1,5)))
			return err;
		sync_flags[tsk] = 0;
		if (numcells == _ncel) {
			ap_bput(0,0,0,&sync_flags[tsk],0);
		} else {
			for (i=1;i<numcells;i++)
				ap_put(phys_map[i],0,0,0,&sync_flags[tsk],0);
		}
		return 0;
	}
	
	ap_put(basecell,0,0,0,&sync_flags[tsk],0);
	if ((err=wait_on_int(&sync_flags[tsk],1,5)))
		return err;
	sync_flags[tsk] = 0;
	return 0;
}

