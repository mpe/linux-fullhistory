/*
 *  linux/arch/ppc/kernel/process.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/i386/kernel/process.c"
 *    Copyright (C) 1995  Linus Torvalds
 *
 *  Modified by Cort Dougan (cort@cs.nmt.edu) and
 *  Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/config.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp_lock.h>

int dump_fpu(void);
void switch_to(struct task_struct *, struct task_struct *);
void print_backtrace(unsigned long *);
void show_regs(struct pt_regs * regs);
void inline zero_paged(void);
extern unsigned long _get_SP(void);

#undef SHOW_TASK_SWITCHES 1
#undef CHECK_STACK 1
#undef IDLE_ZERO 1

unsigned long
kernel_stack_top(struct task_struct *tsk)
{
	return ((unsigned long)tsk) + sizeof(union task_union);
}

unsigned long
task_top(struct task_struct *tsk)
{
	return ((unsigned long)tsk) + sizeof(struct task_struct);
}
	
static struct vm_area_struct init_mmap = INIT_MMAP;
static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS;

struct mm_struct init_mm = INIT_MM;
union task_union init_task_union = { INIT_TASK };

int
dump_fpu(void)
{
	return (1);
}

/* check to make sure the kernel stack is healthy */
int check_stack(struct task_struct *tsk)
{
	unsigned long stack_top = kernel_stack_top(tsk);
	unsigned long tsk_top = task_top(tsk);
	int ret = 0;
	unsigned long *i;

#if 0	
	/* check tss magic */
	if ( tsk->tss.magic != TSS_MAGIC )
	{
		ret |= 1;
		printk("tss.magic bad: %08x\n", tsk->tss.magic);
	}
#endif
	
	if ( !tsk )
		printk("check_stack(): tsk bad tsk %p\n",tsk);
	
	/* check if stored ksp is bad */
	if ( (tsk->tss.ksp > stack_top) || (tsk->tss.ksp < tsk_top) )
	{
		printk("stack out of bounds: %s/%d\n"
		       " tsk_top %08x ksp %08x stack_top %08x\n",
		       tsk->comm,tsk->pid,
		       tsk_top, tsk->tss.ksp, stack_top);
		ret |= 2;
	}
	
	/* check if stack ptr RIGHT NOW is bad */
	if ( (tsk == current) && ((_get_SP() > stack_top ) || (_get_SP() < tsk_top)) )
	{
		printk("current stack ptr out of bounds: %s/%d\n"
		       " tsk_top %08x sp %08x stack_top %08x\n",
		       current->comm,current->pid,
		       tsk_top, _get_SP(), stack_top);
		ret |= 4;
	}

#if 0	
	/* check amount of free stack */
	for ( i = (unsigned long *)task_top(tsk) ; i < kernel_stack_top(tsk) ; i++ )
	{
		if ( !i )
			printk("check_stack(): i = %p\n", i);
		if ( *i != 0 )
		{
			/* only notify if it's less than 900 bytes */
			if ( (i - (unsigned long *)task_top(tsk))  < 900 )
				printk("%d bytes free on stack\n",
				       i - task_top(tsk));
			break;
		}
	}
#endif

	if (ret)
	{
		panic("bad kernel stack");
	}
	return(ret);
}

void
switch_to(struct task_struct *prev, struct task_struct *new)
{
	struct thread_struct *new_tss, *old_tss;
	int s = _disable_interrupts();
	struct pt_regs *regs = (struct pt_regs *)(new->tss.ksp+STACK_FRAME_OVERHEAD);

#if CHECK_STACK
	check_stack(prev);
	check_stack(new);
#endif
        /* turn off fpu for task last to run */
	/*prev->tss.regs->msr &= ~MSR_FP;*/
	
#ifdef SHOW_TASK_SWITCHES
	printk("%s/%d (%x) -> %s/%d (%x) ctx %x\n",
	       prev->comm,prev->pid,prev->tss.regs->nip,
	       new->comm,new->pid,new->tss.regs->nip,new->mm->context);
#endif
	new_tss = &new->tss;
	old_tss = &current->tss;
	_switch(old_tss, new_tss, new->mm->context);
        /* turn off fpu for task last to run */	
	_enable_interrupts(s);
}
	
