/*
 * linux/kernel/swsusp.c
 *
 * This file is to realize architecture-independent
 * machine suspend feature using pretty near only high-level routines
 *
 * Copyright (C) 1998-2001 Gabor Kuti <seasons@fornax.hu>
 * Copyright (C) 1998,2001,2002 Pavel Machek <pavel@suse.cz>
 *
 * I'd like to thank the following people for their work:
 * 
 * Pavel Machek <pavel@ucw.cz>:
 * Modifications, defectiveness pointing, being with me at the very beginning,
 * suspend to swap space, stop all tasks.
 *
 * Steve Doddi <dirk@loth.demon.co.uk>: 
 * Support the possibility of hardware state restoring.
 *
 * Raph <grey.havens@earthling.net>:
 * Support for preserving states of network devices and virtual console
 * (including X and svgatextmode)
 *
 * Kurt Garloff <garloff@suse.de>:
 * Straightened the critical function in order to prevent compilers from
 * playing tricks with local variables.
 *
 * Andreas Mohr <a.mohr@mailto.de>
 *
 * Alex Badea <vampire@go.ro>:
 * Fixed runaway init
 *
 * More state savers are welcome. Especially for the scsi layer...
 *
 * For TODOs,FIXMEs also look in Documentation/swsusp.txt
 */

/*
 * TODO:
 *
 * - we should launch a kernel_thread to process suspend request, cleaning up
 * bdflush from this task. (check apm.c for something similar).
 */

/* FIXME: try to poison to memory */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/swapctl.h>
#include <linux/suspend.h>
#include <linux/smp_lock.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/compile.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/vt_kern.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/kbd_kern.h>
#include <linux/keyboard.h>
#include <linux/spinlock.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/blk.h>
#include <linux/swap.h>
#include <linux/pm.h>
#include <linux/device.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/io.h>

unsigned char software_suspend_enabled = 0;

/* #define SUSPEND_CONSOLE	(MAX_NR_CONSOLES-1) */
/* With SUSPEND_CONSOLE defined, it suspend looks *really* cool, but
   we probably do not take enough locks for switching consoles, etc,
   so bad things might happen.
*/
#ifndef CONFIG_VT
#undef SUSPEND_CONSOLE
#endif

#define TIMEOUT	(6 * HZ)			/* Timeout for stopping processes */
#define ADDRESS(x) ((unsigned long) phys_to_virt(((x) << PAGE_SHIFT)))

extern void wakeup_bdflush(void);
extern int C_A_D;

/* References to section boundaries */
extern char _text, _etext, _edata, __bss_start, _end;
extern char __nosave_begin, __nosave_end;

extern int console_loglevel;
extern int is_head_of_free_region(struct page *);

/* Locks */
spinlock_t suspend_pagedir_lock __nosavedata = SPIN_LOCK_UNLOCKED;

/* Variables to be preserved over suspend */
static int new_loglevel = 7;
static int orig_loglevel = 0;
static int orig_fgconsole;
static int pagedir_order_check;
static int nr_copy_pages_check;

static int resume_status = 0;
static char resume_file[256] = "";			/* For resume= kernel option */
static kdev_t resume_device;
/* Local variables that should not be affected by save */
static unsigned int nr_copy_pages __nosavedata = 0;

static int pm_suspend_state = 0;

/* Suspend pagedir is allocated before final copy, therefore it
   must be freed after resume 

   Warning: this is evil. There are actually two pagedirs at time of
   resume. One is "pagedir_save", which is empty frame allocated at
   time of suspend, that must be freed. Second is "pagedir_nosave", 
   allocated at time of resume, that travells through memory not to
   collide with anything.
 */
static suspend_pagedir_t *pagedir_nosave __nosavedata = NULL;
static unsigned long pagedir_save;
static int pagedir_order __nosavedata = 0;

struct link {
	char dummy[PAGE_SIZE - sizeof(swp_entry_t)];
	swp_entry_t next;
};

union diskpage {
	union swap_header swh;
	struct link link;
	struct suspend_header sh;
};

/*
 * XXX: We try to keep some more pages free so that I/O operations succeed
 * without paging. Might this be more?
 *
 * [If this is not enough, might it corrupt our data silently?]
 */
#define PAGES_FOR_IO	512

static const char *name_suspend = "Suspend Machine: ";
static const char *name_resume = "Resume Machine: ";

/*
 * Debug
 */
#define	DEBUG_DEFAULT	1
#undef	DEBUG_PROCESS
#undef	DEBUG_SLOW
#define TEST_SWSUSP 0		/* Set to 1 to reboot instead of halt machine after suspension */

#ifdef DEBUG_DEFAULT
#define PRINTD(func, f, a...)	\
	do { \
		printk("%s", func); \
		printk(f, ## a); \
	} while(0)
#define PRINTS(f, a...)	PRINTD(name_suspend, f, ## a)
#define PRINTR(f, a...)	PRINTD(name_resume, f, ## a)
#define PRINTK(f, a...)	printk(f, ## a)
#else
#define PRINTD(func, f, a...)
#define PRINTS(f, a...)
#define PRINTR(f, a...)
#define PRINTK(f, a...)
#endif
#ifdef DEBUG_SLOW
#define MDELAY(a) mdelay(a)
#else
#define MDELAY(a)
#endif

/*
 * Refrigerator and related stuff
 */

#define INTERESTING(p) \
			/* We don't want to touch kernel_threads..*/ \
			if (p->flags & PF_IOTHREAD) \
				continue; \
			if (p == current) \
				continue; \
			if (p->state == TASK_ZOMBIE) \
				continue;

/* Refrigerator is place where frozen processes are stored :-). */
void refrigerator(unsigned long flag)
{
	/* You need correct to work with real-time processes.
	   OTOH, this way one process may see (via /proc/) some other
	   process in stopped state (and thereby discovered we were
	   suspended. We probably do not care. 
	 */
	long save;
	save = current->state;
	current->state = TASK_STOPPED;
//	PRINTK("%s entered refrigerator\n", current->comm);
	printk(":");
	current->flags &= ~PF_FREEZE;
	if (flag)
		flush_signals(current); /* We have signaled a kernel thread, which isn't normal behaviour
					   and that may lead to 100%CPU sucking because those threads
					   just don't manage signals. */
	current->flags |= PF_FROZEN;
	while (current->flags & PF_FROZEN)
		schedule();
//	PRINTK("%s left refrigerator\n", current->comm);
	printk(":");
	current->state = save;
}

/* 0 = success, else # of processes that we failed to stop */
int freeze_processes(void)
{
	int todo, start_time;
	struct task_struct *p;
	
	PRINTS( "Waiting for tasks to stop... " );
	
	start_time = jiffies;
	do {
		todo = 0;
		read_lock(&tasklist_lock);
		for_each_task(p) {
			unsigned long flags;
			INTERESTING(p);
			if (p->flags & PF_FROZEN)
				continue;

			/* FIXME: smp problem here: we may not access other process' flags
			   without locking */
			p->flags |= PF_FREEZE;
			spin_lock_irqsave(&p->sigmask_lock, flags);
			signal_wake_up(p);
			spin_unlock_irqrestore(&p->sigmask_lock, flags);
			todo++;
		}
		read_unlock(&tasklist_lock);
		sys_sched_yield();
		schedule();
		if (time_after(jiffies, start_time + TIMEOUT)) {
			PRINTK( "\n" );
			printk(KERN_ERR " stopping tasks failed (%d tasks remaining)\n", todo );
			return todo;
		}
	} while(todo);
	
	PRINTK( " ok\n" );
	return 0;
}

void thaw_processes(void)
{
	struct task_struct *p;

	PRINTR( "Restarting tasks..." );
	read_lock(&tasklist_lock);
	for_each_task(p) {
		INTERESTING(p);
		
		if (p->flags & PF_FROZEN) p->flags &= ~PF_FROZEN;
		else
			printk(KERN_INFO " Strange, %s not stopped\n", p->comm );
		wake_up_process(p);
	}
	read_unlock(&tasklist_lock);
	PRINTK( " done\n" );
	MDELAY(500);
}

/*
 * Saving part...
 */

static __inline__ int fill_suspend_header(struct suspend_header *sh)
{
	memset((char *)sh, 0, sizeof(*sh));

	sh->version_code = LINUX_VERSION_CODE;
	sh->num_physpages = num_physpages;
	strncpy(sh->machine, system_utsname.machine, 8);
	strncpy(sh->version, system_utsname.version, 20);
	sh->num_cpus = smp_num_cpus;
	sh->page_size = PAGE_SIZE;
	sh->suspend_pagedir = (unsigned long)pagedir_nosave;
	if (pagedir_save != pagedir_nosave)
		panic("Must not happen");
	sh->num_pbes = nr_copy_pages;
	/* TODO: needed? mounted fs' last mounted date comparison
	 * [so they haven't been mounted since last suspend.
	 * Maybe it isn't.] [we'd need to do this for _all_ fs-es]
	 */
	return 0;
}

/*
 * This is our sync function. With this solution we probably won't sleep
 * but that should not be a problem since tasks are stopped..
 */

static void do_suspend_sync(void)
{
//	sync_dev(0); FIXME
	while (1) {
		run_task_queue(&tq_disk);
		if (!TQ_ACTIVE(tq_disk))
			break;
		printk(KERN_ERR "Hm, tq_disk is not empty after run_task_queue\n");
	}
}

/* We memorize in swapfile_used what swap devices are used for suspension */
#define SWAPFILE_UNUSED    0
#define SWAPFILE_SUSPEND   1	/* This is the suspending device */
#define SWAPFILE_IGNORED   2	/* Those are other swap devices ignored for suspension */

static unsigned short swapfile_used[MAX_SWAPFILES];
static unsigned short root_swap;
#define MARK_SWAP_SUSPEND 0
#define MARK_SWAP_RESUME 2

static void mark_swapfiles(swp_entry_t prev, int mode)
{
	swp_entry_t entry;
	union diskpage *cur;
	
	cur = (union diskpage *)get_free_page(GFP_ATOMIC);
	if (!cur)
		panic("Out of memory in mark_swapfiles");
	/* XXX: this is dirty hack to get first page of swap file */
	entry = SWP_ENTRY(root_swap, 0);
	lock_page(virt_to_page((unsigned long)cur));
	rw_swap_page_nolock(READ, entry, (char *) cur);

	if (mode == MARK_SWAP_RESUME) {
	  	if (!memcmp("SUSP1R",cur->swh.magic.magic,6))
		  	memcpy(cur->swh.magic.magic,"SWAP-SPACE",10);
		else if (!memcmp("SUSP2R",cur->swh.magic.magic,6))
			memcpy(cur->swh.magic.magic,"SWAPSPACE2",10);
		else printk("%sUnable to find suspended-data signature (%.10s - misspelled?\n", 
		      	name_resume, cur->swh.magic.magic);
	} else {
	  	if ((!memcmp("SWAP-SPACE",cur->swh.magic.magic,10)))
		  	memcpy(cur->swh.magic.magic,"SUSP1R....",10);
		else if ((!memcmp("SWAPSPACE2",cur->swh.magic.magic,10)))
			memcpy(cur->swh.magic.magic,"SUSP2R....",10);
		else panic("\nSwapspace is not swapspace (%.10s)\n", cur->swh.magic.magic);
		cur->link.next = prev; /* prev is the first/last swap page of the resume area */
		/* link.next lies *no more* in last 4 bytes of magic */
	}
	lock_page(virt_to_page((unsigned long)cur));
	rw_swap_page_nolock(WRITE, entry, (char *)cur);
	
	free_page((unsigned long)cur);
}

static void read_swapfiles(void) /* This is called before saving image */
{
	int i, len;
	
	len=strlen(resume_file);
	root_swap = 0xFFFF;
	
	swap_list_lock();
	for(i=0; i<MAX_SWAPFILES; i++) {
		if (swap_info[i].flags == 0) {
			swapfile_used[i]=SWAPFILE_UNUSED;
		} else {
			if(!len) {
	    			printk(KERN_WARNING "resume= option should be used to set suspend device" );
				if(root_swap == 0xFFFF) {
					swapfile_used[i] = SWAPFILE_SUSPEND;
					root_swap = i;
				} else
					swapfile_used[i] = SWAPFILE_IGNORED;				  
			} else {
	  			/* we ignore all swap devices that are not the resume_file */
				if (1) {
// FIXME				if(resume_device == swap_info[i].swap_device) {
					swapfile_used[i] = SWAPFILE_SUSPEND;
					root_swap = i;
				} else {
#if 0
					PRINTS( "device %s (%x != %x) ignored\n", swap_info[i].swap_file->d_name.name, swap_info[i].swap_device, resume_device );				  
#endif
				  	swapfile_used[i] = SWAPFILE_IGNORED;
				}
			}
		}
	}
	swap_list_unlock();
}

static void lock_swapdevices(void) /* This is called after saving image so modification
				      will be lost after resume... and that's what we want. */
{
	int i;

	swap_list_lock();
	for(i = 0; i< MAX_SWAPFILES; i++)
		if(swapfile_used[i] == SWAPFILE_IGNORED) {
//			PRINTS( "device %s locked\n", swap_info[i].swap_file->d_name.name );
			swap_info[i].flags ^= 0xFF; /* we make the device unusable. A new call to
						       lock_swapdevices can unlock the devices. */
		}
	swap_list_unlock();
}

kdev_t suspend_device;

static int write_suspend_image(void)
{
	int i;
	swp_entry_t entry, prev = { 0 };
	int nr_pgdir_pages = SUSPEND_PD_PAGES(nr_copy_pages);
	union diskpage *cur,  *buffer = (union diskpage *)get_free_page(GFP_ATOMIC);
	unsigned long address;

	PRINTS( "Writing data to swap (%d pages): ", nr_copy_pages );
	for (i=0; i<nr_copy_pages; i++) {
		if (!(i%100))
			PRINTK( "." );
		if (!(entry = get_swap_page()).val)
			panic("\nNot enough swapspace when writing data" );
		
		if(swapfile_used[SWP_TYPE(entry)] != SWAPFILE_SUSPEND)
			panic("\nPage %d: not enough swapspace on suspend device", i );
	    
		address = (pagedir_nosave+i)->address;
		lock_page(virt_to_page(address));
		{
			long dummy1, dummy2;
			get_swaphandle_info(entry, &dummy1, &suspend_device);
		}
		rw_swap_page_nolock(WRITE, entry, (char *) address);
		(pagedir_nosave+i)->swap_address = entry;
	}
	PRINTK(" done\n");
	PRINTS( "Writing pagedir (%d pages): ", nr_pgdir_pages);
	for (i=0; i<nr_pgdir_pages; i++) {
		cur = (union diskpage *)((char *) pagedir_nosave)+i;
		if ((char *) cur != (((char *) pagedir_nosave) + i*PAGE_SIZE))
			panic("Something is of wrong size");
		PRINTK( "." );
		if (!(entry = get_swap_page()).val) {
			printk(KERN_CRIT "Not enough swapspace when writing pgdir\n" );
			panic("Don't know how to recover");
			free_page((unsigned long) buffer);
			return -ENOSPC;
		}

		if(swapfile_used[SWP_TYPE(entry)] != SWAPFILE_SUSPEND)
		  panic("\nNot enough swapspace for pagedir on suspend device" );

		if (sizeof(swp_entry_t) != sizeof(long))
			panic("I need swp_entry_t to be sizeof long, otherwise next assignment could damage pagedir");
		if (PAGE_SIZE % sizeof(struct pbe))
			panic("I need PAGE_SIZE to be integer multiple of struct pbe, otherwise next assignment could damage pagedir");
		cur->link.next = prev;				
		lock_page(virt_to_page((unsigned long)cur));
		rw_swap_page_nolock(WRITE, entry, (char *) cur);
		prev = entry;
	}
	PRINTK(", header");
	if (sizeof(struct suspend_header) > PAGE_SIZE-sizeof(swp_entry_t))
		panic("sizeof(struct suspend_header) too big: %d",
				sizeof(struct suspend_header));
	if (sizeof(union diskpage) != PAGE_SIZE)
		panic("union diskpage has bad size");
	if (!(entry = get_swap_page()).val)
		panic( "\nNot enough swapspace when writing header" );
	if(swapfile_used[SWP_TYPE(entry)] != SWAPFILE_SUSPEND)
	  panic("\nNot enough swapspace for header on suspend device" );

	cur = (void *) buffer;
	if(fill_suspend_header(&cur->sh))
		panic("\nOut of memory while writing header");
		
	cur->link.next = prev;

	lock_page(virt_to_page((unsigned long)cur));
	rw_swap_page_nolock(WRITE, entry, (char *) cur);
	prev = entry;

	PRINTK( ", signature" );
#if 0
	if (SWP_TYPE(entry) != 0)
		panic("Need just one swapfile");
#endif
	mark_swapfiles(prev, MARK_SWAP_SUSPEND);
	PRINTK( ", done\n" );

	MDELAY(1000);
	free_page((unsigned long) buffer);
	return 0;
}

/* if pagedir_p != NULL it also copies the counted pages */
static int count_and_copy_data_pages(struct pbe *pagedir_p)
{
	int chunk_size;
	int nr_copy_pages = 0;
	int loop;
	
	if (max_mapnr != num_physpages)
		panic("mapnr is not expected");
	for(loop = 0; loop < max_mapnr; loop++) {
		if(PageHighMem(mem_map+loop))
			panic("No highmem for me, sorry.");
		if(!PageReserved(mem_map+loop)) {
			if(PageNosave(mem_map+loop))
				continue;

			if((chunk_size=is_head_of_free_region(mem_map+loop))!=0) {
				loop += chunk_size - 1;
				continue;
			}
		} else if(PageReserved(mem_map+loop)) {
			if(PageNosave(mem_map+loop))
				panic("What?");

			/*
			 * Just copy whole code segment. Hopefully it is not that big.
			 */
			if (ADDRESS(loop) >= (unsigned long)
				&__nosave_begin && ADDRESS(loop) < 
				(unsigned long)&__nosave_end) {
				printk("[nosave]");
				continue;
			}
			/* Hmm, perhaps copying all reserved pages is not too healthy as they may contain 
			   critical bios data? */
		} else panic("No third thing should be possible");

		nr_copy_pages++;
		if(pagedir_p) {
			pagedir_p->orig_address = ADDRESS(loop);
			copy_page(pagedir_p->address, pagedir_p->orig_address);
			pagedir_p++;
		}
	}
	return nr_copy_pages;
}

static void free_suspend_pagedir(unsigned long this_pagedir)
{
	struct page *page = mem_map;
	int i;
	unsigned long this_pagedir_end = this_pagedir +
		(PAGE_SIZE << pagedir_order);

	for(i=0; i < num_physpages; i++, page++) {
		if(!TestClearPageNosave(page))
			continue;

		if(ADDRESS(i) >= this_pagedir && ADDRESS(i) < this_pagedir_end)
			continue; /* old pagedir gets freed in one */
		
		free_page(ADDRESS(i));
	}
	free_pages(this_pagedir, pagedir_order);
}

static suspend_pagedir_t *create_suspend_pagedir(int nr_copy_pages)
{
	int i;
	suspend_pagedir_t *pagedir;
	struct pbe *p;
	struct page *page;

	pagedir_order = get_bitmask_order(SUSPEND_PD_PAGES(nr_copy_pages));

	p = pagedir = (suspend_pagedir_t *)__get_free_pages(GFP_ATOMIC, pagedir_order);
	if(!pagedir)
		return NULL;

	page = virt_to_page(pagedir);
	for(i=0; i < 1<<pagedir_order; i++)
		SetPageNosave(page++);
		
	while(nr_copy_pages--) {
		p->address = get_free_page(GFP_ATOMIC);
		if(!p->address) {
			panic("oom");
			free_suspend_pagedir((unsigned long) pagedir);
			return NULL;
		}
		SetPageNosave(virt_to_page(p->address));
		p->orig_address = 0;
		p++;
	}
	return pagedir;
}

static int prepare_suspend_console(void)
{
	orig_loglevel = console_loglevel;
	console_loglevel = new_loglevel;

#ifdef CONFIG_VT
	orig_fgconsole = fg_console;
#ifdef SUSPEND_CONSOLE
	if(vc_allocate(SUSPEND_CONSOLE))
	  /* we can't have a free VC for now. Too bad,
	   * we don't want to mess the screen for now. */
		return 1;

	set_console (SUSPEND_CONSOLE);
	if(vt_waitactive(SUSPEND_CONSOLE)) {
		PRINTS("Bummer. Can't switch VCs.");
		return 1;
	}
#endif
#endif
	return 0;
}

static void restore_console(void)
{
	console_loglevel = orig_loglevel;
#ifdef SUSPEND_CONSOLE
	set_console (orig_fgconsole);
#endif
	return;
}

static int prepare_suspend_processes(void)
{
	PRINTS( "Stopping processes\n" );
	MDELAY(1000);
	if (freeze_processes()) {
		PRINTS( "Not all processes stopped!\n" );
//		panic("Some processes survived?\n");
		thaw_processes();
		return 1;
	}
	do_suspend_sync();
	return 0;
}

/*
 *	Free as much memory as possible
 */

static void **eaten_memory;

static void eat_memory(void)
{
	int i = 0;
	void **c= eaten_memory, *m;

	printk("Eating pages ");
	while ((m = (void *) get_free_page(GFP_HIGHUSER))) {
		memset(m, 0, PAGE_SIZE);
		eaten_memory = m;
		if (!(i%100))
			printk( ".(%d)", i ); 
		*eaten_memory = c;
		c = eaten_memory;
		i++; 
#if 1
	/* 40000 == 160MB */
	/* 10000 for 64MB */
	/* 2500 for  16MB */
		if (i > 40000)
			break;
#endif
	}
	printk("(%dK)\n", i*4);
}

static void free_memory(void)
{
	int i = 0;
	void **c = eaten_memory, *f;
	
	printk( "Freeing pages " );
	while (c) {
		if (!(i%5000))
		printk( "." ); 
		f = *c;
		c = *c;
		if (f) { free_page( (long) f ); i++; }
	}
	printk( "(%dK)\n", i*4 );
	eaten_memory = NULL;
}

/*
 * Try to free as much memory as possible, but do not OOM-kill anyone
 *
 * Notice: all userland should be stopped at this point, or livelock is possible.
 */
static void free_some_memory(void)
{
#if 1
	PRINTS("Freeing memory: ");
	while (try_to_free_pages(&contig_page_data.node_zones[ZONE_HIGHMEM], GFP_KSWAPD, 0))
		printk(".");
	printk("\n");
#else
	printk("Using memeat\n");
	eat_memory();
	free_memory();
#endif
}

/* Make disk drivers accept operations, again */
static void drivers_unsuspend(void)
{
	device_resume(RESUME_ENABLE);
	device_resume(RESUME_RESTORE_STATE);
}

/* Called from process context */
static int drivers_suspend(void)
{
	device_suspend(4, SUSPEND_NOTIFY);
	device_suspend(4, SUSPEND_SAVE_STATE);
	device_suspend(4, SUSPEND_DISABLE);
	if(!pm_suspend_state) {
		if(pm_send_all(PM_SUSPEND,(void *)3)) {
			printk(KERN_WARNING "Problem while sending suspend event\n");
			return(1);
		}
		pm_suspend_state=1;
	} else
		printk(KERN_WARNING "PM suspend state already raised\n");
	  
	return(0);
}

#define RESUME_PHASE1 1 /* Called from interrupts disabled */
#define RESUME_PHASE2 2 /* Called with interrupts enabled */
#define RESUME_ALL_PHASES (RESUME_PHASE1 | RESUME_PHASE2)
static void drivers_resume(int flags)
{
	device_resume(RESUME_ENABLE);
	device_resume(RESUME_RESTORE_STATE);
  	if(flags & RESUME_PHASE2) {
		if(pm_suspend_state) {
			if(pm_send_all(PM_RESUME,(void *)0))
				printk(KERN_WARNING "Problem while sending resume event\n");
			pm_suspend_state=0;
		} else
			printk(KERN_WARNING "PM suspend state wasn't raised\n");

#ifdef SUSPEND_CONSOLE
		update_screen(fg_console);	/* Hmm, is this the problem? */
#endif
	}
}

static int suspend_save_image(void)
{
	struct sysinfo i;
	unsigned int nr_needed_pages = 0;

	pagedir_nosave = NULL;
	PRINTS( "/critical section: Counting pages to copy" );
	nr_copy_pages = count_and_copy_data_pages(NULL);
	nr_needed_pages = nr_copy_pages + PAGES_FOR_IO;
	
	PRINTK(" (pages needed: %d+%d=%d free: %d)\n",nr_copy_pages,PAGES_FOR_IO,nr_needed_pages,nr_free_pages());
	if(nr_free_pages() < nr_needed_pages) {
		printk(KERN_CRIT "%sCouldn't get enough free pages, on %d pages short\n",
		       name_suspend, nr_needed_pages-nr_free_pages());
		spin_unlock_irq(&suspend_pagedir_lock);
		return 1;
	}
	si_swapinfo(&i);	/* FIXME: si_swapinfo(&i) returns all swap devices information.
				   We should only consider resume_device. */
	if (i.freeswap < nr_needed_pages)  {
		printk(KERN_CRIT "%sThere's not enough swap space available, on %ld pages short\n",
		       name_suspend, nr_needed_pages-i.freeswap);
		spin_unlock_irq(&suspend_pagedir_lock);
		return 1;
	}

	PRINTK( "Alloc pagedir\n" ); 
	pagedir_save = pagedir_nosave = create_suspend_pagedir(nr_copy_pages);
	if(!pagedir_nosave) {
		/* Shouldn't happen */
		printk(KERN_CRIT "%sCouldn't allocate enough pages\n",name_suspend);
		panic("Really should not happen");
		spin_unlock_irq(&suspend_pagedir_lock);
		return 1;
	}
	nr_copy_pages_check = nr_copy_pages;
	pagedir_order_check = pagedir_order;

	if (nr_copy_pages != count_and_copy_data_pages(pagedir_nosave))	/* copy */
		panic("Count and copy returned another count than when counting?\n");

	/*
	 * End of critical section. From now on, we can write to memory,
	 * but we should not touch disk. This specially means we must _not_
	 * touch swap space! Except we must write out our image of course.
	 *
	 * Following line enforces not writing to disk until we choose.
	 */
	suspend_device = NODEV;					/* We do not want any writes, thanx */
	drivers_unsuspend();
	spin_unlock_irq(&suspend_pagedir_lock);
	PRINTS( "critical section/: done (%d pages copied)\n", nr_copy_pages );

	lock_swapdevices();
	write_suspend_image();
	lock_swapdevices();	/* This will unlock ignored swap devices since writing is finished */

	/* Image is saved, call sync & restart machine */
	PRINTS( "Syncing disks\n" );
	/* It is important _NOT_ to umount filesystems at this point. We want
	 * them synced (in case something goes wrong) but we DO not want to mark
	 * filesystem clean: it is not. (And it does not matter, if we resume
	 * correctly, we'll mark system clean, anyway.)
	 */
	return 0;
}

void suspend_power_down(void)
{
	C_A_D = 0;
	printk(KERN_EMERG "%sTrying to power down.\n", name_suspend);
#ifdef CONFIG_VT
	printk(KERN_EMERG "shift_state: %04x\n", shift_state);
	mdelay(1000);
	if (TEST_SWSUSP ^ (!!(shift_state & (1 << KG_CTRL))))
		machine_restart(NULL);
	else
#endif
		machine_power_off();

	printk(KERN_EMERG "%sProbably not capable for powerdown. System halted.\n", name_suspend);
	machine_halt();
	while (1);
	/* NOTREACHED */
}

/* forward decl */
void do_software_suspend(void);

/*
 * Magic happens here
 */

static void do_magic_resume_1(void)
{
	barrier();
	mb();
	spin_lock_irq(&suspend_pagedir_lock);	/* Done to disable interrupts */ 

	printk( "Waiting for DMAs to settle down...\n");
	mdelay(1000);	/* We do not want some readahead with DMA to corrupt our memory, right?
			   Do it with disabled interrupts for best effect. That way, if some
			   driver scheduled DMA, we have good chance for DMA to finish ;-). */
}

static void do_magic_resume_2(void)
{
	if (nr_copy_pages_check != nr_copy_pages)
		panic("nr_copy_pages changed?!");
	if (pagedir_order_check != pagedir_order)
		panic("pagedir_order changed?!");

	PRINTR( "Freeing prev allocated pagedir\n" );
	free_suspend_pagedir(pagedir_save);
	__flush_tlb_global();		/* Even mappings of "global" things (vmalloc) need to be fixed */
	drivers_resume(RESUME_ALL_PHASES);
	spin_unlock_irq(&suspend_pagedir_lock);

	PRINTK( "Fixing swap signatures... " );
	mark_swapfiles(((swp_entry_t) {0}), MARK_SWAP_RESUME);
	PRINTK( "ok\n" );

#ifdef SUSPEND_CONSOLE
	update_screen(fg_console);	/* Hmm, is this the problem? */
#endif
	suspend_tq.routine = (void *)do_software_suspend;
}

static void do_magic_suspend_1(void)
{
	mb();
	barrier();
	spin_lock_irq(&suspend_pagedir_lock);
}

static void do_magic_suspend_2(void)
{
	read_swapfiles();
	if (!suspend_save_image()) {
#if 1
		suspend_power_down ();	/* FIXME: if suspend_power_down is commented out, console is lost after few suspends ?! */
#endif
	}

	suspend_device = NODEV;
	printk(KERN_WARNING "%sSuspend failed, trying to recover...\n", name_suspend);
	MDELAY(1000); /* So user can wait and report us messages if armageddon comes :-) */

	barrier();
	mb();
	drivers_resume(RESUME_PHASE2);
	spin_lock_irq(&suspend_pagedir_lock);	/* Done to disable interrupts */ 
	mdelay(1000);

	free_pages((unsigned long) pagedir_nosave, pagedir_order);
	drivers_resume(RESUME_PHASE1);
	spin_unlock_irq(&suspend_pagedir_lock);
	mark_swapfiles(((swp_entry_t) {0}), MARK_SWAP_RESUME);
	suspend_tq.routine = (void *)do_software_suspend;
	printk(KERN_WARNING "%sLeaving do_magic_suspend_2...\n", name_suspend);	
}

#define SUSPEND_C
#include <asm/suspend.h>

/*
 * This function is triggered using process bdflush. We try to swap out as
 * much as we can then make a copy of the occupied pages in memory so we can
 * make a copy of kernel state atomically, the I/O needed by saving won't
 * bother us anymore.
 */
void do_software_suspend(void)
{
	arch_prepare_suspend();
	if (!prepare_suspend_console()) {
		if (!prepare_suspend_processes()) {
			free_some_memory();

			/* No need to invalidate any vfsmnt list -- they will be valid after resume, anyway.
			 *
			 * We sync here -- so you have consistent filesystem state when things go wrong.
			 * -- so that noone writes to disk after we do atomic copy of data.
			 */
			PRINTS( "Syncing disks before copy\n" );
			do_suspend_sync();
			drivers_suspend();
			if(drivers_suspend()==0)
				do_magic(0);			/* This function returns after machine woken up from resume */
			PRINTR("Restarting processes...\n");
			thaw_processes();
		}
	}
	software_suspend_enabled = 1;
	MDELAY(1000);
	restore_console ();
}

struct tq_struct suspend_tq =
	{ routine: (void *)(void *)do_software_suspend, 
	  data: 0 };

/*
 * This is the trigger function, we must queue ourself since we
 * can be called from interrupt && bdflush context is needed
 */
void software_suspend(void)
{
	if(!software_suspend_enabled)
		return;

	software_suspend_enabled = 0;
	queue_task(&suspend_tq, &tq_bdflush);
	wakeup_bdflush();
}

/* More restore stuff */

/* FIXME: Why not memcpy(to, from, 1<<pagedir_order*PAGE_SIZE)? */
static void copy_pagedir(suspend_pagedir_t *to, suspend_pagedir_t *from)
{
	int i;
	char *topointer=(char *)to, *frompointer=(char *)from;

	for(i=0; i < 1 << pagedir_order; i++) {
		copy_page(topointer, frompointer);
		topointer += PAGE_SIZE;
		frompointer += PAGE_SIZE;
	}
}

#define does_collide(addr)	\
		does_collide_order(pagedir_nosave, addr, 0)

/*
 * Returns true if given address/order collides with any orig_address 
 */
static int does_collide_order(suspend_pagedir_t *pagedir, unsigned long addr,
		int order)
{
	int i;
	unsigned long addre = addr + (PAGE_SIZE<<order);
	
	for(i=0; i < nr_copy_pages; i++)
		if((pagedir+i)->orig_address >= addr &&
			(pagedir+i)->orig_address < addre)
			return 1;

	return 0;
}

/*
 * We check here that pagedir & pages it points to won't collide with pages
 * where we're going to restore from the loaded pages later
 */

static int check_pagedir(void)
{
	int i;

	for(i=0; i < nr_copy_pages; i++) {
		unsigned long addr;

		do {
			addr = get_free_page(GFP_ATOMIC);
			if(!addr)
				return -ENOMEM;
		} while (does_collide(addr));

		(pagedir_nosave+i)->address = addr;
	}
	return 0;
}

static int relocate_pagedir(void)
{
	/* This is deep magic
	   We have to avoid recursion (not to overflow kernel stack), and that's why
	   code looks pretty cryptic
	*/
	suspend_pagedir_t *new_pagedir, *old_pagedir = pagedir_nosave;
	void **eaten_memory = NULL;
	void **c = eaten_memory, *m, *f;


	if(!does_collide_order(old_pagedir, (unsigned long)old_pagedir, pagedir_order)) {
		printk("not neccessary\n");
		return 0;
	}

	while ((m = (void *) __get_free_pages(GFP_ATOMIC, pagedir_order))) {
		memset(m, 0, PAGE_SIZE);
		if (!does_collide_order(old_pagedir, (unsigned long)m, pagedir_order))
			break;
		eaten_memory = m;
		printk( "." ); 
		*eaten_memory = c;
		c = eaten_memory;
	}

	if (!m)
		return -ENOMEM;

	pagedir_nosave = new_pagedir = m;
	copy_pagedir(new_pagedir, old_pagedir);

	c = eaten_memory;
	while(c) {
		printk(":");
		f = *c;
		c = *c;
		if (f)
			free_pages((unsigned long)f, pagedir_order);
	}
	printk("okay\n");
	return 0;
}

/*
 * Sanity check if this image makes sense with this kernel/swap context
 * I really don't think that it's foolproof but more than nothing..
 */

static int sanity_check_failed(char *reason)
{
	printk(KERN_ERR "%s%s\n",name_resume,reason);
	return -EPERM;
}

static int sanity_check(struct suspend_header *sh)
{
	if(sh->version_code != LINUX_VERSION_CODE)
		return sanity_check_failed("Incorrect kernel version");
	if(sh->num_physpages != num_physpages)
		return sanity_check_failed("Incorrect memory size");
	if(strncmp(sh->machine, system_utsname.machine, 8))
		return sanity_check_failed("Incorrect machine type");
	if(strncmp(sh->version, system_utsname.version, 20))
		return sanity_check_failed("Incorrect version");
	if(sh->num_cpus != smp_num_cpus)
		return sanity_check_failed("Incorrect number of cpus");
	if(sh->page_size != PAGE_SIZE)
		return sanity_check_failed("Incorrect PAGE_SIZE");
	return 0;
}

static int bdev_read_page(kdev_t dev, long pos, void *buf)
{
	struct buffer_head *bh;
	struct block_device *bdev;

	if (pos%PAGE_SIZE) panic("Sorry, dave, I can't let you do that!\n");
	bdev = bdget(kdev_t_to_nr(dev));
	blkdev_get(bdev, FMODE_READ, O_RDONLY, BDEV_RAW);
	if (!bdev) {
		printk("No block device for %s\n", __bdevname(dev));
		BUG();
	}
	set_blocksize(bdev, PAGE_SIZE);
	bh = __bread(bdev, pos/PAGE_SIZE, PAGE_SIZE);
	if (!bh || (!bh->b_data)) {
		return -1;
	}
	memcpy(buf, bh->b_data, PAGE_SIZE);	/* FIXME: may need kmap() */
	BUG_ON(!buffer_uptodate(bh));
	brelse(bh);
	blkdev_put(bdev, BDEV_RAW);
	return 0;
} 

extern kdev_t __init name_to_kdev_t(char *line);

static int resume_try_to_read(const char * specialfile, int noresume)
{
	union diskpage *cur;
	swp_entry_t next;
	int i, nr_pgdir_pages, error;
	int blksize = 0;

	resume_device = name_to_kdev_t(specialfile);
	cur = (void *) get_free_page(GFP_ATOMIC);
	if (!cur) {
		printk( "%sNot enough memory?\n", name_resume );
		error = -ENOMEM;
		goto resume_read_error;
	}

	printk("Resuming from device %x\n", resume_device);

#define READTO(pos, ptr) \
	if (bdev_read_page(resume_device, pos, ptr)) { error = -EIO; goto resume_read_error; }
#define PREPARENEXT \
	{	next = cur->link.next; \
		next.val = SWP_OFFSET(next) * PAGE_SIZE; \
        }

	error = -EIO;
	READTO(0, cur);

	if ((!memcmp("SWAP-SPACE",cur->swh.magic.magic,10)) ||
	    (!memcmp("SWAPSPACE2",cur->swh.magic.magic,10))) {
		printk(KERN_ERR "%sThis is normal swap space\n", name_resume );
		error = -EINVAL;
		goto resume_read_error;
	}

	PREPARENEXT; /* We have to read next position before we overwrite it */

	if (!memcmp("SUSP1R",cur->swh.magic.magic,6))
		memcpy(cur->swh.magic.magic,"SWAP-SPACE",10);
	else if (!memcmp("SUSP2R",cur->swh.magic.magic,6))
		memcpy(cur->swh.magic.magic,"SWAPSPACE2",10);
	else {
		panic("%sUnable to find suspended-data signature (%.10s - misspelled?\n", 
			name_resume, cur->swh.magic.magic);
	}
	printk( "%sSignature found, resuming\n", name_resume );
	MDELAY(1000);

	READTO(next.val, cur);

	error = -EPERM;
	if (sanity_check(&cur->sh))
		goto resume_read_error;

	/* Probably this is the same machine */	

	PREPARENEXT;

	pagedir_save = cur->sh.suspend_pagedir;
	nr_copy_pages = cur->sh.num_pbes;
	nr_pgdir_pages = SUSPEND_PD_PAGES(nr_copy_pages);
	pagedir_order = get_bitmask_order(nr_pgdir_pages);

	error = -ENOMEM;
	pagedir_nosave = (suspend_pagedir_t *)__get_free_pages(GFP_ATOMIC, pagedir_order);
	if(!pagedir_nosave)
		goto resume_read_error;

	PRINTR( "%sReading pagedir, ", name_resume );

	/* We get pages in reverse order of saving! */
	error=-EIO;
	for (i=nr_pgdir_pages-1; i>=0; i--) {
		if (!next.val)
			panic( "Preliminary end of suspended data?" );
		cur = (union diskpage *)((char *) pagedir_nosave)+i;
		READTO(next.val, cur);
		PREPARENEXT;
	}
	if (next.val)
		panic( "Suspended data too long?" );

	printk("Relocating pagedir");
	if((error=relocate_pagedir())!=0)
		goto resume_read_error;
	if((error=check_pagedir())!=0)
		goto resume_read_error;

	PRINTK( "image data (%d pages): ", nr_copy_pages );
	error = -EIO;
	for(i=0; i < nr_copy_pages; i++) {
		swp_entry_t swap_address = (pagedir_nosave+i)->swap_address;
		if (!(i%100))
			PRINTK( "." );
		next.val = SWP_OFFSET (swap_address) * PAGE_SIZE;
		/* You do not need to check for overlaps...
		   ... check_pagedir already did this work */
		READTO(next.val, (char *)((pagedir_nosave+i)->address));
	}
	PRINTK( " done\n" );
	error = 0;

resume_read_error:
	switch (error) {
		case 0:
			PRINTR("Reading resume file was successful\n");
			break;
		case -EINVAL:
			break;
		case -EIO:
			printk( "%sI/O error\n", name_resume);
			panic("Wanted to resume but it did not work\n");
			break;
		case -ENOENT:
			printk( "%s%s: No such file or directory\n", name_resume, specialfile);
			panic("Wanted to resume but it did not work\n");
			break;
		default:
			printk( "%sError %d resuming\n", name_resume, error );
			panic("Wanted to resume but it did not work\n");
	}
	MDELAY(1000);
	return error;
}

/*
 * Called from init kernel_thread.
 * We check if we have an image and if so we try to resume
 */

void software_resume(void)
{
#ifdef CONFIG_SMP
	printk(KERN_WARNING "Software Suspend has a malfunctioning SMP support. Disabled :(\n");
#else
	/* We enable the possibility of machine suspend */
	software_suspend_enabled = 1;
#endif
	if(!resume_status)
		return;

	printk( "%s", name_resume );
	if(resume_status == NORESUME) {
		/* FIXME: Signature should be restored here */
		printk( "disabled\n" );
		return;
	}
	MDELAY(1000);

	orig_loglevel = console_loglevel;
	console_loglevel = new_loglevel;

	if(!resume_file[0] && resume_status == RESUME_SPECIFIED) {
		printk( "nowhere to resume from\n" );
		return;
	}

	printk( "resuming from %s\n", resume_file);
	if(resume_try_to_read(resume_file, 0))
		goto read_failure;
	do_magic(1);
	panic("This never returns");

read_failure:
	console_loglevel = orig_loglevel;
	return;
}

static int __init resume_setup(char *str)
{
	if(resume_status)
		return 1;

	strncpy( resume_file, str, 255 );
	resume_status = RESUME_SPECIFIED;

	return 1;
}

static int __init software_noresume(char *str)
{
	if(!resume_status)
		printk(KERN_WARNING "noresume option lacks a resume= option\n");
	resume_status = NORESUME;
	
	return 1;
}

__setup("noresume", software_noresume);
__setup("resume=", resume_setup);

EXPORT_SYMBOL(software_suspend);
EXPORT_SYMBOL(software_suspend_enabled);
EXPORT_SYMBOL(refrigerator);
