/* fault.c:  Page fault handlers for the Sparc.
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

asmlinkage void sparc_txtmem_error(int type, unsigned long sync_err_reg,
				   unsigned long sync_vaddr,
				   unsigned long async_err_reg,
				   unsigned long async_vaddr)
{
  printk("Aieee, sparc text page access error, halting\n");
  printk("type = %d  sync_err_reg = 0x%x  sync_vaddr = 0x%x\n",
	 type, (unsigned int) sync_err_reg, (unsigned int) sync_vaddr);
  printk("async_err_reg = 0x%x  async_vaddr = 0x%x\n",
	 (unsigned int) async_err_reg, (unsigned int) async_vaddr);
  halt();
}

/* #define DEBUG_SPARC_TEXT_ACCESS_FAULT */

asmlinkage void sparc_text_access_fault(int type, unsigned long sync_err_reg,
					unsigned long sync_vaddr,
					unsigned long pc, unsigned long psr,
					struct pt_regs *regs)
{
  struct vm_area_struct *vma;
  unsigned long address;

  address = sync_vaddr;
#ifdef DEBUG_SPARC_TEXT_ACCESS_FAULT
  printk("Text FAULT: address = %08lx  code = %08lx\n",
	 (unsigned long) address, (unsigned long) sync_err_reg);
  printk("PC = %08lx\n", (unsigned long) regs->pc);
  SP_ENTER_DEBUGGER;
  halt();
#endif
  vma = find_vma(current, address);
  if(!vma) {
    goto bad_area;
  }
  if(vma->vm_start <= address)
    goto good_area;
  goto bad_area;

/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
  handle_mm_fault(vma, address, 0);
  return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
  if((unsigned long) address < PAGE_SIZE) {
    printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
  } else
    printk(KERN_ALERT "Unable to handle kernel paging request");
  printk(" at virtual address %08lx\n",address);
  printk(KERN_ALERT "current->tss.pgd_ptr = %08lx\n",
	 (unsigned long) current->tss.pgd_ptr);
  halt();
}

asmlinkage void sparc_datamem_error(int type, unsigned long sync_err_reg,
				    unsigned long sync_vaddr,
				    unsigned long async_err_reg,
				    unsigned long async_vaddr)
{
  printk("Aieee, sparc data page access error, halting\n");
  printk("type = %d  sync_err_reg = 0x%x  sync_vaddr = 0x%x\n",
	 type, (unsigned int) sync_err_reg, (unsigned int) sync_vaddr);
  printk("async_err_reg = 0x%x  async_vaddr = 0x%x\n",
	 (unsigned int) async_err_reg, (unsigned int) async_vaddr);
  printk("SYNC PAGE has MMU entry %08lx\n",
	 (unsigned long) get_pte(sync_vaddr));
  halt();
}

/* #define DEBUG_SPARC_DATA_ACCESS_FAULT */

asmlinkage void sparc_data_access_fault(int type, unsigned long sync_err_reg,
					unsigned long sync_vaddr,
					unsigned long pc, unsigned long psr,
					struct pt_regs *regs)
{
  struct vm_area_struct *vma;
  unsigned long address;
  int error_code;

  address = sync_vaddr;
#ifdef DEBUG_SPARC_DATA_ACCESS_FAULT 
  printk("Data FAULT: address = %08lx  code = %08lx\n",
	 (unsigned long) address, (unsigned long) sync_err_reg);
  printk("PC = %08lx\n", (unsigned long) regs->pc);
  printk("PTE = %08lx\n", (unsigned long) get_pte(address));
#endif
  vma = find_vma(current, address);
  if(!vma) {
#ifdef DEBUG_SPARC_DATA_ACCESS_FAULT 
    printk("NULL VMA\n");
#endif
    goto bad_area;
  }
  if(vma->vm_start <= address)
    goto good_area;

  if(!(vma->vm_flags & VM_GROWSDOWN)) {
    goto bad_area;
  }
  if(vma->vm_end - address > current->rlim[RLIMIT_STACK].rlim_cur) {
    goto bad_area;
  }

