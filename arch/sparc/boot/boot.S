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
   already mapped for you, however as far as I can tell the virtual
   address cache is not turned on although the MMU is translating 
   things. You get loaded at 0xf8004000 exactly. So, when you link
   a boot-loadable object you want to do something like:

        ld -e start -T f8004000 -o mykernel myobj1.o myobj2.o ....

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
*/

#include "boot.h"
#include "version.h"

	.data

/* First thing to go in the data segment is the interrupt stack. */

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

/*
 * Sun people can't spell worth damn. "compatability" indeed.
 * At least we *know* we can't spell, and use a spell-checker.
 */
_cputypvar:
	.asciz "compatability"

_cputypvallen = _cputypvar - _cputypval

/* This hold the prom-interface-version number for either v0 or v2. */

	.align 4
	.globl 	_prom_iface_vers

_prom_iface_vers:	.skip 4

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

/*
   Fill up the prom vector, note in particular the kind first element,
   no joke.
*/

			.globl _prom_vector_p

_prom_vector_p:		.skip 4
prom_magic:  		.skip 4			! magic mushroom, beware...
prom_rom_vers: 		.skip 4			! interface version (v0 or v2)
prom_pluginvers:	.skip 4			! XXX help help help ???
prom_revision:		.skip 4			! PROM revision (ie. 1.4)
prom_bootstr:		.skip 4			! what we are invoked with
prom_putchar:		.skip 4			! void putchar(int ch)  BLOCKING.
prom_getchar:		.skip 4			! int getchar(void)     BLOCKING.
prom_nputchar:		.skip 4			! int putchar(int ch)   non-block
prom_ngetchar:		.skip 4			! int getchar(void)     non-block
prom_halt:		.skip 4			! void halt(void)  solaris friend
prom_eval:		.skip 4			! void eval(int len, char* string)
prom_v0mem_desc:	.skip 4			! V0 memory descriptor list ptr.
prom_nodefuncs:		.skip 4			! Magical Node functions
prom_v0devfuncs:	.skip 4			! V0 device operations
prom_putstring:		.skip 4			! prom putstring()
prom_bootme:		.skip 4			! reset()
prom_printf:		.skip 4			! minimal printf()

/* The prom_abort pointer MUST be mapped in all contexts, because if you
   don't then if a user process is running when you press the abort key
   sequence, all sorts of bad things can happen
*/

prom_abort:		.skip 4			! "L1-A" magic cookie
						! must be mapped in ALL contexts
prom_ticks:		.skip 4			! number of ticks since reset

/* prom_sync is a place where the kernel should place a pointer to a kernel
   function that when called will sync all pending information to the drives
   and then promptly return. If the kernel gets aborted with 'L1-A' one can
   give the 'sync' command to the boot prompt and this magic cookie gets
   executed. Nice feature eh?
*/

prom_sync:		.skip 4			! hook in prom for "sync" func
prom_v0bootarg:		.skip 4			! v0 prom boot arguments
prom_v2bootarg:		.skip 4			! same as above for v2 proms
prom_ethaddr_func:	.skip 4			! extract ethernet device address
prom_v2devfunc:		.skip 4			! ptr to v2 style device ops.
prom_xtra_array:	.skip 4			! who knows :-( help help
prom_setcontext:	.skip 4			! set context on sun4c
prom_stdin:		.skip 4			! prom stdin magic cookie
prom_stdout:		.skip 4			! prom stdout magic cookie


	.align 4

	.globl boot_msg

/* memory descriptor property strings, v2 = yuk yuk yuk  */
/* XXX how to figure out vm mapped by prom? May have to scan magic addresses */

mem_prop_physavail:	.asciz "available"
mem_prop_phystot:	.asciz "reg"

/* v2_memory descriptor struct kludged here for assembly, if it ain't broke */

		.align 4
v2_mem_struct: 	.skip 0xff

			.align 4
v2_printf_physavail:	.asciz "Physical Memory Available: 0x%x bytes"
v2_printf_phystot:	.asciz "Physical Memory: 0x%x bytes"

/* A place to store property strings returned from the prom 'node' funcs */

			.align 4
prop_string_buf:	.skip 32

prop_name:	.asciz "name"
		.align 4

current_node:	.skip 4
		.align 4


/* nice little boot message */

boot_msg:	
	.ascii "Booting Sparc-Linux V0.00PRE-ALPHA "
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

whereis_kernbase      = KERNBASE
whereis_prom_vector_p = _prom_vector_p-KERNBASE
	
/* Ok, things start to get interesting. We get linked such that 'start'
   is the entry symbol. However, it is real low in kernel address space
   and as such a nifty place to place the trap table. We achieve this goal
   by just jumping to 'dostart' for the first trap's entry as the sparc
   never receives the zero trap as it is real special.

   Each trap entry point is the size of 4 sparc instructions (or 4 bytes
   * 4 insns = 16 bytes). There are 128 hardware traps (some undefined
   or unimplemented) and 128 software traps (ditto).

   One of the instructions must be a branch. More often than not this
   will be to a trap handler entry point because it is completely
   impossible to handle any trap in 4 insns. I welcome anyone to 
   challenge this theory. :-)

   On entry into this table the hardware has loaded the program counter
   at which the trap occurred into register %l1 and the next program
   counter into %l2, this way we can return from the trap with a simple

           jmp %l1; rett %l2

   after properly servicing the trap. It wouldn't be a bad idea to load
   some more information into the local regs since we have technically
   2 or 3 instructions to play with besides the jmp to the 'real' trap
   handler (one can even go in the delay slot). For now I am going to put
   the %psr (processor status register) and the trap-type value in %l0
   and %l3 respectively.

   TODO: Write cheesy macros to make this table more manageable.
         Ugh, this shit is long...

*/

	.globl	start
	.globl  _trapbase
start:
_trapbase:
	b dostart; nop; nop; nop	! we never get trap #0 it is special
					! TRAP code should go here, TODO :>

_msgbufmapped:
        .word   1


/* The following two things point to window management tables. The first
   one is used to quickly look up how many user windows there are from
   trap-land. The second is used in a trap handler to determine if a rett
   instruction will land us smack inside the invalid window that possibly
   the trap was called to fix-up.
*/

          .data
          .skip   32                      ! alignment byte & negative indices
lnx_uw:       .skip   32                      ! u_char uwtab[-31..31];
lnx_winmask:  .skip   32                      ! u_char wmask[0..31];

        .text
	
	
/* Cool, here we go. Pick up the romvec pointer in %o0 and stash it in
   %g7 and at _prom_vector_p. And also quickly check whether we are on
   a v0 or v2 prom.
*/

dostart:	mov	%o0, %g7
		st	%o0, [_prom_vector_p]   ! we will need it later
		ld	[%g7 + 0x4], %o2
		cmp	%o2, 2			! a v2 prom?
		be	found_v2
		nop

/* Old sun4's pass our load address into %o0 instead of the prom
   pointer. On sun4's you have to hard code the romvec pointer into
   your code. Sun probably still does that because they don't even
   trust their own "OpenBoot" specifications.
*/

		set	0x4000, %g6
		cmp	%o0, %g6		! an old sun4?
		beq	no_sun4_here
		nop

		st	%g0, [_prom_iface_vers] ! useless, disappear soon
		b	not_v2
		nop

found_v2:
		set	0x2, %o5
		st	%o5, [_prom_iface_vers]

not_v2:

/* Get the machine type via the mysterious romvec node operations.
   Here we can find out whether we are on a sun4 sun4c, sun4m, or
   a sun4m. The "nodes" are set up as a bunch of n-ary trees which
   you can traverse to get information about devices and such. The
   information acquisition happens via the node-ops which are defined
   in the linux_openprom.h header file. Of particular interest is the
   'nextnode(int node)' function as it does the smart thing when
   presented with a value of '0', it gives you the first node in the
   tree. These node integers probably offset into some internal prom
   pointer table the openboot has. It's completely undocumented, so
   I'm not about to go sifting through the prom address space, but may
   do so if I get suspicious enough. :-)
*/

		mov	0, %o0			! next_node(0) = first_node
		ld	[%g7 + 0x1c], %o4
		ld	[%o4], %o4
		call 	%o4
		nop

		set	_cputypvar, %o1		! first node has cpu-arch
		set	_cputypval, %o2		! information, the string
		ld	[%g7 + 0x1c], %o4	! 'compatibility' tells
		ld	[%o4 + 0x0c], %o4	! that we want 'sun4x' where
		call	%o4			! x is one of '', 'c', 'm',
		nop				! 'd' or 'e'. %o2 holds pointer
						! to a buf where above string
						! will get stored by the prom.
		set	_cputypval, %o2
		ldub	[%o2 + 4], %o0
		cmp	%o0, 'c'		! we already know we are not
		beq	is_sun4c		! on a plain sun4 because of
		nop				! the check for 0x4000 in %o0
		cmp	%o0, 'm'		! at start:
		beq	is_sun4m
		nop
		b	no_sun4d_here		! god bless the person who
		nop				! tried to run this on sun4d

is_sun4m:
is_sun4c:					! OK, this is a sun4c, yippie
		mov 	%g7, %g6		! load up the promvec offsets
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

/* That was easy, now lets try to print some message on the screen.
   We have to be careful because the prom addressed things weird and
   we aren't really mapped into memory as far as the rom routines are
   concerned. So all addresses we have ourselves and would like the
   prom to actually use must be calculated as (addr - KERNBASE) in order
   for anything to work at all. We will map ourselves later before we
   call any c-code to avoid this hassle.
*/

		set	boot_msg-KERNBASE, %o0	
		ld	[prom_printf-KERNBASE], %o2
		ld	[%o2], %o1
		call	%o1  			! print boot message #1
		nop

_newline:	set	newline-KERNBASE, %o0
		ld	[prom_printf-KERNBASE], %o2
		ld	[%o2], %o1
		call	%o1
		nop

		b	0f
		nop				! damn delay slots...

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
		ld	[_prom_iface_vers], %o3
		ld	[%o3], %o1
		call 	%o2
		nop; nop; nop

/* Print out various bits of memory information. At this point
   I just cycle through the documented v0_prom memory lists for
   the values. They are linked lists and allow for description of
   non-contiguous physical memory configurations, thus the 'memloop'
   things to traverse the linked lists.
*/

/* Things are different for v0 and v2. v2 requires traversing the node trees
   and that really sucks.
*/

/* Another Note:
	The prom printf() function can take up to 5 arguments in registers
	%o1 -- %o5 , the format string goes in %o0. It is your usual libc
	printf() believe it or not.
*/

		cmp	%o0, 0x2
		be	v2_mem_probe
		nop

		set	pstring4-KERNBASE, %o0
		ld	[prom_printf-KERNBASE], %o5
		ld	[%o5], %o4
		ld	[_prom_vector_p], %l1
		ld	[%l1+16], %l2
		ld	[%l2], %l3
		ld	[%l3 + 8], %o1		! 'nbytes' memory accumulator

		ld 	[_prom_vector_p], %l1
		ld	[%l1 + 16], %l2
		ld	[%l2], %l3
		ld	[%l3], %l4

memloop:
		cmp	%l4, 0
		be 	mv_to_vmprom		! is there more?
		nop

		ld	[%l4 + 0x8], %l6	! apparently so...
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
		ld	[%l4 + 0x8], %l6	! apparently so...
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
		ld	[%l4 + 0x8], %l6	! apparently so...
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

no_sun4_here:
		ld	[%g7 + 0x68], %o1
		set	sun4_notsup, %o0
		call	%o1
		nop
		b	rest_of_boot			! next stage...
		nop

v2_mem_probe:
		set	you_lose-KERNBASE, %o0		! I just print this
		ld	[prom_printf-KERNBASE], %o1	! crap to debug my node
		ld	[%o1], %o2			! routines :-)
		call 	%o2
		nop

		st	%g0, [current_node]
		set	prop_string_buf, %o2
		or	%g0, %g0, %o0
		ld	[prop_name], %o1
		or	%g0, 31, %o3		

node_find_loop:
		ld	[prom_nodefuncs], %o4
		ld	[%o4 + 0xc], %o4
		call	%o4
		nop
		ld	[prop_string_buf], %l3
		cmp	%l3, 'm'
		bne	node_find_loop2
		ld	[prop_string_buf + 1], %l3
		cmp	%l3, 'e'
		bne	node_find_loop2
		ld	[prop_string_buf + 2], %l3
		cmp	%l3, 'm'
		bne	node_find_loop2
		nop
		b	found_mem_node
		nop

node_find_loop2:
		ld	[current_node], %o0		! get next node
		ld	[prom_nodefuncs], %o1
		ld	[%o1], %o1
		call	%o1
		nop
		st	%o0, [current_node]
		set	prop_string_buf, %o2
		set	prop_name, %o1
		b	node_find_loop
		or	%g0, 31, %o3		
						
found_mem_node:
		set	v2_mem_struct-KERNBASE, %o2
		set	0xff, %o3
		set	mem_prop_physavail-KERNBASE, %o1
		ld	[current_node], %o0
		ld	[prom_nodefuncs], %o4
		ld	[%o4 + 0xc], %o4
		call	%o4
		nop

		set	v2_printf_physavail-KERNBASE, %o0
		ld	[v2_mem_struct + 0x8], %o1
		ld	[prom_printf], %o4
		ld	[%o4], %o4		
		call	%o4
		
		set	v2_mem_struct-KERNBASE, %o2
		set	0xff, %o3
		set	mem_prop_phystot-KERNBASE, %o1
		ld	[current_node], %o0
		ld	[prom_nodefuncs], %o4
		ld	[%o4 + 0xc], %o4
		call	%o4
		nop

		set	v2_printf_physavail-KERNBASE, %o0
		ld	[v2_mem_struct + 0x8], %o1
		ld	[prom_printf], %o4
		ld	[%o4], %o4		
		call	%o4
		nop

		b	rest_of_boot
		nop

rest_of_boot:
		call	halt_me
		nop				! who cares at this point

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
		




