/*
 * SNI RM200 PCI specific interrupt handler code.
 *
 * Copyright (C) 1994 - 1997 by Ralf Baechle
 */
#include <asm/asm.h>
#include <linux/config.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/sni.h>
#include <asm/stackframe.h>

		.set	noreorder
		.set	noat
		.align	5
		NESTED(sni_rm200_pci_handle_int, PT_SIZE, sp)
		SAVE_ALL
		REG_S	sp,PT_OR2(sp)
		CLI
		/*
		 * Asume we received an interrupt from the PCI ASIC.
		 */
		.set	at
		lui	s0,%hi(SNI_PORT_BASE)
		li	a0,0x0f
		sb	a0,%lo(SNI_PORT_BASE+0x20)(s0)	# poll command
		lb	a0,%lo(SNI_PORT_BASE+0x20)(s0)	# read result
		bgtz	a0,poll_second
		andi	a0,7
		beq	a0,2,poll_second		# cascade?
		li	s1,1				# delay slot
		/*
		 * Acknowledge first pic
		 */
		lb	t2,%lo(SNI_PORT_BASE+0x21)(s0)
		lui	s4,%hi(cache_21)
		lb	t0,%lo(cache_21)(s4)
		sllv	s1,s1,a0
		or	t0,s1
		sb	t0,%lo(cache_21)(s4)
		sb	t0,%lo(SNI_PORT_BASE+0x21)(s0)
		li	t2,0x20
		sb	t2,%lo(SNI_PORT_BASE+0x20)(s0)
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
		lbu	t1,%lo(SNI_PORT_BASE+0x21)(s0)
		lb	t1,%lo(cache_21)(s4)
		nor	s1,zero,s1
		and	t1,s1
		sb	t1,%lo(cache_21)(s4)
		jr	v0
		sb	t1,%lo(SNI_PORT_BASE+0x21)(s0)	# delay slot

		/*
		 * Cascade interrupt from second PIC
		 */
		.align	5
poll_second:	li	a0,0x0f
		sb	a0,%lo(SNI_PORT_BASE+0xa0)(s0)	# poll command
		lb	a0,%lo(SNI_PORT_BASE+0xa0)(s0)	# read result
		bgtz	a0,3f
		andi	a0,7
		/*
		 * Acknowledge second pic
		 */
		lbu	t2,%lo(SNI_PORT_BASE+0xa1)(s0)
		lui	s4,%hi(cache_A1)
		lb	t3,%lo(cache_A1)(s4)
		sllv	s1,s1,a0
		or	t3,s1
		sb	t3,%lo(cache_A1)(s4)
		sb	t3,%lo(SNI_PORT_BASE+0xa1)(s0)
		li	t3,0x20
		sb	t3,%lo(SNI_PORT_BASE+0xa0)(s0)
		sb	t3,%lo(SNI_PORT_BASE+0x20)(s0)
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
		lb	t1,%lo(SNI_PORT_BASE+0xa1)(s0)
		lb	t1,%lo(cache_A1)(s4)
		subu	t0,1
		nor	s1,zero,s1
		and	t1,t1,s1
		sb	t1,%lo(cache_A1)(s4)
		jr	v0
		sb	t1,%lo(SNI_PORT_BASE+0xa1)(s0)	# delay slot

/*
 * FIXME:  This is definatly wrong but I'll have to do it this way
 *         'till I get more hardware info.
 * XXX: Apparently the Lance is attached to interrupt #5.
 */
#ifdef CONFIG_PCNET32

/*
 * FIXME: detect this address
 */
#define LANCE_BASE 0xbb000100

/* Offsets from base I/O address. */
#define LANCE_DATA 0x10
#define LANCE_ADDR 0x12
#define LANCE_RESET 0x14
#define LANCE_BUS_IF 0x16
#define LANCE_TOTAL_SIZE 0x18

/*
 * ... check if we were interrupted by the Lance ...
 */
3:		lh	s0,LANCE_BASE+LANCE_ADDR
		sh	zero,LANCE_BASE+LANCE_ADDR
		lh	t1,LANCE_BASE+LANCE_DATA
		andi	t2,t1,0x80
		beqz	t1,3f				# no Lance interrupt?
		mfc0	t0,CP0_STATUS			# delay slot
		ori	t0,0x041f
		xori	t0,0x041e
		mtc0	t0,CP0_STATUS
		li	a0,PCIMT_IRQ_ETHERNET
		jal	do_IRQ
		 move	a1,sp				# delay slot
		sh	s0,LANCE_BASE+LANCE_ADDR
		mfc0	t0,CP0_STATUS
		ori	t0,0x0401
		xori	t0,0x0001
		mtc0	t0,CP0_STATUS
		j	ret_from_sys_call
		nop					# delay slot

#endif /* CONFIG_PCNET32 */

#ifdef CONFIG_SCSI_NCR53C8XX

/*
 * ... check if we were interrupted by the NCR ...
 */
3:		lb	t0,PCIMT_CSITPEND
		andi	t0,0x40
		bnez	t0,3f				# bit 6 == 0 -> SCSI IRQ
		nop					# delay slot
		jal	do_fast_IRQ
		li	a0,PCIMT_IRQ_SCSI		# delay slot
		j	return
		nop					# delay slot

#endif /* CONFIG_SCSI_NCR53C8XX */

/*
 * "Jump extender" to reach spurious_interrupt
 */
3:		j	spurious_interrupt
		nop					# delay slot
		END(sni_rm200_pci_handle_int)
