/*
 * linux/include/asm-arm/arch-rpc/system.h
 *
 * Copyright (c) 1996-1999 Russell King.
 */
#include <asm/arch/hardware.h>
#include <asm/iomd.h>
#include <asm/io.h>

#define arch_do_idle()		cpu_do_idle()
#define arch_power_off()	do { } while (0)

extern __inline__ void arch_reset(char mode)
{
	extern void ecard_reset(int card);

	ecard_reset(-1);

	outb(0, IOMD_ROMCR0);

	/*
	 * Jump into the ROM
	 */
	cpu_reset(0);
}
