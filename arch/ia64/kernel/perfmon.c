/*
 * This file implements the perfmon subsystem which is used
 * to program the IA-64 Performance Monitoring Unit (PMU).
 *
 * Originaly Written by Ganesh Venkitachalam, IBM Corp.
 * Copyright (C) 1999 Ganesh Venkitachalam <venkitac@us.ibm.com>
 *
 * Modifications by Stephane Eranian, Hewlett-Packard Co.
 * Modifications by David Mosberger-Tang, Hewlett-Packard Co.
 *
 * Copyright (C) 1999-2002  Hewlett Packard Co
 *               Stephane Eranian <eranian@hpl.hp.com>
 *               David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/wrapper.h>
#include <linux/mm.h>

#include <asm/bitops.h>
#include <asm/errno.h>
#include <asm/page.h>
#include <asm/pal.h>
#include <asm/perfmon.h>
#include <asm/processor.h>
#include <asm/signal.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/delay.h> /* for ia64_get_itc() */

#ifdef CONFIG_PERFMON

/*
 * For PMUs which rely on the debug registers for some features, you
 * must enable the following flag to activate the support for
 * accessing the registers via the perfmonctl() interface.
 */
#ifdef CONFIG_ITANIUM
#define PFM_PMU_USES_DBR	1
#endif

/*
 * perfmon context states
 */
#define PFM_CTX_DISABLED	0
#define PFM_CTX_ENABLED		1

/*
 * Reset register flags
 */
#define PFM_RELOAD_LONG_RESET	1
#define PFM_RELOAD_SHORT_RESET	2

/*
 * Misc macros and definitions
 */
#define PMU_FIRST_COUNTER	4

#define PFM_IS_DISABLED() pmu_conf.pfm_is_disabled

#define PMC_OVFL_NOTIFY(ctx, i)	((ctx)->ctx_soft_pmds[i].flags &  PFM_REGFL_OVFL_NOTIFY)
#define PFM_FL_INHERIT_MASK	(PFM_FL_INHERIT_NONE|PFM_FL_INHERIT_ONCE|PFM_FL_INHERIT_ALL)

#define PMC_IS_IMPL(i)	  (i<pmu_conf.num_pmcs && pmu_conf.impl_regs[i>>6] & (1UL<< (i) %64))
#define PMD_IS_IMPL(i)	  (i<pmu_conf.num_pmds &&  pmu_conf.impl_regs[4+(i>>6)] & (1UL<<(i) % 64))

#define PMD_IS_COUNTING(i) (i >=0  && i < 256 && pmu_conf.counter_pmds[i>>6] & (1UL <<(i) % 64))
#define PMC_IS_COUNTING(i) PMD_IS_COUNTING(i)

#define IBR_IS_IMPL(k)	  (k<pmu_conf.num_ibrs)
#define DBR_IS_IMPL(k)	  (k<pmu_conf.num_dbrs)

#define PMC_IS_BTB(a)	  (((pfm_monitor_t *)(a))->pmc_es == PMU_BTB_EVENT)

#define LSHIFT(x)		(1UL<<(x))
#define PMM(x)			LSHIFT(x)
#define PMC_IS_MONITOR(c)	((pmu_conf.monitor_pmcs[0] & PMM((c))) != 0)

#define CTX_IS_ENABLED(c) 	((c)->ctx_flags.state == PFM_CTX_ENABLED)
#define CTX_OVFL_NOBLOCK(c)	((c)->ctx_fl_block == 0)
#define CTX_INHERIT_MODE(c)	((c)->ctx_fl_inherit)
#define CTX_HAS_SMPL(c)		((c)->ctx_psb != NULL)
#define CTX_USED_PMD(ctx,n) 	(ctx)->ctx_used_pmds[(n)>>6] |= 1UL<< ((n) % 64)

#define CTX_USED_IBR(ctx,n) 	(ctx)->ctx_used_ibrs[(n)>>6] |= 1UL<< ((n) % 64)
#define CTX_USED_DBR(ctx,n) 	(ctx)->ctx_used_dbrs[(n)>>6] |= 1UL<< ((n) % 64)
#define CTX_USES_DBREGS(ctx)	(((pfm_context_t *)(ctx))->ctx_fl_using_dbreg==1)

#define LOCK_CTX(ctx)	spin_lock(&(ctx)->ctx_lock)
#define UNLOCK_CTX(ctx)	spin_unlock(&(ctx)->ctx_lock)

#define SET_PMU_OWNER(t)    do { pmu_owners[smp_processor_id()].owner = (t); } while(0)
#define PMU_OWNER()	    pmu_owners[smp_processor_id()].owner

#define LOCK_PFS()	    spin_lock(&pfm_sessions.pfs_lock)
#define UNLOCK_PFS()	    spin_unlock(&pfm_sessions.pfs_lock)

#define PFM_REG_RETFLAG_SET(flags, val)	do { flags &= ~PFM_REG_RETFL_MASK; flags |= (val); } while(0)

/*
 * debugging
 */
#define DBprintk(a) \
	do { \
		if (pfm_debug_mode >0) { printk("%s.%d: CPU%d ", __FUNCTION__, __LINE__, smp_processor_id()); printk a; } \
	} while (0)


/* 
 * These are some helpful architected PMC and IBR/DBR register layouts
 */
typedef struct {
	unsigned long pmc_plm:4;	/* privilege level mask */
	unsigned long pmc_ev:1;		/* external visibility */
	unsigned long pmc_oi:1;		/* overflow interrupt */
	unsigned long pmc_pm:1;		/* privileged monitor */
	unsigned long pmc_ig1:1;	/* reserved */
	unsigned long pmc_es:8;		/* event select */
	unsigned long pmc_ig2:48;	/* reserved */
} pfm_monitor_t;

/*
 * There is one such data structure per perfmon context. It is used to describe the
 * sampling buffer. It is to be shared among siblings whereas the pfm_context 
 * is not.
 * Therefore we maintain a refcnt which is incremented on fork().
 * This buffer is private to the kernel only the actual sampling buffer 
 * including its header are exposed to the user. This construct allows us to 
 * export the buffer read-write, if needed, without worrying about security 
 * problems.
 */
typedef struct _pfm_smpl_buffer_desc {
	spinlock_t		psb_lock;	/* protection lock */
	unsigned long		psb_refcnt;	/* how many users for the buffer */
	int			psb_flags;	/* bitvector of flags */

	void			*psb_addr;	/* points to location of first entry */
	unsigned long		psb_entries;	/* maximum number of entries */
	unsigned long		psb_size;	/* aligned size of buffer */
	unsigned long		psb_index;	/* next free entry slot XXX: must use the one in buffer */
	unsigned long		psb_entry_size;	/* size of each entry including entry header */
	perfmon_smpl_hdr_t	*psb_hdr;	/* points to sampling buffer header */

	struct _pfm_smpl_buffer_desc *psb_next;	/* next psb, used for rvfreeing of psb_hdr */

} pfm_smpl_buffer_desc_t;

#define LOCK_PSB(p)	spin_lock(&(p)->psb_lock)
#define UNLOCK_PSB(p)	spin_unlock(&(p)->psb_lock)

#define PFM_PSB_VMA	0x1			/* a VMA is describing the buffer */

/*
 * This structure is initialized at boot time and contains
 * a description of the PMU main characteristic as indicated
 * by PAL
 */
typedef struct {
	unsigned long pfm_is_disabled;	/* indicates if perfmon is working properly */
	unsigned long perf_ovfl_val;	/* overflow value for generic counters   */
	unsigned long max_counters;	/* upper limit on counter pair (PMC/PMD) */
	unsigned long num_pmcs ;	/* highest PMC implemented (may have holes) */
	unsigned long num_pmds;		/* highest PMD implemented (may have holes) */
	unsigned long impl_regs[16];	/* buffer used to hold implememted PMC/PMD mask */
	unsigned long num_ibrs;		/* number of instruction debug registers */
	unsigned long num_dbrs;		/* number of data debug registers */
	unsigned long monitor_pmcs[4];	/* which pmc are controlling monitors */
	unsigned long counter_pmds[4];	/* which pmd are used as counters */
} pmu_config_t;

/*
 * 64-bit software counter structure
 */
typedef struct {
	u64 val;	/* virtual 64bit counter value */
	u64 ival;	/* initial value from user */
	u64 long_reset;	/* reset value on sampling overflow */
	u64 short_reset;/* reset value on overflow */
	u64 reset_pmds[4]; /* which other pmds to reset when this counter overflows */
	int flags;	/* notify/do not notify */
} pfm_counter_t;

/*
 * perfmon context. One per process, is cloned on fork() depending on 
 * inheritance flags
 */
typedef struct {
	unsigned int state:1;		/* 0=disabled, 1=enabled */
	unsigned int inherit:2;		/* inherit mode */
	unsigned int block:1;		/* when 1, task will blocked on user notifications */
	unsigned int system:1;		/* do system wide monitoring */
	unsigned int frozen:1;		/* pmu must be kept frozen on ctxsw in */
	unsigned int protected:1;	/* allow access to creator of context only */
	unsigned int using_dbreg:1;	/* using range restrictions (debug registers) */
	unsigned int reserved:24;
} pfm_context_flags_t;

/*
 * perfmon context: encapsulates all the state of a monitoring session
 * XXX: probably need to change layout
 */
typedef struct pfm_context {
	pfm_smpl_buffer_desc_t	*ctx_psb;		/* sampling buffer, if any */
	unsigned long		ctx_smpl_vaddr;		/* user level virtual address of smpl buffer */

	spinlock_t		ctx_lock;
	pfm_context_flags_t	ctx_flags;		/* block/noblock */

	struct task_struct	*ctx_notify_task;	/* who to notify on overflow */
	struct task_struct	*ctx_owner;		/* pid of creator (debug) */

	unsigned long		ctx_ovfl_regs[4];	/* which registers overflowed (notification) */
	unsigned long		ctx_smpl_regs[4];	/* which registers to record on overflow */

	struct semaphore	ctx_restart_sem;   	/* use for blocking notification mode */

	unsigned long		ctx_used_pmds[4];	/* bitmask of used PMD (speedup ctxsw) */
	unsigned long		ctx_saved_pmcs[4];	/* bitmask of PMC to save on ctxsw */
	unsigned long		ctx_reload_pmcs[4];	/* bitmask of PMC to reload on ctxsw (SMP) */

	unsigned long		ctx_used_ibrs[4];	/* bitmask of used IBR (speedup ctxsw) */
	unsigned long		ctx_used_dbrs[4];	/* bitmask of used DBR (speedup ctxsw) */

	pfm_counter_t		ctx_soft_pmds[IA64_NUM_PMD_REGS]; /* XXX: size should be dynamic */

	u64			ctx_saved_psr;		/* copy of psr used for lazy ctxsw */
	unsigned long		ctx_saved_cpus_allowed;	/* copy of the task cpus_allowed (system wide) */
	unsigned long		ctx_cpu;		/* cpu to which perfmon is applied (system wide) */

	atomic_t		ctx_saving_in_progress;	/* flag indicating actual save in progress */
	atomic_t		ctx_last_cpu;		/* CPU id of current or last CPU used */
} pfm_context_t;

#define ctx_fl_inherit		ctx_flags.inherit
#define ctx_fl_block		ctx_flags.block
#define ctx_fl_system		ctx_flags.system
#define ctx_fl_frozen		ctx_flags.frozen
#define ctx_fl_protected	ctx_flags.protected
#define ctx_fl_using_dbreg	ctx_flags.using_dbreg

/*
 * global information about all sessions
 * mostly used to synchronize between system wide and per-process
 */
typedef struct {
	spinlock_t		pfs_lock;		/* lock the structure */

	unsigned long		pfs_task_sessions;	/* number of per task sessions */
	unsigned long		pfs_sys_sessions;	/* number of per system wide sessions */
	unsigned long   	pfs_sys_use_dbregs;	  	/* incremented when a system wide session uses debug regs */
	unsigned long   	pfs_ptrace_use_dbregs;	  /* incremented when a process uses debug regs */
	struct task_struct	*pfs_sys_session[NR_CPUS];  /* point to task owning a system-wide session */
} pfm_session_t;

/*
 * structure used to pass argument to/from remote CPU 
 * using IPI to check and possibly save the PMU context on SMP systems.
 *
 * not used in UP kernels
 */
typedef struct {
	struct task_struct *task;	/* which task we are interested in */
	int retval;			/* return value of the call: 0=you can proceed, 1=need to wait for completion */
} pfm_smp_ipi_arg_t;

/*
 * perfmon command descriptions
 */
typedef struct {
	int		(*cmd_func)(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs);
	int		cmd_flags;
	unsigned int	cmd_narg;
	size_t		cmd_argsize;
} pfm_cmd_desc_t;

#define PFM_CMD_PID		0x1	/* command requires pid argument */
#define PFM_CMD_ARG_READ	0x2	/* command must read argument(s) */
#define PFM_CMD_ARG_WRITE	0x4	/* command must write argument(s) */
#define PFM_CMD_CTX		0x8	/* command needs a perfmon context */
#define PFM_CMD_NOCHK		0x10	/* command does not need to check task's state */

#define PFM_CMD_IDX(cmd)	(cmd)

#define PFM_CMD_IS_VALID(cmd)	((PFM_CMD_IDX(cmd) >= 0) && (PFM_CMD_IDX(cmd) < PFM_CMD_COUNT) \
				  && pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_func != NULL)

#define PFM_CMD_USE_PID(cmd)	((pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_flags & PFM_CMD_PID) != 0)
#define PFM_CMD_READ_ARG(cmd)	((pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_flags & PFM_CMD_ARG_READ) != 0)
#define PFM_CMD_WRITE_ARG(cmd)	((pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_flags & PFM_CMD_ARG_WRITE) != 0)
#define PFM_CMD_USE_CTX(cmd)	((pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_flags & PFM_CMD_CTX) != 0)
#define PFM_CMD_CHK(cmd)	((pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_flags & PFM_CMD_NOCHK) == 0)

#define PFM_CMD_ARG_MANY	-1 /* cannot be zero */
#define PFM_CMD_NARG(cmd)	(pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_narg)
#define PFM_CMD_ARG_SIZE(cmd)	(pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_argsize)


/*
 * perfmon internal variables
 */
static pmu_config_t	pmu_conf; 	/* PMU configuration */
static int		pfm_debug_mode;	/* 0= nodebug, >0= debug output on */
static pfm_session_t	pfm_sessions;	/* global sessions information */
static struct proc_dir_entry *perfmon_dir; /* for debug only */
static unsigned long pfm_spurious_ovfl_intr_count; /* keep track of spurious ovfl interrupts */
static unsigned long pfm_ovfl_intr_count; /* keep track of spurious ovfl interrupts */
static unsigned long pfm_recorded_samples_count;


static unsigned long reset_pmcs[IA64_NUM_PMC_REGS];	/* contains PAL reset values for PMCS */

static void pfm_vm_close(struct vm_area_struct * area);
static struct vm_operations_struct pfm_vm_ops={
	close: pfm_vm_close
};

/*
 * keep track of task owning the PMU per CPU.
 */
static struct {
	struct task_struct *owner;
} ____cacheline_aligned pmu_owners[NR_CPUS];



/*
 * forward declarations
 */
static void ia64_reset_pmu(struct task_struct *);
#ifdef CONFIG_SMP
static void pfm_fetch_regs(int cpu, struct task_struct *task, pfm_context_t *ctx);
#endif
static void pfm_lazy_save_regs (struct task_struct *ta);

static inline unsigned long
pfm_read_soft_counter(pfm_context_t *ctx, int i)
{
	return ctx->ctx_soft_pmds[i].val + (ia64_get_pmd(i) & pmu_conf.perf_ovfl_val);
}

static inline void
pfm_write_soft_counter(pfm_context_t *ctx, int i, unsigned long val)
{
	ctx->ctx_soft_pmds[i].val = val  & ~pmu_conf.perf_ovfl_val;
	/*
	 * writing to unimplemented part is ignore, so we do not need to
	 * mask off top part
	 */
	ia64_set_pmd(i, val);
}

/*
 * finds the number of PM(C|D) registers given
 * the bitvector returned by PAL
 */
static unsigned long __init
find_num_pm_regs(long *buffer)
{
	int i=3; /* 4 words/per bitvector */

	/* start from the most significant word */
	while (i>=0 && buffer[i] == 0 ) i--;
	if (i< 0) {
		printk(KERN_ERR "perfmon: No bit set in pm_buffer\n");
		return 0;
	}
	return 1+ ia64_fls(buffer[i]) + 64 * i;
}


/*
 * Generates a unique (per CPU) timestamp
 */
static inline unsigned long
pfm_get_stamp(void)
{
	/*
	 * XXX: must find something more efficient
	 */
	return ia64_get_itc();
}

/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the area.
 */
static inline unsigned long
pfm_kvirt_to_pa(unsigned long adr)
{
	__u64 pa = ia64_tpa(adr);
	//DBprintk(("kv2pa(%lx-->%lx)\n", adr, pa));
	return pa;
}


static void *
pfm_rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size=PAGE_ALIGN(size);
	mem=vmalloc(size);
	if (mem) {
		//printk("perfmon: CPU%d pfm_rvmalloc(%ld)=%p\n", smp_processor_id(), size, mem);
		memset(mem, 0, size); /* Clear the ram out, no junk to the user */
		adr=(unsigned long) mem;
		while (size > 0) {
			mem_map_reserve(vmalloc_to_page((void *)adr));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
	}
	return mem;
}

static void
pfm_rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (mem) {
		adr=(unsigned long) mem;
		while ((long) size > 0) {
			mem_map_unreserve(vmalloc_to_page((void*)adr));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
		vfree(mem);
	}
	return;
}

/*
 * This function gets called from mm/mmap.c:exit_mmap() only when there is a sampling buffer
 * attached to the context AND the current task has a mapping for it, i.e., it is the original
 * creator of the context.
 *
 * This function is used to remember the fact that the vma describing the sampling buffer
 * has now been removed. It can only be called when no other tasks share the same mm context.
 *
 */
static void 
pfm_vm_close(struct vm_area_struct *vma)
{
	pfm_smpl_buffer_desc_t *psb = (pfm_smpl_buffer_desc_t *)vma->vm_private_data;

	if (psb == NULL) {
		printk("perfmon: psb is null in [%d]\n", current->pid);
		return;
	}
	/*
	 * Add PSB to list of buffers to free on release_thread() when no more users
	 *
	 * This call is safe because, once the count is zero is cannot be modified anymore.
	 * This is not because there is no more user of the mm context, that the sampling
	 * buffer is not being used anymore outside of this task. In fact, it can still
	 * be accessed from within the kernel by another task (such as the monitored task).
	 *
	 * Therefore, we only move the psb into the list of buffers to free when we know
	 * nobody else is using it.
	 * The linked list if independent of the perfmon context, because in the case of
	 * multi-threaded processes, the last thread may not have been involved with
	 * monitoring however it will be the one removing the vma and it should therefore
	 * also remove the sampling buffer. This buffer cannot be removed until the vma
	 * is removed.
	 *
	 * This function cannot remove the buffer from here, because exit_mmap() must first
	 * complete. Given that there is no other vma related callback in the generic code,
	 * we have created on own with the linked list of sampling buffer to free which
	 * is part of the thread structure. In release_thread() we check if the list is
	 * empty. If not we call into perfmon to free the buffer and psb. That is the only
	 * way to ensure a safe deallocation of the sampling buffer which works when
	 * the buffer is shared between distinct processes or with multi-threaded programs.
	 *
	 * We need to lock the psb because the refcnt test and flag manipulation must
	 * looked like an atomic operation vis a vis pfm_context_exit()
	 */
	LOCK_PSB(psb);

	if (psb->psb_refcnt == 0) {

		psb->psb_next = current->thread.pfm_smpl_buf_list;
		current->thread.pfm_smpl_buf_list = psb;

		DBprintk(("psb for [%d] smpl @%p size %ld inserted into list\n", 
			current->pid, psb->psb_hdr, psb->psb_size));
	}
	DBprintk(("psb vma flag cleared for [%d] smpl @%p size %ld inserted into list\n", 
			current->pid, psb->psb_hdr, psb->psb_size));

	/*
	 * indicate to pfm_context_exit() that the vma has been removed. 
	 */
	psb->psb_flags &= ~PFM_PSB_VMA;

	UNLOCK_PSB(psb);
}

/*
 * This function is called from pfm_destroy_context() and also from pfm_inherit()
 * to explicitely remove the sampling buffer mapping from the user level address space.
 */
static int
pfm_remove_smpl_mapping(struct task_struct *task)
{
	pfm_context_t *ctx = task->thread.pfm_context;
	pfm_smpl_buffer_desc_t *psb;
	int r;

	/*
	 * some sanity checks first
	 */
	if (ctx == NULL || task->mm == NULL || ctx->ctx_smpl_vaddr == 0 || ctx->ctx_psb == NULL) {
		printk("perfmon: invalid context mm=%p\n", task->mm);
		return -1;
	}
	psb = ctx->ctx_psb;	

	down_write(&task->mm->mmap_sem);

	r = do_munmap(task->mm, ctx->ctx_smpl_vaddr, psb->psb_size);

	up_write(&task->mm->mmap_sem);
	if (r !=0) {
		printk("perfmon: pid %d unable to unmap sampling buffer @0x%lx size=%ld\n", 
				task->pid, ctx->ctx_smpl_vaddr, psb->psb_size);
	}
	DBprintk(("[%d] do_unmap(0x%lx, %ld)=%d\n", 
		task->pid, ctx->ctx_smpl_vaddr, psb->psb_size, r));

	/* 
	 * make sure we suppress all traces of this buffer
	 * (important for pfm_inherit)
	 */
	ctx->ctx_smpl_vaddr = 0;

	return 0;
}

static pfm_context_t *
pfm_context_alloc(void)
{
	pfm_context_t *ctx;

	/* allocate context descriptor */
	ctx = kmalloc(sizeof(pfm_context_t), GFP_KERNEL);
	if (ctx) memset(ctx, 0, sizeof(pfm_context_t));
	
	return ctx;
}

static void
pfm_context_free(pfm_context_t *ctx)
{
	if (ctx) kfree(ctx);
}

static int
pfm_remap_buffer(struct vm_area_struct *vma, unsigned long buf, unsigned long addr, unsigned long size)
{
	unsigned long page;

	DBprintk(("CPU%d buf=0x%lx addr=0x%lx size=%ld\n", smp_processor_id(), buf, addr, size));

	while (size > 0) {
		page = pfm_kvirt_to_pa(buf);

		if (remap_page_range(vma, addr, page, PAGE_SIZE, PAGE_SHARED)) return -ENOMEM;
		
		addr  += PAGE_SIZE;
		buf   += PAGE_SIZE;
		size  -= PAGE_SIZE;
	}
	return 0;
}

/*
 * counts the number of PMDS to save per entry.
 * This code is generic enough to accomodate more than 64 PMDS when they become available
 */
static unsigned long
pfm_smpl_entry_size(unsigned long *which, unsigned long size)
{
	unsigned long res = 0;
	int i;

	for (i=0; i < size; i++, which++) res += hweight64(*which);

	DBprintk(("weight=%ld\n", res));

	return res;
}

/*
 * Allocates the sampling buffer and remaps it into caller's address space
 */
static int
pfm_smpl_buffer_alloc(pfm_context_t *ctx, unsigned long *which_pmds, unsigned long entries, 
		      void **user_vaddr)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	unsigned long size, regcount;
	void *smpl_buf;
	pfm_smpl_buffer_desc_t *psb;

	regcount = pfm_smpl_entry_size(which_pmds, 1);

	/* note that regcount might be 0, in this case only the header for each
	 * entry will be recorded.
	 */

	/*
	 * 1 buffer hdr and for each entry a header + regcount PMDs to save
	 */
	size = PAGE_ALIGN(  sizeof(perfmon_smpl_hdr_t)
			  + entries * (sizeof(perfmon_smpl_entry_t) + regcount*sizeof(u64)));
	/*
	 * check requested size to avoid Denial-of-service attacks
	 * XXX: may have to refine this test	
	 * Check against address space limit.
	 *
	 * if ((mm->total_vm << PAGE_SHIFT) + len> current->rlim[RLIMIT_AS].rlim_cur) 
	 * 	return -ENOMEM;
	 */
	if (size > current->rlim[RLIMIT_MEMLOCK].rlim_cur) return -EAGAIN;

	/*
	 * We do the easy to undo allocations first.
 	 *
	 * pfm_rvmalloc(), clears the buffer, so there is no leak
	 */
	smpl_buf = pfm_rvmalloc(size);
	if (smpl_buf == NULL) {
		DBprintk(("Can't allocate sampling buffer\n"));
		return -ENOMEM;
	}

	DBprintk(("smpl_buf @%p\n", smpl_buf));

	/* allocate sampling buffer descriptor now */
	psb = kmalloc(sizeof(*psb), GFP_KERNEL);
	if (psb == NULL) {
		DBprintk(("Can't allocate sampling buffer descriptor\n"));
		pfm_rvfree(smpl_buf, size);
		return -ENOMEM;
	}

	/* allocate vma */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!vma) {
		DBprintk(("Cannot allocate vma\n"));
		goto error;
	}
	/*
	 * partially initialize the vma for the sampling buffer
	 */
	vma->vm_flags	     = VM_READ| VM_MAYREAD |VM_RESERVED;
	vma->vm_page_prot    = PAGE_READONLY; /* XXX may need to change */
	vma->vm_ops	     = &pfm_vm_ops; /* necesarry to get the close() callback */
	vma->vm_pgoff	     = 0;
	vma->vm_file	     = NULL;
	vma->vm_raend	     = 0;
	vma->vm_private_data = psb;	/* information needed by the pfm_vm_close() function */

	/*
	 * Now we have everything we need and we can initialize
	 * and connect all the data structures
	 */

	psb->psb_hdr	 = smpl_buf;
	psb->psb_addr    = ((char *)smpl_buf)+sizeof(perfmon_smpl_hdr_t); /* first entry */
	psb->psb_size    = size; /* aligned size */
	psb->psb_index   = 0;
	psb->psb_entries = entries;
	psb->psb_flags   = PFM_PSB_VMA; /* remember that there is a vma describing the buffer */
	psb->psb_refcnt  = 1;

	spin_lock_init(&psb->psb_lock);

	/*
	 * XXX: will need to do cacheline alignment to avoid false sharing in SMP mode and
	 * multitask monitoring.
	 */
	psb->psb_entry_size = sizeof(perfmon_smpl_entry_t) + regcount*sizeof(u64);

	DBprintk(("psb @%p entry_size=%ld hdr=%p addr=%p\n", 
		  (void *)psb,psb->psb_entry_size, (void *)psb->psb_hdr, 
		  (void *)psb->psb_addr));

	/* initialize some of the fields of user visible buffer header */
	psb->psb_hdr->hdr_version    = PFM_SMPL_VERSION;
	psb->psb_hdr->hdr_entry_size = psb->psb_entry_size;
	psb->psb_hdr->hdr_pmds[0]    = which_pmds[0];

	/*
	 * Let's do the difficult operations next.
	 *
	 * now we atomically find some area in the address space and
	 * remap the buffer in it.
	 */
	down_write(&current->mm->mmap_sem);


	/* find some free area in address space, must have mmap sem held */
	vma->vm_start = get_unmapped_area(NULL, 0, size, 0, MAP_PRIVATE|MAP_ANONYMOUS);
	if (vma->vm_start == 0UL) {
		DBprintk(("Cannot find unmapped area for size %ld\n", size));
		up_write(&current->mm->mmap_sem);
		goto error;
	}
	vma->vm_end = vma->vm_start + size;

	DBprintk(("entries=%ld aligned size=%ld, unmapped @0x%lx\n", entries, size, vma->vm_start));
		
	/* can only be applied to current, need to have the mm semaphore held when called */
	if (pfm_remap_buffer(vma, (unsigned long)smpl_buf, vma->vm_start, size)) {
		DBprintk(("Can't remap buffer\n"));
		up_write(&current->mm->mmap_sem);
		goto error;
	}

	/*
	 * now insert the vma in the vm list for the process, must be
	 * done with mmap lock held
	 */
	insert_vm_struct(mm, vma);

	mm->total_vm  += size >> PAGE_SHIFT;

	up_write(&current->mm->mmap_sem);

	/* store which PMDS to record */
	ctx->ctx_smpl_regs[0] = which_pmds[0];


	/* link to perfmon context */
	ctx->ctx_psb        = psb;

	/*
	 * keep track of user level virtual address 
	 */
	ctx->ctx_smpl_vaddr = *(unsigned long *)user_vaddr = vma->vm_start;

	return 0;

error:
	pfm_rvfree(smpl_buf, size);
	kfree(psb);
	return -ENOMEM;
}

/*
 * XXX: do something better here
 */
static int
pfm_bad_permissions(struct task_struct *task)
{
	/* stolen from bad_signal() */
	return (current->session != task->session)
	    && (current->euid ^ task->suid) && (current->euid ^ task->uid)
	    && (current->uid ^ task->suid) && (current->uid ^ task->uid);
}


static int
pfx_is_sane(struct task_struct *task, pfarg_context_t *pfx)
{
	int ctx_flags;
	int cpu;

	/* valid signal */

	/* cannot send to process 1, 0 means do not notify */
	if (pfx->ctx_notify_pid == 1) {
		DBprintk(("invalid notify_pid %d\n", pfx->ctx_notify_pid));
		return -EINVAL;
	}
	ctx_flags = pfx->ctx_flags;

	if (ctx_flags & PFM_FL_SYSTEM_WIDE) {
		DBprintk(("cpu_mask=0x%lx\n", pfx->ctx_cpu_mask));
		/*
		 * cannot block in this mode 
		 */
		if (ctx_flags & PFM_FL_NOTIFY_BLOCK) {
			DBprintk(("cannot use blocking mode when in system wide monitoring\n"));
			return -EINVAL;
		}
		/*
		 * must only have one bit set in the CPU mask
		 */
		if (hweight64(pfx->ctx_cpu_mask) != 1UL) {
			DBprintk(("invalid CPU mask specified\n"));
			return -EINVAL;
		}
		/*
		 * and it must be a valid CPU
		 */
		cpu = ffs(pfx->ctx_cpu_mask);
		if (cpu > smp_num_cpus) {
			DBprintk(("CPU%d is not online\n", cpu));
			return -EINVAL;
		}
		/*
		 * check for pre-existing pinning, if conflicting reject
		 */
		if (task->cpus_allowed != ~0UL && (task->cpus_allowed & (1UL<<cpu)) == 0) {
			DBprintk(("[%d] pinned on 0x%lx, mask for CPU%d \n", task->pid, 
				task->cpus_allowed, cpu));
			return -EINVAL;
		}

	} else {
		/*
		 * must provide a target for the signal in blocking mode even when
		 * no counter is configured with PFM_FL_REG_OVFL_NOTIFY
		 */
		if ((ctx_flags & PFM_FL_NOTIFY_BLOCK) && pfx->ctx_notify_pid == 0) return -EINVAL;
	}
	/* probably more to add here */

	return 0;
}

static int
pfm_create_context(struct task_struct *task, pfm_context_t *ctx, void *req, int count, 
		   struct pt_regs *regs)
{
	pfarg_context_t tmp;
	void *uaddr = NULL;
	int ret, cpu = 0;
	int ctx_flags;
	pid_t notify_pid;

	/* a context has already been defined */
	if (ctx) return -EBUSY;

	/*
	 * not yet supported
	 */
	if (task != current) return -EINVAL;

	if (copy_from_user(&tmp, req, sizeof(tmp))) return -EFAULT;

	ret = pfx_is_sane(task, &tmp);
	if (ret < 0) return ret;

	ctx_flags = tmp.ctx_flags;

	ret =  -EBUSY;

	LOCK_PFS();

	if (ctx_flags & PFM_FL_SYSTEM_WIDE) {

		/* at this point, we know there is at least one bit set */
		cpu = ffs(tmp.ctx_cpu_mask) - 1;

		DBprintk(("requesting CPU%d currently on CPU%d\n",cpu, smp_processor_id()));

		if (pfm_sessions.pfs_task_sessions > 0) {
			DBprintk(("system wide not possible, task_sessions=%ld\n", pfm_sessions.pfs_task_sessions));
			goto abort;
		}

		if (pfm_sessions.pfs_sys_session[cpu]) {
			DBprintk(("system wide not possible, conflicting session [%d] on CPU%d\n",pfm_sessions.pfs_sys_session[cpu]->pid, cpu));
			goto abort;
		}
		pfm_sessions.pfs_sys_session[cpu] = task;
		/*
		 * count the number of system wide sessions
		 */
		pfm_sessions.pfs_sys_sessions++;

	} else if (pfm_sessions.pfs_sys_sessions == 0) {
		pfm_sessions.pfs_task_sessions++;
	} else {
		/* no per-process monitoring while there is a system wide session */
		goto abort;
	}

	UNLOCK_PFS();

	ret = -ENOMEM;

	ctx = pfm_context_alloc();
	if (!ctx) goto error;

	/* record the creator (important for inheritance) */
	ctx->ctx_owner = current;

	notify_pid = tmp.ctx_notify_pid;

	spin_lock_init(&ctx->ctx_lock);

	if (notify_pid == current->pid) {

		ctx->ctx_notify_task = task = current;
		current->thread.pfm_context = ctx;

	} else if (notify_pid!=0) {
		struct task_struct *notify_task;

		read_lock(&tasklist_lock);

		notify_task = find_task_by_pid(notify_pid);

		if (notify_task) {

			ret = -EPERM;

			/*
			 * check if we can send this task a signal
			 */
			if (pfm_bad_permissions(notify_task)) goto buffer_error;

			/* 
		 	 * make visible
		 	 * must be done inside critical section
		 	 *
		 	 * if the initialization does not go through it is still
		 	 * okay because child will do the scan for nothing which
		 	 * won't hurt.
		 	 */
			current->thread.pfm_context = ctx;

			/*
			 * will cause task to check on exit for monitored
			 * processes that would notify it. see release_thread()
			 * Note: the scan MUST be done in release thread, once the
			 * task has been detached from the tasklist otherwise you are
			 * exposed to race conditions.
			 */
			atomic_add(1, &ctx->ctx_notify_task->thread.pfm_notifiers_check);

			ctx->ctx_notify_task = notify_task;
		}
		read_unlock(&tasklist_lock);
	}

	/*
	 * notification process does not exist
	 */
	if (notify_pid != 0 && ctx->ctx_notify_task == NULL) {
		ret = -EINVAL;
		goto buffer_error;
	}

	if (tmp.ctx_smpl_entries) {
		DBprintk(("sampling entries=%ld\n",tmp.ctx_smpl_entries));

		ret = pfm_smpl_buffer_alloc(ctx, tmp.ctx_smpl_regs, 
						 tmp.ctx_smpl_entries, &uaddr);
		if (ret<0) goto buffer_error;

		tmp.ctx_smpl_vaddr = uaddr;
	}
	/* initialization of context's flags */
	ctx->ctx_fl_inherit   = ctx_flags & PFM_FL_INHERIT_MASK;
	ctx->ctx_fl_block     = (ctx_flags & PFM_FL_NOTIFY_BLOCK) ? 1 : 0;
	ctx->ctx_fl_system    = (ctx_flags & PFM_FL_SYSTEM_WIDE) ? 1: 0;
	ctx->ctx_fl_frozen    = 0;
	/*
	 * setting this flag to 0 here means, that the creator or the task that the
	 * context is being attached are granted access. Given that a context can only
	 * be created for the calling process this, in effect only allows the creator
	 * to access the context. See pfm_protect() for more.
	 */
	ctx->ctx_fl_protected = 0;

	/* for system wide mode only (only 1 bit set) */
	ctx->ctx_cpu         = cpu;

	atomic_set(&ctx->ctx_last_cpu,-1); /* SMP only, means no CPU */

	/* 
	 * Keep track of the pmds we want to sample
	 * XXX: may be we don't need to save/restore the DEAR/IEAR pmds
	 * but we do need the BTB for sure. This is because of a hardware
	 * buffer of 1 only for non-BTB pmds.
	 *
	 * We ignore the unimplemented pmds specified by the user
	 */
	ctx->ctx_used_pmds[0]  = tmp.ctx_smpl_regs[0] & pmu_conf.impl_regs[4];
	ctx->ctx_saved_pmcs[0] = 1; /* always save/restore PMC[0] */

	sema_init(&ctx->ctx_restart_sem, 0); /* init this semaphore to locked */


	if (copy_to_user(req, &tmp, sizeof(tmp))) {
		ret = -EFAULT;
		goto buffer_error;
	}

	DBprintk(("context=%p, pid=%d notify_task=%p\n",
			(void *)ctx, task->pid, ctx->ctx_notify_task));

	DBprintk(("context=%p, pid=%d flags=0x%x inherit=%d block=%d system=%d\n", 
			(void *)ctx, task->pid, ctx_flags, ctx->ctx_fl_inherit, 
			ctx->ctx_fl_block, ctx->ctx_fl_system));

	/*
	 * when no notification is required, we can make this visible at the last moment
	 */
	if (notify_pid == 0) task->thread.pfm_context = ctx;
	/*
	 * pin task to CPU and force reschedule on exit to ensure
	 * that when back to user level the task runs on the designated
	 * CPU.
	 */
	if (ctx->ctx_fl_system) {
		ctx->ctx_saved_cpus_allowed = task->cpus_allowed;
		set_cpus_allowed(task, 1UL << cpu);
		DBprintk(("[%d] rescheduled allowed=0x%lx\n", task->pid,task->cpus_allowed));
	}

	return 0;

buffer_error:
	pfm_context_free(ctx);
error:
	/*
	 * undo session reservation
	 */
	LOCK_PFS();

	if (ctx_flags & PFM_FL_SYSTEM_WIDE) {
		pfm_sessions.pfs_sys_session[cpu] = NULL;
		pfm_sessions.pfs_sys_sessions--;
	} else {
		pfm_sessions.pfs_task_sessions--;
	}
abort:
	UNLOCK_PFS();

	return ret;
}

static void
pfm_reset_regs(pfm_context_t *ctx, unsigned long *ovfl_regs, int flag)
{
	unsigned long mask = ovfl_regs[0];
	unsigned long reset_others = 0UL;
	unsigned long val;
	int i;

	DBprintk(("masks=0x%lx\n", mask));

	/*
	 * now restore reset value on sampling overflowed counters
	 */
	mask >>= PMU_FIRST_COUNTER;
	for(i = PMU_FIRST_COUNTER; mask; i++, mask >>= 1) {
		if (mask & 0x1) {
			val  = flag == PFM_RELOAD_LONG_RESET ? 
					ctx->ctx_soft_pmds[i].long_reset:
					ctx->ctx_soft_pmds[i].short_reset;

			reset_others |= ctx->ctx_soft_pmds[i].reset_pmds[0];

			DBprintk(("[%d] %s reset soft_pmd[%d]=%lx\n", 
			  	current->pid, 
				flag == PFM_RELOAD_LONG_RESET ? "long" : "short", i, val));

			/* upper part is ignored on rval */
			pfm_write_soft_counter(ctx, i, val);
		}
	}

	/*
	 * Now take care of resetting the other registers
	 */
	for(i = 0; reset_others; i++, reset_others >>= 1) {

		if ((reset_others & 0x1) == 0) continue;

		val  = flag == PFM_RELOAD_LONG_RESET ? 
					ctx->ctx_soft_pmds[i].long_reset:
					ctx->ctx_soft_pmds[i].short_reset;

		if (PMD_IS_COUNTING(i)) {
			pfm_write_soft_counter(ctx, i, val);
		} else {
			ia64_set_pmd(i, val);
		}

		DBprintk(("[%d] %s reset_others pmd[%d]=%lx\n", 
			  	current->pid, 
				flag == PFM_RELOAD_LONG_RESET ? "long" : "short", i, val));
	}
	/* just in case ! */
	ctx->ctx_ovfl_regs[0] = 0UL;
}

static int
pfm_write_pmcs(struct task_struct *ta, pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct thread_struct *th = &ta->thread;
	pfarg_reg_t tmp, *req = (pfarg_reg_t *)arg;
	unsigned int cnum;
	int i;
	int ret = 0, reg_retval = 0;

	/* we don't quite support this right now */
	if (ta != current) return -EINVAL;

	if (!CTX_IS_ENABLED(ctx)) return -EINVAL;

	/* XXX: ctx locking may be required here */

	for (i = 0; i < count; i++, req++) {


		if (copy_from_user(&tmp, req, sizeof(tmp))) return -EFAULT;

		cnum = tmp.reg_num;

		/* 
		 * we reject all non implemented PMC as well
		 * as attempts to modify PMC[0-3] which are used
		 * as status registers by the PMU
		 */
		if (!PMC_IS_IMPL(cnum) || cnum < 4) {
			DBprintk(("pmc[%u] is unimplemented or invalid\n", cnum));
			ret = -EINVAL;
			goto abort_mission;
		}
		/*
		 * A PMC used to configure monitors must be:
		 * 	- system-wide session: privileged monitor
		 * 	- per-task : user monitor
		 * any other configuration is rejected.
		 */
		if (PMC_IS_MONITOR(cnum)) {
			pfm_monitor_t *p = (pfm_monitor_t *)&tmp.reg_value;

			DBprintk(("pmc[%u].pm = %d\n", cnum, p->pmc_pm));

			if (ctx->ctx_fl_system ^ p->pmc_pm) {
			//if ((ctx->ctx_fl_system == 1 && p->pmc_pm == 0)
			 //  ||(ctx->ctx_fl_system == 0 && p->pmc_pm == 1)) {
				ret = -EINVAL;
				goto abort_mission;
			}
			/*
			 * enforce generation of overflow interrupt. Necessary on all
			 * CPUs which do not implement 64-bit hardware counters.
			 */
			p->pmc_oi = 1;
		}

		if (PMC_IS_COUNTING(cnum)) {
			if (tmp.reg_flags & PFM_REGFL_OVFL_NOTIFY) {
				/*
				 * must have a target for the signal
				 */
				if (ctx->ctx_notify_task == NULL) {
					ret = -EINVAL;
					goto abort_mission;
				}

				ctx->ctx_soft_pmds[cnum].flags |= PFM_REGFL_OVFL_NOTIFY;
			}
			/*
			 * copy reset vector
			 */
			ctx->ctx_soft_pmds[cnum].reset_pmds[0] = tmp.reg_reset_pmds[0];
			ctx->ctx_soft_pmds[cnum].reset_pmds[1] = tmp.reg_reset_pmds[1];
			ctx->ctx_soft_pmds[cnum].reset_pmds[2] = tmp.reg_reset_pmds[2];
			ctx->ctx_soft_pmds[cnum].reset_pmds[3] = tmp.reg_reset_pmds[3];

			/*
			 * needed in case the user does not initialize the equivalent
			 * PMD. Clearing is done in reset_pmu() so there is no possible
			 * leak here.
			 */
			CTX_USED_PMD(ctx, cnum);
		}
abort_mission:
		if (ret == -EINVAL) reg_retval = PFM_REG_RETFL_EINVAL;

		PFM_REG_RETFLAG_SET(tmp.reg_flags, reg_retval);

		/*
		 * update register return value, abort all if problem during copy.
		 */
		if (copy_to_user(req, &tmp, sizeof(tmp))) return -EFAULT;

		/*
		 * if there was something wrong on this register, don't touch
		 * the hardware at all and abort write request for others.
		 *
		 * On error, the user mut sequentially scan the table and the first
		 * entry which has a return flag set is the one that caused the error.
		 */
		if (ret != 0) {
			DBprintk(("[%d] pmc[%u]=0x%lx error %d\n",
				  ta->pid, cnum, tmp.reg_value, reg_retval));
			break;
		}

		/* 
		 * We can proceed with this register!
		 */
		
		/* 
		 * keep copy the pmc, used for register reload
		 */
		th->pmc[cnum] = tmp.reg_value;

		ia64_set_pmc(cnum, tmp.reg_value);

		DBprintk(("[%d] pmc[%u]=0x%lx flags=0x%x save_pmcs=0%lx reload_pmcs=0x%lx\n", 
			  ta->pid, cnum, tmp.reg_value, 
			  ctx->ctx_soft_pmds[cnum].flags, 
			  ctx->ctx_saved_pmcs[0], ctx->ctx_reload_pmcs[0]));

	}
	return ret;
}

static int
pfm_write_pmds(struct task_struct *ta, pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	pfarg_reg_t tmp, *req = (pfarg_reg_t *)arg;
	unsigned int cnum;
	int i;
	int ret = 0, reg_retval = 0;

	/* we don't quite support this right now */
	if (ta != current) return -EINVAL;

	/* 
	 * Cannot do anything before PMU is enabled 
	 */
	if (!CTX_IS_ENABLED(ctx)) return -EINVAL;


	/* XXX: ctx locking may be required here */

	for (i = 0; i < count; i++, req++) {

		if (copy_from_user(&tmp, req, sizeof(tmp))) return -EFAULT;

		cnum = tmp.reg_num;

		if (!PMD_IS_IMPL(cnum)) {
			ret = -EINVAL;
			goto abort_mission;
		}

		/* update virtualized (64bits) counter */
		if (PMD_IS_COUNTING(cnum)) {
			ctx->ctx_soft_pmds[cnum].ival = tmp.reg_value;
			ctx->ctx_soft_pmds[cnum].val  = tmp.reg_value & ~pmu_conf.perf_ovfl_val;
			ctx->ctx_soft_pmds[cnum].long_reset = tmp.reg_long_reset;
			ctx->ctx_soft_pmds[cnum].short_reset = tmp.reg_short_reset;

		}
abort_mission:
		if (ret == -EINVAL) reg_retval = PFM_REG_RETFL_EINVAL;

		PFM_REG_RETFLAG_SET(tmp.reg_flags, reg_retval);

		if (copy_to_user(req, &tmp, sizeof(tmp))) return -EFAULT;

		/*
		 * if there was something wrong on this register, don't touch
		 * the hardware at all and abort write request for others.
		 *
		 * On error, the user mut sequentially scan the table and the first
		 * entry which has a return flag set is the one that caused the error.
		 */
		if (ret != 0) {
			DBprintk(("[%d] pmc[%u]=0x%lx error %d\n",
				  ta->pid, cnum, tmp.reg_value, reg_retval));
			break;
		}

		/* keep track of what we use */
		CTX_USED_PMD(ctx, cnum);

		/* writes to unimplemented part is ignored, so this is safe */
		ia64_set_pmd(cnum, tmp.reg_value);

		/* to go away */
		ia64_srlz_d();
		DBprintk(("[%d] pmd[%u]: soft_pmd=0x%lx  short_reset=0x%lx "
			  "long_reset=0x%lx hw_pmd=%lx notify=%c used_pmds=0x%lx reset_pmds=0x%lx\n",
				ta->pid, cnum,
				ctx->ctx_soft_pmds[cnum].val,
				ctx->ctx_soft_pmds[cnum].short_reset,
				ctx->ctx_soft_pmds[cnum].long_reset,
				ia64_get_pmd(cnum) & pmu_conf.perf_ovfl_val,
				PMC_OVFL_NOTIFY(ctx, cnum) ? 'Y':'N',
				ctx->ctx_used_pmds[0],
				ctx->ctx_soft_pmds[cnum].reset_pmds[0]));
	}
	return ret;
}

static int
pfm_read_pmds(struct task_struct *ta, pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct thread_struct *th = &ta->thread;
	unsigned long val=0;
	pfarg_reg_t tmp, *req = (pfarg_reg_t *)arg;
	int i;

	if (!CTX_IS_ENABLED(ctx)) return -EINVAL;

	/*
	 * XXX: MUST MAKE SURE WE DON"T HAVE ANY PENDING OVERFLOW BEFORE READING
	 * This is required when the monitoring has been stoppped by user or kernel.
	 * If it is still going on, then that's fine because we a re not guaranteed
	 * to return an accurate value in this case.
	 */

	/* XXX: ctx locking may be required here */

	DBprintk(("ctx_last_cpu=%d for [%d]\n", atomic_read(&ctx->ctx_last_cpu), ta->pid));

	for (i = 0; i < count; i++, req++) {
		unsigned long reg_val = ~0UL, ctx_val = ~0UL;

		if (copy_from_user(&tmp, req, sizeof(tmp))) return -EFAULT;

		if (!PMD_IS_IMPL(tmp.reg_num)) goto abort_mission;

		/*
		 * If the task is not the current one, then we check if the
		 * PMU state is still in the local live register due to lazy ctxsw.
		 * If true, then we read directly from the registers.
		 */
		if (atomic_read(&ctx->ctx_last_cpu) == smp_processor_id()){
			ia64_srlz_d();
			val = reg_val = ia64_get_pmd(tmp.reg_num);
			DBprintk(("reading pmd[%u]=0x%lx from hw\n", tmp.reg_num, val));
		} else {
#ifdef CONFIG_SMP
			int cpu;
			/*
			 * for SMP system, the context may still be live on another
			 * CPU so we need to fetch it before proceeding with the read
			 * This call we only be made once for the whole loop because
			 * of ctx_last_cpu becoming == -1.
			 *
			 * We cannot reuse ctx_last_cpu as it may change before we get to the
			 * actual IPI call. In this case, we will do the call for nothing but
			 * there is no way around it. The receiving side will simply do nothing.
			 */
			cpu = atomic_read(&ctx->ctx_last_cpu);
			if (cpu != -1) {
				DBprintk(("must fetch on CPU%d for [%d]\n", cpu, ta->pid));
				pfm_fetch_regs(cpu, ta, ctx);
			}
#endif
			/* context has been saved */
			val = reg_val = th->pmd[tmp.reg_num];
		}
		if (PMD_IS_COUNTING(tmp.reg_num)) {
			/*
			 * XXX: need to check for overflow
			 */

			val &= pmu_conf.perf_ovfl_val;
			val += ctx_val = ctx->ctx_soft_pmds[tmp.reg_num].val;
		} else {

			val = reg_val = ia64_get_pmd(tmp.reg_num);
		}
		PFM_REG_RETFLAG_SET(tmp.reg_flags, 0);
		tmp.reg_value = val;

		DBprintk(("read pmd[%u] soft_pmd=0x%lx reg=0x%lx pmc=0x%lx\n", 
					tmp.reg_num, ctx_val, reg_val, 
					ia64_get_pmc(tmp.reg_num)));

		if (copy_to_user(req, &tmp, sizeof(tmp))) return -EFAULT;
	}
	return 0;
abort_mission:
	PFM_REG_RETFLAG_SET(tmp.reg_flags, PFM_REG_RETFL_EINVAL);
	/* 
	 * XXX: if this fails, we stick we the original failure, flag not updated!
	 */
	copy_to_user(req, &tmp, sizeof(tmp));
	return -EINVAL;

}

#ifdef PFM_PMU_USES_DBR
/*
 * Only call this function when a process it trying to
 * write the debug registers (reading is always allowed)
 */
int
pfm_use_debug_registers(struct task_struct *task)
{
	pfm_context_t *ctx = task->thread.pfm_context;
	int ret = 0;

	DBprintk(("called for [%d]\n", task->pid));

	/*
	 * do it only once
	 */
	if (task->thread.flags & IA64_THREAD_DBG_VALID) return 0;

	/*
	 * Even on SMP, we do not need to use an atomic here because
	 * the only way in is via ptrace() and this is possible only when the
	 * process is stopped. Even in the case where the ctxsw out is not totally
	 * completed by the time we come here, there is no way the 'stopped' process
	 * could be in the middle of fiddling with the pfm_write_ibr_dbr() routine.
	 * So this is always safe.
	 */
	if (ctx && ctx->ctx_fl_using_dbreg == 1) return -1;

	/*
	 * XXX: not pretty
	 */
	LOCK_PFS();

	/*
	 * We only allow the use of debug registers when there is no system
	 * wide monitoring 
	 * XXX: we could relax this by 
	 */
	if (pfm_sessions.pfs_sys_use_dbregs> 0)
		ret = -1;
	else
		pfm_sessions.pfs_ptrace_use_dbregs++;

	DBprintk(("ptrace_use_dbregs=%lu  sys_use_dbregs=%lu by [%d] ret = %d\n", 
		  pfm_sessions.pfs_ptrace_use_dbregs, 
		  pfm_sessions.pfs_sys_use_dbregs, 
		  task->pid, ret));

	UNLOCK_PFS();

	return ret;
}

/*
 * This function is called for every task that exits with the
 * IA64_THREAD_DBG_VALID set. This indicates a task which was
 * able to use the debug registers for debugging purposes via
 * ptrace(). Therefore we know it was not using them for
 * perfmormance monitoring, so we only decrement the number
 * of "ptraced" debug register users to keep the count up to date
 */
int
pfm_release_debug_registers(struct task_struct *task)
{
	int ret;

	LOCK_PFS();
	if (pfm_sessions.pfs_ptrace_use_dbregs == 0) {
		printk("perfmon: invalid release for [%d] ptrace_use_dbregs=0\n", task->pid);
		ret = -1;
	}  else {
		pfm_sessions.pfs_ptrace_use_dbregs--;
		ret = 0;
	}
	UNLOCK_PFS();

	return ret;
}
#else /* PFM_PMU_USES_DBR is true */
/*
 * in case, the PMU does not use the debug registers, these two functions are nops.
 * The first function is called from arch/ia64/kernel/ptrace.c.
 * The second function is called from arch/ia64/kernel/process.c.
 */
int
pfm_use_debug_registers(struct task_struct *task)
{
	return 0;
}
int
pfm_release_debug_registers(struct task_struct *task)
{
	return 0;
}
#endif /* PFM_PMU_USES_DBR */

static int
pfm_restart(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, 
	 struct pt_regs *regs)
{
	void *sem = &ctx->ctx_restart_sem;

	/* 
	 * Cannot do anything before PMU is enabled 
	 */
	if (!CTX_IS_ENABLED(ctx)) return -EINVAL;

#if 0
	if (ctx->ctx_fl_frozen==0) {
		printk("task %d without pmu_frozen set\n", task->pid);
		return -EINVAL;
	}
#endif

	if (task == current) {
		DBprintk(("restarting self %d frozen=%d \n", current->pid, ctx->ctx_fl_frozen));

		pfm_reset_regs(ctx, ctx->ctx_ovfl_regs, PFM_RELOAD_LONG_RESET);

		ctx->ctx_ovfl_regs[0] = 0UL;

		/*
		 * We ignore block/don't block because we never block
		 * for a self-monitoring process.
		 */
		ctx->ctx_fl_frozen = 0;

		if (CTX_HAS_SMPL(ctx)) {
			ctx->ctx_psb->psb_hdr->hdr_count = 0;
			ctx->ctx_psb->psb_index = 0;
		}

		/* simply unfreeze */
		ia64_set_pmc(0, 0);
		ia64_srlz_d();

		return 0;
	} 
	/* restart on another task */

	/*
	 * if blocking, then post the semaphore.
	 * if non-blocking, then we ensure that the task will go into
	 * pfm_overflow_must_block() before returning to user mode. 
	 * We cannot explicitely reset another task, it MUST always
	 * be done by the task itself. This works for system wide because
	 * the tool that is controlling the session is doing "self-monitoring".
	 *
	 * XXX: what if the task never goes back to user?
	 *
	 */
	if (CTX_OVFL_NOBLOCK(ctx) == 0) {
		DBprintk(("unblocking %d \n", task->pid));
		up(sem);
	} else {
		task->thread.pfm_ovfl_block_reset = 1;
		set_tsk_thread_flag(current, TIF_NOTIFY_RESUME);
	}
#if 0
	/*
	 * in case of non blocking mode, then it's just a matter of
	 * of reseting the sampling buffer (if any) index. The PMU
	 * is already active.
	 */

	/*
	 * must reset the header count first
	 */
	if (CTX_HAS_SMPL(ctx)) {
		DBprintk(("resetting sampling indexes for %d \n", task->pid));
		ctx->ctx_psb->psb_hdr->hdr_count = 0;
		ctx->ctx_psb->psb_index = 0;
	}
#endif
	return 0;
}

#ifndef CONFIG_SMP
/*
 * On UP kernels, we do not need to constantly set the psr.pp bit
 * when a task is scheduled. The psr.pp bit can only be changed in
 * the kernel because of a user request. Given we are on a UP non preeemptive 
 * kernel we know that no other task is running, so we cna simply update their
 * psr.pp from their saved state. There is this no impact on the context switch
 * code compared to the SMP case.
 */
static void
pfm_tasklist_toggle_pp(unsigned int val)
{
	struct task_struct *p;
	struct pt_regs *regs;

	DBprintk(("invoked by [%d] pp=%u\n", current->pid, val));

	read_lock(&tasklist_lock);

	for_each_task(p) {
       		regs = (struct pt_regs *)((unsigned long) p + IA64_STK_OFFSET);

		/*
		 * position on pt_regs saved on stack on 1st entry into the kernel
		 */
		regs--;

		/*
		 * update psr.pp
		 */
		ia64_psr(regs)->pp = val;
	}
	read_unlock(&tasklist_lock);
}
#endif



static int
pfm_stop(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, 
	 struct pt_regs *regs)
{
	/* we don't quite support this right now */
	if (task != current) return -EINVAL;

	/* 
	 * Cannot do anything before PMU is enabled 
	 */
	if (!CTX_IS_ENABLED(ctx)) return -EINVAL;

	DBprintk(("[%d] fl_system=%d owner=%p current=%p\n",
				current->pid,
				ctx->ctx_fl_system, PMU_OWNER(),
				current));
	/* simply stop monitoring but not the PMU */
	if (ctx->ctx_fl_system) {

		__asm__ __volatile__ ("rsm psr.pp;;"::: "memory");

		/* disable dcr pp */
		ia64_set_dcr(ia64_get_dcr() & ~IA64_DCR_PP);

#ifdef CONFIG_SMP
		local_cpu_data->pfm_dcr_pp  = 0;
#else
		pfm_tasklist_toggle_pp(0);
#endif

		ia64_psr(regs)->pp = 0;

	} else {
		__asm__ __volatile__ ("rum psr.up;;"::: "memory");

		ia64_psr(regs)->up = 0;
	}
	return 0;
}

static int
pfm_disable(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, 
	   struct pt_regs *regs)
{	
	/* we don't quite support this right now */
	if (task != current) return -EINVAL;

	if (!CTX_IS_ENABLED(ctx)) return -EINVAL;

	/*
	 * stop monitoring, freeze PMU, and save state in context
	 * this call will clear IA64_THREAD_PM_VALID for per-task sessions.
	 */
	pfm_flush_regs(task);

	if (ctx->ctx_fl_system) {	
		ia64_psr(regs)->pp = 0;
	} else {
		ia64_psr(regs)->up = 0;
	}
	/* 
	 * goes back to default behavior 
	 * no need to change live psr.sp because useless at the kernel level
	 */
	ia64_psr(regs)->sp = 1;

	DBprintk(("enabling psr.sp for [%d]\n", current->pid));

	ctx->ctx_flags.state = PFM_CTX_DISABLED;

	return 0;
}



static int
pfm_destroy_context(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, 
	 struct pt_regs *regs)
{
	/* we don't quite support this right now */
	if (task != current) return -EINVAL;

	/*
	 * if context was never enabled, then there is not much
	 * to do
	 */
	if (!CTX_IS_ENABLED(ctx)) goto skipped_stop;

	/*
	 * Disable context: stop monitoring, flush regs to software state (useless here), 
	 * and freeze PMU
	 * 
	 * The IA64_THREAD_PM_VALID is cleared by pfm_flush_regs() called from pfm_disable()
	 */
	pfm_disable(task, ctx, arg, count, regs);

	if (ctx->ctx_fl_system) {	
		ia64_psr(regs)->pp = 0;
	} else {
		ia64_psr(regs)->up = 0;
	}

	/* restore security level */
	ia64_psr(regs)->sp = 1;

skipped_stop:
	/*
	 * remove sampling buffer mapping, if any
	 */
	if (ctx->ctx_smpl_vaddr) pfm_remove_smpl_mapping(task);

	/* now free context and related state */
	pfm_context_exit(task);

	return 0;
}

/*
 * does nothing at the moment
 */
static int
pfm_unprotect_context(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, 
	 struct pt_regs *regs)
{
	return 0;
}

static int
pfm_protect_context(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, 
	 struct pt_regs *regs)
{
	DBprintk(("context from [%d] is protected\n", task->pid));
	/*
	 * from now on, only the creator of the context has access to it
	 */
	ctx->ctx_fl_protected = 1;

	/*
	 * reinforce secure monitoring: cannot toggle psr.up
	 */
	ia64_psr(regs)->sp = 1;

	return 0;
}

static int
pfm_debug(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, 
	 struct pt_regs *regs)
{
	unsigned int mode = *(unsigned int *)arg;

	pfm_debug_mode = mode == 0 ? 0 : 1;

	printk("perfmon debugging %s\n", pfm_debug_mode ? "on" : "off");

	return 0;
}

#ifdef PFM_PMU_USES_DBR

typedef struct {
	unsigned long ibr_mask:56;
	unsigned long ibr_plm:4;
	unsigned long ibr_ig:3;
	unsigned long ibr_x:1;
} ibr_mask_reg_t;

typedef struct {
	unsigned long dbr_mask:56;
	unsigned long dbr_plm:4;
	unsigned long dbr_ig:2;
	unsigned long dbr_w:1;
	unsigned long dbr_r:1;
} dbr_mask_reg_t;

typedef union {
	unsigned long  val;
	ibr_mask_reg_t ibr;
	dbr_mask_reg_t dbr;
} dbreg_t;


static int
pfm_write_ibr_dbr(int mode, struct task_struct *task, void *arg, int count, struct pt_regs *regs)
{
	struct thread_struct *thread = &task->thread;
	pfm_context_t *ctx = task->thread.pfm_context;
	pfarg_dbreg_t tmp, *req = (pfarg_dbreg_t *)arg;
	dbreg_t dbreg;
	unsigned int rnum;
	int first_time;
	int i, ret = 0;

	/*
	 * for range restriction: psr.db must be cleared or the
	 * the PMU will ignore the debug registers.
	 *
	 * XXX: may need more in system wide mode,
	 * no task can have this bit set?
	 */
	if (ia64_psr(regs)->db == 1) return -EINVAL;


	first_time = ctx->ctx_fl_using_dbreg == 0;

	/*
	 * check for debug registers in system wide mode
	 *
	 */
	LOCK_PFS();
	if (ctx->ctx_fl_system && first_time) {
		if (pfm_sessions.pfs_ptrace_use_dbregs) 
			ret = -EBUSY;
		else
			pfm_sessions.pfs_sys_use_dbregs++;
	}
	UNLOCK_PFS();

	if (ret != 0) return ret;

	if (ctx->ctx_fl_system) {
		/* we mark ourselves as owner  of the debug registers */
		ctx->ctx_fl_using_dbreg = 1;
	} else {
       		if (ctx->ctx_fl_using_dbreg == 0) {
			ret= -EBUSY;
			if ((thread->flags & IA64_THREAD_DBG_VALID) != 0) {
				DBprintk(("debug registers already in use for [%d]\n", task->pid));
				goto abort_mission;
			}
			/* we mark ourselves as owner  of the debug registers */
			ctx->ctx_fl_using_dbreg = 1;

			/* 
			 * Given debug registers cannot be used for both debugging 
			 * and performance monitoring at the same time, we reuse
			 * the storage area to save and restore the registers on ctxsw.
			 */
			memset(task->thread.dbr, 0, sizeof(task->thread.dbr));
			memset(task->thread.ibr, 0, sizeof(task->thread.ibr));

			/*
			 * clear hardware registers to make sure we don't leak
			 * information and pick up stale state
			 */
			for (i=0; i < pmu_conf.num_ibrs; i++) {
				ia64_set_ibr(i, 0UL);
			}
			for (i=0; i < pmu_conf.num_dbrs; i++) {
				ia64_set_dbr(i, 0UL);
			}
		}
	}

	ret = -EFAULT;

	/*
	 * Now install the values into the registers
	 */
	for (i = 0; i < count; i++, req++) {

		
		if (copy_from_user(&tmp, req, sizeof(tmp))) goto abort_mission;
		
		rnum      = tmp.dbreg_num;
		dbreg.val = tmp.dbreg_value;
		
		ret = -EINVAL;

		if ((mode == 0 && !IBR_IS_IMPL(rnum)) || ((mode == 1) && !DBR_IS_IMPL(rnum))) {
			DBprintk(("invalid register %u val=0x%lx mode=%d i=%d count=%d\n", 
				  rnum, dbreg.val, mode, i, count));

			goto abort_mission;
		}

		/*
		 * make sure we do not install enabled breakpoint
		 */
		if (rnum & 0x1) {
			if (mode == 0) 
				dbreg.ibr.ibr_x = 0;
			else
				dbreg.dbr.dbr_r = dbreg.dbr.dbr_w = 0;
		}

		/*
		 * clear return flags and copy back to user
		 *
		 * XXX: fix once EAGAIN is implemented
		 */
		ret = -EFAULT;

		PFM_REG_RETFLAG_SET(tmp.dbreg_flags, 0);

		if (copy_to_user(req, &tmp, sizeof(tmp))) goto abort_mission;

		/*
		 * Debug registers, just like PMC, can only be modified
		 * by a kernel call. Moreover, perfmon() access to those
		 * registers are centralized in this routine. The hardware
		 * does not modify the value of these registers, therefore,
		 * if we save them as they are written, we can avoid having
		 * to save them on context switch out. This is made possible
		 * by the fact that when perfmon uses debug registers, ptrace()
		 * won't be able to modify them concurrently.
		 */
		if (mode == 0) {
			CTX_USED_IBR(ctx, rnum);

			ia64_set_ibr(rnum, dbreg.val);

			thread->ibr[rnum] = dbreg.val;

			DBprintk(("write ibr%u=0x%lx used_ibrs=0x%lx\n", rnum, dbreg.val, ctx->ctx_used_ibrs[0]));
		} else {
			CTX_USED_DBR(ctx, rnum);

			ia64_set_dbr(rnum, dbreg.val);

			thread->dbr[rnum] = dbreg.val;

			DBprintk(("write dbr%u=0x%lx used_dbrs=0x%lx\n", rnum, dbreg.val, ctx->ctx_used_dbrs[0]));
		}
	}

	return 0;

abort_mission:
	/*
	 * in case it was our first attempt, we undo the global modifications
	 */
	if (first_time) {
		LOCK_PFS();
		if (ctx->ctx_fl_system) {
			pfm_sessions.pfs_sys_use_dbregs--;
		}
		UNLOCK_PFS();
		ctx->ctx_fl_using_dbreg = 0;
	}
	/*
	 * install error return flag
	 */
	if (ret != -EFAULT) {
		/*
		 * XXX: for now we can only come here on EINVAL
		 */
		PFM_REG_RETFLAG_SET(tmp.dbreg_flags, PFM_REG_RETFL_EINVAL);
		copy_to_user(req, &tmp, sizeof(tmp));
	}
	return ret;
}

static int
pfm_write_ibrs(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, 
	 struct pt_regs *regs)
{	
	/* we don't quite support this right now */
	if (task != current) return -EINVAL;

	if (!CTX_IS_ENABLED(ctx)) return -EINVAL;

	return pfm_write_ibr_dbr(0, task, arg, count, regs);
}

static int
pfm_write_dbrs(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, 
	 struct pt_regs *regs)
{	
	/* we don't quite support this right now */
	if (task != current) return -EINVAL;

	if (!CTX_IS_ENABLED(ctx)) return -EINVAL;

	return pfm_write_ibr_dbr(1, task, arg, count, regs);
}

#endif /* PFM_PMU_USES_DBR */

static int
pfm_get_features(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	pfarg_features_t tmp;

	memset(&tmp, 0, sizeof(tmp));

	tmp.ft_version      = PFM_VERSION;
	tmp.ft_smpl_version = PFM_SMPL_VERSION;

	if (copy_to_user(arg, &tmp, sizeof(tmp))) return -EFAULT;

	return 0;
}

static int
pfm_start(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, 
	  struct pt_regs *regs)
{
	/* we don't quite support this right now */
	if (task != current) return -EINVAL;

	/* 
	 * Cannot do anything before PMU is enabled 
	 */
	if (!CTX_IS_ENABLED(ctx)) return -EINVAL;

	DBprintk(("[%d] fl_system=%d owner=%p current=%p\n",
				current->pid,
				ctx->ctx_fl_system, PMU_OWNER(),
				current));

	if (PMU_OWNER() != task) {
		printk("perfmon: pfm_start task [%d] not pmu owner\n", task->pid);
		return -EINVAL;
	}

	if (ctx->ctx_fl_system) {
		
		/* enable dcr pp */
		ia64_set_dcr(ia64_get_dcr()|IA64_DCR_PP);

#ifdef CONFIG_SMP
		local_cpu_data->pfm_dcr_pp  = 1;
#else
		pfm_tasklist_toggle_pp(1);
#endif
		ia64_psr(regs)->pp = 1;

		__asm__ __volatile__ ("ssm psr.pp;;"::: "memory");

	} else {
		if ((task->thread.flags & IA64_THREAD_PM_VALID) == 0) {
			printk("perfmon: pfm_start task flag not set for [%d]\n", task->pid);
			return -EINVAL;
		}
		ia64_psr(regs)->up = 1;
		__asm__ __volatile__ ("sum psr.up;;"::: "memory");
	}
	ia64_srlz_d();

	return 0;
}

static int
pfm_enable(struct task_struct *task, pfm_context_t *ctx, void *arg, int count, 
	   struct pt_regs *regs)
{
	/* we don't quite support this right now */
	if (task != current) return -EINVAL;

	if (ctx->ctx_fl_system == 0 && PMU_OWNER()  && PMU_OWNER() != current) 
		pfm_lazy_save_regs(PMU_OWNER());

	/* reset all registers to stable quiet state */
	ia64_reset_pmu(task);

	/* make sure nothing starts */
	if (ctx->ctx_fl_system) {
		ia64_psr(regs)->pp = 0;
		ia64_psr(regs)->up = 0; /* just to make sure! */

		__asm__ __volatile__ ("rsm psr.pp;;"::: "memory");

#ifdef CONFIG_SMP
		local_cpu_data->pfm_syst_wide = 1;
		local_cpu_data->pfm_dcr_pp    = 0;
#endif
	} else {
		/*
		 * needed in case the task was a passive task during
		 * a system wide session and now wants to have its own
		 * session
		 */
		ia64_psr(regs)->pp = 0; /* just to make sure! */
		ia64_psr(regs)->up = 0;

		__asm__ __volatile__ ("rum psr.up;;"::: "memory");
		/*
		 * allow user control (user monitors only)
		if (task  == ctx->ctx_owner) {
		 */
		{
			DBprintk(("clearing psr.sp for [%d]\n", current->pid));
			ia64_psr(regs)->sp = 0;
		}
		task->thread.flags |= IA64_THREAD_PM_VALID;
	}

	SET_PMU_OWNER(task);


	ctx->ctx_flags.state = PFM_CTX_ENABLED;
	atomic_set(&ctx->ctx_last_cpu, smp_processor_id());

	/* simply unfreeze */
	ia64_set_pmc(0, 0);
	ia64_srlz_d();

	return 0;
}

/*
 * functions MUST be listed in the increasing order of their index (see permfon.h)
 */
static pfm_cmd_desc_t pfm_cmd_tab[]={
/* 0  */{ NULL, 0, 0, 0}, /* not used */
/* 1  */{ pfm_write_pmcs, PFM_CMD_PID|PFM_CMD_CTX|PFM_CMD_ARG_READ|PFM_CMD_ARG_WRITE, PFM_CMD_ARG_MANY, sizeof(pfarg_reg_t)}, 
/* 2  */{ pfm_write_pmds, PFM_CMD_PID|PFM_CMD_CTX|PFM_CMD_ARG_READ, PFM_CMD_ARG_MANY, sizeof(pfarg_reg_t)},
/* 3  */{ pfm_read_pmds, PFM_CMD_PID|PFM_CMD_CTX|PFM_CMD_ARG_READ|PFM_CMD_ARG_WRITE, PFM_CMD_ARG_MANY, sizeof(pfarg_reg_t)},
/* 4  */{ pfm_stop, PFM_CMD_PID|PFM_CMD_CTX, 0, 0},
/* 5  */{ pfm_start, PFM_CMD_PID|PFM_CMD_CTX, 0, 0},
/* 6  */{ pfm_enable, PFM_CMD_PID|PFM_CMD_CTX, 0, 0},
/* 7  */{ pfm_disable, PFM_CMD_PID|PFM_CMD_CTX, 0, 0},
/* 8  */{ pfm_create_context, PFM_CMD_ARG_READ, 1, sizeof(pfarg_context_t)},
/* 9  */{ pfm_destroy_context, PFM_CMD_PID|PFM_CMD_CTX, 0, 0},
/* 10 */{ pfm_restart, PFM_CMD_PID|PFM_CMD_CTX|PFM_CMD_NOCHK, 0, 0},
/* 11 */{ pfm_protect_context, PFM_CMD_PID|PFM_CMD_CTX, 0, 0},
/* 12 */{ pfm_get_features, PFM_CMD_ARG_WRITE, 0, 0},
/* 13 */{ pfm_debug, 0, 1, sizeof(unsigned int)},
/* 14 */{ pfm_unprotect_context, PFM_CMD_PID|PFM_CMD_CTX, 0, 0},
/* 15 */{ NULL, 0, 0, 0}, /* not used */
/* 16 */{ NULL, 0, 0, 0}, /* not used */
/* 17 */{ NULL, 0, 0, 0}, /* not used */
/* 18 */{ NULL, 0, 0, 0}, /* not used */
/* 19 */{ NULL, 0, 0, 0}, /* not used */
/* 20 */{ NULL, 0, 0, 0}, /* not used */
/* 21 */{ NULL, 0, 0, 0}, /* not used */
/* 22 */{ NULL, 0, 0, 0}, /* not used */
/* 23 */{ NULL, 0, 0, 0}, /* not used */
/* 24 */{ NULL, 0, 0, 0}, /* not used */
/* 25 */{ NULL, 0, 0, 0}, /* not used */
/* 26 */{ NULL, 0, 0, 0}, /* not used */
/* 27 */{ NULL, 0, 0, 0}, /* not used */
/* 28 */{ NULL, 0, 0, 0}, /* not used */
/* 29 */{ NULL, 0, 0, 0}, /* not used */
/* 30 */{ NULL, 0, 0, 0}, /* not used */
/* 31 */{ NULL, 0, 0, 0}, /* not used */
#ifdef PFM_PMU_USES_DBR
/* 32 */{ pfm_write_ibrs, PFM_CMD_PID|PFM_CMD_CTX|PFM_CMD_ARG_READ|PFM_CMD_ARG_WRITE, PFM_CMD_ARG_MANY, sizeof(pfarg_dbreg_t)},
/* 33 */{ pfm_write_dbrs, PFM_CMD_PID|PFM_CMD_CTX|PFM_CMD_ARG_READ|PFM_CMD_ARG_WRITE, PFM_CMD_ARG_MANY, sizeof(pfarg_dbreg_t)}
#endif
};
#define PFM_CMD_COUNT	(sizeof(pfm_cmd_tab)/sizeof(pfm_cmd_desc_t))

static int
check_task_state(struct task_struct *task)
{
	int ret = 0;
#ifdef CONFIG_SMP
	/* We must wait until the state has been completely
	 * saved. There can be situations where the reader arrives before
	 * after the task is marked as STOPPED but before pfm_save_regs()
	 * is completed.
	 */
	for (;;) {

		task_lock(task);
		if (1 /*XXX !task_has_cpu(task)*/) break;
		task_unlock(task);

		do {
			if (task->state != TASK_ZOMBIE && task->state != TASK_STOPPED) return -EBUSY;
			barrier();
			cpu_relax();
		} while (0 /*task_has_cpu(task)*/);
	}
	task_unlock(task);
#else
	if (task->state != TASK_ZOMBIE && task->state != TASK_STOPPED) {
		DBprintk(("warning [%d] not in stable state %ld\n", task->pid, task->state));
		ret = -EBUSY;
	}
#endif
	return ret;
}

asmlinkage int
sys_perfmonctl (pid_t pid, int cmd, void *arg, int count, long arg5, long arg6, long arg7, 
		long arg8, long stack)
{
	struct pt_regs *regs = (struct pt_regs *)&stack;
	struct task_struct *task = current;
	pfm_context_t *ctx = task->thread.pfm_context;
	size_t sz;
	int ret = -ESRCH, narg;

	/* 
	 * reject any call if perfmon was disabled at initialization time
	 */
	if (PFM_IS_DISABLED()) return -ENOSYS;

	DBprintk(("cmd=%d idx=%d valid=%d narg=0x%x\n", cmd, PFM_CMD_IDX(cmd), 
		  PFM_CMD_IS_VALID(cmd), PFM_CMD_NARG(cmd)));

	if (PFM_CMD_IS_VALID(cmd) == 0) return -EINVAL;

	/* ingore arguments when command has none */
	narg = PFM_CMD_NARG(cmd);
	if ((narg == PFM_CMD_ARG_MANY  && count == 0) || (narg > 0 && narg != count)) return -EINVAL;

	sz = PFM_CMD_ARG_SIZE(cmd);

	if (PFM_CMD_READ_ARG(cmd) && !access_ok(VERIFY_READ, arg, sz*count)) return -EFAULT;

	if (PFM_CMD_WRITE_ARG(cmd) && !access_ok(VERIFY_WRITE, arg, sz*count)) return -EFAULT;

	if (PFM_CMD_USE_PID(cmd))  {
		/* 
		 * XXX: may need to fine tune this one
		 */
		if (pid < 2) return -EPERM;

		if (pid != current->pid) {

			read_lock(&tasklist_lock);

			task = find_task_by_pid(pid);

			if (!task) goto abort_call;

			ret = -EPERM;

			if (pfm_bad_permissions(task)) goto abort_call;

			if (PFM_CMD_CHK(cmd)) {
				ret = check_task_state(task);
				if (ret != 0) goto abort_call;
			}
			ctx = task->thread.pfm_context;
		}
	} 

	if (PFM_CMD_USE_CTX(cmd)) {
		ret = -EINVAL;
	       if (ctx == NULL) {
			DBprintk(("no context for task %d\n", task->pid));
			goto abort_call;
	       }
	       ret = -EPERM;
	       /*
		* we only grant access to the context if:
		* 	- the caller is the creator of the context (ctx_owner)
		*  OR   - the context is attached to the caller AND The context IS NOT 
		*  	  in protected mode
		*/
	       if (ctx->ctx_owner != current && (ctx->ctx_fl_protected || task != current)) {
				DBprintk(("context protected, no access for [%d]\n", task->pid));
				goto abort_call;
	       }
	}

	ret = (*pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_func)(task, ctx, arg, count, regs);

abort_call:
	if (task != current) read_unlock(&tasklist_lock);

	return ret;
}

void
pfm_ovfl_block_reset (void)
{
	struct thread_struct *th = &current->thread;
	pfm_context_t *ctx = current->thread.pfm_context;
	int ret;

	/*
	 * clear the flag, to make sure we won't get here
	 * again
	 */
	th->pfm_ovfl_block_reset = 0;

	/*
	 * do some sanity checks first
	 */
	if (!ctx) {
		printk("perfmon: [%d] has no PFM context\n", current->pid);
		return;
	}

	if (CTX_OVFL_NOBLOCK(ctx)) goto non_blocking;

	DBprintk(("[%d] before sleeping\n", current->pid));

	/*
	 * may go through without blocking on SMP systems
	 * if restart has been received already by the time we call down()
	 */
	ret = down_interruptible(&ctx->ctx_restart_sem);

	DBprintk(("[%d] after sleeping ret=%d\n", current->pid, ret));

	/*
	 * in case of interruption of down() we don't restart anything
	 */
	if (ret >= 0) {

non_blocking:
		/* we reactivate on context switch */
		ctx->ctx_fl_frozen = 0;
		/*
		 * the ovfl_sem is cleared by the restart task and this is safe because we always
		 * use the local reference
		 */

		pfm_reset_regs(ctx, ctx->ctx_ovfl_regs, PFM_RELOAD_LONG_RESET);

		ctx->ctx_ovfl_regs[0] = 0UL;

		/*
		 * Unlock sampling buffer and reset index atomically
		 * XXX: not really needed when blocking
		 */
		if (CTX_HAS_SMPL(ctx)) {
			ctx->ctx_psb->psb_hdr->hdr_count = 0;
			ctx->ctx_psb->psb_index = 0;
		}

		ia64_set_pmc(0, 0);
		ia64_srlz_d();

		/* state restored, can go back to work (user mode) */
	}
}

/*
 * This function will record an entry in the sampling if it is not full already.
 * Return:
 * 	0 : buffer is not full (did not BECOME full: still space or was already full)
 * 	1 : buffer is full (recorded the last entry)
 */
static int
pfm_record_sample(struct task_struct *task, pfm_context_t *ctx, unsigned long ovfl_mask, struct pt_regs *regs)
{
	pfm_smpl_buffer_desc_t *psb = ctx->ctx_psb;
	unsigned long *e, m, idx;
	perfmon_smpl_entry_t *h;
	int j;


pfm_recorded_samples_count++;
	idx = ia64_fetch_and_add(1, &psb->psb_index);
	DBprintk(("recording index=%ld entries=%ld\n", idx-1, psb->psb_entries));

	/*
	* XXX: there is a small chance that we could run out on index before resetting
	* but index is unsigned long, so it will take some time.....
	* We use > instead of == because fetch_and_add() is off by one (see below)
	*
	* This case can happen in non-blocking mode or with multiple processes.
	* For non-blocking, we need to reload and continue.
	 */
	if (idx > psb->psb_entries) return 0;

	/* first entry is really entry 0, not 1 caused by fetch_and_add */
	idx--;

	h = (perfmon_smpl_entry_t *)(((char *)psb->psb_addr) + idx*(psb->psb_entry_size));

	/*
	 * initialize entry header
	 */
	h->pid  = task->pid;
	h->cpu  = smp_processor_id();
	h->rate = 0; /* XXX: add the sampling rate used here */
	h->ip   = regs ? regs->cr_iip : 0x0;	/* where did the fault happened */
	h->regs = ovfl_mask; 			/* which registers overflowed */

	/* guaranteed to monotonically increase on each cpu */
	h->stamp  = pfm_get_stamp();
	h->period = 0UL; /* not yet used */

	/* position for first pmd */
	e = (unsigned long *)(h+1);

	/*
	 * selectively store PMDs in increasing index number
	 */
	m = ctx->ctx_smpl_regs[0];
	for (j=0; m; m >>=1, j++) {

		if ((m & 0x1) == 0) continue;

		if (PMD_IS_COUNTING(j)) {
			*e  =  pfm_read_soft_counter(ctx, j);
			/* check if this pmd overflowed as well */
			*e +=  ovfl_mask & (1UL<<j) ? 1 + pmu_conf.perf_ovfl_val : 0;
		} else {
			*e = ia64_get_pmd(j); /* slow */
		}
		DBprintk(("e=%p pmd%d =0x%lx\n", (void *)e, j, *e));
		e++;
	}
	/*
	 * make the new entry visible to user, needs to be atomic
	 */
	ia64_fetch_and_add(1, &psb->psb_hdr->hdr_count);

	DBprintk(("index=%ld entries=%ld hdr_count=%ld\n", 
				idx, psb->psb_entries, psb->psb_hdr->hdr_count));
	/* 
	 * sampling buffer full ? 
	 */
	if (idx == (psb->psb_entries-1)) {
		DBprintk(("sampling buffer full\n"));
		/*
		 * XXX: must reset buffer in blocking mode and lost notified
		 */
		return 1;
	}
	return 0;
}

/*
 * main overflow processing routine.
 * it can be called from the interrupt path or explicitely during the context switch code
 * Return:
 *	new value of pmc[0]. if 0x0 then unfreeze, else keep frozen
 */
static unsigned long
pfm_overflow_handler(struct task_struct *task, u64 pmc0, struct pt_regs *regs)
{
	unsigned long mask;
	struct thread_struct *t;
	pfm_context_t *ctx;
	unsigned long old_val;
	unsigned long ovfl_notify = 0UL, ovfl_pmds = 0UL;
	int i;
	int my_cpu = smp_processor_id();
	int ret = 1;
	struct siginfo si;
	/*
	 * It is never safe to access the task for which the overflow interrupt is destinated
	 * using the current variable as the interrupt may occur in the middle of a context switch
	 * where current does not hold the task that is running yet.
	 *
	 * For monitoring, however, we do need to get access to the task which caused the overflow
	 * to account for overflow on the counters.
	 *
	 * We accomplish this by maintaining a current owner of the PMU per CPU. During context
	 * switch the ownership is changed in a way such that the reflected owner is always the
	 * valid one, i.e. the one that caused the interrupt.
	 */

	if (task == NULL) {
		DBprintk(("owners[%d]=NULL\n", my_cpu));
		return 0x1;
	}
	t   = &task->thread;
	ctx = task->thread.pfm_context;

	if (!ctx) {
		printk("perfmon: Spurious overflow interrupt: process %d has no PFM context\n", 
			task->pid);
		return 0;
	}

	/*
	 * XXX: debug test
	 * Don't think this could happen given upfront tests
	 */
	if ((t->flags & IA64_THREAD_PM_VALID) == 0 && ctx->ctx_fl_system == 0) {
		printk("perfmon: Spurious overflow interrupt: process %d not using perfmon\n", 
			task->pid);
		return 0x1;
	}
	/*
	 * sanity test. Should never happen
	 */
	if ((pmc0 & 0x1) == 0) {
		printk("perfmon: pid %d pmc0=0x%lx assumption error for freeze bit\n", 
			task->pid, pmc0);
		return 0x0;
	}

	mask = pmc0 >> PMU_FIRST_COUNTER;

	DBprintk(("pmc0=0x%lx pid=%d iip=0x%lx, %s"
		  " mode used_pmds=0x%lx save_pmcs=0x%lx reload_pmcs=0x%lx\n", 
			pmc0, task->pid, (regs ? regs->cr_iip : 0), 
			CTX_OVFL_NOBLOCK(ctx) ? "nonblocking" : "blocking",
			ctx->ctx_used_pmds[0],
			ctx->ctx_saved_pmcs[0],
			ctx->ctx_reload_pmcs[0]));

	/*
	 * First we update the virtual counters
	 */
	for (i = PMU_FIRST_COUNTER; mask ; i++, mask >>= 1) {

		/* skip pmd which did not overflow */
		if ((mask & 0x1) == 0) continue;

		DBprintk(("PMD[%d] overflowed hw_pmd=0x%lx soft_pmd=0x%lx\n", 
			  i, ia64_get_pmd(i), ctx->ctx_soft_pmds[i].val));

		/*
		 * Because we sometimes (EARS/BTB) reset to a specific value, we cannot simply use
		 * val to count the number of times we overflowed. Otherwise we would loose the 
		 * current value in the PMD (which can be >0). So to make sure we don't loose
		 * the residual counts we set val to contain full 64bits value of the counter.
		 */
		old_val = ctx->ctx_soft_pmds[i].val;
		ctx->ctx_soft_pmds[i].val = 1 + pmu_conf.perf_ovfl_val + pfm_read_soft_counter(ctx, i);


		DBprintk(("soft_pmd[%d].val=0x%lx old_val=0x%lx pmd=0x%lx\n", 
			  i, ctx->ctx_soft_pmds[i].val, old_val, 
			  ia64_get_pmd(i) & pmu_conf.perf_ovfl_val));

		/*
		 * now that we have extracted the hardware counter, we can clear it to ensure
		 * that a subsequent PFM_READ_PMDS will not include it again.
		 */
		ia64_set_pmd(i, 0UL);

		/*
		 * check for overflow condition
		 */
		if (old_val > ctx->ctx_soft_pmds[i].val) {

			ovfl_pmds |= 1UL << i;

			DBprintk(("soft_pmd[%d] overflowed flags=0x%x, ovfl=0x%lx\n", i, ctx->ctx_soft_pmds[i].flags, ovfl_pmds));

			if (PMC_OVFL_NOTIFY(ctx, i)) {
				ovfl_notify |= 1UL << i;
			}
		}
	}

	/*
	 * check for sampling buffer
	 *
	 * if present, record sample. We propagate notification ONLY when buffer
	 * becomes full.
	 */
	if(CTX_HAS_SMPL(ctx)) {
		ret = pfm_record_sample(task, ctx, ovfl_pmds, regs);
		if (ret == 1) {
			/*
			 * Sampling buffer became full
			 * If no notication was requested, then we reset buffer index
			 * and reset registers (done below) and resume.
			 * If notification requested, then defer reset until pfm_restart()
			 */
			if (ovfl_notify == 0UL) {
				ctx->ctx_psb->psb_hdr->hdr_count = 0UL;
				ctx->ctx_psb->psb_index		 = 0UL;
			}
		} else {
			/*
			 * sample recorded in buffer, no need to notify user
			 */
			ovfl_notify = 0UL;
		}
	}

	/*
	 * No overflow requiring a user level notification
	 */
	if (ovfl_notify == 0UL) {
		pfm_reset_regs(ctx, &ovfl_pmds, PFM_RELOAD_SHORT_RESET);
		return 0x0;
	}

	/* 
	 * keep track of what to reset when unblocking 
	 */
	ctx->ctx_ovfl_regs[0]  = ovfl_pmds; 

	/*
	 * we have come to this point because there was an overflow and that notification
	 * was requested. The notify_task may have disappeared, in which case notify_task
	 * is NULL.
	 */
	if (ctx->ctx_notify_task) {

		si.si_errno    = 0;
		si.si_addr     = NULL;
		si.si_pid      = task->pid; /* who is sending */

		si.si_signo    = SIGPROF;
		si.si_code     = PROF_OVFL; /* indicates a perfmon SIGPROF signal */
		/*
		 * Shift the bitvector such that the user sees bit 4 for PMD4 and so on.
		 * We only use smpl_ovfl[0] for now. It should be fine for quite a while
		 * until we have more than 61 PMD available.
		 */
		si.si_pfm_ovfl[0] = ovfl_notify;
	
		/*
		 * when the target of the signal is not ourself, we have to be more
		 * careful. The notify_task may being cleared by the target task itself
		 * in release_thread(). We must ensure mutual exclusion here such that
		 * the signal is delivered (even to a dying task) safely.
		 */

		if (ctx->ctx_notify_task != current) {
			/*
			 * grab the notification lock for this task
			 * This guarantees that the sequence: test + send_signal
			 * is atomic with regards to the ctx_notify_task field.
			 *
			 * We need a spinlock and not just an atomic variable for this.
			 *
			 */
			spin_lock(&ctx->ctx_lock);

			/*
			 * now notify_task cannot be modified until we're done
			 * if NULL, they it got modified while we were in the handler
			 */
			if (ctx->ctx_notify_task == NULL) {

				spin_unlock(&ctx->ctx_lock);

				/*
				 * If we've lost the notified task, then we will run
				 * to completion wbut keep the PMU frozen. Results
				 * will be incorrect anyway. We do not kill task
				 * to leave it possible to attach perfmon context
				 * to already running task.
				 */
				goto lost_notify;
			}
			/*
			 * required by send_sig_info() to make sure the target
			 * task does not disappear on us.
			 */
			read_lock(&tasklist_lock);
		}
		/*
	 	 * in this case, we don't stop the task, we let it go on. It will
	 	 * necessarily go to the signal handler (if any) when it goes back to
	 	 * user mode.
	 	 */
		DBprintk(("[%d] sending notification to [%d]\n", 
			  task->pid, ctx->ctx_notify_task->pid));


		/* 
		 * this call is safe in an interrupt handler, so does read_lock() on tasklist_lock
		 */
		ret = send_sig_info(SIGPROF, &si, ctx->ctx_notify_task);
		if (ret != 0) 
			printk("send_sig_info(process %d, SIGPROF)=%d\n",  
			       ctx->ctx_notify_task->pid, ret);
		/*
		 * now undo the protections in order
		 */
		if (ctx->ctx_notify_task != current) {
			read_unlock(&tasklist_lock);
			spin_unlock(&ctx->ctx_lock);
		}

		/*
		 * if we block set the pfm_must_block bit
		 * when in block mode, we can effectively block only when the notified
		 * task is not self, otherwise we would deadlock. 
		 * in this configuration, the notification is sent, the task will not 
		 * block on the way back to user mode, but the PMU will be kept frozen
		 * until PFM_RESTART.
		 * Note that here there is still a race condition with notify_task
		 * possibly being nullified behind our back, but this is fine because
		 * it can only be changed to NULL which by construction, can only be
		 * done when notify_task != current. So if it was already different
		 * before, changing it to NULL will still maintain this invariant.
		 * Of course, when it is equal to current it cannot change at this point.
		 */
		DBprintk(("block=%d notify [%d] current [%d]\n", 
			ctx->ctx_fl_block,
			ctx->ctx_notify_task ? ctx->ctx_notify_task->pid: -1, 
			current->pid ));

		if (!CTX_OVFL_NOBLOCK(ctx) && ctx->ctx_notify_task != task) {
			t->pfm_ovfl_block_reset = 1; /* will cause blocking */
		}
	} else {
lost_notify: /* XXX: more to do here, to convert to non-blocking (reset values) */

		DBprintk(("notification task has disappeared !\n"));
		/*
		 * for a non-blocking context, we make sure we do not fall into the 
		 * pfm_overflow_notify() trap. Also in the case of a blocking context with lost 
		 * notify process, then we do not want to block either (even though it is 
		 * interruptible). In this case, the PMU will be kept frozen and the process will 
		 * run to completion without monitoring enabled.
		 *
		 * Of course, we cannot loose notify process when self-monitoring.
		 */
		t->pfm_ovfl_block_reset = 0; 

	}
	/*
	 * If notification was successful, then we rely on the pfm_restart()
	 * call to unfreeze and reset (in both blocking or non-blocking mode).
	 *
	 * If notification failed, then we will keep the PMU frozen and run
	 * the task to completion
	 */
	ctx->ctx_fl_frozen = 1;

	DBprintk(("reload pmc0=0x%x must_block=%ld\n",
				ctx->ctx_fl_frozen ? 0x1 : 0x0, t->pfm_ovfl_block_reset));

	return ctx->ctx_fl_frozen ? 0x1 : 0x0;
}

static void
perfmon_interrupt (int irq, void *arg, struct pt_regs *regs)
{
	u64 pmc0;
	struct task_struct *task;

	pfm_ovfl_intr_count++;

	/* 
	 * srlz.d done before arriving here
	 *
	 * This is slow
	 */
	pmc0 = ia64_get_pmc(0); 

	/*
	 * if we have some pending bits set
	 * assumes : if any PM[0].bit[63-1] is set, then PMC[0].fr = 1
	 */
	if ((pmc0 & ~0x1UL)!=0UL && (task=PMU_OWNER())!= NULL) {

		/* 
		 * assumes, PMC[0].fr = 1 at this point 
		 *
		 * XXX: change protype to pass &pmc0
		 */
		pmc0 = pfm_overflow_handler(task, pmc0, regs);

		/* we never explicitely freeze PMU here */
		if (pmc0 == 0) {
			ia64_set_pmc(0, 0);
			ia64_srlz_d();
		}
	} else {
		pfm_spurious_ovfl_intr_count++;

		DBprintk(("perfmon: Spurious PMU overflow interrupt on CPU%d: pmc0=0x%lx owner=%p\n", 
			smp_processor_id(), pmc0, (void *)PMU_OWNER()));
	}
}

/* for debug only */
static int
perfmon_proc_info(char *page)
{
#ifdef CONFIG_SMP
#define cpu_is_online(i) (cpu_online_map & (1UL << i))
#else
#define cpu_is_online(i)        1
#endif
	char *p = page;
	u64 pmc0 = ia64_get_pmc(0);
	int i;

	p += sprintf(p, "perfmon enabled: %s\n", pmu_conf.pfm_is_disabled ? "No": "Yes");

	p += sprintf(p, "monitors_pmcs0]=0x%lx\n", pmu_conf.monitor_pmcs[0]);
	p += sprintf(p, "counter_pmcds[0]=0x%lx\n", pmu_conf.counter_pmds[0]);
	p += sprintf(p, "overflow interrupts=%lu\n", pfm_ovfl_intr_count);
	p += sprintf(p, "spurious overflow interrupts=%lu\n", pfm_spurious_ovfl_intr_count);
	p += sprintf(p, "recorded samples=%lu\n", pfm_recorded_samples_count);

	p += sprintf(p, "CPU%d.pmc[0]=%lx\nPerfmon debug: %s\n", 
			smp_processor_id(), pmc0, pfm_debug_mode ? "On" : "Off");

#ifdef CONFIG_SMP
	p += sprintf(p, "CPU%d cpu_data.pfm_syst_wide=%d cpu_data.dcr_pp=%d\n", 
			smp_processor_id(), local_cpu_data->pfm_syst_wide, local_cpu_data->pfm_dcr_pp);
#endif

	LOCK_PFS();
	p += sprintf(p, "proc_sessions=%lu\nsys_sessions=%lu\nsys_use_dbregs=%lu\nptrace_use_dbregs=%lu\n", 
			pfm_sessions.pfs_task_sessions, 
			pfm_sessions.pfs_sys_sessions,
			pfm_sessions.pfs_sys_use_dbregs,
			pfm_sessions.pfs_ptrace_use_dbregs);

	UNLOCK_PFS();

	for(i=0; i < NR_CPUS; i++) {
		if (cpu_is_online(i)) {
			p += sprintf(p, "CPU%d.pmu_owner: %-6d\n",
					i, 
					pmu_owners[i].owner ? pmu_owners[i].owner->pid: -1);
		}
	}

	return p - page;
}

/* /proc interface, for debug only */
static int
perfmon_read_entry(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = perfmon_proc_info(page);

	if (len <= off+count) *eof = 1;

	*start = page + off;
	len   -= off;

	if (len>count) len = count;
	if (len<0) len = 0;

	return len;
}

#ifdef CONFIG_SMP
void
pfm_syst_wide_update_task(struct task_struct *task, int mode)
{
	struct pt_regs *regs = (struct pt_regs *)((unsigned long) task + IA64_STK_OFFSET);

	regs--;

	/*
	 * propagate the value of the dcr_pp bit to the psr
	 */
	ia64_psr(regs)->pp = mode ? local_cpu_data->pfm_dcr_pp : 0;
}
#endif


void
pfm_save_regs (struct task_struct *task)
{
	pfm_context_t *ctx;
	u64 psr;

	ctx = task->thread.pfm_context;


	/*
	 * save current PSR: needed because we modify it
	 */
	__asm__ __volatile__ ("mov %0=psr;;": "=r"(psr) :: "memory");

	/*
	 * stop monitoring:
	 * This is the last instruction which can generate an overflow
	 *
	 * We do not need to set psr.sp because, it is irrelevant in kernel.
	 * It will be restored from ipsr when going back to user level
	 */
	__asm__ __volatile__ ("rum psr.up;;"::: "memory");

	ctx->ctx_saved_psr = psr;

	//ctx->ctx_last_cpu  = smp_processor_id();

}

static void
pfm_lazy_save_regs (struct task_struct *task)
{
	pfm_context_t *ctx;
	struct thread_struct *t;
	unsigned long mask;
	int i;

	DBprintk(("on [%d] by [%d]\n", task->pid, current->pid));

	t   = &task->thread;
	ctx = task->thread.pfm_context;

#ifdef CONFIG_SMP
	/* 
	 * announce we are saving this PMU state
	 * This will cause other CPU, to wait until we're done
	 * before using the context.h
	 *
	 * must be an atomic operation
	 */
	atomic_set(&ctx->ctx_saving_in_progress, 1);

	 /*
	  * if owner is NULL, it means that the other CPU won the race
	  * and the IPI has caused the context to be saved in pfm_handle_fectch_regs()
	  * instead of here. We have nothing to do
	  *
	  * note that this is safe, because the other CPU NEVER modifies saving_in_progress.
	  */
	if (PMU_OWNER() == NULL) goto do_nothing;
#endif

	/*
	 * do not own the PMU
	 */
	SET_PMU_OWNER(NULL);

	ia64_srlz_d();

	/*
	 * XXX needs further optimization.
	 * Also must take holes into account
	 */
	mask = ctx->ctx_used_pmds[0];
	for (i=0; mask; i++, mask>>=1) {
		if (mask & 0x1) t->pmd[i] =ia64_get_pmd(i);
	}
	/*
	 * XXX: simplify to pmc0 only
	 */
	mask = ctx->ctx_saved_pmcs[0];
	for (i=0; mask; i++, mask>>=1) {
		if (mask & 0x1) t->pmc[i] = ia64_get_pmc(i);
	}

	/* not owned by this CPU */
	atomic_set(&ctx->ctx_last_cpu, -1);

#ifdef CONFIG_SMP
do_nothing:
#endif
	/*
	 * declare we are done saving this context
	 *
	 * must be an atomic operation
	 */
	atomic_set(&ctx->ctx_saving_in_progress,0);

}

#ifdef CONFIG_SMP
/*
 * Handles request coming from other CPUs
 */
static void 
pfm_handle_fetch_regs(void *info)
{
	pfm_smp_ipi_arg_t *arg = info;
	struct thread_struct *t;
	pfm_context_t *ctx;
	unsigned long mask;
	int i;

	ctx = arg->task->thread.pfm_context;
	t   = &arg->task->thread;

	DBprintk(("task=%d owner=%d saving=%d\n", 
		  arg->task->pid,
		  PMU_OWNER() ? PMU_OWNER()->pid: -1,
		  atomic_read(&ctx->ctx_saving_in_progress)));

	/* must wait if saving was interrupted */
	if (atomic_read(&ctx->ctx_saving_in_progress)) {
		arg->retval = 1;
		return;
	}

	/* can proceed, done with context */
	if (PMU_OWNER() != arg->task) {
		arg->retval = 0;
		return;
	}

	DBprintk(("saving state for [%d] save_pmcs=0x%lx all_pmcs=0x%lx used_pmds=0x%lx\n", 
		arg->task->pid,
		ctx->ctx_saved_pmcs[0],
		ctx->ctx_reload_pmcs[0],
		ctx->ctx_used_pmds[0]));

	/*
	 * XXX: will be replaced with pure assembly call
	 */
	SET_PMU_OWNER(NULL);

	ia64_srlz_d();

	/*
	 * XXX needs further optimization.
	 * Also must take holes into account
	 */
	mask = ctx->ctx_used_pmds[0];
	for (i=0; mask; i++, mask>>=1) {
		if (mask & 0x1) t->pmd[i] =ia64_get_pmd(i);
	}
	
	mask = ctx->ctx_saved_pmcs[0];
	for (i=0; mask; i++, mask>>=1) {
		if (mask & 0x1) t->pmc[i] = ia64_get_pmc(i);
	}
	/* not owned by this CPU */
	atomic_set(&ctx->ctx_last_cpu, -1);

	/* can proceed */
	arg->retval = 0;
}

/*
 * Function call to fetch PMU state from another CPU identified by 'cpu'.
 * If the context is being saved on the remote CPU, then we busy wait until
 * the saving is done and then we return. In this case, non IPI is sent.
 * Otherwise, we send an IPI to the remote CPU, potentially interrupting 
 * pfm_lazy_save_regs() over there.
 *
 * If the retval==1, then it means that we interrupted remote save and that we must
 * wait until the saving is over before proceeding.
 * Otherwise, we did the saving on the remote CPU, and it was done by the time we got there.
 * in either case, we can proceed.
 */
static void
pfm_fetch_regs(int cpu, struct task_struct *task, pfm_context_t *ctx)
{
	pfm_smp_ipi_arg_t  arg;
	int ret;

	arg.task   = task;
	arg.retval = -1;

	if (atomic_read(&ctx->ctx_saving_in_progress)) {
		DBprintk(("no IPI, must wait for [%d] to be saved on [%d]\n", task->pid, cpu));

		/* busy wait */
		while (atomic_read(&ctx->ctx_saving_in_progress));
		return;
	}
	DBprintk(("calling CPU %d from CPU %d\n", cpu, smp_processor_id()));

	if (cpu == -1) {
		printk("refusing to use -1 for [%d]\n", task->pid);
		return;
	}

	/* will send IPI to other CPU and wait for completion of remote call */
	if ((ret=smp_call_function_single(cpu, pfm_handle_fetch_regs, &arg, 0, 1))) {
		printk("perfmon: remote CPU call from %d to %d error %d\n", smp_processor_id(), cpu, ret);
		return;
	}
	/*
	 * we must wait until saving is over on the other CPU
	 * This is the case, where we interrupted the saving which started just at the time we sent the
	 * IPI.
	 */
	if (arg.retval == 1) {
		DBprintk(("must wait for [%d] to be saved on [%d]\n", task->pid, cpu));
		while (atomic_read(&ctx->ctx_saving_in_progress));
		DBprintk(("done saving for [%d] on [%d]\n", task->pid, cpu));
	}
}
#endif /* CONFIG_SMP */

void
pfm_load_regs (struct task_struct *task)
{
	struct thread_struct *t;
	pfm_context_t *ctx;
	struct task_struct *owner;
	unsigned long mask;
	u64 psr;
	int i;
#ifdef CONFIG_SMP
	int cpu;
#endif

	owner = PMU_OWNER();
	ctx   = task->thread.pfm_context;

	/*
	 * if we were the last user, then nothing to do except restore psr
	 */
	if (owner == task) {
		if (atomic_read(&ctx->ctx_last_cpu) != smp_processor_id())
			DBprintk(("invalid last_cpu=%d for [%d]\n", 
				atomic_read(&ctx->ctx_last_cpu), task->pid));

		psr = ctx->ctx_saved_psr;
		__asm__ __volatile__ ("mov psr.l=%0;; srlz.i;;"::"r"(psr): "memory");

		return;
	}
	DBprintk(("load_regs: must reload for [%d] owner=%d\n", 
		task->pid, owner ? owner->pid : -1 ));
	/*
	 * someone else is still using the PMU, first push it out and
	 * then we'll be able to install our stuff !
	 */
	if (owner) pfm_lazy_save_regs(owner);

#ifdef CONFIG_SMP
	/* 
	 * check if context on another CPU (-1 means saved)
	 * We MUST use the variable, as last_cpu may change behind our 
	 * back. If it changes to -1 (not on a CPU anymore), then in cpu
	 * we have the last CPU the context was on. We may be sending the 
	 * IPI for nothing, but we have no way of verifying this. 
	 */
	cpu = atomic_read(&ctx->ctx_last_cpu);
	if (cpu != -1) {
		pfm_fetch_regs(cpu, task, ctx);
	}
#endif
	t   = &task->thread;

	/*
	 * XXX: will be replaced by assembly routine
	 * We clear all unused PMDs to avoid leaking information
	 */
	mask = ctx->ctx_used_pmds[0];
	for (i=0; mask; i++, mask>>=1) {
		if (mask & 0x1) 
			ia64_set_pmd(i, t->pmd[i]);
		else
			ia64_set_pmd(i, 0UL);
	}
	/* XXX: will need to clear all unused pmd, for security */

	/* 
	 * skip pmc[0] to avoid side-effects, 
	 * all PMCs are systematically reloaded, unsued get default value
	 * to avoid picking up stale configuration
	 */	
	mask = ctx->ctx_reload_pmcs[0]>>1;
	for (i=1; mask; i++, mask>>=1) {
		if (mask & 0x1) ia64_set_pmc(i, t->pmc[i]);
	}

	/*
	 * restore debug registers when used for range restrictions.
	 * We must restore the unused registers to avoid picking up
	 * stale information.
	 */
	mask = ctx->ctx_used_ibrs[0];
	for (i=0; mask; i++, mask>>=1) {
		if (mask & 0x1) 
			ia64_set_ibr(i, t->ibr[i]);
		else
			ia64_set_ibr(i, 0UL);
	}

	mask = ctx->ctx_used_dbrs[0];
	for (i=0; mask; i++, mask>>=1) {
		if (mask & 0x1) 
			ia64_set_dbr(i, t->dbr[i]);
		else
			ia64_set_dbr(i, 0UL);
	}

	if (t->pmc[0] & ~0x1) {
		ia64_srlz_d();
		pfm_overflow_handler(task, t->pmc[0], NULL);
	}

	/*
	 * fl_frozen==1 when we are in blocking mode waiting for restart
	 */
	if (ctx->ctx_fl_frozen == 0) {
		ia64_set_pmc(0, 0);
		ia64_srlz_d();
	}
	atomic_set(&ctx->ctx_last_cpu, smp_processor_id());

	SET_PMU_OWNER(task);

	/*
	 * restore the psr we changed in pfm_save_regs()
	 */
	psr = ctx->ctx_saved_psr;
	__asm__ __volatile__ ("mov psr.l=%0;; srlz.i;;"::"r"(psr): "memory");

}

/*
 * XXX: make this routine able to work with non current context
 */
static void
ia64_reset_pmu(struct task_struct *task)
{
	struct thread_struct *t = &task->thread;
	pfm_context_t *ctx = t->pfm_context;
	unsigned long mask;
	int i;

	if (task != current) {
		printk("perfmon: invalid task in ia64_reset_pmu()\n");
		return;
	}

	/* Let's make sure the PMU is frozen */
	ia64_set_pmc(0,1);

	/*
	 * install reset values for PMC. We skip PMC0 (done above)
	 * XX: good up to 64 PMCS
	 */
	mask = pmu_conf.impl_regs[0] >> 1;
	for(i=1; mask; mask>>=1, i++) {
		if (mask & 0x1) {
			ia64_set_pmc(i, reset_pmcs[i]);
			/*
			 * When restoring context, we must restore ALL pmcs, even the ones 
			 * that the task does not use to avoid leaks and possibly corruption
			 * of the sesion because of configuration conflicts. So here, we 
			 * initializaed the table used in the context switch restore routine.
	 		 */
			t->pmc[i] = reset_pmcs[i];
			DBprintk((" pmc[%d]=0x%lx\n", i, reset_pmcs[i]));
						 
		}
	}
	/*
	 * clear reset values for PMD. 
	 * XX: good up to 64 PMDS. Suppose that zero is a valid value.
	 */
	mask = pmu_conf.impl_regs[4];
	for(i=0; mask; mask>>=1, i++) {
		if (mask & 0x1) ia64_set_pmd(i, 0UL);
	}

	/*
	 * On context switched restore, we must restore ALL pmc even
	 * when they are not actively used by the task. In UP, the incoming process 
	 * may otherwise pick up left over PMC state from the previous process.
	 * As opposed to PMD, stale PMC can cause harm to the incoming
	 * process because they may change what is being measured. 
	 * Therefore, we must systematically reinstall the entire
	 * PMC state. In SMP, the same thing is possible on the 
	 * same CPU but also on between 2 CPUs.
	 *
	 * There is unfortunately no easy way to avoid this problem
	 * on either UP or SMP. This definitively slows down the 
	 * pfm_load_regs(). 
	 */
	
	 /*
	  * We must include all the PMC in this mask to make sure we don't
	  * see any side effect of the stale state, such as opcode matching
	  * or range restrictions, for instance.
	  */
	ctx->ctx_reload_pmcs[0] = pmu_conf.impl_regs[0];

	/*
	 * useful in case of re-enable after disable
	 */
	ctx->ctx_used_pmds[0] = 0UL;
	ctx->ctx_used_ibrs[0] = 0UL;
	ctx->ctx_used_dbrs[0] = 0UL;

	ia64_srlz_d();
}

/*
 * This function is called when a thread exits (from exit_thread()).
 * This is a simplified pfm_save_regs() that simply flushes the current
 * register state into the save area taking into account any pending
 * overflow. This time no notification is sent because the task is dying
 * anyway. The inline processing of overflows avoids loosing some counts.
 * The PMU is frozen on exit from this call and is to never be reenabled
 * again for this task.
 *
 */
void
pfm_flush_regs (struct task_struct *task)
{
	pfm_context_t *ctx;
	u64 pmc0;
	unsigned long mask, mask2, val;
	int i;

	ctx = task->thread.pfm_context;

	if (ctx == NULL) return;

	/* 
	 * that's it if context already disabled
	 */
	if (ctx->ctx_flags.state == PFM_CTX_DISABLED) return;

	/*
	 * stop monitoring:
	 * This is the only way to stop monitoring without destroying overflow
	 * information in PMC[0].
	 * This is the last instruction which can cause overflow when monitoring
	 * in kernel.
	 * By now, we could still have an overflow interrupt in-flight.
	 */
	if (ctx->ctx_fl_system) {

		__asm__ __volatile__ ("rsm psr.pp;;"::: "memory");

		/* disable dcr pp */
		ia64_set_dcr(ia64_get_dcr() & ~IA64_DCR_PP);

#ifdef CONFIG_SMP
		local_cpu_data->pfm_syst_wide = 0;
		local_cpu_data->pfm_dcr_pp    = 0;
#else
		pfm_tasklist_toggle_pp(0);
#endif

	} else  {

		__asm__ __volatile__ ("rum psr.up;;"::: "memory");

		/* no more save/restore on ctxsw */
		current->thread.flags &= ~IA64_THREAD_PM_VALID;
	}

	/*
	 * Mark the PMU as not owned
	 * This will cause the interrupt handler to do nothing in case an overflow
	 * interrupt was in-flight
	 * This also guarantees that pmc0 will contain the final state
	 * It virtually gives us full control on overflow processing from that point
	 * on.
	 * It must be an atomic operation.
	 */
	SET_PMU_OWNER(NULL);

	/*
	 * read current overflow status:
	 *
	 * we are guaranteed to read the final stable state
	 */
	ia64_srlz_d();
	pmc0 = ia64_get_pmc(0); /* slow */

	/*
	 * freeze PMU:
	 *
	 * This destroys the overflow information. This is required to make sure
	 * next process does not start with monitoring on if not requested
	 */
	ia64_set_pmc(0, 1);
	ia64_srlz_d();

	/*
	 * We don't need to restore psr, because we are on our way out anyway
	 */

	/*
	 * This loop flushes the PMD into the PFM context.
	 * It also processes overflow inline.
	 *
	 * IMPORTANT: No notification is sent at this point as the process is dying.
	 * The implicit notification will come from a SIGCHILD or a return from a
	 * waitpid().
	 *
	 */

	if (atomic_read(&ctx->ctx_last_cpu) != smp_processor_id()) 
		printk("perfmon: [%d] last_cpu=%d\n", task->pid, atomic_read(&ctx->ctx_last_cpu));

	mask  = pmc0 >> PMU_FIRST_COUNTER;
	mask2 = ctx->ctx_used_pmds[0] >> PMU_FIRST_COUNTER;

	for (i = PMU_FIRST_COUNTER; mask2; i++, mask>>=1, mask2>>=1) {

		/* skip non used pmds */
		if ((mask2 & 0x1) == 0) continue;

		val = ia64_get_pmd(i);

		if (PMD_IS_COUNTING(i)) {

			DBprintk(("[%d] pmd[%d] soft_pmd=0x%lx hw_pmd=0x%lx\n", task->pid, i, ctx->ctx_soft_pmds[i].val, val & pmu_conf.perf_ovfl_val));

			/* collect latest results */
			ctx->ctx_soft_pmds[i].val += val & pmu_conf.perf_ovfl_val;

			/*
			 * now everything is in ctx_soft_pmds[] and we need
			 * to clear the saved context from save_regs() such that
			 * pfm_read_pmds() gets the correct value
			 */
			task->thread.pmd[i] = 0;

			/* take care of overflow inline */
			if (mask & 0x1) {
				ctx->ctx_soft_pmds[i].val += 1 + pmu_conf.perf_ovfl_val;
				DBprintk(("[%d] pmd[%d] overflowed soft_pmd=0x%lx\n",
					task->pid, i, ctx->ctx_soft_pmds[i].val));
			}
		} else {
			DBprintk(("[%d] pmd[%d] hw_pmd=0x%lx\n", task->pid, i, val));
			/* not a counter, just save value as is */
			task->thread.pmd[i] = val;
		}
	}
	/* 
	 * indicates that context has been saved
	 */
	atomic_set(&ctx->ctx_last_cpu, -1);

}



/*
 * task is the newly created task, pt_regs for new child
 */
