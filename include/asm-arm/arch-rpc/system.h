/*
 * linux/include/asm-arm/arch-rpc/system.h
 *
 * Copyright (c) 1996-1999 Russell King.
 */
#include <asm/arch/hardware.h>
#include <asm/iomd.h>
#include <asm/io.h>

#define arch_do_idle() cpu_do_idle()

extern __inline__ void arch_reset(char mode)
{
	extern void ecard_reset(int card);

	ecard_reset(-1);

	outb(0, IOMD_ROMCR0);

	__asm__ __volatile__(
		"mcr p15, 0, %0, c1, c0, 0\n\t"
		"mov pc, #0"
		 : : "r" (cpu_reset()));
}
