!
!	setup.s		(C) 1991 Linus Torvalds
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

! NOTE! These had better be the same as in bootsect.s!
#include <linux/config.h>

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

! ok, the read went well so we get current cursor position and save it for
! posterity.

	mov	ax,#INITSEG	! this is done in bootsect already, but...
	mov	ds,ax

! Get memory size (extended mem, kB)

	mov	ah,#0x88
	int	0x15
	mov	[2],ax

! set the keyboard repeat rate to the max

	mov	ax,#0x0305
	mov	bx,#0x0000
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
	call	chsvga
novga:	mov	[14],ax
	mov	ah,#0x03	! read cursor pos
	xor	bh,bh
	int	0x10		! save it in known place, con_init fetches
	mov	[0],dx		! it from 0x90000.
	
! Get video-card data:
	
	mov	ah,#0x0f
	int	0x10
	mov	[4],bx		! bh = display page
	mov	[6],ax		! al = video mode, ah = window width

! Get hd0 data

	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x41]
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0080
	mov	cx,#0x10
	cld
	rep
	movsb

! Get hd1 data

	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x46]
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	cld
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
	mov	ax,#0x00
	cld
	rep
	stosb
is_disk1:

! now we want to move to protected mode ...

	cli			! no interrupts allowed !

! first we move the system to it's rightful place

	mov	ax,#0x0000
	cld			! 'direction'=0, movs moves forward
do_move:
	mov	es,ax		! destination segment
	add	ax,#0x1000
	cmp	ax,#0x9000
	jz	end_move
	mov	ds,ax		! source segment
	sub	di,di
	sub	si,si
	mov 	cx,#0x8000
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

! well, that went ok, I hope. Now we have to reprogram the interrupts :-(
! we put them right after the intel-reserved hardware interrupts, at
! int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
! messed this up with the original PC, and they haven't been able to
! rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
! which is used for the internal hardware interrupts as well. We just
! have to reprogram the 8259's, and it isn't fun.

	mov	al,#0x11		! initialization sequence
	out	#0x20,al		! send it to 8259A-1
	.word	0x00eb,0x00eb		! jmp $+2, jmp $+2
	out	#0xA0,al		! and to 8259A-2
	.word	0x00eb,0x00eb
	mov	al,#0x20		! start of hardware int's (0x20)
	out	#0x21,al
	.word	0x00eb,0x00eb
	mov	al,#0x28		! start of hardware int's 2 (0x28)
	out	#0xA1,al
	.word	0x00eb,0x00eb
	mov	al,#0x04		! 8259-1 is master
	out	#0x21,al
	.word	0x00eb,0x00eb
	mov	al,#0x02		! 8259-2 is slave
	out	#0xA1,al
	.word	0x00eb,0x00eb
	mov	al,#0x01		! 8086 mode for both
	out	#0x21,al
	.word	0x00eb,0x00eb
	out	#0xA1,al
	.word	0x00eb,0x00eb
	mov	al,#0xFF		! mask off all interrupts for now
	out	#0xA1,al
	.word	0x00eb,0x00eb
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
! absolute address 0x00000, in 32-bit protected mode.

	mov	ax,#0x0001	! protected mode (PE) bit
	lmsw	ax		! This is it!
	jmpi	0,8		! jmp offset 0 of segment 8 (cs)

! This routine checks that the keyboard command queue is empty
! No timeout is used - if this hangs there is something wrong with
! the machine, and we probably couldn't proceed anyway.
empty_8042:
	.word	0x00eb,0x00eb
	in	al,#0x64	! 8042 status port
	test	al,#2		! is input buffer full?
	jnz	empty_8042	! yes - loop
	ret

getkey:
	in	al,#0x60	! Quick and dirty...
	.word	0x00eb,0x00eb		! jmp $+2, jmp $+2
	mov	bl,al
	in	al,#0x61
	.word	0x00eb,0x00eb
	mov	ah,al
	or	al,#0x80
	out	#0x61,al
	.word	0x00eb,0x00eb
	mov	al,ah
	out	#0x61,al
	.word	0x00eb,0x00eb
	mov	al,bl
	ret

! Routine trying to recognize type of SVGA-board present (if any)
! and if it recognize one gives the choices of resolution it offers.
! If one is found the resolution chosen is given by al,ah (rows,cols).

chsvga:	cld
	push	ds
	push	cs
	pop	ds

! First try and execute a VESA BIOS call

	mov	ax,#0x4f00		! AX = VESA BIOS func RETURN SVGA Info
	push	cs
	pop	es
	lea	di,vib			! ES:[DI] -> VESA Information Block Ptr
	int	0x10

	cmp	ax,#0x004f		! Check result status
	jne	novesa			! VESA BIOS not supported or failed

! OK!  We got a VESA BIOS, let's figure out what we can do!

! Print out the VESA information from the VIB

	lea	si,vib			! This should print out VESA
	lodsb
	call	prnt1
	lodsb
	call	prnt1
	lodsb
	call	prnt1
	lodsb
	call	prnt1
	call	space

	mov	al,vib+5		! This is the version of VESA supported
	call	dprnt
	mov	al,#0x2e
	call	prnt1
	mov	al,vib+4
	call	dprnt
	call	space

	push	ds
	lds	si,vib+6		! This prints out the OEM string
	call	prtstr
	call	space
	pop	ds

	mov	al,vib+10		! This prints out the Vesa Capabilities
	call	dprnt
	mov	al,vib+11
	call	dprnt
	mov	al,vib+12
	call	dprnt
	mov	al,vib+13
	call	dprnt

	push	ds			! Finally, go through the list of modes
	lds	si,vib+14
model:	lodsw				! Get mode number
	cmp	ax,#0xFFFF
	je	isvesa
	call	addmod			! Check to see if this is a TEXT mode
	jmp	model

isvesa:	call	docr
	pop	ds
	lea	si,dscvesa
	lea	di,movesa
	lea	cx,selmod
	jmp	cx

novesa:	mov 	ax,#0xc000
	mov	es,ax
	lea	si,msg1
	call	prtstr			! Press <RETURN> to see SVGA-modes ...
flush:	in	al,#0x60		! Flush the keyboard buffer
	cmp	al,#0x82
	jb	nokey
	jmp	flush
nokey:	call getkey
	cmp	al,#0x82
	jb	nokey
	cmp	al,#0xe0
	ja	nokey
	cmp	al,#0x9c
	je	svga
	mov	ax,#0x5019
	pop	ds
	ret
svga:	cld
	lea 	si,idati		! Check ATI 'clues'
	mov	di,#0x31
	mov 	cx,#0x09
	repe
	cmpsb
	jne	noati
	lea	si,dscati
	lea	di,moati
	lea	cx,selmod
	jmp	cx
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
	lea	cx,selmod
	jmp	cx
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
	lea	cx,selmod
	jmp	cx
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
	lea	cx,selmod
	jmp	cx
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
	lea	cx,selmod
	jmp	cx
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
	seg es
	and	al,(di)
	cmp	al,(si)
	loope 	l1
	cmp	cx,#0x00
	jne	nogen
	lea	si,dscgenoa
	lea	di,mogenoa
	lea	cx,selmod
	jmp	cx
nogen:	cld
	lea	si,idparadise		! Check Paradise 'clues'
	mov	di,#0x7d
	mov	cx,#0x04
	repe
	cmpsb
	jne	nopara
	lea	si,dscparadise
	lea	di,moparadise
	lea	cx,selmod
	jmp	cx
nopara:	mov	dx,#0x3c4		! Check Trident 'clues'
	mov	al,#0x0e
	out	dx,al
	inc	dx
	in	al,dx
	xchg	ah,al
	mov	al,#0x00
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
	lea	cx,selmod
	jmp	cx
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
	lea	cx,selmod
	jmp	cx
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
	je	isvideo7
	lea	cx,set8x8
	jmp	cx
isvideo7:
	lea	si,dscvideo7
	lea	di,movideo7

! Upon Entry to SELMOD, SI -> list of Modes, DI -> List of Mode Numbers

selmod:	push	si
	lea	si,msg2			! Numb:   Mode:   COLSxROWS
	call	prtstr
	mov	cx,(di)			! This gets Number of Modes in list
	pop	si
	push	si
	push	cx
tbl:	pop	bx
	push	bx
	mov	ax,bx
	sub	ax,cx
	call	hprntl			! Print out selection number
	push	ax
	call	spcing
	pop	ax
	push	di
	add	ax,ax
	add	ax,#2
	add	di,ax
	mov	ax,(di)
	call	hprntl			! Print out MODE number
	call	spcing
	pop	di
	lodsw
	xchg	al,ah
	call	dprnt
	xchg	ah,al
	push	ax
	mov	al,#0x78
	call	prnt1
	pop	ax
	call	dprnt
	call	docr
	loop	tbl
	pop	cx
	call	docr
	lea	si,msg3			! Choose Mode Number
	call	prtstr
	pop	si
	add	cl,#0x80
nonum:	call	getkey
	cmp	al,#0x82
	jb	nonum
	cmp	al,#0x8b
	je	zero
	cmp	al,cl
	ja	nonum
	jmp	nozero
zero:	sub	al,#0x0a
nozero:	sub	al,#0x80
	dec	al
	xor	ah,ah
	shl	ax,#1
	push	ax
	add	di,ax
	inc	di
	inc	di
	mov	ax,(di)			! AX = Mode
	cmp	ah,#0
	jne	setvesa
	int 	0x10			! Set OLD style mode

retmode:
	pop	ax
	add	si,ax
	lodsw				! Get COLSxROWS
	pop	ds
	ret

setvesa:
	pop	bx
	cmp	ah,#0xFF		! Special, mode FF, set 8x8 font
	je	set8x8

	push	bx
	mov	bx,ax			! Mode to set
	mov	ax,#0x4f02		! Set VESA mode
	int	0x10

	jmp	retmode
		
! If we can't find the adapter in the table, at least set 80x50

set8x8:
	mov	ax,#0x1112
	mov	bl,#0
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

! Routine to add mode in ax to VESA selection table

addmod:	push	cx
	push	ds
	push	es
	push	di
	push	bx
	push	dx
	push	ax

	mov	cx,ax			! CX = VESA mode number
	push	cs
	pop	es
	lea	di,mib			! ES:[DI] -> Mode Information Block
	mov	ax,#0x4f01		! AX = Get VESA Mode Info
	int	0x10	

	cmp	ax,#0x004f		! If fails, assume it's not a TEXT mode
	jne	adfail

	push	cs
	pop	ds			! Make DS contain something reasonable

	mov	ax,mib			! Get Mode Attributes field
	and	al,#0x12		! Mask Text and Extended bits
	cmp	al,#0x02		! Text and Extended info available?
	jne	adfail

	call	space

	mov	ax,mib+18		! Horizontal Resolution
	mov	bl,mib+22		! X Char Size
	div	bl
! HACK:  For some reason, my Diamond Stealth card returns 160 cols for its
!	 132 coloumn modes, so don't return any sizes > 132?
	sub	al,#132
	jbe	orgcol
	sub	al,al
orgcol:	add	al,#132			! MIN(cols, 132)
	mov	dh,al			! Put num cols in DH
	mov	ax,mib+20		! Vertical Resolution
	mov	bl,mib+23		! Y Char Size
	div	bl
	mov	dl,al			! Put num rows in DL

	mov	bx,movesa		! Get current number of video modes
	lea	di,movesa
	inc	(di)			! This is a NEW mode
	add	bx,bx
	add	di,bx
	add	di,#2
	pop	ax			! Get Mode number back
	push	ax
	mov	(di),ax			! Mode number
	lea	di,dscvesa
	add	di,bx
	mov	(di),dx			! Screen resolution

adfail:	pop	ax
	pop	dx
	pop	bx
	pop	di
	pop	es
	pop	ds
	pop	cx

	ret

! Routine that 'tabs' to next col.

spcing:	mov	al,#0x2e
	call	prnt1	
space3:	mov	al,#0x20
	call	prnt1	
space2:	mov	al,#0x20
	call	prnt1	
space:	mov	al,#0x20
	call	prnt1
	ret	

! Routine to print asciiz-string at DS:SI

prtstr:	lodsb
	and	al,al
	jz	fin
	call	prnt1
	jmp	prtstr
fin:	ret

! Routine to print out HEX values on screen.
! The value to be printed is in the AX register.

hprntl:	xchg	ah,al
	call	hprnt
	xchg	ah,al
	call	hprnt
	ret

! Routine to print out HEX values on the screen
! The valueto be printed is in the AL register.  AH is preserved.

hprnt:	push	ax
	shr	al,4
	and	al,#0xf
	call	hprnt1
	pop	ax
	push	ax
	and	al,#0xf
	call	hprnt1
	pop	ax
	ret

! Routine to print out one HEX digit on the screen.
! The value to be printed is in al (0-F)

hprnt1:	cmp	al,#10
	jl	hdec
	add	al,#7			! Convert 10-15 to A-F
hdec:	add	al,#0x30		! Convert to ASCII
	call	prnt1			! print it
	ret

! Routine to print a decimal value on screen, the value to be
! printed is put in al (i.e 0-255). 

dprnt:	push	ax
	push	cx
	mov	ah,#0x00		
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

! Part of above routine, this one just prints ascii al

prnt1:	push	ax
	push	cx
	mov	bh,#0x00
	mov	cx,#0x01
	mov	ah,#0x0e
	int	0x10
	pop	cx
	pop	ax
	ret

! Prints <CR> + <LF>

docr:	push	ax
	push	cx
	mov	bh,#0x00
	mov	ah,#0x0e
	mov	al,#0x0a
	mov	cx,#0x01
	int	0x10
	mov	al,#0x0d
	int	0x10
	pop	cx
	pop	ax
	ret	
	
gdt:
	.word	0,0,0,0		! dummy

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

msg1:		.ascii	"Press <RETURN> to see SVGA-modes available or any other key to continue."
		db	0x0d, 0x0a, 0x0a, 0x00
msg2:		.ascii	"Numb:   Mode:  COLSxROWS:"
		db	0x0d, 0x0a, 0x0a, 0x00
msg3:		.ascii	"Choose mode by pressing the corresponding number."
		db	0x0d, 0x0a, 0x00
		
idati:		.ascii	"761295520"
idcandt:	.byte	0xa5
idgenoa:	.byte	0x77, 0x00, 0x66, 0x99
idparadise:	.ascii	"VGA="

! Manufacturer:	  Numofmodes:	Mode:

moati:		.word	0x02,	0x23, 0x33 
moahead:	.word	0x05,	0x22, 0x23, 0x24, 0x2f, 0x34
mocandt:	.word	0x02,	0x60, 0x61
mocirrus:	.word	0x04,	0x1f, 0x20, 0x22, 0x31
moeverex:	.word	0x0a,	0x03, 0x04, 0x07, 0x08, 0x0a, 0x0b, 0x16, 0x18, 0x21, 0x40
mogenoa:	.word	0x0a,	0x58, 0x5a, 0x60, 0x61, 0x62, 0x63, 0x64, 0x72, 0x74, 0x78
moparadise:	.word	0x02,	0x55, 0x54
motrident:	.word	0x07,	0x50, 0x51, 0x52, 0x57, 0x58, 0x59, 0x5a
motseng:	.word	0x05,	0x26, 0x2a, 0x23, 0x24, 0x22
movideo7:	.word	0x06,	0x40, 0x43, 0x44, 0x41, 0x42, 0x45
movesa:		.word	0x02,	0x03, 0xFFFF, 254*0

!			msb = Cols lsb = Rows:

dscati:		.word	0x8419, 0x842c
dscahead:	.word	0x842c, 0x8419, 0x841c, 0xa032, 0x5042
dsccandt:	.word	0x8419, 0x8432
dsccirrus:	.word	0x8419, 0x842c, 0x841e, 0x6425
dsceverex:	.word	0x5022, 0x503c, 0x642b, 0x644b, 0x8419, 0x842c, 0x501e, 0x641b, 0xa040, 0x841e
dscgenoa:	.word	0x5020, 0x642a, 0x8419, 0x841d, 0x8420, 0x842c, 0x843c, 0x503c, 0x5042, 0x644b
dscparadise:	.word	0x8419, 0x842b
dsctrident:	.word 	0x501e, 0x502b, 0x503c, 0x8419, 0x841e, 0x842b, 0x843c
dsctseng:	.word	0x503c, 0x6428, 0x8419, 0x841c, 0x842c
dscvideo7:	.word	0x502b, 0x503c, 0x643c, 0x8419, 0x842c, 0x841c
dscvesa:	.word	0x5019, 0x5032, 254*0
	
vib:		.word	256*0
mib:		.word	256*0

.text
endtext:
.data
enddata:
.bss
endbss:
