/* $Id: unaligned.c,v 1.1 1997/06/06 10:56:19 jj Exp $
 * unaligned.c: Unaligned load/store trap handling with special
 *              cases for the kernel to do them more quickly.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */


#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/asi.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#define DEBUG_MNA

enum direction {
	load,    /* ld, ldd, ldh, ldsh */
	store,   /* st, std, sth, stsh */
	both,    /* Swap, ldstub, cas, ... */
	fpload,
	fpstore,
	invalid,
};

#ifdef DEBUG_MNA
static char *dirstrings[] = {
  "load", "store", "both", "fpload", "fpstore", "invalid"
};
#endif

static inline enum direction decode_direction(unsigned int insn)
{
	unsigned long tmp = (insn >> 21) & 1;

	if(!tmp)
		return load;
	else {
		switch ((insn>>19)&0xf) {
		case 15: /* swap* */
			return both;
		default:
			return store;
		}
	}
}

/* 16 = double-word, 8 = extra-word, 4 = word, 2 = half-word */
static inline int decode_access_size(unsigned int insn)
{
	unsigned int tmp;

	if (((insn >> 19) & 0xf) == 14)
		return 8;	/* stx* */
	tmp = (insn >> 19) & 3;
	if(!tmp)
		return 4;
	else if(tmp == 3)
		return 16;	/* ldd/std - Although it is actually 8 */
	else if(tmp == 2)
		return 2;
	else {
		printk("Impossible unaligned trap. insn=%08x\n", insn);
		die_if_kernel("Byte sized unaligned access?!?!", current->tss.kregs);
	}
}

static inline int decode_asi(unsigned int insn, struct pt_regs *regs)
{
	if (insn & 0x800000) {
		if (insn & 0x2000)
			return (unsigned char)(regs->tstate >> 24);	/* %asi */
		else
			return (unsigned char)(insn >> 5);		/* imm_asi */
	} else
		return ASI_P;
}

/* 0x400000 = signed, 0 = unsigned */
static inline int decode_signedness(unsigned int insn)
{
	return (insn & 0x400000);
}

static inline void maybe_flush_windows(unsigned int rs1, unsigned int rs2,
				       unsigned int rd)
{
	if(rs2 >= 16 || rs1 >= 16 || rd >= 16) {
		flushw_user();
	}
}

static inline long sign_extend_imm13(long imm)
{
	return imm << 51 >> 51;
}

static inline unsigned long fetch_reg(unsigned int reg, struct pt_regs *regs)
{
	struct reg_window *win;

	if(reg < 16)
		return (!reg ? 0 : regs->u_regs[reg]);

	/* Ho hum, the slightly complicated case. */
	win = (struct reg_window *) regs->u_regs[UREG_FP];
	return win->locals[reg - 16]; /* yes, I know what this does... */
}

static inline unsigned long *fetch_reg_addr(unsigned int reg, struct pt_regs *regs)
{
	struct reg_window *win;

	if(reg < 16)
		return &regs->u_regs[reg];
	win = (struct reg_window *) regs->u_regs[UREG_FP];
	return &win->locals[reg - 16];
}

static inline unsigned long compute_effective_address(struct pt_regs *regs,
						      unsigned int insn)
{
	unsigned int rs1 = (insn >> 14) & 0x1f;
	unsigned int rs2 = insn & 0x1f;
	unsigned int rd = (insn >> 25) & 0x1f;

	if(insn & 0x2000) {
		maybe_flush_windows(rs1, 0, rd);
		return (fetch_reg(rs1, regs) + sign_extend_imm13(insn));
	} else {
		maybe_flush_windows(rs1, rs2, rd);
		return (fetch_reg(rs1, regs) + fetch_reg(rs2, regs));
	}
}

/* This is just to make gcc think panic does return... */
static void unaligned_panic(char *str)
{
	panic(str);
}

#define do_integer_load(dest_reg, size, saddr, is_signed, asi, errh) ({		\
__asm__ __volatile__ (								\
	"wr	%4, 0, %%asi\n\t"						\
	"cmp	%1, 8\n\t"							\
	"bge,pn	%%icc, 9f\n\t"							\
	" cmp	%1, 4\n\t"							\
	"be,pt	%%icc, 6f\n"							\
"4:\t"	" lduba	[%2] %%asi, %%l1\n"						\
"5:\t"	"lduba	[%2 + 1] %%asi, %%l2\n\t"					\
	"sll	%%l1, 8, %%l1\n\t"						\
	"brz,pt	%3, 3f\n\t"							\
	" add	%%l1, %%l2, %%l1\n\t"						\
	"sllx	%%l1, 48, %%l1\n\t"						\
	"srax	%%l1, 48, %%l1\n"						\
"3:\t"	"ba,pt	%%xcc, 0f\n\t"							\
	" stx	%%l1, [%0]\n"							\
"6:\t"	"lduba	[%2 + 1] %%asi, %%l2\n\t"					\
	"sll	%%l1, 24, %%l1\n"						\
"7:\t"	"lduba	[%2 + 2] %%asi, %%g7\n\t"					\
	"sll	%%l2, 16, %%l2\n"						\
"8:\t"	"lduba	[%2 + 3] %%asi, %%g1\n\t"					\
	"sll	%%g7, 8, %%g7\n\t"						\
	"or	%%l1, %%l2, %%l1\n\t"						\
	"or	%%g7, %%g1, %%g7\n\t"						\
	"or	%%l1, %%g7, %%l1\n\t"						\
	"brnz,a,pt %3, 3f\n\t"							\
	" sra	%%l1, 0, %%l1\n"						\
"3:\t"	"ba,pt	%%xcc, 0f\n\t"							\
	" stx	%%l1, [%0]\n"							\
"9:\t"	"lduba	[%2] %%asi, %%l1\n"						\
"10:\t"	"lduba	[%2 + 1] %%asi, %%l2\n\t"					\
	"sllx	%%l1, 56, %%l1\n"						\
"11:\t"	"lduba	[%2 + 2] %%asi, %%g7\n\t"					\
	"sllx	%%l2, 48, %%l2\n"						\
"12:\t"	"lduba	[%2 + 3] %%asi, %%g1\n\t"					\
	"sllx	%%g7, 40, %%g7\n\t"						\
	"sllx	%%g1, 32, %%g1\n\t"						\
	"or	%%l1, %%l2, %%l1\n\t"						\
	"or	%%g7, %%g1, %%g7\n"						\
"13:\t"	"lduba	[%2 + 4] %%asi, %%l2\n\t"					\
	"or	%%l1, %%g7, %%g7\n"						\
"14:\t"	"lduba	[%2 + 5] %%asi, %%g1\n\t"					\
	"sllx	%%l2, 24, %%l2\n"						\
"15:\t"	"lduba	[%2 + 6] %%asi, %%l1\n\t"					\
	"sllx	%%g1, 16, %%g1\n\t"						\
	"or	%%g7, %%l2, %%g7\n"						\
"16:\t"	"lduba	[%2 + 7] %%asi, %%l2\n\t"					\
	"sllx	%%l1, 8, %%l1\n\t"						\
	"or	%%g7, %%g1, %%g7\n\t"						\
	"or	%%l1, %%l2, %%l1\n\t"						\
	"or	%%g7, %%l1, %%g7\n\t"						\
	"cmp	%1, 8\n\t"							\
	"be,a,pt %%icc, 0f\n\t"							\
	" stx	%%g7, [%0]\n\t"							\
	"srlx	%%g7, 32, %%l1\n\t"						\
	"sra	%%g7, 0, %%g7\n\t"						\
	"stx	%%l1, [%0]\n\t"							\
	"stx	%%g7, [%0 + 8]\n"						\
"0:\n\n\t"									\
	".section __ex_table\n\t"						\
	".word	4b, " #errh "\n\t"						\
	".word	5b, " #errh "\n\t"						\
	".word	6b, " #errh "\n\t"						\
	".word	7b, " #errh "\n\t"						\
	".word	8b, " #errh "\n\t"						\
	".word	9b, " #errh "\n\t"						\
	".word	10b, " #errh "\n\t"						\
	".word	11b, " #errh "\n\t"						\
	".word	12b, " #errh "\n\t"						\
	".word	13b, " #errh "\n\t"						\
	".word	14b, " #errh "\n\t"						\
	".word	15b, " #errh "\n\t"						\
	".word	16b, " #errh "\n\n\t"						\
	".previous\n\t"								\
	: : "r" (dest_reg), "r" (size), "r" (saddr), "r" (is_signed), "r" (asi)	\
	: "l1", "l2", "g7", "g1", "cc");					\
})
	
#define store_common(dst_addr, size, src_val, asi, errh) ({			\
__asm__ __volatile__ (								\
	"wr	%3, 0, %%asi\n\t"						\
	"ldx	[%2], %%l1\n"							\
	"cmp	%1, 2\n\t"							\
	"be,pn	%%icc, 2f\n\t"							\
	" cmp	%1, 4\n\t"							\
	"be,pt	%%icc, 1f\n\t"							\
	" srlx	%%l1, 24, %%l2\n\t"						\
	"srlx	%%l1, 56, %%g1\n\t"						\
	"srlx	%%l1, 48, %%g7\n"						\
"4:\t"	"stba	%%g1, [%0] %%asi\n\t"						\
	"srlx	%%l1, 40, %%g1\n"						\
"5:\t"	"stba	%%g7, [%0 + 1] %%asi\n\t"					\
	"srlx	%%l1, 32, %%g7\n"						\
"6:\t"	"stba	%%g1, [%0 + 2] %%asi\n"						\
"7:\t"	"stba	%%g7, [%0 + 3] %%asi\n\t"					\
	"srlx	%%l1, 16, %%g1\n"						\
"8:\t"	"stba	%%l2, [%0 + 4] %%asi\n\t"					\
	"srlx	%%l1, 8, %%g7\n"						\
"9:\t"	"stba	%%g1, [%0 + 5] %%asi\n"						\
"10:\t"	"stba	%%g7, [%0 + 6] %%asi\n\t"					\
	"ba,pt	%%xcc, 0f\n"							\
"11:\t"	" stba	%%l1, [%0 + 7] %%asi\n"						\
"1:\t"	"srl	%%l1, 16, %%g7\n"						\
"12:\t"	"stba	%%l2, [%0] %%asi\n\t"						\
	"srl	%%l1, 8, %%l2\n"						\
"13:\t"	"stba	%%g7, [%0 + 1] %%asi\n"						\
"14:\t"	"stba	%%l2, [%0 + 2] %%asi\n\t"					\
	"ba,pt	%%xcc, 0f\n"							\
"15:\t"	" stba	%%l1, [%0 + 3] %%asi\n"						\
"2:\t"	"srl	%%l1, 8, %%l2\n"						\
"16:\t"	"stba	%%l2, [%0] %%asi\n"						\
"17:\t"	"stba	%%l1, [%0 + 1] %%asi\n"						\
"0:\n\n\t"									\
	".section __ex_table\n\t"						\
	".word	4b, " #errh "\n\t"						\
	".word	5b, " #errh "\n\t"						\
	".word	6b, " #errh "\n\t"						\
	".word	7b, " #errh "\n\t"						\
	".word	8b, " #errh "\n\t"						\
	".word	9b, " #errh "\n\t"						\
	".word	10b, " #errh "\n\t"						\
	".word	11b, " #errh "\n\t"						\
	".word	12b, " #errh "\n\t"						\
	".word	13b, " #errh "\n\t"						\
	".word	14b, " #errh "\n\t"						\
	".word	15b, " #errh "\n\t"						\
	".word	16b, " #errh "\n\t"						\
	".word	17b, " #errh "\n\n\t"						\
	".previous\n\t"								\
	: : "r" (dst_addr), "r" (size), "r" (src_val), "r" (asi)		\
	: "l1", "l2", "g7", "g1", "cc");					\
})

