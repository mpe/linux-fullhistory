#include <linux/config.h>
#include "../kernel/ppc_defs.h"
#include "../kernel/ppc_asm.tmpl"
#include <asm/processor.h>
#include <asm/cache.h>

	.text

/*
 * $Id: head.S,v 1.26 1998/09/19 01:21:20 cort Exp $
 *	
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
	mfmsr	r3		/* Turn off interrupts  */
	li	r4,0
	ori	r4,r4,MSR_EE
	andc	r3,r3,r4
	mtmsr	r3

/* check if we need to relocate ourselves to the link addr or were we
   loaded there to begin with -- Cort */
	lis	r4,start@h
	ori	r4,r4,start@l
	mflr	r3
	subi	r3,r3,4		/* we get the nip, not the ip of the branch */
	mr	r8,r3
	cmp	0,r3,r4
	bne	1010f
/* compute size of whole image in words.  this should be moved to
 * start_ldr() -- Cort
 */
	lis	r4,start@h
	ori	r4,r4,start@l
	lis	r5,end@h
	ori	r5,r5,end@l
	addi	r5,r5,3		/* round up */
	sub	r5,r5,r4
	srwi	r5,r5,2
	mr	r7,r5
	b	start_ldr
1010:
#if 0	
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
#endif	
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
	mr	r3, r11
        mr      r21, r11
	bl      serial_init		/* Init MBX serial port */

	lis	r8, 0xfa200000@h	/* Disable Ethernet SCC */
	li	r0, 0
	stw	r0, 0x0a00(r8)

	mr      r11, r21
	lis	r8,start@h
	ori	r8,r8,start@l
	li	r9,end@h
	ori	r9,r9,end@l
	sub	r7,r8,r9
	srwi	r7,r7,2
#define ILAP_ADDRESS    0xfa000020
        lis     r8, ILAP_ADDRESS@h
        lwz     r8, ILAP_ADDRESS@l(r8)
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
	lwz	r6, 0(r6)
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

.globl _get_HID0
_get_HID0:		
	mfspr	r3,HID0
	blr

.globl _put_HID0
_put_HID0:		
	mtspr	HID0,r3
	blr
		
.globl _get_MSR
_get_MSR:		
	mfmsr	r3
	blr
	
.globl _put_MSR
_put_MSR:		
	mtmsr	r3
	blr

/*
 * Flush instruction cache
 * *** I'm really paranoid here!
 */
_GLOBAL(flush_instruction_cache)
	mflr	r5
	bl	flush_data_cache
#ifndef CONFIG_MBX
	mfspr	r3,HID0	/* Caches are controlled by this register */
	li	r4,0
	ori	r4,r4,(HID0_ICE|HID0_ICFI)
	or	r3,r3,r4	/* Need to enable+invalidate to clear */
	mtspr	HID0,r3
	andc	r3,r3,r4
	ori	r3,r3,HID0_ICE	/* Enable cache */
	mtspr	HID0,r3
#endif	
	mtlr	r5
	blr
	
#define NUM_CACHE_LINES 128*8
#define CACHE_LINE_SIZE 32 
#if 0
cache_flush_buffer:
	.space	NUM_CACHE_LINES*CACHE_LINE_SIZE	/* CAUTION! these need to match hardware */
#else
#define cache_flush_buffer 0x1000
#endif

/*
 * Flush data cache
 * *** I'm really paranoid here!
 */
_GLOBAL(flush_data_cache)
	lis	r3,cache_flush_buffer@h
	ori	r3,r3,cache_flush_buffer@l
	li	r4,NUM_CACHE_LINES
	mtctr	r4
#if 0
00:	dcbz	0,r3			/* Flush cache line with minimal BUS traffic */
#else
00:	lwz	r4,0(r3)
#endif
	addi	r3,r3,CACHE_LINE_SIZE	/* Next line, please */
	bdnz	00b	
10:	blr
	.comm	.stack,4096*2,4
