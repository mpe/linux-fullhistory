/*
 * Minimalist Kernel Debugger
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Scott Lurndal (slurn@engr.sgi.com)
 * Copyright (C) Scott Foehner (sfoehner@engr.sgi.com)
 * Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
 * Copyright (C) David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Written March 1999 by Scott Lurndal at Silicon Graphics, Inc.
 *
 * Modifications from:
 *      Richard Bass                    1999/07/20
 *              Many bug fixes and enhancements.
 *      Scott Foehner
 *              Port to ia64
 *      Srinivasa Thirumalachar
 *              RSE support for ia64
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kdb.h>
#include <linux/stddef.h> 
#include <linux/vmalloc.h>

#include <asm/delay.h>
#include <asm/kdbsupport.h>
#include <asm/rse.h>
#include <asm/uaccess.h>

extern kdb_state_t kdb_state ;
k_machreg_t dbregs[KDB_DBREGS];

static int __init
kdb_setup (char *str)
{
	kdb_flags |= KDB_FLAG_EARLYKDB;
	return 1;
}

__setup("kdb", kdb_setup);

static int
kdb_ia64_itm (int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int diag;
	unsigned long val;

	diag = kdbgetularg(argv[1], &val);
	if (diag)
		return diag;
	kdb_printf("new itm=%0xlx\n", val);

	ia64_set_itm(val);
	return 0;
}

static int
kdb_ia64_sir (int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	u64 lid, tpr, lrr0, lrr1, itv, pmv, cmcv;

	asm ("mov %0=cr.lid" : "=r"(lid));
	asm ("mov %0=cr.tpr" : "=r"(tpr));
	asm ("mov %0=cr.lrr0" : "=r"(lrr0));
	asm ("mov %0=cr.lrr1" : "=r"(lrr1));
	printk("lid=0x%lx, tpr=0x%lx, lrr0=0x%lx, llr1=0x%lx\n", lid, tpr, lrr0, lrr1);

	asm ("mov %0=cr.itv" : "=r"(itv));
	asm ("mov %0=cr.pmv" : "=r"(pmv));
	asm ("mov %0=cr.cmcv" : "=r"(cmcv));
	printk("itv=0x%lx, pmv=0x%lx, cmcv=0x%lx\n", itv, pmv, cmcv);

	printk("irr=0x%016lx,0x%016lx,0x%016lx,0x%016lx\n",
		ia64_get_irr0(), ia64_get_irr1(), ia64_get_irr2(), ia64_get_irr3());

	printk("itc=0x%016lx, itm=0x%016lx\n", ia64_get_itc(), ia64_get_itm());
	return 0;
}

void __init
kdb_init (void)
{
	extern void kdb_inittab(void);
	unsigned long reg;

	kdb_inittab();
	kdb_initbptab();
#if 0
	kdb_disinit();
#endif
	kdb_printf("kdb version %d.%d by Scott Lurndal. "\
		   "Copyright SGI, All Rights Reserved\n",
		   KDB_MAJOR_VERSION, KDB_MINOR_VERSION);

	/* Enable debug registers */
        __asm__ ("mov %0=psr":"=r"(reg));
        reg |= IA64_PSR_DB;
        __asm__ ("mov psr.l=%0"::"r"(reg));
        ia64_srlz_d();

	/* Init kdb state */
	kdb_state.bkpt_handling_state = BKPTSTATE_NOT_HANDLED ;

	kdb_register("irr", kdb_ia64_sir, "", "Show interrupt registers", 0);
	kdb_register("itm", kdb_ia64_itm, "", "Set new ITM value", 0);
}

/*
 * kdbprintf
 * kdbgetword
 * kdb_getstr
 */

char *
kbd_getstr(char *buffer, size_t bufsize, char *prompt)
{
	extern char* kdb_getscancode(char *, size_t);

#if defined(CONFIG_SMP)
	kdb_printf(prompt, smp_processor_id());
#else
	kdb_printf("%s", prompt);
#endif

	return kdb_getscancode(buffer, bufsize);

}

int
kdb_printf(const char *fmt, ...)
{
	char buffer[256];
	va_list	ap;
	int diag;
	int linecount;

	diag = kdbgetintenv("LINES", &linecount);
	if (diag) 
		linecount = 22;

	va_start(ap, fmt);
	vsprintf(buffer, fmt, ap);
	va_end(ap);

	printk("%s", buffer);
#if 0
	if (strchr(buffer, '\n') != NULL) {
		kdb_nextline++;
	}

	if (kdb_nextline == linecount) {
		char buf1[16];
		char buf2[32];
		extern char* kdb_getscancode(char *, size_t);
		char *moreprompt;

		/*
		 * Pause until cr.
		 */
		moreprompt = kdbgetenv("MOREPROMPT");
		if (moreprompt == NULL) {
			moreprompt = "more> ";
		}

#if defined(CONFIG_SMP)
		if (strchr(moreprompt, '%')) {
			sprintf(buf2, moreprompt, smp_processor_id());
			moreprompt = buf2;
		}
#endif

		printk(moreprompt);
		(void) kdb_getscancode(buf1, sizeof(buf1));

		kdb_nextline = 1;

		if ((buf1[0] == 'q') 
		 || (buf1[0] == 'Q')) {
			kdb_longjmp(&kdbjmpbuf, 1);
		}
	}
#endif	
	return 0;
}

unsigned long
kdbgetword(unsigned long addr, int width)
{
	/*
	 * This function checks the address for validity.  Any address
	 * in the range PAGE_OFFSET to high_memory is legal, any address
	 * which maps to a vmalloc region is legal, and any address which
	 * is a user address, we use get_user() to verify validity.
	 */

	if (addr < PAGE_OFFSET) {
		/*
		 * Usermode address.
		 */
		unsigned long diag;
		unsigned long ulval;

		switch (width) {
	        case 8:
		{	unsigned long *lp;

			lp = (unsigned long *) addr;
			diag = get_user(ulval, lp);
			break;
		}
		case 4:
		{	unsigned int *ip;

			ip = (unsigned int *) addr;
			diag = get_user(ulval, ip);
			break;
		}
		case 2:
		{	unsigned short *sp;

			sp = (unsigned short *) addr;
			diag = get_user(ulval, sp);
			break;
		}
		case 1:
		{	unsigned char *cp;

			cp = (unsigned char *) addr;
			diag = get_user(ulval, cp);
			break;
		}
		default:
			printk("kdbgetword: Bad width\n");
			return 0L;
		}
			
		if (diag) {
			if ((kdb_flags & KDB_FLAG_SUPRESS) == 0) {
				printk("kdb: Bad user address 0x%lx\n", addr);
				kdb_flags |= KDB_FLAG_SUPRESS;
			}
			return 0L;
		}
		kdb_flags &= ~KDB_FLAG_SUPRESS;
		return ulval;
	}

	if (addr > (unsigned long)high_memory) {
		extern int kdb_vmlist_check(unsigned long, unsigned long);

		if (!kdb_vmlist_check(addr, addr+width)) {
			/*
			 * Would appear to be an illegal kernel address; 
			 * Print a message once, and don't print again until
			 * a legal address is used.
			 */
			if ((kdb_flags & KDB_FLAG_SUPRESS) == 0) {
				printk("kdb: Bad kernel address 0x%lx\n", addr);
				kdb_flags |= KDB_FLAG_SUPRESS;
			}
			return 0L;
		}
	}

	/*
	 * A good address.  Reset error flag.
	 */
	kdb_flags &= ~KDB_FLAG_SUPRESS;

	switch (width) {
	case 8:
	{	unsigned long *lp;

		lp = (unsigned long *)(addr);
		return *lp;
	}
	case 4:
	{	unsigned int *ip;

		ip = (unsigned int *)(addr);
		return *ip;
	}
	case 2:
	{	unsigned short *sp;

		sp = (unsigned short *)(addr);
		return *sp;
	}
	case 1:
	{	unsigned char *cp;

		cp = (unsigned char *)(addr);
		return *cp;
	}
	}

	printk("kdbgetword: Bad width\n");
	return 0L;
}

/*
 * Start of breakpoint management routines
 */

/*
 * Arg: bp structure
 */

int
kdb_allocdbreg(kdb_bp_t *bp)
{
	int i=0;

	/* For inst bkpt, just return. No hw reg alloc to be done. */

	if (bp->bp_mode == BKPTMODE_INST) {
		return i;
	} else if (bp->bp_mode == BKPTMODE_DATAW) {
		for(i=0; i<KDB_DBREGS; i++) {
			if (dbregs[i] == 0xffffffff) {
				dbregs[i] = 0;
				return i;
			}
		}
	}

	return -1;
}

void
kdb_freedbreg(kdb_bp_t *bp)
{
	if (bp->bp_mode == BKPTMODE_DATAW)
		dbregs[bp->bp_reg] = 0xffffffff;
}

void
kdb_initdbregs(void)
{
	int i;

	for(i=0; i<KDB_DBREGS; i++) {
		dbregs[i] = 0xffffffff;
	}
}
int
kdbinstalltrap(int type, handler_t newh, handler_t *oldh)
{
	/*
	 * Usurp INTn.  XXX - TBD.
	 */

	return 0;
}

int
install_instbkpt(kdb_bp_t *bp)
{
	unsigned long	*addr = (unsigned long *)bp->bp_addr ;
	bundle_t	*bundle = (bundle_t *)bp->bp_longinst;

	/* save current bundle */
	*bundle = *(bundle_t *)addr ;

	/* Set the break point! */
	((bundle_t *)addr)->lform.low8 = (
		(((bundle_t *)addr)->lform.low8 & ~INST_SLOT0_MASK) |
		BREAK_INSTR);

	/* set flag */
	bp->bp_instvalid = 1 ;

	/* flush icache as it is stale now */
	ia64_flush_icache_page((unsigned long)addr) ;

#ifdef KDB_DEBUG
	kdb_printf ("[0x%016lx]: install 0x%016lx with 0x%016lx\n",
		    addr, bundle->lform.low8, addr[0]) ;
#endif
	return 0 ;
}

int
install_databkpt(kdb_bp_t *bp)
{
	unsigned long dbreg_addr = bp->bp_reg * 2;
	unsigned long dbreg_cond = dbreg_addr + 1;
	unsigned long value = 0x8fffffffffffffff;
	unsigned long addr = (unsigned long)bp->bp_addr;
	__asm__ ("mov dbr[%0]=%1"::"r"(dbreg_cond),"r"(value));
//	__asm__ ("movl %0,%%db0\n\t"::"r"(contents));
	__asm__ ("mov dbr[%0]=%1"::"r"(dbreg_addr),"r"(addr));
	ia64_insn_group_barrier();
	ia64_srlz_i();
	ia64_insn_group_barrier();

#ifdef KDB_DEBUG
	kdb_printf("installed dbkpt at 0x%016lx\n", addr) ;
#endif
	return 0;
}

int
kdbinstalldbreg(kdb_bp_t *bp)
{
	if (bp->bp_mode == BKPTMODE_INST) {
		return install_instbkpt(bp) ;
	} else if (bp->bp_mode == BKPTMODE_DATAW) {
		return install_databkpt(bp) ;
	}
	return 0;
}

void
remove_instbkpt(kdb_bp_t *bp)
{
	unsigned long   *addr = (unsigned long *)bp->bp_addr ;
	bundle_t        *bundle = (bundle_t *)bp->bp_longinst;

	if (!bp->bp_instvalid)
	/* Nothing to remove. If we just alloced the bkpt
	 * but never resumed, the bp_inst will not be valid. */
		return ;

#ifdef KDB_DEBUG
	kdb_printf ("[0x%016lx]: remove 0x%016lx with 0x%016lx\n", 
		    addr, addr[0], bundle->lform.low8) ;
#endif

	/* restore current bundle */
	*(bundle_t *)addr = *bundle ;
	/* reset the flag */
	bp->bp_instvalid = 0 ;
	ia64_flush_icache_page((unsigned long)addr) ;
}
  
void
remove_databkpt(kdb_bp_t *bp)
{
	int		regnum = bp->bp_reg ;
	unsigned long dbreg_addr = regnum * 2;
	unsigned long dbreg_cond = dbreg_addr + 1;
	unsigned long value = 0x0fffffffffffffff;
	__asm__ ("mov dbr[%0]=%1"::"r"(dbreg_cond),"r"(value));
//	__asm__ ("movl %0,%%db0\n\t"::"r"(contents));
	ia64_insn_group_barrier();
	ia64_srlz_i();
	ia64_insn_group_barrier();

#ifdef KDB_DEBUG
	kdb_printf("removed dbkpt at 0x%016lx\n", bp->bp_addr) ;
#endif
}

void
kdbremovedbreg(kdb_bp_t *bp)
{
	if (bp->bp_mode == BKPTMODE_INST) {
		remove_instbkpt(bp) ;
	} else if (bp->bp_mode == BKPTMODE_DATAW) {
		remove_databkpt(bp) ;
	}
}

k_machreg_t
kdb_getdr6(void)
{
	return kdb_getdr(6);
}

k_machreg_t
kdb_getdr7(void)
{
	return kdb_getdr(7);
}

k_machreg_t
kdb_getdr(int regnum)
{
	k_machreg_t contents = 0;
	unsigned long reg = (unsigned long)regnum;

	__asm__ ("mov %0=ibr[%1]"::"r"(contents),"r"(reg));
//        __asm__ ("mov ibr[%0]=%1"::"r"(dbreg_cond),"r"(value));	

	return contents;
}


k_machreg_t
kdb_getcr(int regnum)
{
	k_machreg_t contents = 0;
	return contents;
}

void
kdb_putdr6(k_machreg_t contents)
{
	kdb_putdr(6, contents);
}

void
kdb_putdr7(k_machreg_t contents)
{
	kdb_putdr(7, contents);
}

void
kdb_putdr(int regnum, k_machreg_t contents)
{
}

void
get_fault_regs(fault_regs_t *fr)
{
	fr->ifa = 0 ;
	fr->isr = 0 ;

	__asm__ ("rsm psr.ic;;") ;
        ia64_srlz_d();
        __asm__ ("mov %0=cr.ifa" : "=r"(fr->ifa));
        __asm__ ("mov %0=cr.isr" : "=r"(fr->isr));
	__asm__ ("ssm psr.ic;;") ;
        ia64_srlz_d();
}

/*
 * kdb_db_trap
 *
 * 	Perform breakpoint processing upon entry to the
 *	processor debugger fault.   Determine and print
 *	the active breakpoint.
 *
 * Parameters:
 *	ef	Exception frame containing machine register state
 *	reason	Why did we enter kdb - fault or break
 * Outputs:
 *	None.
 * Returns:
 *	0	Standard instruction or data breakpoint encountered
 *	1	Single Step fault ('ss' command)
 *	2	Single Step fault, caller should continue ('ssb' command)
 * Locking:
 *	None.
 * Remarks:
 *	Yup, there be goto's here.
 */

int
kdb_db_trap(struct pt_regs *ef, int reason)
{
	int i, rv=0;

	/* Trying very hard to not change the interface to kdb.
	 * So, eventhough we have these values in the fault function
	 * it is not passed in but read again.
	 */
	fault_regs_t	faultregs ;

	if (reason == KDB_REASON_FLTDBG)
		get_fault_regs(&faultregs) ;

	/* NOTE : XXX: This has to be done only for data bkpts */
	/* Prevent it from continuously faulting */
	ef->cr_ipsr |= 0x0000002000000000;

	if (ef->cr_ipsr & 0x0000010000000000) {
		/* single step */
		ef->cr_ipsr &= 0xfffffeffffffffff;
		if ((kdb_state.bkpt_handling_state == BKPTSTATE_HANDLED) 
			&& (kdb_state.cmd_given == CMDGIVEN_GO)) 
				; 
		else
			kdb_printf("SS trap at 0x%lx\n", ef->cr_iip + ia64_psr(ef)->ri);
		rv = 1;
		kdb_state.reason_for_entry = ENTRYREASON_SSTEP ;
		goto handled;
	} else
		kdb_state.reason_for_entry = ENTRYREASON_GO ;

        /*
         * Determine which breakpoint was encountered.
         */
        for(i=0; i<KDB_MAXBPT; i++) {
                if ((breakpoints[i].bp_enabled)
                 && ((breakpoints[i].bp_addr == ef->cr_iip) ||
			((faultregs.ifa) && 
			 (breakpoints[i].bp_addr == faultregs.ifa)))) {
                        /*
                         * Hit this breakpoint.  Remove it while we are
                         * handling hit to avoid recursion. XXX ??
                         */
			if (breakpoints[i].bp_addr == faultregs.ifa)
                        	kdb_printf("Data breakpoint #%d for 0x%lx at 0x%lx\n",
                                  i, breakpoints[i].bp_addr, ef->cr_iip + ia64_psr(ef)->ri);
			else
                        	kdb_printf("%s breakpoint #%d at 0x%lx\n",
                                  	rwtypes[0],
                                  	i, breakpoints[i].bp_addr);

                        /*
                         * For an instruction breakpoint, disassemble
                         * the current instruction.
                         */
#if 0
                        if (rw == 0) {
                                kdb_id1(ef->eip);
                        }
#endif

                        goto handled;
                }
        }

#if 0
unknown:
#endif
        kdb_printf("Unknown breakpoint.  Should forward. \n");
	/* Need a flag for this. The skip should be done XXX
	 * when a go or single step command is done for this session.
	 * For now it is here.
 	 */
	ia64_increment_ip(ef) ;
	return rv ;

handled:

	/* We are here after handling a break inst/data bkpt */
	if (kdb_state.bkpt_handling_state == BKPTSTATE_NOT_HANDLED) {
		kdb_state.bkpt_handling_state = BKPTSTATE_HANDLED ;
		if (kdb_state.reason_for_entry == ENTRYREASON_GO) {
			kdb_setsinglestep(ef) ;
			kdb_state.kdb_action = ACTION_NOBPINSTALL; 
			/* We dont want bp install just this once */
			kdb_state.cmd_given = CMDGIVEN_UNKNOWN ;
		}
	} else if (kdb_state.bkpt_handling_state == BKPTSTATE_HANDLED) {
                kdb_state.bkpt_handling_state = BKPTSTATE_NOT_HANDLED ;
                if (kdb_state.reason_for_entry == ENTRYREASON_SSTEP) {
			if (kdb_state.cmd_given == CMDGIVEN_GO)
				kdb_state.kdb_action = ACTION_NOPROMPT ;
			kdb_state.cmd_given = CMDGIVEN_UNKNOWN ;
		}
	} else
		kdb_printf("Unknown value of bkpt state\n") ;

	return rv;

}

