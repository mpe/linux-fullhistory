/* head.S: The initial boot code for the Sparc port of Linux.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 *         This file has to serve three purposes.
 *
 *	   1) determine the prom-version and cpu/architecture
 *	   2) print enough useful info before we start to execute
 *	      c-code that I can possibly begin to debug things
 *	   3) Hold the vector of trap entry points
 *
 *  The Sparc offers many challenges to kernel design. Here I will
 * document those I have come across thus far. Upon bootup the boot
 * prom loads your a.out image into memory. This memory the prom has
 * already mapped for you in two places, however as far as I can tell
 * the virtual address cache is not turned on although the MMU is
 * translating things. You get loaded at 0x4000 exactly and you are
 * aliased to 0xf8004000 with the appropriate mmu entries. So, when
 * you link a boot-loadable object you want to do something like
 *
 *      ld -e start -Ttext 4000 -o mykernel myobj1.o myobj2.o ....
 *
 * to produce a proper image.
 *
 * At boot time you are given (as far as I can tell at this time)
 * one key to figure out what machine you are one and what devices
 * are available. The prom when it loads you leaves a pointer to
 * the 'rom vector' in register %o0 right before it jumps to your
 * starting address. This is a pointer to a struct that is full of
 * pointer to functions (ie. printf, halt, reboot), pointers to
 * linked lists (ie. memory mappings), and pointer to empirical
 * constants (ie. stdin and stdout magic cookies + rom version).
 * Starting with this piece of information you can figure out 
 * just about anything you want about the machine you are on.
 *
 * Although I don't use it now, if you are on a Multiprocessor and
 * therefore a v3 or above prom, register %o2 at boot contains a
 * function pointer you must call before you proceed to invoke the
 * other cpu's on the machine. I have no idea what kind of magic this
 * is, give me time.
 */

#include <asm/cprefix.h>
#include <asm/head.h>
#include <linux/version.h>
#include <asm/asi.h>
#include <asm/contregs.h>
#include <asm/psr.h>
#include <asm/page.h>
#include <asm/kdebug.h>

	.data

/* First thing to go in the data segment is the interrupt stack. */

        .globl  C_LABEL(intstack)
        .globl  C_LABEL(eintstack)
	.align 4
C_LABEL(intstack):
        .skip   4 * PAGE_SIZE                ! 16k = 128 128-byte stack frames
C_LABEL(eintstack):



/* 
 * The following are used with the prom_vector node-ops to figure out
 * the cpu-type 
 */

	.align 4
        .globl  C_LABEL(cputyp)
C_LABEL(cputyp):
        .word   1

	.align 4
	.globl C_LABEL(cputypval)
C_LABEL(cputypval):
	.asciz "sun4c"
	.ascii "     "

C_LABEL(cputypvalend):
C_LABEL(cputypvallen) = C_LABEL(cputypvar) - C_LABEL(cputypval)

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

/* Tested on SS-5, SS-10. Probably someone at Sun applied a spell-checker. --P3 */
	.align 4
C_LABEL(cputypvar_sun4m):
	.asciz "compatible"

/* WARNING: evil messages follow */

	.align 4

sun4_notsup:
	.asciz  "Sparc-Linux sun4 support not implemented yet\n\n"
	.align 4

sun4d_notsup:
        .asciz  "Sparc-Linux sun4d support does not exist\n\n"
	.align 4

sun4e_notsup:
        .asciz  "Sparc-Linux sun4e support does not exist\n\n"
	.align 4

sun4u_notsup:
        .asciz  "Sparc-Linux sun4u support does not exist\n\n"
	.align 4

/* Ok, things start to get interesting. We get linked such that 'start'
 * is the entry symbol. However, it is real low in kernel address space
 * and as such a nifty place to place the trap table. We achieve this goal
 * by just jumping to 'gokernel' for the first trap's entry as the sparc
 * never receives the zero trap as it is real special (hw reset).
 *
 * Each trap entry point is the size of 4 sparc instructions (or 4 bytes
 * * 4 insns = 16 bytes). There are 128 hardware traps (some undefined
 * or unimplemented) and 128 software traps (sys-calls, etc.).
 *
 * One of the instructions must be a branch. More often than not this
 * will be to a trap handler entry point because it is completely
 * impossible to handle any trap in 4 insns. I welcome anyone to 
 * challenge this theory. :-)
 *
 * On entry into this table the hardware has loaded the program counter
 * at which the trap occurred into register %l1 and the next program
 * counter into %l2, this way we can return from the trap with a simple
 *
 *         jmp %l1; rett %l2  ! poof...
 *
 * after properly servicing the trap. It wouldn't be a bad idea to load
 * some more information into the local regs since we have technically
 * 2 or 3 instructions to play with besides the jmp to the 'real' trap
 * handler (one can even go in the delay slot). For now I am going to put
 * the %psr (processor status register) and the trap-type value in %l0
 * and %l3 respectively. Also, for IRQ's I'll put the level in %l4.
 */

	.text

	.globl	start
	.globl 	_start  /* warning, solaris hack */
	.globl  C_LABEL(trapbase)
_start:   /* danger danger */
start:
C_LABEL(trapbase):
/* XXX Grrr, this table is basically sun4c specific, sort of... XXX */
/* We get control passed to us here at t_zero. */
t_zero:	b gokernel; nop; nop; nop;

t_tflt:	TRAP_ENTRY(0x1, sparc_text_fault)    /* Inst. Access Exception        */
t_bins:	TRAP_ENTRY(0x2, bad_instruction)     /* Illegal Instruction           */
t_pins:	TRAP_ENTRY(0x3, bad_instruction)     /* Privileged Instruction        */
t_fpd:	TRAP_ENTRY(0x4, fpd_trap_handler)    /* Floating Point Disabled       */
t_wovf:	TRAP_ENTRY(0x5, spill_window_entry)  /* Window Overflow               */
t_wunf:	TRAP_ENTRY(0x6, fill_window_entry)   /* Window Underflow              */
t_mna:	TRAP_ENTRY(0x7, mna_handler)         /* Memory Address Not Aligned    */
t_fpe:	TRAP_ENTRY(0x8, fpe_trap_handler)    /* Floating Point Exception      */
t_dflt:	TRAP_ENTRY(0x9, sparc_data_fault)    /* Data Miss Exception           */
t_tio:	TRAP_ENTRY(0xa, do_tag_overflow)     /* Tagged Instruction Ovrflw     */
t_wpt:	TRAP_ENTRY(0xb, do_watchpoint)       /* Watchpoint Detected           */
t_badc:	TRAP_ENTRY(0xc, bad_trap_handler)    /* Undefined...                  */
t_badd:	TRAP_ENTRY(0xd, bad_trap_handler)    /* Undefined...                  */
t_bade:	TRAP_ENTRY(0xe, bad_trap_handler)    /* Undefined...                  */
t_badf:	TRAP_ENTRY(0xf, bad_trap_handler)    /* Undefined...                  */
t_bad10:TRAP_ENTRY(0x10, bad_trap_handler)   /* Undefined...                  */
t_irq1:	TRAP_ENTRY_INTERRUPT(1)	             /* IRQ Software/SBUS Level 1     */
t_irq2:	TRAP_ENTRY_INTERRUPT(2)              /* IRQ SBUS Level 2              */
t_irq3:	TRAP_ENTRY_INTERRUPT(3)              /* IRQ SCSI/DMA/SBUS Level 3     */
t_irq4:	TRAP_ENTRY_INTERRUPT(4)              /* IRQ Software Level 4          */
t_irq5:	TRAP_ENTRY_INTERRUPT(5)              /* IRQ SBUS/Ethernet Level 5     */
t_irq6:	TRAP_ENTRY_INTERRUPT(6)              /* IRQ Software Level 6          */
t_irq7:	TRAP_ENTRY_INTERRUPT(7)              /* IRQ Video/SBUS Level 5        */
t_irq8:	TRAP_ENTRY_INTERRUPT(8)              /* IRQ SBUS Level 6              */
t_irq9:	TRAP_ENTRY_INTERRUPT(9)              /* IRQ SBUS Level 7              */
t_irq10:TRAP_ENTRY_TIMER                     /* IRQ Timer #1 (one we use)     */
t_irq11:TRAP_ENTRY_INTERRUPT(11)             /* IRQ Floppy Intr.              */
t_irq12:TRAP_ENTRY_INTERRUPT(12)             /* IRQ Zilog serial chip         */
t_irq13:TRAP_ENTRY_INTERRUPT(13)             /* IRQ Audio Intr.               */
t_irq14:TRAP_ENTRY_INTERRUPT(14)             /* IRQ Timer #2                  */
t_nmi:	NMI_TRAP                             /* Level 15 (NMI)                */
t_racc:	TRAP_ENTRY(0x20, do_reg_access)      /* General Register Access Error */
t_iacce:TRAP_ENTRY(0x21, do_iacc_error)      /* Instruction Access Error      */
t_bad22:TRAP_ENTRY(0x22, bad_trap_handler)   /* Undefined...                  */
t_bad23:TRAP_ENTRY(0x23, bad_trap_handler)   /* Undefined...                  */
t_cpdis:TRAP_ENTRY(0x24, do_cp_disabled)     /* Co-Processor Disabled         */
t_uflsh:TRAP_ENTRY(0x25, do_bad_flush)       /* Unimplemented FLUSH inst.     */
t_bad26:TRAP_ENTRY(0x26, bad_trap_handler)   /* Undefined...                  */
t_bad27:TRAP_ENTRY(0x27, bad_trap_handler)   /* Undefined...                  */
t_cpexc:TRAP_ENTRY(0x28, do_cp_exception)    /* Co-Processor Exception        */
t_dacce:TRAP_ENTRY(0x29, do_dacc_error)      /* Data Access Error             */
t_hwdz:	TRAP_ENTRY(0x2a, do_hw_divzero)      /* Division by zero, you lose... */
t_dserr:TRAP_ENTRY(0x2b, do_dstore_err)      /* Data Store Error              */
t_daccm:TRAP_ENTRY(0x2c, do_dacc_mmu_miss)   /* Data Access MMU-Miss          */
t_bad2d:TRAP_ENTRY(0x2d, bad_trap_handler)   /* Undefined...                  */
t_bad2e:TRAP_ENTRY(0x2e, bad_trap_handler)   /* Undefined...                  */
t_bad2f:TRAP_ENTRY(0x2f, bad_trap_handler)   /* Undefined...                  */
t_bad30:TRAP_ENTRY(0x30, bad_trap_handler)   /* Undefined...                  */
t_bad31:TRAP_ENTRY(0x31, bad_trap_handler)   /* Undefined...                  */
t_bad32:TRAP_ENTRY(0x32, bad_trap_handler)   /* Undefined...                  */
t_bad33:TRAP_ENTRY(0x33, bad_trap_handler)   /* Undefined...                  */
t_bad34:TRAP_ENTRY(0x34, bad_trap_handler)   /* Undefined...                  */
t_bad35:TRAP_ENTRY(0x35, bad_trap_handler)   /* Undefined...                  */
t_bad36:TRAP_ENTRY(0x36, bad_trap_handler)   /* Undefined...                  */
t_bad37:TRAP_ENTRY(0x37, bad_trap_handler)   /* Undefined...                  */
t_bad38:TRAP_ENTRY(0x38, bad_trap_handler)   /* Undefined...                  */
t_bad39:TRAP_ENTRY(0x39, bad_trap_handler)   /* Undefined...                  */
t_bad3a:TRAP_ENTRY(0x3a, bad_trap_handler)   /* Undefined...                  */
t_bad3b:TRAP_ENTRY(0x3b, bad_trap_handler)   /* Undefined...                  */
t_iaccm:TRAP_ENTRY(0x3c, do_iacc_mmu_miss)   /* Instruction Access MMU-Miss   */
t_bad3d:TRAP_ENTRY(0x3d, bad_trap_handler)   /* Undefined...                  */
t_bad3e:TRAP_ENTRY(0x3e, bad_trap_handler)   /* Undefined...                  */
t_bad3f:TRAP_ENTRY(0x3f, bad_trap_handler)   /* Undefined...                  */
t_bad40:TRAP_ENTRY(0x40, bad_trap_handler)   /* Undefined...                  */
t_bad41:TRAP_ENTRY(0x41, bad_trap_handler)   /* Undefined...                  */
t_bad42:TRAP_ENTRY(0x42, bad_trap_handler)   /* Undefined...                  */
t_bad43:TRAP_ENTRY(0x43, bad_trap_handler)   /* Undefined...                  */
t_bad44:TRAP_ENTRY(0x44, bad_trap_handler)   /* Undefined...                  */
t_bad45:TRAP_ENTRY(0x45, bad_trap_handler)   /* Undefined...                  */
t_bad46:TRAP_ENTRY(0x46, bad_trap_handler)   /* Undefined...                  */
t_bad47:TRAP_ENTRY(0x47, bad_trap_handler)   /* Undefined...                  */
t_bad48:TRAP_ENTRY(0x48, bad_trap_handler)   /* Undefined...                  */
t_bad49:TRAP_ENTRY(0x49, bad_trap_handler)   /* Undefined...                  */
t_bad4a:TRAP_ENTRY(0x4a, bad_trap_handler)   /* Undefined...                  */
t_bad4b:TRAP_ENTRY(0x4b, bad_trap_handler)   /* Undefined...                  */
t_bad4c:TRAP_ENTRY(0x4c, bad_trap_handler)   /* Undefined...                  */
t_bad4d:TRAP_ENTRY(0x4d, bad_trap_handler)   /* Undefined...                  */
t_bad4e:TRAP_ENTRY(0x4e, bad_trap_handler)   /* Undefined...                  */
t_bad4f:TRAP_ENTRY(0x4f, bad_trap_handler)   /* Undefined...                  */
t_bad50:TRAP_ENTRY(0x50, bad_trap_handler)   /* Undefined...                  */
t_bad51:TRAP_ENTRY(0x51, bad_trap_handler)   /* Undefined...                  */
t_bad52:TRAP_ENTRY(0x52, bad_trap_handler)   /* Undefined...                  */
t_bad53:TRAP_ENTRY(0x53, bad_trap_handler)   /* Undefined...                  */
t_bad54:TRAP_ENTRY(0x54, bad_trap_handler)   /* Undefined...                  */
t_bad55:TRAP_ENTRY(0x55, bad_trap_handler)   /* Undefined...                  */
t_bad56:TRAP_ENTRY(0x56, bad_trap_handler)   /* Undefined...                  */
t_bad57:TRAP_ENTRY(0x57, bad_trap_handler)   /* Undefined...                  */
t_bad58:TRAP_ENTRY(0x58, bad_trap_handler)   /* Undefined...                  */
t_bad59:TRAP_ENTRY(0x59, bad_trap_handler)   /* Undefined...                  */
t_bad5a:TRAP_ENTRY(0x5a, bad_trap_handler)   /* Undefined...                  */
t_bad5b:TRAP_ENTRY(0x5b, bad_trap_handler)   /* Undefined...                  */
t_bad5c:TRAP_ENTRY(0x5c, bad_trap_handler)   /* Undefined...                  */
t_bad5d:TRAP_ENTRY(0x5d, bad_trap_handler)   /* Undefined...                  */
t_bad5e:TRAP_ENTRY(0x5e, bad_trap_handler)   /* Undefined...                  */
t_bad5f:TRAP_ENTRY(0x5f, bad_trap_handler)   /* Undefined...                  */
t_bad60:TRAP_ENTRY(0x60, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad61:TRAP_ENTRY(0x61, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad62:TRAP_ENTRY(0x62, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad63:TRAP_ENTRY(0x63, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad64:TRAP_ENTRY(0x64, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad65:TRAP_ENTRY(0x65, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad66:TRAP_ENTRY(0x66, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad67:TRAP_ENTRY(0x67, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad68:TRAP_ENTRY(0x68, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad69:TRAP_ENTRY(0x69, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad6a:TRAP_ENTRY(0x6a, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad6b:TRAP_ENTRY(0x6b, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad6c:TRAP_ENTRY(0x6c, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad6d:TRAP_ENTRY(0x6d, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad6e:TRAP_ENTRY(0x6e, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad6f:TRAP_ENTRY(0x6f, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad70:TRAP_ENTRY(0x70, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad71:TRAP_ENTRY(0x71, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad72:TRAP_ENTRY(0x72, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad73:TRAP_ENTRY(0x73, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad74:TRAP_ENTRY(0x74, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad75:TRAP_ENTRY(0x75, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad76:TRAP_ENTRY(0x76, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad77:TRAP_ENTRY(0x77, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad78:TRAP_ENTRY(0x78, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad79:TRAP_ENTRY(0x79, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad7a:TRAP_ENTRY(0x7a, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad7b:TRAP_ENTRY(0x7b, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad7c:TRAP_ENTRY(0x7c, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad7d:TRAP_ENTRY(0x7d, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad7e:TRAP_ENTRY(0x7e, bad_trap_handler)   /* Impl-Dep Exception            */
t_bad7f:TRAP_ENTRY(0x7f, bad_trap_handler)   /* Impl-Dep Exception            */
t_sunos:SUNOS_SYSCALL_TRAP                   /* SunOS System Call             */
t_sbkpt:TRAP_ENTRY(0x81, bad_trap_handler)   /* Software Breakpoint           */
t_divz:	TRAP_ENTRY(0x82, bad_trap_handler)   /* Divide by zero trap           */
t_flwin:TRAP_ENTRY(0x83, bad_trap_handler)   /* Flush Windows Trap            */
t_clwin:TRAP_ENTRY(0x84, bad_trap_handler)   /* Clean Windows Trap            */
t_rchk:	TRAP_ENTRY(0x85, bad_trap_handler)   /* Range Check                   */
t_funal:TRAP_ENTRY(0x86, bad_trap_handler)   /* Fix Unaligned Access Trap     */
t_iovf:	TRAP_ENTRY(0x87, bad_trap_handler)   /* Integer Overflow Trap         */
t_slowl:SOLARIS_SYSCALL_TRAP                 /* Slowaris System Call          */
t_netbs:NETBSD_SYSCALL_TRAP                  /* Net-B.S. System Call          */
t_bad8a:TRAP_ENTRY(0x8a, bad_trap_handler)   /* Software Trap                 */
t_bad8b:TRAP_ENTRY(0x8b, bad_trap_handler)   /* Software Trap                 */
t_bad8c:TRAP_ENTRY(0x8c, bad_trap_handler)   /* Software Trap                 */
t_bad8d:TRAP_ENTRY(0x8d, bad_trap_handler)   /* Software Trap                 */
t_bad8e:TRAP_ENTRY(0x8e, bad_trap_handler)   /* Software Trap                 */
t_bad8f:TRAP_ENTRY(0x8f, bad_trap_handler)   /* Software Trap                 */
t_linux:LINUX_SYSCALL_TRAP                   /* Linux System Call             */
t_bad91:TRAP_ENTRY(0x91, bad_trap_handler)   /* Software Trap                 */
t_bad92:TRAP_ENTRY(0x92, bad_trap_handler)   /* Software Trap                 */
t_bad93:TRAP_ENTRY(0x93, bad_trap_handler)   /* Software Trap                 */
t_bad94:TRAP_ENTRY(0x94, bad_trap_handler)   /* Software Trap                 */
t_bad95:TRAP_ENTRY(0x95, bad_trap_handler)   /* Software Trap                 */
t_bad96:TRAP_ENTRY(0x96, bad_trap_handler)   /* Software Trap                 */
t_bad97:TRAP_ENTRY(0x97, bad_trap_handler)   /* Software Trap                 */
t_bad98:TRAP_ENTRY(0x98, bad_trap_handler)   /* Software Trap                 */
t_bad99:TRAP_ENTRY(0x99, bad_trap_handler)   /* Software Trap                 */
t_bad9a:TRAP_ENTRY(0x9a, bad_trap_handler)   /* Software Trap                 */
t_bad9b:TRAP_ENTRY(0x9b, bad_trap_handler)   /* Software Trap                 */
t_bad9c:TRAP_ENTRY(0x9c, bad_trap_handler)   /* Software Trap                 */
t_bad9d:TRAP_ENTRY(0x9d, bad_trap_handler)   /* Software Trap                 */
t_bad9e:TRAP_ENTRY(0x9e, bad_trap_handler)   /* Software Trap                 */
t_bad9f:TRAP_ENTRY(0x9f, bad_trap_handler)   /* Software Trap                 */
t_getcc:GETCC_TRAP                           /* Get Condition Codes           */
t_setcc:SETCC_TRAP                           /* Set Condition Codes           */
t_bada2:TRAP_ENTRY(0xa2, bad_trap_handler)   /* Software Trap                 */
t_bada3:TRAP_ENTRY(0xa3, bad_trap_handler)   /* Software Trap                 */
t_bada4:TRAP_ENTRY(0xa4, bad_trap_handler)   /* Software Trap                 */
t_bada5:TRAP_ENTRY(0xa5, bad_trap_handler)   /* Software Trap                 */
t_bada6:TRAP_ENTRY(0xa6, bad_trap_handler)   /* Software Trap                 */
t_bada7:TRAP_ENTRY(0xa7, bad_trap_handler)   /* Software Trap                 */
t_bada8:TRAP_ENTRY(0xa8, bad_trap_handler)   /* Software Trap                 */
t_bada9:TRAP_ENTRY(0xa9, bad_trap_handler)   /* Software Trap                 */
t_badaa:TRAP_ENTRY(0xaa, bad_trap_handler)   /* Software Trap                 */
t_badab:TRAP_ENTRY(0xab, bad_trap_handler)   /* Software Trap                 */
t_badac:TRAP_ENTRY(0xac, bad_trap_handler)   /* Software Trap                 */
t_badad:TRAP_ENTRY(0xad, bad_trap_handler)   /* Software Trap                 */
t_badae:TRAP_ENTRY(0xae, bad_trap_handler)   /* Software Trap                 */
t_badaf:TRAP_ENTRY(0xaf, bad_trap_handler)   /* Software Trap                 */
t_badb0:TRAP_ENTRY(0xb0, bad_trap_handler)   /* Software Trap                 */
t_badb1:TRAP_ENTRY(0xb1, bad_trap_handler)   /* Software Trap                 */
t_badb2:TRAP_ENTRY(0xb2, bad_trap_handler)   /* Software Trap                 */
t_badb3:TRAP_ENTRY(0xb3, bad_trap_handler)   /* Software Trap                 */
t_badb4:TRAP_ENTRY(0xb4, bad_trap_handler)   /* Software Trap                 */
t_badb5:TRAP_ENTRY(0xb5, bad_trap_handler)   /* Software Trap                 */
t_badb6:TRAP_ENTRY(0xb6, bad_trap_handler)   /* Software Trap                 */
t_badb7:TRAP_ENTRY(0xb7, bad_trap_handler)   /* Software Trap                 */
t_badb8:TRAP_ENTRY(0xb8, bad_trap_handler)   /* Software Trap                 */
t_badb9:TRAP_ENTRY(0xb9, bad_trap_handler)   /* Software Trap                 */
t_badba:TRAP_ENTRY(0xba, bad_trap_handler)   /* Software Trap                 */
t_badbb:TRAP_ENTRY(0xbb, bad_trap_handler)   /* Software Trap                 */
t_badbc:TRAP_ENTRY(0xbc, bad_trap_handler)   /* Software Trap                 */
t_badbd:TRAP_ENTRY(0xbd, bad_trap_handler)   /* Software Trap                 */
t_badbe:TRAP_ENTRY(0xbe, bad_trap_handler)   /* Software Trap                 */
t_badbf:TRAP_ENTRY(0xbf, bad_trap_handler)   /* Software Trap                 */
t_badc0:TRAP_ENTRY(0xc0, bad_trap_handler)   /* Software Trap                 */
t_badc1:TRAP_ENTRY(0xc1, bad_trap_handler)   /* Software Trap                 */
t_badc2:TRAP_ENTRY(0xc2, bad_trap_handler)   /* Software Trap                 */
t_badc3:TRAP_ENTRY(0xc3, bad_trap_handler)   /* Software Trap                 */
t_badc4:TRAP_ENTRY(0xc4, bad_trap_handler)   /* Software Trap                 */
t_badc5:TRAP_ENTRY(0xc5, bad_trap_handler)   /* Software Trap                 */
t_badc6:TRAP_ENTRY(0xc6, bad_trap_handler)   /* Software Trap                 */
t_badc7:TRAP_ENTRY(0xc7, bad_trap_handler)   /* Software Trap                 */
t_badc8:TRAP_ENTRY(0xc8, bad_trap_handler)   /* Software Trap                 */
t_badc9:TRAP_ENTRY(0xc9, bad_trap_handler)   /* Software Trap                 */
t_badca:TRAP_ENTRY(0xca, bad_trap_handler)   /* Software Trap                 */
t_badcb:TRAP_ENTRY(0xcb, bad_trap_handler)   /* Software Trap                 */
t_badcc:TRAP_ENTRY(0xcc, bad_trap_handler)   /* Software Trap                 */
t_badcd:TRAP_ENTRY(0xcd, bad_trap_handler)   /* Software Trap                 */
t_badce:TRAP_ENTRY(0xce, bad_trap_handler)   /* Software Trap                 */
t_badcf:TRAP_ENTRY(0xcf, bad_trap_handler)   /* Software Trap                 */
t_badd0:TRAP_ENTRY(0xd0, bad_trap_handler)   /* Software Trap                 */
t_badd1:TRAP_ENTRY(0xd1, bad_trap_handler)   /* Software Trap                 */
t_badd2:TRAP_ENTRY(0xd2, bad_trap_handler)   /* Software Trap                 */
t_badd3:TRAP_ENTRY(0xd3, bad_trap_handler)   /* Software Trap                 */
t_badd4:TRAP_ENTRY(0xd4, bad_trap_handler)   /* Software Trap                 */
t_badd5:TRAP_ENTRY(0xd5, bad_trap_handler)   /* Software Trap                 */
t_badd6:TRAP_ENTRY(0xd6, bad_trap_handler)   /* Software Trap                 */
t_badd7:TRAP_ENTRY(0xd7, bad_trap_handler)   /* Software Trap                 */
t_badd8:TRAP_ENTRY(0xd8, bad_trap_handler)   /* Software Trap                 */
t_badd9:TRAP_ENTRY(0xd9, bad_trap_handler)   /* Software Trap                 */
t_badda:TRAP_ENTRY(0xda, bad_trap_handler)   /* Software Trap                 */
t_baddb:TRAP_ENTRY(0xdb, bad_trap_handler)   /* Software Trap                 */
t_baddc:TRAP_ENTRY(0xdc, bad_trap_handler)   /* Software Trap                 */
t_baddd:TRAP_ENTRY(0xdd, bad_trap_handler)   /* Software Trap                 */
t_badde:TRAP_ENTRY(0xde, bad_trap_handler)   /* Software Trap                 */
t_baddf:TRAP_ENTRY(0xdf, bad_trap_handler)   /* Software Trap                 */
t_bade0:TRAP_ENTRY(0xe0, bad_trap_handler)   /* Software Trap                 */
t_bade1:TRAP_ENTRY(0xe1, bad_trap_handler)   /* Software Trap                 */
t_bade2:TRAP_ENTRY(0xe2, bad_trap_handler)   /* Software Trap                 */
t_bade3:TRAP_ENTRY(0xe3, bad_trap_handler)   /* Software Trap                 */
t_bade4:TRAP_ENTRY(0xe4, bad_trap_handler)   /* Software Trap                 */
t_bade5:TRAP_ENTRY(0xe5, bad_trap_handler)   /* Software Trap                 */
t_bade6:TRAP_ENTRY(0xe6, bad_trap_handler)   /* Software Trap                 */
t_bade7:TRAP_ENTRY(0xe7, bad_trap_handler)   /* Software Trap                 */
t_bade8:TRAP_ENTRY(0xe8, bad_trap_handler)   /* Software Trap                 */
t_bade9:TRAP_ENTRY(0xe9, bad_trap_handler)   /* Software Trap                 */
t_badea:TRAP_ENTRY(0xea, bad_trap_handler)   /* Software Trap                 */
t_badeb:TRAP_ENTRY(0xeb, bad_trap_handler)   /* Software Trap                 */
t_badec:TRAP_ENTRY(0xec, bad_trap_handler)   /* Software Trap                 */
t_baded:TRAP_ENTRY(0xed, bad_trap_handler)   /* Software Trap                 */
t_badee:TRAP_ENTRY(0xee, bad_trap_handler)   /* Software Trap                 */
t_badef:TRAP_ENTRY(0xef, bad_trap_handler)   /* Software Trap                 */
t_badf0:TRAP_ENTRY(0xf0, bad_trap_handler)   /* Software Trap                 */
t_badf1:TRAP_ENTRY(0xf1, bad_trap_handler)   /* Software Trap                 */
t_badf2:TRAP_ENTRY(0xf2, bad_trap_handler)   /* Software Trap                 */
t_badf3:TRAP_ENTRY(0xf3, bad_trap_handler)   /* Software Trap                 */
t_badf4:TRAP_ENTRY(0xf4, bad_trap_handler)   /* Software Trap                 */
t_badf5:TRAP_ENTRY(0xf5, bad_trap_handler)   /* Software Trap                 */
t_badf6:TRAP_ENTRY(0xf6, bad_trap_handler)   /* Software Trap                 */
t_badf7:TRAP_ENTRY(0xf7, bad_trap_handler)   /* Software Trap                 */
t_badf8:TRAP_ENTRY(0xf8, bad_trap_handler)   /* Software Trap                 */
t_badf9:TRAP_ENTRY(0xf9, bad_trap_handler)   /* Software Trap                 */
t_badfa:TRAP_ENTRY(0xfa, bad_trap_handler)   /* Software Trap                 */
t_badfb:TRAP_ENTRY(0xfb, bad_trap_handler)   /* Software Trap                 */
t_badfc:TRAP_ENTRY(0xfc, bad_trap_handler)   /* Software Trap                 */
t_badfd:TRAP_ENTRY(0xfd, bad_trap_handler)   /* Software Trap                 */
dbtrap:	TRAP_ENTRY(0xfe, bad_trap_handler)   /* Debugger/PROM breakpoint #1   */
dbtrap2:TRAP_ENTRY(0xff, bad_trap_handler)   /* Debugger/PROM breakpoint #2   */	

	.globl	C_LABEL(end_traptable)
C_LABEL(end_traptable):

	.skip 4096

/* This was the only reasonable way I could think of to properly align
 * these page-table data structures.
 *
 * XXX swapper_pg_dir is going to have to be 'per-CPU' for SMP support
 */

	.globl C_LABEL(auxio_reg_addr)
C_LABEL(auxio_reg_addr):	.skip	(PAGE_SIZE)

	.globl C_LABEL(clock_reg_addr)
C_LABEL(clock_reg_addr):	.skip	(PAGE_SIZE*5)

	.globl C_LABEL(int_reg_addr)
C_LABEL(int_reg_addr):		.skip	(PAGE_SIZE*5)

	.globl C_LABEL(pg0)
	.globl C_LABEL(empty_bad_page)
	.globl C_LABEL(empty_bad_page_table)
	.globl C_LABEL(empty_zero_page)
	.globl C_LABEL(swapper_pg_dir)
C_LABEL(swapper_pg_dir):		.skip 0x1000
C_LABEL(pg0):				.skip 0x1000
C_LABEL(empty_bad_page):		.skip 0x1000
C_LABEL(empty_bad_page_table):		.skip 0x1000
C_LABEL(empty_zero_page):		.skip 0x1000


/* Cool, here we go. Pick up the romvec pointer in %o0 and stash it in
 * %g7 and at prom_vector_p. And also quickly check whether we are on
 * a v0, v2, or v3 prom.  We also get a debug structure of some sort from
 * the boot loader (or is it the prom?) in %o1.  Finally a call back vector
 * is passed in %o2.  I think this is how you register yourself with a
 * debugger.  I do know that it wants my %o7 (return PC - 8) as it's
 * first argument.  I will poke around and figure out what the debug
 * vector is, it could contain useful stuff.
 */

/* Grrr, in order to be Sparc ABI complient, the kernel has to live in
 * an address space above 0xe0000000  ;(  Must remain position independant
 * until we 'remap' ourselves from low to high addresses.  We only map the
 * first 3MB of addresses into upper ram as that is how much the PROM
 * promises to set up for us.
 */
gokernel:
		/* Ok, it's nice to know, as early as possible, if we
		 * are already mapped where we expect to be in virtual
		 * memory.  The Solaris /boot elf format bootloader
		 * will peek into our elf header and load us where
		 * we want to be, otherwise we have to re-map.
		 *
		 * Some boot loaders don't place the jmp'rs address
		 * in %o7, so we do a pc-relative call to a local
		 * label, then see what %o7 has.
		 */

		/* XXX Sparc V9 detection goes here XXX */

		or	%g0, %o7, %g4		! Save %o7

		/* Jump to it, and pray... */
current_pc:
		call	1f
		nop

1:
		or	%g0, %o7, %g3

got_pc:
		or	%g0, %g4, %o7	/* Previous %o7. */
	
		or	%g0, %o0, %l0		! stash away romvec
		or	%g0, %o0, %g7		! put it here too
		or	%g0, %o1, %l1		! stash away debug_vec too
		rd	%psr, %l2		! Save psr
		rd	%wim, %l3		! wim
		rd	%tbr, %l4		! tbr
		or	%g0, %o2, %l5		! and the possible magic func

		/* Ok, let's check out our run time program counter. */
		set	current_pc, %g5
		cmp	%g3, %g5
		be	already_mapped

		/* %l6 will hold the offset we have to subtract
		 * from absolute symbols in order to access areas
		 * in our own image.  If already mapped this is
		 * just plain zero, else it is PAGE_OFFSET which is
		 * also KERNBASE.
		 */
		set	PAGE_OFFSET, %l6
		b	copy_prom_lvl14
		nop

already_mapped:
		or	%g0, %g0, %l6

		/* Copy over the Prom's level 14 clock handler. */
copy_prom_lvl14:
		rd	%tbr, %g1
		andn	%g1, 0xfff, %g1		! proms trap table base
		or	%g0, (0x1e<<4), %g2	! offset to lvl14 intr
		or	%g1, %g2, %g2
		set	t_irq14, %g3
		sub	%g3, %l6, %g3
		ldd	[%g2], %g4
		std	%g4, [%g3]
		ldd	[%g2 + 0x8], %g4
		std	%g4, [%g3 + 0x8]	! Copy proms handler

		/* Copy over the Prom/debugger's trap entry points. */
copy_prom_bpoint:
		or	%g0, (0xfe<<4), %g2
		or	%g1, %g2, %g2
		set	dbtrap, %g3
		sub	%g3, %l6, %g3
		ldd	[%g2], %g4
		std	%g4, [%g3]
		ldd	[%g2 + 0x8], %g4
		std	%g4, [%g3 + 0x8]
		ldd	[%g2 + 0x10], %g4
		std	%g4, [%g3 + 0x10]
		ldd	[%g2 + 0x18], %g4
		std	%g4, [%g3 + 0x18]

copy_prom_done:
		ld	[%o0 + 0x4], %g1
		and	%g1, 0x3, %g1
		subcc	%g1, 0x0, %g0
		be	set_sane_psr		! Not on v0 proms
		nop

		subcc	%o2, 0x0, %g0		! check for boot routine pointer
		bz	set_sane_psr
		nop
		jmpl    %o2, %o7		! call boot setup func
		add	%o7, 0x8, %o0

set_sane_psr:
		/* Traps are on, kadb can be tracing through here.  But
		 * we have no clue what the PIL is. So we set up a sane
		 * %psr, but preserve CWP or our locals could disappear!
		 */
		rd	%psr, %g2
		and	%g2, 0x1f, %g2			! %g2 has CWP now
		set	(PSR_S|PSR_PIL), %g1		! Supervisor + PIL high
		or	%g1, %g2, %g1			! mix mix mix
		wr	%g1, 0x0, %psr			! let PIL set in
		wr	%g1, PSR_ET, %psr		! now turn on traps

		/* A note about the last two instructions...
		 * If you are going to increase PIL and turn on
		 * traps at the same time you are asking for trouble.
		 * You MUST set a %psr with traps off containing
		 * your new PIL, then turn on the ET bit with the
		 * next write.  On certain buggy Sparc chips if you
		 * set both at the same time you can get a Watchdog
		 * Reset under certain conditions.  This is no fun.
		 * Basically the PIL bits get there before the
		 * EnableTrap bit does or something like that.
		 */

		/* Insane asylum... */
		WRITE_PAUSE

/* Must determine whether we are on a sun4c MMU, SRMMU, or SUN4/400 MUTANT
 * MMU so we can remap ourselves properly.  DONT TOUCH %l0 thru %l5 in these
 * remapping routines, we need their values afterwards!
 *
 * XXX UGH, need to write some sun4u SpitFire remapping V9 code RSN... XXX
 */
		/* Now check whether we are already mapped, if we
		 * are we can skip all this garbage coming up.
		 */
		subcc	%l6, 0x0, %g0
		bz	go_to_highmem		! this will be a nop then
		nop

		sethi	%hi(LOAD_ADDR), %g6
		subcc	%g7, %g6, %g0
		bne	remap_not_a_sun4	! This is not a Sun4
		nop

		or	%g0, 0x1, %g1
		lduba	[%g1] ASI_CONTROL, %g1	! Only safe to try on Sun4.
		subcc	%g1, 0x24, %g0		! Is this a mutant Sun4/400???
		be	sun4_mutant_remap	! Ugh, it is...
		nop

remap_not_a_sun4:
		lda	[%g0] ASI_M_MMUREGS, %g1 ! same as ASI_PTE on sun4c
		and	%g1, 0x1, %g1		! Test SRMMU Enable bit ;-)
		subcc	%g1, 0x0, %g0
		bz	sun4c_remap		! A sun4c MMU or normal Sun4
		nop
srmmu_remap:
		/* First, check for a viking (TI) module. */
		set	0x40000000, %g2
		rd	%psr, %g3
		and	%g2, %g3, %g3
		subcc	%g3, 0x0, %g0
		bz	srmmu_nviking
		nop

		/* Figure out what kind of viking we are on.
		 * We need to know if we have to play with the
		 * AC bit and disable traps or not.
		 */

		/* I've only seen MicroSparc's on SparcClassics with this
		 * bit set.
		 */
		set	0x800, %g2
		lda	[%g0] ASI_M_MMUREGS, %g3	! peek in the control reg
		and	%g2, %g3, %g3
		subcc	%g3, 0x0, %g0
		bnz	srmmu_nviking			! is in mbus mode
		nop
		
		rd	%psr, %g3			! DONT TOUCH %g3
		andn	%g3, PSR_ET, %g2
		wr	%g2, 0x0, %psr
		WRITE_PAUSE
		
		/* Get context table pointer, then convert to
		 * a physical address, which is 36 bits.
		 */
		set	AC_M_CTPR, %g4
		lda	[%g4] ASI_M_MMUREGS, %g4
		sll	%g4, 0x4, %g4			! We use this below
							! DONT TOUCH %g4

		/* Set the AC bit in the Viking's MMU control reg. */
		lda	[%g0] ASI_M_MMUREGS, %g5	! DONT TOUCH %g5
		set	0x8000, %g6			! AC bit mask
		or	%g5, %g6, %g6			! Or it in...
		sta	%g6, [%g0] ASI_M_MMUREGS	! Close your eyes...

		/* Grrr, why does it seem like every other load/store
		 * on the sun4m is in some ASI space...
		 * Fine with me, let's get the pointer to the level 1
		 * page table directory and fetch it's entry.
		 */
		lda	[%g4] ASI_M_BYPASS, %o1		! This is a level 1 ptr
		srl	%o1, 0x4, %o1			! Clear low 4 bits
		sll	%o1, 0x8, %o1			! Make physical
		
		/* Ok, pull in the PTD. */
		lda	[%o1] ASI_M_BYPASS, %o2		! This is the 0x0 16MB pgd

		/* Calculate to KERNBASE entry.
		 *
		 * XXX Should not use imperical constant, but Gas gets an  XXX
		 * XXX upset stomach with the bitshift I would have to use XXX
		 */
		add	%o1, 0x3c0, %o3		

		/* Poke the entry into the calculated address. */
		sta	%o2, [%o3] ASI_M_BYPASS

		/* I don't get it Sun, if you engineered all these
		 * boot loaders and the PROM (thank you for the debugging
		 * features btw) why did you not have them load kernel
		 * images up in high address space, since this is necessary
		 * for ABI compliance anyways?  Does this low-mapping provide
		 * enhanced interoperability?
		 *
		 * "The PROM is the computer."
		 */

		/* Ok, restore the MMU control register we saved in %g5 */
		sta	%g5, [%g0] ASI_M_MMUREGS	! POW... ouch

		/* Turn traps back on.  We saved it in %g3 earlier. */
		wr	%g3, 0x0, %psr			! tick tock, tick tock

		/* Now we burn precious CPU cycles due to bad engineering. */
		WRITE_PAUSE

		/* Wow, all that just to move a 32-bit value from one
		 * place to another...  Jump to high memory.
		 */
		b	go_to_highmem
		nop

		/* This works on viking's in Mbus mode and all
		 * other MBUS modules.  It is virtually the same as
		 * the above madness sans turning traps off and flipping
		 * the AC bit.
		 */
srmmu_nviking:
		set	AC_M_CTPR, %g1
		lda	[%g1] ASI_M_MMUREGS, %g1	! get ctx table ptr
		sll	%g1, 0x4, %g1			! make physical addr
		lda	[%g1] ASI_M_BYPASS, %g1		! ptr to level 1 pg_table
		srl	%g1, 0x4, %g1
		sll	%g1, 0x8, %g1			! make phys addr for l1 tbl

		lda	[%g1] ASI_M_BYPASS, %g2		! get level1 entry for 0x0
		add	%g1, 0x3c0, %g3			! XXX AWAY WITH IMPERICALS
		sta	%g2, [%g3] ASI_M_BYPASS		! place at KERNBASE entry
		b	go_to_highmem
		nop					! wheee....

		/* This remaps the kernel on Sun4/4xx machines
		 * that have the Sun Mutant Three Level MMU.
		 * It's like a platypus, Sun didn't have the
		 * SRMMU in conception so they kludged the three
		 * level logic in the regular Sun4 MMU probably.
		 *
		 * Basically, you take each entry in the top level
		 * directory that maps the low 3MB starting at
		 * address zero and put the mapping in the KERNBASE
		 * slots.  These top level pgd's are called regmaps.
		 */
sun4_mutant_remap:
		or	%g0, %g0, %g3		! source base
		sethi	%hi(KERNBASE), %g4	! destination base
		or	%g4, %lo(KERNBASE), %g4
		sethi	%hi(0x300000), %g5
		or	%g5, %lo(0x300000), %g5	! upper bound 3MB
		or	%g0, 0x1, %l6
		sll	%l6, 24, %l6		! Regmap mapping size
		add	%g3, 0x2, %g3		! Base magic
		add	%g4, 0x2, %g4		! Base magic

		/* Main remapping loop on Sun4-Mutant-MMU.
		 * "I am not an animal..." -Famous Mutant Person
		 */
sun4_mutant_loop:
		lduha	[%g3] ASI_REGMAP, %g2	! Get lower entry
		stha	%g2, [%g4] ASI_REGMAP	! Store in high entry
		add	%g4, %l6, %g4		! Move up high memory ptr
		subcc	%g3, %g5, %g0		! Reached our limit?
		blu	sun4_mutant_loop	! Nope, loop again
		add	%g3, %l6, %g3		! delay, Move up low ptr
		b	go_to_highmem		! Jump to high memory.
		nop

/* The following works for normal (ie. non Sun4/400) Sun4 MMU's */
sun4c_remap:
		or	%g0, %g0, %g3		! source base
		sethi	%hi(KERNBASE), %g4	! destination base
		or	%g4, %lo(KERNBASE), %g4
		sethi	%hi(0x300000), %g5
		or	%g5, %lo(0x300000), %g5	! upper bound 3MB
		or	%g0, 0x1, %l6
		sll	%l6, 18, %l6		! sun4c mmu segmap size
sun4c_remap_loop:
		lda	[%g3] ASI_SEGMAP, %g6	! load phys_seg
		sta	%g6, [%g4] ASI_SEGMAP   ! store new virt mapping
		add	%g3, %l6, %g3		! Increment source ptr
		subcc	%g3, %g5, %g0		! Reached limit?
		bl	sun4c_remap_loop	! Nope, loop again
		add	%g4, %l6, %g4		! delay, Increment dest ptr

/* Now do a non-relative jump so that PC is in high-memory */
go_to_highmem:
		set	execute_in_high_mem, %g1
		jmp	%g1
		nop

/* Acquire boot time privileged register values, this will help debugging.
 * I figure out and store nwindows and nwindowsm1 later on.
 */
execute_in_high_mem:
		or	%g0, %l0, %o0		! put back romvec
		or	%g0, %l1, %o1		! and debug_vec

		sethi	%hi( C_LABEL(prom_vector_p) ), %g1
		st	%o0, [%g1 + %lo( C_LABEL(prom_vector_p) )]

		sethi	%hi( C_LABEL(linux_dbvec) ), %g1
		st	%o1, [%g1 + %lo( C_LABEL(linux_dbvec) )]

		ld	[%o0 + 0x4], %o3
		and	%o3, 0x3, %o5			! get the version

		subcc	%o3, 0x2, %g0			! a v2 prom?
		be	found_version
		nop

		/* paul@sfe.com.au */
		subcc	%o3, 0x3, %g0			! a v3 prom?
		be	found_version
		nop

/* Old sun4's pass our load address into %o0 instead of the prom
 * pointer. On sun4's you have to hard code the romvec pointer into
 * your code. Sun probably still does that because they don't even
 * trust their own "OpenBoot" specifications.
 */

		sethi	%hi(LOAD_ADDR), %g6
		subcc	%o0, %g6, %g0		! an old sun4?
		be	no_sun4_here
		nop

found_version:

/* Get the machine type via the mysterious romvec node operations.
 * Here we can find out whether we are on a sun4, sun4c, sun4m, 
 * sun4d, or a sun4e. The "nodes" are set up as a bunch of n-ary trees 
 * which you can traverse to get information about devices and such.
 * The information acquisition happens via the node-ops which are
 * defined in the openprom.h header file. Of particular interest
 * is the 'nextnode(int node)' function as it does the smart thing when
 * presented with a value of '0', it gives you the root node in the
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
		or	%o0, %g0, %g6

		sethi	%hi( C_LABEL(cputypvar) ), %o1	! First node has cpu-arch
		or	%o1, %lo( C_LABEL(cputypvar) ), %o1
		sethi	%hi( C_LABEL(cputypval) ), %o2	! information, the string
		or	%o2, %lo( C_LABEL(cputypval) ), %o2
		ld	[%l1], %l0		! 'compatibility' tells
		ld	[%l0 + 0xc], %l0	! that we want 'sun4x' where
		call	%l0			! x is one of '', 'c', 'm',
		nop				! 'd' or 'e'. %o2 holds pointer
						! to a buf where above string
						! will get stored by the prom.

		subcc	%o0, %g0, %g0
		bpos	got_prop		! Got the property
		nop

		or	%g6, %g0, %o0
		sethi	%hi( C_LABEL(cputypvar_sun4m) ), %o1
		or	%o1, %lo( C_LABEL(cputypvar_sun4m) ), %o1
		sethi	%hi( C_LABEL(cputypval) ), %o2
		or	%o2, %lo( C_LABEL(cputypval) ), %o2
		ld	[%l1], %l0
		ld	[%l0 + 0xc], %l0
		call	%l0
		nop

got_prop:
		sethi	%hi( C_LABEL(cputypval) ), %o2
		or	%o2, %lo( C_LABEL(cputypval) ), %o2

		ldub	[%o2 + 0x4], %l1
		subcc	%l1, 'c', %g0		! We already know we are not
		be	1f			! on a plain sun4 because of
		nop				! the check for 0x4000 in %o0
		subcc	%l1, 'm', %g0		! at start
		be	1f
		nop

		subcc	%l1, 'd', %g0
		be	no_sun4d_here		! God bless the person who
		nop				! tried to run this on sun4d.
	
		subcc	%l1, 'e', %g0
		be	no_sun4e_here		! Could be a sun4e.
		nop

		b	no_sun4u_here		! AIEEE, a V9 sun4u...
		nop


1:
		or	%g0, PAGE_SHIFT, %g5

		sethi	%hi( C_LABEL(cputypval) ), %l1
		or	%l1, %lo( C_LABEL(cputypval) ), %l1
		ldub	[%l1 + 0x4], %l1
		subcc	%l1, 'm', %g0           ! Test for sun4d, sun4e ?
		be	sun4m_init
		nop

		sethi	%hi(AC_CONTEXT), %g1	! kernel context, safe now
						! the only valid context
						! until we call paging_init()
		stba	%g0, [%g1] ASI_CONTROL

		b	sun4c_continue_boot
		nop

sun4m_init:
/* P3: I just do not know what to do here. But I do know that ASI_CONTROL
 * will not serve on sun4m. Also I do not want to smash the current MMU
 * setup until we call paging_init().
 */

/* Ok, the PROM could have done funny things and apple cider could still
 * be sitting in the fault status/address registers.  Read them all to
 * clear them so we don't get magic faults later on.
 */
/* This sucks, aparently this makes Vikings call prom panic, will fix later */

		set	(0x40000000), %o1
		rd	%psr, %o0
		andcc	%o0, %o1, %g0
		bne	sun4c_continue_boot	! quick hack
		nop

clr_srmmu_fregs:
		set	AC_M_SFSR, %o0
		lda	[%o0] ASI_M_MMUREGS, %g0
		set	AC_M_SFAR, %o0
		lda	[%o0] ASI_M_MMUREGS, %g0
		set	AC_M_AFSR, %o0
		lda	[%o0] ASI_M_MMUREGS, %g0
		set	AC_M_AFAR, %o0
		lda	[%o0] ASI_M_MMUREGS, %g0
		nop


sun4c_continue_boot:


/* Aieee, now set PC and nPC, enable traps, give ourselves a stack and it's
 * show-time!
 */

		sethi	%hi( C_LABEL(cputyp) ), %o0
		st	%g4, [%o0 + %lo( C_LABEL(cputyp) )]

		/* Turn on PreviousSupervisor, Supervisor, EnableFloating,
		 * and all the PIL bits.  Also puts us in register window
		 * zero.
		 */
		sethi	%hi(PSR_PS | PSR_S | PSR_PIL | PSR_EF), %g2
		or	%g2, %lo(PSR_PS | PSR_S | PSR_PIL | PSR_EF), %g2
		wr	%g2, 0x0, %psr
		WRITE_PAUSE

		wr	%g0, 0x2, %wim		! Make window 1 invalid.
		WRITE_PAUSE

		/* Initialize the WIM value for init_task. */
		or	%g0, 0x1, %g1
		sethi	%hi( C_LABEL(current) + THREAD_WIM), %g2
		st	%g1, [%g2 + %lo( C_LABEL(current) + THREAD_WIM)]

/* I want a kernel stack NOW! */
/* Grrr, gotta be real careful about alignment here */

		set	( C_LABEL(init_user_stack) + PAGE_SIZE - 96 - 96 - 80), %g1
		andn	%g1, 0x7, %g1
		or	%g1, 0x0, %fp
		add	%fp, (96+80), %sp

		/* Enable traps. */
		rd	%psr, %l0
		wr	%l0, PSR_ET, %psr
		WRITE_PAUSE

/*
 * Maybe the prom zeroes out our BSS section, maybe it doesn't. I certainly 
 * don't know, do you?
 */

		set	C_LABEL(edata) , %o0	! First address of BSS
		set	C_LABEL(end) , %o1	! Last address of BSS

		/* Argh, ELF gets me again... */
		andn	%o0, 0x3, %o0
		andn	%o1, 0x3, %o1

/* Friggin' bzero() kludge. */

1:	
		st	%g0, [%o0]
		add	%o0, 0x4, %o0
		subcc	%o0, %o1, %g0
		bl	1b
		nop

/* Compute NWINDOWS and stash it away. Now uses %wim trick explained
 * in the V8 manual. Ok, this method seems to work, Sparc is cool...
 * No, it doesn't work, have to play the save/readCWP/restore trick.
 */

		rd	%wim, %g1
		rd	%psr, %g2
		wr	%g0, 0x0, %wim			! so we dont get a trap
		andn	%g2, 0x1f, %g3
		wr	%g3, 0x0, %psr
		WRITE_PAUSE
		save
		rd	%psr, %g3
		restore
		and	%g3, 0x1f, %g3
		add	%g3, 0x1, %g3
		wr	%g2, 0x0, %psr
		wr	%g1, 0x0, %wim

		cmp	%g3, 0x7
		bne,a	2f
		sethi	%hi( C_LABEL(nwindows) ), %g4


		/* Nop out one save and one restore in the save state code
		 * and system call entry if this is a seven window Sparc.
		 */
		sethi	%hi(nop7), %g5
		or	%g5, %lo(nop7), %g5
		sethi	%hi(NOP_INSN), %g6
		or	%g6, %lo(NOP_INSN), %g6
		/* patch 1 */
		st	%g6, [%g5]
		st	%g6, [%g5 + 0x4]
		sethi	%hi(rnop7), %g5
		or	%g5, %lo(rnop7), %g5
		/* patch 2 */
		st	%g6, [%g5]
		st	%g6, [%g5 + 0x4]

		sethi	%hi( C_LABEL(nwindows) ), %g4

2:		
		st	%g3, [%g4 + %lo( C_LABEL(nwindows) )]	! store final value
		sub	%g3, 0x1, %g3
		sethi	%hi( C_LABEL(nwindowsm1) ), %g4
		st	%g3, [%g4 + %lo( C_LABEL(nwindowsm1) )]

		/* Initialize lnx_winmask. */
		set	lnx_winmask, %g4
		or	%g0, 0x2, %g5
		or	%g0, 0x0, %g6
msk_loop:
		stb	%g5, [%g4 + %g6]
		add	%g6, 0x1, %g6
		cmp	%g6, %g3
		bl,a	msk_loop
		sll	%g5, 0x1, %g5
		or	%g0, 0x1, %g5
		stb	%g5, [%g4 + %g3]

		/* Here we go */
		set	C_LABEL(trapbase), %g3
		wr	%g3, 0x0, %tbr
		WRITE_PAUSE


/* First we call prom_init() to set up PROMLIB, then off to start_kernel() */
/* XXX put this in setup_arch() */

		sethi	%hi( C_LABEL(prom_vector_p) ), %g5
		call	C_LABEL(prom_init)
		ld	[%g5 + %lo( C_LABEL(prom_vector_p) )], %o0

		subcc	%o0, 0x1, %g0
		be	halt_me			! promlib init failed
		nop

		call 	C_LABEL(start_kernel)
		nop
	
		/* We should not get here. */
		call	halt_me
		nop

/* There, happy now Adrian? */

		/* XXX Fix this... XXX */
no_sun4_here:
		sethi	%hi(SUN4_PROM_VECTOR+SUN4_PRINTF), %o1
		ld	[%o1 + %lo(SUN4_PROM_VECTOR+SUN4_PRINTF)], %o1
		set	sun4_notsup, %o0
		call	%o1
		nop
1:
		ba	1b			! Cannot exit into KMON
		nop

no_sun4d_here:
		ld	[%g7 + 0x68], %o1
		set	sun4d_notsup, %o0
		call	%o1
		nop
		b	halt_me
		nop

no_sun4e_here:
		ld	[%g7 + 0x68], %o1
		set	sun4e_notsup, %o0
		call	%o1
		nop
		b	halt_me
		nop

no_sun4u_here:
		ld	[%g7 + 0x68], %o1
		set	sun4u_notsup, %o0
		call	%o1
		nop
		b	halt_me
		nop

halt_me:
		ld	[%g7 + 0x74], %o0
		call	%o0			! Get us out of here...
		nop				! Apparently Solaris is better.

	.data
	.align 4

/*
 * Fill up the prom vector, note in particular the kind first element,
 * no joke. I don't need all of them in here as the entire prom vector
 * gets initialized in c-code so all routines can use it.
 */

			.globl C_LABEL(prom_vector_p)
C_LABEL(prom_vector_p):	.skip 4

	.align 4

/* We calculate the following at boot time, window fills/spills and trap entry
 * code uses these to keep track of the register windows.
 */

	.globl C_LABEL(nwindows)
	.globl C_LABEL(nwindowsm1)
C_LABEL(nwindows):	.skip 4
C_LABEL(nwindowsm1):	.skip 4

	.align 4

/* Boot time debugger vector value.  We need this later on. */

	.globl C_LABEL(linux_dbvec)
C_LABEL(linux_dbvec):		.skip 4

	.align 4

/* Just to get the kernel through the compiler for now */
	.globl C_LABEL(floppy_track_buffer)
C_LABEL(floppy_track_buffer):
	.fill 512*2*36,1,0
