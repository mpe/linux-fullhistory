  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * Initialize the AP1000 hardware: BIF, MSC+, MC+, etc.
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/mpp.h>
#include <asm/irq.h>
#include <asm/ap1000/apservice.h>
#include <asm/ap1000/apreg.h>

#define APLOG 0

/* these make using CellOS code easier */
int cap_nopt0;
int cap_cid0;
int cap_ncel0;

unsigned _cid, _ncel, _ncelx, _ncely, _cidx, _cidy;

/* yuck - needed for sun4c! */
static unsigned char dummy;
unsigned char *auxio_register = &dummy;


extern struct cap_init cap_init;

static void unexpected_irq(int irq, void *dev_id, struct pt_regs *regs)
{
  ap_panic("** unexpected interrupt %d **\n",irq); 
}

static void ap_other_irqs(void)
{
	request_irq(3, unexpected_irq, SA_INTERRUPT, "unused", 0);
	request_irq(5, unexpected_irq, SA_INTERRUPT, "unused", 0);
	request_irq(12, unexpected_irq, SA_INTERRUPT, "unused", 0);
	request_irq(15, unexpected_irq, SA_INTERRUPT, "unused", 0);
}

int ap_memory_size(void)
{
	if ((MSC_IN(MSC_SIMMCHK) & MSC_SIMMCHK_MASK) == 0) {
		return 16*1024*1024; 
	}
	return 64*1024*1024;
}

static void show_registers(void)
{
	extern struct pt_regs *bif_pt_regs;
	if (bif_pt_regs) 
		show_regs(bif_pt_regs);
	else
		printk("unable to show registers\n");
}


static void check_alive(void)
{
	printk("Cell %d is alive\n",mpp_cid());	       
}



static void show_task(struct task_struct *t)
{
	printk("cell=%3d uid=%5d pid=%5d utime=%3d stime=%3d etime=%3d name=%s\n",
	       mpp_cid(),
	       t->uid,
	       (int)t->pid,
	       (int)t->utime,
	       (int)t->stime,
	       (jiffies - (int)t->start_time) / 100,
	       t->comm);
}

static void show_ptasks(void)
{
	extern struct task_struct *task[];
	struct task_struct *p;
	int i;
	int count=0;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		struct task_struct **tp = p->tarray_ptr;

		if(tp >= &task[MPP_TASK_BASE]) {
			show_task(p);
			count++;
		}
	}
	read_unlock(&tasklist_lock);

	if (count == 0)
		printk("no parallel tasks on cell %d\n",mpp_cid());
}

static void show_utasks(void)
{
	extern struct task_struct *task[];
	struct task_struct *p;
	int i;
	int count=0;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if(p->uid > 1) {
			show_task(task[i]);
			count++;
		}
	}
	read_unlock(&tasklist_lock);

	if (count == 0)
		printk("no user tasks on cell %d\n",mpp_cid());
}


static void show_otasks(void)
{
	extern struct task_struct *task[];
	struct task_struct *p;
	int i;
	int count=0;
	extern int ap_current_uid;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if(p->uid == ap_current_uid) {
			show_task(task[i]);
			count++;
		}
	}
	read_unlock(&tasklist_lock);

	if (count == 0)
		printk("no tasks on cell %d\n",mpp_cid());
}


void do_panic(void)
{
	int *x = 0;
	*x = 1; /* uggh */
}


void mpp_hw_init(void)
{
	extern void show_state(void);
	extern void breakpoint(void);
	extern void ctrl_alt_del(void);
	extern void mac_print_state(void);
	extern void show_debug_keys(void);
	
	bif_add_debug_key('c',check_alive,"check if a cell is alive");
	bif_add_debug_key('k',show_debug_keys,"show the kernel debug keys");
	bif_add_debug_key('p',show_registers,"show register info");
	bif_add_debug_key('p',show_registers,"show register info");
	bif_add_debug_key('m',show_mem,"detailed memory stats");
	bif_add_debug_key('s',show_state,"detailed process stats");
	bif_add_debug_key('D',ap_start_debugger,"launch the kernel debugger");
	bif_add_debug_key('i',breakpoint,"send a breakpoint");
	bif_add_debug_key('r',ctrl_alt_del,"run shutdown (doesn't work)");  
	bif_add_debug_key('P',show_ptasks,"show running parallel tasks");  
	bif_add_debug_key('U',show_utasks,"show all user tasks");  
	bif_add_debug_key('O',show_otasks,"show own user tasks");  
	bif_add_debug_key('^',do_panic,"panic :-)");  
	
	
	cap_cid0 = BIF_IN(BIF_CIDR1);
	cap_ncel0 = cap_init.numcells;
	
	_cid = cap_cid0;
	_ncel = cap_ncel0;
	_ncelx = _ncel<8?_ncel:8;
	_ncely = ((_ncel-1) / _ncelx) + 1;
	_cidx = _cid % _ncelx;
	_cidy = _cid / _ncelx;
	
	ap_bif_init();
	ap_msc_init();
	ap_tnet_init();
	ap_profile_init();
	ap_other_irqs();
	ap_ringbuf_init();
#if APLOG
	ap_log(NULL,-1);
#endif
}