void
kdb_setsinglestep(struct pt_regs *regs)
{
	regs->cr_ipsr |= 0x0000010000000000;
#if 0
	regs->eflags |= EF_TF;
#endif
}

/*
 * Symbol table functions.
 */

/*
 * kdbgetsym
 *
 *	Return the symbol table entry for the given symbol
 *
 * Parameters:
 * 	symname	Character string containing symbol name
 * Outputs:
 * Returns:
 *	NULL	Symbol doesn't exist
 *	ksp	Pointer to symbol table entry
 * Locking:
 *	None.
 * Remarks:
 */

__ksymtab_t *
kdbgetsym(const char *symname)
{
	__ksymtab_t *ksp = __kdbsymtab;
	int i;

	if (symname == NULL)
		return NULL;

	for (i=0; i<__kdbsymtabsize; i++, ksp++) {
		if (ksp->name && (strcmp(ksp->name, symname)==0)) {
			return ksp;
		}
	}

	return NULL;
}

/*
 * kdbgetsymval
 *
 *	Return the address of the given symbol.
 *
 * Parameters:
 * 	symname	Character string containing symbol name
 * Outputs:
 * Returns:
 *	0	Symbol name is NULL
 *	addr	Address corresponding to symname
 * Locking:
 *	None.
 * Remarks:
 */

unsigned long
kdbgetsymval(const char *symname)
{
	__ksymtab_t *ksp = kdbgetsym(symname);

	return (ksp?ksp->value:0);
}

/*
 * kdbaddmodsym
 *
 *	Add a symbol to the kernel debugger symbol table.  Called when
 *	a new module is loaded into the kernel.
 *
 * Parameters:
 * 	symname	Character string containing symbol name
 *	value	Value of symbol
 * Outputs:
 * Returns:
 *	0	Successfully added to table.
 *	1	Duplicate symbol
 *	2	Symbol table full
 * Locking:
 *	None.
 * Remarks:
 */

int
kdbaddmodsym(char *symname, unsigned long value) 
{

	/*
	 * Check for duplicate symbols.
	 */
	if (kdbgetsym(symname)) {
		printk("kdb: Attempt to register duplicate symbol '%s' @ 0x%lx\n",
			symname, value);
		return 1;
	}

	if (__kdbsymtabsize < __kdbmaxsymtabsize) {
		__ksymtab_t *ksp = &__kdbsymtab[__kdbsymtabsize++];
		
		ksp->name = symname;
		ksp->value = value;
		return 0;
	}

	/*
	 * No room left in kernel symbol table.
	 */
	{
		static int __kdbwarn = 0;

		if (__kdbwarn == 0) {
			__kdbwarn++;
			printk("kdb: Exceeded symbol table size.  Increase CONFIG_KDB_SYMTAB_SIZE in kernel configuration\n");
		}
	}

	return 2;
}

/*
 * kdbdelmodsym
 *
 *	Add a symbol to the kernel debugger symbol table.  Called when
 *	a new module is loaded into the kernel.
 *
 * Parameters:
 * 	symname	Character string containing symbol name
 *	value	Value of symbol
 * Outputs:
 * Returns:
 *	0	Successfully added to table.
 *	1	Symbol not found
 * Locking:
 *	None.
 * Remarks:
 */

int
kdbdelmodsym(const char *symname)
{
	__ksymtab_t *ksp, *endksp;

	if (symname == NULL)
		return 1;

	/*
	 * Search for the symbol.  If found, move 
	 * all successive symbols down one position
	 * in the symbol table to avoid leaving holes.
	 */
	endksp = &__kdbsymtab[__kdbsymtabsize];
	for (ksp = __kdbsymtab; ksp < endksp; ksp++) {
		if (ksp->name && (strcmp(ksp->name, symname) == 0)) {
			endksp--;
			for ( ; ksp < endksp; ksp++) {
				*ksp = *(ksp + 1);
			}
			__kdbsymtabsize--;
			return 0;
		}
	}

	return 1;
}

/*
 * kdbnearsym
 *
 *	Return the name of the symbol with the nearest address
 *	less than 'addr'.
 *
 * Parameters:
 * 	addr	Address to check for symbol near
 * Outputs:
 * Returns:
 *	NULL	No symbol with address less than 'addr'
 *	symbol	Returns the actual name of the symbol.
 * Locking:
 *	None.
 * Remarks:
 */

