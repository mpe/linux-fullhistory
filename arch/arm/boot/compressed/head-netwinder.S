		.section	".start", #alloc, #execinstr

		adr	r2, 1f
		ldmdb	r2, {r7, r8}
		and	r3, r2, #0xc000
		teq	r3, #0x8000
		beq	2f
		bic	r3, r2, #0xc000
		orr	r3, r3, #0x8000
		mov	r0, r3
		mov	r4, #64
		sub	r5, r8, r7
		b	1f

		.word	_start
		.word	__bss_start

1:
		.rept	4
		ldmia	r2!, {r6, r7, r8, r9}
		stmia	r3!, {r6, r7, r8, r9}
		.endr
		subs	r4, r4, #64
		bcs	1b
		movs	r4, r5
		mov	r5, #0
		mov	r1, #5		@ only here to fix NeTTroms which dont set r1
		movne	pc, r0

		mov	r0, #0
2:
