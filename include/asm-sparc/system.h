#ifndef __SPARC_SYSTEM_H
#define __SPARC_SYSTEM_H

/*
 * System defines.. Note that this is included both from .c and .S
 * files, so it does only defines, not any C code.
 */

/*
 * I wish the boot time image was as beautiful as the Alpha's
 * but no such luck. The icky PROM loads us at 0x0, and jumps
 * to magic addess 0x4000 to start thing going. This means that
 * I can stick the pcb and user/kernel stacks in the area from
 * 0x0-0x4000 and be reasonably sure that this is sane.
 *
 * Sorry, I can't impress people with cool looking 64-bit values
 * yet. ;-)
 */

#include <asm/openprom.h>

#define INIT_PCB	0x00011fe0
#define INIT_STACK	0x00013fe0
#define START_ADDR	0x00004000
#define START_SIZE	(32*1024)

#ifndef __ASSEMBLY__

extern void wrent(void *, unsigned long);
extern void wrkgp(unsigned long);
extern struct linux_romvec *romvec;

#define halt() { romvec->pv_halt(); }
#define move_to_user_mode() halt()
#define switch_to(x) halt()

#ifndef stbar  /* store barrier Sparc insn to snchronize stores in PSO */
#define stbar() __asm__ __volatile__("stbar": : :"memory")
#endif

/* Changing the PIL on the sparc is a bit hairy. I figure out some
 * more optimized way of doing this soon.
 */

#define swpipl(__new_ipl) \
({ unsigned long __old_ipl, psr; \
__asm__ __volatile__( \
	"and %1, 15, %1\n\t" \
	"sll %1, 8, %1\n\t" \
	"rd %%psr, %2\n\t" \
	"or %%g0, %2, %0\n\t" \
	"or %2, %1, %2\n\t" \
	"wr %2, 0x0, %%psr\n\t" \
	"srl %0, 8, %0\n\t" \
        "and %0, 15, %0\n\t" \
	: "=r" (__old_ipl) \
	: "r" (__new_ipl), "r" (psr=0)); \
__old_ipl; })

#define cli()			swpipl(15)  /* 15 = no int's except nmi's */
#define sti()			swpipl(0)   /* same as alpha */
#define save_flags(flags)	do { flags = swpipl(15); } while (0)
#define restore_flags(flags)	swpipl(flags)

/* Must this be atomic? */

extern inline void *xchg_u32(int * m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
		"ld %1,%2\n\t"
		"st %0, %1\n\t"
		"or %%g0, %2, %0"
		: "=r" (val), "=m" (*m), "=r" (dummy)
		: "1" (*m), "2" (val));
	return (void *)val;
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
