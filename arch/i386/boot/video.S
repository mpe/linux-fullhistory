!
!	Display adapter & video mode setup, version 2.12 (25-May-98)
!
!	Copyright (C) 1995 -- 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
!	Based on the original setup.S code (C) Linus Torvalds and Mats Anderson
!

#include <linux/config.h> /* for CONFIG_VIDEO_* */

! Enable autodetection of SVGA adapters and modes. If you really need this
! feature, drop me a mail as I think of removing it some day...
#undef CONFIG_VIDEO_SVGA

! Enable autodetection of VESA modes
#define CONFIG_VIDEO_VESA

! Enable compacting of mode table
#define CONFIG_VIDEO_COMPACT

! Retain screen contents when switching modes
#define CONFIG_VIDEO_RETAIN

! Enable local mode list
#undef CONFIG_VIDEO_LOCAL

! Force 400 scan lines for standard modes (hack to fix bad behaviour
! of certain broken BIOSes -- don't use unless needed)
#undef CONFIG_VIDEO_400_HACK

! A special hack allowing to force specific BIOS mode ID along with specific
! dimensions. Especially useful for certain X-Window graphics mode hacks
! (e.g., 800x600 modes on IBM ThinkPad).
#undef CONFIG_VIDEO_GFX_HACK
#define VIDEO_GFX_BIOS_AX 0x4f02	/* 800x600 on ThinkPad */
#define VIDEO_GFX_BIOS_BX 0x0102
#define VIDEO_GFX_DUMMY_RESOLUTION 0x6425	/* 100x37 */

! This code uses an extended set of video mode numbers. These include:
! Aliases for standard modes
!	NORMAL_VGA (-1)
!	EXTENDED_VGA (-2)
!	ASK_VGA (-3)
! Video modes numbered by menu position -- NOT RECOMMENDED because of lack
! of compatibility when extending the table. These are between 0x00 and 0xff.
#define VIDEO_FIRST_MENU 0x0000
! Standard BIOS video modes (BIOS number + 0x0100)
#define VIDEO_FIRST_BIOS 0x0100
! VESA BIOS video modes (VESA number + 0x0200)
#define VIDEO_FIRST_VESA 0x0200
! Video7 special modes (BIOS number + 0x0900)
#define VIDEO_FIRST_V7 0x0900
! Special video modes
#define VIDEO_FIRST_SPECIAL 0x0f00
#define VIDEO_80x25 0x0f00
#define VIDEO_8POINT 0x0f01
#define VIDEO_80x43 0x0f02
#define VIDEO_80x28 0x0f03
#define VIDEO_CURRENT_MODE 0x0f04
#define VIDEO_80x30 0x0f05
#define VIDEO_80x34 0x0f06
#define VIDEO_80x60 0x0f07
#define VIDEO_GFX_HACK 0x0f08
#define VIDEO_LAST_SPECIAL 0x0f09
! Video modes given by resolution
#define VIDEO_FIRST_RESOLUTION 0x1000

! The "recalculate timings" flag
#define VIDEO_RECALC 0x8000

! Positions of various video parameters passed to the kernel
! (see also include/linux/tty.h)
#define PARAM_CURSOR_POS	0x00
#define PARAM_VIDEO_PAGE	0x04
#define PARAM_VIDEO_MODE	0x06
#define PARAM_VIDEO_COLS	0x07
#define PARAM_VIDEO_EGA_BX	0x0a
#define PARAM_VIDEO_LINES	0x0e
#define PARAM_HAVE_VGA		0x0f
#define PARAM_FONT_POINTS	0x10

#define PARAM_LFB_WIDTH		0x12
#define PARAM_LFB_HEIGHT	0x14
#define PARAM_LFB_DEPTH		0x16
#define PARAM_LFB_BASE		0x18
#define PARAM_LFB_SIZE		0x1c
#define PARAM_LFB_LINELENGTH	0x24
#define PARAM_LFB_COLORS	0x26
#define PARAM_VESAPM_SEG	0x2e
#define PARAM_VESAPM_OFF	0x30
#define PARAM_LFB_PAGES		0x32

! Define DO_STORE according to CONFIG_VIDEO_RETAIN
#ifdef CONFIG_VIDEO_RETAIN
#define DO_STORE call store_screen
#else
#define DO_STORE
#endif /* CONFIG_VIDEO_RETAIN */

!
! This is the main entry point called by setup.S
!
! Input:
!	DS pointing to the boot sector

video:	push	ds		! We use different segments
	push	ds		! FS contains original DS
	pop	fs
	push	cs		! DS is equal to CS
	pop	ds
	push	cs		! ES is equal to CS
	pop	es
	xor	ax,ax
	mov	gs,ax		! GS is zero
	cld
	call	basic_detect	! Basic adapter type testing (EGA/VGA/MDA/CGA)
#ifdef CONFIG_VIDEO_SELECT
	seg	fs		! User-selected video mode
	mov	ax,[0x01fa]
	cmp	ax,#ASK_VGA	! Bring up the menu
	jz	vid2
	call	mode_set	! Set the mode
	jc	vid1
	lea	si,badmdt	! Invalid mode ID
	call	prtstr
vid2:	call	mode_menu
vid1:
#ifdef CONFIG_VIDEO_RETAIN
	call	restore_screen	! Restore screen contents
#endif /* CONFIG_VIDEO_RETAIN */
#endif /* CONFIG_VIDEO_SELECT */
	call	mode_params	! Store mode parameters
	pop	ds		! Restore original DS
	ret

!
! Detect if we have CGA, MDA, EGA or VGA and pass it to the kernel.
!

basic_detect:
	seg	fs		! Default is no VGA
	movb	[PARAM_HAVE_VGA],#0

	mov	ah,#0x12	! Check EGA/VGA
	mov	bl,#0x10
	int	0x10
	seg	fs
	mov	[PARAM_VIDEO_EGA_BX],bx	! Used for identification of EGA in the kernel
	cmp	bl,#0x10	! No, this is a CGA/MDA/HGA card.
	je	basret
	incb	[adapter]

	mov	ax,#0x1a00	! Check for EGA/VGA discrimination
	int	0x10
	cmp	al,#0x1a	! 1a means VGA, anything else EGA
	jne	basret
	seg	fs
	incb	[PARAM_HAVE_VGA]	! We've detected a VGA
	incb	[adapter]

basret:	ret

!
! Store the video mode parameters for later usage by the kernel.
! This is done by asking the BIOS except for the rows/columns
! parameters in the default 80x25 mode -- these are set directly,
! because some very obscure BIOSes supply insane values.
!

mode_params:
#ifdef CONFIG_VIDEO_SELECT
	cmpb	[graphic_mode],#0
	jnz	mopar_gr
#endif

	mov	ah,#0x03	! Read cursor position
	xor	bh,bh
	int	0x10
	seg	fs
	mov	[PARAM_CURSOR_POS],dx

	mov	ah,#0x0f	! Read page/mode/width
	int	0x10
	seg	fs
	mov	[PARAM_VIDEO_PAGE],bx
	seg	fs
	mov	[PARAM_VIDEO_MODE],ax	! Video mode and screen width
	cmp	al,#7			! MDA/HGA => segment differs
	jnz	mopar0
	mov	[video_segment],#0xb000
mopar0:	seg	gs			! Font size
	mov	ax,[0x485]
	seg	fs
	mov	[PARAM_FONT_POINTS],ax	! (valid only on EGA/VGA)

	mov	ax,[force_size]	! Forced size?
	or	ax,ax
	jz	mopar1
	seg	fs
	mov	[PARAM_VIDEO_COLS],ah
	seg	fs
	mov	[PARAM_VIDEO_LINES],al
	ret

mopar1:	mov	al,#25
	cmpb	[adapter],#0	! If we are on CGA/MDA/HGA, the screen must
	jz	mopar2		! have 25 lines.
	seg	gs		! On EGA/VGA, use the EGA+ BIOS variable
	mov	al,[0x484]	! containing maximal line number.
	inc	al
mopar2:	seg	fs
	movb	[PARAM_VIDEO_LINES],al
	ret

#ifdef CONFIG_VIDEO_SELECT

!
! Fetching of VESA frame buffer parameters
!

mopar_gr:
	lea	di,modelist+1024
	seg	fs
	movb	[PARAM_HAVE_VGA],#0x23

	mov	ax,(di+16)
	seg	fs
	mov	[PARAM_LFB_LINELENGTH],ax

	mov	ax,(di+18)
	seg	fs
	mov	[PARAM_LFB_WIDTH],ax

	mov	ax,(di+20)
	seg	fs
	mov	[PARAM_LFB_HEIGHT],ax

	mov	al,(di+25)
	mov	ah,#0
	seg	fs
	mov	[PARAM_LFB_DEPTH],ax

	mov	al,(di+29)
	mov	ah,#0
	seg	fs
	mov	[PARAM_LFB_PAGES],ax

	mov	eax,(di+40)
	seg	fs
	mov	[PARAM_LFB_BASE],eax

	mov	eax,(di+31)
	seg	fs
	mov	[PARAM_LFB_COLORS],eax

	mov	eax,(di+35)
	seg	fs
	mov	[PARAM_LFB_COLORS+4],eax

	! get video mem size
	lea	di,modelist+1024
	mov	ax,#0x4f00
	int	0x10

	xor	eax,eax	
	mov	ax,(di+18)
	seg	fs
	mov	[PARAM_LFB_SIZE],eax

	! get protected mode interface informations	
	mov	ax,#0x4f0a
	xor	bx,bx
	xor	di,di
	int	0x10
	cmp	ax,#0x004f
	jnz	no_pm
	seg	fs
	mov	[PARAM_VESAPM_SEG],es
	seg	fs
	mov	[PARAM_VESAPM_OFF],di

no_pm:
	ret

!
! The video mode menu
!

mode_menu:
	lea	si,keymsg	! "Return/Space/Timeout" message
	call	prtstr
	call	flush
nokey:	call	getkt
	cmp	al,#0x0d	! ENTER ?
	je	listm		! yes - manual mode selection
	cmp	al,#0x20	! SPACE ?
	je	defmd1		! no - repeat
	call 	beep
	jmp	nokey
defmd1:	ret			! No mode selected => use the 80x25 default

listm:	call	mode_table	! We need a mode table to be listed
listm0:	lea	si,name_bann	! Print adapter name
	call	prtstr
	mov	si,[card_name]
	or	si,si
	jnz	an2
	mov	al,[adapter]
	lea	si,old_name
	or	al,al
	jz	an1
	lea	si,ega_name
	dec	al
	jz	an1
	lea	si,vga_name
	jmp	an1
an2:	call	prtstr
	lea	si,svga_name
an1:	call	prtstr
	lea	si,listhdr	! Table header
	call	prtstr
	mov	dl,#0x30	! DL holds mode number
	lea	si,modelist
lm1:	cmp	(si),#ASK_VGA	! End?
	jz	lm2
	mov	al,dl		! Menu selection number
	call	prtchr
	call	prtsp2
	lodsw
	call	prthw		! Mode ID
	call	prtsp2
	mov	al,(si+1)
	call	prtdec		! Rows
	mov	al,#0x78	! 'x'
	call	prtchr
	lodsw
	call	prtdec		! Columns
	mov	al,#0x0d	! New line
	call	prtchr
	mov	al,#0x0a
	call	prtchr
	inc	dl		! Next character
	cmp	dl,#0x3a
	jnz	lm1
	mov	dl,#0x61
	jmp	lm1

lm2:	lea	si,prompt	! Mode prompt
	call	prtstr
	lea	di,edit_buf	! Editor buffer
lm3:	call	getkey
	cmp	al,#0x0d	! Enter?
	jz	lment
	cmp	al,#0x08	! Backspace?
	jz	lmbs
	cmp	al,#0x20	! Printable?
	jc	lm3
	cmp	di,#edit_buf+4	! Enough space?
	jz	lm3
	stosb
	call	prtchr
	jmp	lm3

lmbs:	cmp	di,#edit_buf	! Backspace
	jz	lm3
	dec	di
	mov	al,#0x08
	call	prtchr
	call	prtspc
	mov	al,#0x08
	call	prtchr
	jmp	lm3

lment:	movb	(di),#0
	lea	si,crlft
	call	prtstr
	lea	si,edit_buf
	cmpb	(si),#0		! Empty string => use default mode
	jz	lmdef
	cmpb	(si+1),#0	! One character => menu selection
	jz	mnusel
	cmp	(si),#0x6373	! "scan" => mode scanning
	jnz	lmhx
	cmp	(si+2),#0x6e61
	jz	lmscan
lmhx:	xor	bx,bx		! Else => mode ID in hex
lmhex:	lodsb
	or	al,al
	jz	lmuse1
	sub	al,#0x30
	jc	lmbad
	cmp	al,#10
	jc	lmhx1
	sub	al,#7
	and	al,#0xdf
	cmp	al,#10
	jc	lmbad
	cmp	al,#16
	jnc	lmbad
lmhx1:	shl	bx,#4
	or	bl,al
	jmp	lmhex
lmuse1:	mov	ax,bx
	jmp	lmuse

mnusel:	lodsb			! Menu selection
	xor	ah,ah
	sub	al,#0x30
	jc	lmbad
	cmp	al,#10
	jc	lmuse
	cmp	al,#0x61-0x30
	jc	lmbad
	sub	al,#0x61-0x30-10
	cmp	al,#36
	jnc	lmbad
lmuse:	call	mode_set
	jc	lmdef
lmbad:	lea	si,unknt
	call	prtstr
	br	lm2

lmscan:	cmpb	[adapter],#0	! Scanning supported only on EGA/VGA
	jz	lmbad
	mov	[mt_end],#0	! Scanning of modes: done as new autodetection
	movb	[scanning],#1
	call	mode_table
	br	listm0

lmdef:	ret

!
! Additional parts of mode_set... (relative jumps, you know)
!

setv7:				! Video7 extended modes
	DO_STORE
	sub	bh,#VIDEO_FIRST_V7>>8
	mov	ax,#0x6f05
	int	0x10
	stc
	ret

_setrec:	br	setrec		! Ugly...
_set_80x25:	br	set_80x25

!
! Aliases for backward compatibility.
!

setalias:
	mov	ax,#VIDEO_80x25
	inc	bx
	jz	mode_set
	mov	al,#VIDEO_8POINT-VIDEO_FIRST_SPECIAL
	inc	bx
	jnz	setbad

	! Fall-through!

!
! Setting of user mode (AX=mode ID) => CF=success
!

mode_set:
	mov	bx,ax
	cmp	ah,#0xff
	jz	setalias
	test	ah,#VIDEO_RECALC>>8
	jnz	_setrec
	cmp	ah,#VIDEO_FIRST_RESOLUTION>>8
	jnc	setres
	cmp	ah,#VIDEO_FIRST_SPECIAL>>8
	jz	setspc
	cmp	ah,#VIDEO_FIRST_V7>>8
	jz	setv7
	cmp	ah,#VIDEO_FIRST_VESA>>8
	jnc	check_vesa
	or	ah,ah
	jz	setmenu
	dec	ah
	jz	setbios
setbad:	clc
	movb	[do_restore],#0	! The screen needn't be restored
	ret

setvesa:
	DO_STORE
	sub	bh,#VIDEO_FIRST_VESA>>8
	mov	ax,#0x4f02	! VESA BIOS mode set call
	int	0x10
	cmp	ax,#0x004f	! AL=4f if implemented, AH=0 if OK
	jnz	setbad
	stc
	ret

setbios:
	DO_STORE
	int	0x10		! Standard BIOS mode set call
	push	bx
	mov	ah,#0x0f	! Check if really set
	int	0x10
	pop	bx
	cmp	al,bl
	jnz	setbad
	stc
	ret

setspc:	xor	bh,bh		! Set special mode
	cmp	bl,#VIDEO_LAST_SPECIAL-VIDEO_FIRST_SPECIAL
	jnc	setbad
	add	bx,bx
	.word	0xa7ff, spec_inits	! JMP [BX+spec_inits]

setmenu:
	or	al,al		! 80x25 is an exception
	jz	_set_80x25
	push	bx		! Set mode chosen from menu
	call	mode_table	! Build the mode table
	pop	ax
	shl	ax,#2
	add	si,ax
	cmp	si,di
	jnc	setbad
	mov	ax,(si)		! Fetch mode ID
_m_s:	jmp	mode_set

setres:
	push	bx		! Set mode chosen by its resolution
	call	mode_table
	pop	bx
	xchg	bh,bl
setr1:	lodsw
	cmp	ax,#ASK_VGA	! End of the list?
	jz	setbad
	lodsw
	cmp	ax,bx
	jnz	setr1
	mov	ax,(si-4)	! Fetch mode ID
	jmp	_m_s


check_vesa:
	lea	di,modelist+1024
	sub	bh,#VIDEO_FIRST_VESA>>8
	mov	cx,bx		! Get mode information structure
	mov	ax,#0x4f01
	int	0x10
	add	bh,#VIDEO_FIRST_VESA>>8
	cmp	ax,#0x004f
	jnz	setbad

	mov	al,(di)		! Check capabilities.
	and	al,#0x19
	cmp	al,#0x09
	jz	setvesa		! this is a text mode

	mov	al,(di)		! Check capabilities.
	and	al,#0x99
	cmp	al,#0x99
	jnz	_setbad		! to bad, no linear frame buffer

	sub	bh,#VIDEO_FIRST_VESA>>8
	or	bx,#0x4000	! want use linear frame buffer	
	mov	ax,#0x4f02	! VESA BIOS mode set call
	int	0x10
	cmp	ax,#0x004f	! AL=4f if implemented, AH=0 if OK
	jnz	_setbad

	movb	[graphic_mode],#1 ! flag graphic mode
	movb	[do_restore],#0   ! no screen restore
	stc
	ret
	
_setbad:	br	setbad		! Ugly...
	
!
! Recalculate vertical display end registers -- this fixes various
! inconsistencies of extended modes on many adapters. Called when
! the VIDEO_RECALC flag is set in the mode ID.
!

setrec:	sub	ah,#VIDEO_RECALC>>8	! Set the base mode
	call	mode_set
	jnc	rct3
	seg	gs			! Font size in pixels
	mov	ax,[0x485]
	seg	gs			! Number of rows
	mov	bl,[0x484]
	inc	bl
	mul	bl			! Number of visible
	dec	ax			! scan lines - 1
	mov	dx,#0x3d4
	mov	bx,ax
	mov	al,#0x12		! Lower 8 bits
	mov	ah,bl
	out	dx,ax
	mov	al,#0x07		! Bits 8 and 9 in the overflow register
	call	inidx
	xchg	ah,al
	and	ah,#0xbd
	shr	bh,#1
	jnc	rct1
	or	ah,#0x02
rct1:	shr	bh,#1
	jnc	rct2
	or	ah,#0x40
rct2:	mov	al,#0x07
	out	dx,ax
	stc
rct3:	ret

!
! Table of routines for setting of the special modes.
!

spec_inits:
	.word	set_80x25
	.word	set_8pixel
	.word	set_80x43
	.word	set_80x28
	.word	set_current
	.word	set_80x30
	.word	set_80x34
	.word	set_80x60
	.word	set_gfx

!
! Set the 80x25 mode. If already set, do nothing.
!

set_80x25:
	mov	[force_size],#0x5019	! Override possibly broken BIOS vars
use_80x25:
#ifdef CONFIG_VIDEO_400_HACK
	mov	ax,#0x1202	! Force 400 scan lines
	mov	bl,#0x30
	int	0x10
#else
	mov	ah,#0x0f	! Get current mode ID
	int	0x10
	cmp	ax,#0x5007	! Mode 7 (80x25 mono) is the only one available
	jz	st80		! on CGA/MDA/HGA and is also available on EGAM
	cmp	ax,#0x5003	! Unknown mode => force 80x25 color
	jnz	force3
st80:	cmpb	[adapter],#0	! CGA/MDA/HGA => mode 3/7 is always 80x25
	jz	set80
	seg	gs		! This is EGA+ -- beware of 80x50 etc.
	mov	al,[0x0484]
	or	al,al		! Some buggy BIOSes set 0 rows
	jz	set80
	cmp	al,#24		! Let's hope this is correct
	jz	set80
#endif /* CONFIG_VIDEO_400_HACK */
force3:	DO_STORE
	mov	ax,#0x0003	! Forced set
	int	0x10
set80:	stc
	ret

!
! Set the 80x50/80x43 8-pixel mode. Simple BIOS calls.
!

set_8pixel:
	DO_STORE
	call	use_80x25	! The base is 80x25
set_8pt:
	mov	ax,#0x1112	! Use 8x8 font
	xor	bl,bl
	int	0x10
	mov	ax,#0x1200	! Use alternate print screen
	mov	bl,#0x20
	int	0x10
	mov	ax,#0x1201	! Turn off cursor emulation
	mov	bl,#0x34
	int	0x10
	mov	ah,#0x01	! Define cursor (scan lines 6 to 7)
	mov	cx,#0x0607
	int	0x10
set_current:
	stc
	ret

!
! Set the 80x28 mode. This mode works on all VGA's, because it's a standard
! 80x25 mode with 14-point fonts instead of 16-point.
!

set_80x28:
	DO_STORE
	call	use_80x25	! The base is 80x25
set14:	mov	ax,#0x1111	! Use 9x14 font
	xor	bl,bl
	int	0x10
	mov	ah,#0x01	! Define cursor (scan lines 11 to 12)
	mov	cx,#0x0b0c
	int	0x10
	stc
	ret

!
! Set the 80x43 mode. This mode is works on all VGA's.
! It's a 350-scanline mode with 8-pixel font.
!

set_80x43:
	DO_STORE
	mov	ax,#0x1201	! Set 350 scans
	mov	bl,#0x30
	int	0x10
	mov	ax,#0x0003	! Reset video mode
	int	0x10
	jmp	set_8pt		! Use 8-pixel font

!
! Set the 80x30 mode (all VGA's). 480 scanlines, 16-pixel font.
!

set_80x30:
	call	use_80x25	! Start with real 80x25
	DO_STORE
	mov	dx,#0x3cc	! Get CRTC port
	in	al,dx
	mov	dl,#0xd4
	ror	al,#1		! Mono or color?
	jc	set48a
	mov	dl,#0xb4
set48a:	mov	ax,#0x0c11	! Vertical sync end (also unlocks CR0-7)
 	call	outidx
	mov	ax,#0x0b06	! Vertical total
 	call	outidx
	mov	ax,#0x3e07	! (Vertical) overflow
 	call	outidx
	mov	ax,#0xea10	! Vertical sync start
 	call	outidx
	mov	ax,#0xdf12	! Vertical display end
	call	outidx
	mov	ax,#0xe715	! Vertical blank start
 	call	outidx
	mov	ax,#0x0416	! Vertical blank end
 	call	outidx
	push	dx
	mov	dl,#0xcc	! Misc output register (read)
 	in	al,dx
 	mov	dl,#0xc2	! (write)
 	and	al,#0x0d	! Preserve clock select bits and color bit
 	or	al,#0xe2	! Set correct sync polarity
 	out	dx,al
	pop	dx
	mov	[force_size],#0x501e
	stc			! That's all.
	ret

!
! Set the 80x34 mode (all VGA's). 480 scans, 14-pixel font.
!

set_80x34:
	call	set_80x30	! Set 480 scans
	call	set14		! And 14-pt font
	mov	ax,#0xdb12	! VGA vertical display end
	mov	[force_size],#0x5022
setvde:	call	outidx
	stc
	ret

!
! Set the 80x60 mode (all VGA's). 480 scans, 8-pixel font.
!

set_80x60:
	call	set_80x30	! Set 480 scans
	call	set_8pt		! And 8-pt font
	mov	ax,#0xdf12	! VGA vertical display end
	mov	[force_size],#0x503c
	jmp	setvde

!
! Special hack for ThinkPad graphics
!

set_gfx:
#ifdef CONFIG_VIDEO_GFX_HACK
	mov	ax,# VIDEO_GFX_BIOS_AX
	mov	bx,# VIDEO_GFX_BIOS_BX
	int	0x10
	mov	[force_size],# VIDEO_GFX_DUMMY_RESOLUTION
	stc
#endif
	ret

#ifdef CONFIG_VIDEO_RETAIN

!
! Store screen contents to temporary buffer.
!

store_screen:
	cmpb	[do_restore],#0		! Already stored?
	jnz	stsr
	testb	[loadflags],#CAN_USE_HEAP	! Have we space for storing?
	jz	stsr
	push	ax
	push	bx
	push	[force_size]		! Don't force specific size
	mov	[force_size],#0
	call	mode_params		! Obtain params of current mode
	pop	[force_size]

	seg	fs
	mov	ah,[PARAM_VIDEO_LINES]
	seg	fs
	mov	al,[PARAM_VIDEO_COLS]
	mov	bx,ax			! BX=dimensions
	mul	ah
	mov	cx,ax			! CX=number of characters to store
	add	ax,ax			! Calculate image size
	add	ax,#modelist+1024+4
	cmp	ax,[heap_end_ptr]
	jnc	sts1			! Unfortunately, out of memory

	seg	fs			! Store mode params
	mov	ax,[PARAM_CURSOR_POS]
	lea	di,modelist+1024
	stosw
	mov	ax,bx
	stosw

	push	ds			! Store the screen
	mov	ds,[video_segment]
	xor	si,si
	rep
	movsw
	pop	ds
	incb	[do_restore]		! Screen will be restored later
sts1:	pop	bx
	pop	ax
stsr:	ret

!
! Restore screen contents from temporary buffer.
!

restore_screen:
	cmpb	[do_restore],#0		! Has the screen been stored?
	jz	res1
	call	mode_params		! Get parameters of current mode
	seg	fs
	mov	cl,[PARAM_VIDEO_LINES]
	seg	fs
	mov	ch,[PARAM_VIDEO_COLS]
	lea	si,modelist+1024	! Screen buffer
	lodsw				! Set cursor position
	mov	dx,ax
	cmp	dh,cl
	jc	res2
	mov	dh,cl
	dec	dh
res2:	cmp	dl,ch
	jc	res3
	mov	dl,ch
	dec	dl
res3:	mov	ah,#0x02
	mov	bh,#0x00
	int	0x10
	lodsw				! Display size
	mov	dl,ah			! DL=number of lines
	mov	ah,#0			! BX=physical length of orig. line
	mov	bx,ax
	cmp	dl,cl			! Too many?
	jc	res4
	push	ax
	mov	al,dl
	sub	al,cl
	mul	bl
	add	si,ax
	add	si,ax
	pop	ax
	mov	dl,cl
res4:	cmp	al,ch			! Too wide?
	jc	res5
	mov	al,ch			! AX=width of src. line
res5:	mov	cl,#0
	xchg	cl,ch
	mov	bp,cx			! BP=width of dest. line
	push	es
	mov	es,[video_segment]
	xor	di,di			! Move the data
	add	bx,bx			! Convert BX and BP to _bytes_
	add	bp,bp
res6:	push	si
	push	di
	mov	cx,ax
	rep
	movsw
	pop	di
	pop	si
	add	di,bp
	add	si,bx
	dec	dl
	jnz	res6
	pop	es			! Done
res1:	ret

#endif /* CONFIG_VIDEO_RETAIN */

!
! Write to indexed VGA register (AL=index, AH=data, DX=index reg. port)
!

outidx:	out	dx,al
	push	ax
	mov	al,ah
	inc	dx
	out	dx,al
	dec	dx
	pop	ax
	ret

!
! Build the table of video modes (stored after the setup.S code at the
! `modelist' label. Each video mode record looks like:
!	.word	MODE-ID		(our special mode ID (see above))
!	.byte	rows		(number of rows)
!	.byte	columns		(number of columns)
! Returns address of the end of the table in DI, the end is marked
! with a ASK_VGA ID.
!

mode_table:
	mov	di,[mt_end]	! Already filled?
	or	di,di
	jnz	mtab1x
	lea	di,modelist	! Store standard modes:

	mov	eax,#VIDEO_80x25 + 0x50190000	! The 80x25 mode (ALL)
	stosd
	mov	al,[adapter]	! CGA/MDA/HGA -- no more modes
	or	al,al
	jz	mtabe
	dec	al
	jnz	mtabv
	mov	eax,#VIDEO_8POINT + 0x502b0000	! The 80x43 EGA mode
	stosd
	jmp	mtabe
mtab1x:	jmp	mtab1

mtabv:	lea	si,vga_modes			! All modes for std VGA
	mov	cx,#vga_modes_end-vga_modes
	rep	! I'm unable to use movsw as I don't know how to store a half
	movsb	! of the expression above to cx without using explicit shr.

	cmpb	[scanning],#0			! Mode scan requested?
	jz	mscan1
	call	mode_scan
mscan1:

#ifdef CONFIG_VIDEO_LOCAL
	call	local_modes
#endif /* CONFIG_VIDEO_LOCAL */
#ifdef CONFIG_VIDEO_VESA
	call	vesa_modes			! Detect VESA VGA modes
#endif /* CONFIG_VIDEO_VESA */
#ifdef CONFIG_VIDEO_SVGA
	cmpb	[scanning],#0			! Bypass when scanning
	jnz	mscan2
	call	svga_modes			! Detect SVGA cards & modes
mscan2:
#endif /* CONFIG_VIDEO_SVGA */

mtabe:

#ifdef CONFIG_VIDEO_COMPACT
	lea	si,modelist	! Compact video mode list if requested.
	mov	dx,di
	mov	di,si
cmt1:	cmp	si,dx		! Scan all modes
	jz	cmt2
	lea	bx,modelist	! Find in previous entries
	mov	cx,(si+2)
cmt3:	cmp	si,bx
	jz	cmt4
	cmp	cx,(bx+2)	! Found => don't copy this entry
	jz	cmt5
	add	bx,#4
	jmp	cmt3

cmt4:	movsd			! Copy entry
	jmp	cmt1

cmt5:	add	si,#4		! Skip entry
	jmp	cmt1

cmt2:
#endif	/* CONFIG_VIDEO_COMPACT */

	mov	(di),#ASK_VGA	! End marker
	mov	[mt_end],di
mtab1:	lea	si,modelist	! Returning: SI=mode list, DI=list end
ret0:	ret

! Modes usable on all standard VGAs

vga_modes:
	.word	VIDEO_8POINT
	.word	0x5032		! 80x50
	.word	VIDEO_80x43
	.word	0x502b		! 80x43
	.word	VIDEO_80x28
	.word	0x501c		! 80x28
	.word	VIDEO_80x30
	.word	0x501e		! 80x30
	.word	VIDEO_80x34
	.word	0x5022		! 80x34
	.word	VIDEO_80x60
	.word	0x503c		! 80x60
#ifdef CONFIG_VIDEO_GFX_HACK
	.word	VIDEO_GFX_HACK
	.word	VIDEO_GFX_DUMMY_RESOLUTION
#endif
vga_modes_end:

!
! Detect VESA modes.
!

#ifdef CONFIG_VIDEO_VESA

vesa_modes:
	cmpb	[adapter],#2	! VGA only
	jnz	ret0
	mov	bp,di		! BP=original mode table end
	add	di,#0x200	! Buffer space
	mov	ax,#0x4f00	! VESA Get card info call
	int	#0x10
	mov	di,bp
	cmp	ax,#0x004f	! Successful?
	jnz	ret0
	cmp	(di+0x200),#0x4556
	jnz	ret0
	cmp	(di+0x202),#0x4153
	jnz	ret0
	mov	[card_name],#vesa_name	! Set name to "VESA VGA"
	push	gs
	lgs	si,(di+0x20e)	! GS:SI=mode list
	mov	cx,#128		! Iteration limit
vesa1:	seg	gs		! Get next mode in the list
	lodsw
	cmp	ax,#0xffff	! End of the table?
	jz	vesar
	cmp	ax,#0x0080	! Check validity of mode ID
	jc	vesa2
	or	ah,ah		! Valid IDs are 0x0000-0x007f and 0x0100-0x07ff
	jz	vesan		! [Certain BIOSes erroneously report 0x80-0xff]
	cmp	ax,#0x0800
	jnc	vesae
vesa2:	push	cx
	mov	cx,ax		! Get mode information structure
	mov	ax,#0x4f01
	int	0x10
	mov	bx,cx		! BX=mode number
	add	bh,#VIDEO_FIRST_VESA>>8
	pop	cx
	cmp	ax,#0x004f
	jnz	vesan		! Don't report errors (buggy BIOSES :-[ )
	mov	al,(di)		! Check capabilities. We require
	and	al,#0x19	! a color text mode.
	cmp	al,#0x09
	jnz	vesan
	cmp	(di+8),#0xb800	! Standard video memory address required
	jnz	vesan
	testb	(di),#2		! Mode characteristics supplied?
	mov	(di),bx		! Store mode number
	jz	vesa3
	xor	dx,dx
	mov	bx,(di+0x12)	! Width
	or	bh,bh
	jnz	vesan
	mov	(di+3),bl
	mov	ax,(di+0x14)	! Height
	or	ah,ah
	jnz	vesan
	mov	(di+2),al
	mul	bl
	cmp	ax,#8193	! Small enough for Linux console driver?
	jnc	vesan
	jmp	vesaok

vesa3:	sub	bx,#0x8108	! This mode has no detailed info specified,
	jc	vesan		! so it must be a standard VESA mode.
	cmp	bx,#5
	jnc	vesan
	mov	ax,(bx+vesa_text_mode_table)
	mov	(di+2),ax
vesaok:	add	di,#4		! The mode is valid. Store it.
vesan:	loop	vesa1		! Next mode. Limit exceeded => error
vesae:	lea	si,vesaer
	call	prtstr
	mov	di,bp		! Discard already found modes.
vesar:	pop	gs
	ret

!
! Dimensions of standard VESA text modes
!

vesa_text_mode_table:
	db	60, 80		! 0108
	db	25, 132		! 0109
	db	43, 132		! 010A
	db	50, 132		! 010B
	db	60, 132		! 010C

#endif	/* CONFIG_VIDEO_VESA */

!
! Scan for video modes. A bit dirty, but should work.
!

mode_scan:
	mov	cx,#0x0100	! Start with mode 0
scm1:	mov	ah,#0		! Test the mode
	mov	al,cl
	int	0x10
	mov	ah,#0x0f
	int	0x10
	cmp	al,cl
	jnz	scm2		! Mode not set
	mov	dx,#0x3c0	! Test if it's a text mode
	mov	al,#0x10		! Mode bits
	call	inidx
	and	al,#0x03
	jnz	scm2
	mov	dl,#0xce		! Another set of mode bits
	mov	al,#0x06
	call	inidx
	shr	al,#1
	jc	scm2
	mov	dl,#0xd4		! Cursor location
	mov	al,#0x0f
	call	inidx
	or	al,al
	jnz	scm2
	mov	ax,cx		! OK, store the mode
	stosw
	seg	gs		! Number of rows
	mov	al,[0x484]
	inc	al
	stosb
	seg	gs		! Number of columns
	mov	ax,[0x44a]
	stosb
scm2:	inc	cl
	jns	scm1
	mov	ax,#0x0003	! Return back to mode 3
	int	0x10
	ret

tstidx:	out	dx,ax		! OUT DX,AX and inidx
inidx:	out	dx,al		! Read from indexed VGA register
	inc	dx		! AL=index, DX=index reg port -> AL=data
	in	al,dx
	dec	dx
	ret

!
! Try to detect type of SVGA card and supply (usually approximate) video
! mode table for it.
!

#ifdef CONFIG_VIDEO_SVGA

svga_modes:
	lea	si,svga_table	! Test all known SVGA adapters
dosvga:	lodsw
	mov	bp,ax		! Default mode table
	or	ax,ax
	jz	didsv1
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
	mov	ah,[svga_prefix]
cpsvga:	lodsb
	or	al,al
	jz	didsv
	stosw
	movsw
	jmp	cpsvga

didsv:	mov	[card_name],si	! Store pointer to card name
didsv1:	ret

!
! Table of all known SVGA cards. For each card, we store a pointer to
! a table of video modes supported by the card and a pointer to a routine
! used for testing of presence of the card. The video mode table is always
! followed by the name of the card or the chipset.
!

svga_table:
	.word	ati_md, ati_test
	.word	oak_md, oak_test
	.word	paradise_md, paradise_test
	.word	realtek_md, realtek_test
	.word	s3_md, s3_test
	.word	chips_md, chips_test
	.word	video7_md, video7_test
	.word	cirrus5_md, cirrus5_test
	.word	cirrus6_md, cirrus6_test
	.word	cirrus1_md, cirrus1_test
	.word	ahead_md, ahead_test
	.word	everex_md, everex_test
	.word	genoa_md, genoa_test
	.word	trident_md, trident_test
	.word	tseng_md, tseng_test
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
	br	outidx

idS3:	.byte	0x81, 0x82, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95
	.byte	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa8, 0xb0

s3_md:	.byte	0x54, 0x2b, 0x84
	.byte	0x55, 0x19, 0x84
	.byte	0
	.ascii	"S3"
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
	.ascii	"ATI"
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
	.ascii	"Ahead"
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
	.ascii	"Chips & Technologies"
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
	.ascii	"Cirrus Logic 5X0"
	.byte	0

! Cirrus Logic 54XX

cirrus5_test:
	mov	dx,#0x3c4
	mov	al,#6
	call	inidx
	mov	bl,al			! BL=backup
	mov	ax,#6
	call	tstidx
	cmp	al,#0x0f
	jne	c5fail
	mov	ax,#0x1206
	call	tstidx
	cmp	al,#0x12
	jne	c5fail
	mov	al,#0x1e
	call	inidx
	mov	bh,al
	mov	ah,bh
	and	ah,#0xc0
	mov	al,#0x1e
	call	tstidx
	and	al,#0x3f
	jne	c5xx
	mov	al,#0x1e
	mov	ah,bh
	or	ah,#0x3f
	call	tstidx
	xor	al,#0x3f
	and	al,#0x3f
c5xx:	pushf
	mov	al,#0x1e
	mov	ah,bh
	out	dx,ax
	popf
	je	c5done
c5fail:	xor	bp,bp
c5done:	mov	al,#6
	mov	ah,bl
	out	dx,ax
	ret

cirrus5_md:
	.byte	0x14, 0x19, 0x84
	.byte	0x54, 0x2b, 0x84
	.byte	0
	.ascii	"Cirrus Logic 54XX"
	.byte	0

! Cirrus Logic 64XX -- no known extra modes, but must be identified, because
! it's misidentified by the Ahead test.

cirrus6_test:
	mov	dx,#0x3ce
	mov	al,#0x0a
	call	inidx
	mov	bl,al			! BL=backup
	mov	ax,#0xce0a
	call	tstidx
	or	al,al
	jne	c2fail
	mov	ax,#0xec0a
	call	tstidx
	cmp	al,#0x01
	jne	c2fail
	mov	al,#0xaa
	call	inidx	! 4X, 5X, 7X and 8X are valid 64XX chip IDs
	shr	al,#4
	sub	al,#4
	jz	c6done
	dec	al
	jz	c6done
	sub	al,#2
	jz	c6done
	dec	al
	jz	c6done
c2fail:	xor	bp,bp
c6done:	mov	al,#0x0a
	mov	ah,bl
	out	dx,ax
	ret

cirrus6_md:
	.byte	0
	.ascii	"Cirrus Logic 64XX"
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
	.ascii	"Everex/Trident"
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
	.ascii	"Genoa"
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
	.ascii	"OAK"
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
	.ascii	"Paradise"
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
	.ascii	"Trident"
	.byte	0

! Tseng.

tseng_test:
	mov	dx,#0x3cd
	in	al,dx			! Could things be this simple? :-)
	mov	bl,al
	mov	al,#0x55
	out	dx,al
	in	al,dx
	mov	ah,al
	mov	al,bl
	out	dx,al
	cmp	ah,#0x55
 	je	istsen
isnot:	xor	bp,bp
istsen:	ret

tseng_md:
	.byte	0x26, 0x3c, 0x50
	.byte	0x2a, 0x28, 0x64
	.byte	0x23, 0x19, 0x84
	.byte	0x24, 0x1c, 0x84
	.byte	0x22, 0x2c, 0x84
	.byte	0x21, 0x3c, 0x84
	.byte	0
	.ascii	"Tseng"
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
	jne	isnot
	movb	[svga_prefix],#VIDEO_FIRST_V7>>8	! Use special mode switching
	ret

video7_md:
	.byte	0x40, 0x2b, 0x50
	.byte	0x43, 0x3c, 0x50
	.byte	0x44, 0x3c, 0x64
	.byte	0x41, 0x19, 0x84
	.byte	0x42, 0x2c, 0x84
	.byte	0x45, 0x1c, 0x84
	.byte	0
	.ascii	"Video 7"
	.byte	0

! Realtek VGA

realtek_test:
	lea	si,idrtvga
	mov	di,#0x45
	mov	cx,#0x0b
	repe
	cmpsb
	je	isrt
	xor	bp,bp
isrt:	ret

idrtvga:	.ascii	"REALTEK VGA"

realtek_md:
	.byte	0x1a, 0x3c, 0x50
	.byte	0x1b, 0x19, 0x84
	.byte	0x1c, 0x1e, 0x84
	.byte	0x1d, 0x2b, 0x84
	.byte	0x1e, 0x3c, 0x84
	.byte	0
	.ascii	"REALTEK"
	.byte	0

#endif	/* CONFIG_VIDEO_SVGA */

!
! User-defined local mode table (VGA only)
!

#ifdef CONFIG_VIDEO_LOCAL

local_modes:
	lea	si,local_mode_table
locm1:	lodsw
	or	ax,ax
	jz	locm2
	stosw
	movsw
	jmp	locm1
locm2:	ret

! This is the table of local video modes which can be supplied manually
! by the user. Each entry consists of mode ID (word) and dimensions
! (byte for column count and another byte for row count). These modes
! are placed before all SVGA and VESA modes and override them if table
! compacting is enabled. The table must end with a zero word followed
! by NUL-terminated video adapter name.

local_mode_table:
	.word	0x0100		! Example: 40x25
	.byte	25,40
	.word	0
	.ascii	"Local"
	.byte	0

#endif	/* CONFIG_VIDEO_LOCAL */

!
! Read a key and return the ASCII code in al, scan code in ah
!

getkey:	xor	ah,ah
	int	0x16
	ret

!
! Read a key with a timeout of 30 seconds. The hardware clock is used to get
! the time.
!

getkt:	call	gettime
	add	al,#30		! Wait 30 seconds
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
! Print hexadecimal number.
!

prthw:	push	ax
	mov	al,ah
	call	prthb
	pop	ax
prthb:	push	ax
	shr	al,#4
	call	prthn
	pop	ax
	and	al,#0x0f
prthn:	cmp	al,#0x0a
	jc	prth1
	add	al,#0x07
prth1:	add	al,#0x30
	br	prtchr

!
! Print decimal number (AL).
!

prtdec:	push	ax
	push	cx
	xor	ah,ah		! Clear ah
	mov	cl,#0x0a
	idiv	cl
	cmp	al,#0x09
	jbe	lt100
	call	prtdec
	jmp	skip10
lt100:	add	al,#0x30
	call	prtchr
skip10:	mov	al,ah
	add	al,#0x30
	call	prtchr	
	pop	cx
	pop	ax
	ret

!
! VIDEO_SELECT-only variables
!

mt_end:		.word	0	! End of video mode table if built
edit_buf:	.space	6	! Line editor buffer
card_name:	.word	0	! Pointer to adapter name
scanning:	.byte	0	! Performing mode scan
do_restore:	.byte	0	! Screen contents altered during mode change
svga_prefix:	.byte	VIDEO_FIRST_BIOS>>8	! Default prefix for BIOS modes
graphic_mode:	.byte	0	! Graphic mode with a linear frame buffer

!
! Messages:
!

keymsg:		.ascii	"Press <RETURN> to see video modes available, "
		.ascii	"<SPACE> to continue or wait 30 secs"
		db	0x0d, 0x0a, 0
listhdr:	db	0x0d, 0x0a
		.ascii	"Mode:    COLSxROWS:"
crlft:		db	0x0d, 0x0a, 0
prompt:		db	0x0d, 0x0a
		.ascii	"Enter mode number or `scan': "
		db	0
unknt:		.ascii	"Unknown mode ID. Try again."
		db	0
badmdt:		.ascii	"You passed an undefined mode number."
		db	0x0d, 0x0a, 0
vesaer:		.ascii	"Error: Scanning of VESA modes failed. Please "
		.ascii	"report to <mj@k332.feld.cvut.cz>."
		db	0x0d, 0x0a, 0
old_name:	.ascii	"CGA/MDA/HGA"
		db	0
ega_name:	.ascii	"EGA"
		db	0
svga_name:	.ascii	" "
vga_name:	.ascii	"VGA"
		db	0
vesa_name:	.ascii	"VESA"
		db	0
name_bann:	.ascii	"Video adapter: "
		db	0

#endif /* CONFIG_VIDEO_SELECT */

!
! Other variables:
!

adapter:	.byte	0	! Video adapter: 0=CGA/MDA/HGA,1=EGA,2=VGA
video_segment:	.word	0xb800	! Video memory segment
force_size:	.word	0	! Use this size instead of the one in BIOS vars
