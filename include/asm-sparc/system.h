#ifndef __SPARC_SYSTEM_H
#define __SPARC_SYSTEM_H

#include <asm/segment.h>

/*
 * System defines.. Note that this is included both from .c and .S
 * files, so it does only defines, not any C code.
 */

/*
 * I wish the boot time image was as beautiful as the Alpha's
 * but no such luck. The icky PROM loads us at 0x0, and jumps
 * to magic address 0x4000 to start thing going. This means that
 * I can stick the pcb and user/kernel stacks in the area from
 * 0x0-0x4000 and be reasonably sure that this is sane.
 *
 * Sorry, I can't impress people with cool looking 64-bit values
 * yet. ;-)
 */

#include <asm/openprom.h>
#include <asm/psr.h>

#define INIT_PCB	0x00011fe0
#define INIT_STACK	0x00013fe0
#define START_ADDR	0x00004000
#define START_SIZE	(32*1024)
#define EMPTY_PGT	0x00001000
#define EMPTY_PGE	0x00001000
#define ZERO_PGE	0x00001000

#define IRQ_ENA_ADR     0x2000        /* This is a bitmap of all activated IRQ's
				       * which is mapped in head.S during boot.
				       */

#ifndef __ASSEMBLY__

extern void wrent(void *, unsigned long);
extern void wrkgp(unsigned long);
extern struct linux_romvec *romvec;

#define halt() { romvec->pv_halt(); }
#define move_to_user_mode() halt()
#define switch_to(x) halt()

#ifndef stbar  /* store barrier Sparc insn to synchronize stores in PSO */
#define stbar() __asm__ __volatile__("stbar": : :"memory")
#endif

/* Changing the PIL on the sparc is a bit hairy. I'll figure out some
 * more optimized way of doing this soon. This is bletcherous code.
 */

#define swpipl(__new_ipl) \
({ unsigned long psr, retval; \
__asm__ __volatile__( \
        "rd %%psr, %0\n\t" : "=&r" (psr)); \
retval = psr; \
psr = (psr & ~(PSR_PIL)); \
psr |= ((__new_ipl << 8) & PSR_PIL); \
__asm__ __volatile__( \
	"wr  %0, 0x0, %%psr\n\t" \
	: : "r" (psr)); \
retval = ((retval>>8)&15); \
retval; })

#define cli()			swpipl(15)  /* 15 = no int's except nmi's */
#define sti()			swpipl(0)   /* I'm scared */
#define save_flags(flags)	do { flags = swpipl(15); } while (0)
#define restore_flags(flags)	swpipl(flags)

#define iret() __asm__ __volatile__ ("jmp %%l1\n\t" \
				     "rett %%l2\n\t": : :"memory")

#define _set_gate(gate_addr,type,dpl,addr) \
__asm__ __volatile__ ("nop\n\t")

#define set_intr_gate(n,addr) \
	_set_gate(&idt[n],14,0,addr)

#define set_trap_gate(n,addr) \
	_set_gate(&idt[n],15,0,addr)

#define set_system_gate(n,addr) \
	_set_gate(&idt[n],15,3,addr)

#define set_call_gate(a,addr) \
	_set_gate(a,12,3,addr)


extern inline unsigned int get_psr(void)
{
  unsigned int ret_val;
  __asm__("rd %%psr, %0\n\t" :
	  "=r" (ret_val));
  return ret_val;
}

extern inline void put_psr(unsigned int new_psr)
{
  __asm__("wr %0, 0x0, %%psr\n\t" : :
	  "r" (new_psr));
}

/* Must this be atomic? */

extern inline void *xchg_u32(int * m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
		"ld %1,%2   ! xchg_u32() is here\n\t"
		"st %0, %1\n\t"
		"or %%g0, %2, %0"
		: "=r" (val), "=m" (*m), "=r" (dummy)
		: "0" (val));
	return (void *) val;
}


/* pointers are 32 bits on the sparc (at least the v8, and they'll work
 * on the V9 none the less). I don't need the xchg_u64 routine for now.
 */

extern inline void *xchg_ptr(void *m, void *val)
{
	return (void *) xchg_u32((int *) m, (unsigned long) val);
}



#endif /* __ASSEMBLY__ */

#endif
