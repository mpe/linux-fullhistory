!
!	setup.S		Copyright (C) 1991, 1992 Linus Torvalds
!
! setup.s is responsible for getting the system data from the BIOS,
! and putting them into the appropriate places in system memory.
! both setup.s and system has been loaded by the bootblock.
!
! This code asks the bios for memory/disk/other parameters, and
! puts them in a "safe" place: 0x90000-0x901FF, ie where the
! boot-block used to be. It is then up to the protected mode
! system to read them from there before the area is overwritten
! for buffer-blocks.
!
! Move PS/2 aux init code to psaux.c
! (troyer@saifr00.cfsat.Honeywell.COM) 03Oct92
!
! some changes and additional features by Christoph Niemann,
! March 1993/June 1994 (Christoph.Niemann@linux.org)
!
! add APM BIOS checking by Stephen Rothwell, May 1994
! (Stephen.Rothwell@pd.necisa.oz.au)
!
! High load stuff, initrd support and position independency
! by Hans Lermen & Werner Almesberger, February 1996
! <lermen@elserv.ffm.fgan.de>, <almesber@lrc.epfl.ch>

! NOTE! These had better be the same as in bootsect.s!
#define __ASSEMBLY__
#include <linux/config.h>
#include <asm/segment.h>
#include <linux/version.h>
#include <linux/compile.h>

#ifndef SVGA_MODE
#define SVGA_MODE ASK_VGA
#endif

! Signature words to ensure LILO loaded us right
#define SIG1	0xAA55
#define SIG2	0x5A5A

INITSEG  = DEF_INITSEG	! 0x9000, we move boot here - out of the way
SYSSEG   = DEF_SYSSEG	! 0x1000, system loaded at 0x10000 (65536).
SETUPSEG = DEF_SETUPSEG	! 0x9020, this is the current segment
			! ... and the former contents of CS
DELTA_INITSEG = SETUPSEG - INITSEG ! 0x0020

.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

entry start
start:
	jmp	start_of_setup
! ------------------------ start of header --------------------------------
!
! SETUP-header, must start at CS:2 (old 0x9020:2)
!
		.ascii	"HdrS"		! Signature for SETUP-header
		.word	0x0200		! Version number of header format
					! (must be >= 0x0105
					! else old loadlin-1.5 will fail)
realmode_swtch:	.word	0,0		! default_switch,SETUPSEG
start_sys_seg:	.word	SYSSEG
		.word	kernel_version	! pointing to kernel version string
  ! note: above part of header is compatible with loadlin-1.5 (header v1.5),
  !        must not change it

type_of_loader:	.byte	0		! = 0, old one (LILO, Loadlin,
					!      Bootlin, SYSLX, bootsect...)
					! else it is set by the loader:
					! 0xTV: T=0 for LILO
					!       T=1 for Loadlin
					!       T=2 for bootsect-loader
					!       V = version
loadflags:	.byte	0	! unused bits =0 (reserved for future development)
LOADED_HIGH	= 1		! bit within loadflags,
				! if set, then the kernel is loaded high
setup_move_size: .word  0x8000	! size to move, when we (setup) are not
				! loaded at 0x90000. We will move ourselves
				! to 0x90000 then just before jumping into
				! the kernel. However, only the loader
				! know how much of data behind us also needs
				! to be loaded.
code32_start:	.long	0x1000		! here loaders can put a different
					! start address for 32-bit code.
					!   0x1000 = default for zImage
					! 0x100000 = default for big kernel
ramdisk_image:	.long	0	! address of loaded ramdisk image
				! Here the loader (or kernel generator) puts
				! the 32-bit address were it loaded the image.
				! This only will be interpreted by the kernel.
ramdisk_size:	.long	0	! its size in bytes
bootsect_kludge:
		.word   bootsect_helper,SETUPSEG
! ------------------------ end of header ----------------------------------

start_of_setup:
! Bootlin depends on this being done early
	mov	ax,#0x01500
	mov	dl,#0x81
	int	0x13

! set DS=CS, we know that SETUPSEG == CS at this point
	mov	ax,cs		! aka #SETUPSEG
	mov	ds,ax

! Check signature at end of setup
	cmp	setup_sig1,#SIG1
	jne	bad_sig
	cmp	setup_sig2,#SIG2
	jne	bad_sig
	jmp	good_sig1

! Routine to print asciiz-string at DS:SI

prtstr:	lodsb
	and	al,al
	jz	fin
	call	prnt1
	jmp	prtstr
fin:	ret

! Part of above routine, this one just prints ascii al

prnt1:	push	ax
	push	cx
	xor	bh,bh
	mov	cx,#0x01
	mov	ah,#0x0e
	int	0x10
	pop	cx
	pop	ax
	ret

beep:	mov	al,#0x07
	jmp	prnt1
	
no_sig_mess:	.ascii	"No setup signature found ..."
		db	0x00

good_sig1:
	jmp	good_sig

! We now have to find the rest of the setup code/data
bad_sig:
	mov	ax,cs		! aka #SETUPSEG
	sub	ax,#DELTA_INITSEG ! aka #INITSEG
	mov	ds,ax
	xor	bh,bh
	mov	bl,[497]	! get setup sects from boot sector
	sub	bx,#4		! LILO loads 4 sectors of setup
	shl	bx,#8		! convert to words
	mov	cx,bx
	shr	bx,#3		! convert to segment
	add	bx,#SYSSEG
	seg cs
	mov	start_sys_seg,bx

! Move rest of setup code/data to here
	mov	di,#2048	! four sectors loaded by LILO
	sub	si,si
	mov	ax,cs		! aka #SETUPSEG
	mov	es,ax
	mov	ax,#SYSSEG
	mov	ds,ax
	rep
	movsw

	mov	ax,cs		! aka #SETUPSEG
	mov	ds,ax
	cmp	setup_sig1,#SIG1
	jne	no_sig
	cmp	setup_sig2,#SIG2
	jne	no_sig
	jmp	good_sig

no_sig:
	lea	si,no_sig_mess
	call	prtstr
no_sig_loop:
	jmp	no_sig_loop

good_sig:
	mov	ax,cs		! aka #SETUPSEG
	sub	ax,#DELTA_INITSEG ! aka #INITSEG
	mov	ds,ax

! check if an old loader tries to load a big-kernel
	seg cs
	test	byte ptr loadflags,#LOADED_HIGH ! have we a big kernel ?
	jz	loader_ok	! NO, no danger even for old loaders
				! YES, we have a big-kernel
	seg cs
	cmp	byte ptr type_of_loader,#0 ! have we one of the new loaders ?
	jnz	loader_ok	! YES, ok
				! NO, we have an old loader, must give up
	push    cs
	pop	ds
	lea	si,loader_panic_mess
	call	prtstr
	jmp	no_sig_loop
loader_panic_mess: 
	.ascii	"Wrong loader, giving up..."
	db	0

loader_ok:
! Get memory size (extended mem, kB)

	mov	ah,#0x88
	int	0x15
	mov	[2],ax

! set the keyboard repeat rate to the max

	mov	ax,#0x0305
	xor	bx,bx		! clear bx
	int	0x16

! check for EGA/VGA and some config parameters

	mov	ah,#0x12
	mov	bl,#0x10
	int	0x10
	mov	[8],ax
	mov	[10],bx
	mov	[12],cx
	mov	ax,#0x5019
	cmp	bl,#0x10
	je	novga
	mov	ax,#0x1a00	! Added check for EGA/VGA discrimination
	int	0x10
	mov	bx,ax
	mov	ax,#0x5019
	movb	[15],#0		! by default, no VGA
	cmp	bl,#0x1a	! 1a means VGA, anything else EGA or lower
	jne	novga
	movb	[15],#1		! we've detected a VGA
	call	chsvga
novga:	mov	[14],al
	mov	ah,#0x03	! read cursor pos
	xor	bh,bh		! clear bh
	int	0x10		! save it in known place, con_init fetches
	mov	[0],dx		! it from 0x90000.
	
! Get video-card data:
	
	mov	ah,#0x0f
	int	0x10
	mov	[4],bx		! bh = display page
	mov	[6],ax		! al = video mode, ah = window width
	xor	ax,ax
	mov	es,ax		! Access low memory
	seg es
	mov	ax,[0x485]	! POINTS - Height of character matrix
	mov	[16],ax

! Get hd0 data

	xor	ax,ax		! clear ax
	mov	ds,ax
	lds	si,[4*0x41]
	mov	ax,cs		! aka #SETUPSEG
	sub	ax,#DELTA_INITSEG ! aka #INITSEG
	push	ax
	mov	es,ax
	mov	di,#0x0080
	mov	cx,#0x10
	push	cx
	cld
	rep
	movsb

! Get hd1 data

	xor	ax,ax		! clear ax
	mov	ds,ax
	lds	si,[4*0x46]
	pop	cx
	pop	es
	mov	di,#0x0090
	rep
	movsb

! Check that there IS a hd1 :-)

	mov	ax,#0x01500
	mov	dl,#0x81
	int	0x13
	jc	no_disk1
	cmp	ah,#3
	je	is_disk1
no_disk1:
	mov	ax,cs		! aka #SETUPSEG
	sub	ax,#DELTA_INITSEG ! aka #INITSEG
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	xor	ax,ax		! clear ax
	cld
	rep
	stosb
is_disk1:

! check for PS/2 pointing device

	mov	ax,cs		! aka #SETUPSEG
	sub	ax,#DELTA_INITSEG ! aka #INITSEG
	mov	ds,ax
	mov	[0x1ff],#0	! default is no pointing device
	int	0x11		! int 0x11: equipment determination
	test	al,#0x04	! check if pointing device installed
	jz	no_psmouse
	mov	[0x1ff],#0xaa	! device present
no_psmouse:

#ifdef CONFIG_APM
! check for APM BIOS
		! NOTE:	DS is pointing to the bootsector
		!
	mov	[64],#0		! version == 0 means no APM BIOS

	mov	ax,#0x05300	! APM BIOS installation check
	xor	bx,bx
	int	0x15
	jc	done_apm_bios	! error -> no APM BIOS

	cmp	bx,#0x0504d	! check for "PM" signature
	jne	done_apm_bios	! no signature -> no APM BIOS

	mov	[64],ax		! record the APM BIOS version
	mov	[76],cx		!	and flags
	and	cx,#0x02	! Is 32 bit supported?
	je	done_apm_bios	!	no ...

	mov	ax,#0x05304	! Disconnect first just in case
	xor	bx,bx
	int	0x15		! ignore return code

	mov	ax,#0x05303	! 32 bit connect
	xor	bx,bx
	int	0x15
	jc	no_32_apm_bios	! error

	mov	[66],ax		! BIOS code segment
	mov	[68],ebx	! BIOS entry point offset
	mov	[72],cx		! BIOS 16 bit code segment
	mov	[74],dx		! BIOS data segment
	mov	[78],si		! BIOS code segment length
	mov	[80],di		! BIOS data segment length
	jmp	done_apm_bios

no_32_apm_bios:
	and	[76], #0xfffd	! remove 32 bit support bit

done_apm_bios:
#endif

! now we want to move to protected mode ...

	seg cs
	cmp	realmode_swtch,#0
	jz	rmodeswtch_normal
	seg cs
	callf	far * realmode_swtch
	jmp	rmodeswtch_end
rmodeswtch_normal:
        push	cs
	call	default_switch
rmodeswtch_end:

! we get the code32 start address and modify the below 'jmpi'
! (loader may have changed it)
	seg cs
	mov	eax,code32_start
	seg cs
	mov	code32,eax

! Now we move the system to its rightful place
! ...but we check, if we have a big-kernel.
! in this case we *must* not move it ...
	seg cs
	test	byte ptr loadflags,#LOADED_HIGH
	jz	do_move0	! we have a normal low loaded zImage
				! we have a high loaded big kernel
	jmp	end_move	! ... and we skip moving

do_move0:
	mov	ax,#0x100	! start of destination segment
	mov	bp,cs		! aka #SETUPSEG
	sub	bp,#DELTA_INITSEG ! aka #INITSEG
	seg cs
	mov	bx,start_sys_seg	! start of source segment
	cld			! 'direction'=0, movs moves forward
do_move:
	mov	es,ax		! destination segment
	inc	ah		! instead of add ax,#0x100
	mov	ds,bx		! source segment
	add	bx,#0x100
	sub	di,di
	sub	si,si
	mov 	cx,#0x800
	rep
	movsw
	cmp	bx,bp		! we assume start_sys_seg > 0x200,
				! so we will perhaps read one page more then
				! needed, but never overwrite INITSEG because
				! destination is minimum one page below source
	jb	do_move

! then we load the segment descriptors

end_move:
	mov	ax,cs ! aka #SETUPSEG	! right, forgot this at first. didn't work :-)
	mov	ds,ax

! If we have our code not at 0x90000, we need to move it there now.
! We also then need to move the params behind it (commandline)
! Because we would overwrite the code on the current IP, we move
! it in two steps, jumping high after the first one.
	mov	ax,cs
	cmp	ax,#SETUPSEG
	je	end_move_self
	cli	! make sure we really have interrupts disabled !
		! because after this the stack should not be used
	sub	ax,#DELTA_INITSEG ! aka #INITSEG
	mov	dx,ss
	cmp	dx,ax
	jb	move_self_1
	add	dx,#INITSEG
	sub	dx,ax		! this will be SS after the move
move_self_1:
	mov	ds,ax
	mov	ax,#INITSEG	! real INITSEG
	mov	es,ax
	seg cs
	mov	cx,setup_move_size
	std		! we have to move up, so we use direction down
			! because the areas may overlap
	mov	di,cx
	dec	di
	mov	si,di
	sub	cx,#move_self_here+0x200
	rep
	movsb
	jmpi	move_self_here,SETUPSEG ! jump to our final place
move_self_here:
	mov	cx,#move_self_here+0x200
	rep
	movsb
	mov	ax,#SETUPSEG
	mov	ds,ax
	mov	ss,dx
			! now we are at the right place
end_move_self:

	lidt	idt_48		! load idt with 0,0
	lgdt	gdt_48		! load gdt with whatever appropriate

! that was painless, now we enable A20

	call	empty_8042
	mov	al,#0xD1		! command write
	out	#0x64,al
	call	empty_8042
	mov	al,#0xDF		! A20 on
	out	#0x60,al
	call	empty_8042

! make sure any possible coprocessor is properly reset..

	xor	ax,ax
	out	#0xf0,al
	call	delay
	out	#0xf1,al
	call	delay

