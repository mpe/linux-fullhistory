/*
 *  linux/arch/m68k/kernel/traps.c
 *
 *  Copyright (C) 1993, 1994 by Hamish Macdonald
 *
 *  68040 fixes by Michael Rausch
 *  68040 fixes by Martin Apel
 *  68060 fixes by Roman Hodek
 *  68060 fixes by Jesper Skov
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

/*
 * Sets up all exception vectors
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/a.out.h>
#include <linux/user.h>
#include <linux/string.h>
#include <linux/linkage.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/traps.h>
#include <asm/bootinfo.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>

/* assembler routines */
asmlinkage void system_call(void);
asmlinkage void buserr(void);
asmlinkage void trap(void);
asmlinkage void inthandler(void);
asmlinkage void nmihandler(void);

e_vector vectors[256] = {
	0, 0, buserr, trap, trap, trap, trap, trap,
	trap, trap, trap, trap, trap, trap, trap, trap,
	trap, trap, trap, trap, trap, trap, trap, trap,
	inthandler, inthandler, inthandler, inthandler,
	inthandler, inthandler, inthandler, inthandler,
	/* TRAP #0-15 */
	system_call, trap, trap, trap, trap, trap, trap, trap,
	trap, trap, trap, trap, trap, trap, trap, trap,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

/* nmi handler for the Amiga */
asm(".text\n"
    __ALIGN_STR "\n"
    SYMBOL_NAME_STR(nmihandler) ": rte");

void trap_init (void)
{
	int i;

	/* setup the exception vector table */
	__asm__ volatile ("movec %0,%/vbr" : : "r" ((void*)vectors));

	for (i = 48; i < 64; i++)
		vectors[i] = trap;

	for (i = 64; i < 256; i++)
		vectors[i] = inthandler;

        /* if running on an amiga, make the NMI interrupt do nothing */
        if (MACH_IS_AMIGA) {
                vectors[VEC_INT7] = nmihandler;
        }

#ifdef CONFIG_FPSP_040
	if (m68k_is040or060 == 4) {
		/* set up FPSP entry points */
		asmlinkage void dz_vec(void) asm ("dz");
		asmlinkage void inex_vec(void) asm ("inex");
		asmlinkage void ovfl_vec(void) asm ("ovfl");
		asmlinkage void unfl_vec(void) asm ("unfl");
		asmlinkage void snan_vec(void) asm ("snan");
		asmlinkage void operr_vec(void) asm ("operr");
		asmlinkage void bsun_vec(void) asm ("bsun");
		asmlinkage void fline_vec(void) asm ("fline");
		asmlinkage void unsupp_vec(void) asm ("unsupp");

		vectors[VEC_FPDIVZ] = dz_vec;
		vectors[VEC_FPIR] = inex_vec;
		vectors[VEC_FPOVER] = ovfl_vec;
		vectors[VEC_FPUNDER] = unfl_vec;
		vectors[VEC_FPNAN] = snan_vec;
		vectors[VEC_FPOE] = operr_vec;
		vectors[VEC_FPBRUC] = bsun_vec;
		vectors[VEC_FPBRUC] = bsun_vec;
		vectors[VEC_LINE11] = fline_vec;
		vectors[VEC_FPUNSUP] = unsupp_vec;
	}
#endif
#ifdef CONFIG_IFPSP_060
	if (m68k_is040or060 == 6) {
	  /* set up IFPSP entry points */
 	  asmlinkage void snan_vec(void) asm ("_060_fpsp_snan");
 	  asmlinkage void operr_vec(void) asm ("_060_fpsp_operr");
 	  asmlinkage void ovfl_vec(void) asm ("_060_fpsp_ovfl");
 	  asmlinkage void unfl_vec(void) asm ("_060_fpsp_unfl");
 	  asmlinkage void dz_vec(void) asm ("_060_fpsp_dz");
 	  asmlinkage void inex_vec(void) asm ("_060_fpsp_inex");
 	  asmlinkage void fline_vec(void) asm ("_060_fpsp_fline");
 	  asmlinkage void unsupp_vec(void) asm ("_060_fpsp_unsupp");
 	  asmlinkage void effadd_vec(void) asm ("_060_fpsp_effadd");
  
 	  asmlinkage void unimp_vec(void) asm ("_060_isp_unimp");
  
 	  vectors[VEC_FPNAN] = snan_vec;
 	  vectors[VEC_FPOE] = operr_vec;
 	  vectors[VEC_FPOVER] = ovfl_vec;
 	  vectors[VEC_FPUNDER] = unfl_vec;
 	  vectors[VEC_FPDIVZ] = dz_vec;
 	  vectors[VEC_FPIR] = inex_vec;
 	  vectors[VEC_LINE11] = fline_vec;
 	  vectors[VEC_FPUNSUP] = unsupp_vec;
 	  vectors[VEC_UNIMPEA] = effadd_vec;
  
 	  /* set up ISP entry points */
  
 	  vectors[VEC_UNIMPII] = unimp_vec;
  
  	}
#endif
}

void set_evector(int vecnum, void (*handler)(void))
{
	if (vecnum >= 0 && vecnum <= 256)
		vectors[vecnum] = handler;
}


static inline void console_verbose(void)
{
	extern int console_loglevel;
	console_loglevel = 15;
	mach_debug_init();
}

char *vec_names[] = {
	"RESET SP", "RESET PC", "BUS ERROR", "ADDRESS ERROR",
	"ILLEGAL INSTRUCTION", "ZERO DIVIDE", "CHK", "TRAPcc",
	"PRIVILEGE VIOLATION", "TRACE", "LINE 1010", "LINE 1111",
	"UNASSIGNED RESERVED 12", "COPROCESSOR PROTOCOL VIOLATION",
	"FORMAT ERROR", "UNINITIALIZED INTERRUPT",
	"UNASSIGNED RESERVED 16", "UNASSIGNED RESERVED 17",
	"UNASSIGNED RESERVED 18", "UNASSIGNED RESERVED 19",
	"UNASSIGNED RESERVED 20", "UNASSIGNED RESERVED 21",
	"UNASSIGNED RESERVED 22", "UNASSIGNED RESERVED 23",
	"SPURIOUS INTERRUPT", "LEVEL 1 INT", "LEVEL 2 INT", "LEVEL 3 INT",
	"LEVEL 4 INT", "LEVEL 5 INT", "LEVEL 6 INT", "LEVEL 7 INT",
	"SYSCALL", "TRAP #1", "TRAP #2", "TRAP #3",
	"TRAP #4", "TRAP #5", "TRAP #6", "TRAP #7",
	"TRAP #8", "TRAP #9", "TRAP #10", "TRAP #11",
	"TRAP #12", "TRAP #13", "TRAP #14", "TRAP #15"
	};

char *space_names[] = {
	"Space 0", "User Data", "User Program", "Space 3",
	"Space 4", "Super Data", "Super Program", "CPU"
	};



extern void die_if_kernel(char *,struct pt_regs *,int);
asmlinkage int do_page_fault(struct pt_regs *regs, unsigned long address,
			      unsigned long error_code);

asmlinkage void trap_c(struct frame *fp);

static inline void access_error060 (struct frame *fp)
{
	unsigned long fslw = fp->un.fmt4.pc; /* is really FSLW for access error */

#ifdef DEBUG
	printk("fslw=%#lx, fa=%#lx\n", ssw, fp->un.fmt4.effaddr);
#endif

	if (fslw & MMU060_BPE) {
		/* branch prediction error -> clear branch cache */
		__asm__ __volatile__ ("movec %/cacr,%/d0\n\t"
				      "orl   #0x00400000,%/d0\n\t"
				      "movec %/d0,%/cacr"
				      : : : "d0" );
		/* return if there's no other error */
		if (!(fslw & MMU060_ERR_BITS))
			return;
	}
	
	if (fslw & (MMU060_DESC_ERR | MMU060_WP)) {
		unsigned long errorcode;
		unsigned long addr = fp->un.fmt4.effaddr;
		errorcode = ((fslw & MMU060_WP) ? 1 : 0) |
					((fslw & MMU060_W)  ? 2 : 0);
#ifdef DEBUG
		printk("errorcode = %d\n", errorcode );
#endif
		if (fslw & MMU060_MA)
		  addr = PAGE_ALIGN(addr);
		do_page_fault( (struct pt_regs *)fp, addr, errorcode );
	}
	else {
		printk( "68060 access error, fslw=%lx\n", fslw );
		trap_c( fp );
	}
}

static unsigned long probe040 (int iswrite, int fc, unsigned long addr)
{
	unsigned long mmusr;
	unsigned long fs = get_fs();

	set_fs (fc);

  	if (iswrite)
  		/* write */
		asm volatile ("movel %1,%/a0\n\t"
			      ".word 0xf548\n\t"	/* ptestw (a0) */
			      ".long 0x4e7a8805\n\t"	/* movec mmusr,a0 */
			      "movel %/a0,%0"
			      : "=g" (mmusr)
			      : "g" (addr)
			      : "a0");
	else
		asm volatile ("movel %1,%/a0\n\t"
			      ".word 0xf568\n\t"	/* ptestr (a0) */
			      ".long 0x4e7a8805\n\t"	/* movec mmusr,a0 */
			      "movel %/a0,%0"
			      : "=g" (mmusr)
			      : "g" (addr)
			      : "a0");


	set_fs (fs);

	return mmusr;
}

static void do_040writeback (unsigned short ssw,
			     unsigned short wbs,
			     unsigned long wba,
			     unsigned long wbd,
			     struct frame *fp)
{
	unsigned long fs = get_fs ();
	unsigned long mmusr;
	unsigned long errorcode;

	/*
	 * No special handling for the second writeback anymore.
	 * It misinterpreted the misaligned status sometimes.
	 * This way an extra page-fault may be caused (Martin Apel).
	 */

	mmusr = probe040 (1, wbs & WBTM_040,  wba);
	errorcode = (mmusr & MMU_R_040) ? 3 : 2;
	if (do_page_fault ((struct pt_regs *)fp, wba, errorcode))
	  /* just return if we can't perform the writeback */
	  return;

	set_fs (wbs & WBTM_040);
	switch (wbs & WBSIZ_040) {
	    case BA_SIZE_BYTE:
		put_fs_byte (wbd & 0xff, (char *)wba);
		break;
	    case BA_SIZE_WORD:
		put_fs_word (wbd & 0xffff, (short *)wba);
		break;
	    case BA_SIZE_LONG:
		put_fs_long (wbd, (int *)wba);
		break;
	}
	set_fs (fs);
}

static inline void access_error040 (struct frame *fp)
{
	unsigned short ssw = fp->un.fmt7.ssw;
	unsigned long mmusr;

#ifdef DEBUG
	printk("ssw=%#x, fa=%#lx\n", ssw, fp->un.fmt7.faddr);
        printk("wb1s=%#x, wb2s=%#x, wb3s=%#x\n", fp->un.fmt7.wb1s,  
		fp->un.fmt7.wb2s, fp->un.fmt7.wb3s);
	printk ("wb2a=%lx, wb3a=%lx, wb2d=%lx, wb3d=%lx\n", 
		fp->un.fmt7.wb2a, fp->un.fmt7.wb3a,
		fp->un.fmt7.wb2d, fp->un.fmt7.wb3d);
#endif


	if (ssw & ATC_040) {
		unsigned long addr = fp->un.fmt7.faddr;
		unsigned long errorcode;

		/*
		 * The MMU status has to be determined AFTER the address
		 * has been corrected if there was a misaligned access (MA).
		 */
		if (ssw & MA_040)
			addr = PAGE_ALIGN (addr);

		/* MMU error, get the MMUSR info for this access */
		mmusr = probe040 (!(ssw & RW_040), ssw & TM_040, addr);
		/*
#ifdef DEBUG
		printk("mmusr = %lx\n", mmusr);
#endif
*/
		errorcode = ((mmusr & MMU_R_040) ? 1 : 0) |
			((ssw & RW_040) ? 0 : 2);
		do_page_fault ((struct pt_regs *)fp, addr, errorcode);
	} else {
		printk ("68040 access error, ssw=%x\n", ssw);
		trap_c (fp);
	}

#if 0
	if (fp->un.fmt7.wb1s & WBV_040)
		printk("access_error040: cannot handle 1st writeback. oops.\n");
#endif

/*
 *  We may have to do a couple of writebacks here.
 *
 *  MR: we can speed up the thing a little bit and let do_040writeback()
 *  not produce another page fault as wb2 corresponds to the address that
 *  caused the fault. on write faults no second fault is generated, but
 *  on read faults for security reasons (although per definitionem impossible)
 */

	if (fp->un.fmt7.wb2s & WBV_040 && (fp->un.fmt7.wb2s &
					   WBTT_040) != BA_TT_MOVE16)
		do_040writeback (ssw,
				 fp->un.fmt7.wb2s, fp->un.fmt7.wb2a,
				 fp->un.fmt7.wb2d, fp);

	if (fp->un.fmt7.wb3s & WBV_040)
		do_040writeback (ssw, fp->un.fmt7.wb3s,
				 fp->un.fmt7.wb3a, fp->un.fmt7.wb3d,
				 fp);
}

static inline void bus_error030 (struct frame *fp)
{
	volatile unsigned short temp;
	unsigned short mmusr;
	unsigned long addr, desc, errorcode;
	unsigned short ssw = fp->un.fmtb.ssw;
	int user_space_fault = 1;

#if DEBUG
	printk ("pid = %x  ", current->pid);
	printk ("SSW=%#06x  ", ssw);

	if (ssw & (FC | FB))
		printk ("Instruction fault at %#010lx\n",
			ssw & FC ?
			fp->ptregs.format == 0xa ? fp->ptregs.pc + 2 : fp->un.fmtb.baddr - 2
			:
			fp->ptregs.format == 0xa ? fp->ptregs.pc + 4 : fp->un.fmtb.baddr);
	if (ssw & DF)
		printk ("Data %s fault at %#010lx in %s (pc=%#lx)\n",
			ssw & RW ? "read" : "write",
			fp->un.fmtb.daddr,
			space_names[ssw & DFC], fp->ptregs.pc);
#endif

	if (fp->ptregs.sr & PS_S) {
		/* kernel fault must be a data fault to user space */
		if (! ((ssw & DF) && ((ssw & DFC) == USER_DATA))) {
			/* instruction fault or kernel data fault! */
			if (ssw & (FC | FB))
				printk ("Instruction fault at %#010lx\n",
					fp->ptregs.pc);
			if (ssw & DF) {
				printk ("Data %s fault at %#010lx in %s (pc=%#lx)\n",
					ssw & RW ? "read" : "write",
					fp->un.fmtb.daddr,
					space_names[ssw & DFC], fp->ptregs.pc);
			}
			printk ("BAD KERNEL BUSERR\n");
			die_if_kernel("Oops",&fp->ptregs,0);
			force_sig(SIGSEGV, current);
			user_space_fault = 0;
		}
	} else {
		/* user fault */
		if (!(ssw & (FC | FB)) && !(ssw & DF))
			/* not an instruction fault or data fault! BAD */
			panic ("USER BUSERR w/o instruction or data fault");
		user_space_fault = 1;
#if DEBUG
		printk("User space bus-error\n");
#endif
	}

	/* ++andreas: If a data fault and an instruction fault happen
	   at the same time map in both pages.  */

	/* First handle the data fault, if any.  */
	if (ssw & DF)
	  {
	    addr = fp->un.fmtb.daddr;

	    if (user_space_fault) {
		    asm volatile ("ptestr #1,%2@,#7,%0\n\t"
				  "pmove %/psr,%1@"
				  : "=a&" (desc)
				  : "a" (&temp), "a" (addr));
		    mmusr = temp;
	    } else
		    mmusr = MMU_I;
      
#if DEBUG
	    printk ("mmusr is %#x for addr %#lx in task %p\n",
		    mmusr, addr, current);
	    printk ("descriptor address is %#lx, contents %#lx\n",
		    mm_ptov(desc), *(unsigned long *)mm_ptov(desc));
#endif

	    errorcode = (mmusr & MMU_I) ? 0 : 1;
	      /* if (!(ssw & RW)) updated to 1.2.13pl6 */
 	    if (!(ssw & RW) || ssw & RM)
		    errorcode |= 2;

	    if (mmusr & MMU_I)
		    do_page_fault ((struct pt_regs *)fp, addr, errorcode);

	    /* else if ((mmusr & MMU_WP) && !(ssw & RW)) */

 	    else if ((mmusr & MMU_WP) && (!(ssw & RW) || ssw & RM))
		    do_page_fault ((struct pt_regs *)fp, addr, errorcode);
	    else if (mmusr & (MMU_B|MMU_L|MMU_S)) {
		    printk ("invalid %s access at %#lx from pc %#lx\n",
			    !(ssw & RW) ? "write" : "read", addr,
			    fp->ptregs.pc);
		    die_if_kernel("Oops",&fp->ptregs,mmusr);
		    force_sig(SIGSEGV, current);
		    return;
	    } else {
#ifdef DEBUG
		    static volatile long tlong;
#endif

		    printk ("weird %s access at %#lx from pc %#lx (ssw is %#x)\n",
			    !(ssw & RW) ? "write" : "read", addr,
			    fp->ptregs.pc, ssw);
		    asm volatile ("ptestr #1,%1@,#0\n\t"
				  "pmove %/psr,%0@"
				  : /* no outputs */
				  : "a" (&temp), "a" (addr));
		    mmusr = temp;

		    printk ("level 0 mmusr is %#x\n", mmusr);
#if 0
		    asm volatile ("pmove %/tt0,%0@"
				  : /* no outputs */
				  : "a" (&tlong));
		    printk ("tt0 is %#lx, ", tlong);
		    asm volatile ("pmove %/tt1,%0@"
				  : /* no outputs */
				  : "a" (&tlong));
		    printk ("tt1 is %#lx\n", tlong);
#endif
#if DEBUG
		    printk("Unknown SIGSEGV - 1\n");
#endif
		    die_if_kernel("Oops",&fp->ptregs,mmusr);
		    force_sig(SIGSEGV, current);
		    return;
	    }

	    /* setup an ATC entry for the access about to be retried */
	    if (!(ssw & RW))
		    asm volatile ("ploadw #1,%0@" : /* no outputs */
				  : "a" (addr));
	    else
		    asm volatile ("ploadr #1,%0@" : /* no outputs */
				  : "a" (addr));

	    /* If this was a data fault due to an invalid page and a
	       prefetch is pending on the same page, simulate it (but
	       only if the page is now valid).  Otherwise we'll get an
	       weird insn access.  */
	    if ((ssw & RB) && (mmusr & MMU_I))
	      {
		unsigned long iaddr;

		if ((fp->ptregs.format) == 0xB)
		  iaddr = fp->un.fmtb.baddr;
		else
		  iaddr = fp->ptregs.pc + 4;
		if (((addr ^ iaddr) & PAGE_MASK) == 0)
		  {
		    /* We only need to check the ATC as the entry has
		       already been set up above.  */
		    asm volatile ("ptestr #1,%1@,#0\n\t"
				  "pmove %/psr,%0@"
				  : : "a" (&temp), "a" (iaddr));
		    mmusr = temp;
#ifdef DEBUG
		    printk ("prefetch iaddr=%#lx ssw=%#x mmusr=%#x\n",
			    iaddr, ssw, mmusr);
#endif
		    if (!(mmusr & MMU_I))
		      {
			unsigned short insn;
			asm volatile ("movesw %1@,%0"
				      : "=r" (insn)
				      : "a" (iaddr));
			fp->un.fmtb.isb = insn;
			fp->un.fmtb.ssw &= ~RB;
		      }
		  }
	      }
	  }

	/* Now handle the instruction fault. */

	/* get the fault address */
	if ((fp->ptregs.format) == 0xA )
		if (ssw & FC)
			addr = fp->ptregs.pc + 2;
		else if (ssw & FB)
			addr = fp->ptregs.pc + 4;
		else
			return;
	else
		if (ssw & FC)
			addr = fp->un.fmtb.baddr - 2;
		else if (ssw & FB)
			addr = fp->un.fmtb.baddr;
		else
			return;

	if ((ssw & DF) && ((addr ^ fp->un.fmtb.daddr) & PAGE_MASK) == 0)
		/* Insn fault on same page as data fault */
		return;

	if (user_space_fault) {
		asm volatile ("ptestr #1,%2@,#7,%0\n\t"
			      "pmove %/psr,%1@"
			      : "=a&" (desc)
			      : "a" (&temp), "a" (addr));
		mmusr = temp;
	} else
		mmusr = MMU_I;
      
#ifdef DEBUG
	printk ("mmusr is %#x for addr %#lx in task %p\n",
		mmusr, addr, current);
	printk ("descriptor address is %#lx, contents %#lx\n",
		mm_ptov(desc), *(unsigned long *)mm_ptov(desc));
#endif

	errorcode = (mmusr & MMU_I) ? 0 : 1;

	if (mmusr & MMU_I)
		do_page_fault ((struct pt_regs *)fp, addr, errorcode);
	else if (mmusr & (MMU_B|MMU_L|MMU_S)) {
		printk ("invalid insn access at %#lx from pc %#lx\n",
			addr, fp->ptregs.pc);
#if DEBUG
		printk("Unknown SIGSEGV - 2\n");
#endif
		die_if_kernel("Oops",&fp->ptregs,mmusr);
		force_sig(SIGSEGV, current);
		return;
	} else {
#ifdef DEBUG
		static volatile long tlong;
#endif

		printk ("weird insn access at %#lx from pc %#lx (ssw is %#x)\n",
			addr, fp->ptregs.pc, ssw);
		asm volatile ("ptestr #1,%1@,#0\n\t"
			      "pmove %/psr,%0@"
			      : /* no outputs */
			      : "a" (&temp), "a" (addr));
		mmusr = temp;
		      
		printk ("level 0 mmusr is %#x\n", mmusr);
#ifdef DEBUG
		if (boot_info.cputype & CPU_68030) {
			asm volatile ("pmove %/tt0,%0@"
				      : /* no outputs */
				      : "a" (&tlong));
			printk ("tt0 is %#lx, ", tlong);
			asm volatile ("pmove %/tt1,%0@"
				      : /* no outputs */
				      : "a" (&tlong));
			printk ("tt1 is %#lx\n", tlong);
		}

#endif
#if DEBUG
		printk("Unknown SIGSEGV - 3\n");
#endif
		die_if_kernel("Oops",&fp->ptregs,mmusr);
		force_sig(SIGSEGV, current);
		return;
	}

	/* setup an ATC entry for the access about to be retried */
	asm volatile ("ploadr #1,%0@" : /* no outputs */
		      : "a" (addr));
}

asmlinkage void buserr_c(struct frame *fp)
{
	/* Only set esp0 if coming from user mode */
	if (user_mode(&fp->ptregs))
		current->tss.esp0 = (unsigned long) fp;

#if DEBUG
	printk ("*** Bus Error *** Format is %x\n", fp->ptregs.format);
#endif

	switch (fp->ptregs.format) {
	case 4:				/* 68060 access error */
	  access_error060 (fp);
	  break;
	case 0x7:			/* 68040 access error */
	  access_error040 (fp);
	  break;
	case 0xa:
	case 0xb:
	  bus_error030 (fp);
	  break;
	default:
	  die_if_kernel("bad frame format",&fp->ptregs,0);
#if DEBUG
	  printk("Unknown SIGSEGV - 4\n");
#endif
	  force_sig(SIGSEGV, current);
	}
}


int kstack_depth_to_print = 48;

/* MODULE_RANGE is a guess of how much space is likely to be
   vmalloced.  */
#define MODULE_RANGE (8*1024*1024)

static void dump_stack(struct frame *fp)
{
	unsigned long *stack, *endstack, addr, module_start, module_end;
	extern char _start, _etext;
	int i;

	addr = (unsigned long)&fp->un;
	printk("Frame format=%X ", fp->ptregs.format);
	switch (fp->ptregs.format) {
	case 0x2:
	    printk("instr addr=%08lx\n", fp->un.fmt2.iaddr);
	    addr += sizeof(fp->un.fmt2);
	    break;
	case 0x3:
	    printk("eff addr=%08lx\n", fp->un.fmt3.effaddr);
	    addr += sizeof(fp->un.fmt3);
	    break;
	case 0x4:
	    printk((m68k_is040or060 == 6 ? "fault addr=%08lx fslw=%08lx\n"
		    : "eff addr=%08lx pc=%08lx\n"),
		   fp->un.fmt4.effaddr, fp->un.fmt4.pc);
	    addr += sizeof(fp->un.fmt4);
	    break;
	case 0x7:
	    printk("eff addr=%08lx ssw=%04x faddr=%08lx\n",
		   fp->un.fmt7.effaddr, fp->un.fmt7.ssw, fp->un.fmt7.faddr);
	    printk("wb 1 stat/addr/data: %04x %08lx %08lx\n",
		   fp->un.fmt7.wb1s, fp->un.fmt7.wb1a, fp->un.fmt7.wb1dpd0);
	    printk("wb 2 stat/addr/data: %04x %08lx %08lx\n",
		   fp->un.fmt7.wb2s, fp->un.fmt7.wb2a, fp->un.fmt7.wb2d);
	    printk("wb 3 stat/addr/data: %04x %08lx %08lx\n",
		   fp->un.fmt7.wb3s, fp->un.fmt7.wb3a, fp->un.fmt7.wb3d);
	    printk("push data: %08lx %08lx %08lx %08lx\n",
		   fp->un.fmt7.wb1dpd0, fp->un.fmt7.pd1, fp->un.fmt7.pd2,
		   fp->un.fmt7.pd3);
	    addr += sizeof(fp->un.fmt7);
	    break;
	case 0x9:
	    printk("instr addr=%08lx\n", fp->un.fmt9.iaddr);
	    addr += sizeof(fp->un.fmt9);
	    break;
	case 0xa:
	    printk("ssw=%04x isc=%04x isb=%04x daddr=%08lx dobuf=%08lx\n",
		   fp->un.fmta.ssw, fp->un.fmta.isc, fp->un.fmta.isb,
		   fp->un.fmta.daddr, fp->un.fmta.dobuf);
	    addr += sizeof(fp->un.fmta);
	    break;
	case 0xb:
	    printk("ssw=%04x isc=%04x isb=%04x daddr=%08lx dobuf=%08lx\n",
		   fp->un.fmtb.ssw, fp->un.fmtb.isc, fp->un.fmtb.isb,
		   fp->un.fmtb.daddr, fp->un.fmtb.dobuf);
	    printk("baddr=%08lx dibuf=%08lx ver=%x\n",
		   fp->un.fmtb.baddr, fp->un.fmtb.dibuf, fp->un.fmtb.ver);
	    addr += sizeof(fp->un.fmtb);
	    break;
	default:
	    printk("\n");
	}

	stack = (unsigned long *)addr;
	endstack = (unsigned long *)PAGE_ALIGN(addr);

	printk("Stack from %08lx:\n       ", (unsigned long)stack);
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (stack + 1 > endstack)
			break;
		if (i && ((i % 8) == 0))
			printk("\n       ");
		printk("%08lx ", *stack++);
	}

	printk ("\nCall Trace: ");
	stack = (unsigned long *) addr;
	i = 1;
	module_start = VMALLOC_START;
	module_end = module_start + MODULE_RANGE;
	while (stack + 1 <= endstack) {
		addr = *stack++;
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (((addr >= (unsigned long) &_start) &&
		     (addr <= (unsigned long) &_etext)) ||
		    ((addr >= module_start) && (addr <= module_end))) {
			if (i && ((i % 8) == 0))
				printk("\n       ");
			printk("[<%08lx>] ", addr);
			i++;
		}
	}
	printk("\nCode: ");
	for (i = 0; i < 10; i++)
		printk("%04x ", 0xffff & ((short *) fp->ptregs.pc)[i]);
	printk ("\n");
}

void bad_super_trap (struct frame *fp)
{
	console_verbose();
	if ((fp->ptregs.vector) < 48*4)
		printk ("*** %s ***   FORMAT=%X\n",
			vec_names[(fp->ptregs.vector) >> 2],
			fp->ptregs.format);
	else
		printk ("*** Exception %d ***   FORMAT=%X\n",
			(fp->ptregs.vector) >> 2, 
			fp->ptregs.format);
	if (((fp->ptregs.vector) >> 2) == VEC_ADDRERR
	    && !m68k_is040or060) {
		unsigned short ssw = fp->un.fmtb.ssw;

		printk ("SSW=%#06x  ", ssw);

		if (ssw & RC)
			printk ("Pipe stage C instruction fault at %#010lx\n",
				(fp->ptregs.format) == 0xA ?
				fp->ptregs.pc + 2 : fp->un.fmtb.baddr - 2);
		if (ssw & RB)
			printk ("Pipe stage B instruction fault at %#010lx\n",
				(fp->ptregs.format) == 0xA ?
				fp->ptregs.pc + 4 : fp->un.fmtb.baddr);
		if (ssw & DF)
			printk ("Data %s fault at %#010lx in %s (pc=%#lx)\n",
				ssw & RW ? "read" : "write",
				fp->un.fmtb.daddr, space_names[ssw & DFC],
				fp->ptregs.pc);
	}
	printk ("Current process id is %d\n", current->pid);
	die_if_kernel("BAD KERNEL TRAP", &fp->ptregs, 0);
}

asmlinkage void trap_c(struct frame *fp)
{
	int sig;

	if ((fp->ptregs.sr & PS_S)
	    && ((fp->ptregs.vector) >> 2) == VEC_TRACE
	    && !(fp->ptregs.sr & PS_T)) {
		/* traced a trapping instruction */
		unsigned char *lp = ((unsigned char *)&fp->un.fmt2) + 4;
		current->flags |= PF_DTRACE;
		/* clear the trace bit */
		(*(unsigned short *)lp) &= ~PS_T;
		return;
	} else if (fp->ptregs.sr & PS_S) {
		bad_super_trap(fp);
		return;
	}

	/* send the appropriate signal to the user program */
	switch ((fp->ptregs.vector) >> 2) {
	    case VEC_ADDRERR:
		sig = SIGBUS;
		break;
	    case VEC_BUSERR:
		sig = SIGSEGV;
		break;
	    case VEC_ILLEGAL:
	    case VEC_PRIV:
	    case VEC_LINE10:
	    case VEC_LINE11:
	    case VEC_COPROC:
	    case VEC_TRAP1:
	    case VEC_TRAP2:
	    case VEC_TRAP3:
	    case VEC_TRAP4:
	    case VEC_TRAP5:
	    case VEC_TRAP6:
	    case VEC_TRAP7:
	    case VEC_TRAP8:
	    case VEC_TRAP9:
	    case VEC_TRAP10:
	    case VEC_TRAP11:
	    case VEC_TRAP12:
	    case VEC_TRAP13:
	    case VEC_TRAP14:
		sig = SIGILL;
		break;
	    case VEC_FPBRUC:
	    case VEC_FPIR:
	    case VEC_FPDIVZ:
	    case VEC_FPUNDER:
	    case VEC_FPOE:
	    case VEC_FPOVER:
	    case VEC_FPNAN:
		{
		  unsigned char fstate[216];

		  __asm__ __volatile__ ("fsave %0@" : : "a" (fstate) : "memory");
		  /* Set the exception pending bit in the 68882 idle frame */
		  if (*(unsigned short *) fstate == 0x1f38)
		    {
		      fstate[fstate[1]] |= 1 << 3;
		      __asm__ __volatile__ ("frestore %0@" : : "a" (fstate));
		    }
		}
		/* fall through */
	    case VEC_ZERODIV:
	    case VEC_TRAP:
		sig = SIGFPE;
		break;
	    case VEC_TRACE:		/* ptrace single step */
		fp->ptregs.sr &= ~PS_T;
	    case VEC_TRAP15:		/* breakpoint */
		sig = SIGTRAP;
		break;
	    default:
		sig = SIGILL;
		break;
	}

	force_sig (sig, current);
}

asmlinkage void set_esp0 (unsigned long ssp)
{
  current->tss.esp0 = ssp;
}

void die_if_kernel (char *str, struct pt_regs *fp, int nr)
{
	if (!(fp->sr & PS_S))
		return;

	console_verbose();
	printk("%s: %08x\n",str,nr);
	printk("PC: %08lx\nSR: %04x  SP: %p\n", fp->pc, fp->sr, fp);
	printk("d0: %08lx    d1: %08lx    d2: %08lx    d3: %08lx\n",
	       fp->d0, fp->d1, fp->d2, fp->d3);
	printk("d4: %08lx    d5: %08lx    a0: %08lx    a1: %08lx\n",
	       fp->d4, fp->d5, fp->a0, fp->a1);

	if (STACK_MAGIC != *(unsigned long *)current->kernel_stack_page)
		printk("Corrupted stack page\n");
	printk("Process %s (pid: %d, stackpage=%08lx)\n",
		current->comm, current->pid, current->kernel_stack_page);
	dump_stack((struct frame *)fp);
	do_exit(SIGSEGV);
}