  vma->vm_offset -= vma->vm_start - (address & PAGE_MASK);
  vma->vm_start = (address & PAGE_MASK);

/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
#ifdef DEBUG_SPARC_DATA_ACCESS_FAULT 
  printk("Found good_area\n");
#endif
  if((sync_err_reg & SUN4C_SYNC_BADWRITE) &&
     (sync_err_reg & SUN4C_SYNC_NPRESENT)) {
    if(!(vma->vm_flags & VM_WRITE)) {
#ifdef DEBUG_SPARC_DATA_ACCESS_FAULT 
      printk("oops, vma not writable\n");
#endif
      goto bad_area;
    }
  } else {
    if(sync_err_reg & SUN4C_SYNC_PROT) {
#ifdef DEBUG_SPARC_DATA_ACCESS_FAULT 
      printk("PROT violation\n");
#endif
      goto bad_area;
    }
    if(!(vma->vm_flags & (VM_READ | VM_EXEC))) {
#ifdef DEBUG_SPARC_DATA_ACCESS_FAULT 
      printk("vma not readable nor executable\n");
#endif
      goto bad_area;
    }
  }

  if(sync_err_reg & SUN4C_SYNC_BADWRITE)
    error_code = 0x2;
  else
    error_code = 0x0;

#ifdef DEBUG_SPARC_DATA_ACCESS_FAULT 
  printk("calling handle_mm_fault vma=%08lx addr=%08lx code=%d\n",
	 (unsigned long) vma, (unsigned long) address,
	 (int) error_code);
#endif
  handle_mm_fault(vma, address, error_code);
  return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
  if (wp_works_ok < 0 && address == 0x0) {
	  wp_works_ok = 1;
	  pg0[0] = pte_val(mk_pte(PAGE_OFFSET, PAGE_SHARED));
	  put_pte((unsigned long) 0x0, pg0[0]);
	  printk("This Sparc honours the WP bit even when in supervisor mode. Good.\n");
	  return;
  }

  if((unsigned long) address < PAGE_SIZE) {
    printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
  } else
    printk(KERN_ALERT "Unable to handle kernel paging request");
  printk(" at virtual address %08lx\n",address);
  printk(KERN_ALERT "current->tss.pgd_ptr = %08lx\n",
	 (unsigned long) current->tss.pgd_ptr);
  halt();
}

/* Dump the contents of the SRMMU fsr in a human readable format. */
void
dump_srmmu_fsr(unsigned long fsr)
{
	unsigned long ebe, l, at, ft, fav, ow;

	ebe = (fsr&SRMMU_FSR_EBE_MASK)>>10;
	l = (fsr&SRMMU_FSR_L_MASK)>>8;
	at = (fsr&SRMMU_FSR_AT_MASK)>>5;
	ft = (fsr&SRMMU_FSR_FT_MASK)>>2;
	fav = (fsr&SRMMU_FSR_FAV_MASK)>>1;
	ow = (fsr&SRMMU_FSR_OW_MASK);

	printk("FSR %08lx: ", fsr);

	/* Ugh, the ebe is arch-dep, have to find out the meanings. */
	printk("EBE<%s> ", (ebe == 0 ? "none" : (ebe == 1 ? "bus err" :
						 (ebe == 2 ? "bus timeout" :
						  (ebe == 4 ? "uncorrectable err" :
						   (ebe == 8 ? "undefined err" :
						    (ebe == 16 ? "parity err" :
						     (ebe == 24 ? "tsunami parity err" :
						      (ebe == 32 ? "store buf err" :
						       (ebe == 64 ? "control space err" :
							"VIKING emergency response team"))))))))));
	printk("L<%s> ", (l == 0 ? "context table" : (l == 1 ? "level1" :
						      (l == 2 ? "level2" :
						       "level3"))));
	printk("AT<%s> ", (at == 0 ? "user load" :
			   (at == 1 ? "priv load" :
			    (at == 2 ? "user rd/exec" :
			     (at == 3 ? "priv rd/exec" :
			      (at == 4 ? "user store data" :
			       (at == 5 ? "priv store data" :
				(at == 6 ? "user store inst" :
				 "priv store inst"))))))));

	printk("FT<%s> ", (ft == 0 ? "none" :
			   (ft == 1 ? "invalid address" :
			    (ft == 2 ? "prot violation" :
			     (ft == 3 ? "priv violation" :
			      (ft == 4 ? "translation error" :
			       (ft == 5 ? "bus acc error" :
				(ft == 6 ? "internal error" :
				 "reserved"))))))));

	printk("FAV<%s> ", (fav == 0 ? "faddr invalid" : "faddr valid"));
	printk("OW<%s>\n", (ow == 0 ? "fsr not overwritten" : "fsr overwritten"));

	return;
}

/* #define DEBUG_SRMMU_TEXT_ACCESS_FAULT */

void
really_bad_srmmu_fault(int type, unsigned long fstatus, unsigned long faddr)
{
	/* Learn how to handle these later... */
	printk("REALLY BAD SRMMU FAULT DETECTED\n");
	printk("Bailing out...\n");
	dump_srmmu_fsr(fstatus);
	prom_halt();
	return;
}

asmlinkage void srmmu_text_access_fault(int type, unsigned long fstatus,
					unsigned long faddr,
					unsigned long pc, unsigned long psr,
					struct pt_regs *regs)
{
  struct vm_area_struct *vma;
  unsigned long address;

  /* Check for external bus errors and uncorrectable errors */
  if(fstatus&SRMMU_FSR_EBE_MASK)
	  printk("External Bus Error detected during a text fault.\n");

  /* Check for multiple faults... */
  if(fstatus&SRMMU_FSR_OW_MASK && (fstatus&SRMMU_FSR_FT_TRANS)) {
	  printk("Multiple faults detected in text fault handler\n");
	  printk("Fault number one: Text fault at addr %08lx", faddr);
	  printk("Fault number two: Translation Error\n");
	  printk("Giving up...\n");
	  prom_halt();
  }
	  
  if(fstatus&(SRMMU_FSR_EBE_BERR | SRMMU_FSR_EBE_BTIMEO | SRMMU_FSR_EBE_UNCOR))
	  really_bad_srmmu_fault(type, fstatus, faddr);

  /* Ok, we should be able to handle it at this point. */

  address = faddr;
#ifdef DEBUG_SRMMU_TEXT_ACCESS_FAULT
  printk("Text FAULT: address = %08lx  code = %08lx\n",
	 (unsigned long) address, (unsigned long) fstatus);
  printk("PC = %08lx\n", (unsigned long) regs->pc);
  dump_srmmu_fsr(fstatus);
  halt();
#endif

  /* Ugh, how often does this happen? ;-( */
  if(!(fstatus&SRMMU_FSR_FAV_MASK)) {
	  printk("Fault address register is INVALID!  Since this is a text\n");
	  printk("fault I'll use the value of the trapped PC instead...\n");
	  address = regs->pc;
  }

  /* Ugh, I don't wanna know... */

  vma = find_vma(current, address);
  if(!vma) {
    goto bad_area;
  }
  if(vma->vm_start <= address)
    goto good_area;
  goto bad_area;

/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
  do_no_page(vma, address, 0);
  return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
  if((unsigned long) address < PAGE_SIZE) {
    printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
  } else
    printk(KERN_ALERT "Unable to handle kernel paging request");
  printk(" at virtual address %08lx\n",address);
  printk(KERN_ALERT "current->tss.pgd_ptr = %08lx\n",
	 (unsigned long) current->tss.pgd_ptr);
  halt();
}

/* #define DEBUG_SRMMU_DATA_ACCESS_FAULT */

asmlinkage void srmmu_data_access_fault(int type, unsigned long fstatus,
					unsigned long faddr,
					unsigned long afstatus,
					unsigned long afaddr,
					struct pt_regs *regs)
{
  struct vm_area_struct *vma;
  unsigned long address, pc, psr;
  int error_code;

