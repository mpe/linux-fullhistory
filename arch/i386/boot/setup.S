!
!	setup.S		Copyright (C) 1991, 1992 Linus Torvalds
!
! This code performs all initialization procedures that should be done
! before entering the protected mode. It's responsible for getting of all
! system data offered by BIOS and for detection / selection of video
! mode. All information is put in a "safe" place: 0x90000-0x901FF, i. e.
! where the boot-block used to be.  It is then up to the protected mode
! system to read them from there before the area is overwritten
! for buffer-blocks.
!
! Move PS/2 aux init code to psaux.c
! (troyer@saifr00.cfsat.Honeywell.COM) 03Oct92
!
! Some changes and additional features by Christoph Niemann,
! March 1993/June 1994 (Christoph.Niemann@linux.org)
!
! Completely new video-mode handling code, VESA mode detection, support
! for new Cirrus Logic cards and some additional changes
! by Martin Mares <mj@k332.feld.cvut.cz>, October 1995.
!

! NOTE! These had better be the same as in bootsect.S!
#define __ASSEMBLY__
#include <linux/config.h>
#include <asm/segment.h>

! Uncomment this if you want the BIOS mode numbers to be listed
!#define SHOW_BIOS_MODES

! Signature words to ensure LILO loaded us right
#define SIG1	0xAA55
#define SIG2	0x5A5A

INITSEG  = DEF_INITSEG	! we move boot here - out of the way
SYSSEG   = DEF_SYSSEG	! system loaded at 0x10000 (65536).
SETUPSEG = DEF_SETUPSEG	! this is the current segment

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

! Bootlin depends on this being done early
	mov	ax,#0x01500
	mov	dl,#0x81
	int	0x13

! Check signature at end of setup
	mov	ax,#SETUPSEG
	mov	ds,ax
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
	push	bx
	push	cx
	xor	bh,bh
	mov	cx,#0x01
	mov	ah,#0x0e
	int	0x10
	pop	cx
	pop	bx
	pop	ax
	ret

beep:	mov	al,#0x07
	jmp	prnt1
	
no_sig_mess:	.ascii	"No setup signature found ..."
		db	0x00
start_sys_seg:	.word	SYSSEG

good_sig1:
	jmp	good_sig

! We now have to find the rest of the setup code/data
bad_sig:
	mov	ax,#INITSEG
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
	mov	ax,#SETUPSEG
	mov	es,ax
	mov	ax,#SYSSEG
	mov	ds,ax
	rep
	movsw

	mov	ax,#SETUPSEG
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
	mov	ax,#INITSEG
	mov	ds,ax

! Get memory size (extended mem, kB)

	mov	ah,#0x88
	int	0x15
	mov	[2],ax

! set the keyboard repeat rate to the max

	mov	ax,#0x0305
	xor	bx,bx		! clear bx
	int	0x16

! Check for video adapter and its parameters
! Video mode selection is also handled here

	call	video

! Get hd0 data

	xor	ax,ax		! clear ax
	mov	ds,ax
	lds	si,[4*0x41]
	mov	ax,#INITSEG
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
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	xor	ax,ax		! clear ax
	cld
	rep
	stosb
is_disk1:

! check for PS/2 pointing device

	mov	ax,#INITSEG
	mov	ds,ax
	mov	[0x1ff],#0	! default is no pointing device
	int	0x11		! int 0x11: equipment determination
	test	al,#0x04	! check if pointing device installed
	jz	no_psmouse
	mov	[0x1ff],#0xaa	! device present
no_psmouse:
! now we want to move to protected mode ...

	cli			! no interrupts allowed !
	mov	al,#0x80	! disable NMI for the bootup sequence
	out	#0x70,al

! first we move the system to its rightful place

	mov	ax,#0x100	! start of destination segment
	seg cs
	mov	bx,start_sys_seg	! start of source segment
	cld			! 'direction'=0, movs moves forward
