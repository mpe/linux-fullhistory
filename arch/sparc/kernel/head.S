/* boot.S: The initial boot code for the Sparc port of Linux.

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)

           This file has to serve three purposes.

	   1) determine the prom-version and cpu/architecture
	   2) print enough useful info before we start to execute
	      c-code that I can possibly begin to debug things
	   3) Hold the vector of trap entry points

   The Sparc offers many challenges to kernel design. Here I will
   document those I have come across thus far. Upon bootup the boot
   prom loads your a.out image into memory. This memory the prom has
   already mapped for you in two places, however as far as I can tell
   the virtual address cache is not turned on although the MMU is
   translating things. You get loaded at 0x4000 exactly and you are
   aliased to 0xf8004000 with the appropriate mmu entries. So, when
   you link a boot-loadable object you want to do something like:

        ld -e start -Ttext 4000 -o mykernel myobj1.o myobj2.o ....

   to produce a proper image.

   At boot time you are given (as far as I can tell at this time)
   one key to figure out what machine you are one and what devices
   are available. The prom when it loads you leaves a pointer to
   the 'rom vector' in register %o0 right before it jumps to your
   starting address. This is a pointer to a struct that is full of
   pointer to functions (ie. printf, halt, reboot), pointers to
   linked lists (ie. memory mappings), and pointer to empirical
   constants (ie. stdin and stdout magic cookies + rom version).
   Starting with this piece of information you can figure out 
   just about anything you want about the machine you are on.

   Although I don't use it now, if you are on a Multiprocessor and
   therefore a v3 or above prom, register %o2 at boot contains a
   function pointer you must call before you proceed to invoke the
   other cpu's on the machine. I have no idea what kind of magic this
   is, give me time.
*/

#include <asm/cprefix.h>
#include <asm/head.h>
#include <asm/version.h>
#include <asm/asi.h>
#include <asm/contregs.h>
#include <asm/psr.h>
#include <asm/page.h>

	.data

/* First thing to go in the data segment is the interrupt stack. */

        .globl  C_LABEL(intstack)
        .globl  C_LABEL(eintstack)
C_LABEL(intstack):
        .skip   4 * PAGE_SIZE                ! 16k = 128 128-byte stack frames
C_LABEL(eintstack):



/* 
   The following are used with the prom_vector node-ops to figure out
   the cpu-type 
*/

        .globl  C_LABEL(cputyp)

C_LABEL(cputyp):
        .word   1

C_LABEL(cputypval):
	.asciz "sun4c"
	.ascii "     "

	.align 4
/*
 * Sun people can't spell worth damn. "compatability" indeed.
 * At least we *know* we can't spell, and use a spell-checker.
 */

/* Uh, actually Linus it is I who cannot spell. Too much murky
 * Sparc assembly will do this to ya.
 */
C_LABEL(cputypvar):
	.asciz "compatability"

C_LABEL(cputypvallen) = C_LABEL(cputypvar) - C_LABEL(cputypval)

/* This hold the prom-interface-version number for either v0 or v2. */

	.align 4
	.globl 	C_LABEL(prom_iface_vers)

C_LABEL(prom_iface_vers):	.skip 4

/* WARNING: evil messages follow */

	.align 4

sun4_notsup:
	.asciz  "Sparc-Linux: sun4 support not implemented yet\n\n"
	.align 4

sun4m_notsup:
        .asciz  "Sparc-Linux: sun4m support does not exist\n\n"
	.align 4

sun4d_notsup:
        .asciz  "Sparc-Linux: sun4d support does not exist\n\n"
	.align 4

you_lose:
	.asciz	"You lose..... Thanks for playing...\n"
	.align 4


	.globl boot_msg

/* memory descriptor property strings, v2 = yuk yuk yuk  */
/* XXX how to figure out vm mapped by prom? May have to scan magic addresses */

mem_prop_physavail:	.asciz "available"
	
			.align 4
mem_prop_phystot:	.asciz "reg"

/* v2_memory descriptor struct kludged here for assembly, if it ain't broke */

		.align 4
v2_mem_struct: 	.skip 0xff

			.align 4
v2_printf_physavail:	.asciz "Physical Memory Available: 0x%x bytes"
	
			.align 4
v2_printf_phystot:	.asciz "Physical Memory: 0x%x bytes"

/* A place to store property strings returned from the prom 'node' funcs */

			.align 4
prop_string_buf:	.skip 32

		.align 4
prop_name:	.asciz "name"
	
		.align 4
current_node:	.skip 4


/* nice little boot message */

		.align 4
boot_msg:	
	.ascii "Booting Sparc-Linux V0.00PRE-ALPHA "
	.ascii WHO_COMPILED_ME 
	.ascii "\r\n"
	.align 4

	.globl boot_msg2

boot_msg2:
	.asciz "Booting Sparclinux V0.00 PRE-ALPHA on a (SUN4C)\r\n\n"

	.align 4

pstring1:
	.asciz "Prom Magic Cookie: 0x%x  \n"
	.align 4

pstring2:
	.asciz "Interface Version: v%d\n"
	.align 4

pstring3:
	.asciz "Prom Revision: V%d\n\n"
	.align 4

pstring4:
	.ascii "Total Physical Memory: %d bytes\nVM mapped by Prom: %d bytes\n"
	.asciz "Available Physical Memory: %d bytes\n"
	.align 4


	.text

        .globl  C_LABEL(msgbuf)
msgbufsize = PAGE_SIZE                       ! 1 page for msg buffer
C_LABEL(msgbuf) =  PAGE_SIZE


IE_reg_addr = C_LABEL(msgbuf) + msgbufsize   ! this page not used; points to IEreg

	
/* Ok, things start to get interesting. We get linked such that 'start'
   is the entry symbol. However, it is real low in kernel address space
   and as such a nifty place to place the trap table. We achieve this goal
   by just jumping to 'gokernel' for the first trap's entry as the sparc
   never receives the zero trap as it is real special (hw reset).

   Each trap entry point is the size of 4 sparc instructions (or 4 bytes
   * 4 insns = 16 bytes). There are 128 hardware traps (some undefined
   or unimplemented) and 128 software traps (sys-calls, etc.).

   One of the instructions must be a branch. More often than not this
   will be to a trap handler entry point because it is completely
   impossible to handle any trap in 4 insns. I welcome anyone to 
   challenge this theory. :-)

   On entry into this table the hardware has loaded the program counter
   at which the trap occurred into register %l1 and the next program
   counter into %l2, this way we can return from the trap with a simple

           jmp %l1; rett %l2  ! poof...

   after properly servicing the trap. It wouldn't be a bad idea to load
   some more information into the local regs since we have technically
   2 or 3 instructions to play with besides the jmp to the 'real' trap
   handler (one can even go in the delay slot). For now I am going to put
   the %psr (processor status register) and the trap-type value in %l0
   and %l3 respectively. Also, for IRQ's I'll put the level in %l4.

*/

	.globl	start
	.globl 	_start  /* warning, solaris hack */
	.globl  C_LABEL(trapbase)
_start:   /* danger danger */
start:
C_LABEL(trapbase):
	b gokernel; nop; nop; nop;	! we never get trap #0 it is special 

	TRAP_ENTRY(0x1, my_trap_handler) /* Instruction Access Exception */
	TRAP_ENTRY(0x2, my_trap_handler) /* Illegal Instruction */
	TRAP_ENTRY(0x3, my_trap_handler) /* Privileged Instruction */
	TRAP_ENTRY(0x4, my_trap_handler) /* Floating Point Disabled */
	TRAP_ENTRY(0x5, spill_window_entry)   /* Window Overflow */
	TRAP_ENTRY(0x6, fill_window_entry)  /* Window Underflow */
	TRAP_ENTRY(0x7, my_trap_handler) /* Memory Address Not Aligned */
	TRAP_ENTRY(0x8, my_trap_handler) /* Floating Point Exception */
	TRAP_ENTRY(0x9, my_trap_handler) /* Data Miss Exception */
	TRAP_ENTRY(0xa, my_trap_handler) /* Tagged Instruction Overflow */
	TRAP_ENTRY(0xb, my_trap_handler) /* Watchpoint Detected */
	TRAP_ENTRY(0xc, my_trap_handler) /* Undefined... */
	TRAP_ENTRY(0xd, my_trap_handler) /* Undefined... */
	TRAP_ENTRY(0xe, my_trap_handler) /* Undefined... */
	TRAP_ENTRY(0xf, my_trap_handler) /* Undefined... */
	TRAP_ENTRY(0x10, my_trap_handler) /* Undefined... */

