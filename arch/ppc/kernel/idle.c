/*
 * $Id: idle.c,v 1.3 1997/08/10 04:49:08 davem Exp $
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
#include <linux/config.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp_lock.h>
#include <asm/processor.h>

int zero_paged(void *unused);
int power_saved(void *unused);

asmlinkage int sys_idle(void)
{
	int ret = -EPERM;

	if (current->pid != 0)
		goto out;

	/*
	 * want one per cpu since it would be nice to have all
	 * processors who aren't doing anything
	 * zero-ing pages since this daemon is lock-free
	 * -- Cort
	 */
	kernel_thread(zero_paged, NULL, 0);
	/* no powersaving modes on 601 */
	/*if(  (_get_PVR()>>16) != 1 )
		kernel_thread(power_saved, NULL, 0);*/

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

/*
 * vars for idle task zero'ing out pages
 */
unsigned long zero_list = 0;	/* head linked list of pre-zero'd pages */
unsigned long bytecount = 0;	/* pointer into the currently being zero'd page */
unsigned long zerocount = 0;	/* # currently pre-zero'd pages */
unsigned long zerototal = 0;	/* # pages zero'd over time -- for ooh's and ahhh's */
unsigned long pageptr = 0;	/* current page being zero'd */
unsigned long zeropage_hits = 0;/* # zero'd pages request that we've done */
unsigned long zeropage_calls = 0;/* # zero'd pages request that've been made */
static struct wait_queue * page_zerod_wait = NULL;
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
		wake_up(&page_zerod_wait);
		resched_force();
		
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

int zero_paged(void *unused)
{
	extern pte_t *get_pte( struct mm_struct *mm, unsigned long address );
	pgd_t *dir;
	pmd_t *pmd;
	pte_t *pte;

	sprintf(current->comm, "zero_paged (idle)");
	current->blocked = ~0UL;
	
	printk("Started zero_paged\n");
	
	__sti();
	while ( 1 )
	{
		/* don't want to be pre-empted by swapper or power_saved */
		current->priority = -98;
		current->counter = -98;
		/* we don't want to run until we have something to do */
		while ( zerocount >= PAGE_THRESHOLD )
			sleep_on(&page_zerod_wait);
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
		
		if ( resched_needed() )
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
			if ( resched_needed() )
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

int power_saved(void *unused)
{
	unsigned long msr, hid0;
	sprintf(current->comm, "power_saved (idle)");
	current->blocked = ~0UL;
	
	printk("Power saving daemon started\n");
	
	__sti();
	while (1)
	{
		/* don't want to be pre-empted by swapper */
		current->priority = -99;
		current->counter = -99;
		/* go ahead and wakeup page_zerod() */
		wake_up(&page_zerod_wait);
		schedule();
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
		/*
		 * The ibm carolina spec says that the eagle memory
		 * controller will detect the need for a snoop
		 * and wake up the processor so we don't need to
		 * check for cache operations that need to be
		 * snooped.  The ppc book says the run signal
		 * must be asserted while napping for this though.
		 *
		 * Paul, what do you know about the pmac here?
		 * -- Cort
		 */
		schedule();
	}
}
