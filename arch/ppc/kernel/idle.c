/*
 * $Id: idle.c,v 1.37 1998/04/26 06:59:12 cort Exp $
 *
 * Idle daemon for PowerPC.  Idle daemon will handle any action
 * that needs to be taken when the system becomes idle.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#define __KERNEL_SYSCALLS__

#include <linux/config.h>
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

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp_lock.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#ifdef CONFIG_PMAC
#include <asm/mediabay.h>
#endif

void zero_paged(void);
void power_save(void);
void inline htab_reclaim(void);

unsigned long htab_reclaim_on = 0;
unsigned long zero_paged_on = 0;

int idled(void *unused)
{
	int ret = -EPERM;

	for (;;)
	{
		__sti();
		
		/* endless loop with no priority at all */
		current->priority = -100;
		current->counter = -100;
		
		check_pgt_cache();

		if ( !need_resched && zero_paged_on ) zero_paged();
		if ( !need_resched && htab_reclaim_on ) htab_reclaim();

		/*
		 * Only processor 1 may sleep now since processor 2 would
		 * never wake up.  Need to add timer code for processor 2
		 * then it can sleep. -- Cort
		 */
#ifndef __SMP__
		if ( !need_resched ) power_save();
#endif /* __SMP__ */
		schedule();
	}
	ret = 0;
	return ret;
}


/*
 * Mark 'zombie' pte's in the hash table as invalid.
 * This improves performance for the hash table reload code
 * a bit since we don't consider unused pages as valid.
 *  -- Cort
 */
PTE *reclaim_ptr = 0;
void inline htab_reclaim(void)
{
#ifndef CONFIG_8xx		
#if 0	
	PTE *ptr, *start;
	static int dir = 1;
#endif	
	struct task_struct *p;
	unsigned long valid = 0;
	extern PTE *Hash, *Hash_end;
	extern unsigned long Hash_size;

	/* if we don't have a htab */
	if ( Hash_size == 0 )
		return;
	lock_dcache(1);

#if 0	
	/* find a random place in the htab to start each time */
	start = &Hash[jiffies%(Hash_size/sizeof(PTE))];
	/* go a different direction each time */
	dir *= -1;
        for ( ptr = start;
	      !need_resched && (ptr != Hash_end) && (ptr != Hash);
	      ptr += dir)
	{
#else
	if ( !reclaim_ptr ) reclaim_ptr = Hash;
	while ( !need_resched )
	{
		reclaim_ptr++;
		if ( reclaim_ptr == Hash_end ) reclaim_ptr = Hash;
#endif	  
		if (!reclaim_ptr->v)
			continue;
		valid = 0;
		for_each_task(p)
		{
			if ( need_resched )
				goto out;
			/* if this vsid/context is in use */
			if ( (reclaim_ptr->vsid >> 4) == p->mm->context )
			{
				valid = 1;
				break;
			}
		}
		if ( valid )
			continue;
		/* this pte isn't used */
		reclaim_ptr->v = 0;
	}
out:
	if ( need_resched ) printk("need_resched: %x\n", need_resched);
	unlock_dcache();
#endif /* CONFIG_8xx */
}
	
/*
 * Syscall entry into the idle task. -- Cort
 */
asmlinkage int sys_idle(void)
{
	extern int media_bay_task(void *);
	if(current->pid != 0)
		return -EPERM;

#ifdef CONFIG_PMAC
	if (media_bay_present)
		kernel_thread(media_bay_task, NULL, 0);
#endif

	idled(NULL);
	return 0; /* should never execute this but it makes gcc happy -- Cort */
}

#ifdef __SMP__
/*
 * SMP entry into the idle task - calls the same thing as the
 * non-smp versions. -- Cort
 */
int cpu_idle(void *unused)
{
	idled(unused);
	return 0; 
}
#endif /* __SMP__ */

/*
 * vars for idle task zero'ing out pages
 */
unsigned long zero_list = 0;	/* head linked list of pre-zero'd pages */
unsigned long bytecount = 0;	/* pointer into the currently being zero'd page */
unsigned long zerocount = 0;	/* # currently pre-zero'd pages */
unsigned long zerototal = 0;	/* # pages zero'd over time -- for ooh's and ahhh's */
unsigned long zeropage_hits = 0;/* # zero'd pages request that we've done */
unsigned long zeropage_calls = 0;/* # zero'd pages request that've been made */
#define PAGE_THRESHOLD 96       /* how many pages to keep pre-zero'd */

/*
 * Returns a pre-zero'd page from the list otherwise returns
 * NULL.
 */
unsigned long get_prezerod_page(void)
{
	unsigned long page;

	atomic_inc((atomic_t *)&zeropage_calls);
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
		need_resched = 1;
		
		/* zero out the pointer to next in the page */
		*(unsigned long *)page = 0;
		return page;
	}
	return 0;
}

/*
 * Experimental stuff to zero out pages in the idle task
 * to speed up get_free_pages(). Zero's out pages until
 * we've reached the limit of zero'd pages.  We handle
 * reschedule()'s in here so when we return we know we've
 * zero'd all we need to for now.
 */
void zero_paged(void)
{
	unsigned long pageptr = 0;	/* current page being zero'd */
	pte_t *pte;
	
	while ( zerocount <= PAGE_THRESHOLD )
	{
		/*
		 * Mark a page as reserved so we can mess with it
		 * If we're interrupted we keep this page and our place in it
		 * since we validly hold it and it's reserved for us.
		 */
		pageptr = __get_free_pages(GFP_ATOMIC, 0);
		if ( !pageptr )
			return;
		
		if ( need_resched )
			schedule();
		
		/*
		 * Make the page no cache so we don't blow our cache with 0's
		 */
		pte = find_pte(init_task.mm, pageptr);
		if ( !pte )
		{
			printk("pte NULL in zero_paged()\n");
			return;
		}
		
		pte_uncache(*pte);
		flush_tlb_page(find_vma(init_task.mm,pageptr),pageptr);
	
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
	}
}

int powersave_mode = HID0_DOZE;

void power_save(void)
{
	unsigned long msr, hid0;

	/* only sleep on the 603-family/750 processors */
	switch (_get_PVR() >> 16) {
	case 3:			/* 603 */
	case 6:			/* 603e */
	case 7:			/* 603ev */
	case 8:			/* 750 */
		save_flags(msr);
		cli();
		if (!need_resched) {
			asm("mfspr %0,1008" : "=r" (hid0) :);
			hid0 &= ~(HID0_NAP | HID0_SLEEP | HID0_DOZE);
			hid0 |= powersave_mode | HID0_DPM;
			asm("mtspr 1008,%0" : : "r" (hid0));
			msr |= MSR_POW;
		}
		restore_flags(msr);
	default:
		return;
	}
	
}
