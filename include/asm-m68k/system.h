#ifndef _M68K_SYSTEM_H
#define _M68K_SYSTEM_H

#include <linux/config.h> /* get configuration macros */
#include <linux/linkage.h>
#include <asm/segment.h>

extern inline unsigned long rdusp(void) {
  	unsigned long usp;

	__asm__ __volatile__("movec %/usp,%0"
			     : "=d" (usp));
	return usp;
}

extern inline void wrusp(unsigned long usp) {
	__asm__ __volatile__("movec %0,%/usp"
			     :
			     : "d" (usp));
}

/*
 * switch_to(n) should switch tasks to task ptr, first checking that
 * ptr isn't the current task, in which case it does nothing.  This
 * also clears the TS-flag if the task we switched to has used the
 * math co-processor latest.
 */
/*
 * switch_to() saves the extra registers, that are not saved
 * automatically by SAVE_SWITCH_STACK in resume(), ie. d0-d5 and
 * a0-a1. Some of these are used by schedule() and its predecessors
 * and so we might get see unexpected behaviors when a task returns
 * with unexpected register values.
 *
 * syscall stores these registers itself and none of them are used
 * by syscall after the function in the syscall has been called.
 *
 * Beware that resume now expects *next to be in d1 and the offset of
 * tss to be in a1. This saves a few instructions as we no longer have
 * to push them onto the stack and read them back right after.
 *
 * 02/17/96 - Jes Sorensen (jds@kom.auc.dk)
 */
asmlinkage void resume(void);
#define switch_to(prev,next) { \
  register int k __asm__ ("a1") = (int)&((struct task_struct *)0)->tss; \
  register int n __asm__ ("d1") = (int)next; \
  __asm__ __volatile__("jbsr " SYMBOL_NAME_STR(resume) "\n\t" \
		       : : "a" (k), "d" (n) \
		       : "d0", "d1", "d2", "d3", "d4", "d5", "a0", "a1"); \
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((volatile struct __xchg_dummy *)(x))

#if defined(CONFIG_ATARI) && !defined(CONFIG_AMIGA) && !defined(CONFIG_MAC)
/* block out HSYNC on the atari */
#define sti() __asm__ __volatile__ ("andiw #0xfbff,%/sr": : : "memory")
#else /* portable version */
#define sti() __asm__ __volatile__ ("andiw #0xf8ff,%/sr": : : "memory")
#endif /* machine compilation types */ 
#define cli() __asm__ __volatile__ ("oriw  #0x0700,%/sr": : : "memory")
#define nop() __asm__ __volatile__ ("nop"::)
#define mb()  __asm__ __volatile__ (""   : : :"memory")

#define save_flags(x) \
__asm__ __volatile__("movew %/sr,%0":"=d" (x) : /* no input */ :"memory")

#define restore_flags(x) \
__asm__ __volatile__("movew %0,%/sr": /* no outputs */ :"d" (x) : "memory")

#define iret() __asm__ __volatile__ ("rte": : :"memory", "sp", "cc")

#if 1
static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
  unsigned long tmp, flags;

  save_flags(flags);
  cli();

  switch (size) {
  case 1:
    __asm__ __volatile__
    ("moveb %2,%0\n\t"
     "moveb %1,%2"
    : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
    break;
  case 2:
    __asm__ __volatile__
    ("movew %2,%0\n\t"
     "movew %1,%2"
    : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
    break;
  case 4:
    __asm__ __volatile__
    ("movel %2,%0\n\t"
     "movel %1,%2"
    : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
    break;
  }
  restore_flags(flags);
  return tmp;
}
#else
static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
	    case 1:
		__asm__ __volatile__
			("moveb %2,%0\n\t"
			 "1:\n\t"
			 "casb %0,%1,%2\n\t"
			 "jne 1b"
			 : "=&d" (x) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	    case 2:
		__asm__ __volatile__
			("movew %2,%0\n\t"
			 "1:\n\t"
			 "casw %0,%1,%2\n\t"
			 "jne 1b"
			 : "=&d" (x) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	    case 4:
		__asm__ __volatile__
			("movel %2,%0\n\t"
			 "1:\n\t"
			 "casl %0,%1,%2\n\t"
			 "jne 1b"
			 : "=&d" (x) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	}
	return x;
}
#endif

#endif /* _M68K_SYSTEM_H */