! well, that went ok, I hope. Now we have to reprogram the interrupts :-(
! we put them right after the intel-reserved hardware interrupts, at
! int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
! messed this up with the original PC, and they haven't been able to
! rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
! which is used for the internal hardware interrupts as well. We just
! have to reprogram the 8259's, and it isn't fun.

	mov	al,#0x11		! initialization sequence
	out	#0x20,al		! send it to 8259A-1
	call	delay
	out	#0xA0,al		! and to 8259A-2
	call	delay
	mov	al,#0x20		! start of hardware int's (0x20)
	out	#0x21,al
	call	delay
	mov	al,#0x28		! start of hardware int's 2 (0x28)
	out	#0xA1,al
	call	delay
	mov	al,#0x04		! 8259-1 is master
	out	#0x21,al
	call	delay
	mov	al,#0x02		! 8259-2 is slave
	out	#0xA1,al
	call	delay
	mov	al,#0x01		! 8086 mode for both
	out	#0x21,al
	call	delay
	out	#0xA1,al
	call	delay
	mov	al,#0xFF		! mask off all interrupts for now
	out	#0xA1,al
	call	delay
	mov	al,#0xFB		! mask all irq's but irq2 which
	out	#0x21,al		! is cascaded

! well, that certainly wasn't fun :-(. Hopefully it works, and we don't
! need no steenking BIOS anyway (except for the initial loading :-).
! The BIOS-routine wants lots of unnecessary data, and it's less
! "interesting" anyway. This is how REAL programmers do it.
!
! Well, now's the time to actually move into protected mode. To make
! things as simple as possible, we do no register set-up or anything,
! we let the gnu-compiled 32-bit programs do that. We just jump to
! absolute address 0x1000 (or the loader supplied one),
! in 32-bit protected mode.
!
! Note that the short jump isn't strictly needed, although there are
! reasons why it might be a good idea. It won't hurt in any case.
!
	xor	ax,ax
	inc	ax		! protected mode (PE) bit
	lmsw	ax		! This is it!
	jmp	flush_instr
flush_instr:
	mov	bx,#0		! Flag to indicate a boot

! NOTE: For high loaded big kernels we need a
!	jmpi    0x100000,KERNEL_CS
!
!	but we yet haven't reloaded the CS register, so the default size 
!	of the target offset still is 16 bit.
!       However, using an operant prefix (0x66), the CPU will properly
!	take our 48 bit far pointer. (INTeL 80386 Programmer's Reference
!	Manual, Mixing 16-bit and 32-bit code, page 16-6)
	db	0x66,0xea	! prefix + jmpi-opcode
code32:	dd	0x1000		! will be set to 0x100000 for big kernels
	dw	KERNEL_CS


kernel_version:	.ascii	UTS_RELEASE
		.ascii	" ("
		.ascii	LINUX_COMPILE_BY
		.ascii	"@"
		.ascii	LINUX_COMPILE_HOST
		.ascii	") "
		.ascii	UTS_VERSION
		db	0

! This is the default real mode switch routine.
! to be called just before protected mode transition

default_switch:
	cli			! no interrupts allowed !
	mov	al,#0x80	! disable NMI for the bootup sequence
	out	#0x70,al
	retf

! This routine only gets called, if we get loaded by the simple
! bootsect loader _and_ have a bzImage to load.
! Because there is no place left in the 512 bytes of the boot sector,
! we must emigrate to code space here.
!
bootsect_helper:
	seg cs
	cmp	word ptr bootsect_es,#0
	jnz	bootsect_second
	seg cs
	mov	byte ptr type_of_loader,#0x20
	mov	ax,es
	shr	ax,#4
	seg	cs
	mov	byte ptr bootsect_src_base+2,ah
	mov	ax,es
	seg cs
	mov	bootsect_es,ax
	sub	ax,#SYSSEG
	retf			! nothing else to do for now
bootsect_second:
	push	cx
	push	si
	push	bx
	test	bx,bx	! 64K full ?
	jne	bootsect_ex
	mov	cx,#0x8000	! full 64K move, INT15 moves words
	push	cs
	pop	es
	mov	si,#bootsect_gdt
	mov	ax,#0x8700
	int	0x15
	jc	bootsect_panic	! this, if INT15 fails
	seg cs
	mov	es,bootsect_es	! we reset es to always point to 0x10000
	seg cs
	inc	byte ptr bootsect_dst_base+2
bootsect_ex:
	seg cs
	mov	ah, byte ptr bootsect_dst_base+2
	shl	ah,4	! we now have the number of moved frames in ax
	xor	al,al
	pop	bx
	pop	si
	pop	cx
	retf

bootsect_gdt:
	.word	0,0,0,0
	.word	0,0,0,0
bootsect_src:
	.word	0xffff
bootsect_src_base:
	.byte	0,0,1			! base = 0x010000
	.byte	0x93			! typbyte
	.word	0			! limit16,base24 =0
bootsect_dst:
	.word	0xffff
bootsect_dst_base:
	.byte	0,0,0x10		! base = 0x100000
	.byte	0x93			! typbyte
	.word	0			! limit16,base24 =0
	.word	0,0,0,0			! BIOS CS
	.word	0,0,0,0			! BIOS DS
bootsect_es:
	.word	0

bootsect_panic:
	push	cs
	pop	ds
	cld
	lea	si,bootsect_panic_mess
	call	prtstr
bootsect_panic_loop:
	jmp	bootsect_panic_loop
bootsect_panic_mess:
	.ascii	"INT15 refuses to access high mem, giving up..."
	db	0

! This routine checks that the keyboard command queue is empty
! (after emptying the output buffers)
!
! No timeout is used - if this hangs there is something wrong with
! the machine, and we probably couldn't proceed anyway.
empty_8042:
	call	delay
	in	al,#0x64	! 8042 status port
	test	al,#1		! output buffer?
	jz	no_output
	call	delay
	in	al,#0x60	! read it
	jmp	empty_8042
no_output:
	test	al,#2		! is input buffer full?
	jnz	empty_8042	! yes - loop
	ret
!
! Read a key and return the (US-)ascii code in al, scan code in ah
!
getkey:
	xor	ah,ah
	int	0x16
	ret

!
! Read a key with a timeout of 30 seconds. The cmos clock is used to get
! the time.
!
getkt:
	call	gettime
	add	al,#30		! wait 30 seconds
	cmp	al,#60
	jl	lminute
	sub	al,#60
lminute:
	mov	cl,al
again:	mov	ah,#0x01
	int	0x16
	jnz	getkey		! key pressed, so get it
	call	gettime
	cmp	al,cl
	jne	again
	mov	al,#0x20	! timeout, return default char `space'
	ret

!
! Flush the keyboard buffer
!
flush:	mov	ah,#0x01
	int	0x16
	jz	empty
	xor	ah,ah
	int	0x16
	jmp	flush
empty:	ret

!
! Read the cmos clock. Return the seconds in al
!
gettime:
	push	cx
	mov	ah,#0x02
	int	0x1a
	mov	al,dh			! dh contains the seconds
	and	al,#0x0f
	mov	ah,dh
	mov	cl,#0x04
	shr	ah,cl
	aad
	pop	cx
	ret

!
! Delay is needed after doing i/o
!
delay:
	.word	0x00eb			! jmp $+2
	ret

! Routine trying to recognize type of SVGA-board present (if any)
! and if it recognize one gives the choices of resolution it offers.
! If one is found the resolution chosen is given by al,ah (rows,cols).

chsvga:	cld
	push	ds
	push	cs
	mov	ax,[0x01fa]
	pop	ds
	mov	modesave,ax
	mov 	ax,#0xc000
	mov	es,ax
	mov	ax,modesave
	cmp	ax,#NORMAL_VGA
	je	defvga
	cmp	ax,#EXTENDED_VGA
	je	vga50
	cmp	ax,#ASK_VGA
	jne	svga
	lea	si,msg1
	call	prtstr
	call	flush
nokey:	call	getkt
	cmp	al,#0x0d		! enter ?
	je	svga			! yes - svga selection
	cmp	al,#0x20		! space ?
	je	defvga			! no - repeat
	call 	beep
	jmp	nokey
defvga:	mov	ax,#0x5019
	pop	ds
	ret
/* extended vga mode: 80x50 */
vga50:
	mov	ax,#0x1112
	xor	bl,bl
	int	0x10		! use 8x8 font set (50 lines on VGA)
	mov	ax,#0x1200
	mov	bl,#0x20
	int	0x10		! use alternate print screen
	mov	ax,#0x1201
	mov	bl,#0x34
	int	0x10		! turn off cursor emulation
	mov	ah,#0x01
	mov	cx,#0x0607
	int	0x10		! turn on cursor (scan lines 6 to 7)
	pop	ds
	mov	ax,#0x5032	! return 80x50
	ret
/* extended vga mode: 80x28 */
vga28:
	pop	ax		! clean the stack
	mov	ax,#0x1111
	xor	bl,bl
	int	0x10		! use 9x14 fontset (28 lines on VGA)
	mov	ah, #0x01
	mov	cx,#0x0b0c
	int	0x10		! turn on cursor (scan lines 11 to 12)
	pop	ds
	mov	ax,#0x501c	! return 80x28
	ret
/* svga modes */
!
!	test for presence of an S3 VGA chip. The algorithm was taken
!	from the SuperProbe package of XFree86 1.2.1
!	report bugs to Christoph.Niemann@linux.org
!
svga:   cld
	mov	cx,#0x0f35	! we store some constants in cl/ch
	mov	dx,#0x03d4
	movb	al,#0x38
	call	inidx
	mov	bh,al		! store current value of CRT-register 0x38
	mov	ax,#0x0038
	call	outidx		! disable writing to special regs
	movb	al,cl		! check whether we can write special reg 0x35
	call	inidx
	movb	bl,al		! save the current value of CRT reg 0x35
	andb	al,#0xf0	! clear bits 0-3
	movb	ah,al
	movb	al,cl		! and write it to CRT reg 0x35
	call	outidx
	call	inidx		! now read it back
	andb	al,ch		! clear the upper 4 bits
	jz	s3_2		! the first test failed. But we have a
	movb	ah,bl		! second chance
	mov	al,cl
	call	outidx
	jmp	s3_1		! do the other tests
s3_2:	mov	ax,cx		! load ah with 0xf and al with 0x35
	orb	ah,bl		! set the upper 4 bits of ah with the orig value
	call	outidx		! write ...
	call	inidx		! ... and reread 
	andb	al,cl		! turn off the upper 4 bits
	push	ax
	movb	ah,bl		! restore old value in register 0x35
	movb	al,cl
	call	outidx
	pop	ax
	cmp	al,ch		! setting lower 4 bits was successful => bad
	je	no_s3		! writing is allowed => this is not an S3
s3_1:	mov	ax,#0x4838	! allow writing to special regs by putting
	call	outidx		! magic number into CRT-register 0x38
	movb	al,cl		! check whether we can write special reg 0x35
	call	inidx
	movb	bl,al
	andb	al,#0xf0
	movb	ah,al
	movb	al,cl
	call	outidx
	call	inidx
	andb	al,ch
	jnz	no_s3		! no, we can't write => no S3
	mov	ax,cx
	orb	ah,bl
	call	outidx
	call	inidx
	andb	al,ch
	push	ax
	movb	ah,bl		! restore old value in register 0x35
	movb	al,cl
	call	outidx
	pop	ax
	cmp	al,ch
	jne	no_s31		! writing not possible => no S3
	movb	al,#0x30
	call	inidx		! now get the S3 id ...
	lea	di,idS3
	mov	cx,#0x10
	repne
	scasb
	je	no_s31
	lea 	si,dsc_S3	! table of descriptions of video modes for BIOS
	lea	di,mo_S3	! table of sizes of video modes for my BIOS
	movb	ah,bh
	movb	al,#0x38
	call	outidx		! restore old value of CRT register 0x38
	br	selmod		! go ask for video mode
no_s3:	movb	al,#0x35	! restore CRT register 0x35
	movb	ah,bl
	call	outidx
no_s31:	movb	ah,bh
	movb	al,#0x38
	call	outidx		! restore old value of CRT register 0x38

	lea 	si,idati		! Check ATI 'clues'
	mov	di,#0x31
	mov 	cx,#0x09
	repe
	cmpsb
	jne	noati
	lea	si,dscati
	lea	di,moati
	br	selmod
noati:	mov	ax,#0x200f		! Check Ahead 'clues'
	mov	dx,#0x3ce
	out	dx,ax
	inc	dx
	in	al,dx
	cmp	al,#0x20
	je	isahed
	cmp	al,#0x21
	jne	noahed
isahed:	lea	si,dscahead
	lea	di,moahead
	br	selmod
noahed:	mov	dx,#0x3c3		! Check Chips & Tech. 'clues'
	in	al,dx
	or	al,#0x10
	out	dx,al
	mov	dx,#0x104		
	in	al,dx
	mov	bl,al
	mov	dx,#0x3c3
	in	al,dx
	and	al,#0xef
	out	dx,al
	cmp	bl,[idcandt]
	jne	nocant
	lea	si,dsccandt
	lea	di,mocandt
	br	selmod
nocant:	mov	dx,#0x3d4		! Check Cirrus 'clues'
	mov	al,#0x0c
	out	dx,al
	inc	dx
	in	al,dx
	mov	bl,al
	xor	al,al
	out	dx,al
	dec	dx
	mov	al,#0x1f
	out	dx,al
	inc	dx
	in	al,dx
	mov	bh,al
	xor	ah,ah
	shl	al,#4
	mov	cx,ax
	mov	al,bh
	shr	al,#4
	add	cx,ax
	shl	cx,#8
	add	cx,#6
	mov	ax,cx
	mov	dx,#0x3c4
	out	dx,ax
	inc	dx
	in	al,dx
	and	al,al
	jnz	nocirr
	mov	al,bh
	out	dx,al
	in	al,dx
	cmp	al,#0x01
	jne	nocirr
	call	rst3d4	
	lea	si,dsccirrus
	lea	di,mocirrus
	br	selmod
rst3d4:	mov	dx,#0x3d4
	mov	al,bl
	xor	ah,ah
	shl	ax,#8
	add	ax,#0x0c
	out	dx,ax
	ret	
nocirr:	call	rst3d4			! Check Everex 'clues'
	mov	ax,#0x7000
	xor	bx,bx
	int	0x10
	cmp	al,#0x70
	jne	noevrx
	shr	dx,#4
	cmp	dx,#0x678
	je	istrid
	cmp	dx,#0x236
	je	istrid
	lea	si,dsceverex
	lea	di,moeverex
	br	selmod
istrid:	lea	cx,ev2tri
	jmp	cx
noevrx:	lea	si,idgenoa		! Check Genoa 'clues'
	xor 	ax,ax
	seg es
	mov	al,[0x37]
	mov	di,ax
	mov	cx,#0x04
	dec	si
	dec	di
l1:	inc	si
	inc	di
	mov	al,(si)
	test	al,al
	jz	l2
	seg es
	cmp	al,(di)
l2:	loope 	l1
	cmp	cx,#0x00
	jne	nogen
	lea	si,dscgenoa
	lea	di,mogenoa
	br	selmod
nogen:	cld
	lea	si,idoakvga
	mov	di,#0x08
	mov	cx,#0x08
	repe
	cmpsb
	jne	nooak
	lea	si,dscoakvga
	lea	di,mooakvga
	br	selmod
nooak:	cld
	lea	si,idparadise		! Check Paradise 'clues'
	mov	di,#0x7d
	mov	cx,#0x04
	repe
	cmpsb
	jne	nopara
	lea	si,dscparadise
	lea	di,moparadise
	br	selmod
nopara:	mov	dx,#0x3c4		! Check Trident 'clues'
	mov	al,#0x0e
	out	dx,al
	inc	dx
	in	al,dx
	xchg	ah,al
	xor	al,al
	out	dx,al
	in	al,dx
	xchg	al,ah
	mov	bl,al		! Strange thing ... in the book this wasn't
	and	bl,#0x02	! necessary but it worked on my card which
	jz	setb2		! is a trident. Without it the screen goes
	and	al,#0xfd	! blurred ...
	jmp	clrb2		!
setb2:	or	al,#0x02	!
clrb2:	out	dx,al
	and	ah,#0x0f
	cmp	ah,#0x02
	jne	notrid
ev2tri:	lea	si,dsctrident
	lea	di,motrident
	jmp	selmod
notrid:	mov	dx,#0x3cd		! Check Tseng 'clues'
	in	al,dx			! Could things be this simple ! :-)
	mov	bl,al
	mov	al,#0x55
	out	dx,al
	in	al,dx
	mov	ah,al
	mov	al,bl
	out	dx,al
	cmp	ah,#0x55
 	jne	notsen
	lea	si,dsctseng
	lea	di,motseng
	jmp	selmod
