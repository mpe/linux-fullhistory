/*
 *
 *	Trampoline.S	Derived from Setup.S by Linus Torvalds
 *
 *	4 Jan 1997 Michael Chastain: changed to gnu as.
 *
 *	Entry: CS:IP point to the start of our code, we are 
 *	in real mode with no stack, but the rest of the 
 *	trampoline page to make our stack and everything else
 *	is a mystery.
 *
 *	In fact we don't actually need a stack so we don't
 *	set one up.
 *
 *	We jump into the boot/compressed/head.S code. So you'd
 *	better be running a compressed kernel image or you
 *	won't get very far.
 *
 *	On entry to trampoline_data, the processor is in real mode
 *	with 16-bit addressing and 16-bit data.  CS has some value
 *	and IP is zero.  Thus, data addresses need to be absolute
 *	(no relocation) and are taken with regard to r_base.
 *
 *	On the transition to protected mode, this page appears at
 *	address 8192, so protected mode addresses are with regard
 *	to p_base.
 *
 *	If you work on this file, check the object module with objdump
 *	--full-contents --reloc to make sure there are no relocation
 *	entries.
 */

#include <linux/linkage.h>
#include <asm/segment.h>

.data

.code16

ENTRY(trampoline_data)
r_base = .
p_base = . - 8192

	mov	%cs, %ax	# Code and data in the same place
	mov	%ax, %ds

	mov	%ax, %cx	# Pass stack info to the 32bit boot
	shl	$4, %cx		# Segment -> Offset
	add	$4096, %cx	# End of page is wanted

	mov	$1, %bx		# Flag an SMP trampoline
	cli			# We should be safe anyway

	movl	$0xA5A5A5A5, trampoline_data - r_base
				# write marker for master knows we're running

	lidt	idt_48 - r_base	# load idt with 0, 0
	lgdt	gdt_48 - r_base	# load gdt with whatever is appropriate

	xor	%ax, %ax
	inc	%ax		# protected mode (PE) bit
	lmsw	%ax		# into protected mode
	jmp	flush_instr
flush_instr:
	ljmp	$KERNEL_CS, $0x00100000
				# jump to startup_32

idt_48:
	.word	0			# idt limit = 0
	.word	0, 0			# idt base = 0L

gdt_48:
	.word	0x0800			# gdt limit = 2048, 256 GDT entries
	.word	gdt - p_base, 0x0	# gdt base = gdt (first SMP CPU)
					# we load the others with first table
					# saves rewriting gdt_48 for each
gdt:
	.word	0, 0, 0, 0	# dummy

	.word	0, 0, 0, 0	# unused

# walken modif

	.word	0xFFFF		# 4 Gb - (0x100000*0x1000 = 4Gb)
	.word	0x0000		# base address = 0
	.word	0x9A00		# code read / exec
	.word	0x00CF		# granularity = 4096, 386 (+5th nibble of limit)

	.word	0xFFFF		# 4 Gb - (0x100000*0x1000 = 4Gb)
	.word	0x0000		# base address = 0
	.word	0x9200		# data read / write
	.word	0x00CF		# granularity = 4096, 386 (+5th nibble of limit)

# walken modif

#	.word	0x07FF		# 8 Mb - limit = 2047 (2048 * 4096 = 8 Mb)
#	.word	0x0000		# base address = 0
#	.word	0x9A00		# code read / exec
#	.word	0x00C0		# granularity = 4096, 386

#	.word	0x07FF		# 8 Mb - limit = 2047 (2048 * 4096 = 8 Mb)
#	.word	0x0000		# base address = 0
#	.word	0x9200		# data read / write
#	.word	0x00C0		# granularity = 4096, 386

.globl SYMBOL_NAME(trampoline_end)
SYMBOL_NAME_LABEL(trampoline_end)
