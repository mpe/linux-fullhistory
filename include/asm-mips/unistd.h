#ifndef __ASM_MIPS_UNISTD_H
#define __ASM_MIPS_UNISTD_H

#ifdef __KERNEL__

#include <asm/mipsconfig.h>
/*
 * Ugly kludge to enforce 32bit mode proof code.
 * Access errno via USEG, not KSEGx for internal kernel syscalls
 */
#define errno (*(int *)((unsigned long)&errno - KERNELBASE))

#endif /* __KERNEL__ */

/* XXX - _foo needs to be __foo, while __NR_bar could be _NR_bar. */
#define _syscall0(type,name) \
type name(void) \
{ \
register long __res __asm__ ("$2"); \
__asm__ volatile ("syscall" \
                  : "=r" (__res) \
                  : "0" (__NR_##name)); \
if (__res >= 0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall1(type,name,atype,a) \
type name(atype a) \
{ \
register long __res __asm__ ("$2"); \
__asm__ volatile ("move\t$4,%2\n\t" \
                  "syscall" \
                  : "=r" (__res) \
                  : "0" (__NR_##name),"r" ((long)(a)) \
                  : "$4"); \
if (__res >= 0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall2(type,name,atype,a,btype,b) \
type name(atype a,btype b) \
{ \
register long __res __asm__ ("$2"); \
__asm__ volatile ("move\t$4,%2\n\t" \
                  "move\t$5,%3\n\t" \
                  "syscall" \
                  : "=r" (__res) \
                  : "0" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b))); \
                  : "$4","$5"); \
if (__res >= 0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall3(type,name,atype,a,btype,b,ctype,c) \
type name (atype a, btype b, ctype c) \
{ \
register long __res __asm__ ("$2"); \
__asm__ volatile ("move\t$4,%2\n\t" \
                  "move\t$5,%3\n\t" \
                  "move\t$6,%4\n\t" \
                  "syscall" \
                  : "=r" (__res) \
                  : "0" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)) \
                  : "$4","$5","$6"); \
if (__res>=0) \
	return (type) __res; \
errno=-__res; \
return -1; \
}

#define _syscall4(type,name,atype,a,btype,b,ctype,c,dtype,d) \
type name (atype a, btype b, ctype c, dtype d) \
{ \
register long __res __asm__ ("$2"); \
__asm__ volatile (".set\tnoat\n\t" \
                  "move\t$4,%2\n\t" \
                  "move\t$5,%3\n\t" \
                  "move\t$6,%4\n\t" \
                  "move\t$7,%5\n\t" \
                  "syscall" \
                  : "=r" (__res) \
                  : "0" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)) \
                  : "$4","$5","$6","$7"); \
if (__res>=0) \
	return (type) __res; \
errno=-__res; \
return -1; \
}

#define _syscall5(type,name,atype,a,btype,b,ctype,c,dtype,d,etype,e) \
type name (atype a,btype b,ctype c,dtype d,etype e) \
{ \
register long __res __asm__ ("$2"); \
__asm__ volatile (".set\tnoat\n\t" \
                  "move\t$4,%2\n\t" \
                  "move\t$5,%3\n\t" \
                  "move\t$6,%4\n\t" \
                  "move\t$7,%5\n\t" \
                  "move\t$3,%6\n\t" \
                  "syscall" \
                  : "=r" (__res) \
                  : "0" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)), \
                                      "r" ((long)(e)) \
                  : "$3","$4","$5","$6","$7"); \
if (__res>=0) \
	return (type) __res; \
errno=-__res; \
return -1; \
}

#endif /* __ASM_MIPS_UNISTD_H */