notsen:	mov	dx,#0x3cc		! Check Video7 'clues'
	in	al,dx
	mov	dx,#0x3b4
	and	al,#0x01
	jz	even7
	mov	dx,#0x3d4
even7:	mov	al,#0x0c
	out	dx,al
	inc	dx
	in	al,dx
	mov	bl,al
	mov	al,#0x55
	out	dx,al
	in	al,dx
	dec	dx
	mov	al,#0x1f
	out	dx,al
	inc	dx
	in	al,dx
	mov	bh,al
	dec	dx
	mov	al,#0x0c
	out	dx,al
	inc	dx
	mov	al,bl
	out	dx,al
	mov	al,#0x55
	xor	al,#0xea
	cmp	al,bh
	jne	novid7
	lea	si,dscvideo7
	lea	di,movideo7
	jmp	selmod
novid7:	lea	si,dsunknown
	lea	di,mounknown
selmod:	xor	cx,cx
	mov	cl,(di)
	mov	ax,modesave
	cmp	ax,#ASK_VGA
	je	askmod
	cmp	ax,#NORMAL_VGA
	je	askmod
	cmp	al,cl
	jl	gotmode
	push	si
	lea	si,msg4
	call	prtstr
	pop	si
askmod:	push	si
	lea	si,msg2
	call	prtstr
	pop	si
	push	si
	push	cx
