/* $Id: unaligned.c,v 1.10 1996/11/10 21:25:47 davem Exp $
 * unaligned.c: Unaligned load/store trap handling with special
 *              cases for the kernel to do them more quickly.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>

/* #define DEBUG_MNA */

extern void die_if_kernel(char *, struct pt_regs *);

enum direction {
	load,    /* ld, ldd, ldh, ldsh */
	store,   /* st, std, sth, stsh */
	both,    /* Swap, ldstub, etc. */
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
		if(((insn>>19)&0x3f) == 15)
			return both;
		else
			return store;
	}
}

/* 8 = double-word, 4 = word, 2 = half-word */
static inline int decode_access_size(unsigned int insn)
{
	insn = (insn >> 19) & 3;

	if(!insn)
		return 4;
	else if(insn == 3)
		return 8;
	else if(insn == 2)
		return 2;
	else {
		printk("Impossible unaligned trap. insn=%08x\n", insn);
		die_if_kernel("Byte sized unaligned access?!?!", current->tss.kregs);
		return 4; /* just to keep gcc happy. */
	}
}

/* 1 = signed, 0 = unsigned */
static inline int decode_signedness(unsigned int insn)
{
	return (insn >> 22) & 1;
}

static inline void maybe_flush_windows(unsigned int rs1, unsigned int rs2,
				       unsigned int rd)
{
	int yep;

	if(rs2 >= 16 || rs1 >= 16 || rd >= 16)
		yep = 1;
	else
		yep = 0;
	if(yep) {
		/* Wheee... */
		__asm__ __volatile__("save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "restore; restore; restore; restore;\n\t"
				     "restore; restore; restore;\n\t");
	}
}

static inline int sign_extend_halfword(int hword)
{
	return hword << 16 >> 16;
}

static inline int sign_extend_imm13(int imm)
{
	return imm << 19 >> 19;
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
	unsigned int imm13 = (insn & 0x1fff);

	if(insn & 0x2000) {
		maybe_flush_windows(rs1, 0, rd);
		return (fetch_reg(rs1, regs) + sign_extend_imm13(imm13));
	} else {
		maybe_flush_windows(rs1, rs2, rd);
		return (fetch_reg(rs1, regs) + fetch_reg(rs2, regs));
	}
}

static inline void do_integer_load(unsigned long *dest_reg, int size,
				   unsigned long *saddr, int is_signed)
{
	unsigned char bytes[4];

	switch(size) {
	case 2:
		bytes[0] = *((unsigned char *)saddr + 1);
		bytes[1] = *((unsigned char *)saddr + 0);
		*dest_reg = (bytes[0] | (bytes[1] << 8));
		if(is_signed)
			*dest_reg = sign_extend_halfword(*dest_reg);
		break;

	case 4:
		bytes[0] = *((unsigned char *)saddr + 3);
		bytes[1] = *((unsigned char *)saddr + 2);
		bytes[2] = *((unsigned char *)saddr + 1);
		bytes[3] = *((unsigned char *)saddr + 0);
		*dest_reg = (bytes[0] | (bytes[1] << 8) |
			     (bytes[2] << 16) | (bytes[3] << 24));
		break;

	case 8:
		bytes[0] = *((unsigned char *)saddr + 3);
		bytes[1] = *((unsigned char *)saddr + 2);
		bytes[2] = *((unsigned char *)saddr + 1);
		bytes[3] = *((unsigned char *)saddr + 0);
		*dest_reg++ = (bytes[0] | (bytes[1] << 8) |
			     (bytes[2] << 16) | (bytes[3] << 24));
		saddr++;
		bytes[0] = *((unsigned char *)saddr + 3);
		bytes[1] = *((unsigned char *)saddr + 2);
		bytes[2] = *((unsigned char *)saddr + 1);
		bytes[3] = *((unsigned char *)saddr + 0);
		*dest_reg = (bytes[0] | (bytes[1] << 8) |
			     (bytes[2] << 16) | (bytes[3] << 24));
		break;

	default:
		panic("Impossible unaligned load.");
	};
}

static inline void store_common(unsigned long *src_val,
				int size, unsigned long *dst_addr)
{
	unsigned char *daddr = (unsigned char *) dst_addr;
	switch(size) {
	case 2:
		daddr[0] = ((*src_val) >> 8) & 0xff;
		daddr[1] = (*src_val & 0xff);
		break;

	case 4:
		daddr[0] = ((*src_val) >> 24) & 0xff;
		daddr[1] = ((*src_val) >> 16) & 0xff;
		daddr[2] = ((*src_val) >> 8) & 0xff;
		daddr[3] = (*src_val & 0xff);
		break;

	case 8:
		daddr[0] = ((*src_val) >> 24) & 0xff;
		daddr[1] = ((*src_val) >> 16) & 0xff;
		daddr[2] = ((*src_val) >> 8) & 0xff;
		daddr[3] = (*src_val & 0xff);
		daddr += 4;
		src_val++;
		daddr[0] = ((*src_val) >> 24) & 0xff;
		daddr[1] = ((*src_val) >> 16) & 0xff;
		daddr[2] = ((*src_val) >> 8) & 0xff;
		daddr[3] = (*src_val & 0xff);
		break;

	default:
		panic("Impossible unaligned store.");
	}
}

static inline void do_integer_store(int reg_num, int size,
				    unsigned long *dst_addr,
				    struct pt_regs *regs)
{
	unsigned long *src_val;
	static unsigned long zero[2] = { 0, 0 };

	if(reg_num)
		src_val = fetch_reg_addr(reg_num, regs);
	else
		src_val = &zero[0];
	store_common(src_val, size, dst_addr);
}

static inline void do_atomic(unsigned long *srcdest_reg, unsigned long *mem)
{
	unsigned long flags, tmp;

#ifdef __SMP__
	/* XXX Need to capture/release other cpu's around this. */
#endif
	save_and_cli(flags);
	tmp = *srcdest_reg;
	do_integer_load(srcdest_reg, 4, mem, 0);
	store_common(&tmp, 4, mem);
	restore_flags(flags);
}

static inline void advance(struct pt_regs *regs)
{
	regs->pc   = regs->npc;
	regs->npc += 4;
}

static inline int floating_point_load_or_store_p(unsigned int insn)
{
	return (insn >> 24) & 1;
}

static inline int ok_for_kernel(unsigned int insn)
{
	return !floating_point_load_or_store_p(insn);
}

asmlinkage void kernel_unaligned_trap(struct pt_regs *regs, unsigned int insn)
{
	enum direction dir = decode_direction(insn);
	int size = decode_access_size(insn);

	if(!ok_for_kernel(insn) || dir == both) {
		printk("Unsupported unaligned load/store trap for kernel at <%08lx>.\n",
		       regs->pc);
		panic("Wheee. Kernel does fpu/atomic unaligned load/store.");
		/* Not reached... */
	} else {
		unsigned long addr = compute_effective_address(regs, insn);

#ifdef DEBUG_MNA
		printk("KMNA: pc=%08lx [dir=%s addr=%08lx size=%d] retpc[%08lx]\n",
		       regs->pc, dirstrings[dir], addr, size, regs->u_regs[UREG_RETPC]);
#endif
		switch(dir) {
		case load:
			do_integer_load(fetch_reg_addr(((insn>>25)&0x1f), regs),
					size, (unsigned long *) addr,
					decode_signedness(insn));
			break;

		case store:
			do_integer_store(((insn>>25)&0x1f), size,
					 (unsigned long *) addr, regs);
			break;
		case both:
#if 0 /* unsupported */
			do_atomic(fetch_reg_addr(((insn>>25)&0x1f), regs),
				  (unsigned long *) addr);
			break;
#endif
		default:
			panic("Impossible kernel unaligned trap.");
			/* Not reached... */
		}
		advance(regs);
	}
}

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

asmlinkage void user_unaligned_trap(struct pt_regs *regs, unsigned int insn)
{
	enum direction dir;

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
					decode_signedness(insn));
			break;

		case store:
			do_integer_store(((insn>>25)&0x1f), size,
					 (unsigned long *) addr, regs);
			break;

		case both:
			do_atomic(fetch_reg_addr(((insn>>25)&0x1f), regs),
				  (unsigned long *) addr);
			break;

		default:
			panic("Impossible user unaligned trap.");
		}
		advance(regs);
		return;
	}

kill_user:
	current->tss.sig_address = regs->pc;
	current->tss.sig_desc = SUBSIG_PRIVINST;
	send_sig(SIGBUS, current, 1);
}
