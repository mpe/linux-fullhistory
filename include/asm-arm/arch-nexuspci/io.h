/*
 * linux/include/asm-arm/arch-nexuspci/io.h
 *
 * Copyright (C) 1997,1998 Russell King
 *
 * Modifications:
 *  06-Dec-1997	RMK	Created.
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

/*
 * Dynamic IO functions - let the compiler
 * optimize the expressions
 */
#define DECLARE_DYN_OUT(fnsuffix,instr,typ)					\
extern __inline__ void __out##fnsuffix (unsigned int value, unsigned int port)	\
{										\
	__asm__ __volatile__(							\
	"str" ##instr## "	%0, [%1, %2]"					\
	: 									\
	: "r" (value), "r" (PCIO_BASE), typ (port));				\
}

#define DECLARE_DYN_IN(sz,fnsuffix,instr,typ)					\
extern __inline__ unsigned sz __in##fnsuffix (unsigned int port)		\
{										\
	unsigned long value;							\
	__asm__ __volatile__(							\
	"ldr" ##instr## "	%0, [%1, %2]"					\
	: "=&r" (value)								\
	: "r" (PCIO_BASE), typ (port));						\
	return (unsigned sz)value;						\
}

extern __inline__ unsigned int __ioaddr (unsigned int port)			\
{										\
	return (unsigned int)(PCIO_BASE + port);				\
}

#define DECLARE_IO(sz,fnsuffix,instr,typ)	\
	DECLARE_DYN_OUT(fnsuffix,instr,typ)	\
	DECLARE_DYN_IN(sz,fnsuffix,instr,typ)

DECLARE_IO(char,b,"b","Jr")
DECLARE_IO(short,w,"h","r")
DECLARE_IO(int,l,"","Jr")

#undef DECLARE_IO
#undef DECLARE_DYN_OUT
#undef DECLARE_DYN_IN

/*
 * Constant address IO functions
 *
 * These have to be macros for the 'J' constraint to work -
 * +/-4096 immediate operand.
 */
#define __outbc(value,port)							\
({										\
	__asm__ __volatile__(							\
	"strb	%0, [%1, %2]"							\
	:									\
	: "r" (value), "r" (PCIO_BASE), "Jr" (port));				\
})

#define __inbc(port)								\
({										\
	unsigned char result;							\
	__asm__ __volatile__(							\
	"ldrb	%0, [%1, %2]"							\
	: "=r" (result)								\
	: "r" (PCIO_BASE), "Jr" (port));					\
	result;									\
})

#define __outwc(value,port)							\
({										\
	__asm__ __volatile__(							\
	"strh	%0, [%1, %2]"							\
	:									\
	: "r" (value), "r" (PCIO_BASE), "r" (port));				\
})

#define __inwc(port)								\
({										\
	unsigned short result;							\
	__asm__ __volatile__(							\
	"ldrh	%0, [%1, %2]"							\
	: "=r" (result)								\
	: "r" (PCIO_BASE), "r" (port));						\
	result & 0xffff;							\
})

#define __outlc(value,port)							\
({										\
	__asm__ __volatile__(							\
	"str	%0, [%1, %2]"							\
	:									\
	: "r" (value), "r" (PCIO_BASE), "Jr" (port));				\
})

#define __inlc(port)								\
({										\
	unsigned long result;							\
	__asm__ __volatile__(							\
	"ldr	%0, [%1, %2]"							\
	: "=r" (result)								\
	: "r" (PCIO_BASE), "Jr" (port));					\
	result;									\
})

#define __ioaddrc(port)								\
({										\
	unsigned long addr;							\
	addr = PCIO_BASE + port;						\
	addr;									\
})

#define inb(p)	 	(__builtin_constant_p((p)) ? __inbc(p)    : __inb(p))
#define inw(p)	 	(__builtin_constant_p((p)) ? __inwc(p)    : __inw(p))
#define inl(p)	 	(__builtin_constant_p((p)) ? __inlc(p)    : __inl(p))
#define outb(v,p)	(__builtin_constant_p((p)) ? __outbc(v,p) : __outb(v,p))
#define outw(v,p)	(__builtin_constant_p((p)) ? __outwc(v,p) : __outw(v,p))
#define outl(v,p)	(__builtin_constant_p((p)) ? __outlc(v,p) : __outl(v,p))
#define __ioaddr(p)	(__builtin_constant_p((p)) ? __ioaddr(p)  : __ioaddrc(p))

/*
 * Translated address IO functions
 *
 * IO address has already been translated to a virtual address
 */
#define outb_t(v,p)								\
	(*(volatile unsigned char *)(p) = (v))

#define inb_t(p)								\
	(*(volatile unsigned char *)(p))

#define outl_t(v,p)								\
	(*(volatile unsigned long *)(p) = (v))

#define inl_t(p)								\
	(*(volatile unsigned long *)(p))

#endif
