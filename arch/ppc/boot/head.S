#include "../kernel/ppc_defs.h"
#include "../kernel/ppc_asm.tmpl"
#include <asm/processor.h>

	.text

/*
 * This code is loaded by the ROM loader at some arbitrary location.
 * Move it to high memory so that it can load the kernel at 0x0000.
 */

	.globl	start
start:
	bl	start_
start_:
/* TEMP - No residual data on BeBox (yet) */
#if 0	
#define IS_BE_BOX	0x42654278	/* 'BeBx' */
	lis	r2,IS_BE_BOX>>16
	ori	r2,r2,IS_BE_BOX&0xFFFF
	cmp	0,r30,r2
	bne	notBeBox
	li	r3,0
#endif	
notBeBox:
/* TEMP */
	mr	r11,r3		/* Save pointer to residual data */
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
	bl	flush_instruction_cache
	mtlr	r21
	mtctr	r22
	bctr			/* Jump to code */
/* Relocate code to final resting spot */
relocate:
	mflr	r3		/* Compute code bias */
	subi	r3,r3,4
	mr	r8,r3
	lis	r4,start@h
	ori	r4,r4,start@l
	lis	r5,edata@h
	ori	r5,r5,edata@l
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
	mr	r3,r8			/* Load point */
	mr	r4,r7			/* Program length */
	mr	r5,r6			/* Checksum */
	mr	r6,r11			/* Residual data */
	bl	decompress_kernel
	/*mr	r29,r3*/			/* R3 = TotalMemory */
	/*lis	r28,hold_residual@h
	ori	r28,r28,hold_residual@l*/
	/* changed to use r3 (as firmware does) for kernel
	   as ptr to residual -- Cort*/
	li	r5,0x100		/* Kernel code starts here */
	mtlr	r5
	blr	
hang:
	b	hang	

	.globl	_get_SP
_get_SP:
	mr	r3,r1
	blr

	.globl	_get_PVR
_get_PVR:
	mfspr	r3,PVR
	blr

	.globl	_get_MSR
_get_MSR:
	mfmsr	r3
	blr

	.globl	_put_MSR
_put_MSR:
	sync
	mtmsr	r3
	blr

	.globl	_get_HID0
_get_HID0:
	mfspr	r3,HID0
	blr
	
	.globl	_put_HID0
_put_HID0:
	sync
	mtspr	HID0,r3
	blr

/*
 * This space [buffer] is used to forceably flush the data cache when
 * running in copyback mode.  This is necessary IFF the data cache could
 * contain instructions for which the instruction cache has stale data.
 * Since the instruction cache NEVER snoops the data cache, memory must
 * be made coherent with the data cache to insure that the instruction
 * cache gets a valid instruction stream.  Note that this flushing is
 * only performed when switching from system to user mode since this is
 * the only juncture [as far as the OS goes] where the data cache may
 * contain instructions, e.g. after a disk read.
 */
#define NUM_CACHE_LINES 128*8
#define CACHE_LINE_SIZE 32 
#if 0
cache_flush_buffer:
	.space	NUM_CACHE_LINES*CACHE_LINE_SIZE	/* CAUTION! these need to match hardware */
#else
#define cache_flush_buffer 0x1000
#endif


/*
 * Flush instruction cache
 * *** I'm really paranoid here!
 */
_GLOBAL(flush_instruction_cache)
	mflr	r5
	bl	flush_data_cache
	mfspr	r3,HID0	/* Caches are controlled by this register */
	li	r4,0
	ori	r4,r4,(HID0_ICE|HID0_ICFI)
	or	r3,r3,r4	/* Need to enable+invalidate to clear */
	mtspr	HID0,r3
	andc	r3,r3,r4
	ori	r3,r3,HID0_ICE	/* Enable cache */
	mtspr	HID0,r3
	mtlr	r5
	blr

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

/*
 * Flush a particular page from the DATA cache
 * Note: this is necessary because the instruction cache does *not*
 * snoop from the data cache.
 *	void flush_page(void *page)
 */
_GLOBAL(flush_page)
	li	r4,0x0FFF
	andc	r3,r3,r4		/* Get page base address */
	li	r4,4096/CACHE_LINE_SIZE	/* Number of lines in a page */
	mtctr	r4
00:	dcbf	0,r3			/* Clear line */
	icbi	0,r3
	addi	r3,r3,CACHE_LINE_SIZE
	bdnz	00b
	blr

/*
 * Execute a [foreign] function
 *
 * run(p1, p2, cp, ep, entry)
 *
 */
 	.globl	run
run:
	mtctr	r7			/* Entry point */
#define IS_PreP 0x50726550		/* 'PreP' */	
	lis	r30,IS_PreP>>16
	ori	r30,r30,IS_PreP&0xFFFF
	mr	11,r5
	mr	12,r6
	mr	r28,r5
	mr	r29,r6
	mr	11,r5
	mr	12,r6
	bctr

	.comm	.stack,4096*2,4
