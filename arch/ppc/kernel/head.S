#include "ppc_asm.tmpl"
#include "ppc_defs.h"

#define SYNC() \
	isync; \
	sync

/* #define TLB_STATS /* Trace TLB exceptions */

/* Keep track of low-level exceptions - rather crude, but informative */	
#define STATS

/*
 * Increment a [64 bit] statistic counter
 * Uses R2, R3
 */
#define BUMP(ctr) /*\
	lis	r2,ctr@h; \
	ori	r2,r2,ctr@l; \
	lwz	r3,4(r2); \
	addic	r3,r3,1; \
	stw	r3,4(r2); \
	lwz	r3,0(r2); \
	addze	r3,r3; \
	stw	r3,0(r2)*/

/* The same as 'BUMP' but running unmapped (TLB code) */	
#define BUMP_UNMAPPED(ctr) /*\
	mfspr	r0,XER; \
	lis	r2,ctr@h; \
	ori	r2,r2,ctr@l; \
	lis	r3,0xF000; \
	andc	r2,r2,r3; \
	lwz	r3,4(r2); \
	addic	r3,r3,1; \
	stw	r3,4(r2); \
	lwz	r3,0(r2); \
	addze	r3,r3; \
	mtspr	XER,r0; \
	stw	r3,0(r2)*/

/* These macros can be used to generate very raw traces of low-level */
/* operations (where printf, etc. can't help).  All they provide is */
/* some after-the-fact documentation of what took place.  Since [in */
/* most instances] they have no registers to work with, they use the */
/* hardware "special" registers SPRx for storage.  Because of this, */
/* defining more than one of them simultaneously will yield incorrect */
/* results and a non-functional system.  Note: the trick here is to */
/* gather some data without disturbing anything - Heisenberg are you watching? */

/* CAUTION! Don't turn on more than one of these at once! */	
/* #define DO_TRAP_TRACE   */
/* #define DO_TLB_TRACE    */
/* #define DO_RFI_TRACE   */

#ifdef DO_RFI_TRACE
#define DO_RFI_TRACE_UNMAPPED(mark) \
	mtspr	SPR0,r1; \
	mtspr	SPR1,r2; \
	mtspr	SPR2,r3; \
	mfcr	r3; \
	mtspr	SPR3,r3; \
	lis	r1,_RFI_ptr@h; \
	ori	r1,r1,_RFI_ptr@l; \
	lis	r3,0xF000; \
	andc	r1,r1,r3; \
	lwz	r1,0(r1); \
	andc	r1,r1,r3; \
	subi	r1,r1,4; \
	lis	r2,(mark>>16); \
	ori	r2,r2,(mark&0xFFFF); \
	stwu	r2,4(r1); \
	mfspr	r2,SRR0; \
	stwu	r2,4(r1); \
	mfspr	r2,SRR1; \
	stwu	r2,4(r1); \
	addi	r1,r1,4+4; \
	lis	r2,_RFI_ptr@h; \
	ori	r2,r2,_RFI_ptr@l; \
	andc	r2,r2,r3; \
	stw	r1,0(r2); \
	mfspr	r3,SPR3; \
	mtcrf	0xFF,r3; \
	mfspr	r1,SPR0; \
	mfspr	r2,SPR1; \
	mfspr	r3,SPR2
#define DO_RFI_TRACE_MAPPED(mark) \
	mtspr	SPR0,r1; \
	mtspr	SPR1,r2; \
	mtspr	SPR2,r3; \
	mfcr	r3; \
	mtspr	SPR3,r3; \
	lis	r1,_RFI_ptr@h; \
	ori	r1,r1,_RFI_ptr@l; \
	lwz	r1,0(r1); \
	lis	r3,0x9000; \
	or	r1,r1,r3; \
	subi	r1,r1,4; \
	lis	r2,(mark>>16); \
	ori	r2,r2,(mark&0xFFFF); \
	stwu	r2,4(r1); \
	mfspr	r2,SRR0; \
	stwu	r2,4(r1); \
	mfspr	r2,SRR1; \
	stwu	r2,4(r1); \
	addi	r1,r1,4+4; \
	lis	r2,_RFI_ptr@h; \
	ori	r2,r2,_RFI_ptr@l; \
	stw	r1,0(r2); \
	mfspr	r3,SPR3; \
	mtcrf	0xFF,r3; \
	mfspr	r1,SPR0; \
	mfspr	r2,SPR1; \
	mfspr	r3,SPR2
#else
#define DO_RFI_TRACE_UNMAPPED(mark)
#define DO_RFI_TRACE_MAPPED(mark)
#endif

#ifdef DO_TRAP_TRACE
#define DEFAULT_TRAP(offset) \
	mtspr	SPR0,r1; \
	mtspr	SPR1,r2; \
	mtspr	SPR2,r3; \
	lis	r1,_TRAP_ptr@h; \
	ori	r1,r1,_TRAP_ptr@l; \
	lis	r3,0xF000; \
	andc	r1,r1,r3; \
	lwz	r1,0(r1); \
	andc	r1,r1,r3; \
	subi	r1,r1,4; \
	lis	r2,0xCACA; \
	ori	r2,r2,offset; \
	stwu	r2,4(r1); \
	mfspr	r2,SRR0; \
	stwu	r2,4(r1); \
	mfspr	r2,SRR1; \
	stwu	r2,4(r1); \
	mfspr	r2,SPR0; \
	stwu	r2,4(r1); \
	addi	r1,r1,4; \
	lis	r2,_TRAP_ptr@h; \
	ori	r2,r2,_TRAP_ptr@l; \
	andc	r2,r2,r3; \
	stw	r1,0(r2); \
	mfspr	r1,SPR0; \
	mfspr	r2,SPR1; \
	mfspr	r3,SPR2; \
	li	r13,0; \
	ori	r13,r13,HID0_ICE; \
	mtspr	HID0,r13; \
	lis	r13,0xFFF00000>>16; \
	ori	r13,r13,offset; \
	mtlr	r13; \
	b	hang
#define TRACE_TRAP(offset) \
	mtspr	SPR0,r1; \
	mtspr	SPR1,r2; \
	mtspr	SPR2,r3; \
	mfcr	r3; \
	mtspr	SPR3,r3; \
	lis	r1,_TRAP_ptr@h; \
	ori	r1,r1,_TRAP_ptr@l; \
	lis	r3,0xF000; \
	andc	r1,r1,r3; \
	lwz	r1,0(r1); \
	andc	r1,r1,r3; \
	subi	r1,r1,4; \
	lis	r2,0xCABB; \
	ori	r2,r2,offset; \
	stwu	r2,4(r1); \
	dcbst	0,r1; \
	mfspr	r2,SRR0; \
	stwu	r2,4(r1); \
	dcbst	0,r1; \
	mfspr	r2,SRR1; \
	stwu	r2,4(r1); \
	dcbst	0,r1; \
	li	r2,offset; \
	cmpi	0,r2,0x0C00; \
	beq	01f; \
	cmpi	0,r2,0x0300; \
	beq	00f; \
	cmpi	0,r2,0x0400; \
	beq	00f; \
	mfspr	r2,SPR0; \
	b	02f; \
00:	mfspr	r2,DAR; \
	b	02f; \
01:	mr	r2,r0; \
02:	stwu	r2,4(r1); \
	dcbst	0,r1; \
	addi	r1,r1,4; \
	mflr	r2; \
	stw	r2,0(r1); \
	bl	check_trace; \
	lwz	r2,0(r1); \
	mtlr	r2; \
02:	lis	r2,_TRAP_ptr@h; \
	ori	r2,r2,_TRAP_ptr@l; \
	oris	r1,r1,0x9000; \
	cmp	0,r1,r2; \
	bne	00f; \
	lis	r1,_TRAP_TRACE@h; \
	ori	r1,r1,_TRAP_TRACE@l; \
00:	lis	r3,0xF000; \
	andc	r2,r2,r3; \
	stw	r1,0(r2); \
	mfspr	r1,SPR0; \
	mfspr	r2,SPR1; \
	mfspr	r3,SPR3; \
	mtcrf	0xFF,r3; \
	mfspr	r3,SPR2
#else
#define DEFAULT_TRAP(offset) \
	li	r13,0; \
	ori	r13,r13,HID0_ICE; \
	mtspr	HID0,r13; \
	lis	r13,0xFFF00000>>16; \
	ori	r13,r13,offset; \
	mtlr	r13; \
	blr
#define TRACE_TRAP(offset)	
#endif

#define DATA_CACHE_OFF() \
	mfspr	r2,HID0; \
	li	r3,0; \
	ori	r3,r3,HID0_DCE; \
	andc	r2,r2,r3; \
	mtspr	HID0,r2;

#define DATA_CACHE_ON() \
	mfspr	r2,HID0; \
	ori	r2,r2,HID0_DCE; \
	mtspr	HID0,r2;

/* This instruction is not implemented on the PPC 603 */
#define tlbia \
	li	r4,32; \
	mtspr	CTR,r4; \
	lis	r4,0x9000; \
0:	tlbie	r4; \
	addi	r4,r4,0x1000; \
	bdnz	0b

/* Validate kernel stack - check for overflow */
#define CHECK_STACK()
#define _CHECK_STACK()\
	mtspr	SPR0,r3; \
	lis	r2,current@ha; \
	lwz	r2,current@l(r2); \
	lwz	r2,KERNEL_STACK_PAGE(r2); \
	lis	r3,sys_stack@h; \
	ori	r3,r3,sys_stack@l; \
	cmpl	0,r1,r3; \
	ble	02f; \
	li	r3,0x0FFF; \
	andc	r2,r2,r3; \
	andc	r3,r1,r3; \
	cmp	0,r3,r2; \
	beq	02f; \
	mr	r3,r1; \
	bl	_EXTERN(bad_stack); \
02:	mfspr	r3,SPR0

/* Save all registers on kernel stack during an exception */	
#define SAVE_REGS(mark) \
	subi	r1,r1,INT_FRAME_SIZE;	/* Make room for frame */ \
	stmw	r3,GPR3(r1);	/* Save R3..R31 */ \
	stw	r3,ORIG_GPR3(r1); \
	stw	r0,GPR0(r1); \
	mfspr	r2,SPR0; \
	stw	r2,GPR1(r1); \
	mfspr	r2,SPR1; \
	stw	r2,GPR2(r1); \
	mfspr	r2,SPR2; \
	stw	r2,_NIP(r1); \
	mfspr	r2,SPR3; \
	stw	r2,_MSR(r1); \
	mfctr	r2; \
	stw	r2,_CTR(r1); \
	mflr	r2; \
	stw	r2,_LINK(r1); \
	mfcr	r2; \
	stw	r2,_CCR(r1); \
	mfspr	r2,XER; \
	stw	r2,_XER(r1); \
	stfd	fr0,FPR0(r1); \
	stfd	fr1,FPR1(r1); \
	stfd	fr2,FPR2(r1); \
	stfd	fr3,FPR3(r1); \
	mffs	fr0; \
	stfd	fr0,FPCSR(r1); \
	lis	r2,_break_lwarx@h; \
	ori	r2,r2,_break_lwarx@l; \
	stwcx.	r2,0,r2; \
	li	r2,mark; \
	stw	r2,TRAP(r1); \
	lis	r2,0xDEAD; \
	ori	r2,r2,0xDEAD; \
	stw	r2,MARKER(r1); \
	li	r2,0; \
	stw	r2,RESULT(r1)

#define SAVE_PAGE_FAULT_REGS(offset) \
	mfspr	r2,DAR; \
	stw	r2,_DAR(r1); \
	mfspr	r2,DSISR; \
	stw	r2,_DSISR(r1); \
	mfspr	r2,HASH1; \
	stw	r2,_HASH1(r1); \
	mfspr	r2,HASH2; \
	stw	r2,_HASH2(r1); \
	mfspr	r2,IMISS; \
	stw	r2,_IMISS(r1); \
	mfspr	r2,DMISS; \
	stw	r2,_DMISS(r1); \
	mfspr	r2,ICMP; \
	stw	r2,_ICMP(r1); \
	mfspr	r2,DCMP; \
	stw	r2,_DCMP(r1)
	
#define SAVE_INT_REGS(mark) \
	mtspr	SPR0,r1;	/* Save current stack pointer */ \
	mtspr	SPR1,r2;	/* Scratch */ \
	mfcr	r2; \
	mtspr	SPR2,r2; \
	mfspr	r2,SRR1;	/* Interrupt from user/system mode */ \
	andi.	r2,r2,MSR_PR; \
	beq+	10f;		/* Jump if system - already have stack */ \
	mfspr	r2,SPR2;	/* Restore CCR */ \
	mtcrf	0xFF,r2; \
	mfspr	r2,SRR0;	/* Preserve interrupt registers */ \
	mtspr	SPR2,r2; \
	mfspr	r2,SRR1; \
	mtspr	SPR3,r2; \
	lis	r2,05f@h; \
	ori	r2,r2,05f@l; \
	mtspr	SRR0,r2; \
	mfmsr	r2; \
	ori	r2,r2,MSR_|MSR_DR|MSR_IR; \
	mtspr	SRR1,r2; \
	rfi; \
05:	lis	r2,current@ha; \
	lwz	r2,current@l(r2); \
	mfspr	r1,SPR2; \
	stw	r1,TSS+LAST_PC(r2); \
	lwz	r1,TSS+KSP(r2); \
	b	20f; \
10:	mfspr	r2,SPR2;	/* Restore CCR */ \
	mtcrf	0xFF,r2; \
	mfspr	r2,SRR0;	/* Preserve interrupt registers */ \
	mtspr	SPR2,r2; \
	mfspr	r2,SRR1; \
	mtspr	SPR3,r2; \
	lis	r2,20f@h; \
	ori	r2,r2,20f@l; \
	mtspr	SRR0,r2; \
	mfmsr	r2; \
	ori	r2,r2,MSR_|MSR_DR|MSR_IR; \
	mtspr	SRR1,r2; \
	SYNC(); \
	rfi; \
20:   SAVE_REGS(mark); \
	CHECK_STACK()

#define RETURN_FROM_INT(mark) \
90:	mfmsr	r0;		/* Disable interrupts */ \
	li	r4,0; \
	ori	r4,r4,MSR_EE; \
	andc	r0,r0,r4; \
	mtmsr	r0; \
	SYNC(); \
	lis	r2,intr_count@ha; /* Need to run 'bottom half' */ \
	lwz	r3,intr_count@l(r2); \
	cmpi	0,r3,0; \
	bne	00f; \
	lis	r4,bh_mask@ha; \
	lwz	r4,bh_mask@l(r4); \
	lis	r5,bh_active@ha; \
	lwz	r5,bh_active@l(r5); \
	and.	r4,r4,r5; \
	beq	00f; \
	addi	r3,r3,1; \
	stw	r3,intr_count@l(r2); \
	bl	_EXTERN(_do_bottom_half); \
	lis	r2,intr_count@ha; \
	lwz	r3,intr_count@l(r2); \
	subi	r3,r3,1; \
	stw	r3,intr_count@l(r2); \
00:	lwz	r2,_MSR(r1); /* Returning to user mode? */ \
	andi.	r2,r2,MSR_PR; \
	beq+	10f;		/* no - no need to mess with stack */ \
	lis	r2,kernel_pages_are_copyback@ha; \
	lwz	r2,kernel_pages_are_copyback@l(r2); \
	cmpi	0,r2,0; \
	beq	05f; \
	bl	_EXTERN(flush_instruction_cache); \
05:	lis	r3,current@ha;	/* need to save kernel stack pointer */ \
	lwz	r3,current@l(r3); \
	addi	r4,r1,INT_FRAME_SIZE;	/* size of frame */ \
	stw	r4,TSS+KSP(r3); \
	lwz	r4,STATE(r3);	/* If state != 0, can't run */ \
	cmpi	0,r4,0; \
	beq	06f; \
	bl	_EXTERN(schedule); \
	b	90b; \
06:	lwz	r4,COUNTER(r3);	/* Time quantum expired? */ \
	cmpi	0,r4,0; \
	bne	07f; \
	bl	_EXTERN(schedule); \
	b	90b; \
07:	lwz	r4,BLOCKED(r3);	/* Check for pending unblocked signals */ \
	lwz	r5,SIGNAL(r3); \
	andc.	r0,r5,r4;	/* Lets thru any unblocked */ \
	beq	10f; \
	mr	r3,r4; \
	mr	r4,r1; \
	bl	_EXTERN(do_signal); \
10:	lwz	r2,_NIP(r1);	/* Restore environment */ \
	mtspr	SRR0,r2; \
	lwz	r2,_MSR(r1); \
	mtspr	SRR1,r2; \
	lmw	r3,GPR3(r1); \
	lwz	r2,_CTR(r1); \
	mtctr	r2; \
	lwz	r2,_LINK(r1); \
	mtlr	r2; \
	lwz	r2,_XER(r1); \
	mtspr	XER,r2; \
	lwz	r2,_CCR(r1); \
	mtcrf	0xFF,r2; \
	lfd	fr0,FPCSR(r1); \
	mtfsf	0xFF,fr0; \
	lfd	fr0,FPR0(r1); \
	lfd	fr1,FPR1(r1); \
	lfd	fr2,FPR2(r1); \
	lfd	fr3,FPR3(r1); \
	lwz	r0,GPR0(r1); \
	lwz	r2,GPR2(r1); \
	lwz	r1,GPR1(r1); \
	SYNC(); \
	rfi

_TEXT()
	.globl	_start
_start:
	.globl  _stext
_stext:

hang:
	ori	r0,r0,0
	b	hang
	
_ORG(0x0100)

/* Hard Reset */
	.globl	HardReset
HardReset:
	b	Reset

_ORG(0x0200)
	b	MachineCheck

_ORG(0x0300)
	b	DataAccess

_ORG(0x0400)
	b	InstructionAccess

_ORG(0x0500)
	b	HardwareInterrupt
	
_ORG(0x0600)
	b	Alignment

_ORG(0x0700)
	b	ProgramCheck

_ORG(0x0800)
	b	FloatingPointCheck

/* Decrementer register - ignored for now... */
_ORG(0x0900)
/* TRACE_TRAP(0x900) */
	mtspr	SPR0,r1
	lis	r1,0x7FFF
	ori	r1,r1,0xFFFF
	mtspr	DEC,r1
	mfspr	r1,SPR0
#if 0	
	SYNC
#endif	
	rfi
	
_ORG(0x0A00)
DEFAULT_TRAP(0x0A00)	
_ORG(0x0B00)
DEFAULT_TRAP(0x0B00)

/*
 * System call
 */
_ORG(0x0C00)
	b	SystemCall

_ORG(0x0D00)
DEFAULT_TRAP(0x0D00)	
_ORG(0x0E00)
DEFAULT_TRAP(0x0E00)	
_ORG(0x0F00)
DEFAULT_TRAP(0x0F00)	

/*
 * Handle TLB Miss on an instruction load
 */
_ORG(0x1000)
/* Note: It is *unsafe* to use the TRACE TRAP macro here since there */
/* could be a 'trace' in progress when the TLB miss occurs.          */
/* TRACE_TRAP(0x1000) */
#ifdef TLB_STATS
	lis	r2,DataLoadTLB_trace_ptr@h
	ori	r2,r2,DataLoadTLB_trace_ptr@l
	lis	r3,0xF000
	andc	r2,r2,r3
	lwz	r1,0(r2)
	andc	r1,r1,r3
	li	r0,0x1000
	stw	r0,0(r1)
	mftbu	r0
	stw	r0,4(r1)
	mftb	r0
	stw	r0,8(r1)
	mfspr	r0,IMISS
	mfspr	r3,SRR1
	extrwi	r3,r3,1,14
	or	r0,r0,r3
	stw	r0,12(r1)
	addi	r1,r1,16
	mfcr	r0
	cmpl	0,r1,r2
	blt	00f
	lis	r1,DataLoadTLB_trace_buf@h
	ori	r1,r1,DataLoadTLB_trace_buf@l
	lis	r3,0xF000
	andc	r1,r1,r3
00:	mtcrf	0xFF,r0
	stw	r1,0(r2)
#endif	
	b	InstructionTLBMiss

