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
!
! Video handling moved to video.S by Martin Mares, March 1996
! <mj@k332.feld.cvut.cz>

! NOTE! These had better be the same as in bootsect.s!
#define __ASSEMBLY__
#include <linux/config.h>
#include <asm/segment.h>
#include <linux/version.h>
#include <linux/compile.h>

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
		.word	0x0201		! Version number of header format
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
					!	T=1 for Loadlin
					!	T=2 for bootsect-loader
					!	T=3 for SYSLX
					!	T=4 for ETHERBOOT
					!       V = version
loadflags:	.byte	0	! unused bits =0 (reserved for future development)
LOADED_HIGH	= 1		! bit within loadflags,
				! if set, then the kernel is loaded high
CAN_USE_HEAP	= 0x80		! if set, the loader also has set heap_end_ptr
				! to tell how much space behind setup.S
				| can be used for heap purposes.
				! Only the loader knows what is free!
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
heap_end_ptr:	.word	modelist+1024	! space from here (exclusive) down to
				! end of setup code can be used by setup
				! for local heap purposes.
! ------------------------ end of header ----------------------------------

start_of_setup:
! Bootlin depends on this being done early
	mov	ax,#0x01500
	mov	dl,#0x81
	int	0x13

#ifdef SAFE_RESET_DISK_CONTROLLER
! Reset the disk controller.
	mov	ax,#0x0000
	mov	dl,#0x80
	int	0x13
#endif

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
	call	prtchr
	jmp	prtstr
fin:	ret

! Space printing

prtsp2:	call	prtspc		! Print double space
prtspc:	mov	al,#0x20	! Print single space (fall-thru!)

! Part of above routine, this one just prints ascii al

prtchr:	push	ax
	push	cx
	xor	bh,bh
	mov	cx,#0x01
	mov	ah,#0x0e
	int	0x10
	pop	cx
	pop	ax
	ret

beep:	mov	al,#0x07
	jmp	prtchr
	
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

! Set the keyboard repeat rate to the max

	mov	ax,#0x0305
	xor	bx,bx		! clear bx
	int	0x16

! Check for video adapter and its parameters and allow the
! user to browse video modes.

	call	video	! NOTE: we need DS pointing to bootsector

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

! Check for PS/2 pointing device

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

! Now we want to move to protected mode ...

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

! Well, that certainly wasn't fun :-(. Hopefully it works, and we don't
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
	mov	ax,#1		! protected mode (PE) bit
	lmsw	ax		! This is it!
	jmp	flush_instr
flush_instr:
	xor	bx,bx		! Flag to indicate a boot

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
! Delay is needed after doing I/O
!
delay:
	.word	0x00eb			! jmp $+2
	ret

!
! Descriptor tables
!

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

!
! Include video setup & detection code
!

#include "video.S"

!
! Setup signature -- must be last
!

setup_sig1:	.word	SIG1
setup_sig2:	.word	SIG2

!
! After this point, there is some free space which is used by the video mode
! handling code to store the temporary mode table (not used by the kernel).
!

modelist:

.text
endtext:
.data
enddata:
.bss
endbss:
