/*
 * arch/mips/kernel/tyne.S
 *
 * Deskstation Tyne specific Assembler code
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * written by Ralf Baechle and Andreas Busse
 */
#include <asm/asm.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>

/*
 * Deskstation Tyne interrupt handler
 */
		.text
		.set	noreorder
		.set	noat
		.align	5
		NESTED(deskstation_tyne_handle_int, FR_SIZE, sp)
		SAVE_ALL
		CLI
		.set	at
		lui	s0,%hi(PORT_BASE)
		li	t1,0x0f
		sb	t1,%lo(PORT_BASE+0x20)(s0)	# poll command
		lb	t1,%lo(PORT_BASE+0x20)(s0)	# read result
		li	s1,1
		bgtz	t1,Lpoll_second
		andi	t1,t1,7
		/*
		 * Acknowledge first pic
		 */
		lb	t2,%lo(PORT_BASE+0x21)(s0)
		lui	s4,%hi(cache_21)
		lb	t0,%lo(cache_21)(s4)
		sllv	s1,s1,t1
		or	t0,t0,s1
		sb	t0,%lo(cache_21)(s4)
		sb	t0,%lo(PORT_BASE+0x21)(s0)
		lui	s3,%hi(intr_count)
		lw	t0,%lo(intr_count)(s3)
		li	t2,0x20
		sb	t2,%lo(PORT_BASE+0x20)(s0)
		/*
		 * Now call the real handler
		 */
		la	t3,IRQ_vectors
		sll	t2,t1,2
		addu	t3,t3,t2
		lw	t3,(t3)
		addiu	t0,t0,1
		jalr	t3
		sw	t0,%lo(intr_count)(s3)			# delay slot
		lw	t0,%lo(intr_count)(s3)
		/*
		 * Unblock first pic
		 */
		lbu	t1,%lo(PORT_BASE+0x21)(s0)
		lb	t1,%lo(cache_21)(s4)
		subu	t0,t0,1
		sw	t0,%lo(intr_count)(s3)
		nor	s1,zero,s1
		and	t1,t1,s1
		sb	t1,%lo(cache_21)(s4)
		jr	v0
		sb	t1,%lo(PORT_BASE+0x21)(s0)	# delay slot

		.align	5
Lpoll_second:	li	t1,0x0f
		sb	t1,%lo(PORT_BASE+0xa0)(s0)	# poll command
		lb	t1,%lo(PORT_BASE+0xa0)(s0)	# read result
		lui	s4,%hi(cache_A1)
		bgtz	t1,spurious_interrupt
		andi	t1,t1,7
		/*
		 * Acknowledge second pic
		 */
		lbu	t2,%lo(PORT_BASE+0xa1)(s0)
		lb	t3,%lo(cache_A1)(s4)
		sllv	s1,s1,t1
		or	t3,t3,s1
		sb	t3,%lo(cache_A1)(s4)
		sb	t3,%lo(PORT_BASE+0xa1)(s0)
		li	t3,0x20
		sb	t3,%lo(PORT_BASE+0xa0)(s0)
		lui	s3,%hi(intr_count)
		lw	t0,%lo(intr_count)(s3)
		sb	t3,%lo(PORT_BASE+0x20)(s0)
		/*
		 * Now call the real handler
		 */
		la	t0,IRQ_vectors
		sll	t2,t1,2
		addu	t0,t0,t2
		lw	t0,32(t0)
		addiu	t0,t0,1
		jalr	t0
		sw	t0,%lo(intr_count)(s3)			# delay slot
		lw	t0,%lo(intr_count)(s3)
		/*
		 * Unblock second pic
		 */
		lb	t1,%lo(PORT_BASE+0xa1)(s0)
		lb	t1,%lo(cache_A1)(s4)
		subu	t0,t0,1
		lw	t0,%lo(intr_count)(s3)
		nor	s1,zero,s1
		and	t1,t1,s1
		sb	t1,%lo(cache_A1)(s4)
		jr	v0
		sb	t1,%lo(PORT_BASE+0xa1)(s0)	# delay slot
		END(deskstation_tyne_handle_int)

		.align	5
LEAF(spurious_interrupt)
		/*
		 * Nothing happened... (whistle)
		 */
		lui	t1,%hi(spurious_count)
		lw	t0,%lo(spurious_count)(t1)
		la	v0,return
		addiu	t0,1
		jr	ra
		sw	t0,%lo(spurious_count)(t1)
		END(spurious_interrupt)

