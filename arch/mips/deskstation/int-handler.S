/*
 * Deskstation rPC44/Tyne specific interrupt handler code
 *
 * Copyright (C) 1994, 1995, 1996 by Ralf Baechle
 */
#include <asm/asm.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

#error "FIXME - PORT_BASE is defined to port_base which breaks this file"

		.text
		.set	noreorder
		.set	noat
		.align	5
		NESTED(deskstation_handle_int, PT_SIZE, sp)
		SAVE_ALL
		REG_S	sp,PT_OR2(sp)
		CLI
		.set	at
		lui	s0,%hi(PORT_BASE)
		li	a0,0x0f
		sb	a0,%lo(PORT_BASE+0x20)(s0)	# poll command
		lb	a0,%lo(PORT_BASE+0x20)(s0)	# read result
		bgtz	a0,poll_second
		andi	a0,7
		beq	a0,2,poll_second		# cascade?
		li	s1,1				# delay slot
		/*
		 * Acknowledge first pic
		 */
		lb	t2,%lo(PORT_BASE+0x21)(s0)
		lui	s4,%hi(cache_21)
		lb	t0,%lo(cache_21)(s4)
		sllv	s1,s1,a0
		or	t0,s1
		sb	t0,%lo(cache_21)(s4)
		sb	t0,%lo(PORT_BASE+0x21)(s0)
		li	t2,0x20
		sb	t2,%lo(PORT_BASE+0x20)(s0)
		/*
		 * Now call the real handler
		 */
		la	t3,IRQ_vectors
		sll	t2,a0,PTRLOG
		addu	t3,t2
		LONG_L	t3,(t3)
		jalr	t3
		nop					# delay slot
		/*
		 * Unblock first pic
		 */
		lbu	t1,%lo(PORT_BASE+0x21)(s0)
		lb	t1,%lo(cache_21)(s4)
		nor	s1,zero,s1
		and	t1,s1
		sb	t1,%lo(cache_21)(s4)
		jr	v0
		sb	t1,%lo(PORT_BASE+0x21)(s0)	# delay slot

		/*
		 * Cascade interrupt from second PIC
		 */
		.align	5
poll_second:	li	a0,0x0f
		sb	a0,%lo(PORT_BASE+0xa0)(s0)	# poll command
		lb	a0,%lo(PORT_BASE+0xa0)(s0)	# read result
		bgtz	a0,3f
		andi	a0,7
		/*
		 * Acknowledge second pic
		 */
		lbu	t2,%lo(PORT_BASE+0xa1)(s0)
		lui	s4,%hi(cache_A1)
		lb	t3,%lo(cache_A1)(s4)
		sllv	s1,s1,a0
		or	t3,s1
		sb	t3,%lo(cache_A1)(s4)
		sb	t3,%lo(PORT_BASE+0xa1)(s0)
		li	t3,0x20
		sb	t3,%lo(PORT_BASE+0xa0)(s0)
		sb	t3,%lo(PORT_BASE+0x20)(s0)
		/*
		 * Now call the real handler
		 */
		la	t3,IRQ_vectors
		addiu	a0,8
		sll	t2,a0,PTRLOG
		addu	t3,t2
		LONG_L	t3,(t3)
		jalr	t3
		nop					# delay slot
		/*
		 * Unblock second pic
		 */
		lb	t1,%lo(PORT_BASE+0xa1)(s0)
		lb	t1,%lo(cache_A1)(s4)
		nor	s1,zero,s1
		and	t1,t1,s1
		sb	t1,%lo(cache_A1)(s4)
		jr	v0
		sb	t1,%lo(PORT_BASE+0xa1)(s0)	# delay slot

/*
 * "Jump extender" to reach spurious_interrupt
 */
3:		j	spurious_interrupt
		nop					# delay slot
		END(deskstation_handle_int)