char *
kdbnearsym(unsigned long addr)
{
	__ksymtab_t *ksp = __kdbsymtab;
	__ksymtab_t *kpp = NULL;
	int i;

	for(i=0; i<__kdbsymtabsize; i++, ksp++) {
		if (!ksp->name) 
			continue;

		if (addr == ksp->value) {
			kpp = ksp;
			break;
		}
		if (addr > ksp->value) {
			if ((kpp == NULL) 
			 || (ksp->value > kpp->value)) {
				kpp = ksp;
			}
		}
	}

	/*
	 * If more than 128k away, don't bother.
	 */
	if ((kpp == NULL)
	 || ((addr - kpp->value) > 0x20000)) {
		return NULL;
	}

	return kpp->name;
}

/*
 * kdbgetregcontents
 *
 *	Return the contents of the register specified by the 
 *	input string argument.   Return an error if the string
 *	does not match a machine register.
 *
 *	The following pseudo register names are supported:
 *	   &regs	 - Prints address of exception frame
 *	   kesp		 - Prints kernel stack pointer at time of fault
 * 	   sstk		 - Prints switch stack for ia64
 *	   %<regname>	 - Uses the value of the registers at the 
 *			   last time the user process entered kernel
 *			   mode, instead of the registers at the time
 *			   kdb was entered.
 *
 * Parameters:
 *	regname		Pointer to string naming register
 *	regs		Pointer to structure containing registers.
 * Outputs:
 *	*contents	Pointer to unsigned long to recieve register contents
 * Returns:
 *	0		Success
 *	KDB_BADREG	Invalid register name
 * Locking:
 * 	None.
 * Remarks:
 *
 * 	Note that this function is really machine independent.   The kdb
 *	register list is not, however.
 */

static struct kdbregs {
	char   *reg_name;
	size_t	reg_offset;
} kdbreglist[] = {
        { " psr",	offsetof(struct pt_regs, cr_ipsr) },
	{ " ifs",	offsetof(struct pt_regs, cr_ifs) }, 
        { "  ip",	offsetof(struct pt_regs, cr_iip) },

	{ "unat", 	offsetof(struct pt_regs, ar_unat) },
	{ " pfs",	offsetof(struct pt_regs, ar_pfs) },
	{ " rsc", 	offsetof(struct pt_regs, ar_rsc) },

	{ "rnat",	offsetof(struct pt_regs, ar_rnat) },
	{ "bsps",	offsetof(struct pt_regs, ar_bspstore) },
	{ "  pr",	offsetof(struct pt_regs, pr) },

	{ "ldrs",	offsetof(struct pt_regs, loadrs) },
	{ " ccv",	offsetof(struct pt_regs, ar_ccv) },
	{ "fpsr",	offsetof(struct pt_regs, ar_fpsr) },

	{ "  b0",	offsetof(struct pt_regs, b0) },
	{ "  b6",	offsetof(struct pt_regs, b6) },
	{ "  b7",	offsetof(struct pt_regs, b7) },

	{ "  r1",offsetof(struct pt_regs, r1) },
        { "  r2",offsetof(struct pt_regs, r2) },
        { "  r3",offsetof(struct pt_regs, r3) },

        { "  r8",offsetof(struct pt_regs, r8) },
        { "  r9",offsetof(struct pt_regs, r9) },
        { " r10",offsetof(struct pt_regs, r10) },

	{ " r11",offsetof(struct pt_regs, r11) },
        { " r12",offsetof(struct pt_regs, r12) },
        { " r13",offsetof(struct pt_regs, r13) },

        { " r14",offsetof(struct pt_regs, r14) },
        { " r15",offsetof(struct pt_regs, r15) },
        { " r16",offsetof(struct pt_regs, r16) },

        { " r17",offsetof(struct pt_regs, r17) },
        { " r18",offsetof(struct pt_regs, r18) },
        { " r19",offsetof(struct pt_regs, r19) },

        { " r20",offsetof(struct pt_regs, r20) },
        { " r21",offsetof(struct pt_regs, r21) },
        { " r22",offsetof(struct pt_regs, r22) },

        { " r23",offsetof(struct pt_regs, r23) },
        { " r24",offsetof(struct pt_regs, r24) },
        { " r25",offsetof(struct pt_regs, r25) },

        { " r26",offsetof(struct pt_regs, r26) },
        { " r27",offsetof(struct pt_regs, r27) },
        { " r28",offsetof(struct pt_regs, r28) },

        { " r29",offsetof(struct pt_regs, r29) },
        { " r30",offsetof(struct pt_regs, r30) },
        { " r31",offsetof(struct pt_regs, r31) },

};

static const int nkdbreglist = sizeof(kdbreglist) / sizeof(struct kdbregs);

int
kdbgetregcontents(const char *regname, 
		  struct pt_regs *regs,
		  unsigned long *contents)
{
	int i;

	if (strcmp(regname, "&regs") == 0) {
		*contents = (unsigned long)regs;
		return 0;
	}

	if (strcmp(regname, "sstk") == 0) {
		*contents = (unsigned long)getprsregs(regs) ;
		return 0;
	}

	if (strcmp(regname, "isr") == 0) {
		fault_regs_t fr ;
		get_fault_regs(&fr) ;
		*contents = fr.isr ;
		return 0 ;
	} 

#if 0
	/* XXX need to verify this */
        if (strcmp(regname, "kesp") == 0) {
                *contents = (unsigned long)regs + sizeof(struct pt_regs);
                return 0;
        }

	if (regname[0] == '%') {
		/* User registers:  %%e[a-c]x, etc */
		regname++;
		regs = (struct pt_regs *)
			(current->thread.ksp - sizeof(struct pt_regs));
	}
#endif

	for (i=0; i<nkdbreglist; i++) {
		if (strstr(kdbreglist[i].reg_name, regname))
			break;
	}

	if (i == nkdbreglist) {
		/* Lets check the rse maybe */
		if (regname[0] == 'r')
			if (show_cur_stack_frame(regs, simple_strtoul(regname+1, 0, 0) - 31, 
					contents))
				return 0 ;
		return KDB_BADREG;
	}

	*contents = *(unsigned long *)((unsigned long)regs +
			kdbreglist[i].reg_offset);

	return 0;
}

/*
 * kdbsetregcontents
 *
 *	Set the contents of the register specified by the 
 *	input string argument.   Return an error if the string
 *	does not match a machine register.
 *
 *	Supports modification of user-mode registers via
 *	%<register-name>
 *
 * Parameters:
 *	regname		Pointer to string naming register
 *	regs		Pointer to structure containing registers.
 *	contents	Unsigned long containing new register contents
 * Outputs:
 * Returns:
 *	0		Success
 *	KDB_BADREG	Invalid register name
 * Locking:
 * 	None.
 * Remarks:
 */

int
kdbsetregcontents(const char *regname, 
		  struct pt_regs *regs,
		  unsigned long contents)
{
	int i;

	if (regname[0] == '%') {
		regname++;
		regs = (struct pt_regs *)
			(current->thread.ksp - sizeof(struct pt_regs));
	}

	for (i=0; i<nkdbreglist; i++) {
		if (strnicmp(kdbreglist[i].reg_name, 
			     regname, 
			     strlen(regname)) == 0)
			break;
	}

	if ((i == nkdbreglist) 
	 || (strlen(kdbreglist[i].reg_name) != strlen(regname))) {
		return KDB_BADREG;
	}

	*(unsigned long *)((unsigned long)regs + kdbreglist[i].reg_offset) = 
		contents;

	return 0;
}

/*
 * kdbdumpregs
 *
 *	Dump the specified register set to the display.
 *
 * Parameters:
 *	regs		Pointer to structure containing registers.
 *	type		Character string identifying register set to dump
 *	extra		string further identifying register (optional)
 * Outputs:
 * Returns:
 *	0		Success
 * Locking:
 * 	None.
 * Remarks:
 *	This function will dump the general register set if the type
 *	argument is NULL (struct pt_regs).   The alternate register 
 *	set types supported by this function:
 *
 *	d 		Debug registers
 *	c		Control registers
 *	u		User registers at most recent entry to kernel
 * Following not yet implemented:
 *	m		Model Specific Registers (extra defines register #)
 *	r		Memory Type Range Registers (extra defines register)
 *
 *	For now, all registers are covered as follows:
 *
 * 	rd 		- dumps all regs
 *	rd	%isr	- current interrupt status reg, read freshly
 *	rd	s	- valid stacked regs
 * 	rd 	%sstk	- gets switch stack addr. dump memory and search
 *	rd	d	- debug regs, may not be too useful
 *
 *	ARs		TB Done
 *	Interrupt regs 	TB Done ??
 *	OTHERS		TB Decided ??
 *
 *	Intel wish list
 *	These will be implemented later - Srinivasa
 *
 *      type        action
 *      ----        ------
 *      g           dump all General static registers
 *      s           dump all general Stacked registers
 *      f           dump all Floating Point registers
 *      p           dump all Predicate registers
 *      b           dump all Branch registers
 *      a           dump all Application registers
 *      c           dump all Control registers
 *
 */

int
kdbdumpregs(struct pt_regs *regs,
	    const char *type,
	    const char *extra)

{
	int i;
	int count = 0;

	if (type 
	 && (type[0] == 'u')) {
		type = NULL;
		regs = (struct pt_regs *)
			(current->thread.ksp - sizeof(struct pt_regs));
	}

	if (type == NULL) {
		for (i=0; i<nkdbreglist; i++) {
			kdb_printf("%s: 0x%16.16lx  ", 
				   kdbreglist[i].reg_name, 
				   *(unsigned long *)((unsigned long)regs + 
						  kdbreglist[i].reg_offset));

			if ((++count % 3) == 0)
				kdb_printf("\n");
		}

		kdb_printf("&regs = 0x%16.16lx\n", regs);

		return 0;
	}

	switch (type[0]) {
	case 'd':
	{
		for(i=0; i<8; i+=2) {
			kdb_printf("idr%d: 0x%16.16lx  idr%d: 0x%16.16lx\n", i, 
					kdb_getdr(i), i+1, kdb_getdr(i+1));
					
		}
		return 0;
	}
#if 0
	case 'c':
	{
		unsigned long cr[5];

		for (i=0; i<5; i++) {
			cr[i] = kdb_getcr(i);
		}
		kdb_printf("cr0 = 0x%8.8x  cr1 = 0x%8.8x  cr2 = 0x%8.8x  cr3 = 0x%8.8x\ncr4 = 0x%8.8x\n",
			   cr[0], cr[1], cr[2], cr[3], cr[4]);
		return 0;
	}
#endif
	case 'm':
		break;
	case 'r':
		break;

	case 's':
	{
		show_cur_stack_frame(regs, 0, NULL) ;

		return 0 ;
	}

	case '%':
	{
		unsigned long contents ;

		if (!kdbgetregcontents(type+1, regs, &contents))
                	kdb_printf("%s = 0x%16.16lx\n", type+1, contents) ;
		else
                        kdb_printf("diag: Invalid register %s\n", type+1) ;

		return 0 ;
	}

	default:
		return KDB_BADREG;
	}

	/* NOTREACHED */
	return 0;
}

k_machreg_t
kdb_getpc(struct pt_regs *regs)
{
	return regs->cr_iip + ia64_psr(regs)->ri;
}

int
kdb_setpc(struct pt_regs *regs, k_machreg_t newpc)
{
	regs->cr_iip = newpc & ~0xf;
	ia64_psr(regs)->ri = newpc & 0x3;
	return 0;
}

void
kdb_disableint(kdbintstate_t *state)
{
	int *fp = (int *)state;
	int   flags;

        __save_flags(flags);
        __cli();

	*fp = flags;
}

void
kdb_restoreint(kdbintstate_t *state)
{
	int flags = *(int *)state;
	__restore_flags(flags);
}

int
kdb_putword(unsigned long addr, unsigned long contents)
{
	*(unsigned long *)addr = contents;
	return 0;
}	

int
kdb_getcurrentframe(struct pt_regs *regs)
{
#if 0
	regs->xcs = 0;
#if defined(CONFIG_KDB_FRAMEPTR)
	asm volatile("movl %%ebp,%0":"=m" (*(int *)&regs->ebp));
#endif
	asm volatile("movl %%esp,%0":"=m" (*(int *)&regs->esp));
#endif
	return 0;
}

unsigned long
show_cur_stack_frame(struct pt_regs *regs, int regno, unsigned long *contents)
{
        long sof = regs->cr_ifs & ((1<<7)-1) ;	/* size of frame */
        unsigned long   i ;
	int j;
	struct switch_stack *prs_regs = getprsregs(regs) ;
        unsigned long *sofptr = (prs_regs? ia64_rse_skip_regs(
			(unsigned long *)prs_regs->ar_bspstore, -sof) : NULL) ;

	if (!sofptr) {
		printk("Unable to display Current Stack Frame\n") ;
		return 0 ;
	}

	if (regno < 0) 
		return 0 ;

	for (i=sof, j=0;i;i--,j++) {
		/* remember to skip the nat collection dword */
		if ((((unsigned long)sofptr>>3) & (((1<<6)-1))) 
				== ((1<<6)-1))
			sofptr++ ;

		/* return the value in the reg if regno is non zero */

		if (regno) {
			if ((j+1) == regno) {
				if (contents)
					*contents = *sofptr ;
				return -1;
			}
			sofptr++ ;
		} else {
			printk(" r%d: %016lx ", 32+j, *sofptr++) ;
			if (!((j+1)%3)) printk("\n") ;
		}
	}

	if (regno) {
		if (!i) /* bogus rse number */
			return 0 ;
	} else
		printk("\n") ;

	return 0 ;
}
