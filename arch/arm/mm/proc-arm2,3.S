/*
 * linux/arch/arm/mm/arm2,3.S: MMU functions for ARM2,3
 *
 * (C) 1997 Russell King
 *
 * These are the low level assembler for performing cache
 * and memory functions on ARM2, ARM250 and ARM3 processors.
 */
#include <linux/linkage.h>

#include <asm/assembler.h>
#include "../lib/constants.h"

/*
 * Code common to all processors - MEMC specific not processor
 * specific!
 */

LC1:		.word	SYMBOL_NAME(page_nr)
/*
 * Function: arm2_3_update_map (struct task_struct *tsk)
 *
 * Params  : tsk	Task structure to be updated
 *
 * Purpose : Re-generate memc maps for task from its pseudo page tables
 */
_arm2_3_update_map:
		mov	ip, sp
		stmfd	sp!, {r4 - r6, fp, ip, lr, pc}
		sub	fp, ip, #4
		add	r1, r0, #TSS_MEMCMAP
		ldr	r2, LC1
		ldr	r2, [r2]
		mov	r3, #0x03f00000
		orr	r3, r3, #0x00000f00
		orr	r4, r3, #1
		orr	r5, r3, #2
		orr	r6, r3, #3
1:		stmia	r1!, {r3, r4, r5, r6}		@ Default mapping (null mapping)
		add	r3, r3, #4
		add	r4, r4, #4
		add	r5, r5, #4
		add	r6, r6, #4
		stmia	r1!, {r3, r4, r5, r6}		@ Default mapping (null mapping)
		add	r3, r3, #4
		add	r4, r4, #4
		add	r5, r5, #4
		add	r6, r6, #4
		subs	r2, r2, #8
		bhi	1b

		adr	r2, Lphystomemc32		@ r2 = conversion table to logical page number
		ldr	r4, [r0, #TSS_MEMMAP]		@ r4 = active mem map
		add	r5, r4, #32 << 2		@ r5 = end of active mem map
		add	r0, r0, #TSS_MEMCMAP		@ r0 = memc map

		mov	r6, #0
2:		ldmia	r4!, {r1, r3}
		tst	r1, #PAGE_PRESENT
		blne	update_map_pgd
		add	r6, r6, #32 << 2
		tst	r3, #PAGE_PRESENT
		blne	update_map_pgd3
		add	r6, r6, #32 << 2
		cmp	r4, r5
		blt	2b
		ldmea	fp, {r4 - r6, fp, sp, pc}^

@ r0,r2,r3,r4,r5 = preserve
@ r1,ip = available
@ r0 = memc map
@ r1 = pgd entry
@ r2 = conversion table
@ r6 = logical page no << 2

update_map_pgd3:
		mov	r1, r3
update_map_pgd: stmfd	sp!, {r3, r4, r5, lr}
		bic	r4, r1, #3			@ r4 = page table
		sub	r5, r6, #1 << 2
		add	ip, r4, #32 << 2		@ ip = end of page table

1:		ldr	r1, [r4], #4			@ get entry
		add	r5, r5, #1 << 2
		tst	r1, #PAGE_PRESENT		@ page present?
		blne	Lconvertmemc			@ yes
		ldr	r1, [r4], #4			@ get entry
		add	r5, r5, #1 << 2
		tst	r1, #PAGE_PRESENT		@ page present?
		blne	Lconvertmemc			@ yes
		ldr	r1, [r4], #4			@ get entry
		add	r5, r5, #1 << 2
		tst	r1, #PAGE_PRESENT		@ page present?
		blne	Lconvertmemc			@ yes
		ldr	r1, [r4], #4			@ get entry
		add	r5, r5, #1 << 2
		tst	r1, #PAGE_PRESENT		@ page present?
		blne	Lconvertmemc			@ yes
		cmp	r4, ip
		blt	1b
		ldmfd	sp!, {r3, r4, r5, pc}^

Lconvertmemc:	mov	r3, r1, lsr #13			@
		and	r3, r3, #0x3fc			@ Convert to memc physical page no
		ldr	r3, [r2, r3]			@

		tst	r1, #PAGE_OLD|PAGE_NOT_USER	@ check for MEMC read
		biceq	r3, r3, #0x200			@
		tsteq	r1, #PAGE_READONLY|PAGE_CLEAN	@ check for MEMC write
		biceq	r3, r3, #0x300			@

		orr	r3, r3, r5, lsl #13
		and	r1, r5, #0x01800000 >> 13
		orr	r3, r3, r1

		and	r1, r3, #255
		str	r3, [r0, r1, lsl #2]
		movs	pc, lr

/*
 * Function: arm2_3_update_cache (struct task_struct *tsk, unsigned long addr, pte_t pte)
 * Params  : tsk      Task to update
 *	     address  Address of fault.
 *	     pte      New PTE at address
 * Purpose : Update the mapping for this address.
 * Notes   : does the ARM3 run faster if you do not use the result in the next instruction?
 */
_arm2_3_update_cache:
		tst	r2, #PAGE_PRESENT
		moveqs	pc, lr
		mov	r3, r2, lsr #13			@ Physical page no.
		adr	ip, Lphystomemc32		@ Convert to logical page number
		and	r3, r3, #0x3fc
		mov	r1, r1, lsr #15
		ldr	r3, [ip, r3]			@ Convert to memc phys page no.
		tst	r2, #PAGE_OLD|PAGE_NOT_USER
		biceq	r3, r3, #0x200
		tsteq	r2, #PAGE_READONLY|PAGE_CLEAN
		biceq	r3, r3, #0x300
		mov	ip, sp, lsr #13
		orr	r3, r3, r1, lsl #15
		mov	ip, ip, lsl #13
		and	r1, r1, #0x300
		teq	ip, r0
		orr	r3, r3, r1, lsl #2
		add	r0, r0, #TSS_MEMCMAP
		and	r2, r3, #255
		streqb	r3, [r3]
		str	r3, [r0, r2, lsl #2]
		movs    pc, lr

#define PCD(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af) \
		.long	a0| 0x03800300; .long	a1| 0x03800300;\
		.long	a2| 0x03800300; .long	a3| 0x03800300;\
		.long	a4| 0x03800300; .long	a5| 0x03800300;\
		.long	a6| 0x03800300; .long	a7| 0x03800300;\
		.long	a8| 0x03800300; .long	a9| 0x03800300;\
		.long	aa| 0x03800300; .long	ab| 0x03800300;\
		.long	ac| 0x03800300; .long	ad| 0x03800300;\
		.long	ae| 0x03800300; .long	af| 0x03800300

@ Table to map from page number to vidc page number
Lphystomemc32:	PCD(0x00,0x08,0x10,0x18,0x20,0x28,0x30,0x38,0x40,0x48,0x50,0x58,0x60,0x68,0x70,0x78)
		PCD(0x01,0x09,0x11,0x19,0x21,0x29,0x31,0x39,0x41,0x49,0x51,0x59,0x61,0x69,0x71,0x79)
		PCD(0x04,0x0C,0x14,0x1C,0x24,0x2C,0x34,0x3C,0x44,0x4C,0x54,0x5C,0x64,0x6C,0x74,0x7C)
		PCD(0x05,0x0D,0x15,0x1D,0x25,0x2D,0x35,0x3D,0x45,0x4D,0x55,0x5D,0x65,0x6D,0x75,0x7D)
		PCD(0x02,0x0A,0x12,0x1A,0x22,0x2A,0x32,0x3A,0x42,0x4A,0x52,0x5A,0x62,0x6A,0x72,0x7A)
		PCD(0x03,0x0B,0x13,0x1B,0x23,0x2B,0x33,0x3B,0x43,0x4B,0x53,0x5B,0x63,0x6B,0x73,0x7B)
		PCD(0x06,0x0E,0x16,0x1E,0x26,0x2E,0x36,0x3E,0x46,0x4E,0x56,0x5E,0x66,0x6E,0x76,0x7E)
		PCD(0x07,0x0F,0x17,0x1F,0x27,0x2F,0x37,0x3F,0x47,0x4F,0x57,0x5F,0x67,0x6F,0x77,0x7F)
		PCD(0x80,0x88,0x90,0x98,0xA0,0xA8,0xB0,0xB8,0xC0,0xC8,0xD0,0xD8,0xE0,0xE8,0xF0,0xF8)
		PCD(0x81,0x89,0x91,0x99,0xA1,0xA9,0xB1,0xB9,0xC1,0xC9,0xD1,0xD9,0xE1,0xE9,0xF1,0xF9)
		PCD(0x84,0x8C,0x94,0x9C,0xA4,0xAC,0xB4,0xBC,0xC4,0xCC,0xD4,0xDC,0xE4,0xEC,0xF4,0xFC)
		PCD(0x85,0x8D,0x95,0x9D,0xA5,0xAD,0xB5,0xBD,0xC5,0xCD,0xD5,0xDD,0xE5,0xED,0xF5,0xFD)
		PCD(0x82,0x8A,0x92,0x9A,0xA2,0xAA,0xB2,0xBA,0xC2,0xCA,0xD2,0xDA,0xE2,0xEA,0xF2,0xFA)
		PCD(0x83,0x8B,0x93,0x9B,0xA3,0xAB,0xB3,0xBB,0xC3,0xCB,0xD3,0xDB,0xE3,0xEB,0xF3,0xFB)
		PCD(0x86,0x8E,0x96,0x9E,0xA6,0xAE,0xB6,0xBE,0xC6,0xCE,0xD6,0xDE,0xE6,0xEE,0xF6,0xFE)
		PCD(0x87,0x8F,0x97,0x9F,0xA7,0xAF,0xB7,0xBF,0xC7,0xCF,0xD7,0xDF,0xE7,0xEF,0xF7,0xFF)

/*
 * Function: arm2_3_data_abort ()
 *
 * Params  : r0 = address of aborted instruction
 *
 * Purpose :
 *
 * Returns : r0 = address of abort
 *	   : r1 = FSR
 *	   : r2 != 0 if writing
 */

_arm2_3_data_abort:
		movs	pc, lr

_arm2_3_check_bugs:
		movs	pc, lr

/*
 * Processor specific - ARM2
 */

LC0:		.word	SYMBOL_NAME(page_nr)
/*
 * Function: arm2_switch_to (struct task_struct *prev, struct task_struct *next)
 *
 * Params  : prev	Old task structure
 *	   : next	New task structure for process to run
 *
 * Purpose : Perform a task switch, saving the old processes state, and restoring
 *	     the new.
 *
 * Notes   : We don't fiddle with the FP registers here - we postpone this until
 *	     the new task actually uses FP.  This way, we don't swap FP for tasks
 *	     that do not require it.
 */
_arm2_switch_to:
		stmfd	sp!, {r4 - r9, fp, lr}		@ Store most regs on stack
		str	sp, [r0, #TSS_SAVE]		@ Save sp_SVC
		ldr	sp, [r1, #TSS_SAVE]		@ Get saved sp_SVC
		mov	r4, r1
		add	r0, r1, #TSS_MEMCMAP		@ Remap MEMC
		ldr	r1, LC0
		ldr	r1, [r1]
1:		ldmia	r0!, {r2, r3, r5, r6}
		strb	r2, [r2]
		strb	r3, [r3]
		strb	r5, [r5]
		strb	r6, [r6]
		ldmia	r0!, {r2, r3, r5, r6}
		strb	r2, [r2]
		strb	r3, [r3]
		strb	r5, [r5]
		strb	r6, [r6]
		subs	r1, r1, #8
		bhi	1b
		ldmfd	sp!, {r4 - r9, fp, pc}^		@ Load all regs saved previously

/*
 * Function: arm2_remap_memc (struct task_struct *tsk)
 *
 * Params  : tsk	Task structure specifing the new mapping structure
 *
 * Purpose : remap MEMC tables
 */
_arm2_remap_memc:
		stmfd	sp!, {lr}
		add	r0, r0, #TSS_MEMCMAP
		ldr	r1, LC0
		ldr	r1, [r1]
1:		ldmia	r0!, {r2, r3, ip, lr}
		strb	r2, [r2]
		strb	r3, [r3]
		strb	ip, [ip]
		strb	lr, [lr]
		ldmia	r0!, {r2, r3, ip, lr}
		strb	r2, [r2]
		strb	r3, [r3]
		strb	ip, [ip]
		strb	lr, [lr]
		subs	r1, r1, #8
		bhi	1b
		ldmfd	sp!, {pc}^

/*
 * Function: arm2_xchg_1 (int new, volatile void *ptr)
 *
 * Params  : new	New value to store at...
 *	   : ptr	pointer to byte-wide location
 *
 * Purpose : Performs an exchange operation
 *
 * Returns : Original byte data at 'ptr'
 *
 * Notes   : This will have to be changed if we ever use multi-processing using these
 *	     processors, but that is very unlikely...
 */
_arm2_xchg_1:	mov	r2, pc
		orr	r2, r2, #I_BIT
		teqp	r2, #0
		ldrb	r2, [r1]
		strb	r0, [r1]
		mov	r0, r2
		movs	pc, lr

/*
 * Function: arm2_xchg_4 (int new, volatile void *ptr)
 *
 * Params  : new	New value to store at...
 *	   : ptr	pointer to word-wide location
 *
 * Purpose : Performs an exchange operation
 *
 * Returns : Original word data at 'ptr'
 *
 * Notes   : This will have to be changed if we ever use multi-processing using these
 *	     processors, but that is very unlikely...
 */
_arm2_xchg_4:	mov	r2, pc
		orr	r2, r2, #I_BIT
		teqp	r2, #0
		ldr	r2, [r1]
		str	r0, [r1]
		mov	r0, r2
/*
 * fall through
 */
/*
 * Function: arm2_proc_init (void)
 *	   : arm2_proc_fin (void)
 *
 * Purpose : Initialise / finalise processor specifics (none required)
 */
_arm2_proc_init:
_arm2_proc_fin:	movs	pc, lr
/*
 * Function: arm3_switch_to (struct task_struct *prev, struct task_struct *next)
 *
 * Params  : prev	Old task structure
 *	   : next	New task structure for process to run
 *
 * Purpose : Perform a task switch, saving the old processes state, and restoring
 *	     the new.
 *
 * Notes   : We don't fiddle with the FP registers here - we postpone this until
 *	     the new task actually uses FP.  This way, we don't swap FP for tasks
 *	     that do not require it.
 */
_arm3_switch_to:
		stmfd	sp!, {r4 - r9, fp, lr}		@ Store most regs on stack
		str	sp, [r0, #TSS_SAVE]		@ Save sp_SVC
		ldr	sp, [r1, #TSS_SAVE]		@ Get saved sp_SVC
		mov	r4, r1
		add	r0, r1, #TSS_MEMCMAP		@ Remap MEMC
		ldr	r1, LC0
		ldr	r1, [r1]
1:		ldmia	r0!, {r2, r3, r5, r6}
		strb	r2, [r2]
		strb	r3, [r3]
		strb	r5, [r5]
		strb	r6, [r6]
		ldmia	r0!, {r2, r3, r5, r6}
		strb	r2, [r2]
		strb	r3, [r3]
		strb	r5, [r5]
		strb	r6, [r6]
		subs	r1, r1, #8
		bhi	1b
		mcr	p15, 0, r0, c1, c0, 0		@ flush cache
		ldmfd	sp!, {r4 - r9, fp, pc}^		@ Load all regs saved previously
/*
 * Function: arm3_remap_memc (struct task_struct *tsk)
 *
 * Params  : tsk	Task structure specifing the new mapping structure
 *
 * Purpose : remap MEMC tables
 */
_arm3_remap_memc:
		stmfd	sp!, {lr}
		add	r0, r0, #TSS_MEMCMAP
		ldr	r1, LC0
		ldr	r1, [r1]
1:		ldmia	r0!, {r2, r3, ip, lr}
		strb	r2, [r2]
		strb	r3, [r3]
		strb	ip, [ip]
		strb	lr, [lr]
		ldmia	r0!, {r2, r3, ip, lr}
		strb	r2, [r2]
		strb	r3, [r3]
		strb	ip, [ip]
		strb	lr, [lr]
		subs	r1, r1, #8
		bhi	1b
		mcr	p15, 0, r0, c1, c0, 0		@ flush cache
		ldmfd	sp!, {pc}^

/*
 * Function: arm3_proc_init (void)
 *
 * Purpose : Initialise the cache control registers
 */
_arm3_proc_init:
		mov	r0, #0x001f0000
		orr	r0, r0, #0x0000ff00
		orr	r0, r0, #0x000000ff
		mcr	p15, 0, r0, c3, c0		@ ARM3 Cacheable
		mcr     p15, 0, r0, c4, c0		@ ARM3 Updateable
		mov	r0, #0
		mcr     p15, 0, r0, c5, c0		@ ARM3 Disruptive
		mcr     p15, 0, r0, c1, c0		@ ARM3 Flush
		mov	r0, #3
		mcr     p15, 0, r0, c2, c0		@ ARM3 Control
		movs	pc, lr

/*
 * Function: arm3_proc_fin (void)
 *
 * Purpose : Finalise processor (disable caches)
 */
_arm3_proc_fin:	mov	r0, #2
		mcr	p15, 0, r0, c2, c0
		movs	pc, lr

/*
 * Function: arm3_xchg_1 (int new, volatile void *ptr)
 *
 * Params  : new	New value to store at...
 *	   : ptr	pointer to byte-wide location
 *
 * Purpose : Performs an exchange operation
 *
 * Returns : Original byte data at 'ptr'
 */
_arm3_xchg_1:	swpb	r0, r0, [r1]
		movs	pc, lr

/*
 * Function: arm3_xchg_4 (int new, volatile void *ptr)
 *
 * Params  : new	New value to store at...
 *	   : ptr	pointer to word-wide location
 *
 * Purpose : Performs an exchange operation
 *
 * Returns : Original word data at 'ptr'
 */
_arm3_xchg_4:	swp	r0, r0, [r1]
		movs	pc, lr


/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
_arm2_name:
		.ascii	"arm2\0"
		.align

		.globl	SYMBOL_NAME(arm2_processor_functions)
SYMBOL_NAME(arm2_processor_functions):
		.word	_arm2_name			@  0
		.word	_arm2_switch_to			@  4
		.word	_arm2_3_data_abort		@  8
		.word	_arm2_3_check_bugs		@ 12
		.word	_arm2_proc_init			@ 16
		.word	_arm2_proc_fin			@ 20

		.word	_arm2_remap_memc		@ 24
		.word	_arm2_3_update_map		@ 28
		.word	_arm2_3_update_cache		@ 32
		.word	_arm2_xchg_1			@ 36
		.word	SYMBOL_NAME(abort)		@ 40
		.word	_arm2_xchg_4			@ 44

_arm250_name:
		.ascii	"arm250\0"
		.align

		.globl	SYMBOL_NAME(arm250_processor_functions)
SYMBOL_NAME(arm250_processor_functions):
		.word	_arm250_name			@  0
		.word	_arm2_switch_to			@  4
		.word	_arm2_3_data_abort		@  8
		.word	_arm2_3_check_bugs		@ 12
		.word	_arm2_proc_init			@ 16
		.word	_arm2_proc_fin			@ 20

		.word	_arm2_remap_memc		@ 24
		.word	_arm2_3_update_map		@ 28
		.word	_arm2_3_update_cache		@ 32
		.word	_arm3_xchg_1			@ 36
		.word	SYMBOL_NAME(abort)		@ 40
		.word	_arm3_xchg_4			@ 44

_arm3_name:
		.ascii	"arm3\0"
		.align

		.globl	SYMBOL_NAME(arm3_processor_functions)
SYMBOL_NAME(arm3_processor_functions):
		.word	_arm3_name			@  0
		.word	_arm3_switch_to			@  4
		.word	_arm2_3_data_abort		@  8
		.word	_arm2_3_check_bugs		@ 12
		.word	_arm3_proc_init			@ 16
		.word	_arm3_proc_fin			@ 20

		.word	_arm3_remap_memc		@ 24
		.word	_arm2_3_update_map		@ 28
		.word	_arm2_3_update_cache		@ 32
		.word	_arm3_xchg_1			@ 36
		.word	SYMBOL_NAME(abort)		@ 40
		.word	_arm3_xchg_4			@ 44

