!
!	Trampoline.S	Derived from Setup.S by Linus Torvalds
!
!	Entry: CS:IP point to the start of our code, we are 
!	in real mode with no stack, but the rest of the 
!	trampoline page to make our stack and everything else
!	is a mystery.
!
!	In fact we don't actually need a stack so we don't
!	set one up.
!
!	We jump into the boot/compressed/head.S code. So you'd
!	better be running a compressed kernel image or you
!	won't get very far.
!
#define __ASSEMBLY__
#include <asm/segment.h>

.text
	extrn startup32

entry start
start:
!	nop
!	jmp start	! 	Test
	mov ax,cs	!	Code and data in the same place
	mov ds,ax	!
	mov cx,ax	!	Pass stack info to the 32bit boot
	add cx,cx
	add cx,cx
	add cx,cx
	add cx,cx	!	Segment -> Offset
	add cx, #4096   !	End of page is wanted
	mov     bx,#1	!	Flag an SMP trampoline
	cli		!	We should be safe anyway

	lidt	idt_48	!	load idt with 0,0
	lgdt	gdt_48	!	load gdt with whatever is appropriate

	xor	ax,ax	
	inc	ax	!	protected mode (PE) bit
	lmsw	ax	!	Into protected mode
	jmp	flush_instr
flush_instr:
	jmpi	8192+startup32,KERNEL_CS	!	Jump to the 32bit trampoline code
!	jmpi	0x100000,KERNEL_CS		!	Jump into the 32bit startup
!	.byte   0x66,0x67			!	32bit
!	.byte	0xea,0x00,0x00,0x10,0x00,0x10,0x00	!jmpi	.0x100000,KERNEL_CS

gdt:
	.word	0,0,0,0		! dummy

	.word	0,0,0,0		! unused

	.word	0x07FF		! 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		! base address=0
	.word	0x9A00		! code read/exec
	.word	0x00C0		! granularity=4096, 386

	.word	0x07FF		! 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		! base address=0
	.word	0x9200		! data read/write
	.word	0x00C0		! granularity=4096, 386

idt_48:
	.word	0			! idt limit=0
	.word	0,0			! idt base=0L

gdt_48:
	.word	0x800		! gdt limit=2048, 256 GDT entries
	.word	8192+gdt,0x0	! gdt base = 8192+gdt (first SMP CPU)
				! we load the others with the first table
				! saves rewriting gdt_48 for each

