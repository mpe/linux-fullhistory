#include "../kernel/ppc_defs.h"
#include "../kernel/ppc_asm.tmpl"
#include <asm/processor.h>
#include <asm/cache.h>

	.text

/*
 * This code is loaded by the ROM loader at some arbitrary location.
 * Move it to high memory so that it can load the kernel at 0x0000.
 *
 * The MBX EPPC-Bug understands ELF, so it loads us into the location
 * specified in the header.  This is a two step process.  First, EPPC-Bug
 * loads the file into the intermediate buffer memory location specified
 * by the environment parameters.  When it discovers this is an ELF
 * binary, it relocates to the link address for us.  Unfortunately, the
 * header does not move with the file, so we have to find the
 * intermediate load location and read the header from there.  From
 * information provided by Motorola (thank you), we know this intermediate
 * location can be found from the NVRAM environment.
 * All of these addresses must be somewhat carefully chosen to make sure
 * we don't overlap the regions.  I chose to load the kernel at 0, the
 * compressed image loads at 0x00100000, and the MBX intermediate buffer
 * was set to 0x00200000.  Provided the loaded kernel image never grows
 * over one megabyte (which I am going to ensure never happens :-), these
 * will work fine.  When we get called from EPPC-Bug, registers are:
 *              R1 - Stack pointer at a high memory address.
 *              R3 - Pointer to Board Information Block.
 *              R4 - Pointer to argument string.
 *              Interrupts masked, cache and MMU disabled.
 */

	.globl	start
start:
	bl	start_
start_:
	mr	r11,r3		/* Save pointer to residual/board data */
	
#ifndef CONFIG_MBX
	mfmsr	r3		/* Turn off interrupts */
	li	r4,0
	ori	r4,r4,MSR_EE
	andc	r3,r3,r4
	mtmsr	r3
/* Copy relocation code down to location 0x0100 (where we hope it's safe!) */
	mflr	r3
	addi	r5,r3,start_ldr-start_
	addi	r3,r3,relocate-start_
	li	r4,0x0100
	mtctr	r4
	subi	r3,r3,4
	subi	r4,r4,4
00:	lwzu	r6,4(r3)
	stwu	r6,4(r4)
	cmp	0,r3,r5
	bne	00b
	mflr	r21
	mfctr	r22
	mtlr	r21
	mtctr	r22
	bctr			/* Jump to code */
/* 
 * no matter where we're loaded, move ourselves to -Ttext address
 */
relocate:
	mflr	r3		/* Compute code bias */
	subi	r3,r3,4
	mr	r8,r3
	lis	r4,start@h
	ori	r4,r4,start@l
#if 0	
	lis	r5,edata@h
	ori	r5,r5,edata@l
#else
	lis	r5,end@h
	ori	r5,r5,end@l
#endif		
	addi	r5,r5,3			/* Round up - just in case */
	sub	r5,r5,r4		/* Compute # longwords to move */
	srwi	r5,r5,2
	mtctr	r5
	mr	r7,r5
	li	r6,0
	subi	r3,r3,4			/* Set up for loop */
	subi	r4,r4,4
00:	lwzu	r5,4(r3)
	stwu	r5,4(r4)
	xor	r6,r6,r5
	bdnz	00b
	lis	r3,start_ldr@h
	ori	r3,r3,start_ldr@l
	mtlr	r3			/* Easiest way to do an absolute jump */
	blr
start_ldr:	
#endif /* ndef CONFIG_MBX */
	
/* Clear all of BSS */
	lis	r3,edata@h
	ori	r3,r3,edata@l
	lis	r4,end@h
	ori	r4,r4,end@l
	subi	r3,r3,4
	subi	r4,r4,4
	li	r0,0
50:	stwu	r0,4(r3)
	cmp	0,r3,r4
	bne	50b
90:	mr	r9,r1			/* Save old stack pointer (in case it matters) */
	lis	r1,.stack@h
	ori	r1,r1,.stack@l
	addi	r1,r1,4096*2
	subi	r1,r1,256
	li	r2,0x000F		/* Mask pointer to 16-byte boundary */
	andc	r1,r1,r2
/* Run loader */
#ifdef CONFIG_MBX
        bl      serial_init             /* Init MBX serial port */
#define ILAP_ADDRESS    0xfa000020
        lis     r8, ILAP_ADDRESS@h
        lwz     r8, ILAP_ADDRESS@l(r8)
	li	r9,end@h
	ori	r9,r9,end@l
	sub	r7,r8,r9
        addis   r8, r8, 1               /* Add 64K */
#endif	
	mr	r3,r8			/* Load point */
	mr	r4,r7			/* Program length */
	mr	r5,r6			/* Checksum */
	mr	r6,r11			/* Residual data */
	bl	decompress_kernel
	/* changed to use r3 (as firmware does) for kernel
	   as ptr to residual -- Cort*/
	lis	r6,cmd_line@h
	ori	r6,r6,cmd_line@l
	subi	r7,r6,1
00:	lbzu	r2,1(r7)
	cmpi	0,r2,0
	bne	00b

	/* r4,r5 have initrd_start, size */
	lis	r2,initrd_start@h
	ori	r2,r2,initrd_start@l
	lwz	r4,0(r2)
	lis	r2,initrd_end@h
	ori	r2,r2,initrd_end@l
	lwz	r5,0(r2)
	
	/* tell kernel we're prep */
	/* 
	 * get start address of kernel code which is stored as a coff
	 * entry.  see boot/head.S -- Cort 
	 */
	li	r9,0x0
	lwz	r9,0(r9)
	mtlr	r9
#ifndef CONFIG_MBX
	li	r9,0
	lis	r10,0xdeadc0de@h
	ori	r10,r10,0xdeadc0de@l
	stw	r10,0(r9)
#endif
	blr	
hang:
	b	hang	

/*
 * Delay for a number of microseconds
 * -- Use the BUS timer (assumes 66MHz)
 */
	.globl	udelay
udelay:		
	mfspr	r4,PVR
	srwi	r4,r4,16
	cmpi	0,r4,1		/* 601 ? */
	bne	.udelay_not_601
00:	li	r0,86	/* Instructions / microsecond? */
	mtctr	r0
10:	addi	r0,r0,0 /* NOP */
	bdnz	10b
	subic.	r3,r3,1
	bne	00b
	blr

.udelay_not_601:		
	mulli	r4,r3,1000	/* nanoseconds */
	addi	r4,r4,59
	li	r5,60
	divw	r4,r4,r5	/* BUS ticks */
1:	mftbu	r5
	mftb	r6
	mftbu	r7
	cmp	0,r5,r7
	bne	1b		/* Get [synced] base time */
	addc	r9,r6,r4	/* Compute end time */
	addze	r8,r5
2:	mftbu	r5
	cmp	0,r5,r8
	blt	2b
	bgt	3f
	mftb	r6
	cmp	0,r6,r9
	blt	2b
3:	blr		
	
	.comm	.stack,4096*2,4