do_move:
	mov	es,ax		! destination segment
	inc	ah		! instead of add ax,#0x100
	cmp	ax,#0x9000
	jz	end_move
	mov	ds,bx		! source segment
	add	bx,#0x100
	sub	di,di
	sub	si,si
	mov 	cx,#0x800
	rep
	movsw
	jmp	do_move

! then we load the segment descriptors

end_move:
	mov	ax,#SETUPSEG	! right, forgot this at first. didn't work :-)
	mov	ds,ax
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
! absolute address 0x10000, in 32-bit protected mode.
!
! Note that the short jump isn't strictly needed, although there are
! reasons why it might be a good idea. It won't hurt in any case.
!
	mov	ax,#1		! protected mode (PE) bit
	lmsw	ax		! This is it!
	jmp	flush_instr
flush_instr:
	xor	bx,bx		! Flag to indicate a boot
	jmpi	0x1000,KERNEL_CS	! jmp offset 1000 of segment 0x10 (cs)

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
! Delay is needed after doing i/o
!
delay:
	.word	0x00eb			! jmp $+2
	ret

!
! Video card / mode detection. We do some hardware testing and build
! a video mode list (placed directly after our code and data). Then we
! choose the right mode given in the configuration or ask the user if
! we are requested to do so. After all, all video parameters are stored
! for later perusal by the kernel.
!

video:	movb	[15],#0		! Default is no VGA
	mov	ax,[0x01fa]
	push	ds
	push	ds
	pop	fs		! In this routine: FS=orig. DS
	push	cs
	pop	ds		! ES=DS=CS
	push	cs
	pop	es
	mov	modesave,ax
	lea	di,modelist	! ES:DI points to current item in our mode list
	mov	eax,#0x50190000	! Store current mode: 80x25
	cld
	stosd

	mov	ah,#0x12	! Check EGA/VGA
	mov	bl,#0x10
	int	0x10
	seg	fs
	mov	[10],bx		! Used for identification of VGA in the kernel
	cmp	bl,#0x10	! Not EGA nor VGA -> 80x25 only
	je	selmd1

	mov	eax,#0x5032FFFF	! EGA or VGA: 80x50 supported
	stosd

	mov	ax,#0x1a00	! Added check for EGA/VGA discrimination
	int	0x10
	cmp	al,#0x1a	! 1a means VGA, anything else EGA
	jne	selmd1
	seg	fs
	movb	[15],#1		! We've detected a VGA

	mov	eax,#0x501cFFFE	! VGA: 80x28 supported
	stosd

	lea	si,vgatable	! Test all known SVGA adapters
dosvga:	lodsw
	mov	bp,ax		! Default mode table
	or	ax,ax
	jz	didsv
	lodsw			! Pointer to test routine
	push	si
	push	di
	push	es
	mov	bx,#0xc000
	mov	es,bx
	call	ax		! Call test routine
	pop	es
	pop	di
	pop	si
	or	bp,bp
	jz	dosvga
	mov	si,bp		! Found, copy the modes
	mov	ah,#0
cpsvga:	lodsb
	or	al,al
	jz	didsv
	stosw
	movsw
	jmp	cpsvga

selmd1:	jmp	selmd

didsv:	mov	ax,#0x4f00	! Fetch VESA information to ES:DI+0x400
	add	di,#0x400
	int	0x10
	sub	di,#0x400
	cmp	al,#0x4f
	jne	selmd
	lgs	bx,(di+0x40e)
	cmp	(di+0x400),#0x4556
	jne	selmd
	cmp	(di+0x402),#0x4153
	jne	selmd

vesa1:	seg	gs
	mov	cx,(bx)
	add	bx,#2
	cmp	cx,#0xffff
	je	selmd
	mov	ax,#0x4f01
	add	di,#0xc00
	int	0x10
	sub	di,#0xc00
	cmp	al,#0x4f
	jne	selmd
	testb	(di+0xc00),#0x10	! Is it a text mode?
	jne	vesa1
	testb	(di+0xc00),#0x08	! Has it colors?
	je	vesa1
	mov	dh,(di+0xc12)	! DX=dimensions, CX=mode
	mov	dl,(di+0xc14)

	lea	si,modelist	! Check if it's already on the list
