#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <linux/kernel.h>
#include <asm/segment.h>

/*
 * Entry into gdt where to find first TSS. GDT layout:
 *   0 - null
 *   1 - not used
 *   2 - kernel code segment
 *   3 - kernel data segment
 *   4 - user code segment
 *   5 - user data segment
 *   6 - not used
 *   7 - not used
 *   8 - APM BIOS support
 *   9 - APM BIOS support
 *  10 - APM BIOS support
 *  11 - APM BIOS support
 *  12 - TSS #0
 *  13 - LDT #0
 *  14 - TSS #1
 *  15 - LDT #1
 */
#define FIRST_TSS_ENTRY 12
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3))
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3))
#define load_TR(n) __asm__ __volatile__("ltr %%ax": /* no output */ :"a" (_TSS(n)))
#define load_ldt(n) __asm__ __volatile__("lldt %%ax": /* no output */ :"a" (_LDT(n)))
#define store_TR(n) \
__asm__("str %%ax\n\t" \
	"subl %2,%%eax\n\t" \
	"shrl $4,%%eax" \
	:"=a" (n) \
	:"0" (0),"i" (FIRST_TSS_ENTRY<<3))

#ifdef __KERNEL__

struct task_struct;	/* one of the stranger aspects of C forward declarations.. */
extern void FASTCALL(__switch_to(struct task_struct *prev, struct task_struct *next));

/*
 * We do most of the task switching in C, but we need
 * to do the EIP/ESP switch in assembly..
 */
#define switch_to(prev,next) do {					\
	unsigned long eax, edx, ecx;					\
	asm volatile("pushl %%ebx\n\t"					\
		     "pushl %%esi\n\t"					\
		     "pushl %%edi\n\t"					\
		     "pushl %%ebp\n\t"					\
		     "movl %%esp,%0\n\t"	/* save ESP */		\
		     "movl %5,%%esp\n\t"	/* restore ESP */	\
		     "movl $1f,%1\n\t"		/* save EIP */		\
		     "pushl %6\n\t"		/* restore EIP */	\
		     "jmp __switch_to\n"				\
		     "1:\t"						\
		     "popl %%ebp\n\t"					\
		     "popl %%edi\n\t"					\
		     "popl %%esi\n\t"					\
		     "popl %%ebx"					\
		     :"=m" (prev->tss.esp),"=m" (prev->tss.eip),	\
		      "=a" (eax), "=d" (edx), "=c" (ecx)		\
		     :"m" (next->tss.esp),"m" (next->tss.eip),		\
		      "a" (prev), "d" (next)); 				\
} while (0)

#define _set_base(addr,base) \
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%1\n\t" \
	"movb %%dh,%2" \
	: /* no output */ \
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7)), \
	 "d" (base) \
	:"dx")

#define _set_limit(addr,limit) \
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %1,%%dh\n\t" \
	"andb $0xf0,%%dh\n\t" \
	"orb %%dh,%%dl\n\t" \
	"movb %%dl,%1" \
	: /* no output */ \
	:"m" (*(addr)), \
	 "m" (*((addr)+6)), \
	 "d" (limit) \
	:"dx")

#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , (base) )
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , ((limit)-1)>>12 )

static inline unsigned long _get_base(char * addr)
{
	unsigned long __base;
	__asm__("movb %3,%%dh\n\t"
		"movb %2,%%dl\n\t"
		"shll $16,%%edx\n\t"
		"movw %1,%%dx"
		:"=&d" (__base)
		:"m" (*((addr)+2)),
		 "m" (*((addr)+4)),
		 "m" (*((addr)+7)));
	return __base;
}

#define get_base(ldt) _get_base( ((char *)&(ldt)) )

/*
 * Load a segment. Fall back on loading the zero
 * segment if something goes wrong..
 */
#define loadsegment(seg,value)			\
	asm volatile("\n"			\
		"1:\t"				\
		"movl %0,%%" #seg "\n"		\
		"2:\n"				\
		".section fixup,\"ax\"\n"	\
		"3:\t"				\
		"pushl $0\n\t"			\
		"popl %%" #seg "\n\t"		\
		"jmp 2b\n"			\
		".previous\n"			\
		".section __ex_table,\"a\"\n\t"	\
		".align 4\n\t"			\
		".long 1b,3b\n"			\
		".previous"			\
		: :"m" (*(unsigned int *)&(value)))

/*
 * Clear and set 'TS' bit respectively
 */
#define clts() __asm__ __volatile__ ("clts")
#define stts() \
__asm__ __volatile__ ( \
	"movl %%cr0,%%eax\n\t" \
	"orl $8,%%eax\n\t" \
	"movl %%eax,%%cr0" \
	: /* no outputs */ \
	: /* no inputs */ \
	:"ax")

#endif	/* __KERNEL__ */

static inline unsigned long get_limit(unsigned long segment)
{
	unsigned long __limit;
	__asm__("lsll %1,%0"
		:"=r" (__limit):"r" (segment));
	return __limit+1;
}

#define nop() __asm__ __volatile__ ("nop")

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((struct __xchg_dummy *)(x))

/*
 * Note: no "lock" prefix even on SMP: xchg always implies lock anyway
 */
static inline unsigned long __xchg(unsigned long x, void * ptr, int size)
{
	switch (size) {
		case 1:
			__asm__("xchgb %b0,%1"
				:"=q" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
		case 2:
			__asm__("xchgw %w0,%1"
				:"=r" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
		case 4:
			__asm__("xchgl %0,%1"
				:"=r" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
	}
	return x;
}

/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 */
#define mb() 	__asm__ __volatile__ ("lock; addl $0,0(%%esp)": : :"memory")

/* interrupt control.. */
#define __sti() __asm__ __volatile__ ("sti": : :"memory")
#define __cli() __asm__ __volatile__ ("cli": : :"memory")
#define __save_flags(x) \
__asm__ __volatile__("pushfl ; popl %0":"=g" (x): /* no input */ :"memory")
#define __restore_flags(x) \
__asm__ __volatile__("pushl %0 ; popfl": /* no output */ :"g" (x):"memory")


#ifdef __SMP__

extern void __global_cli(void);
extern void __global_sti(void);
extern unsigned long __global_save_flags(void);
extern void __global_restore_flags(unsigned long);
#define cli() __global_cli()
#define sti() __global_sti()
#define save_flags(x) ((x)=__global_save_flags())
#define restore_flags(x) __global_restore_flags(x)

#else

#define cli() __cli()
#define sti() __sti()
#define save_flags(x) __save_flags(x)
#define restore_flags(x) __restore_flags(x)

#endif

extern void set_intr_gate(unsigned int irq, void * addr);
void set_ldt_desc(void *n, void *addr, unsigned int size);
void set_tss_desc(void *n, void *addr);

/*
 * This is the ldt that every process will get unless we need
 * something other than this.
 */
extern struct desc_struct default_ldt;

/*
 * disable hlt during certain critical i/o operations
 */
#define HAVE_DISABLE_HLT
void disable_hlt(void);
void enable_hlt(void);

#endif
