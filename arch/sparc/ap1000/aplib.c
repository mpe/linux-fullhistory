  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */

/* kernel based aplib.

   This was initially implemented in user space, but we eventually
   relented when we discovered some really nasty MSC hardware bugs and
   decided to disallow access to the device registers by users. Pity :-(

   Andrew Tridgell, November 1996 
*/


#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

 #include <asm/ap1000/pgtapmmu.h>
#include <asm/ap1000/apreg.h>
#include <asm/ap1000/apservice.h>
#include <asm/ap1000/aplib.h>


extern int *tnet_rel_cid_table;
extern unsigned _cid, _ncel, _ncelx, _ncely, _cidx, _cidy;


/* this is used to stop the task hogging the MSC while paging in data */
static inline void page_in(char *addr,long size)
{
	unsigned sum = 0;
	while (size > 0) {
		sum += *(volatile char *)addr;
		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
}


/* this sets up the aplib structures using info passed in from user space
   it should only be called once, and should be the first aplib call 
   it should be followed by APLIB_SYNC 
   */
static inline int aplib_init(struct aplib_init *init)
{
	struct aplib_struct *aplib;
	int error,i;
	int old_uid;

	error = verify_area(VERIFY_READ,init,sizeof(*init));
	if (error) return error;
	error = verify_area(VERIFY_READ,init->phys_cells,
			    sizeof(int)*init->numcells);
	if (error) return error;
	error = verify_area(VERIFY_WRITE,
			    init->ringbuffer,
			    init->ringbuf_size * sizeof(int));
	if (error) return error;
	error = verify_area(VERIFY_WRITE,
			    (char *)APLIB_PAGE_BASE,
			    APLIB_PAGE_LEN);
	if (error) return error;

	if (!MPP_IS_PAR_TASK(current->taskid))
		return -EINVAL;

	if (current->aplib)
		return -EINVAL;

	aplib = current->aplib = (struct aplib_struct *)APLIB_PAGE_BASE;

	/* lock the aplib structure in memory */
	old_uid = current->euid;
	current->euid = 0;
	memset(aplib,0,APLIB_PAGE_LEN);
	error = sys_mlock(aplib,APLIB_PAGE_LEN);
	current->euid = old_uid;
	if (error) {
		printk("mlock1 failed\n");
		return error;
	}

	/* lock the ringbuffer in memory */
	old_uid = current->euid;
	current->euid = 0;
	memset(init->ringbuffer,0,init->ringbuf_size*4);
	error = sys_mlock(init->ringbuffer,init->ringbuf_size*4);
	current->euid = old_uid;
	if (error) {
		printk("mlock2 failed\n");
		return error;
	}

	aplib->ringbuf = init->ringbuffer;
	aplib->ringbuf_size = init->ringbuf_size;
	aplib->numcells = init->numcells;
	aplib->cid = init->cid;
	aplib->tid = current->taskid;
	aplib->numcells_x = init->numcells_x;
	aplib->numcells_y = init->numcells_y;
	aplib->cidx = init->cid % init->numcells_x;
	aplib->cidy = init->cid / init->numcells_x;

	aplib->physical_cid = (unsigned *)(aplib+1);
	aplib->rel_cid      = aplib->physical_cid + init->numcells;

	if ((char *)(aplib->rel_cid + init->numcells) >
	    (char *)(APLIB_PAGE_BASE + APLIB_PAGE_LEN)) {
		return -ENOMEM;
	}

	memcpy(aplib->physical_cid,init->phys_cells,
	       sizeof(int)*init->numcells);

	/* initialise the relative cid table */
	for (i=0;i<aplib->numcells;i++) 
		aplib->rel_cid[i] = 
			tnet_rel_cid_table[aplib->physical_cid[i]];

	return 0;
}


/* n == which sync line (ignored)
   returns logical or of the stat values across the cells (1 bit resolution) 

   This has to be done very carefully as the tasks can startup on the cells
   in any order, so we don't know which tasks have started up when this
   is called
*/
static inline int aplib_sync(int n,int stat)
{
	struct aplib_struct *aplib = current->aplib;
	static int sync_flags[MPP_NUM_TASKS];
	int i,err;
	int tsk = current->taskid;

	stat &= 1;

	if (aplib->numcells < 2) 
		return stat;

	tsk -= MPP_TASK_BASE;

	if (aplib->cid == 0) {
		if ((err=wait_on_int(&sync_flags[tsk],
				     aplib->numcells-1,5)))
			return err;
		sync_flags[tsk] = 0;
		if (aplib->numcells == _ncel) {
			ap_bput(0,0,0,(u_long)&sync_flags[tsk],0);
		} else {
			for (i=1;i<aplib->numcells;i++)
				ap_put(aplib->physical_cid[i],
				       0,0,0,(u_long)&sync_flags[tsk],0);
		}
	} else {
		ap_put(aplib->physical_cid[0],
		       0,0,0,(u_long)&sync_flags[tsk],0);
		if ((err=wait_on_int(&sync_flags[tsk],1,5)))
			return err;
		sync_flags[tsk] = 0;
	}

	/* I haven't written the xy_ calls yet ... */
	/* aplib_xy_ior(stat,&stat); */

	return stat;
}



static inline void _putget(unsigned q,
			   unsigned rcell,
			   unsigned *src_addr,
			   unsigned size,unsigned *dest_addr,
			   unsigned *dest_flag,unsigned *src_flag)
{
	unsigned flags;
	volatile unsigned *entry = (volatile unsigned *)q;

	save_flags(flags); cli();

	*entry = rcell;
	*entry = size;
	*entry = (unsigned)dest_addr;
	*entry = 0;
	*entry = (unsigned)dest_flag;
	*entry = (unsigned)src_flag;
	*entry = (unsigned)src_addr;
	*entry = 0;

	restore_flags(flags);
}


/* a basic put() operation. Note the avoidance of odd word boundaries
   and transfers sizes beyond what the hardware can deal with */
static inline int aplib_put(struct aplib_putget *put)
{
	int error;
	struct aplib_struct *aplib = current->aplib;

	error = verify_area(VERIFY_WRITE,put,sizeof(*put));
	if (error) return error;

	if (put->cid >= aplib->numcells) 
		return -EINVAL;

	do {
		int n;

		if (put->size && (((unsigned)put->src_addr) & 4)) {
			n = 1;
		} else if (put->size > MAX_PUT_SIZE) {
			n = MAX_PUT_SIZE;
		} else {
			n = put->size;
		}

		put->size -= n;

		page_in((char *)put->src_addr,n<<2);

		_putget(MSC_PUT_QUEUE,
			aplib->rel_cid[put->cid],
			put->src_addr,
			n,
			put->dest_addr,
			put->size?0:put->dest_flag,
			put->size?0:put->src_flag);

		put->dest_addr += n;
		put->src_addr += n;
	} while (put->size);

	if (put->ack) {
		aplib->ack_request++;
		_putget(MSC_GET_QUEUE,
			aplib->rel_cid[put->cid], 
			0, 0, 0,
			&aplib->ack_flag, 0);
	}

	return 0;
}


/* a basic get() operation */
static inline int aplib_get(struct aplib_putget *get)
{
	struct aplib_struct *aplib = current->aplib;
	int error = verify_area(VERIFY_WRITE,get,sizeof(*get));
	if (error) return error;

	if (get->cid >= aplib->numcells) 
		return -EINVAL;

	do {
		int n;

		if (get->size && (((unsigned)get->src_addr) & 4)) {
			n = 1;
		} else if (get->size > MAX_PUT_SIZE) {
			n = MAX_PUT_SIZE;
		} else {
			n = get->size;
		}

		get->size -= n;

		page_in((char *)get->dest_addr,n<<2);

		_putget(MSC_GET_QUEUE,
			aplib->rel_cid[get->cid],
			get->src_addr,
			n,
			get->dest_addr,
			get->size?0:get->dest_flag,
			get->size?0:get->src_flag);
		
		get->dest_addr += n;
		get->src_addr += n;
	} while (get->size);

	return 0;
}


/* we have received a protocol message - now do the get 
 This function is called from interrupt level with interrupts
 disabled 

 note that send->size is now in words
*/
void aplib_bigrecv(unsigned *msgp)
{
	struct aplib_struct *aplib;
	struct aplib_send *send = (struct aplib_send *)(msgp+2);
	unsigned tid = (msgp[1]&0x3FF);
	unsigned cid = (msgp[0]>>22)&0x1FF;
	unsigned octx, ctx;
	struct task_struct *tsk;
	unsigned room;

	tsk = task[tid];
	if (!tsk || !tsk->aplib)
		return;

	octx = apmmu_get_context();
	ctx = MPP_TASK_TO_CTX(tid);
	if (octx != ctx)
		apmmu_set_context(ctx);
	aplib = tsk->aplib;

	if (aplib->write_pointer < aplib->read_pointer) 
		room = aplib->read_pointer - (aplib->write_pointer+1);
	else
		room = aplib->ringbuf_size - 
			((aplib->write_pointer+1)-aplib->read_pointer);

	if (room < (send->size+2)) {
		send_sig(SIGLOST,tsk,1);		
		goto finished;
	}

	aplib->ringbuf[aplib->write_pointer++] = send->info1;
	aplib->ringbuf[aplib->write_pointer++] = send->info2;

	/* now finally do the get() */
	_putget(MSC_GET_QUEUE,
		aplib->rel_cid[cid],
		send->src_addr,
		send->size,
		&aplib->ringbuf[aplib->write_pointer],
		&aplib->rbuf_flag2,
		send->flag_addr);

	aplib->write_pointer += send->size;
	if (aplib->write_pointer >= aplib->ringbuf_size)
		aplib->write_pointer -= aplib->ringbuf_size;
	
finished:
        if (octx != ctx)
                apmmu_set_context(octx);
}


/* note the 8 byte alignment fix for the MSC bug */
static inline int aplib_send(struct aplib_send *send)
{
	struct aplib_struct *aplib = current->aplib;
	int wordSize;
	int byteAlign, byteFix;
	u_long src;
	u_long info1, info2;
	volatile unsigned *q = (volatile unsigned *)MSC_SEND_QUEUE_S;
	extern long system_recv_flag;
	int error;
	unsigned flags, rcell;
	unsigned flag_ptr;

	error = verify_area(VERIFY_WRITE,send,sizeof(*send));
	if (error) return error;

	if (send->cid >= aplib->numcells) 
		return -EINVAL;

	if (send->tag == RBUF_SYSTEM || send->tag == RBUF_BIGSEND)
		return -EINVAL;

	error = verify_area(VERIFY_READ,(char *)send->src_addr,send->size);
	if (error) return error;

	page_in((char *)send->src_addr,send->size);

	rcell = aplib->rel_cid[send->cid];

	byteAlign = send->src_addr & 0x3;
	byteFix = send->size & 0x3;

	wordSize = (send->size + byteAlign + 3) >> 2;

	src = send->src_addr & ~3;

	/* this handles the MSC alignment bug */
	if (wordSize > 1 &&
	    (src & 4)) {
		info1 |= 0x80000000;
		src -= 4;
		wordSize++;
	}
	
	info1 = (aplib->cid<<22) | (byteFix<<20) | wordSize;
	info2 = (send->tag<<28) | (byteAlign<<26) | 
		(send->type<<10) | aplib->tid;
	flag_ptr = (unsigned)&send->flag;

	if (send->size > SMALL_SEND_THRESHOLD) {
		send->info1 = info1;
		send->info2 = info2;
		send->size = wordSize;
		send->src_addr = src;
		send->flag_addr = (unsigned)&send->flag;
		flag_ptr = 0;

		wordSize = sizeof(*send)>>2;
		src = (unsigned)send;
		
		info1 = (aplib->cid<<22) | wordSize;
		info2 = (RBUF_BIGSEND<<28) | aplib->tid;
	}

	save_flags(flags); cli();

	*q = rcell;
	*q = wordSize;
	*q = (u_long)&system_recv_flag;
	*q = flag_ptr;
	*q = (u_long)src;
	*q = 0;
	*q = info1;
	*q = info2;

	restore_flags(flags);

	return 0;
}


static inline int aplib_probe(void)
{
	tnet_check_completion();
	return 0;
}

static inline int aplib_poll(unsigned counter)
{
	struct aplib_struct *aplib = current->aplib;

	while (counter == aplib->rbuf_flag1 + aplib->rbuf_flag2) {
		tnet_check_completion();
		if (current->need_resched)
			break;
		if (signal_pending(current)) break;
	}
	return 0;
}

int sys_aplib(unsigned call,int a1,int a2,int a3,int a4)
{

	if (!current->aplib && call != APLIB_INIT)
		return -EINVAL;

	switch (call) {
	case APLIB_INIT:
		return aplib_init((struct aplib_init *)a1);

	case APLIB_SYNC:
		return aplib_sync(a1,a2);

	case APLIB_PUT:
		return aplib_put((struct aplib_putget *)a1);

	case APLIB_GET:
		return aplib_get((struct aplib_putget *)a1);

	case APLIB_SEND:
		return aplib_send((struct aplib_send *)a1);

	case APLIB_PROBE:
		return aplib_probe();

	case APLIB_POLL:
		return aplib_poll((unsigned)a1);
	}

	return -EINVAL;
}