  pc = regs->pc;
  psr= regs->psr;
  address = faddr;
#ifdef DEBUG_SRMMU_DATA_ACCESS_FAULT 
  printk("Data FAULT: address = %08lx  code = %08lx\n",
	 (unsigned long) address, (unsigned long) fstatus);
  printk("PC = %08lx\n", (unsigned long) pc);
  printk("afsr = %08lx afaddr = %08lx\n", afstatus, afaddr);
  dump_srmmu_fsr(fstatus);
#endif

  /* I figure if I make the panic's look like they came from SunOS or Solaris
   * my life will be a lot easier ;-)
   */
  if(!(fstatus&SRMMU_FSR_FAV_MASK)) {
	  dump_srmmu_fsr(fstatus);
	  panic("hat_pteload: Fault address is invalid on a data fault, I'm confused...\n");
  }

#if 0 /* I see this all the time on supersparcs ;-( */
  if(fstatus&SRMMU_FSR_OW_MASK) {
	  dump_srmmu_fsr(fstatus);
	  panic("hat_pteload: Multiple faults at once, giving up...\n");
  }
#endif

  vma = find_vma(current, address);
  if(!vma) {
#ifdef DEBUG_SRMMU_DATA_ACCESS_FAULT 
    printk("NULL VMA\n");
#endif
    goto bad_area;
  }
  if(vma->vm_start <= address)
    goto good_area;

  if(!(vma->vm_flags & VM_GROWSDOWN)) {
    goto bad_area;
  }
  if(vma->vm_end - address > current->rlim[RLIMIT_STACK].rlim_cur) {
    goto bad_area;
  }

  vma->vm_offset -= vma->vm_start - (address & PAGE_MASK);
  vma->vm_start = (address & PAGE_MASK);

/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
#ifdef DEBUG_SRMMU_DATA_ACCESS_FAULT 
  printk("Found good_area\n");
#endif
  if((fstatus & SUN4C_SYNC_BADWRITE) &&
     (fstatus & SUN4C_SYNC_NPRESENT)) {
    if(!(vma->vm_flags & VM_WRITE)) {
#ifdef DEBUG_SRMMU_DATA_ACCESS_FAULT 
      printk("oops, vma not writable\n");
#endif
      goto bad_area;
    }
  } else {
    if(fstatus & SUN4C_SYNC_PROT) {
#ifdef DEBUG_SRMMU_DATA_ACCESS_FAULT 
      printk("PROT violation\n");
#endif
      goto bad_area;
    }
    if(!(vma->vm_flags & (VM_READ | VM_EXEC))) {
#ifdef DEBUG_SRMMU_DATA_ACCESS_FAULT 
      printk("vma not readable nor executable\n");
#endif
      goto bad_area;
    }
  }

  if(fstatus & SUN4C_SYNC_BADWRITE)
    error_code = 0x2;
  else
    error_code = 0x0;

#ifdef DEBUG_SRMMU_DATA_ACCESS_FAULT 
  printk("calling do_no_page vma=%08lx addr=%08lx code=%d\n",
	 (unsigned long) vma, (unsigned long) address,
	 (int) error_code);
#endif
  do_no_page(vma, address, error_code);
  return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
  if (wp_works_ok < 0 && address == 0x0) {
	  wp_works_ok = 1;
	  /* Advance the PC and NPC over the test store. */
	  regs->pc = regs->npc;
	  regs->npc += 4;
	  printk("This Sparc honours the WP bit even when in supervisor mode. Good.\n");
	  return;
  }

  if((unsigned long) address < PAGE_SIZE) {
    printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
  } else
    printk(KERN_ALERT "Unable to handle kernel paging request");
  printk(" at virtual address %08lx\n",address);
  printk(KERN_ALERT "current->tss.pgd_ptr = %08lx\n",
	 (unsigned long) current->tss.pgd_ptr);
  halt();
}
