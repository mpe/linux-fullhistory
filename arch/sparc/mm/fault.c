/* $Id: fault.c,v 1.42 1995/11/25 00:59:20 davem Exp $
 * fault.c:  Page fault handlers for the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/signal.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/openprom.h>
#include <asm/idprom.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/memreg.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/kdebug.h>

#define ELEMENTS(arr) (sizeof (arr)/sizeof (arr[0]))

extern struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];
extern int prom_node_root;

extern void die_if_kernel(char *,struct pt_regs *,long);

struct linux_romvec *romvec;

/* foo */

int tbase_needs_unmapping;

/* At boot time we determine these two values necessary for setting
 * up the segment maps and page table entries (pte's).
 */

int num_segmaps, num_contexts;
int invalid_segment;

/* various Virtual Address Cache parameters we find at boot time... */

int vac_size, vac_linesize, vac_do_hw_vac_flushes;
int vac_entries_per_context, vac_entries_per_segment;
int vac_entries_per_page;

/*
 * Define this if things work differently on a i386 and a i486:
 * it will (on a i486) warn about kernel memory accesses that are
 * done without a 'verify_area(VERIFY_WRITE,..)'
 */
#undef CONFIG_TEST_VERIFY_AREA

/* Nice, simple, prom library does all the sweating for us. ;) */
int prom_probe_memory (void)
{
	register struct linux_mlist_v0 *mlist;
	register unsigned long bytes, base_paddr, tally;
	register int i;

	i = 0;
	mlist= *prom_meminfo()->v0_available;
	bytes = tally = mlist->num_bytes;
	base_paddr = (unsigned long) mlist->start_adr;
  
	sp_banks[0].base_addr = base_paddr;
	sp_banks[0].num_bytes = bytes;

	while (mlist->theres_more != (void *) 0){
		i++;
		mlist = mlist->theres_more;
		bytes = mlist->num_bytes;
		tally += bytes;
		if (i >= SPARC_PHYS_BANKS-1) {
			printk ("The machine has more banks that this kernel can support\n"
				"Increase the SPARC_PHYS_BANKS setting (currently %d)\n",
				SPARC_PHYS_BANKS);
			i = SPARC_PHYS_BANKS-1;
			break;
		}
    
		sp_banks[i].base_addr = (unsigned long) mlist->start_adr;
		sp_banks[i].num_bytes = mlist->num_bytes;
	}

	i++;
	sp_banks[i].base_addr = 0xdeadbeef;
	sp_banks[i].num_bytes = 0;

	return tally;
}

/* Traverse the memory lists in the prom to see how much physical we
 * have.
 */
unsigned long
probe_memory(void)
{
	int total;

	total = prom_probe_memory();

	/* Oh man, much nicer, keep the dirt in promlib. */
	return total;
}

/* Whee, a level 15 NMI interrupt memory error.  Let's have fun... */
asmlinkage void sparc_lvl15_nmi(struct pt_regs *regs, unsigned long serr,
				unsigned long svaddr, unsigned long aerr,
				unsigned long avaddr)
{
	printk("FAULT: NMI received\n");
	printk("SREGS: Synchronous Error %08lx\n", serr);
	printk("       Synchronous Vaddr %08lx\n", svaddr);
	printk("      Asynchronous Error %08lx\n", aerr);
	printk("      Asynchronous Vaddr %08lx\n", avaddr);
	printk("REGISTER DUMP:\n");
	show_regs(regs);
	prom_halt();
}

/* Whee, looks like an i386 to me ;-) */
asmlinkage void do_sparc_fault(struct pt_regs *regs, unsigned long tbr)
{
	struct vm_area_struct *vma;
	unsigned long address, error_code, trap_type;
	unsigned long from_user;

	from_user = (((regs->psr & PSR_PS) >> 4) ^ FAULT_CODE_USER);
	if(get_fault_info(&address, &error_code, from_user))
		goto bad_area;
	trap_type = ((tbr>>4)&0xff);
	if(trap_type == SP_TRAP_TFLT) {
		/* We play it 'safe'... */
		address = regs->pc;
		error_code = (from_user); /* no page, read */
	} else if(trap_type != SP_TRAP_DFLT)
		panic("Bad sparc trap, trap_type not data or text fault...");

	/* Now actually handle the fault.  Do kernel faults special,
	 * because on the sun4c we could have faulted trying to read
	 * the vma area of the task and without the following code
	 * we'd fault recursively until all our stack is gone. ;-(
	 *
	 * XXX I think there are races with this maneuver. XXX
	 */
	if(!from_user && address >= KERNBASE) {
		update_mmu_cache(0, address, __pte(0));
		return;
	}

	vma = find_vma(current, address);
	if(!vma)
		goto bad_area;
	if(vma->vm_start <= address)
		goto good_area;
	if(!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if(expand_stack(vma, address))
		goto bad_area;
	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
good_area:
	if(error_code & FAULT_CODE_WRITE) {
		if(!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		/* Allow reads even for write-only mappings */
		if(!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}
	handle_mm_fault(vma, address, error_code & FAULT_CODE_WRITE);
	return;
	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
bad_area:
	if(error_code & FAULT_CODE_USER) {
		current->tss.sig_address = address;
		current->tss.sig_desc = SUBSIG_NOMAPPING;
		send_sig(SIGSEGV, current, 1);
		return;
	}
	/* Uh oh, a kernel fault.  Check for bootup wp_test... */
	if (wp_works_ok < 0 && address == 0x0) {
		wp_works_ok = 1;
		printk("This Sparc honours the WP bit even when in supervisor mode. "
		       "Good.\n");
		/* Advance program counter over the store. */
		regs->pc = regs->npc;
		regs->npc += 4;
		return;
	}
	if((unsigned long) address < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	} else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at virtual address %08lx\n",address);
	printk("At PC %08lx nPC %08lx\n", (unsigned long) regs->pc,
	       (unsigned long) regs->npc);
	printk(KERN_ALERT "current->tss.pgd_ptr = %08lx\n",
	       (unsigned long) current->tss.pgd_ptr);
	show_regs(regs);
	panic("KERNAL FAULT");
}

/* When the user does not have a mapped stack and we either
 * need to read the users register window from that stack or
 * we need to save a window to that stack, control flow
 * ends up here to investigate the situation.  This is a
 * very odd situation where a 'user fault' happens from
 * kernel space.
 */

/* #define DEBUG_WINFAULT */
extern void show_regwindow(struct reg_window *);
asmlinkage void do_sparc_winfault(struct pt_regs *regs, int push)
{
	int wincount = 0;
	int signal = 0;
	struct thread_struct *tsp = &current->tss;

	flush_user_windows();
#ifdef DEBUG_WINFAULT
	{
		int i;
		printk("%s[%d]wfault<%d>: WINDOW DUMP --> ", current->comm,
		       current->pid, push);
		if(push==1)
			for(i = 0; i < tsp->w_saved; i++)
				printk("w[%d]sp<%08lx>, ",
				       i, tsp->rwbuf_stkptrs[i]);
		else
			printk("w[0]sp<%08lx>", regs->u_regs[UREG_FP]);
		if(push!=2)
			printk("\n");
	}
#endif
	if(push==1) {
		/* We failed to push a window to users stack. */
		while(wincount < tsp->w_saved) {
			if ((tsp->rwbuf_stkptrs[wincount] & 7) ||
			    (tsp->rwbuf_stkptrs[wincount] > KERNBASE) ||
			    verify_area(VERIFY_WRITE,
					(char *) tsp->rwbuf_stkptrs[wincount],
					sizeof(struct reg_window))) {
				signal = SIGILL;
				break;
			}			
			/* Do it! */
			memcpy((char *) tsp->rwbuf_stkptrs[wincount],
			       (char *)&tsp->reg_window[wincount],
			       sizeof(struct reg_window));
			wincount++;
		}
	} else {
		/* We failed to pull a window from users stack.
		 * For a window underflow from userland we need
		 * to verify two stacks, for a return from trap
		 * we need only inspect the one at UREG_FP.
		 */
		if((regs->u_regs[UREG_FP] & 7) ||
		   (regs->u_regs[UREG_FP] > KERNBASE) ||
		   verify_area(VERIFY_READ,
			       (char *) regs->u_regs[UREG_FP],
			       sizeof(struct reg_window)))
			signal = SIGILL;
		else
			memcpy((char *)&tsp->reg_window[0],
			       (char *) regs->u_regs[UREG_FP],
			       sizeof(struct reg_window));
		if(push==2 && !signal) {
			unsigned long sp = tsp->reg_window[0].ins[6];
#ifdef DEBUG_WINFAULT
			printk(", w[1]sp<%08lx>\n", sp);
			show_regwindow(&tsp->reg_window[0]);
#endif
			if((sp & 7) || (sp > KERNBASE) ||
			   verify_area(VERIFY_READ, (char *) sp,
				       sizeof(struct reg_window)))
				signal = SIGILL;
			else
				memcpy((char *)&tsp->reg_window[1],
				       (char *) sp, sizeof(struct reg_window));
		}
	}
	if(signal) {
		printk("%s[%d]: User has trashed stack pointer pc<%08lx>sp<%08lx>\n",
		       current->comm, current->pid, regs->pc, regs->u_regs[UREG_FP]);
		tsp->sig_address = regs->pc;
		tsp->sig_desc = SUBSIG_STACK;
		send_sig(signal, current, 1);
	} else
		tsp->w_saved = 0;
}