int
pfm_inherit(struct task_struct *task, struct pt_regs *regs)
{
	pfm_context_t *ctx = current->thread.pfm_context;
	pfm_context_t *nctx;
	struct thread_struct *th = &task->thread;
	unsigned long m;
	int i;

	/*
	 * make sure child cannot mess up the monitoring session
	 */
	 ia64_psr(regs)->sp = 1;
	 DBprintk(("enabling psr.sp for [%d]\n", task->pid));

	 /*
	  * remove any sampling buffer mapping from child user 
	  * address space. Must be done for all cases of inheritance.
	  */
	 if (ctx->ctx_smpl_vaddr) pfm_remove_smpl_mapping(task);

	/*
	 * takes care of easiest case first
	 */
	if (CTX_INHERIT_MODE(ctx) == PFM_FL_INHERIT_NONE) {
		DBprintk(("removing PFM context for [%d]\n", task->pid));
		task->thread.pfm_context     = NULL;
		task->thread.pfm_ovfl_block_reset  = 0;

		/* copy_thread() clears IA64_THREAD_PM_VALID */
		return 0;
	}
	nctx = pfm_context_alloc();
	if (nctx == NULL) return -ENOMEM;

	/* copy content */
	*nctx = *ctx;


	if (CTX_INHERIT_MODE(ctx) == PFM_FL_INHERIT_ONCE) {
		nctx->ctx_fl_inherit = PFM_FL_INHERIT_NONE;
		atomic_set(&nctx->ctx_last_cpu, -1);

		/*
		 * task is not yet visible in the tasklist, so we do 
		 * not need to lock the newly created context.
		 * However, we must grab the tasklist_lock to ensure
		 * that the ctx_owner or ctx_notify_task do not disappear
		 * while we increment their check counters.
		 */
		read_lock(&tasklist_lock);

		if (nctx->ctx_notify_task) 
			atomic_inc(&nctx->ctx_notify_task->thread.pfm_notifiers_check);

		if (nctx->ctx_owner)
			atomic_inc(&nctx->ctx_owner->thread.pfm_owners_check);

		read_unlock(&tasklist_lock);

		DBprintk(("downgrading to INHERIT_NONE for [%d]\n", task->pid));

		LOCK_PFS();
		pfm_sessions.pfs_task_sessions++;
		UNLOCK_PFS();
	}

	/* initialize counters in new context */
	m = pmu_conf.counter_pmds[0] >> PMU_FIRST_COUNTER;
	for(i = PMU_FIRST_COUNTER ; m ; m>>=1, i++) {
		if (m & 0x1) {
			nctx->ctx_soft_pmds[i].val = nctx->ctx_soft_pmds[i].ival & ~pmu_conf.perf_ovfl_val;
			th->pmd[i]	      	   = nctx->ctx_soft_pmds[i].ival & pmu_conf.perf_ovfl_val;
		}

	}
	/* clear BTB index register */
	th->pmd[16] = 0;

	/* if sampling then increment number of users of buffer */
	if (nctx->ctx_psb) {

		/*
		 * XXX: nopt very pretty!
		 */
		LOCK_PSB(nctx->ctx_psb);
		nctx->ctx_psb->psb_refcnt++;
		UNLOCK_PSB(nctx->ctx_psb);
		/*
	 	 * remove any pointer to sampling buffer mapping
	 	 */
		nctx->ctx_smpl_vaddr = 0;
	}

	nctx->ctx_fl_frozen = 0;
	nctx->ctx_ovfl_regs[0] = 0UL;

	sema_init(&nctx->ctx_restart_sem, 0); /* reset this semaphore to locked */

	/* clear pending notification */
	th->pfm_ovfl_block_reset = 0;

	/* link with new task */
	th->pfm_context    = nctx;

	DBprintk(("nctx=%p for process [%d]\n", (void *)nctx, task->pid));

	/*
	 * the copy_thread routine automatically clears
	 * IA64_THREAD_PM_VALID, so we need to reenable it, if it was used by the caller
	 */
	if (current->thread.flags & IA64_THREAD_PM_VALID) {
		DBprintk(("setting PM_VALID for [%d]\n", task->pid));
		th->flags |= IA64_THREAD_PM_VALID;
	}

	return 0;
}

/* 
 *
 * We cannot touch any of the PMU registers at this point as we may
 * not be running on the same CPU the task was last run on.  Therefore
 * it is assumed that the PMU has been stopped appropriately in
 * pfm_flush_regs() called from exit_thread(). 
 *
 * The function is called in the context of the parent via a release_thread()
 * and wait4(). The task is not in the tasklist anymore.
 */
void
pfm_context_exit(struct task_struct *task)
{
	pfm_context_t *ctx = task->thread.pfm_context;

	/*
	 * check sampling buffer
	 */
	if (ctx->ctx_psb) {
		pfm_smpl_buffer_desc_t *psb = ctx->ctx_psb;

		LOCK_PSB(psb);

		DBprintk(("sampling buffer from [%d] @%p size %ld vma_flag=0x%x\n",
			task->pid,
			psb->psb_hdr, psb->psb_size, psb->psb_flags));

		/*
		 * in the case where we are the last user, we may be able to free
		 * the buffer
		 */
		psb->psb_refcnt--;

		if (psb->psb_refcnt == 0) {

			/*
			 * The flag is cleared in pfm_vm_close(). which gets 
			 * called from do_exit() via exit_mm(). 
			 * By the time we come here, the task has no more mm context.
			 *
			 * We can only free the psb and buffer here after the vm area
			 * describing the buffer has been removed. This normally happens 
			 * as part of do_exit() but the entire mm context is ONLY removed
			 * once its reference counts goes to zero. This is typically
			 * the case except for multi-threaded (several tasks) processes.
			 *
			 * See pfm_vm_close() and pfm_cleanup_smpl_buf() for more details.
			 */
			if ((psb->psb_flags & PFM_PSB_VMA) == 0) {

				DBprintk(("cleaning sampling buffer from [%d] @%p size %ld\n",
					task->pid,
					psb->psb_hdr, psb->psb_size));

				/* 
				 * free the buffer and psb 
				 */
				pfm_rvfree(psb->psb_hdr, psb->psb_size);
				kfree(psb);
				psb = NULL;
			} 
		} 
		/* psb may have been deleted */
		if (psb) UNLOCK_PSB(psb);
	} 

	DBprintk(("cleaning [%d] pfm_context @%p notify_task=%p check=%d mm=%p\n", 
		task->pid, ctx, 
		ctx->ctx_notify_task, 
		atomic_read(&task->thread.pfm_notifiers_check), task->mm));

	/*
	 * To avoid getting the notified task or owner task scan the entire process 
	 * list when they exit, we decrement notifiers_check and owners_check respectively.
	 *
	 * Of course, there is race condition between decreasing the value and the 
	 * task exiting. The danger comes from the fact that, in both cases, we have a 
	 * direct pointer to a task structure thereby bypassing the tasklist. 
	 * We must make sure that, if we have task!= NULL, the target task is still 
	 * present and is identical to the initial task specified 
	 * during pfm_create_context(). It may already be detached from the tasklist but 
	 * that's okay. Note that it is okay if we miss the deadline and the task scans 
	 * the list for nothing, it will affect performance but not correctness. 
	 * The correctness is ensured by using the ctx_lock which prevents the 
	 * notify_task from changing the fields in our context.
	 * Once holdhing this lock, if we see task!= NULL, then it will stay like
	 * that until we release the lock. If it is NULL already then we came too late.
	 */
	LOCK_CTX(ctx);

	if (ctx->ctx_notify_task != NULL) {
		DBprintk(("[%d], [%d] atomic_sub on [%d] notifiers=%u\n", current->pid,
			task->pid,
			ctx->ctx_notify_task->pid, 
			atomic_read(&ctx->ctx_notify_task->thread.pfm_notifiers_check)));

		atomic_dec(&ctx->ctx_notify_task->thread.pfm_notifiers_check);
	}

	if (ctx->ctx_owner != NULL) {
		DBprintk(("[%d], [%d] atomic_sub on [%d] owners=%u\n", 
			 current->pid, 
			 task->pid,
			 ctx->ctx_owner->pid, 
			 atomic_read(&ctx->ctx_owner->thread.pfm_owners_check)));

		atomic_dec(&ctx->ctx_owner->thread.pfm_owners_check);
	}

	UNLOCK_CTX(ctx);

	LOCK_PFS();

	if (ctx->ctx_fl_system) {

		pfm_sessions.pfs_sys_session[ctx->ctx_cpu] = NULL;
		pfm_sessions.pfs_sys_sessions--;
		DBprintk(("freeing syswide session on CPU%ld\n", ctx->ctx_cpu));
		/* update perfmon debug register counter */
		if (ctx->ctx_fl_using_dbreg) {
			if (pfm_sessions.pfs_sys_use_dbregs == 0) {
				printk("perfmon: invalid release for [%d] sys_use_dbregs=0\n", task->pid);
			} else
				pfm_sessions.pfs_sys_use_dbregs--;
		}

		/*
	 	 * remove any CPU pinning
	 	 */
		set_cpus_allowed(task, ctx->ctx_saved_cpus_allowed);
	} else {
		pfm_sessions.pfs_task_sessions--;
	}
	UNLOCK_PFS();

	pfm_context_free(ctx);
	/* 
	 *  clean pfm state in thread structure,
	 */
	task->thread.pfm_context          = NULL;
	task->thread.pfm_ovfl_block_reset = 0;

	/* pfm_notifiers is cleaned in pfm_cleanup_notifiers() */
}

/*
 * function invoked from release_thread when pfm_smpl_buf_list is not NULL
 */
int
pfm_cleanup_smpl_buf(struct task_struct *task)
{
	pfm_smpl_buffer_desc_t *tmp, *psb = task->thread.pfm_smpl_buf_list;

	if (psb == NULL) {
		printk("perfmon: psb is null in [%d]\n", current->pid);
		return -1;
	}
	/*
	 * Walk through the list and free the sampling buffer and psb
	 */
	while (psb) {
		DBprintk(("[%d] freeing smpl @%p size %ld\n", current->pid, psb->psb_hdr, psb->psb_size));

		pfm_rvfree(psb->psb_hdr, psb->psb_size);
		tmp = psb->psb_next;
		kfree(psb);
		psb = tmp;
	}

	/* just in case */
	task->thread.pfm_smpl_buf_list = NULL;

	return 0;
}

/*
 * function invoked from release_thread to make sure that the ctx_owner field does not
 * point to an unexisting task.
 */
void
pfm_cleanup_owners(struct task_struct *task)
{
	struct task_struct *p;
	pfm_context_t *ctx;

	DBprintk(("called by [%d] for [%d]\n", current->pid, task->pid));

	read_lock(&tasklist_lock);

	for_each_task(p) {
		/*
		 * It is safe to do the 2-step test here, because thread.ctx
		 * is cleaned up only in release_thread() and at that point
		 * the task has been detached from the tasklist which is an
		 * operation which uses the write_lock() on the tasklist_lock
		 * so it cannot run concurrently to this loop. So we have the
		 * guarantee that if we find p and it has a perfmon ctx then
		 * it is going to stay like this for the entire execution of this
		 * loop.
		 */
		ctx = p->thread.pfm_context;

		//DBprintk(("[%d] scanning task [%d] ctx=%p\n", task->pid, p->pid, ctx));

		if (ctx && ctx->ctx_owner == task) {
			DBprintk(("trying for owner [%d] in [%d]\n", task->pid, p->pid));
			/*
			 * the spinlock is required to take care of a race condition
			 * with the send_sig_info() call. We must make sure that 
			 * either the send_sig_info() completes using a valid task,
			 * or the notify_task is cleared before the send_sig_info()
			 * can pick up a stale value. Note that by the time this
			 * function is executed the 'task' is already detached from the
			 * tasklist. The problem is that the notifiers have a direct
			 * pointer to it. It is okay to send a signal to a task in this
			 * stage, it simply will have no effect. But it is better than sending
			 * to a completely destroyed task or worse to a new task using the same
			 * task_struct address.
			 */
			LOCK_CTX(ctx);

			ctx->ctx_owner = NULL;

			UNLOCK_CTX(ctx);

			DBprintk(("done for notifier [%d] in [%d]\n", task->pid, p->pid));
		}
	}
	read_unlock(&tasklist_lock);
}


/*
 * function called from release_thread to make sure that the ctx_notify_task is not pointing
 * to an unexisting task
 */
void
pfm_cleanup_notifiers(struct task_struct *task)
{
	struct task_struct *p;
	pfm_context_t *ctx;

	DBprintk(("called by [%d] for [%d]\n", current->pid, task->pid));

	read_lock(&tasklist_lock);

	for_each_task(p) {
		/*
		 * It is safe to do the 2-step test here, because thread.ctx
		 * is cleaned up only in release_thread() and at that point
		 * the task has been detached from the tasklist which is an
		 * operation which uses the write_lock() on the tasklist_lock
		 * so it cannot run concurrently to this loop. So we have the
		 * guarantee that if we find p and it has a perfmon ctx then
		 * it is going to stay like this for the entire execution of this
		 * loop.
		 */
		ctx = p->thread.pfm_context;

		//DBprintk(("[%d] scanning task [%d] ctx=%p\n", task->pid, p->pid, ctx));

		if (ctx && ctx->ctx_notify_task == task) {
			DBprintk(("trying for notifier [%d] in [%d]\n", task->pid, p->pid));
			/*
			 * the spinlock is required to take care of a race condition
			 * with the send_sig_info() call. We must make sure that 
			 * either the send_sig_info() completes using a valid task,
			 * or the notify_task is cleared before the send_sig_info()
			 * can pick up a stale value. Note that by the time this
			 * function is executed the 'task' is already detached from the
			 * tasklist. The problem is that the notifiers have a direct
			 * pointer to it. It is okay to send a signal to a task in this
			 * stage, it simply will have no effect. But it is better than sending
			 * to a completely destroyed task or worse to a new task using the same
			 * task_struct address.
			 */
			LOCK_CTX(ctx);

			ctx->ctx_notify_task = NULL;

			UNLOCK_CTX(ctx);

			DBprintk(("done for notifier [%d] in [%d]\n", task->pid, p->pid));
		}
	}
	read_unlock(&tasklist_lock);
}

static struct irqaction perfmon_irqaction = {
	handler:	perfmon_interrupt,
	flags:		SA_INTERRUPT,
	name:		"perfmon"
};


static void
pfm_pmu_snapshot(void)
{
	int i;

	for (i=0; i < IA64_NUM_PMC_REGS; i++) {
		if (i >= pmu_conf.num_pmcs) break;
		if (PMC_IS_IMPL(i)) reset_pmcs[i] = ia64_get_pmc(i);
	}
}

/*
 * perfmon initialization routine, called from the initcall() table
 */
int __init
perfmon_init (void)
{
	pal_perf_mon_info_u_t pm_info;
	s64 status;

	register_percpu_irq(IA64_PERFMON_VECTOR, &perfmon_irqaction);

	ia64_set_pmv(IA64_PERFMON_VECTOR);
	ia64_srlz_d();

	pmu_conf.pfm_is_disabled = 1;

	printk("perfmon: version %u.%u (sampling format v%u.%u) IRQ %u\n", 
		PFM_VERSION_MAJ, 
		PFM_VERSION_MIN, 
		PFM_SMPL_VERSION_MAJ, 
		PFM_SMPL_VERSION_MIN, 
		IA64_PERFMON_VECTOR);

	if ((status=ia64_pal_perf_mon_info(pmu_conf.impl_regs, &pm_info)) != 0) {
		printk("perfmon: PAL call failed (%ld), perfmon disabled\n", status);
		return -1;
	}

	pmu_conf.perf_ovfl_val = (1UL << pm_info.pal_perf_mon_info_s.width) - 1;
	pmu_conf.max_counters  = pm_info.pal_perf_mon_info_s.generic;
	pmu_conf.num_pmcs      = find_num_pm_regs(pmu_conf.impl_regs);
	pmu_conf.num_pmds      = find_num_pm_regs(&pmu_conf.impl_regs[4]);

	printk("perfmon: %u bits counters\n", pm_info.pal_perf_mon_info_s.width);

	printk("perfmon: %lu PMC/PMD pairs, %lu PMCs, %lu PMDs\n", 
	       pmu_conf.max_counters, pmu_conf.num_pmcs, pmu_conf.num_pmds);

	/* sanity check */
	if (pmu_conf.num_pmds >= IA64_NUM_PMD_REGS || pmu_conf.num_pmcs >= IA64_NUM_PMC_REGS) {
		printk(KERN_ERR "perfmon: not enough pmc/pmd, perfmon is DISABLED\n");
		return -1; /* no need to continue anyway */
	}

	if (ia64_pal_debug_info(&pmu_conf.num_ibrs, &pmu_conf.num_dbrs)) {
		printk(KERN_WARNING "perfmon: unable to get number of debug registers\n");
		pmu_conf.num_ibrs = pmu_conf.num_dbrs = 0;
	}
	/* PAL reports the number of pairs */
	pmu_conf.num_ibrs <<=1;
	pmu_conf.num_dbrs <<=1;

	/*
	 * take a snapshot of all PMU registers. PAL is supposed
	 * to configure them with stable/safe values, i.e., not
	 * capturing anything.
	 * We take a snapshot now, before we make any modifications. This
	 * will become our master copy. Then we will reuse the snapshot
	 * to reset the PMU in pfm_enable(). Using this technique, perfmon
	 * does NOT have to know about the specific values to program for
	 * the PMC/PMD. The safe values may be different from one CPU model to
	 * the other.
	 */
	pfm_pmu_snapshot();

	/* 
	 * list the pmc registers used to control monitors 
	 * XXX: unfortunately this information is not provided by PAL
	 *
	 * We start with the architected minimum and then refine for each CPU model
	 */
	pmu_conf.monitor_pmcs[0] = PMM(4)|PMM(5)|PMM(6)|PMM(7);

	/*
	 * architected counters
	 */
	pmu_conf.counter_pmds[0] |= PMM(4)|PMM(5)|PMM(6)|PMM(7);

#ifdef CONFIG_ITANIUM
	pmu_conf.monitor_pmcs[0] |= PMM(10)|PMM(11)|PMM(12);
	/* Itanium does not add more counters */
#endif
	/* we are all set */
	pmu_conf.pfm_is_disabled = 0;

	/*
	 * for now here for debug purposes
	 */
	perfmon_dir = create_proc_read_entry ("perfmon", 0, 0, perfmon_read_entry, NULL);

	spin_lock_init(&pfm_sessions.pfs_lock);

	return 0;
}

__initcall(perfmon_init);

void
perfmon_init_percpu (void)
{
	ia64_set_pmv(IA64_PERFMON_VECTOR);
	ia64_srlz_d();
}


#else /* !CONFIG_PERFMON */

asmlinkage int
sys_perfmonctl (int pid, int cmd, void *req, int count, long arg5, long arg6, 
		long arg7, long arg8, long stack)
{
	return -ENOSYS;
}

#endif /* !CONFIG_PERFMON */