/* Level'd interrupt entry points, see macro defs above */

        TRAP_ENTRY_INTERRUPT_SOFT(1, 0x101) /* IRQ Software/SBUS Level 1  */
        TRAP_ENTRY_INTERRUPT(2)             /* IRQ SBUS Level 2           */
        TRAP_ENTRY_INTERRUPT(3)             /* IRQ SCSI/DMA/SBUS Level 3  */
        TRAP_ENTRY_INTERRUPT_SOFT(4, 0x104) /* IRQ Software Level 4       */
        TRAP_ENTRY_INTERRUPT(5)             /* IRQ SBUS/Ethernet Level 5  */
        TRAP_ENTRY_INTERRUPT_SOFT(6, 0x106) /* IRQ Software Level 6       */
        TRAP_ENTRY_INTERRUPT(7)             /* IRQ Video/SBUS Level 5     */
        TRAP_ENTRY_INTERRUPT(8)             /* IRQ SBUS Level 6           */
        TRAP_ENTRY_INTERRUPT(9)             /* IRQ SBUS Level 7           */
        TRAP_ENTRY_INTERRUPT(10)            /* IRQ Timer #1               */
        TRAP_ENTRY_INTERRUPT(11)            /* IRQ Floppy Intr.           */
        TRAP_ENTRY_INTERRUPT(12)            /* IRQ Zilog serial chip      */
        TRAP_ENTRY_INTERRUPT(13)            /* IRQ Audio Intr.            */
        TRAP_ENTRY_TIMER                    /* IRQ Timer #2 (one we use)  */
        TRAP_ENTRY_INTERRUPT_NMI(15, linux_trap_nmi) /* Level 15 (nmi) */

	TRAP_ENTRY(0x20, my_trap_handler)   /* General Register Access Error */
	TRAP_ENTRY(0x21, my_trap_handler)   /* Instruction Access Error      */
	TRAP_ENTRY(0x22, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x23, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x24, my_trap_handler)   /* Co-Processor Disabled         */
	TRAP_ENTRY(0x25, my_trap_handler)   /* Unimplemented FLUSH inst.     */
	TRAP_ENTRY(0x26, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x27, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x28, my_trap_handler)   /* Co-Processor Exception        */
	TRAP_ENTRY(0x29, my_trap_handler)   /* Data Access Error             */
	TRAP_ENTRY(0x2a, my_trap_handler)   /* Division by zero, you lose... */
	TRAP_ENTRY(0x2b, my_trap_handler)   /* Data Store Error              */
	TRAP_ENTRY(0x2c, my_trap_handler)   /* Data Access MMU-Miss          */
	TRAP_ENTRY(0x2d, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x2e, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x2f, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x30, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x31, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x32, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x33, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x34, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x35, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x36, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x37, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x38, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x39, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x3a, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x3b, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x3c, my_trap_handler)   /* Instruction Access MMU-Miss   */
	TRAP_ENTRY(0x3d, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x3e, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x3f, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x40, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x41, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x42, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x43, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x44, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x45, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x46, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x47, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x48, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x49, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x4a, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x4b, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x4c, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x4d, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x4e, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x4f, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x50, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x51, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x52, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x53, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x54, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x55, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x56, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x57, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x58, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x59, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x5a, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x5b, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x5c, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x5d, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x5e, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x5f, my_trap_handler)   /* Undefined...                  */
	TRAP_ENTRY(0x60, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x61, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x62, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x63, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x64, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x65, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x66, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x67, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x68, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x69, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x6a, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x6b, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x6c, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x6d, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x6e, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x6f, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x70, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x71, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x72, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x73, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x74, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x75, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x76, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x77, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x78, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x79, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x7a, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x7b, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x7c, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x7d, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x7e, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x7f, my_trap_handler)   /* Impl-Dep Exception            */
	TRAP_ENTRY(0x80, my_trap_handler)   /* SunOS System Call             */
	TRAP_ENTRY(0x81, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x82, my_trap_handler)   /* Divide by zero trap XXX       */
	TRAP_ENTRY(0x83, my_trap_handler)   /* Flush Windows Trap XXX        */
	TRAP_ENTRY(0x84, my_trap_handler)   /* Clean Windows Trap XXX        */
	TRAP_ENTRY(0x85, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x86, my_trap_handler)   /* Fix Unaligned Access Trap XXX */
	TRAP_ENTRY(0x87, my_trap_handler)   /* Integer Overflow Trap XXX     */
	TRAP_ENTRY(0x88, my_trap_handler)   /* Slowaris System Call          */
	TRAP_ENTRY(0x89, my_trap_handler)   /* NetBSD System Call            */
	TRAP_ENTRY(0x8a, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x8b, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x8c, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x8d, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x8e, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x8f, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x90, my_trap_handler)   /* SparcLinux System Call        */
	TRAP_ENTRY(0x91, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x92, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x93, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x94, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x95, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x96, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x97, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x98, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x99, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x9a, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x9b, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x9c, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x9d, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x9e, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0x9f, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xa0, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xa1, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xa2, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xa3, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xa4, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xa5, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xa6, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xa7, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xa8, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xa9, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xaa, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xab, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xac, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xad, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xae, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xaf, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xb0, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xb1, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xb2, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xb3, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xb4, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xb5, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xb6, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xb7, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xb8, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xb9, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xba, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xbb, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xbc, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xbd, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xbe, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xbf, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xc0, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xc1, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xc2, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xc3, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xc4, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xc5, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xc6, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xc7, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xc8, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xc9, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xca, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xcb, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xcc, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xcd, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xce, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xcf, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xd0, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xd1, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xd2, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xd3, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xd4, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xd5, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xd6, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xd7, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xd8, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xd9, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xda, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xdb, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xdc, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xdd, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xde, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xdf, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xe0, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xe1, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xe2, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xe3, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xe4, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xe5, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xe6, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xe7, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xe8, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xe9, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xea, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xeb, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xec, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xed, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xee, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xef, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xf0, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xf1, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xf2, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xf3, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xf4, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xf5, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xf6, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xf7, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xf8, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xf9, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xfa, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xfb, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xfc, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xfd, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xfe, my_trap_handler)   /* Software Trap                 */
	TRAP_ENTRY(0xff, my_trap_handler)   /* Software Trap                 */	

	.skip 4096

C_LABEL(msgbufmapped):
        .word   1



/* Cool, here we go. Pick up the romvec pointer in %o0 and stash it in
   %g7 and at _prom_vector_p. And also quickly check whether we are on
   a v0 or v2 prom.
*/

gokernel:	or	%g0, %o0, %g7
		sethi	%hi( C_LABEL(prom_vector_p) ), %g1
		st	%o0, [%g1 + %lo( C_LABEL(prom_vector_p) )]   ! we will need it later
		rd	%psr, %l2
		rd	%wim, %l3
		rd	%tbr, %l4
		or	%g0, %o2, %l5		! could be prom magic value...
	
#if 0 /* You think I'm nutz? */
		subcc	%l5, 0x0, %g0		! check for magic SMP pointer
		bne	nosmp	
		nop
		call    %o2			! call smp prom setup 
		nop
#endif /* I will be soon... */

/* Acquire boot time privileged register values, this will help debugging.
 * I figure out and store nwindows later on.
 */

nosmp:		sethi	%hi( C_LABEL(boot_psr) ), %l1
		st	%l2, [%l1 + %lo( C_LABEL(boot_psr) )]
		sethi	%hi( C_LABEL(boot_wim) ), %l1
		st	%l3, [%l1 + %lo( C_LABEL(boot_wim) )]
		sethi	%hi( C_LABEL(boot_tbr) ), %l1
		st	%l4, [%l1 + %lo( C_LABEL(boot_tbr) )]
		sethi	%hi( C_LABEL(boot_smp_ptr) ), %l1
		st	%l5, [%l1 + %lo( C_LABEL(boot_smp_ptr) )]

		or	%g0, %o0, %g7
		sethi	%hi( C_LABEL(prom_vector_p) ), %g5
		st	%o0, [%g5 + %lo( C_LABEL(prom_vector_p) )]   ! we will need it later

		ld	[%g7 + 0x4], %o3
		subcc	%o3, 0x2, %g0			! a v2 prom?
		be	found_v2
		nop

		/* paul@sfe.com.au */
		subcc	%o3, 0x3, %g0			! a v3 prom?
		or	%g0, 0x3, %o5
		sethi	%hi(C_LABEL(prom_iface_vers) ), %g1
		st	%o5, [%g1 + %lo( C_LABEL(prom_iface_vers) )]
		be	not_v2
		nop


/* Old sun4's pass our load address into %o0 instead of the prom
   pointer. On sun4's you have to hard code the romvec pointer into
   your code. Sun probably still does that because they don't even
   trust their own "OpenBoot" specifications.
*/

		sethi	%hi(LOAD_ADDR), %g6
		subcc	%o0, %g6, %g0		! an old sun4?
		be	no_sun4_here
		nop

		sethi	%hi( C_LABEL(prom_iface_vers) ), %g1
		st	%g0, [%g1 + %lo( C_LABEL(prom_iface_vers) )]
		b	not_v2
		nop

found_v2:
		or	%g0, 0x2, %o5
		sethi	%hi( C_LABEL(prom_iface_vers) ), %g1
		st	%o5, [%g1 + %lo( C_LABEL(prom_iface_vers) )]

not_v2:

/* Get the machine type via the mysterious romvec node operations.
 * Here we can find out whether we are on a sun4 sun4c, sun4m, or
 * a sun4m. The "nodes" are set up as a bunch of n-ary trees which
 * you can traverse to get information about devices and such. The
 * information acquisition happens via the node-ops which are defined
 * in the linux_openprom.h header file. Of particular interest is the
 * 'nextnode(int node)' function as it does the smart thing when
 * presented with a value of '0', it gives you the first node in the
 * tree. These node integers probably offset into some internal prom
 * pointer table the openboot has. It's completely undocumented, so
 * I'm not about to go sifting through the prom address space, but may
 * do so if I get suspicious enough. :-)
 */

		or	%g0, %g7, %l1
		add	%l1, 0x1c, %l1		
		ld	[%l1], %l0
		ld	[%l0], %l0
		call 	%l0
		or	%g0, %g0, %o0		! next_node(0) = first_node

		sethi	%hi( C_LABEL(cputypvar) ), %o1	! first node has cpu-arch
		or	%o1, %lo( C_LABEL(cputypvar) ), %o1
		sethi	%hi( C_LABEL(cputypval) ), %o2	! information, the string
		or	%o2, %lo( C_LABEL(cputypval) ), %o2
		ld	[%l1], %l0		! 'compatibility' tells
		ld	[%l0 + 0xc], %l0	! that we want 'sun4x' where
		call	%l0			! x is one of '', 'c', 'm',
		nop				! 'd' or 'e'. %o2 holds pointer
						! to a buf where above string
						! will get stored by the prom.

		sethi	%hi( C_LABEL(cputypval) ), %o2	! better safe than sorry
		or	%o2, %lo( C_LABEL(cputypval) ), %o2
		ldub	[%o2 + 0x4], %o0
		subcc	%o0, 'c', %g0		! we already know we are not
		be	is_sun4c		! on a plain sun4 because of
		nop				! the check for 0x4000 in %o0
		subcc	%o0, 'm', %g0		! at start:
		be	is_sun4m
		nop
		b	no_sun4d_here		! god bless the person who
		nop				! tried to run this on sun4d

is_sun4m:
is_sun4c:					! OK, this is a sun4c, yippie
		or 	%g0, %g7, %g6		! load up the promvec offsets
		sethi	%hi(prom_magic), %g5   	! magic mushroom :>
		st	%g6, [%g5 + %lo(prom_magic)]
		add	%g7, 0x4, %g6
		sethi	%hi(prom_rom_vers), %g5
		st	%g6, [%g5 + %lo(prom_rom_vers)]
		add	%g7, 0x8, %g6
		sethi	%hi(prom_pluginvers), %g5
		st	%g6, [%g5 + %lo(prom_pluginvers)]
		add	%g7, 0xc, %g6
		sethi	%hi(prom_revision), %g5
		st	%g6, [%g5 + %lo(prom_revision)]
		add	%g7, 0x10, %g6
		sethi	%hi(prom_v0mem_desc), %g5
		st	%g6, [%g5 + %lo(prom_v0mem_desc)]
		add	%g7, 0x1c, %g6
		sethi	%hi(prom_nodefuncs), %g5
		st	%g6, [%g5 + %lo(prom_nodefuncs)]
		add	%g7, 0x68, %g6
		sethi	%hi(prom_printf), %g5
		st	%g6, [%g5 + %lo(prom_printf)]
		add	%g7, 0x6c, %g6
		sethi	%hi(prom_abort), %g5
		st	%g6, [%g5 + %lo(prom_abort)]
		add	%g7, 0x74, %g6
		sethi	%hi(prom_halt), %g5
		st	%g6, [%g5 + %lo(prom_halt)]
		add	%g7, 0x78, %g6
		sethi	%hi(prom_sync), %g5
		st	%g6, [%g5 + %lo(prom_sync)]
		add	%g7, 0x7c, %g6
		sethi	%hi(prom_eval), %g5
		st	%g6, [%g5 + %lo(prom_eval)]
		add	%g7, 0x80, %g6
		sethi	%hi(prom_v0bootline), %g6
		st	%g6, [%g5 + %lo(prom_v0bootline)]


/* That was easy, now lets try to print some message on the screen.
 * We don't have to worry about bad address translations when the prom
 * addresses our pointers because our pointers are at 0x0-kern_size
 * as the prom expects.
 */

/* paul@sfe.com.au */
/* V3 doesn't have printf.. And I don't really feel like doing the formatting
 * myself.. So we miss out on some messages (for now).
 */
		ld	[%g7 + 0x4], %o0
		subcc	%o3, 0x3, %g0
		be	v3_bootmsg
		nop

		sethi	%hi(boot_msg), %o0	
		or	%o0, %lo(boot_msg), %o0
		sethi	%hi(prom_printf), %o1
		ld	[%o1 + %lo(prom_printf)], %o1
		ld	[%o1], %o1
		call	%o1  			! print boot message #1
		nop

		sethi	%hi(pstring1), %o0
		or	%o0, %lo(pstring1), %o0
		sethi	%hi(prom_printf), %o2
		ld	[%o2 + %lo(prom_printf)], %o2
		ld	[%o2], %o2
		sethi	%hi(prom_magic), %o1
		ld	[%o1 + %lo(prom_magic)], %o1
		ld	[%o1], %o1
		call	%o2
		nop

		sethi	%hi(pstring2), %o0
		or	%o0, %lo(pstring2), %o0
		sethi	%hi(prom_printf), %o2
		ld	[%o2 + %lo(prom_printf)], %o2
		ld	[%o2], %o2
		sethi	%hi( C_LABEL(prom_iface_vers) ), %o1
		ld	[%o1 + %lo( C_LABEL(prom_iface_vers) )], %o1
		ld	[%o1], %o1
		call 	%o2
		nop

		b	rest_of_boot
		nop

v3_bootmsg:
		ld	[%g7 + 0x94], %o0
		ld	[%o0], %o0
		sethi	%hi(boot_msg), %o1
		or	%o1, %lo(boot_msg), %o1
		mov	BOOT_MSG_LEN, %o2
		ld	[%g7 + 0xb8], %o4
		call	%o4
		nop
 
		ld	[%g7 + 0x94], %o0
		ld	[%o0], %o0
		sethi	%hi(boot_msg2), %o1
		or	%o1, %lo(boot_msg2), %o1
		mov	BOOT_MSG2_LEN, %o2
		ld	[%g7 + 0xb8], %o4
		call	%o4
		nop
		b	rest_of_boot
		nop


no_sun4_here:
		ld	[%g7 + 0x68], %o1
		set	sun4_notsup, %o0
		call	%o1
		nop

rest_of_boot:
		or	%g0, PAGE_SHIFT, %g5

		sethi	%hi(AC_CONTEXT), %g1	! kernel context, safe now
						! the only valid context
						! until we call paging_init()
		stba	%g0, [%g1] ASI_CONTROL


/* I make the kernel image sit in memory relative to 0x0 with the text 
 * starting at 0x4000. Now it looks like the way memory is set in Linux
 * on an ix86.
 */

/* Uh, oh, interrupt time. This crap is real confusing. What I want to do is
 * clear all interrupts, map the interrupt enable register which in effect
 * enables non-maskable interrupts (or NMI's). Actually we take no interrupts
 * until we frob with the %tbr (trap base register) which the prom has set 
 * to all its routines which allows some sanity during bootup.
 */

		sethi	%hi(IE_reg_addr), %l0
		or	%l0, %lo(IE_reg_addr), %l0

		set	0xf4000000, %l3
		sethi	%hi(INT_ENABLE_REG_PHYSADR), %l2
		or	%l2, %lo(INT_ENABLE_REG_PHYSADR), %l2
		srl	%l2, %g5, %l2
		or	%l2, %l3, %l1		

#ifndef CONFIG_SRMMU
		sta	%l1, [%l0] ASI_PTE
#endif
	
		or	%g0, 0x1, %l1
		stb	%l1, [%l0]
	

/* Aieee, now set PC and nPC, enable traps, give ourselves a stack and it's
 * show-time!
 */

		sethi	%hi(1f), %g1
		or	%g1, %lo(1f), %g1
		jmp	%g1
		nop

		.align 4
1:		sethi	%hi( C_LABEL(cputyp) ), %o0
		st	%g4, [%o0 + %lo( C_LABEL(cputyp) )]

		sethi	%hi( C_LABEL(pgshift) ), %o0
		st	%g5, [%o0 + %lo( C_LABEL(pgshift) )]

		mov	1, %o0
		sll	%o0, %g5, %g5
		sethi	%hi( C_LABEL(nbpg) ), %o0
		st	%g5, [%o0 + %lo( C_LABEL(nbpg) )]

		sub	%g5, 1, %g5
		sethi	%hi( C_LABEL(pgofset) ), %o0
		st	%g5, [%o0 + %lo( C_LABEL(pgofset) )]


		rd	%psr, %g3
		andn	%g3, PSR_ET, %g3
		wr	%g3, 0x0, %psr		! make sure traps are off
						! before we play around
		WRITE_PAUSE			! no guarantees until 3 insns


		wr	%g0, 0x0, %wim		! magical invalid window reg
		WRITE_PAUSE			! see above

