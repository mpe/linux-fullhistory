  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * Routines to control the AP1000+ Message Controller (MSC+)
 * and Memory Controller (MC+).
 *
 */
#define _APLIB_
#include <asm/ap1000/apreg.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/ap1000/pgtapmmu.h>
#include <linux/tasks.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

static void msc_interrupt_9(int irq, void *dev_id, struct pt_regs *regs);
static void msc_interrupt_11(int irq, void *dev_id, struct pt_regs *regs);
static void msc_set_ringbuf(int context);
static void msc_update_read_ptr(int context,int overflow);
static void fail_write(int context,int intr,unsigned vaddr);
static void fail_read(int context,int intr,unsigned vaddr);
static void msc_switch_from_check(struct task_struct *tsk);
static void msc_status(void);

#define DEBUG 0

/*
 * This describes how the 5 queues for outgoing requests
 * are mapped into the 256 words of send queue RAM in the MSC+.
 */
#define NSENDQUEUES		5

static struct send_queues {
    int	base;		/* must be a multiple of size */
    int size;		/* must be 32 or 64 */
} send_queues[NSENDQUEUES] = {
    {0, 64},		/* System put/send requests */
    {192, 32},		/* Remote read/write requests */
    {64, 64},		/* User put/send requests */
    {224, 32},		/* Remote read replies */
    {128, 64},		/* Get replies */
};

#define NR_RBUFS MSC_NR_RBUFS

static struct {
  unsigned rbmbwp;
  unsigned rbmmode;
  unsigned rbmrp;
} ringbufs[MSC_NR_RBUFS] = {
  {MSC_RBMBWP0, MSC_RBMMODE0, MSC_RBMRP0},
  {MSC_RBMBWP1, MSC_RBMMODE1, MSC_RBMRP1},
  {MSC_RBMBWP2, MSC_RBMMODE2, MSC_RBMRP2},
};

#define CTX_MASK	0xfff
#define NULL_CONTEXT	CTX_MASK

#define QOF_ORDER       3	/* 32kB queue overflow buffer */
#define QOF_SIZE	((1<<QOF_ORDER)*PAGE_SIZE)
#define QOF_ELT_SIZE	8	/* size of each queue element */
#define QOF_REDZONE_SZ	8192	/* 8kB redzone, imposed by hardware */
#define QOF_NELT	(QOF_SIZE / QOF_ELT_SIZE)
#define QOF_RED_NELT	(QOF_REDZONE_SZ / QOF_ELT_SIZE)
#define QOF_GREEN_NELT	((QOF_SIZE - QOF_REDZONE_SZ) / QOF_ELT_SIZE)

#define MAKE_QBMPTR(qof, size) \
	(MKFIELD((qof) >> 19, MSC_QBMP_BP) \
	 + MKFIELD((qof) >> 3, MSC_QBMP_WP) \
	 + MKFIELD(((qof) + (size) - 1) >> 13, MSC_QBMP_LIM))

#define QBM_UPDATE_WP(wp)	\
	MSC_OUT(MSC_QBMPTR, INSFIELD(MSC_IN(MSC_QBMPTR), (unsigned)(wp) >> 3, \
				     MSC_QBMP_WP))

/* Send queue overflow buffer structure */
struct qof_elt {
    unsigned info;
    unsigned data;
};

/* Fields in qof_elt.info */
#define QOF_QUEUE_SH	24		/* queue bits start at bit 24 */
#define QOF_QUEUE_M	0x1f		/* 5 bits wide */
#define QOF_ENDBIT	1		/* end bit in bit 0 */

static struct qof_elt *qof_base=NULL; /* start of overflow buffer */
static unsigned long  qof_phys;	/* physical start adrs of overflow buffer */
static struct qof_elt *qof_rp;	/* read pointer for refills */
static struct qof_elt *qof_new;	/* first element we haven't yet looked at */
static int qof_present[NSENDQUEUES];/* # elts for each q in [qof_rp,qof_new) */

/* this is used to flag when the msc is blocked, so we can't send 
   messages on it without the possability of deadlock */
int msc_blocked = 0;
int block_parallel_tasks = 0;

static int qbm_full_counter = 0;

#define INTR_LIMIT 10000
static int intr_counter = 0;
static unsigned intr_mask;

#define DUMMY_RINGBUF_ORDER 5
#define DUMMY_RINGBUF_SIZE ((1<<DUMMY_RINGBUF_ORDER)*PAGE_SIZE)

/* 
 * The system ring buffer, used for inter-kernel comms 
 */
struct ringbuf_struct system_ringbuf = {NULL,NULL,SYSTEM_RINGBUF_ORDER,0,0,0,0};
struct ringbuf_struct dummy_ringbuf = {NULL,NULL,DUMMY_RINGBUF_ORDER,0,0,0,0};
unsigned system_read_ptr = (SYSTEM_RINGBUF_SIZE>>5)-1;
unsigned dummy_read_ptr = (DUMMY_RINGBUF_SIZE>>5)-1;

#define SQ_NEW_MODE(mode) do { \
    MSC_OUT(MSC_SQCTRL, ((MSC_IN(MSC_SQCTRL) & ~MSC_SQC_RMODE) \
			 | MSC_SQC_RMODE_ ## mode)); \
    while ((MSC_IN(MSC_SQCTRL) & MSC_SQC_MODE) != MSC_SQC_MODE_ ## mode) \
	/* hang */ ; \
} while (0)

/* Repack the queue overflow buffer if >= this many already-used entries */
#define REPACK_THRESH	64


static void refill_sq(void);
static void repack_qof(void);
static void shuffle_qof(void);
static void async_callback(int, unsigned long, int, int);


static void mask_all_interrupts(void)
{
    /* disable all MSC+ interrupts */
    MSC_OUT(MSC_INTR, 
	    (AP_SET_INTR_MASK << MSC_INTR_QBMFUL_SH) |
	    (AP_SET_INTR_MASK << MSC_INTR_SQFILL_SH) |
	    (AP_SET_INTR_MASK << MSC_INTR_RBMISS_SH) |
	    (AP_SET_INTR_MASK << MSC_INTR_RBFULL_SH) |
	    (AP_SET_INTR_MASK << MSC_INTR_RMASF_SH) |
	    (AP_SET_INTR_MASK << MSC_INTR_RMASE_SH) |
	    (AP_SET_INTR_MASK << MSC_INTR_SMASF_SH) |
	    (AP_SET_INTR_MASK << MSC_INTR_SMASE_SH));
}

static inline int valid_task(struct task_struct *tsk)
{
  return(tsk && 
	 !((tsk)->flags & PF_EXITING) && 
	 tsk->mm && 
	 tsk->mm->context != NO_CONTEXT);
}

static inline unsigned long apmmu_get_raw_ctable_ptr(void)
{
	unsigned int retval;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (retval) :
			     "r" (APMMU_CTXTBL_PTR),
			     "i" (ASI_M_MMUREGS));
	return (retval);
}

static void mc_tlb_map(unsigned phys_page,unsigned vpage,int context)
{
    unsigned long long *tlb4k;
    unsigned long long new_entry;
    unsigned long *new_entryp = (unsigned long *)&new_entry;
    tlb4k = ((unsigned long long *)MC_MMU_TLB4K) + (vpage & 0xFF);
    new_entryp[0] = (phys_page&~7) >> 3;
    new_entryp[1] = ((phys_page & 7) << 29) | (((vpage>>8)&0xFFF) << 17) | 
	(context << 5) | 0x13; 
    tlb4k[0] = new_entry;
#if DEBUG
    printk("mc_tlb_map(%x,%x,%x) %x %x at %x\n",
	   phys_page,vpage,context,new_entryp[0],new_entryp[1],tlb4k);
#endif
}

static void mc_tlb_unmap(unsigned vpage)
{
	unsigned long long *tlb4k = (unsigned long long *)MC_MMU_TLB4K;
	tlb4k = ((unsigned long long *)MC_MMU_TLB4K) + (vpage & 0xFF);
	tlb4k[0] = 0;
}

void mc_tlb_init(void)
{
	unsigned long long *tlb256k, *tlb4k;
	int i;
	
	tlb4k = (unsigned long long *)MC_MMU_TLB4K;
	for (i = MC_MMU_TLB4K_SIZE; i > 0; --i)
		*tlb4k++ = 0;
	tlb256k = (unsigned long long *)MC_MMU_TLB256K;
	for (i = MC_MMU_TLB256K_SIZE; i > 0; --i)
		*tlb256k++ = 0;
}

void ap_msc_init(void)
{
    int i, flags, res;
    unsigned int qp;

    bif_add_debug_key('M',msc_status,"MSC+ status");

#if DEBUG
    printk("MSC+ version %x\n", MSC_IN(MSC_VERSION));
    printk("MC+  version %x\n", MC_IN(MC_VERSION));
#endif

    mc_tlb_init();

    /* Set the MC's copy of the context table pointer */
    MC_OUT(MC_CTP, apmmu_get_raw_ctable_ptr());

    /* Initialize the send queue pointers */
    qp = MSC_SQPTR0;
    for (i = 0; i < 5; ++i) {
	MSC_OUT(qp, ((send_queues[i].size == 64? MSC_SQP_MODE: 0)
		     + ((send_queues[i].base >> 5) << MSC_SQP_BP_SH)));
	qp += (MSC_SQPTR1 - MSC_SQPTR0);
    }

    /* Initialize the send queue RAM */
    for (i = 0; i < 256; ++i)
	MSC_OUT(MSC_SQRAM + i * 8, -1);

    if (!qof_base) {
      qof_base = (struct qof_elt *) __get_free_pages(GFP_ATOMIC, QOF_ORDER);
      for (i = MAP_NR(qof_base); i <= MAP_NR(((char*)qof_base)+QOF_SIZE-1);++i)
	set_bit(PG_reserved, &mem_map[i].flags);
    }

    qof_phys = mmu_v2p((unsigned long) qof_base);
    MSC_OUT(MSC_QBMPTR, MAKE_QBMPTR((unsigned long)qof_base, QOF_SIZE));
    qof_rp = qof_base;
    qof_new = qof_base;
    for (i = 0; i < NSENDQUEUES; ++i)
	qof_present[i] = 0;

    SQ_NEW_MODE(NORMAL);	/* Set the send queue to normal mode */

    /* Register interrupt handler for MSC+ */
    save_flags(flags); cli();
    res = request_irq(APMSC_IRQ, msc_interrupt_11, SA_INTERRUPT,
		      "apmsc", NULL);
    if (res != 0)
	printk("couldn't register MSC interrupt 11: error=%d\n", res);
    res = request_irq(APMAS_IRQ, msc_interrupt_9, SA_INTERRUPT,
		      "apmas", NULL);
    if (res != 0)
	printk("couldn't register MSC interrupt 9: error=%d\n", res);
    restore_flags(flags);

    MSC_OUT(MSC_MASCTRL, 0);

    /* Enable all MSC+ interrupts (for now) */
    MSC_OUT(MSC_INTR, 
	    (AP_CLR_INTR_MASK << MSC_INTR_QBMFUL_SH) |
	    (AP_CLR_INTR_MASK << MSC_INTR_SQFILL_SH) |
	    (AP_CLR_INTR_MASK << MSC_INTR_RBMISS_SH) |
	    (AP_CLR_INTR_MASK << MSC_INTR_RBFULL_SH) |
	    (AP_CLR_INTR_MASK << MSC_INTR_RMASF_SH) |
	    (AP_CLR_INTR_MASK << MSC_INTR_RMASE_SH) |
	    (AP_CLR_INTR_MASK << MSC_INTR_SMASF_SH) |
	    (AP_CLR_INTR_MASK << MSC_INTR_SMASE_SH));

    /* setup invalid contexts */
    for (i=0; i<MSC_NR_RBUFS; i++)
      MSC_OUT(ringbufs[i].rbmmode, NULL_CONTEXT);

    MSC_OUT(MSC_SMASREG,0);
    MSC_OUT(MSC_RMASREG,0);

    if (!system_ringbuf.ringbuf) {
      system_ringbuf.ringbuf = 
	(void *)__get_free_pages(GFP_ATOMIC,SYSTEM_RINGBUF_ORDER);
      for (i=MAP_NR(system_ringbuf.ringbuf);
	   i<=MAP_NR(system_ringbuf.ringbuf+SYSTEM_RINGBUF_SIZE-1);i++)
	set_bit(PG_reserved, &mem_map[i].flags);
      system_ringbuf.write_ptr = mmu_v2p((unsigned)system_ringbuf.ringbuf)<<1;
    }

    if (!dummy_ringbuf.ringbuf) {
      dummy_ringbuf.ringbuf = 
	(void *)__get_free_pages(GFP_ATOMIC,DUMMY_RINGBUF_ORDER);
      for (i=MAP_NR(dummy_ringbuf.ringbuf);
	   i<=MAP_NR(dummy_ringbuf.ringbuf+DUMMY_RINGBUF_SIZE-1);i++)
	set_bit(PG_reserved, &mem_map[i].flags);
      dummy_ringbuf.write_ptr = mmu_v2p((unsigned)dummy_ringbuf.ringbuf)<<1;
    }
}


static inline void qbmfill_interrupt(void)
{
	MSC_OUT(MSC_INTR, AP_CLR_INTR_MASK << MSC_INTR_QBMFUL_SH);
	intr_mask &= ~(AP_INTR_REQ << MSC_INTR_QBMFUL_SH);

	SQ_NEW_MODE(THRU);	/* set send queue ctrlr to through mode */
	refill_sq();		/* refill the send queues */
	SQ_NEW_MODE(NORMAL);	/* set send queue ctrlr back to normal mode */
	/* dismiss the interrupt */
	MSC_OUT(MSC_INTR, AP_CLR_INTR_REQ << MSC_INTR_SQFILL_SH);
}

static inline void qbmful_interrupt(void)
{
	int nvalid, ntot, q;

	qbm_full_counter++;
		
#if DEBUG
	printk("qbm full interrupt\n"); 
#endif

	SQ_NEW_MODE(THRU);	/* set send queue ctrlr to through mode */
	/* stuff as much as we can into the send queue RAM */
	refill_sq();
	/* count how many valid words are left in the qof buffer */
	nvalid = 0;
	for (q = 0; q < NSENDQUEUES; ++q)
		nvalid += qof_present[q];
	if (nvalid >= QOF_GREEN_NELT) {
#if DEBUG
		printk("send queue overflow buffer overflow\n");
#endif
		MSC_OUT(MSC_INTR, AP_SET_INTR_MASK << MSC_INTR_QBMFUL_SH);
		intr_mask |= (AP_INTR_REQ << MSC_INTR_QBMFUL_SH);
		current->need_resched = 1;
		block_parallel_tasks = 1;
		mark_bh(TQUEUE_BH);
	}
	ntot = qof_new - qof_rp;	/* total # words of qof buf used */
	if (ntot - nvalid >= REPACK_THRESH || ntot >= QOF_GREEN_NELT
	    || (ntot > nvalid && nvalid >= QOF_GREEN_NELT - REPACK_THRESH)) {
		repack_qof();
		if (qof_new - qof_rp != nvalid) {
			printk("MSC: qof_present wrong\n");
		}
	} else if (nvalid > 0) {
		shuffle_qof();
	}
	/* N.B. if nvalid == 0, msc_refill_sq has already reset the QBM's WP */
	SQ_NEW_MODE(NORMAL);

	/* dismiss the interrupt */
	MSC_OUT(MSC_INTR, AP_CLR_INTR_REQ << MSC_INTR_QBMFUL_SH);
}


static void msc_interrupt_11(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned intr;
	unsigned long flags;

	save_flags(flags); cli();

	if (intr_counter++ == INTR_LIMIT) {
		mask_all_interrupts();
		printk("too many MSC interrupts\n");
		restore_flags(flags); 
		return;
	}

	intr = MSC_IN(MSC_INTR);

#if DEBUG
	printk("CID(%d) msc_interrupt_11: intr = %x\n", mpp_cid(), intr);
#endif

	if (intr & (AP_INTR_REQ << MSC_INTR_RBMISS_SH)) {
		int context;
		context = MSC_IN(MSC_RMASREG) >> 20;
		
		msc_set_ringbuf(context);
		MSC_OUT(MSC_INTR, AP_CLR_INTR_REQ << MSC_INTR_RBMISS_SH);
	}
	
	if (intr & (AP_INTR_REQ << MSC_INTR_RBFULL_SH)) {
		int context = MSC_IN(MSC_RMASREG) >> 20;
		msc_update_read_ptr(context,1);	
		MSC_OUT(MSC_INTR, AP_CLR_INTR_REQ << MSC_INTR_RBFULL_SH);
	}
	
	if (intr & (AP_INTR_REQ << MSC_INTR_SQFILL_SH)) {
		qbmfill_interrupt();
	}
	
	if (intr & (AP_INTR_REQ << MSC_INTR_QBMFUL_SH)) {
		qbmful_interrupt();
	}

	restore_flags(flags);
}


void msc_timer(void)
{
	/* unmask all the interrupts that are supposed to be unmasked */
	intr_counter = 0;
}

/* assumes NSENDQUEUES == 5 */
static int log2tbl[32] = {
    -1,  0,  1, -1,  2, -1, -1, -1,
     3, -1, -1, -1, -1, -1, -1, -1,
     4, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1
};

static unsigned long direct_queues[NSENDQUEUES][2] = {
    { MSC_SYSTEM_DIRECT, MSC_SYSTEM_DIRECT_END },
    { MSC_REMOTE_DIRECT, MSC_REMOTE_DIRECT_END },
    { MSC_USER_DIRECT, MSC_USER_DIRECT_END },
    { MSC_REMREPLY_DIRECT, MSC_REMREPLY_DIRECT_END },
    { MSC_REPLY_DIRECT, MSC_REPLY_DIRECT_END }
};

/*
 * Copy entries from the queue overflow buffer back to the send queue.
 * This must be called with the send queue controller in THRU mode.
 */
static void refill_sq(void)
{
    int notfull, use_old;
    int q, kept_some;
    int sqp, sqc;
    struct qof_elt *rp, *qof_wp;
    int freew[NSENDQUEUES];	/* # free words in each queue */

    /* give parallel tasks another chance */
    block_parallel_tasks = 0;

    /* get the qbm's write pointer */
    qof_wp = qof_base + (EXTFIELD(MSC_IN(MSC_QBMPTR), MSC_QBMP_WP)
			 & ((QOF_SIZE - 1) >> 3));
#if 0
    printk("refill_sq: rp=%p new=%p wp=%p pres=[",
	   qof_rp, qof_new, qof_wp);
    for (q = 0; q < NSENDQUEUES; ++q)
	printk("%d ", qof_present[q]);
    printk("]\n");
#endif

    /* work out which send queues and aren't full */
    notfull = 0;
    use_old = 0;
    for (q = 0; q < NSENDQUEUES; ++q) {
	sqp = MSC_IN(MSC_SQPTR0 + q * 8);
	freew[q] = (EXTFIELD(sqp, MSC_SQP_RP) - EXTFIELD(sqp, MSC_SQP_WP) - 1)
	    & ((sqp & MSC_SQP_MODE)? 0x3f: 0x1f);
	if (freew[q] > 0)
	    notfull |= 1 << (q + QOF_QUEUE_SH);
	use_old += (freew[q] < qof_present[q])? freew[q]: qof_present[q];
    }

    /*
     * If there are useful entries in the old part of the overflow
     * queue, process them.
     */
    kept_some = 0;
    for (rp = qof_rp; rp < qof_new && use_old > 0; ++rp) {
	if (rp->info & notfull) {
	    /* Here's one we can stuff back into the send queue */
	    q = log2tbl[EXTFIELD(rp->info, QOF_QUEUE)];
	    if (q < 0) {
		printk("bad queue bits in qof info (%x) at %p\n",
		       rp->info, rp);
		/* XXX just ignore this entry - should never happen */
		rp->info = 0;
		continue;
	    }
	    MSC_OUT(direct_queues[q][rp->info & QOF_ENDBIT],rp->data);
	    if (--freew[q] == 0)
		notfull &= ~(1 << (q + QOF_QUEUE_SH));
	    --qof_present[q];
	    --use_old;
	    rp->info = 0;
	} else if (!kept_some && rp->info != 0) {
	    qof_rp = rp;
	    kept_some = 1;
	}
    }

    /* Trim off any further already-used items. */
    if (!kept_some) {
	for (; rp < qof_new; ++rp) {
	    if (rp->info) {
		qof_rp = rp;
		kept_some = 1;
		break;
	    }
	}
    }

    /*
     * Now process everything that's arrived since we last updated qof_new.
     */
    for (rp = qof_new; rp < qof_wp; ++rp) {
      if (rp->info == 0)
	continue;
	q = log2tbl[EXTFIELD(rp->info, QOF_QUEUE)];
	if (q < 0) {
	    printk("bad queue bits in qof info (%x) at %p\n", rp->info, rp);
	    /* XXX just ignore this entry - should never happen */
	    rp->info = 0;
	    continue;
	}
	if (rp->info & notfull) {
	  /* Another one to stuff back into the send queue. */
	  MSC_OUT(direct_queues[q][rp->info & QOF_ENDBIT],rp->data);
	  if (--freew[q] == 0)
	    notfull &= ~(1 << (q + QOF_QUEUE_SH));
	  rp->info = 0;
	} else {
	    ++qof_present[q];
	    if (!kept_some) {
		qof_rp = rp;
		kept_some = 1;
	    }
	}
    }

    /* Update state and the MSC queue-spill flags. */
    if (!kept_some) {
	/* queue is empty; avoid unnecessary overflow interrupt later */
	qof_rp = qof_new = qof_base;
	QBM_UPDATE_WP(mmu_v2p((unsigned long)qof_base));
    } else {
	qof_new = qof_wp;
    }

    sqc = MSC_IN(MSC_SQCTRL);
    for (q = 0; q < NSENDQUEUES; ++q)
	if (qof_present[q] == 0 && freew[q] > 0)
	    sqc &= ~(1 << (q + MSC_SQC_SPLF_SH));
    MSC_OUT(MSC_SQCTRL, sqc);
}

/*
 * Copy the valid entries from their current position
 * in the queue overflow buffer to the beginning.
 * This must be called with the send queue controller in THRU or BLOCKING mode.
 */
static void repack_qof(void)
{
    struct qof_elt *rp, *wp;

    wp = qof_base;
    for (rp = qof_rp; rp < qof_new; ++rp) {
	if (rp->info) {
	    if (rp > wp)
		*wp = *rp;
	    ++wp;
	}
    }
    qof_rp = qof_base;
    qof_new = wp;
    QBM_UPDATE_WP(wp);
}

/*
 * Copy all entries from their current position
 * in the queue overflow buffer to the beginning.
 * This must be called with the send queue controller in THRU or BLOCKING mode.
 */
static void shuffle_qof(void)
{
    int n;

    n = qof_new - qof_rp;
    memmove(qof_base, qof_rp, n * sizeof(struct qof_elt));
    qof_rp = qof_base;
    qof_new = qof_base + n;
    QBM_UPDATE_WP(qof_new);
}

static inline void handle_signal(int context,unsigned vaddr)
{
  int signum = (vaddr - MSC_REM_SIGNAL) >> PAGE_SHIFT;
  int taskid = MPP_CTX_TO_TASK(context);
  if (MPP_IS_PAR_TASK(taskid) && valid_task(task[taskid])) {
    send_sig(signum,task[taskid],1);
#if DEBUG
    printk("CID(%d) sent signal %d to task %d\n",mpp_cid(),signum,taskid);
#endif
  }
}


/*
 * fail a msc write operation. We use Pauls dirty tlb trick to avoide
 * the msc hardware bugs 
 */
static void fail_write(int context,int intr,unsigned vaddr)
{
	int tsk = MPP_CTX_TO_TASK(context);
	int vpage = vaddr >> 12;
#if DEBUG
	printk("fail write tsk=%d intr=%x vaddr=%x RMASREG=%x errproc=%x\n",
	       tsk,intr,vaddr,MSC_IN(MSC_RMASREG),MSC_IN(MSC_RHDERRPROC));
#endif

	mc_tlb_map(0x800000 | (mmu_v2p((unsigned)dummy_ringbuf.ringbuf)>>12),
		   vpage,context);
	MSC_OUT(MSC_MASCTRL, MSC_IN(MSC_MASCTRL) & ~MSC_MASC_RFEXIT);
	MSC_OUT(MSC_INTR, AP_CLR_INTR_REQ << intr);

	mc_tlb_unmap(vpage);	

	if (MPP_IS_PAR_CTX(context) && valid_task(task[tsk])) {
		if (vaddr - MSC_REM_SIGNAL < _NSIG*PAGE_SIZE) {
			handle_signal(context,vaddr);
		} else {
			task[tsk]->tss.sig_address = vaddr;
			task[tsk]->tss.sig_desc = SUBSIG_NOMAPPING;
			send_sig(SIGSEGV, task[tsk], 1);
		}
	}
}

/*
 * fail a msc read operation using the tlb trick */
static void fail_read(int context,int intr,unsigned vaddr)
{
	int tsk = MPP_CTX_TO_TASK(context);  
#if DEBUG
	printk("fail read tsk=%d intr=%x\n",tsk,intr);
#endif
	
	mc_tlb_map(0x800000 | (mmu_v2p((unsigned)dummy_ringbuf.ringbuf)>>12),
		   vaddr>>12,context);
	MSC_OUT(MSC_MASCTRL, MSC_IN(MSC_MASCTRL) & ~MSC_MASC_SFEXIT);
	MSC_OUT(MSC_INTR, AP_CLR_INTR_REQ << intr);    
	
	mc_tlb_unmap(vaddr>>12);

	if (MPP_IS_PAR_CTX(context) && valid_task(task[tsk])) {
		if (vaddr - MSC_REM_SIGNAL < _NSIG*PAGE_SIZE) {
			handle_signal(context,vaddr);
		} else {
			task[tsk]->tss.sig_address = vaddr;
			task[tsk]->tss.sig_desc = SUBSIG_NOMAPPING;
			send_sig(SIGSEGV, task[tsk], 1);
		}
	}	
}

static void async_callback(int tsk,unsigned long vaddr,int write,int ret)
{
	unsigned flags;
	save_flags(flags); cli();

	msc_blocked--;
	if (write) {
		intr_mask &= ~(AP_INTR_REQ << MSC_INTR_RMASF_SH);
		if (ret) {
			fail_write(MPP_TASK_TO_CTX(tsk),MSC_INTR_RMASF_SH,vaddr);
			MSC_OUT(MSC_INTR, AP_CLR_INTR_MASK << MSC_INTR_RMASF_SH);
			restore_flags(flags);
			return;
		}
		MSC_OUT(MSC_MASCTRL, MSC_IN(MSC_MASCTRL) & ~MSC_MASC_RFEXIT);
		MSC_OUT(MSC_INTR, AP_CLR_INTR_REQ << MSC_INTR_RMASF_SH);
		MSC_OUT(MSC_INTR, AP_CLR_INTR_MASK << MSC_INTR_RMASF_SH);
	} else {
		intr_mask &= ~(AP_INTR_REQ << MSC_INTR_SMASF_SH);
		if (ret) {
			fail_read(MPP_TASK_TO_CTX(tsk),MSC_INTR_SMASF_SH,vaddr);
			MSC_OUT(MSC_INTR, AP_CLR_INTR_MASK << MSC_INTR_SMASF_SH);
			restore_flags(flags);
			return;
		}
		MSC_OUT(MSC_MASCTRL, MSC_IN(MSC_MASCTRL) & ~MSC_MASC_SFEXIT);
		MSC_OUT(MSC_INTR, AP_CLR_INTR_REQ << MSC_INTR_SMASF_SH);
		MSC_OUT(MSC_INTR, AP_CLR_INTR_MASK << MSC_INTR_SMASF_SH);
	}
	restore_flags(flags);
}



static inline void msc_write_fault(void)
{
	unsigned context = MSC_IN(MSC_RMASREG) >> 20;
	unsigned vaddr = MSC_IN(MSC_RMASTWP)<<12;
	
	if (context == SYSTEM_CONTEXT) {
		fail_write(context,MSC_INTR_RMASF_SH,vaddr);
		show_mapping_ctx(0,context,vaddr);
		printk("ERROR: system write fault at %x\n",vaddr);
		return;
	}
		
	if (vaddr - MSC_REM_SIGNAL < _NSIG*PAGE_SIZE) {
		fail_write(context,MSC_INTR_RMASF_SH,vaddr);
		return;
	}
		
	if (MPP_IS_PAR_CTX(context)) {
		int tsk = MPP_CTX_TO_TASK(context);
		if (valid_task(task[tsk]) && task[tsk]->ringbuf) {
			MSC_OUT(MSC_INTR, 
				AP_SET_INTR_MASK << MSC_INTR_RMASF_SH);
			intr_mask |= (AP_INTR_REQ << MSC_INTR_RMASF_SH);
#if DEBUG
			show_mapping_ctx(0,context,vaddr);
#endif
			msc_blocked++;
			async_fault(vaddr,1,tsk,async_callback);
			return;
		}
	}
	
#if DEBUG
	printk("CID(%d) mas write fault context=%x vaddr=%x\n",
	       mpp_cid(),context,vaddr);
#endif
	
	fail_write(context,MSC_INTR_RMASF_SH,vaddr);
}


static inline void msc_read_fault(void)
{
	unsigned context = MSC_IN(MSC_SMASREG) >> 20;
	unsigned vaddr = MSC_IN(MSC_SMASTWP)<<12;
		
	if (context == SYSTEM_CONTEXT) {
		fail_read(context,MSC_INTR_SMASF_SH,vaddr);
		show_mapping_ctx(0,context,vaddr);
		printk("ERROR: system read fault at %x\n",vaddr);
		return;
	}

	if (MPP_IS_PAR_CTX(context)) {
		int tsk = MPP_CTX_TO_TASK(context);
		
		if (vaddr - MSC_REM_SIGNAL < _NSIG*PAGE_SIZE) {
			fail_read(context,MSC_INTR_SMASF_SH,vaddr);
			return;
		}

		if (valid_task(task[tsk]) && task[tsk]->ringbuf) {
			MSC_OUT(MSC_INTR, AP_SET_INTR_MASK << MSC_INTR_SMASF_SH);
			intr_mask |= (AP_INTR_REQ << MSC_INTR_SMASF_SH);
			msc_blocked++;
			async_fault(vaddr,0,tsk,async_callback);
			return;
		}
	}
	
#if DEBUG
	printk("CID(%d) mas read fault context=%x vaddr=%x\n",
	       mpp_cid(),context,vaddr);
#endif
	
	fail_read(context,MSC_INTR_SMASF_SH,vaddr);
}
	


static void msc_interrupt_9(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	unsigned intr, cnt, r;

	save_flags(flags); cli();

	if (intr_counter++ == INTR_LIMIT) {
		mask_all_interrupts();
		printk("too many MSC interrupts\n");
		restore_flags(flags); 
		return;
	}

	intr = MSC_IN(MSC_INTR) & ~intr_mask;    

#if DEBUG
	printk("CID(%d) msc_interrupt_9: intr = %x\n", mpp_cid(), intr);
#endif
	
	if (intr & (AP_INTR_REQ << MSC_INTR_RMASF_SH)) {
		msc_write_fault();
	}
	
	if (intr & (AP_INTR_REQ << MSC_INTR_SMASF_SH)) {
		msc_read_fault();
	}

	if (intr & (AP_INTR_REQ << MSC_INTR_RMASE_SH)) {
		printk("recv mas error interrupt (write)\n");
		printk("masctrl = %x\n", MSC_IN(MSC_MASCTRL));
		printk("rmasadr = %x %x\n", MSC_IN(MSC_RMASADR),
		       MSC_IN(MSC_RMASADR + 4));
		printk("rmastwp = %x\n", MSC_IN(MSC_RMASTWP));
		printk("rmasreg = %x\n", MSC_IN(MSC_RMASREG));
		r = MSC_IN(MSC_RMASREG);
		if ((r & MSC_MASR_AVIO) || (r & MSC_MASR_CMD) != MSC_MASR_CMD_XFER)
			/* throw away the rest of the incoming data */
			MSC_OUT(MSC_RHDERRPROC, 0);
		/* clear the interrupt */
		MSC_OUT(MSC_INTR, AP_CLR_INTR_REQ << MSC_INTR_RMASE_SH);
	}
	
	if (intr & (AP_INTR_REQ << MSC_INTR_SMASE_SH)) {
		printk("send mas error interrupt (read)\n");
		printk("masctrl = %x\n", MSC_IN(MSC_MASCTRL));
		printk("smasadr = %x %x\n", MSC_IN(MSC_SMASADR),
		       MSC_IN(MSC_SMASADR + 4));
		printk("smascnt = %x\n", MSC_IN(MSC_SMASCNT));
		printk("smastwp = %x\n", MSC_IN(MSC_SMASTWP));
		printk("smasreg = %x\n", MSC_IN(MSC_SMASREG));
		/* supply dummy data */
		cnt = MSC_IN(MSC_SMASCNT);
		switch (MSC_IN(MSC_SMASREG) & MSC_MASR_CMD) {
		case MSC_MASR_CMD_XFER:
			MSC_OUT(MSC_HDGERRPROC, (EXTFIELD(cnt, MSC_SMCT_MCNT)
						 + EXTFIELD(cnt, MSC_SMCT_ICNT)));
			break;
			/* case remote read: */
		case MSC_MASR_CMD_FOP:
		case MSC_MASR_CMD_CSI:
			MSC_OUT(MSC_HDGERRPROC, 1);
			break;
		}
		/* clear interrupt */
		MSC_OUT(MSC_INTR, AP_CLR_INTR_REQ << MSC_INTR_SMASF_SH);
	}
	
	restore_flags(flags);
}

/*
 * remove access to a tasks ring buffer
 */
void msc_unset_ringbuf(int i)
{
	int ctx = MSC_IN(ringbufs[i].rbmmode) & CTX_MASK;
	int tsk = MPP_CTX_TO_TASK(ctx);
	struct ringbuf_struct *rbuf;
  
#if DEBUG
	printk("msc_unset_ringbuf(%d) %x\n",i,ctx);
#endif

	MSC_OUT(ringbufs[i].rbmmode,NULL_CONTEXT);
	if (ctx == SYSTEM_CONTEXT) { 
		rbuf = &system_ringbuf;
	} else if (ctx != NULL_CONTEXT && MPP_IS_PAR_CTX(ctx) && 
		   valid_task(task[tsk]) && task[tsk]->ringbuf) {
		rbuf = task[tsk]->ringbuf;
	} else if (ctx != NULL_CONTEXT && MPP_IS_PAR_CTX(ctx) &&
		   valid_task(task[tsk]) && task[tsk]->aplib) {
		rbuf = &system_ringbuf;
	} else {
		rbuf = &dummy_ringbuf;
	}

	rbuf->write_ptr = MSC_IN(ringbufs[i].rbmbwp);
}

static void msc_update_read_ptr(int context,int overflow)
{
	int i;
	unsigned new_read_ptr;

	for (i=0;i<NR_RBUFS;i++) {
		if ((MSC_IN(ringbufs[i].rbmmode)&CTX_MASK) == context) break;
	}
	if (i == NR_RBUFS) {
		printk("didn't find context %d in msc_update_read_ptr()\n",
		       context);
		return;
	}

	if (context == SYSTEM_CONTEXT) {
		tnet_check_completion();
		if (overflow && MSC_IN(ringbufs[i].rbmrp) == system_read_ptr)
			printk("system ringbuffer overflow\n");
		new_read_ptr = system_read_ptr;
	} else if (MPP_IS_PAR_CTX(context) && 
		   valid_task(task[MPP_CTX_TO_TASK(context)]) &&
		   task[MPP_CTX_TO_TASK(context)]->ringbuf) {
		struct task_struct *tsk = task[MPP_CTX_TO_TASK(context)];
		struct _kernel_cap_shared *_kernel;
		unsigned soft_read_ptr;
		unsigned octx;

		octx = apmmu_get_context();
		if (octx != context) 
			apmmu_set_context(context);
		_kernel = (struct _kernel_cap_shared *)(RBUF_VBASE + RBUF_SHARED_PAGE_OFF);
		soft_read_ptr = _kernel->rbuf_read_ptr;
		if (octx != context) 
			apmmu_set_context(octx);

		if (overflow && MSC_IN(ringbufs[i].rbmrp) == soft_read_ptr) {
			/* send them a SIGLOST and wipe their ring buffer */
			printk("ring buffer overflow for %s ctx=%x\n",
			       tsk->comm,context);
			send_sig(SIGLOST,tsk,1);
			soft_read_ptr--;
		}
		new_read_ptr = soft_read_ptr;
	} else if (MPP_IS_PAR_CTX(context) && 
		   valid_task(task[MPP_CTX_TO_TASK(context)]) &&
		   task[MPP_CTX_TO_TASK(context)]->aplib) {
		tnet_check_completion();
		if (overflow && MSC_IN(ringbufs[i].rbmrp) == system_read_ptr)
                        printk("system ringbuffer overflow\n");
                new_read_ptr = system_read_ptr;		
	} else {
		dummy_read_ptr = MSC_IN(ringbufs[i].rbmrp) - 1;
		new_read_ptr = dummy_read_ptr - 1;
#if DEBUG
		if (overflow)
			printk("reset dummy ring buffer for context %x\n",
			       context);
#endif
	}


	MSC_OUT(ringbufs[i].rbmrp,new_read_ptr);
}

/*
 * give a task one of the system ring buffers 
 * this is called on a context miss interrupt, so we can assume that
 * the tasks context is not currently set in one of the ringbufs
 */
static void msc_set_ringbuf(int context)
{
	int i;
	int ctx;
	int mode;
	unsigned write_ptr;
	static unsigned next_ctx = 0;
	struct ringbuf_struct *rbuf;

	if (context == SYSTEM_CONTEXT) {
		rbuf = &system_ringbuf;
	} else if (MPP_IS_PAR_CTX(context) && 
		   valid_task(task[MPP_CTX_TO_TASK(context)]) &&
		   task[MPP_CTX_TO_TASK(context)]->ringbuf) {
		struct task_struct *tsk = task[MPP_CTX_TO_TASK(context)];
		rbuf = tsk->ringbuf;
	} else if (MPP_IS_PAR_CTX(context) && 
		   valid_task(task[MPP_CTX_TO_TASK(context)]) &&
		   task[MPP_CTX_TO_TASK(context)]->aplib) {
		rbuf = &system_ringbuf;
	} else {
		/* use the dummy ring buffer */
		rbuf = &dummy_ringbuf;
	}

	for (i=0;i<NR_RBUFS; i++) {
		ctx = MSC_IN(ringbufs[i].rbmmode)&CTX_MASK;
		if (ctx == NULL_CONTEXT) break;
	}
	if (i == NR_RBUFS) {
		i = next_ctx;
		next_ctx = (i+1)%NR_RBUFS;
	}

	ctx = MSC_IN(ringbufs[i].rbmmode)&CTX_MASK;
	if (ctx != NULL_CONTEXT) msc_unset_ringbuf(i);

	write_ptr = rbuf->write_ptr;
	mode = (rbuf->order - 5) >> 1;
	
	MSC_OUT(ringbufs[i].rbmmode,context | (mode << 12));
	MSC_OUT(ringbufs[i].rbmbwp,write_ptr);

	if (rbuf == &system_ringbuf) {
		MSC_OUT(ringbufs[i].rbmrp,system_read_ptr);
	} else {
		msc_update_read_ptr(context,0);
	}
	
#if DEBUG
	printk("CID(%d) mapped ringbuf for context %d in slot %d\n",
	       mpp_cid(),context,i);
#endif
}


/*
 * this is called when a task exits
*/
void exit_msc(struct task_struct *tsk)
{
	int i;

	if (!MPP_IS_PAR_TASK(tsk->taskid))
		return;

#if DEBUG
	printk("exit_msc(%d) ctx=%d\n",tsk->taskid,tsk->mm->context);
#endif

	for (i=0;i<NR_RBUFS; i++) {
		int ctx = MSC_IN(ringbufs[i].rbmmode)&CTX_MASK;
		if (ctx == MPP_TASK_TO_CTX(tsk->taskid))
			msc_unset_ringbuf(i);
	}
	msc_switch_from_check(tsk);

	/* stop it receiving new-style messages */
	tsk->aplib = NULL;

	exit_ringbuf(tsk);
}


static void msc_sq_pause(void)
{
	MSC_OUT(MSC_SQCTRL,MSC_IN(MSC_SQCTRL) | MSC_SQC_PAUSE);
	while (!(MSC_IN(MSC_SQCTRL) & MSC_SQC_STABLE)) /* wait for stable bit */ ;
}

static void msc_sq_resume(void)
{
	MSC_OUT(MSC_SQCTRL,MSC_IN(MSC_SQCTRL) & ~MSC_SQC_PAUSE);
}

static void msc_switch_from_check(struct task_struct *tsk)
{
	int user_count;
	unsigned flags;
	struct ringbuf_struct *rbuf = NULL;
	int octx, ctx;

	if (valid_task(tsk) && tsk->ringbuf)
		rbuf = tsk->ringbuf;
	
	/* it doesn't seem obvious why this field should contain count+1,
	   but it does */
	user_count = EXTFIELD(MSC_IN(MSC_QWORDCNT),MSC_QWDC_USRCNT) - 1;
	
	/* check if the user queue count is != 0 */
	if (user_count == 0) return;

	if (!rbuf)
		printk("switching from dead task\n");
	
#if 1
	printk("saving %d words MSC_QWORDCNT=%x\n",
	       user_count,MSC_IN(MSC_QWORDCNT));
#endif
	
	/* bugger - we have to do some messy work */
	save_flags(flags); cli();

	ctx = MPP_TASK_TO_CTX(tsk->taskid);
	octx = apmmu_get_context();
	if (octx != ctx)
		apmmu_set_context(ctx);

	msc_sq_pause();
	
	/* remember the expected length of the command - usually (always?) 8 */
	if (rbuf)
		rbuf->frag_len = EXTFIELD(MSC_IN(MSC_QWORDCNT),MSC_QWDC_USRLEN);
	
	/* pull words from the overflow first */
	if (MSC_IN(MSC_SQCTRL) & MSC_SQC_USERF) {
		/* we have overflowed */
		struct qof_elt *qof_wp = qof_base + 
			(EXTFIELD(MSC_IN(MSC_QBMPTR), MSC_QBMP_WP) & ((QOF_SIZE - 1) >> 3));
		while (qof_wp != qof_rp && user_count) {
			qof_wp--;
			/* only grab elements in the user queue */
			if (qof_wp->info && log2tbl[EXTFIELD(qof_wp->info, QOF_QUEUE)] == 2) {
				if (qof_wp->info & 1) {
					printk("MSC: end bit set - yikes!\n");
				}
				qof_wp->info = 0;
				if (rbuf) {
					rbuf->sq_fragment[--user_count] = qof_wp->data;
					rbuf->frag_count++;
				}
				if (qof_wp < qof_new)
					qof_present[2]--;
			}
		}
#if DEBUG
		if (rbuf)
			printk("pulled %d elements from overflow (%d left)\n",
			       rbuf->frag_count,user_count);
#endif
	}
	
	/* then pull words direct from the msc ram */
	if (user_count) {
		int wp = EXTFIELD(MSC_IN(MSC_SQPTR2),MSC_SQP_WP);
		int i;
		wp -= user_count;
		if (wp < 0) wp += send_queues[2].size;
		
		for (i=0;i<user_count;i++) {
			int wp2 = (wp + i)%send_queues[2].size;
			if (rbuf)
				rbuf->sq_fragment[i + rbuf->frag_count] = 
					MSC_IN(MSC_SQRAM + (send_queues[2].base + wp2)*8);
		}
		
		if (rbuf)
			rbuf->frag_count += user_count;
		
		MSC_OUT(MSC_SQPTR2,INSFIELD(MSC_IN(MSC_SQPTR2),wp,MSC_SQP_WP));
#if DEBUG
		printk("saved %d words from msc ram\n",rbuf->frag_count);
#endif
	}
  
	/* reset the user count to 1 */
	MSC_OUT(MSC_QWORDCNT,INSFIELD(MSC_IN(MSC_QWORDCNT),1,MSC_QWDC_USRCNT));
	
	msc_sq_resume();

	if (octx != ctx)
		apmmu_set_context(octx);

	restore_flags(flags);
}

static void msc_switch_to_check(struct task_struct *tsk)
{
	int i;
	unsigned flags;
	int octx, ctx;
	
	if (!valid_task(tsk) || !tsk->ringbuf)
		return;

	save_flags(flags); cli();

	
	ctx = MPP_TASK_TO_CTX(tsk->taskid);
	octx = apmmu_get_context();
	if (octx != ctx)
		apmmu_set_context(ctx);

	/* if the task we are switching to has no saved words then 
	   we're finished */
	if (tsk->ringbuf->frag_count == 0) {
		if (octx != ctx)
			apmmu_set_context(octx);
		restore_flags(flags);
		return;
	}
	

#if 1
	printk("frag fill MSC_QWORDCNT=%x frag_count=%d\n",
	       MSC_IN(MSC_QWORDCNT),tsk->ringbuf->frag_count);
#endif	
	
	/* reset the user length */
	MSC_OUT(MSC_QWORDCNT,INSFIELD(MSC_IN(MSC_QWORDCNT),
				      tsk->ringbuf->frag_len,
				      MSC_QWDC_USRLEN));
	
	/* push the words into the direct queue */
	for (i=0;i<tsk->ringbuf->frag_count;i++)
		MSC_OUT(MSC_USER_DIRECT,tsk->ringbuf->sq_fragment[i]);
	
	/* reset the user count */
	MSC_OUT(MSC_QWORDCNT,INSFIELD(MSC_IN(MSC_QWORDCNT),
				      1+tsk->ringbuf->frag_count,
				      MSC_QWDC_USRCNT));
	
#if DEBUG
	printk("frag fill done MSC_QWORDCNT=%x\n",
	       MSC_IN(MSC_QWORDCNT));
#endif
	
	tsk->ringbuf->frag_count = 0;
	tsk->ringbuf->frag_len = 0;
	if (octx != ctx)
		apmmu_set_context(octx);
	restore_flags(flags);
}



void msc_switch_check(struct task_struct *tsk)
{
	static int last_task = 0;

	if (last_task == tsk->taskid) return;

	if (MPP_IS_PAR_TASK(last_task))
		msc_switch_from_check(task[last_task]);

	msc_switch_to_check(tsk);
	
	last_task = tsk->taskid;
}

/* we want to try to avoid task switching while there are partial commands
   in the send queues */
int msc_switch_ok(void)
{
	if ((EXTFIELD(MSC_IN(MSC_QWORDCNT),MSC_QWDC_USRCNT) - 1))
		return 0;

	return 1;
}

/*
 * print out the state of the msc
*/
static void msc_status(void)
{
	int i;

	printk("MSC_SQCTRL=%x\n",MSC_IN(MSC_SQCTRL));
  
	for (i=0;i<5;i++)
		printk("MSC_SQPTR%d=%x\n",i,MSC_IN(MSC_SQPTR0 + 8*i));  
	printk("MSC_OPTADR=%x\n",MSC_IN(MSC_OPTADR));  
	printk("MSC_MASCTRL=%x\n", MSC_IN(MSC_MASCTRL));
	printk("MSC_SMASADR=%x_%x\n", MSC_IN(MSC_SMASADR),MSC_IN(MSC_SMASADR + 4));
	printk("MSC_RMASADR=%x_%x\n", MSC_IN(MSC_RMASADR),MSC_IN(MSC_RMASADR + 4));
	printk("MSC_PID=%x\n",MSC_IN(MSC_PID));
	
	printk("MSC_QWORDCNT=%x\n",MSC_IN(MSC_QWORDCNT));
	
	printk("MSC_INTR=%x\n",MSC_IN(MSC_INTR));
	printk("MSC_CIDRANGE=%x\n",MSC_IN(MSC_CIDRANGE));
	printk("MSC_QBMPTR=%x\n",MSC_IN(MSC_QBMPTR));
	printk("MSC_SMASTWP=%x\n", MSC_IN(MSC_SMASTWP));
	printk("MSC_RMASTWP=%x\n", MSC_IN(MSC_RMASTWP));
	printk("MSC_SMASREG=%x\n", MSC_IN(MSC_SMASREG));
	printk("MSC_RMASREG=%x\n", MSC_IN(MSC_RMASREG));
	printk("MSC_SMASCNT=%x\n", MSC_IN(MSC_SMASCNT));
	printk("MSC_IRL=%x\n", MSC_IN(MSC_IRL));
	printk("MSC_SIMMCHK=%x\n", MSC_IN(MSC_SIMMCHK));
	
	for (i=0;i<3;i++) {
		printk("RBMBWP%d=%x\n",i,MSC_IN(ringbufs[i].rbmbwp));
		printk("RBMMODE%d=%x\n",i,MSC_IN(ringbufs[i].rbmmode));
		printk("RBMRP%d=%x\n",i,MSC_IN(ringbufs[i].rbmrp));
	}
	
	printk("DMA_GEN=%x\n",MSC_IN(DMA_GEN));

	printk("qbm_full_counter=%d\n",qbm_full_counter);
}
