#ifndef _ASM_MIPS_UNISTD_H_
#define _ASM_MIPS_UNISTD_H_

/* XXX - _foo needs to be __foo, while __NR_bar could be _NR_bar. */
#define _syscall0(type,name) \
type name(void) \
{ \
register long __res; \
__asm__ volatile (".set\tnoat\n\t" \
                  "li\t$1,%1\n\t" \
                  ".set\tat\n\t" \
                  "syscall\n\t" \
                  : "=d" (__res) \
                  : "i" (__NR_##name) \
                  : "$1"); \
if (__res >= 0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall1(type,name,atype,a) \
type name(atype a) \
{ \
register long __res; \
__asm__ volatile ("move\t$2,%2\n\t" \
                  ".set\tnoat\n\t" \
                  "li\t$1,%1\n\t" \
                  ".set\tat\n\t" \
                  "syscall" \
                  : "=d" (__res) \
                  : "i" (__NR_##name),"d" ((long)(a)) \
                  : "$1","$2"); \
if (__res >= 0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall2(type,name,atype,a,btype,b) \
type name(atype a,btype b) \
{ \
register long __res; \
__asm__ volatile ("move\t$2,%2\n\t" \
                  "move\t$3,%3\n\t" \
                  ".set\tnoat\n\t" \
                  "li\t$1,%1\n\t" \
                  ".set\tat\n\t" \
                  "syscall" \
                  : "=d" (__res) \
                  : "i" (__NR_##name),"d" ((long)(a)), \
                                      "d" ((long)(b))); \
                  : "$1","$2","$3"); \
if (__res >= 0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall3(type,name,atype,a,btype,b,ctype,c) \
type name (atype a, btype b, ctype c) \
{ \
register long __res; \
__asm__ volatile ("move\t$2,%2\n\t" \
                  "move\t$3,%3\n\t" \
                  "move\t$4,%4\n\t" \
                  ".set\tnoat\n\t" \
                  "li\t$1,%1\n\t" \
                  ".set\tat\n\t" \
                  "syscall" \
                  : "=d" (__res) \
                  : "i" (__NR_##name),"d" ((long)(a)), \
                                      "d" ((long)(b)), \
                                      "d" ((long)(c)) \
                  : "$1","$2","$3","$4"); \
if (__res>=0) \
	return (type) __res; \
errno=-__res; \
return -1; \
}

#define _syscall4(type,name,atype,a,btype,b,ctype,c,dtype,d) \
type name (atype a, btype b, ctype c, dtype d) \
{ \
register long __res; \
__asm__ volatile (".set\tnoat\n\t" \
                  "move\t$2,%2\n\t" \
                  "move\t$3,%3\n\t" \
                  "move\t$4,%4\n\t" \
                  "move\t$5,%5\n\t" \
                  ".set\tnoat\n\t" \
                  "li\t$1,%1\n\t" \
                  ".set\tat\n\t" \
                  "syscall" \
                  : "=d" (__res) \
                  : "i" (__NR_##name),"d" ((long)(a)), \
                                      "d" ((long)(b)), \
                                      "d" ((long)(c)), \
                                      "d" ((long)(d)) \
                  : "$1","$2","$3","$4","$5"); \
if (__res>=0) \
	return (type) __res; \
errno=-__res; \
return -1; \
}

#define _syscall5(type,name,atype,a,btype,b,ctype,c,dtype,d,etype,e) \
type name (atype a,btype b,ctype c,dtype d,etype e) \
{ \
register long __res; \
__asm__ volatile (".set\tnoat\n\t" \
                  "move\t$2,%2\n\t" \
                  "move\t$3,%3\n\t" \
                  "move\t$4,%4\n\t" \
                  "move\t$5,%5\n\t" \
                  "move\t$6,%6\n\t" \
                  ".set\tnoat\n\t" \
                  "li\t$1,%1\n\t" \
                  ".set\tat\n\t" \
                  "syscall" \
                  : "=d" (__res) \
                  : "i" (__NR_##name),"d" ((long)(a)), \
                                      "d" ((long)(b)), \
                                      "d" ((long)(c)), \
                                      "d" ((long)(d)), \
                                      "d" ((long)(e)) \
                  : "$1","$2","$3","$4","$5","$6"); \
if (__res>=0) \
	return (type) __res; \
errno=-__res; \
return -1; \
}

#endif /* _ASM_MIPS_UNISTD_H_ */