#include <linux/mc146818rtc.h>
asmlinkage int sys_debug(long a, long b, long c, long d, long e, long f,struct pt_regs *regs)
{
#if 1
	struct task_struct *p;
	printk("sys_debug(): r3 %x r4 %x r5 %x r6 %x\n", a,b,c,d);
	printk("last %x\n", last_task_used_math);
	printk("cur %x regs %x/%x tss %x/%x\n",
	       current, current->tss.regs,regs,&current->tss,current->tss);
	for_each_task(p)
	{
		if ((long)p < KERNELBASE)
		{
			printk("nip %x lr %x r3 %x\n", regs->nip,regs->link,a);
			print_mm_info();
			__cli();
			while(1);
		}
	}
	return regs->gpr[3];
#endif
#if 0
	/* set the time in the cmos clock */
	unsigned long hwtime, nowtime;
	struct rtc_time tm;
	
	hwtime = get_cmos_time();
	to_tm(hwtime, &tm);
	printk("hw: H:M:S M/D/Y %02d:%02d:%02d %d/%d/%d\n", 
	       tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_mon,
	       tm.tm_mday, tm.tm_year);
	return;
#endif
}

/*
 * vars for idle task zero'ing out pages
 */
unsigned long zero_list = 0;	/* head linked list of pre-zero'd pages */
unsigned long bytecount = 0;	/* pointer into the currently being zero'd page */
unsigned long zerocount = 0;	/* # currently pre-zero'd pages */
unsigned long zerototal = 0;	/* # pages zero'd over time -- for ooh's and ahhh's */
unsigned long pageptr = 0;	/* current page being zero'd */
unsigned long zeropage_hits = 0;/* # zero'd pages request that we've done */

/*
 * Returns a pre-zero'd page from the list otherwise returns
 * NULL.
 */
unsigned long get_prezerod_page(void)
{
	unsigned long page;
	unsigned long s;

	if ( zero_list )
	{
		/* atomically remove this page from the list */
		asm (	"101:lwarx  %1,0,%2\n"  /* reserve zero_list */
			"    lwz    %0,0(%1)\n" /* get next -- new zero_list */
			"    stwcx. %0,0,%2\n"  /* update zero_list */
			"    bne-   101b\n"     /* if lost reservation try again */
			: "=&r" (zero_list), "=&r" (page)
			: "r" (&zero_list)
			: "cc" );
		/* we can update zerocount after the fact since it is not
		 * used for anything but control of a loop which doesn't
		 * matter since it won't effect anything if it zero's one
		 * less page -- Cort
		 */
		atomic_inc((atomic_t *)&zeropage_hits);
		atomic_dec((atomic_t *)&zerocount);
		/* zero out the pointer to next in the page */
		*(unsigned long *)page = 0;
		return page;
	}
	return 0;
}

/*
 * Experimental stuff to zero out pages in the idle task
 * to speed up get_free_pages() -- Cort
 * Zero's out pages until we need to resched or
 * we've reached the limit of zero'd pages.
 */
void inline zero_paged(void)
{
	extern pte_t *get_pte( struct mm_struct *mm, unsigned long address );
	unsigned long tmp;
	pte_t ptep;
	pgd_t *dir;
	pmd_t *pmd;
	pte_t *pte;

	sprintf(current->comm, "zero_paged");
	printk("Started zero_paged\n");
	/* want priority over idle task and powerd */
	current->priority = -98;
	current->counter = -98;
	__sti();
	
	while ( zerocount < 128 )
	{
		/*
		 * Mark a page as reserved so we can mess with it
		 * If we're interrupted we keep this page and our place in it
		 * since we validly hold it and it's reserved for us.
		 */
		pageptr = __get_free_pages(GFP_ATOMIC, 0, 0 );
		if ( !pageptr )
		{
			printk("!pageptr in zero_paged\n");
			goto retry;
		}
		
		if ( need_resched )
			schedule();
		
		/*
		 * Make the page no cache so we don't blow our cache with 0's
		 */
		dir = pgd_offset( init_task.mm, pageptr );
		if (dir)
		{
			pmd = pmd_offset(dir, pageptr & PAGE_MASK);
			if (pmd && pmd_present(*pmd))
			{
				pte = pte_offset(pmd, pageptr & PAGE_MASK);
				if (pte && pte_present(*pte))
				{			
					pte_uncache(*pte);
					flush_tlb_page(find_vma(init_task.mm,pageptr),pageptr);
				}
			}
		}
	
		/*
		 * Important here to not take time away from real processes.
		 */
		for ( bytecount = 0; bytecount < PAGE_SIZE ; bytecount += 4 )
		{
			if ( need_resched )
				schedule();
			*(unsigned long *)(bytecount + pageptr) = 0;
		}
		
		/*
		 * If we finished zero-ing out a page add this page to
		 * the zero_list atomically -- we can't use
		 * down/up since we can't sleep in idle.
		 * Disabling interrupts is also a bad idea since we would
		 * steal time away from real processes.
		 * We can also have several zero_paged's running
		 * on different processors so we can't interfere with them.
		 * So we update the list atomically without locking it.
		 * -- Cort
		 */
		/* turn cache on for this page */
		pte_cache(*pte);
		flush_tlb_page(find_vma(init_task.mm,pageptr),pageptr);
		
		/* atomically add this page to the list */
		asm (	"101:lwarx  %0,0,%1\n"  /* reserve zero_list */
			"    stw    %0,0(%2)\n" /* update *pageptr */
#ifdef __SMP__
			"    sync\n"            /* let store settle */
#endif			
			"    mr     %0,%2\n"    /* update zero_list in reg */
			"    stwcx. %2,0,%1\n"  /* update zero_list in mem */
			"    bne-   101b\n"     /* if lost reservation try again */
			: "=&r" (zero_list)
			: "r" (&zero_list), "r" (pageptr)
			: "cc" );
		/*
		 * This variable is used in the above loop and nowhere
		 * else so the worst that could happen is we would
		 * zero out one more or one less page than we want
		 * per processor on the machine.  This is because
		 * we could add our page to the list but not have
		 * zerocount updated yet when another processor
		 * reads it.  -- Cort
		 */
		atomic_inc((atomic_t *)&zerocount);
		atomic_inc((atomic_t *)&zerototal);
retry:	
		schedule();
	}
}

void powerd(void)
{
	unsigned long msr, hid0;

	sprintf(current->comm, "powerd");
	__sti();
	while (1)
	{
		/* want priority over idle task 'swapper' -- Cort */
		current->priority = -99;
		current->counter = -99;
		asm volatile(
			/* clear powersaving modes and set nap mode */
			"mfspr %3,1008 \n\t"
			"andc  %3,%3,%4 \n\t"
			"or    %3,%3,%5 \n\t"
			"mtspr 1008,%3 \n\t"
			/* enter the mode */
			"mfmsr %0 \n\t"
			"oris  %0,%0,%2 \n\t"
			"sync \n\t"
			"mtmsr %0 \n\t"
			"isync \n\t"
			: "=&r" (msr)
			: "0" (msr), "i" (MSR_POW>>16),
			"r" (hid0),
			"r" (HID0_DOZE|HID0_NAP|HID0_SLEEP),
			"r" (HID0_NAP));
		if ( need_resched )
			schedule();
		/*
		 * The ibm carolina spec says that the eagle memory
		 * controller will detect the need for a snoop
		 * and wake up the processor so we don't need to
		 * check for cache operations that need to be
		 * snooped.  The ppc book says the run signal
		 * must be asserted while napping for this though.
		 * -- Cort
		 */
	}
}
	
asmlinkage int sys_idle(void)
{
	int ret = -EPERM;
	if (current->pid != 0)
		goto out;

#ifdef IDLE_ZERO
	/*
	 * want one per cpu since it would be nice to have all
	 * processors who aren't doing anything
	 * zero-ing pages since this daemon is lock-free
	 * -- Cort
	 */
	kernel_thread(zero_paged, NULL, 0);
#endif /* IDLE_ZERO */

#ifdef CONFIG_POWERSAVING
	/* no powersaving modes on 601 - one per processor */
	if(  (_get_PVR()>>16) != 1 )
		kernel_thread(powerd, NULL, 0);
#endif /* CONFIG_POWERSAVING */
	
	/* endless loop with no priority at all */
	current->priority = -100;
	current->counter = -100;
	for (;;) 
	{
		schedule();
	}
	ret = 0;
out:
	return ret;
}

void show_regs(struct pt_regs * regs)
{
	int i;

	printk("NIP: %08X XER: %08X LR: %08X REGS: %08X TRAP: %04x\n",
	       regs->nip, regs->xer, regs->link, regs,regs->trap);
	printk("MSR: %08x EE: %01x PR: %01x FP: %01x ME: %01x IR/DR: %01x%01x\n",
	       regs->msr, regs->msr&MSR_EE ? 1 : 0, regs->msr&MSR_PR ? 1 : 0,
	       regs->msr & MSR_FP ? 1 : 0,regs->msr&MSR_ME ? 1 : 0,
	       regs->msr&MSR_IR ? 1 : 0,
	       regs->msr&MSR_DR ? 1 : 0);
	printk("TASK = %x[%d] '%s' mm->pgd %08X ",
	       current, current->pid, current->comm, current->mm->pgd);
	printk("Last syscall: %d ", current->tss.last_syscall);
	printk("\nlast math %08X\n", last_task_used_math);
	for (i = 0;  i < 32;  i++)
	{
		long r;
		if ((i % 8) == 0)
		{
			printk("GPR%02d: ", i);
		}

		if ( get_user(r, &(regs->gpr[i])) )
		    goto out;
		printk("%08X ", r);
		if ((i % 8) == 7)
		{
			printk("\n");
		}
	}
out:
}

void exit_thread(void)
{
	if (last_task_used_math == current)
		last_task_used_math = NULL;
}