#define do_integer_store(reg_num, size, dst_addr, regs, asi, errh) ({		\
	unsigned long zero = 0;							\
	unsigned long *src_val = &zero;						\
										\
	if (size == 16) {							\
		size = 8;							\
		zero = (((long)(reg_num ? 					\
		        (unsigned)fetch_reg(reg_num, regs) : 0)) << 32) |	\
			(unsigned)fetch_reg(reg_num + 1, regs);			\
	} else if (reg_num) src_val = fetch_reg_addr(reg_num, regs);		\
	store_common(dst_addr, size, src_val, asi, errh);			\
})

/* XXX Need to capture/release other cpu's for SMP around this. */
#define do_atomic(srcdest_reg, mem, errh) ({					\
	unsigned long flags, tmp;						\
										\
	save_and_cli(flags);							\
	tmp = *srcdest_reg;							\
	do_integer_load(srcdest_reg, 4, mem, 0, errh);				\
	store_common(mem, 4, &tmp, errh);					\
	restore_flags(flags);							\
})

static inline void advance(struct pt_regs *regs)
{
	regs->tpc   = regs->tnpc;
	regs->tnpc += 4;
}

static inline int floating_point_load_or_store_p(unsigned int insn)
{
	return (insn >> 24) & 1;
}

static inline int ok_for_kernel(unsigned int insn)
{
	return !floating_point_load_or_store_p(insn);
}

void kernel_mna_trap_fault(struct pt_regs *regs, unsigned int insn) __asm__ ("kernel_mna_trap_fault");

void kernel_mna_trap_fault(struct pt_regs *regs, unsigned int insn)
{
	unsigned long g2 = regs->u_regs [UREG_G2];
	unsigned long fixup = search_exception_table (regs->tpc, &g2);

	if (!fixup) {
		unsigned long address = compute_effective_address(regs, insn);
        	if(address < PAGE_SIZE) {
                	printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference in mna handler");
        	} else
                	printk(KERN_ALERT "Unable to handle kernel paging request in mna handler");
	        printk(KERN_ALERT " at virtual address %016lx\n",address);
        	printk(KERN_ALERT "current->mm->context = %016lx\n",
	               (unsigned long) current->mm->context);
	        printk(KERN_ALERT "current->mm->pgd = %016lx\n",
        	       (unsigned long) current->mm->pgd);
	        die_if_kernel("Oops", regs);
		/* Not reached */
	}
	regs->tpc = fixup;
	regs->tnpc = regs->tpc + 4;
	regs->u_regs [UREG_G2] = g2;
}

asmlinkage void kernel_unaligned_trap(struct pt_regs *regs, unsigned int insn)
{
	enum direction dir = decode_direction(insn);
	int size = decode_access_size(insn);

	lock_kernel();
	if(!ok_for_kernel(insn) || dir == both) {
		printk("Unsupported unaligned load/store trap for kernel at <%016lx>.\n",
		       regs->tpc);
		unaligned_panic("Wheee. Kernel does fpu/atomic unaligned load/store.");

		__asm__ __volatile__ ("\n"
"kernel_unaligned_trap_fault:\n\t"
		"mov	%0, %%o0\n\t"
		"call	kernel_mna_trap_fault\n\t"
		" mov	%1, %%o1\n\t"
		:
		: "r" (regs), "r" (insn)
		: "o0", "o1", "o2", "o3", "o4", "o5", "o7",
		  "g1", "g2", "g3", "g4", "g5", "g7", "cc");
	} else {
		unsigned long addr = compute_effective_address(regs, insn);

#ifdef DEBUG_MNA
		printk("KMNA: pc=%016lx [dir=%s addr=%016lx size=%d] retpc[%016lx]\n",
		       regs->tpc, dirstrings[dir], addr, size, regs->u_regs[UREG_RETPC]);
#endif
		switch(dir) {
		case load:
			do_integer_load(fetch_reg_addr(((insn>>25)&0x1f), regs),
					size, (unsigned long *) addr,
					decode_signedness(insn), decode_asi(insn, regs),
					kernel_unaligned_trap_fault);
			break;

		case store:
			do_integer_store(((insn>>25)&0x1f), size,
					 (unsigned long *) addr, regs,
					 decode_asi(insn, regs),
					 kernel_unaligned_trap_fault);
			break;
#if 0 /* unsupported */
		case both:
			do_atomic(fetch_reg_addr(((insn>>25)&0x1f), regs),
				  (unsigned long *) addr,
				  kernel_unaligned_trap_fault);
			break;
#endif
		default:
			panic("Impossible kernel unaligned trap.");
			/* Not reached... */
		}
		advance(regs);
	}
	unlock_kernel();
}

#if 0 /* XXX: Implement user mna some day */
static inline int ok_for_user(struct pt_regs *regs, unsigned int insn,
			      enum direction dir)
{
	unsigned int reg;
	int retval, check = (dir == load) ? VERIFY_READ : VERIFY_WRITE;
	int size = ((insn >> 19) & 3) == 3 ? 8 : 4;

	if((regs->pc | regs->npc) & 3)
		return 0;

	/* Must verify_area() in all the necessary places. */
#define WINREG_ADDR(regnum) ((void *)(((unsigned long *)regs->u_regs[UREG_FP])+(regnum)))
	retval = 0;
	reg = (insn >> 25) & 0x1f;
	if(reg >= 16) {
		retval = verify_area(check, WINREG_ADDR(reg - 16), size);
		if(retval)
			return retval;
	}
	reg = (insn >> 14) & 0x1f;
	if(reg >= 16) {
		retval = verify_area(check, WINREG_ADDR(reg - 16), size);
		if(retval)
			return retval;
	}
	if(!(insn & 0x2000)) {
		reg = (insn & 0x1f);
		if(reg >= 16) {
			retval = verify_area(check, WINREG_ADDR(reg - 16), size);
			if(retval)
				return retval;
		}
	}
	return retval;
#undef WINREG_ADDR
}

void user_mna_trap_fault(struct pt_regs *regs, unsigned int insn) __asm__ ("user_mna_trap_fault");

void user_mna_trap_fault(struct pt_regs *regs, unsigned int insn)
{
	current->tss.sig_address = regs->pc;
	current->tss.sig_desc = SUBSIG_PRIVINST;
	send_sig(SIGBUS, current, 1);
}

asmlinkage void user_unaligned_trap(struct pt_regs *regs, unsigned int insn)
{
	enum direction dir;

	lock_kernel();
	if(!(current->tss.flags & SPARC_FLAG_UNALIGNED) ||
	   (((insn >> 30) & 3) != 3))
		goto kill_user;
	dir = decode_direction(insn);
	if(!ok_for_user(regs, insn, dir)) {
		goto kill_user;
	} else {
		int size = decode_access_size(insn);
		unsigned long addr;

		if(floating_point_load_or_store_p(insn)) {
			printk("User FPU load/store unaligned unsupported.\n");
			goto kill_user;
		}

		addr = compute_effective_address(regs, insn);
		switch(dir) {
		case load:
			do_integer_load(fetch_reg_addr(((insn>>25)&0x1f), regs),
					size, (unsigned long *) addr,
					decode_signedness(insn),
					user_unaligned_trap_fault);
			break;

		case store:
			do_integer_store(((insn>>25)&0x1f), size,
					 (unsigned long *) addr, regs,
					 user_unaligned_trap_fault);
			break;

		case both:
			do_atomic(fetch_reg_addr(((insn>>25)&0x1f), regs),
				  (unsigned long *) addr,
				  user_unaligned_trap_fault);
			break;

		default:
			unaligned_panic("Impossible user unaligned trap.");

			__asm__ __volatile__ ("\n"
"user_unaligned_trap_fault:\n\t"
			"mov	%0, %%o0\n\t"
			"call	user_mna_trap_fault\n\t"
			" mov	%1, %%o1\n\t"
			:
			: "r" (regs), "r" (insn)
			: "o0", "o1", "o2", "o3", "o4", "o5", "o7",
			  "g1", "g2", "g3", "g4", "g5", "g7", "cc");
			goto out;
		}
		advance(regs);
		goto out;
	}

kill_user:
	current->tss.sig_address = regs->pc;
	current->tss.sig_desc = SUBSIG_PRIVINST;
	send_sig(SIGBUS, current, 1);
out:
	unlock_kernel();
}
#endif