/*
 * Handle TLB Miss on a data item load
 */
_ORG(0x1100)
/* TRACE_TRAP(0x1100) */
#ifdef TLB_STATS
	lis	r2,DataLoadTLB_trace_ptr@h
	ori	r2,r2,DataLoadTLB_trace_ptr@l
	lis	r3,0xF000
	andc	r2,r2,r3
	lwz	r1,0(r2)
	andc	r1,r1,r3
	li	r0,0x1100
	stw	r0,0(r1)
	mftbu	r0
	stw	r0,4(r1)
	mftb	r0
	stw	r0,8(r1)
	mfspr	r0,DMISS
	mfspr	r3,SRR1
	extrwi	r3,r3,1,14
	or	r0,r0,r3
	stw	r0,12(r1)
	addi	r1,r1,16
	mfcr	r0
	cmpl	0,r1,r2
	blt	00f
	lis	r1,DataLoadTLB_trace_buf@h
	ori	r1,r1,DataLoadTLB_trace_buf@l
	lis	r3,0xF000
	andc	r1,r1,r3
00:	mtcrf	0xFF,r0
	stw	r1,0(r2)
	.data
DataLoadTLB_trace_buf:
	.space	64*1024*4
DataLoadTLB_trace_ptr:
	.long	DataLoadTLB_trace_buf
	.text
#endif	
	b	DataLoadTLBMiss

/*
 * Handle TLB Miss on a store operation
 */
_ORG(0x1200)
/* TRACE_TRAP(0x1200) */
#ifdef TLB_STATS
	lis	r2,DataLoadTLB_trace_ptr@h
	ori	r2,r2,DataLoadTLB_trace_ptr@l
	lis	r3,0xF000
	andc	r2,r2,r3
	lwz	r1,0(r2)
	andc	r1,r1,r3
	li	r0,0x1200
	stw	r0,0(r1)
	mftbu	r0
	stw	r0,4(r1)
	mftb	r0
	stw	r0,8(r1)
	mfspr	r0,DMISS
	mfspr	r3,SRR1
	extrwi	r3,r3,1,14
	or	r0,r0,r3
	stw	r0,12(r1)
	addi	r1,r1,16
	mfcr	r0
	cmpl	0,r1,r2
	blt	00f
	lis	r1,DataLoadTLB_trace_buf@h
	ori	r1,r1,DataLoadTLB_trace_buf@l
	lis	r3,0xF000
	andc	r1,r1,r3
00:	mtcrf	0xFF,r0
	stw	r1,0(r2)
#endif	
	b	DataStoreTLBMiss

_ORG(0x1300)
InstructionAddressBreakpoint:
	DEFAULT_TRAP(0x1300)

_ORG(0x1400)
SystemManagementInterrupt:
	DEFAULT_TRAP(0x1400)

_ORG(0x1500)

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
#define NUM_CACHE_LINES 128*2
#define CACHE_LINE_SIZE 32 
cache_flush_buffer:
	.space	NUM_CACHE_LINES*CACHE_LINE_SIZE	/* CAUTION! these need to match hardware */

#if NUM_CACHE_LINES < 512	
_ORG(0x4000)
#endif

/*
 * Hardware reset [actually from bootstrap]
 * Initialize memory management & call secondary init
 */	
Reset:
	lis	r7,0xF000		/* To mask upper 4 bits */
/* Copy argument string */
	li	r0,0		/* Null terminate string */
	stb	r0,0(r12)
	lis	r1,cmd_line@h
	ori	r1,r1,cmd_line@l
	andc	r1,r1,r7	/* No MMU yet - need unmapped address */
	subi	r1,r1,1
	subi	r11,r11,1
00:	lbzu	r0,1(r11)
	cmpi	0,r0,0
	stbu	r0,1(r1)
	bne	00b	
	lis	r1,sys_stack@h
	ori	r1,r1,sys_stack@l
	li	r2,0x0FFF	/* Mask stack address down to page boundary */
	andc	r1,r1,r2
	subi	r1,r1,INT_FRAME_SIZE	/* Padding for first frame */
	li	r2,0		/* TOC pointer for nanokernel */
	li	r0,MSR_		/* Make sure FPU enabled */
	mtmsr	r0
	lis	r3,_edata@h	/* Clear BSS */
	ori	r3,r3,_edata@l
	andc	r3,r3,r7	/* make unmapped address */
	lis	r4,_end@h
	ori	r4,r4,_end@l
	andc	r4,r4,r7	/* make unmapped address */
	subi	r3,r3,4
	li	r0,0
00:	stwu	r0,4(r3)
	cmp	0,r3,r4
	blt	00b
/* Initialize BAT registers */
	lis	r3,BAT0@h
	ori	r3,r3,BAT0@l
	andc	r3,r3,r7	/* make unmapped address */
	lwz	r0,0(r3)
	mtspr	IBAT0U,r0
	mtspr	DBAT0U,r0
	lwz	r0,4(r3)
	mtspr	IBAT0L,r0
	mtspr	DBAT0L,r0
	lis	r3,BAT1@h
	ori	r3,r3,BAT1@l
	andc	r3,r3,r7	/* make unmapped address */
	lwz	r0,0(r3)
	mtspr	IBAT1U,r0
	mtspr	DBAT1U,r0
	lwz	r0,4(r3)
	mtspr	IBAT1L,r0
	mtspr	DBAT1L,r0
	lis	r3,TMP_BAT2@h
	ori	r3,r3,TMP_BAT2@l
	andc	r3,r3,r7	/* make unmapped address */
	lwz	r0,0(r3)
	mtspr	IBAT2U,r0
	mtspr	DBAT2U,r0
	lwz	r0,4(r3)
	mtspr	IBAT2L,r0
	mtspr	DBAT2L,r0
/* Now we can turn on the MMU */
	mfmsr	r3
	ori	r3,r3,MSR_DR|MSR_IR
	mtspr	SRR1,r3
	lis	r3,10f@h
	ori	r3,r3,10f@l
	mtspr	SRR0,r3
DO_RFI_TRACE_UNMAPPED(0xDEAD0000)	
	SYNC
	rfi				/* enables MMU */
10:	bl	_EXTERN(MMU_init)	/* initialize MMU environment */
DO_RFI_TRACE_MAPPED(0xDEAD0100)	
/* Withdraw BAT2->RAM mapping */
#if 1
	lis	r7,0xF000		/* To mask upper 4 bits */
	lis	r3,20f@h
	ori	r3,r3,20f@l
	andc	r3,r3,r7	/* make unmapped address */
	mtspr	SRR0,r3
	mfmsr	r3
	li	r4,MSR_DR|MSR_IR
	andc	r3,r3,r4
	mtspr	SRR1,r3
	SYNC
DO_RFI_TRACE_MAPPED(0xDEAD0200)	
	SYNC
	rfi
