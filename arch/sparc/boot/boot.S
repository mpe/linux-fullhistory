#include "boot.h"
#include "version.h"

	.data

/* First thing to go in the data segment is the interrupt stack */

        .globl  _intstack
        .globl  _eintstack
_intstack:
        .skip   4 * NBPG                ! 16k = 128 128-byte stack frames
_eintstack:



/* 
   The following are used with the prom_vector node-ops to figure out
   the cpu-type 
*/

        .globl  _cputyp

_cputyp:
        .word   1

_cputypval:
	.asciz "sun4c"
	.ascii "     "

_cputypvar:
	.asciz "compatability"

_cputypvallen = _cputypvar - _cputypval


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


/* The following will disappear real soon as the implementation is easy */

v2_prom_notyet:
	.asciz  "Sparc-Linux: v2 boot-prom support not implemented\n\n"
	.align 4


/*
   Fill up the prom vector, note in particular the kind first element,
   no joke.
*/

			.globl _prom_vector_p

_prom_vector_p:		.skip 4
prom_magic:  		.skip 4			! magic mushroom, beware...
prom_rom_vers: 		.skip 4			! interface version (v0 or v2)
prom_pluginvers:	.skip 4			! XXX help help help
prom_revision:		.skip 4			! PROM revision (ie. 1.4)
prom_bootstr:		.skip 4			! what we are invoked with
prom_putchar:		.skip 4			! void putchar(int ch)  BLOCKING.
prom_getchar:		.skip 4			! int getchar(void)     BLOCKING.
prom_nputchar:		.skip 4			! int purchar(int ch)   non-block
prom_ngetchar:		.skip 4			! int getchar(void)     non-block
prom_halt:		.skip 4			! void halt(void)  solaris friend
prom_eval:		.skip 4			! void eval(int len, char* string)
prom_v0mem_desc:	.skip 4			! V0 memory descriptor list ptr.
prom_nodefuncs:		.skip 4			! Magical Node functions
prom_v0devfuncs:	.skip 4			! V0 device operations
prom_putstring:		.skip 4			! prom putstring()
prom_bootme:		.skip 4			! reset()
prom_printf:		.skip 4			! minimal printf()
prom_abort:		.skip 4			! "L1-A" magic cookie
						! must be mapped in ALL contexts
prom_ticks:		.skip 4			! number of ticks since reset
prom_sync:		.skip 4			! hook in prom for "sync" func
prom_v0bootarg:		.skip 4			! v0 prom boot arguements
prom_v2bootarg:		.skip 4			! same as above for v2 proms
prom_ethaddr_func:	.skip 4			! extract ethernet device address
prom_v2devfunc:		.skip 4			! ptr to v2 style device ops.
prom_xtra_array:	.skip 4			! who knows :-( help help
prom_setcontext:	.skip 4			! set context on sun4c
prom_stdin:		.skip 4			! prom stdin magic cookie
prom_stdout:		.skip 4			! prom stdout magic cookie


	.align 4

	.globl boot_msg

/*
   This gets forth eval'd, just 'cause I think forth is neat to have in a boot
   monitor :-) We may not need no steenkin BIOS but we do need the prom!
*/

boot_msg:	
	.ascii "Booting Sparc-Linux V0.00PRE-ALPHA (SUN4C) "
	.ascii WHO_COMPILED_ME 
	.asciz " \n"
	.align 4

	.globl boot_msg2

boot_msg2:
	.asciz "Booting Sparclinux V0.00 PRE-ALPHA on a (SUN4C)\n\n"

	.align 4

pstring1:
	.asciz "Prom Magic Cookie: 0x%x  "
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


newline:
	.asciz "\n"
	.align 4

	.text

        .globl  _msgbuf
msgbufsize = NBPG                       ! 1 page for msg buffer
_msgbuf = KERNBASE + NBPG


IE_reg_addr = _msgbuf + msgbufsize      ! this page not used; points to IEreg

/*
   ignore the following variable settings, I used them when I had
   no stinkin idea what the linker was doing with the symbols to
   get them in the right place for load time
*/

whereis_bootmsg = boot_msg-KERNBASE
whereis_kernbase = KERNBASE
whereis_prom_vector_p = _prom_vector_p-KERNBASE
whereis_prom_eval = prom_eval-KERNBASE
whereis_prom_halt = prom_halt-KERNBASE
	
	.globl	start
	.globl  _trapbase
start:
_trapbase:
	b dostart; nop; nop; nop	! we never get trap #0 it is special
					! TRAP code should go here, TODO :>

_msgbufmapped:
        .word   1


        .data
        .skip   32                      ! alignment byte & negative indicies
uwtab:  .skip   32                      ! u_char uwtab[-31..31];
wmask:  .skip   32                      ! u_char wmask[0..31];

        .text
	

	
dostart:	mov	%o0, %g7
		st	%o0, [_prom_vector_p]   ! we will need it later
		ld	[%g7 + 0x4], %o2
		cmp	%o2, 2			! a v2 prom?
		be	_no_v2_here
		nop

		set	0x4000, %g6
		cmp	%o0, %g6		! an old sun4?
		beq	no_sun4_here
		nop

		ld	[%g7 + 0x1c], %o4
		ld	[%o4], %o4
		call 	%o4
		mov	0, %o0
		set	_cputypvar, %o1
		set	_cputypval, %o2
		ld	[%g7 + 0x1c], %o4
		ld	[%o4 + 0x0c], %o4
		call	%o4
		nop

		set	_cputypval, %o2
		ldub	[%o2 + 4], %o0
		cmp	%o0, 'c'
		beq	is_sun4c
		nop
		cmp	%o0, 'm'
		beq	no_sun4m_here
		nop
		b	no_sun4d_here
		nop

is_sun4c:					! OK, this is a sun4c, yippie
		mov 	%g7, %g6		! load up them promvec offsets
		st	%g6, [prom_magic]     	! magic mushroom :>
		add	%g7, 0x4, %g6
		st	%g6, [prom_rom_vers]
		add	%g7, 0x8, %g6
		st	%g6, [prom_pluginvers]
		add	%g7, 0xc, %g6
		st	%g6, [prom_revision]
		add	%g7, 0x10, %g6
		st	%g6, [prom_v0mem_desc]
		add	%g7, 0x1c, %g6
		st	%g6, [prom_nodefuncs]
		add	%g7, 0x20, %g6
		st	%g6, [prom_bootstr]
		add	%g7, 0x24, %g6
		st	%g6, [prom_v0devfuncs]
		add	%g7, 0x48, %g6
		st	%g6, [prom_stdin]
		add	%g7, 0x4c, %g6
		st	%g6, [prom_stdout]
		add	%g7, 0x54, %g6
		st	%g6, [prom_putchar]
		add	%g7, 0x50, %g6
		st	%g6, [prom_getchar]
		add	%g7, 0x5c, %g6
		st	%g6, [prom_nputchar]
		add	%g7, 0x58, %g6
		st	%g6, [prom_ngetchar]
		add	%g7, 0x60, %g6
		st	%g6, [prom_putstring]
		add	%g7, 0x64, %g6
		st	%g6, [prom_bootme]
		add	%g7, 0x68, %g6
		st	%g6, [prom_printf]
		add	%g7, 0x6c, %g6
		st	%g6, [prom_abort]
		add	%g7, 0x70, %g6
		st	%g6, [prom_ticks]
		add	%g7, 0x74, %g6
		st	%g6, [prom_halt]
		add	%g7, 0x78, %g6
		st	%g6, [prom_sync]
		add	%g7, 0x7c, %g6
		st	%g6, [prom_eval]
		add	%g7, 0x80, %g6
		st	%g6, [prom_v0bootarg]
		add	%g7, 0x84, %g6
		st	%g6, [prom_ethaddr_func]
		add	%g7, 0x88, %g6
		st	%g6, [prom_v2bootarg]
		add	%g7, 0x98, %g6
		st	%g6, [prom_v2devfunc]
		add	%g7, 0xc8, %g6
		st	%g6, [prom_xtra_array]
		add	%g7, 0x104, %g6
		st	%g6, [prom_setcontext]


		set	boot_msg-KERNBASE, %o0	
		ld	[prom_printf-KERNBASE], %o2
		ld	[%o2], %o1
		call	%o1  			! print boot message #1
		nop
		set	newline-KERNBASE, %o0
		ld	[prom_printf-KERNBASE], %o2
		ld	[%o2], %o1
		call	%o1
		nop

		b	0f
		nop


0:		nop				! duh	
		set	pstring1-KERNBASE, %o0
		ld	[prom_printf-KERNBASE], %o3
		ld	[%o3], %o2
		ld	[prom_magic-KERNBASE], %o3
		ld	[%o3], %o1
		call	%o2
		nop; nop; nop

		set	pstring2-KERNBASE, %o0
		ld	[prom_printf-KERNBASE], %o3
		ld	[%o3], %o2
		ld	[prom_rom_vers-KERNBASE], %o3
		ld	[%o3], %o1
		call 	%o2
		nop; nop; nop

		set	pstring4-KERNBASE, %o0
		ld	[prom_printf-KERNBASE], %o5
		ld	[%o5], %o4
		ld	[_prom_vector_p], %l1
		ld	[%l1+16], %l2
		ld	[%l2], %l3
		ld	[%l3 + 8], %o1

/*
		ld	[%l1], %l2
		ld	[%l2 + 0x8], %o1    ! physical memory accumulator
*/
		ld 	[_prom_vector_p], %l1
		ld	[%l1 + 16], %l2
		ld	[%l2], %l3
		ld	[%l3], %l4

/* 		ld	[%l3], %l4  */
memloop:
		cmp	%l4, 0
		be 	mv_to_vmprom		! is there more?
		nop

		ld	[%l4 + 0x8], %l6	! aparently so...
		add	%o1, %l6, %o1
		b	memloop
		ld	[%l4], %l4

mv_to_vmprom:

		ld	[_prom_vector_p], %l0
		ld	[%l0 + 20], %l1
		ld	[%l1], %l2
		ld	[%l2 + 8], %o2		! memory accumulator

		ld	[_prom_vector_p], %l0
		ld	[%l0 + 20], %l1
		ld	[%l1], %l2
		ld	[%l2], %l4

memloop2:
		cmp	%l4, 0
		be 	mv_to_vmprom2		! is there more?
		nop
		ld	[%l4 + 0x8], %l6	! aparently so...
		add	%o2, %l6, %o2
		b	memloop2
		ld	[%l4], %l4

mv_to_vmprom2:

		ld	[_prom_vector_p], %l0
		ld	[%l0 + 24], %l1
		ld	[%l1], %l2
		ld	[%l2 + 8], %o3		! memory accumulator

		ld	[_prom_vector_p], %l0
		ld	[%l0 + 24], %l1
		ld	[%l1], %l2
		ld	[%l2], %l4

memloop3:
		cmp	%l4, 0
		be 	mv_to_vmprom3		! is there more?
		nop
		ld	[%l4 + 0x8], %l6	! aparently so...
		add	%o3, %l6, %o3
		b	memloop3
		ld	[%l4], %l4

mv_to_vmprom3:

		call 	%o4
		nop; nop; nop


		set	newline-KERNBASE, %o0
		ld	[prom_printf-KERNBASE], %o2
		ld	[%o2], %o1
		call	%o1
		nop

		b	halt_me
		nop

		.globl _no_v2_here
_no_v2_here:
		ld	[%g7 + 0x68], %o1
		set	v2_prom_notyet-KERNBASE, %o0
		call    %o1
		nop
		b	halt_me
		nop

no_sun4_here:
		ld	[%g7 + 0x68], %o1
		set	sun4_notsup, %o0
		call	%o1
		nop
		b	halt_me
		nop

no_sun4m_here:
		ld	[%g7 + 0x68], %o1
		set	sun4m_notsup, %o0
		call	%o1
		nop
		b	halt_me
		nop

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
		nop				! aparently solaris is better

_strlen:
		mov	%o0, %l1
		mov	%g0, %l3
		ldub	[%l1], %l2
		sll	%l2, 24, %l2
		sra	%l2, 24, %l2
len_loop:
		cmp 	%l2, 0
		be	len_loop_end
		nop
		add	%l3, 0x1, %l3
		add	%l1, 0x1, %l1
		ldub	[%l1], %l2
		sll	%l2, 24, %l2
		sra	%l2, 24, %l2
		b	len_loop
		nop

len_loop_end:
		mov 	%l3, %o0
		ret
		nop
		
