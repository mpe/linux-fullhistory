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

struct task_struct;	/* one of the stranger aspects of C forward declarations.. */
extern void FASTCALL(__switch_to(struct task_struct *prev, struct task_struct *next));

/*
 * We do most of the task switching in C, but we need
 * to do the EIP/ESP switch in assembly..
 */
#define switch_to(prev,next) do {					\
	unsigned long eax, edx, ecx;					\
	asm volatile("pushl %%edi\n\t"					\
		     "pushl %%esi\n\t"					\
		     "pushl %%ebp\n\t"					\
		     "pushl %%ebx\n\t"					\
		     "movl %%esp,%0\n\t"	/* save ESP */		\
		     "movl %5,%%esp\n\t"	/* restore ESP */	\
		     "movl $1f,%1\n\t"		/* save EIP */		\
		     "pushl %6\n\t"		/* restore EIP */	\
		     "jmp __switch_to\n"				\
		     "1:\t"						\
		     "popl %%ebx\n\t"					\
		     "popl %%ebp\n\t"					\
		     "popl %%esi\n\t"					\
		     "popl %%edi"					\
		     :"=m" (prev->tss.esp),"=m" (prev->tss.eip),	\
		      "=a" (eax), "=d" (edx), "=c" (ecx)		\
		     :"m" (next->tss.esp),"m" (next->tss.eip),		\
		      "a" (prev), "d" (next));				\
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

static inline unsigned long get_limit(unsigned long segment)
{
	unsigned long __limit;
	__asm__("lsll %1,%0"
		:"=r" (__limit):"r" (segment));
	return __limit+1;
}

#define nop() __asm__ __volatile__ ("nop")

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

#define _set_gate(gate_addr,type,dpl,addr) \
__asm__ __volatile__ ("movw %%dx,%%ax\n\t" \
	"movw %2,%%dx\n\t" \
	"movl %%eax,%0\n\t" \
	"movl %%edx,%1" \
	:"=m" (*((long *) (gate_addr))), \
	 "=m" (*(1+(long *) (gate_addr))) \
	:"i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
	 "d" ((char *) (addr)),"a" (__KERNEL_CS << 16) \
	:"ax","dx")

#define set_intr_gate(n,addr) \
	_set_gate(idt+(n),14,0,addr)

#define set_trap_gate(n,addr) \
	_set_gate(idt+(n),15,0,addr)

#define set_system_gate(n,addr) \
	_set_gate(idt+(n),15,3,addr)

#define set_call_gate(a,addr) \
	_set_gate(a,12,3,addr)

#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*((gate_addr)+1) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*(gate_addr) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); }

#define _set_tssldt_desc(n,addr,limit,type) \
__asm__ __volatile__ ("movw %3,0(%2)\n\t" \
	"movw %%ax,2(%2)\n\t" \
	"rorl $16,%%eax\n\t" \
	"movb %%al,4(%2)\n\t" \
	"movb %4,5(%2)\n\t" \
	"movb $0,6(%2)\n\t" \
	"movb %%ah,7(%2)\n\t" \
	"rorl $16,%%eax" \
	: "=m"(*(n)) : "a" (addr), "r"(n), "ir"(limit), "i"(type))

#define set_tss_desc(n,addr) \
	_set_tssldt_desc(((char *) (n)),((int)(addr)),235,0x89)
#define set_ldt_desc(n,addr,size) \
	_set_tssldt_desc(((char *) (n)),((int)(addr)),((size << 3) - 1),0x82)

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