20:
DO_RFI_TRACE_UNMAPPED(0xDEAD0400)	
20:	lis	r3,BAT2@h
	ori	r3,r3,BAT2@l
	andc	r3,r3,r7	/* make unmapped address */
	lwz	r0,0(r3)
	mtspr	IBAT2U,r0
	mtspr	DBAT2U,r0
	lwz	r0,4(r3)
	mtspr	IBAT2L,r0
	mtspr	DBAT2L,r0
#endif

/* Load up the kernel context */
	lis	r2,init_task@h
	ori	r2,r2,init_task@l
	addi	r2,r2,TSS
	andc	r2,r2,r7	/* make unmapped address */
	SYNC			/* Force all PTE updates to finish */
	tlbia			/* Clear all TLB entries */
	lis	r3,_SDR1@h
	ori	r3,r3,_SDR1@l
	andc	r3,r3,r7	/* make unmapped address */
	lwz	r3,0(r3)
	mtspr	SDR1,r3
	lwz	r0,MMU_SEG0(r2)
	mtsr	SR0,r0
	lwz	r0,MMU_SEG1(r2)
	mtsr	SR1,r0
	lwz	r0,MMU_SEG2(r2)
	mtsr	SR2,r0
	lwz	r0,MMU_SEG3(r2)
	mtsr	SR3,r0
	lwz	r0,MMU_SEG4(r2)
	mtsr	SR4,r0
	lwz	r0,MMU_SEG5(r2)
	mtsr	SR5,r0
	lwz	r0,MMU_SEG6(r2)
	mtsr	SR6,r0
	lwz	r0,MMU_SEG7(r2)
	mtsr	SR7,r0
	lwz	r0,MMU_SEG8(r2)
	mtsr	SR8,r0
	lwz	r0,MMU_SEG9(r2)
	mtsr	SR9,r0
	lwz	r0,MMU_SEG10(r2)
	mtsr	SR10,r0
	lwz	r0,MMU_SEG11(r2)
	mtsr	SR11,r0
	lwz	r0,MMU_SEG12(r2)
	mtsr	SR12,r0
	lwz	r0,MMU_SEG13(r2)
	mtsr	SR13,r0
	lwz	r0,MMU_SEG14(r2)
	mtsr	SR14,r0
	lwz	r0,MMU_SEG15(r2)
	mtsr	SR15,r0
/* Now turn on the MMU for real! */
	mfmsr	r3
	ori	r3,r3,MSR_DR|MSR_IR
	mtspr	SRR1,r3
	lis	r3,30f@h
	ori	r3,r3,30f@l
	mtspr	SRR0,r3
DO_RFI_TRACE_UNMAPPED(0xDEAD0500)	
	SYNC
	rfi				/* enables MMU */
30:
DO_RFI_TRACE_MAPPED(0xDEAD0600)
/* Turn on L1 Data Cache */
	mfspr	r3,HID0		/* Caches are controlled by this register */
	ori	r4,r3,(HID0_ICE|HID0_ICFI)
	ori	r3,r3,(HID0_ICE)
	ori	r4,r4,(HID0_DCE|HID0_DCI)
	ori	r3,r3,(HID0_DCE)
	sync
	mtspr	HID0,r4
	mtspr	HID0,r3
/* L1 cache enable */	
	b	_EXTERN(start_kernel)		/* call main code */
	.long	0		# Illegal!

/*
 * Machine Check (Bus Errors, etc)
 */
MachineCheck:	
	TRACE_TRAP(0x0200)
	SAVE_INT_REGS(0x0200)
	mr	r3,r1		/* Set pointer to saved regs */
	bl	_EXTERN(MachineCheckException)
	RETURN_FROM_INT(0x0200)

/*
 * Data Access exception
 */
DataAccess:
	TRACE_TRAP(0x0300)
	SAVE_INT_REGS(0x0300)
	SAVE_PAGE_FAULT_REGS(0x0300)
	BUMP(__Data_Page_Faults)
	mr	r3,r1		/* Set pointer to saved regs */
	bl	_EXTERN(DataAccessException)
	RETURN_FROM_INT(0x0300)

/*
 * Instruction Access Exception
 */
InstructionAccess:
	TRACE_TRAP(0x0400)
	SAVE_INT_REGS(0x0400)
	SAVE_PAGE_FAULT_REGS(0x0400)
	BUMP(__Instruction_Page_Faults)
	mr	r3,r1		/* Set pointer to saved regs */
	bl	_EXTERN(InstructionAccessException)
	bl	_EXTERN(flush_instruction_cache)
	RETURN_FROM_INT(0x0400)

/*
 * Hardware Interrupt
 */
HardwareInterrupt:	
	SAVE_INT_REGS(0x0500)
	BUMP(__Hardware_Interrupts)
	mr	r3,r1		/* Set pointer to saved regs */
	bl	_EXTERN(handle_IRQ)
	RETURN_FROM_INT(0x0500)

/*
 * Alignment
 */
Alignment:	
	TRACE_TRAP(0x0600)
	SAVE_INT_REGS(0x0600)
	mr	r3,r1		/* Set pointer to saved regs */
	bl	_EXTERN(AlignmentException)
	RETURN_FROM_INT(0x0600)

/*
 * Illegal instruction
 */
ProgramCheck:
	TRACE_TRAP(0x0700)
	SAVE_INT_REGS(0x0700)
	mr	r3,r1		/* Set pointer to saved regs */
	bl	_EXTERN(ProgramCheckException)
	RETURN_FROM_INT(0x0700)

/*
 * Floating point [not available, etc]
 */
FloatingPointCheck:	
	TRACE_TRAP(0x0800)
	SAVE_INT_REGS(0x0800)
	mr	r3,r1		/* Set pointer to saved regs */
	bl	_EXTERN(FloatingPointCheckException)
	RETURN_FROM_INT(0x0200)

/*
 * System Call exception
 */	
SystemCall:
	SAVE_INT_REGS(0x0C00)
	lwz	r2,_CCR(r1)	/* Clear SO bit in CR */
	lis	r9,0x1000
	andc	r2,r2,r9
	stw	r2,_CCR(r1)
	cmpi	0,r0,0x7777	/* Special case for 'sys_sigreturn' */
	bne+	10f
	mr	r3,r1
	bl	_EXTERN(sys_sigreturn)
	cmpi	0,r3,0		/* Check for restarted system call */
	bge	99f
	b	20f
10:	lis	r2,sys_call_table@h
	ori	r2,r2,sys_call_table@l
	slwi	r0,r0,2
	lwzx	r2,r2,r0	/* Fetch system call handler [ptr] */
 
	mtlr	r2
	mr	r9,r1
	blrl			/* Call handler */
20:	stw	r3,RESULT(r1)	/* Save result */	
	cmpi	0,r3,0
	bge	30f
	neg	r3,r3
	lwz	r2,_CCR(r1)	/* Set SO bit in CR */
	oris	r2,r2,0x1000
	stw	r2,_CCR(r1)