void flush_thread(void)
{
	if (last_task_used_math == current)
		last_task_used_math = NULL;
}

void
release_thread(struct task_struct *t)
{
}

/*
  * Copy a thread..
  */
int
copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	    struct task_struct * p, struct pt_regs * regs)
{
	int i;
	struct pt_regs * childregs;
	/* Copy registers */
	childregs = ((struct pt_regs *)
		     ((unsigned long)p + sizeof(union task_union)
		      - STACK_FRAME_OVERHEAD)) - 2;
	*childregs = *regs;

	if ((childregs->msr & MSR_PR) == 0)
		childregs->gpr[2] = (unsigned long) p;	/* `current' in new task */
	childregs->gpr[3] = 0;  /* Result from fork() */
	p->tss.ksp = (unsigned long)(childregs) - STACK_FRAME_OVERHEAD;
	p->tss.regs = (struct pt_regs *)(childregs);
	if (usp >= (unsigned long)regs)
	{ /* Stack is in kernel space - must adjust */
		childregs->gpr[1] = (long)(childregs+1);
	} else
	{ /* Provided stack is in user space */
		childregs->gpr[1] = usp;
	}

	p->tss.last_syscall = -1;

	/*
	 * copy fpu info - assume lazy fpu switch now always
	 * this should really be conditional on whether or
	 * not the process has used the fpu
	 *  -- Cort
	 */
	if ( last_task_used_math == current )
		giveup_fpu();
	
	memcpy(&p->tss.fpr, &current->tss.fpr, sizeof(p->tss.fpr));
	p->tss.fpscr = current->tss.fpscr;
	childregs->msr &= ~MSR_FP;
	
	return 0;
}

asmlinkage int sys_fork(int p1, int p2, int p3, int p4, int p5, int p6,
			struct pt_regs *regs)
{
	int ret;
	lock_kernel();
	ret = do_fork(SIGCHLD, regs->gpr[1], regs);
	unlock_kernel();
	return ret;
}

asmlinkage int sys_execve(unsigned long a0, unsigned long a1, unsigned long a2,
			  unsigned long a3, unsigned long a4, unsigned long a5,
			  struct pt_regs *regs)
{
	int error;
	char * filename;
	filename = (int) getname((char *) a0);
	error = PTR_ERR(filename);
	if(IS_ERR(filename))
		goto out;
	if ( last_task_used_math == current )
		last_task_used_math = NULL;
	error = do_execve(filename, (char **) a1, (char **) a2, regs);

	putname(filename);

out:
	unlock_kernel();
	return error;
}

asmlinkage int sys_clone(int p1, int p2, int p3, int p4, int p5, int p6, struct pt_regs *regs)
{
	unsigned long clone_flags = p1;
	int res;
	
	lock_kernel();
	res = do_fork(clone_flags, regs->gpr[1], regs);
	unlock_kernel();
	return res;
}

void
print_backtrace(unsigned long *sp)
{
	int cnt = 0;
	int i;
	printk("Call backtrace: ");
	while ( !get_user(i, sp) && i)
	{
		if ( get_user( i, &sp[1] ) )
			return;
		printk("%08X ", i);
		if ( get_user( (ulong)sp, sp) )
		    return;
		if (cnt == 6 ) cnt = 7; /* wraparound early -- Cort */
		if (++cnt == 8)
		{
			printk("\n");
		}
		if (cnt > 32) break;
	}
	printk("\n");
}

inline void start_thread(struct pt_regs * regs,
                         unsigned long eip, unsigned long esp)
{
	set_fs(USER_DS);
	regs->nip = eip;
	regs->gpr[1] = esp;
	regs->msr = MSR_USER;
}

/*
 * Low level print for debugging - Cort
 */
int ll_printk(const char *fmt, ...)
{
        va_list args;
	char buf[256];
        int i;

        va_start(args, fmt);
        i=sprintf(buf,fmt,args);
	ll_puts(buf);
        va_end(args);
        return i;
}

char *vidmem = (char *)0xC00B8000;
int lines = 24, cols = 80;
int orig_x = 0, orig_y = 0;

void ll_puts(const char *s)
{
	int x,y;
	char c;

	x = orig_x;
	y = orig_y;

	while ( ( c = *s++ ) != '\0' ) {
		if ( c == '\n' ) {
			x = 0;
			if ( ++y >= lines ) {
				/*scroll();*/
				/*y--;*/
				y = 0;
			}
		} else {
			vidmem [ ( x + cols * y ) * 2 ] = c; 
			if ( ++x >= cols ) {
				x = 0;
				if ( ++y >= lines ) {
					/*scroll();*/
					/*y--;*/
					y = 0;
				}
			}
		}
	}

	orig_x = x;
	orig_y = y;
}
