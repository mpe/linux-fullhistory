/*
 * BK Id: SCCS/s.idle.c 1.16 10/16/01 15:58:42 trini
 */
/*
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
#include <linux/slab.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#include <asm/cputable.h>

void zero_paged(void);
void power_save(void);

unsigned long zero_paged_on = 0;
unsigned long powersave_nap = 0;

unsigned long *zero_cache;    /* head linked list of pre-zero'd pages */
atomic_t zerototal;      /* # pages zero'd over time */
atomic_t zeropage_hits;  /* # zero'd pages request that we've done */
atomic_t zero_sz;	      /* # currently pre-zero'd pages */
atomic_t zeropage_calls; /* # zero'd pages request that've been made */

int idled(void)
{
	int do_power_save = 0;

	if (cur_cpu_spec[smp_processor_id()]->cpu_features & CPU_FTR_CAN_DOZE)
		do_power_save = 1;

	/* endless loop with no priority at all */
	current->nice = 20;
	current->counter = -100;
	init_idle();
	for (;;) {
#ifdef CONFIG_SMP

		if (!do_power_save) {
			/*
			 * Deal with another CPU just having chosen a thread to
			 * run here:
			 */
			int oldval = xchg(&current->need_resched, -1);

			if (!oldval) {
				while(current->need_resched == -1)
					; /* Do Nothing */
			}
		}
#endif
		if (do_power_save && !current->need_resched)
			power_save();

		if (current->need_resched) {
			schedule();
			check_pgt_cache();
		}
	}
	return 0;
}

/*
 * SMP entry into the idle task - calls the same thing as the
 * non-smp versions. -- Cort
 */
int cpu_idle(void)
{
	idled();
	return 0; 
}

#if 0
/*
 * Returns a pre-zero'd page from the list otherwise returns
 * NULL.
 */
unsigned long get_zero_page_fast(void)
{
	unsigned long page = 0;

	atomic_inc(&zero_cache_calls);
	if ( zero_quicklist )
	{
		/* atomically remove this page from the list */
		register unsigned long tmp;
		asm (	"101:lwarx  %1,0,%3\n"  /* reserve zero_cache */
			"    lwz    %0,0(%1)\n" /* get next -- new zero_cache */
			"    stwcx. %0,0,%3\n"  /* update zero_cache */
			"    bne-   101b\n"     /* if lost reservation try again */
			: "=&r" (tmp), "=&r" (page), "+m" (zero_cache)
			: "r" (&zero_quicklist)
			: "cc" );
#ifdef CONFIG_SMP
		/* if another cpu beat us above this can happen -- Cort */
		if ( page == 0 ) 
			return 0;
#endif /* CONFIG_SMP */		
		/* we can update zerocount after the fact since it is not
		 * used for anything but control of a loop which doesn't
		 * matter since it won't affect anything if it zeros one
		 * less page -- Cort
		 */
		atomic_inc((atomic_t *)&zero_cache_hits);
		atomic_dec((atomic_t *)&zero_cache_sz);
		
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
int zero_cache_water[2] = { 25, 96 }; /* high and low water marks for zero cache */
void zero_paged(void)
{
	unsigned long pageptr = 0;	/* current page being zero'd */
	unsigned long bytecount = 0;  
        register unsigned long tmp;
	pte_t *pte;

	if ( atomic_read(&zero_cache_sz) >= zero_cache_water[0] )
		return;
	while ( (atomic_read(&zero_cache_sz) < zero_cache_water[1]) && (!current->need_resched) )
	{
		/*
		 * Mark a page as reserved so we can mess with it
		 * If we're interrupted we keep this page and our place in it
		 * since we validly hold it and it's reserved for us.
		 */
		pageptr = __get_free_pages(GFP_ATOMIC, 0);
		if ( !pageptr )
			return;
		
		if ( current->need_resched )
			schedule();
		
		/*
		 * Make the page no cache so we don't blow our cache with 0's
		 */
		pte = find_pte(&init_mm, pageptr);
		if ( !pte )
		{
			printk("pte NULL in zero_paged()\n");
			return;
		}
		
		pte_uncache(*pte);
		flush_tlb_page(find_vma(&init_mm,pageptr),pageptr);
		/*
		 * Important here to not take time away from real processes.
		 */
		for ( bytecount = 0; bytecount < PAGE_SIZE ; bytecount += 4 )
		{
			if ( current->need_resched )
				schedule();
			*(unsigned long *)(bytecount + pageptr) = 0;
		}
		
		/*
		 * If we finished zero-ing out a page add this page to
		 * the zero_cache atomically -- we can't use
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
		flush_tlb_page(find_vma(&init_mm,pageptr),pageptr);
		/* atomically add this page to the list */
		asm (	"101:lwarx  %0,0,%2\n"  /* reserve zero_cache */
			"    stw    %0,0(%3)\n" /* update *pageptr */
#ifdef CONFIG_SMP
			"    sync\n"            /* let store settle */
#endif			
			"    stwcx. %3,0,%2\n"  /* update zero_cache in mem */
			"    bne-   101b\n"     /* if lost reservation try again */
			: "=&r" (tmp), "+m" (zero_quicklist)
			: "r" (&zero_quicklist), "r" (pageptr)
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
		atomic_inc((atomic_t *)&zero_cache_sz);
		atomic_inc((atomic_t *)&zero_cache_total);
	}
}
#endif /* 0 */

void power_save(void)
{
	unsigned long hid0;
	/*
	 * Disable interrupts to prevent a lost wakeup
	 * when going to sleep.  This is necessary even with
	 * RTLinux since we are not guaranteed an interrupt
	 * didn't come in and is waiting for a __sti() before
	 * emulating one.  This way, we really do hard disable.
	 * 
	 * We assume that we're sti-ed when we come in here.  We
	 * are in the idle loop so if we're cli-ed then it's a bug
	 * anyway.
	 *  -- Cort
	 */
	_nmask_and_or_msr(MSR_EE, 0);
	if (!current->need_resched)
	{
		asm("mfspr %0,1008" : "=r" (hid0) :);
		hid0 &= ~(HID0_NAP | HID0_SLEEP | HID0_DOZE);
		hid0 |= (powersave_nap? HID0_NAP: HID0_DOZE) | HID0_DPM;
		asm("mtspr 1008,%0" : : "r" (hid0));
		
		/* set the POW bit in the MSR, and enable interrupts
		 * so we wake up sometime! */
		_nmask_and_or_msr(0, MSR_POW | MSR_EE);
	}
	_nmask_and_or_msr(0, MSR_EE);
}