/* I keep the timer interrupt on so that BogoMIPS works and the prom
 * keeps updating its "jiffies" counter. 100HZ clock on sparcstations.
 */	

/* If gas wasn't so dumb, I could use or'd macros in this next
 * write. ;-( like this (PSR_PS | PSR_S | PSR_PIL)...
 */

		sethi	%hi(PSR_PS | PSR_S | PSR_PIL | PSR_EF), %g2
		or	%g2, %lo(PSR_PS | PSR_S | PSR_PIL | PSR_EF), %g2
		wr	%g2, 0x0, %psr
		WRITE_PAUSE

		wr	%g0, 0x2, %wim		! window 1 invalid
		WRITE_PAUSE

		or	%g0, 0x1, %g1
		sethi	%hi( C_LABEL(current) + THREAD_WIM), %g2
		st	%g1, [%g2 + %lo( C_LABEL(current) + THREAD_WIM)]

/* I want a kernel stack NOW! */

		set	( C_LABEL(init_user_stack) + 4092 - 96 - 80), %fp	
		set	( C_LABEL(init_user_stack) + 4092), %sp

/* now out stack is set up similarly to the way it is on the i386 */

		rd	%psr, %l0
		wr	%l0, PSR_ET, %psr
		WRITE_PAUSE

/*
 * Maybe the prom zeroes out our BSS section, maybe it doesn't. I certainly 
 * don't know, do you?
 */

		set	C_LABEL(edata) , %o0
		set	C_LABEL(end) , %o1
		sub	%o1, %o0, %g2
		sethi	%hi( C_LABEL(kernel_bss_len) ), %g3
		st	%g2, [%g3 + %lo( C_LABEL(kernel_bss_len) )]
		sethi	%hi( C_LABEL(trapbase) ), %g3
		or	%g3, %lo( C_LABEL(trapbase) ), %g3
		sethi	%hi( C_LABEL(etext) ), %g4
		or	%g4, %lo( C_LABEL(etext) ), %g4			
		sub	%g4, %g3, %g2
		sethi	%hi( C_LABEL(kernel_text_len) ), %g3
		st	%g2, [%g3 + %lo( C_LABEL(kernel_text_len) )]
		sethi	%hi( C_LABEL(etext) ), %g4
		or	%g4, %lo( C_LABEL(etext) ), %g4
		sethi	%hi( C_LABEL(edata) ), %g3
		or	%g3, %lo( C_LABEL(edata) ), %g3
		sub	%g3, %g4, %g2
		sethi	%hi( C_LABEL(kernel_data_len) ), %g3
		st	%g2, [%g3 + %lo( C_LABEL(kernel_data_len) )]
		or	%g0, %g0, %g1

1:	
		st	%g0, [%o0]
		add	%o0, 0x4, %o0
		subcc	%o0, %o1, %g0
		bl	1b
		nop

/* Compute NWINDOWS and stash it away. Now uses %wim trick explained
 * in the V8 manual. Ok, this method seems to work, sparc is cool...
 */

		sethi	%hi(0xffffffff), %g1
		rd	%wim, %g2			! save current value
		or	%g1, %lo(0xffffffff), %g1
		wr	%g1, 0x0, %wim
		rd	%wim, %g1			! get remaining mask
		wr	%g2, 0x0, %wim			! restore old value
		WRITE_PAUSE

		or	%g0, 0x0, %g3

1:		srl	%g1, 0x1, %g1			! shift until highest
		subcc	%g1, 0x0, %g0			! bit set
		bne	1b
		add	%g3, 0x1, %g3
		sethi	%hi( C_LABEL(nwindows) ), %g4
		st	%g3, [%g4 + %lo( C_LABEL(nwindows) )]	! store final value
		sub	%g3, 0x1, %g3
		sethi	%hi( C_LABEL(nwindowsm1) ), %g4
		st	%g3, [%g4 + %lo( C_LABEL(nwindowsm1) )]


/* Here we go */

#ifndef CONFIG_SUN4M
		/* paul@sfe.com.au */
		/* Look into traps later :( */
		set	C_LABEL(trapbase), %g3
		wr	%g3, 0x0, %tbr
		WRITE_PAUSE
#endif


/* First we call init_prom() to set up romvec, then off to start_kernel() */
/* XXX put this in arch_init() */

		sethi	%hi( C_LABEL(prom_vector_p) ), %g5
		call	C_LABEL(init_prom)
		ld	[%g5 + %lo( C_LABEL(prom_vector_p) )], %o0   /* delay slot */

		call 	C_LABEL(start_kernel)
		nop
	
		call	halt_me
		nop

/* There, happy now adrian? */

no_sun4d_here:
		ld	[%g7 + 0x68], %o1
		set	sun4d_notsup, %o0
		call	%o1
		nop
		b	halt_me
		nop

halt_me:
		ld	[%g7 + 0x74], %o0
		call	%o0			! get us out of here...
		nop				! apparently solaris is better

	.data
	.align 4

/*
 * Fill up the prom vector, note in particular the kind first element,
 * no joke. I don't need all of them in here as the entire prom vector
 * gets initialized in c-code so all routines can use it.
 */

			.globl C_LABEL(prom_vector_p)

C_LABEL(prom_vector_p):	.skip 4
prom_magic:  		.skip 4			! magic mushroom, beware...
prom_rom_vers: 		.skip 4			! interface version (v0 or v2)
prom_pluginvers:	.skip 4			! XXX help help help ???
prom_revision:		.skip 4			! PROM revision (ie. 1.4)
prom_halt:		.skip 4			! void halt(void)  solaris friend
prom_eval:		.skip 4			! void eval(int len, char* string)
prom_v0bootline:	.skip 4			! boot command line
prom_v0mem_desc:	.skip 4			! V0 memory descriptor list ptr.
prom_nodefuncs:		.skip 4			! Magical Node functions
prom_printf:		.skip 4			! minimal printf()

/* The prom_abort pointer MUST be mapped in all contexts, because if you
 * don't then if a user process is running when you press the abort key
 * sequence, all sorts of bad things can happen
 */

prom_abort:		.skip 4		! L1-A magic cookie
					! must be mapped in ALL contexts

/* prom_sync is a place where the kernel should place a pointer to a kernel
 * function that when called will sync all pending information to the drives
 * and then promptly return. If the kernel gets aborted with 'L1-A' one can
 * give the 'sync' command to the boot prompt and this magic cookie gets
 * executed. Nice feature eh?
 */

prom_sync:		.skip 4			! hook in prom for sync func

	.align 4

/* We calculate the following at boot time, window fills/spills and trap entry
 * code uses these to keep track of the register windows.
 */

	.globl C_LABEL(nwindows)
	.globl C_LABEL(nwindowsm1)
C_LABEL(nwindows):	.skip 4
C_LABEL(nwindowsm1):	.skip 4

	.align 4
/* Boot time privileged register values, plus magic %o2 value */

	.globl C_LABEL(boot_wim)
	.globl C_LABEL(boot_psr)
	.globl C_LABEL(boot_tbr)
	.globl C_LABEL(boot_smp_ptr)
C_LABEL(boot_wim):		.skip 4
C_LABEL(boot_psr):		.skip 4
C_LABEL(boot_tbr):		.skip 4
C_LABEL(boot_smp_ptr):		.skip 4


	.align 4
/* Miscellaneous pieces of information saved at kernel startup. */
	.globl C_LABEL(kernel_text_len)
	.globl C_LABEL(kernel_data_len)
	.globl C_LABEL(kernel_bss_len)
C_LABEL(kernel_text_len):	.word 0
C_LABEL(kernel_data_len):	.word 0
C_LABEL(kernel_bss_len):	.word 0

/* These are for page alignment/offset information as they change from
   machine to machine.
*/

        .globl  C_LABEL(pgshift)
	.globl 	C_LABEL(nbpg)
	.globl	C_LABEL(pgofset)

	.align 4
C_LABEL(pgshift):
        .word   1
C_LABEL(nbpg):
        .word   1
C_LABEL(pgofset):
        .word   1

/* Just to get the kernel through the compiler for now */
	.globl C_LABEL(swapper_pg_dir), C_LABEL(pg0)
	.globl C_LABEL(empty_bad_page)
	.globl C_LABEL(empty_bad_page_table)
	.globl C_LABEL(empty_zero_page)
	.globl C_LABEL(floppy_track_buffer)
C_LABEL(floppy_track_buffer):
	.fill 512*2*36,1,0

	.align 4
C_LABEL(swapper_pg_dir):		.skip 0x1000
C_LABEL(pg0):				.skip 0x1000
C_LABEL(empty_bad_page):		.skip 0x1000
C_LABEL(empty_bad_page_table):		.skip 0x1000
C_LABEL(empty_zero_page):		.skip 0x1000

		.align 4
diagstr:	.asciz	"DIAG\n"
		.align 4