vesa2:	lodsw
	lodsw
	cmp	ax,dx
	je	vesa1
	cmp	si,di
	jc	vesa2

	mov	ax,cx		! New mode, store it
	stosw
	mov	ax,dx
	stosw
	jmp	vesa1

!
! Video mode table built. Determine the mode we should use and set it.
!
selmd:	mov	ax,modesave
	cmp	ax,#NORMAL_VGA	! Current mode (80x25)
	je	defmd1
	cmp	ax,#EXTENDED_VGA ! 80x50 mode
	je	try50
	cmp	ax,#ASK_VGA
	jne	usemd
banner:	lea	si,keymsg
	call	prtstr
	call	flush
nokey:	call	getkt
	cmp	al,#0x0d	! ENTER ?
	je	listm		! yes - manual mode selection
	cmp	al,#0x20	! SPACE ?
	je	defmd1		! no - repeat
	call 	beep
	jmp	nokey

defmd1:	br	defmd

listm:	call	listmodes	! List all available modes
keymd:	call	getkey		! Get key representing mode ID
	xor	ah,ah
	sub	al,#0x30
	jc	keymd
	cmp	al,#10
	jc	usemd
	sub	al,#39
	cmp	al,#10
	jc	keymd
	cmp	al,#26
	jnc	keymd
	jmp	usemd

try50:	mov	ax,#1		! 80x50 is mode #1
usemd:	shl	ax,#2		! We're requested to set mode in AX
	lea	si,modelist
	add	si,ax
	cmp	si,di
	jc	mdok
	cmp	modesave,#ASK_VGA
	je	keymd
	lea	si,undefd
	call	prtstr
	jmp	banner

mdok:	lodsw			! AX=mode number
	cmp	ah,#0xff
	jz	mdspec
	or	ax,ax
	jz	mdsetd
	or	ah,ah
	jz	mdset
	mov	bx,ax
	mov	ax,#0x4f02
mdset:	int	0x10
mdsetd:	lodsb			! AL=number of lines
	jmp	getpar

mdspec:	inc	ax		! Special modes
	jz	m80x50

m80x28: mov	ax,#0x1111	! Setting 80x28 (VGA with EGA font)
	xor	bl,bl
	int	0x10		! use 9x14 fontset (28 lines on VGA)
	mov	ah,#0x01
	mov	cx,#0x0b0c
	int	0x10		! turn on cursor (scan lines 11 to 12)
	mov	al,#28
	jmp	getpar

m80x50: mov	ax,#0x1112	! Setting 80x50 (EGA/VGA)
	xor	bl,bl
	int	0x10		! use 8x8 font set
	mov	ax,#0x1200
	mov	bl,#0x20
	int	0x10		! use alternate print screen
	mov	ax,#0x1201
	mov	bl,#0x34
	int	0x10		! turn off cursor emulation
	mov	ah,#0x01
	mov	cx,#0x0607
	int	0x10		! turn on cursor (scan lines 6 to 7)
	mov	al,#50
	jmp	getpar

defmd:	mov	al,#25		! Default is 25 lines

!
! Correct video mode set. Determine the remaining parameters.
!

getpar:	pop	ds		! Restore original DS
	mov	[14],al		! Number of lines

	mov	ah,#0x03	! read cursor pos
	xor	bh,bh		! clear bh
	int	0x10		! save it in known place, con_init fetches
	mov	[0],dx		! it from 0x90000.
	
	mov	ah,#0x0f
	int	0x10
	mov	[4],bx		! bh = display page
	mov	[6],ax		! al = video mode, ah = window width
	xor	ax,ax
	mov	es,ax		! Access low memory
	seg es
	mov	ax,[0x485]	! POINTS - Height of character matrix
	mov	[16],ax

	ret			! Well done...

!
! Table of all known SVGA cards. For each card, we store a pointer to
! a table of video modes supported by the card and a pointer to a routine
! used for testing of presence of the card.
!