30:	stw	r3,GPR3(r1)	/* Update return value */
99:
	RETURN_FROM_INT(0x0C00)

/*
 * Handle TLB miss for instruction
 */
InstructionTLBMiss:
	BUMP_UNMAPPED(__Instruction_TLB_Misses)
#ifdef DO_TLB_TRACE
	lis	r1,_TLB_ptr@h
	ori	r1,r1,_TLB_ptr@l
	lis	r2,0xF000
	andc	r1,r1,r2
	lwz	r1,0(r1)
	andc	r1,r1,r2
	subi	r1,r1,4
	lis	r2,0xBEBE
	ori	r2,r2,0x0100
	stwu	r2,4(r1)
	mfspr	r2,SRR0
	stwu	r2,4(r1)
	mfspr	r2,SRR1
	stwu	r2,4(r1)
	mfspr	r2,HASH1
	stwu	r2,4(r1)
	mfspr	r2,HASH2
	stwu	r2,4(r1)
	mfspr	r2,ICMP
	stwu	r2,4(r1)
	mfspr	r2,IMISS
	stwu	r2,4(r1)
	addi	r1,r1,4+(1*4)
	lis	r3,_TLB_ptr@h
	ori	r3,r3,_TLB_ptr@l
	lis	r2,0xF000
	andc	r3,r3,r2
	stw	r1,0(r3)
#endif	
	mfctr	r0		/* Need to save this - CTR can't be touched! */
	mfspr	r2,HASH1	/* Get PTE pointer */
	mfspr	r3,ICMP		/* Partial item compare value */
00:	li	r1,8		/* 8 items / bucket */
	mtctr	r1
	subi	r2,r2,8		/* Preset pointer */
10:	lwzu	r1,8(r2)	/* Get next PTE */
	cmp	0,r1,r3		/* Found entry yet? */
	bdne	10b		/* Jump back if not, until CTR==0 */
	bne	30f		/* Try secondary hash if CTR==0 */
	lwz	r1,4(r2)	/* Get second word of entry */
#if 0	
	andi.	r3,r1,0x08	/* Check guard bit - invalid access if set */
	bne	InstructionFetchError
#endif	
	andi.	r3,r1,0x100	/* Check R bit (referenced) */
	bne	20f		/* If set, all done */
	ori	r1,r1,0x100	/* Set bit */
	stw	r1,4(r2)	/* Update memory image */
20:	mtctr	r0		/* Restore CTR */
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	mfspr	r0,IMISS	/* Set to update TLB */
	mtspr	RPA,r1
	tlbli	r0
#if 0	
	SYNC
#endif
	rfi			/* All done */
/* Secondary hash */
30:	andi.	r1,r3,0x40	/* Already doing secondary hash? */
	bne	InstructionAddressInvalid /* Yes - item not in hash table */
	mfspr	r2,HASH2	/* Get hash table pointer */
	ori	r3,r3,0x40	/* Set secondary hash */
	b	00b			/* Try lookup again */

/*
 * Handle TLB miss for DATA Load operation
 */	
DataLoadTLBMiss:
	BUMP_UNMAPPED(__DataLoad_TLB_Misses)
#ifdef DO_TLB_TRACE
	lis	r1,_TLB_ptr@h
	ori	r1,r1,_TLB_ptr@l
	lis	r2,0xF000
	andc	r1,r1,r2
	lwz	r1,0(r1)
	andc	r1,r1,r2
	subi	r1,r1,4
	lis	r2,0xBEBE
	ori	r2,r2,0x0200
	stwu	r2,4(r1)
	mfspr	r2,SRR0
	stwu	r2,4(r1)
	mfspr	r2,SRR1
	stwu	r2,4(r1)
	mfspr	r2,HASH1
	stwu	r2,4(r1)
	mfspr	r2,HASH2
	stwu	r2,4(r1)
	mfspr	r2,DCMP
	stwu	r2,4(r1)
	mfspr	r2,DMISS
	stwu	r2,4(r1)
	addi	r1,r1,4+(1*4)
	lis	r3,_TLB_ptr@h
	ori	r3,r3,_TLB_ptr@l
	lis	r2,0xF000
	andc	r3,r3,r2
	stw	r1,0(r3)
#endif	
	mfctr	r0		/* Need to save this - CTR can't be touched! */
	mfspr	r2,HASH1	/* Get PTE pointer */
	mfspr	r3,DCMP		/* Partial item compare value */
00:	li	r1,8		/* 8 items / bucket */
	mtctr	r1
	subi	r2,r2,8		/* Preset pointer */
10:	lwzu	r1,8(r2)	/* Get next PTE */
	cmp	0,r1,r3		/* Found entry yet? */
	bdne	10b		/* Jump back if not, until CTR==0 */
	bne	30f		/* Try secondary hash if CTR==0 */
	lwz	r1,4(r2)	/* Get second word of entry */
	andi.	r3,r1,0x100	/* Check R bit (referenced) */
	ori	r1,r1,0x100	/* Set bit */
	bne	20f		/* If set, all done */
	stw	r1,4(r2)	/* Update memory image */
20:	mtctr	r0		/* Restore CTR */
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	mfspr	r0,DMISS	/* Set to update TLB */
	mtspr	RPA,r1
/*	SYNC() */
	tlbld	r0
#if 0	
	SYNC
#endif	
	rfi			/* All done */
/* Secondary hash */
30:	andi.	r1,r3,0x40	/* Already doing secondary hash? */
	bne	DataAddressInvalid /* Yes - item not in hash table */
	mfspr	r2,HASH2	/* Get hash table pointer */
	ori	r3,r3,0x40	/* Set secondary hash */
	b	00b			/* Try lookup again */

/*
 * Handle TLB miss for DATA STORE
 */
DataStoreTLBMiss:
	BUMP_UNMAPPED(__DataStore_TLB_Misses)
#ifdef DO_TLB_TRACE
	lis	r1,_TLB_ptr@h
	ori	r1,r1,_TLB_ptr@l
	lis	r2,0xF000
	andc	r1,r1,r2
	lwz	r1,0(r1)
	andc	r1,r1,r2
	subi	r1,r1,4
	lis	r2,0xBEBE
	ori	r2,r2,0x0300
	stwu	r2,4(r1)
	mfspr	r2,SRR0
	stwu	r2,4(r1)
	mfspr	r2,SRR1
	stwu	r2,4(r1)
	mfspr	r2,HASH1
	stwu	r2,4(r1)
	mfspr	r2,HASH2
	stwu	r2,4(r1)
	mfspr	r2,DCMP
	stwu	r2,4(r1)
	mfspr	r2,DMISS
	stwu	r2,4(r1)
	addi	r1,r1,4+(1*4)
	lis	r3,_TLB_ptr@h
	ori	r3,r3,_TLB_ptr@l
	lis	r2,0xF000
	andc	r3,r3,r2
	stw	r1,0(r3)
#endif	
	mfctr	r0		/* Need to save this - CTR can't be touched! */
	mfspr	r2,HASH1	/* Get PTE pointer */
	mfspr	r3,DCMP		/* Partial item compare value */
00:	li	r1,8		/* 8 items / bucket */
	mtctr	r1
	subi	r2,r2,8		/* Preset pointer */
10:	lwzu	r1,8(r2)	/* Get next PTE */
	cmp	0,r1,r3		/* Found entry yet? */
	bdne	10b		/* Jump back if not, until CTR==0 */
	bne	30f		/* Try secondary hash if CTR==0 */
	lwz	r1,4(r2)	/* Get second word of entry */
	andi.	r3,r1,0x80	/* Check C bit (changed) */
#if 0 /* Note: no validation */
	beq	40f		/* If not set (first time) validate access */
#else
	ori	r1,r1,0x180	/* Set changed, accessed */
	bne	20f
	stw	r1,4(r2)
#endif	
20:	mtctr	r0		/* Restore CTR */
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	mfspr	r0,DMISS	/* Set to update TLB */
	mtspr	RPA,r1
	tlbld	r0
#if 0	
	SYNC
#endif	
	rfi			/* All done */	
/* Secondary hash */
30:	andi.	r1,r3,0x40	/* Already doing secondary hash? */
	bne	DataAddressInvalid /* Yes - item not in hash table */
	mfspr	r2,HASH2	/* Get hash table pointer */
	ori	r3,r3,0x40	/* Set secondary hash */
	b	00b			/* Try lookup again */
/* PTE found - validate access */
40:	rlwinm. r3,r1,30,0,1	/* Extract PP bits */
	bge-	50f		/* Jump if PP=0,1 */
	andi.	r3,r1,1
	beq+	70f		/* Access OK */
	b	WriteProtectError	/* Not OK - fail! */
50:	mfspr	r3,SRR1		/* Check privilege */
	andi.	r3,r3,MSR_PR
	beq+	60f		/* Jump if supervisor mode */
	mfspr	r3,DMISS	/* Get address */
	mfsrin	r3,r3		/* Get segment register */
	andis.	r3,r3,0x2000	/* If Kp==0, OK */
	beq+	70f
	b	WriteProtectError	/* Bad access */
60:	mfspr	r3,DMISS	/* Get address */
	mfsrin	r3,r3		/* Get segment register */
	andis.	r3,r3,0x4000	/* If Ks==0, OK */
	beq+	70f
	b	WriteProtectError	/* Bad access */
70:	ori	r1,r1,0x180	/* Set changed, accessed */
	stw	r1,4(r2)	/* Update PTE in memory */
	b	20b
	
/*
 * These routines are error paths/continuations of the exception
 * handlers above.  They are placed here to avoid the problems
 * of only 0x100 bytes per exception handler.
 */
 
/* Invalid address */
InstructionAddressInvalid:
	mfspr	r3,SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */
	addis	r1,r1,0x4000	/* Set bit 1 -> PTE not found */
	b	10f

/* Fetch from guarded or no-access page */
InstructionFetchError:
	mfspr	r3,SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */
	addis	r1,r1,0x0800	/* Set bit 4 -> protection error */
10:	mtspr	DSISR,r1
	mtctr	r0		/* Restore CTR */
	andi.	r2,r3,0xFFFF	/* Clear upper bits of SRR1 */
	mtspr	SRR1,r2
	mfspr	r1,IMISS	/* Get failing address */
	rlwinm.	r2,r2,0,31,31	/* Check for little endian access */
	beq	20f		/* Jump if big endian */
	xori	r1,r1,3
20:	mtspr	DAR,r1		/* Set fault address */
	mfmsr	r0		/* Restore "normal" registers */
	xoris	r0,r0,MSR_TGPR>>16
	mtcrf	0x80,r3		/* Restore CR0 */
	ori	r0,r0,MSR_FP	/* Need to keep FP enabled */
	mtmsr	r0
	b	InstructionAccess

/* Invalid address */
DataAddressInvalid:
	mfspr	r3,SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */
	addis	r1,r1,0x4000	/* Set bit 1 -> PTE not found */
	b	10f

/* Write to read-only space */
WriteProtectError:
	mfspr	r3,SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */
	addis	r1,r1,0x0800	/* Set bit 4 -> protection error */
10:	mtspr	DSISR,r1
	mtctr	r0		/* Restore CTR */
	andi.	r2,r3,0xFFFF	/* Clear upper bits of SRR1 */
	mtspr	SRR1,r2
	mfspr	r1,DMISS	/* Get failing address */
	rlwinm.	r2,r2,0,31,31	/* Check for little endian access */
	beq	20f		/* Jump if big endian */
	xori	r1,r1,3
20:	mtspr	DAR,r1		/* Set fault address */
	mfmsr	r0		/* Restore "normal" registers */
	xoris	r0,r0,MSR_TGPR>>16
	mtcrf	0x80,r3		/* Restore CR0 */
	ori	r0,r0,MSR_FP	/* Need to keep FP enabled */
	mtmsr	r0
	b	DataAccess

/*
 * Flush instruction cache
 * *** I'm really paranoid here!
 */
_GLOBAL(flush_instruction_cache)
	mflr	r5
	bl	_EXTERN(flush_data_cache)
	mfspr	r3,HID0	/* Caches are controlled by this register */
	li	r4,0
	ori	r4,r4,(HID0_ICE|HID0_ICFI)
	andc	r3,r3,r4
	isync
	mtspr	HID0,r3		/* Disable cache */
	isync
	ori	r3,r3,HID0_ICFI
	isync
	mtspr	HID0,r3		/* Invalidate cache */
	isync
	andc	r3,r3,r4
	isync
	mtspr	HID0,r3		/* Invalidate (step 2) */
	isync
	ori	r3,r3,HID0_ICE
	isync
	mtspr	HID0,r3		/* Enable cache */
	isync
	mtlr	r5
	blr

/*
 * Flush data cache
 * *** I'm really paranoid here!
 */
_GLOBAL(flush_data_cache)
	BUMP(__Cache_Flushes)
	lis	r3,cache_is_copyback@ha
	lwz	r3,cache_is_copyback@l(r3)
	cmpi	0,r3,0
	beq	10f
/* When DATA CACHE is copy-back */
	lis	r3,cache_flush_buffer@h
	ori	r3,r3,cache_flush_buffer@l
	li	r4,NUM_CACHE_LINES
	mtctr	r4
00:	dcbz	0,r3			/* Flush cache line with minimal BUS traffic */
	addi	r3,r3,CACHE_LINE_SIZE	/* Next line, please */
	bdnz	00b	
10:	blr
	
/*
 * This routine switches between two different tasks.  The process
 * state of one is saved on its kernel stack.  Then the state
 * of the other is restored from its kernel stack.  The memory
 * management hardware is updated to the second process's state.
 * Finally, we can return to the second process, via the 'return'.
 *
 * Note: there are two ways to get to the "going out" portion
 * of this code; either by coming in via the entry (_switch)
 * or via "fork" which must set up an environment equivalent
 * to the "_switch" path.  If you change this (or in particular, the
 * SAVE_REGS macro), you'll have to change the fork code also.
 *
 * The code which creates the new task context is in 'copy_thread'
 * in arch/ppc/kernel/process.c
 */	
_GLOBAL(_switch)
	mtspr	SPR0,r1		/* SAVE_REGS prologue */
	mtspr	SPR1,r2
	mflr	r2		/* Return to switch caller */
	mtspr	SPR2,r2
	mfmsr	r2
	mtspr	SPR3,r2
	SAVE_REGS(0x0FF0)
	SYNC()
	stw	r1,KSP(r3)	/* Set old stack pointer */
	BUMP(__Context_Switches)
	lwz	r1,KSP(r4)	/* Load new stack pointer */
	CHECK_STACK()
	lwz	r0,MMU_SEG0(r4)
	mtsr	SR0,r0
	lwz	r0,MMU_SEG1(r4)
	mtsr	SR1,r0
	lwz	r0,MMU_SEG2(r4)
	mtsr	SR2,r0
	lwz	r0,MMU_SEG3(r4)
	mtsr	SR3,r0
	lwz	r0,MMU_SEG4(r4)
	mtsr	SR4,r0
	lwz	r0,MMU_SEG5(r4)
	mtsr	SR5,r0
	lwz	r0,MMU_SEG6(r4)
	mtsr	SR6,r0
	lwz	r0,MMU_SEG7(r4)
	mtsr	SR7,r0
	lwz	r0,MMU_SEG8(r4)
	mtsr	SR8,r0
	lwz	r0,MMU_SEG9(r4)
	mtsr	SR9,r0
	lwz	r0,MMU_SEG10(r4)
	mtsr	SR10,r0
	lwz	r0,MMU_SEG11(r4)
	mtsr	SR11,r0
	lwz	r0,MMU_SEG12(r4)
	mtsr	SR12,r0
	lwz	r0,MMU_SEG13(r4)
	mtsr	SR13,r0
	lwz	r0,MMU_SEG14(r4)
	mtsr	SR14,r0
	lwz	r0,MMU_SEG15(r4)
	mtsr	SR15,r0
	tlbia				/* Invalidate entire TLB */
	BUMP(__TLBIAs)
#ifdef TLB_STATS
/* TEMP */
	lis	r2,DataLoadTLB_trace_ptr@h
	ori	r2,r2,DataLoadTLB_trace_ptr@l
	lis	r3,0x9000
	lwz	r4,0(r2)
	or	r4,r4,r3
	li	r0,0
	stw	r0,0(r4)
	stw	r0,4(r4)
	stw	r0,8(r4)
	stw	r0,12(r4)
	addi	r4,r4,4
	cmpl	0,r4,r2
	blt	00f
	lis	r4,DataLoadTLB_trace_buf@h
	ori	r4,r4,DataLoadTLB_trace_buf@l
00:	stw	r4,0(r2)
/* TEMP */	
#endif
	lwz	r2,_NIP(r1)	/* Force TLB/MMU hit */
	lwz	r2,0(r2)
	RETURN_FROM_INT(0xF000)
	

/*
 * This routine is just here to keep GCC happy - sigh...
 */	
_GLOBAL(__main)
	blr

#ifdef DO_TRAP_TRACE
check_trace:
	sync				/* Force all writes out */
	lwz	r2,-8(r1)
	andi.	r2,r2,MSR_PR
	bne	99f
	lwz	r2,-32(r1)
	lwz	r3,-16(r1)
	cmp	0,r2,r3
	bne	99f
	andi.	r2,r2,0x7FFF
	cmpi	0,r2,0x0C00
	bge	99f
	lwz	r2,-32+4(r1)
	lwz	r3,-16+4(r1)
	cmp	0,r2,r3
	bne	99f
	lwz	r2,-32+8(r1)
	lwz	r3,-16+8(r1)
	cmp	0,r2,r3
	bne	99f
	lwz	r2,-32(r1)
	lwz	r3,-16(r1)
	cmp	0,r2,r3
	bne	99f
	andi.	r2,r2,0x7FFF
	cmpi	0,r2,0x0600
	beq	00f
	lwz	r2,-32+12(r1)
	lwz	r3,-16+12(r1)
	cmp	0,r2,r3
	bne	99f
00:	li	r2,0x7653
	stw	r2,0(r1)
	b	00b
99:	blr
#endif	

	.data
	.globl	sdata
sdata:
	.space	2*4096
sys_stack:

	.globl	empty_zero_page
empty_zero_page:
	.space	4096

/*
 * This space gets a copy of optional info passed to us by the bootstrap
 * Used to pass parameters into the kernel like root=/dev/sda1, etc.
 */	
	.globl	cmd_line
cmd_line:
	.space	512	

#ifdef STATS	
/*
 * Miscellaneous statistics - gathered just for performance info
 */

 	.globl	_INTR_stats
_INTR_stats:
__Instruction_TLB_Misses:
	.long	0,0	/* Instruction TLB misses */
__DataLoad_TLB_Misses:
	.long	0,0	/* Data [load] TLB misses */
__DataStore_TLB_Misses:
	.long	0,0	/* Data [store] TLB misses */
__Instruction_Page_Faults:	
	.long	0,0	/* Instruction page faults */
__Data_Page_Faults:	
	.long	0,0	/* Data page faults */
__Cache_Flushes:	
	.long	0,0	/* Explicit cache flushes */
__Context_Switches:	
	.long	0,0	/* Context switches */
__Hardware_Interrupts:	
	.long	0,0	/* I/O interrupts (disk, timer, etc) */
	.globl	__TLBIAs
__TLBIAs:
	.long	0,0	/* TLB cache forceably flushed */
	.globl	__TLBIEs
__TLBIEs:
	.long	0,0	/* Specific TLB entry flushed */	
#endif

/*
 * This location is used to break any outstanding "lock"s when
 * changing contexts.
 */
_break_lwarx:	.long	0

/*
 * Various trace buffers
 */	
#ifdef DO_TRAP_TRACE
	.data
_TRAP_TRACE: .space	32*1024
_TRAP_ptr:   .long	_TRAP_TRACE
	.text
#endif

#ifdef DO_TLB_TRACE
	.data
_TLB_TRACE: .space	128*1024
_TLB_ptr:   .long	_TLB_TRACE
	.text
#endif

#ifdef DO_RFI_TRACE
	.data
_RFI_DATA: .space	128*1024
_RFI_ptr:  .long	_RFI_DATA
	.text	
#endif
