#ifndef __SPARC_SYSTEM_H
#define __SPARC_SYSTEM_H

#include <asm/segment.h>

/*
 * I wish the boot time image was as beautiful as the Alpha's
 * but no such luck. The icky PROM loads us at 0x0, and jumps
 * to magic address 0x4000 to start things going.
 *
 * Sorry, I can't impress people with cool looking 64-bit values
 * yet. Wait till V9 ;-)
 */

#include <asm/page.h>
#include <asm/openprom.h>
#include <asm/psr.h>

#define START_ADDR	(0x00004000)
#define EMPTY_PGT       (&empty_bad_page)
#define EMPTY_PGE	(&empty_bad_page_table)
#define ZERO_PGE	(&empty_zero_page)

#ifndef __ASSEMBLY__

/*
 * Sparc (general) CPU types
 */
enum sparc_cpu {
  sun4        = 0x00,
  sun4c       = 0x01,
  sun4m       = 0x02,
  sun4d       = 0x03,
  sun4e       = 0x04,
  sun4u       = 0x05,
  sun_unknown = 0x06,
};

extern enum sparc_cpu sparc_cpu_model;

extern unsigned long empty_bad_page;
extern unsigned long empty_bad_page_table;
extern unsigned long empty_zero_page;

extern void wrent(void *, unsigned long);
extern void wrkgp(unsigned long);
extern struct linux_romvec *romvec;

#define halt() do { \
			 printk("Entering monitor in file %s at line %d\n", __FILE__, __LINE__); \
romvec->pv_halt(); } while(0)

#define move_to_user_mode() halt()

#ifndef stbar  /* store barrier Sparc insn to synchronize stores in PSO */
#define stbar() __asm__ __volatile__("stbar": : :"memory")
#endif

/* When a context switch happens we must flush all user windows so that
 * the windows of the current process are flushed onto it's stack. This
 * way the windows are all clean for the next process.
 */

#define flush_user_windows() \
do { __asm__ __volatile__( \
			  "save %sp, -64, %sp\n\t" \
			  "save %sp, -64, %sp\n\t" \
			  "save %sp, -64, %sp\n\t" \
			  "save %sp, -64, %sp\n\t" \
			  "save %sp, -64, %sp\n\t" \
			  "save %sp, -64, %sp\n\t" \
			  "save %sp, -64, %sp\n\t" \
			  "restore\n\t" \
			  "restore\n\t" \
			  "restore\n\t" \
			  "restore\n\t" \
			  "restore\n\t" \
			  "restore\n\t" \
			  "restore\n\t"); } while(0)

extern void sparc_switch_to(void *new_task);

#define switch_to(p) sparc_switch_to(p)

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

/* Must this be atomic? */

extern inline void *xchg_u32(int * m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
		"ld %1,%2\n\t"
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