vgatable:
	.word	s3_md, s3_test
	.word	ati_md, ati_test
	.word	ahead_md, ahead_test
	.word	chips_md, chips_test
	.word	cirrus2_md, cirrus2_test
	.word	cirrus1_md, cirrus1_test
	.word	everex_md, everex_test
	.word	genoa_md, genoa_test
	.word	oak_md, oak_test
	.word	paradise_md, paradise_test
	.word	trident_md, trident_test
	.word	tseng_md, tseng_test
	.word	video7_md, video7_test
	.word	0

!
! Test routines and mode tables:
!

! S3 - The test algorithm was taken from the SuperProbe package
! for XFree86 1.2.1. Report bugs to Christoph.Niemann@linux.org

s3_test:
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
	movb	ah,bh
	movb	al,#0x38
	jmp	s3rest
no_s3:	movb	al,#0x35	! restore CRT register 0x35
	movb	ah,bl
	call	outidx
no_s31:	xor	bp,bp		! Detection failed
s3rest:	movb	ah,bh
	movb	al,#0x38	! restore old value of CRT register 0x38
outidx:	out	dx,al		! Write to indexed VGA register
	push	ax		! AL=index, AH=data, DX=index reg port
	mov	al,ah
	inc	dx
	out	dx,al
	dec	dx
	pop	ax
	ret

tstidx:	out	dx,ax		! OUT DX,AX and inidx
inidx:	out	dx,al		! Read from indexed VGA register
	inc	dx		! AL=index, DX=index reg port -> AL=data
	in	al,dx
	dec	dx
	ret

idS3:	.byte	0x81, 0x82, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95
	.byte	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa8, 0xb0

s3_md:	.byte	0x54, 0x2b, 0x84
	.byte	0x55, 0x19, 0x84
	.byte	0

! ATI cards.

ati_test:
	lea 	si,idati
	mov	di,#0x31
	mov 	cx,#0x09
	repe
	cmpsb
	je	atiok
	xor	bp,bp
atiok:	ret

idati:	.ascii	"761295520"

ati_md:	.byte	0x23, 0x19, 0x84
	.byte	0x33, 0x2c, 0x84
	.byte	0x22, 0x1e, 0x64
	.byte	0x21, 0x19, 0x64
	.byte	0x58, 0x21, 0x50
	.byte	0x5b, 0x1e, 0x50
	.byte	0

! AHEAD

ahead_test:
	mov	ax,#0x200f
	mov	dx,#0x3ce
	out	dx,ax
	inc	dx
	in	al,dx
	cmp	al,#0x20
	je	isahed
	cmp	al,#0x21
	je	isahed
	xor	bp,bp
isahed:	ret

ahead_md:
	.byte	0x22, 0x2c, 0x84
	.byte	0x23, 0x19, 0x84
	.byte	0x24, 0x1c, 0x84
	.byte	0x2f, 0x32, 0xa0
	.byte	0x32, 0x22, 0x50
	.byte	0x34, 0x42, 0x50
	.byte	0

! Chips & Tech.

chips_test:
	mov	dx,#0x3c3
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
	cmp	bl,#0xa5
	je	cantok
	xor	bp,bp
cantok:	ret

chips_md:
	.byte	0x60, 0x19, 0x84
	.byte	0x61, 0x32, 0x84
	.byte	0

! Cirrus Logic 5X0

cirrus1_test:
	mov	dx,#0x3d4
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
	je	iscirr
nocirr:	xor	bp,bp
iscirr: mov	dx,#0x3d4
	mov	al,bl
	xor	ah,ah
	shl	ax,#8
	add	ax,#0x0c
	out	dx,ax
	ret

cirrus1_md:
	.byte	0x1f, 0x19, 0x84
	.byte	0x20, 0x2c, 0x84
	.byte	0x22, 0x1e, 0x84
	.byte	0x31, 0x25, 0x64
	.byte	0

! Cirrus Logic 54XX

cirrus2_test:
	mov	dx,#0x3c4
	mov	al,#6
	call	inidx
	mov	bl,al			! BL=backup
	mov	al,#6
	xor	ah,ah
	call	tstidx
	cmp	al,#0x0f
	jne	c2fail
	mov	ax,#0x1206
	call	tstidx
	cmp	al,#0x12
	jne	c2fail
	mov	al,#0x1e
	call	inidx
	mov	bh,al
	and	bh,#0xc0
	mov	ah,bh
	mov	al,#0x1e
	call	tstidx
	xor	al,bh
	and	al,#0x3f
	jne	c2xx
	mov	al,#0x1e
	mov	ah,bh
	or	ah,#0x3f
	call	tstidx
	xor	al,bh
	xor	al,#0x3f
	and	al,#0x3f
c2xx:	pushf
	mov	al,#0x1e
	mov	ah,bh
	out	dx,ax
	popf
	je	c2done
c2fail:	xor	bp,bp
c2done:	mov	al,#6
	mov	ah,bl
	out	dx,ax
	ret

cirrus2_md:
	.byte	0x14, 0x19, 0x84
	.byte	0x54, 0x2b, 0x84
	.byte	0

! Everex / Trident

everex_test:
	mov	ax,#0x7000
	xor	bx,bx
	int	0x10
	cmp	al,#0x70
	jne	noevrx
	shr	dx,#4
	cmp	dx,#0x678
	je	evtrid
	cmp	dx,#0x236
	jne	evrxok
evtrid:	lea	bp,trident_md
evrxok:	ret

noevrx:	xor	bp,bp
	ret

everex_md:
	.byte	0x03, 0x22, 0x50
	.byte	0x04, 0x3c, 0x50
	.byte	0x07, 0x2b, 0x64
	.byte	0x08, 0x4b, 0x64
	.byte	0x0a, 0x19, 0x84
	.byte	0x0b, 0x2c, 0x84
	.byte	0x16, 0x1e, 0x50
	.byte	0x18, 0x1b, 0x64
	.byte	0x21, 0x40, 0xa0
	.byte	0x40, 0x1e, 0x84
	.byte	0

! Genoa.

genoa_test:
	lea	si,idgenoa		! Check Genoa 'clues'
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
	or	cx,cx
	je	isgen
	xor	bp,bp
isgen:	ret

idgenoa: .byte	0x77, 0x00, 0x99, 0x66

genoa_md:
	.byte	0x58, 0x20, 0x50
	.byte	0x5a, 0x2a, 0x64
	.byte	0x60, 0x19, 0x84
	.byte	0x61, 0x1d, 0x84
	.byte	0x62, 0x20, 0x84
	.byte	0x63, 0x2c, 0x84
	.byte	0x64, 0x3c, 0x84
	.byte	0x6b, 0x4f, 0x64
	.byte	0x72, 0x3c, 0x50
	.byte	0x74, 0x42, 0x50
	.byte	0x78, 0x4b, 0x64
	.byte	0

! OAK

oak_test:
	lea	si,idoakvga
	mov	di,#0x08
	mov	cx,#0x08
	repe
	cmpsb
	je	isoak
	xor	bp,bp
isoak:	ret

idoakvga: .ascii  "OAK VGA "

oak_md: .byte	0x4e, 0x3c, 0x50
	.byte	0x4f, 0x3c, 0x84
	.byte	0x50, 0x19, 0x84
	.byte	0x51, 0x2b, 0x84
	.byte	0

! WD Paradise.

paradise_test:
	lea	si,idparadise
	mov	di,#0x7d
	mov	cx,#0x04
	repe
	cmpsb
	je	ispara
	xor	bp,bp
ispara:	ret

idparadise:	.ascii	"VGA="

paradise_md:
	.byte	0x41, 0x22, 0x50
	.byte	0x47, 0x1c, 0x84
	.byte	0x55, 0x19, 0x84
	.byte	0x54, 0x2c, 0x84
	.byte	0

! Trident.

trident_test:
	mov	dx,#0x3c4
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
	je	istrid
	xor	bp,bp
istrid:	ret

trident_md:
	.byte	0x50, 0x1e, 0x50
	.byte	0x51, 0x2b, 0x50
	.byte	0x52, 0x3c, 0x50
	.byte	0x57, 0x19, 0x84
	.byte	0x58, 0x1e, 0x84
	.byte	0x59, 0x2b, 0x84
	.byte	0x5a, 0x3c, 0x84
	.byte	0

! Tseng.

tseng_test:
	mov	dx,#0x3cd
	in	al,dx			! Could things be this simple ! :-)
	mov	bl,al
	mov	al,#0x55
	out	dx,al
	in	al,dx
	mov	ah,al
	mov	al,bl
	out	dx,al
	cmp	ah,#0x55
 	je	istsen
	xor	bp,bp
istsen:	ret

tseng_md:
	.byte	0x26, 0x3c, 0x50
	.byte	0x2a, 0x28, 0x64
	.byte	0x23, 0x19, 0x84
	.byte	0x24, 0x1c, 0x84
	.byte	0x22, 0x2c, 0x84
	.byte	0

! Video7.

video7_test:
	mov	dx,#0x3cc
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
	je	isvid7
	xor	bp,bp
isvid7:	ret

video7_md:
	.byte	0x40, 0x2b, 0x50
	.byte	0x43, 0x3c, 0x50
	.byte	0x44, 0x3c, 0x64
	.byte	0x41, 0x19, 0x84
	.byte	0x42, 0x2c, 0x84
	.byte	0x45, 0x1c, 0x84
	.byte	0

!
! Displaying of the mode list.
!

listmodes:
	lea	si,listhdr
	call	prtstr
	lea	bx,modelist
	mov	cl,#0x30
listm1:	mov	modenr,cl
	lea	si,modestring
	call	prtstr
	mov	al,(bx+3)
	call	dprnt
	mov	al,#0x78
	call	prnt1
	mov	al,(bx+2)
	call	dprnt
#ifdef SHOW_BIOS_MODES
	mov	al,#0x20
	call	prnt1
	mov	al,#0x28
	call	prnt1
	mov	al,(bx+1)
	call	prthex
	mov	al,(bx)
	call	prthex
	mov	al,#0x29
	call	prnt1
#endif
	lea	si,crlf
	call	prtstr
	add	bx,#4
	inc	cl
	cmp	cl,#0x3a
	jnz	listm2
	mov	cl,#0x61
listm2:	cmp	bx,di
	jc	listm1
	lea	si,prompt
	br	prtstr

! Routine to print a hexadecimal byte (AL) on screen.

#ifdef SHOW_BIOS_MODES
prthex:	push	ax
	shr	al,#4
	call	prth1
	pop	ax
prth1:	and	al,#15
	cmp	al,#10
	jc	prth2
	add	al,#7
prth2:	add	al,#0x30
	br	prnt1
#endif

! Routine to print a decimal value on screen, the value to be
! printed is put in AL (i.e 0-255). 

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
! Descriptor table for our protected mode transition.
!

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
	.word	512+gdt,0x9	! gdt base = 0X9xxxx

!
! Assorted messages.
!

keymsg:		.ascii	"Press <RETURN> to see video modes available, <SPACE> to continue or wait 30 secs"
		db	0x0d, 0x0a, 0x00
listhdr:	.ascii	"Mode:  COLSxROWS:"
		db	0x0d, 0x0a, 0x0a, 0x00
prompt:		db	0x0d, 0x0a
		.ascii	"Choose mode by pressing the corresponding number or letter."
crlf:		db	0x0d, 0x0a, 0x00
undefd:		.ascii	"You passed an undefined mode number to setup. Please choose a new mode."
		db	0x0d, 0x0a, 0x0a, 0x07, 0x00
modestring:	.ascii	"   "
modenr:		db	0x00	! mode number
		.ascii	":    "
		db	0x00

modesave:	.word	0	! Requsted mode ID.

! This must be last
setup_sig1:	.word	SIG1
setup_sig2:	.word	SIG2

! After our code and data, we'll store the mode list.
! Mode record:	.word	modenr
!		.byte	lines
!		.byte	columns
! Mode numbers used: 0=current, >=0x100=VESA, -1=80x50, -2=80x28
modelist:

.text
endtext:
.data
enddata:
.bss
endbss:
