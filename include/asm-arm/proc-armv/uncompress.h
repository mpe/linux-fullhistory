/*
 * linux/include/asm-arm/proc-armv/uncompress.h
 *
 * (c) 1997 Russell King
 */

static inline void proc_decomp_setup (void)
{
	__asm__ __volatile__("
	mrc	p15, 0, r0, c0, c0
	eor	r0, r0, #0x44 << 24
	eor	r0, r0, #0x01 << 16
	eor	r0, r0, #0xA1 << 8
	movs	r0, r0, lsr #4
	mcreq	p15, 0, r0, c7, c5, 0		@ flush I cache
	mrceq	p15, 0, r0, c1, c0
	orreq	r0, r0, #1 << 12
	mcreq	p15, 0, r0, c1, c0		@ enable I cache
	mov	r0, #0
	mcreq	p15, 0, r0, c15, c1, 2		@ enable clock switching
	" : : : "r0", "cc", "memory");
}