tbl:	pop	bx
	push	bx
	mov	al,bl
	sub	al,cl
	call	modepr
	lodsw
	xchg	al,ah
	call	dprnt
	xchg	ah,al
	push	ax
	mov	al,#0x78
	call	prnt1
	pop	ax
	call	dprnt
	push	si
	lea	si,crlf		! print CR+LF
	call	prtstr
	pop	si
	loop	tbl
	pop	cx
	lea	si,msg3
	call	prtstr
	pop	si
	add	cl,#0x30
	jmp	nonum
nonumb:	call	beep
nonum:	call	getkey
	cmp	al,#0x30	! ascii `0'
	jb	nonumb
	cmp	al,#0x3a	! ascii `9'
	jbe	number
	cmp	al,#0x61	! ascii `a'
	jb	nonumb
	cmp	al,#0x7a	! ascii `z'
	ja	nonumb
	sub	al,#0x27
	cmp	al,cl
	jae	nonumb
	sub	al,#0x30
	jmp	gotmode
number: cmp	al,cl
	jae	nonumb
	sub	al,#0x30
gotmode:	xor	ah,ah
	or	al,al
	beq	vga50
	push	ax
	dec	ax
	beq	vga28
	add	di,ax
	mov	al,(di)
	int 	0x10
	pop	ax
	shl	ax,#1
	add	si,ax
	lodsw
	pop	ds
	ret

! Routine to write al into a VGA-register that is
! accessed via an index register
!
! dx contains the address of the index register
! al contains the index
! ah contains the value to write to the data register (dx + 1)
!
! no registers are changed

outidx:	out	dx,al
	push	ax
	mov	al,ah
	inc	dx
	out	dx,al
	dec	dx
	pop	ax
	ret
inidx:	out	dx,al
	inc	dx
	in	al,dx
	dec	dx
	ret

! Routine to print a decimal value on screen, the value to be
! printed is put in al (i.e 0-255). 

dprnt:	push	ax
	push	cx
	xor	ah,ah		! Clear ah
	mov	cl,#0x0a
	idiv	cl
	cmp	al,#0x09
	jbe	lt100
	call	dprnt
	jmp	skip10
lt100:	add	al,#0x30
	call	prnt1
skip10:	mov	al,ah
	add	al,#0x30
	call	prnt1	
	pop	cx
	pop	ax
	ret

!
! Routine to print the mode number key on screen. Mode numbers
! 0-9 print the ascii values `0' to '9', 10-35 are represented by
! the letters `a' to `z'. This routine prints some spaces around the
! mode no.
!

modepr:	push	ax
	cmp	al,#0x0a
	jb	digit		! Here is no check for number > 35
	add	al,#0x27
digit:	add	al,#0x30
	mov	modenr, al
	push 	si
	lea	si, modestring
	call	prtstr
	pop	si
	pop	ax
	ret

gdt:
	.word	0,0,0,0		! dummy

	.word	0,0,0,0		! unused

	.word	0xFFFF		! 4Gb - (0x100000*0x1000 = 4Gb)
	.word	0x0000		! base address=0
	.word	0x9A00		! code read/exec
	.word	0x00CF		! granularity=4096, 386 (+5th nibble of limit)

	.word	0xFFFF		! 4Gb - (0x100000*0x1000 = 4Gb)
	.word	0x0000		! base address=0
	.word	0x9200		! data read/write
	.word	0x00CF		! granularity=4096, 386 (+5th nibble of limit)

idt_48:
	.word	0			! idt limit=0
	.word	0,0			! idt base=0L

gdt_48:
	.word	0x800		! gdt limit=2048, 256 GDT entries
	.word	512+gdt,0x9	! gdt base = 0X9xxxx

msg1:		.ascii	"Press <RETURN> to see SVGA-modes available, <SPACE> to continue or wait 30 secs."
		db	0x0d, 0x0a, 0x0a, 0x00
msg2:		.ascii	"Mode:  COLSxROWS:"
		db	0x0d, 0x0a, 0x0a, 0x00
msg3:		db	0x0d, 0x0a
		.ascii	"Choose mode by pressing the corresponding number or letter."
crlf:		db	0x0d, 0x0a, 0x00
msg4:		.ascii	"You passed an undefined mode number to setup. Please choose a new mode."
		db	0x0d, 0x0a, 0x0a, 0x07, 0x00
modestring:	.ascii	"   "
modenr:		db	0x00	! mode number
		.ascii	":    "
		db	0x00
		
idati:		.ascii	"761295520"
idcandt:	.byte	0xa5
idgenoa:	.byte	0x77, 0x00, 0x99, 0x66
idparadise:	.ascii	"VGA="
idoakvga:	.ascii  "OAK VGA "
idS3:		.byte	0x81, 0x82, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95
		.byte	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa8, 0xb0

! Manufacturer:	  Numofmodes+2:	Mode:
! Number of modes is the number of chip-specific svga modes plus the extended
! modes available on any vga (currently 2)

moati:		.byte	0x06,	0x23, 0x33, 0x22, 0x21
moahead:	.byte	0x07,	0x22, 0x23, 0x24, 0x2f, 0x34
mocandt:	.byte	0x04,	0x60, 0x61
mocirrus:	.byte	0x06,	0x1f, 0x20, 0x22, 0x31
moeverex:	.byte	0x0c,	0x03, 0x04, 0x07, 0x08, 0x0a, 0x0b, 0x16, 0x18, 0x21, 0x40
mogenoa:	.byte	0x0c,	0x58, 0x5a, 0x60, 0x61, 0x62, 0x63, 0x64, 0x72, 0x74, 0x78
moparadise:	.byte	0x04,	0x55, 0x54
motrident:	.byte	0x09,	0x50, 0x51, 0x52, 0x57, 0x58, 0x59, 0x5a
motseng:	.byte	0x07,	0x26, 0x2a, 0x23, 0x24, 0x22
movideo7:	.byte	0x08,	0x40, 0x43, 0x44, 0x41, 0x42, 0x45
mooakvga:	.byte   0x08,   0x00, 0x07, 0x4e, 0x4f, 0x50, 0x51
mo_S3:		.byte	0x04,	0x54, 0x55
mounknown:	.byte	0x02

!			msb = Cols lsb = Rows:
! The first two modes are standard vga modes available on any vga.
! mode 0 is 80x50 and mode 1 is 80x28

dscati:		.word	0x5032, 0x501c, 0x8419, 0x842c, 0x641e, 0x6419
dscahead:	.word	0x5032, 0x501c, 0x842c, 0x8419, 0x841c, 0xa032, 0x5042
dsccandt:	.word	0x5032, 0x501c, 0x8419, 0x8432
dsccirrus:	.word	0x5032, 0x501c, 0x8419, 0x842c, 0x841e, 0x6425
dsceverex:	.word	0x5032, 0x501c, 0x5022, 0x503c, 0x642b, 0x644b, 0x8419, 0x842c, 0x501e, 0x641b, 0xa040, 0x841e
dscgenoa:	.word	0x5032, 0x501c, 0x5020, 0x642a, 0x8419, 0x841d, 0x8420, 0x842c, 0x843c, 0x503c, 0x5042, 0x644b
dscparadise:	.word	0x5032, 0x501c, 0x8419, 0x842c
dsctrident:	.word 	0x5032, 0x501c, 0x501e, 0x502b, 0x503c, 0x8419, 0x841e, 0x842b, 0x843c
dsctseng:	.word	0x5032, 0x501c, 0x503c, 0x6428, 0x8419, 0x841c, 0x842c
dscvideo7:	.word	0x5032, 0x501c, 0x502b, 0x503c, 0x643c, 0x8419, 0x842c, 0x841c
dscoakvga:	.word   0x5032, 0x501c, 0x2819, 0x5019, 0x503c, 0x843c, 0x8419, 0x842b
dsc_S3:		.word	0x5032, 0x501c, 0x842b, 0x8419
dsunknown:	.word	0x5032, 0x501c
modesave:	.word	SVGA_MODE

! This must be last
setup_sig1:	.word	SIG1
setup_sig2:	.word	SIG2

.text
endtext:
.data
enddata:
.bss
endbss:
