/* $Id: int-handler.S,v 1.4 1998/05/07 14:17:47 ralf Exp $
 *
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
		CLI
		.set	at

		lb	t0,led_cache
		addiu	t0,1
		sb	t0,led_cache
		sb	t0,PCIMT_CSLED
		.data
led_cache:	.byte	0
		.text

		mfc0	t0,CP0_STATUS
		mfc0	t1,CP0_CAUSE
		and	t0,t1

		 andi	t1,t0,0x0800			# hardware interrupt 1
		bnez	t1,hwint1
		 andi	t1,t0,0x4000			# hardware interrupt 4
		bnez	t1,eth_int

		 andi	t1,t0,0x1000			# hardware interrupt 2
		bnez	t1,hwint2
		 andi	t1,t0,0x2000			# hardware interrupt 3
		bnez	t1,hwint3
		 andi	t1,t0,0x8000			# hardware interrupt 5
		bnez	t1,hwint5
		 andi	t1,t0,0x0400			# hardware interrupt 0
		bnez	t1,hwint0
		 nop

		j	spurious_interrupt		# Nothing up ...
		 nop

 ##############################################################################

swint0:		PANIC("swint0")
swint1:		PANIC("swint1")

 /* ------------------------------------------------------------------------ */

hwint1:		lbu	t0,PCIMT_CSITPEND

		 andi	t1,t0,0x20
		bnez	t1,eisa_int

#ifdef CONFIG_SCSI_NCR53C8XX
		 andi	t1,t0,0x40
		beqz	t1,scsi_int
#endif
		 nop

		j	spurious_interrupt
		 nop

 /* ------------------------------------------------------------------------ */

hwint0:		lbu	t0,PCIMT_CSITPEND

		 andi	t1,t0,0x01
		beqz	t1,int2

go_spurious:	j	spurious_interrupt		# we got fooled
		 nop

eisa_int:	lui	s0,%hi(SNI_PORT_BASE)
		li	a0,0x0f
		sb	a0,%lo(SNI_PORT_BASE+0x20)(s0)	# poll command
		lb	a0,%lo(SNI_PORT_BASE+0x20)(s0)	# read result
		bgtz	a0,poll_second
		 andi	a0,7
		beq	a0,2,poll_second		# cascade?
		 li	s1,1
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
		jal	do_IRQ
		 move	a1,sp
		/*
		 * Unblock first pic
		 */
		lbu	t1,%lo(SNI_PORT_BASE+0x21)(s0)
		lb	t1,%lo(cache_21)(s4)
		nor	s1,zero,s1
		and	t1,s1
		sb	t1,%lo(cache_21)(s4)
		j	ret_from_irq
		 sb	t1,%lo(SNI_PORT_BASE+0x21)(s0)

		/*
		 * Cascade interrupt from second PIC
		 */
		.align	5
poll_second:	li	a0,0x0f
		sb	a0,%lo(SNI_PORT_BASE+0xa0)(s0)	# poll command
		lb	a0,%lo(SNI_PORT_BASE+0xa0)(s0)	# read result
		bgtz	a0,go_spurious
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
		addiu	a0,8
		jal	do_IRQ
		 move	a1,sp
		/*
		 * Unblock second pic
		 */
		lb	t1,%lo(SNI_PORT_BASE+0xa1)(s0)
		lb	t1,%lo(cache_A1)(s4)
		subu	t0,1
		nor	s1,zero,s1
		and	t1,t1,s1
		sb	t1,%lo(cache_A1)(s4)
		j	ret_from_irq
		 sb	t1,%lo(SNI_PORT_BASE+0xa1)(s0)

/*
 * ... check if we were interrupted by the Lance ...
 */
eth_int:	mfc0	s0,CP0_STATUS
		ori	t0,s0,0x4000
		xori	t0,0x4000
		mtc0	t0,CP0_STATUS

		li	a0,PCIMT_IRQ_ETHERNET
		jal	do_IRQ
		 move	a1,sp

		mtc0	s0,CP0_STATUS

		j	ret_from_irq
		 nop

#ifdef CONFIG_SCSI_NCR53C8XX

/*
 * ... check if we were interrupted by the NCR ...
 */
scsi_int:	li	a0,PCIMT_IRQ_SCSI
		jal	do_IRQ
		 move	a1,sp
		j	ret_from_irq
		 nop

#endif /* CONFIG_SCSI_NCR53C8XX */

pci_int:	PANIC("Received PCI interrupt but no handler yet ...\n")
1:		j	1b
		 nop

int2:		PANIC("Received int2 but no handler yet ...\n")
1:		j	1b
		 nop

hwint2:		PANIC("hwint2 and no handler yet")
hwint3:		PANIC("hwint3 and no handler yet")
hwint5:		PANIC("hwint5 and no handler yet")

		END(sni_rm200_pci_handle_int)
