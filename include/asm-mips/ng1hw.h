/* This is the hardware interface for newport graphics.  It's taken from
   IRIX.

   Alex deVries <puffin@redhat.com>

*/


#ifndef __SYS_NG1HW_H__
#define __SYS_NG1HW_H__

#ident "$Revision: 1.1 $"

#define BIT(n)	(0x1 << n)


#ifndef REX_ASMCODE

typedef union {
    volatile float flt;
    volatile unsigned int word;
} float_long;

typedef volatile unsigned int vol_ulong;
typedef volatile unsigned int fixed16;

typedef union {
    vol_ulong   byword;
    struct {
	volatile unsigned short s0;
	volatile unsigned short s1;
    } byshort;
    struct {
	volatile unsigned char b0, b1, b2;
	volatile unsigned char b3;
    } bybyte;
} DCB_reg;

#ifndef REXSIM
typedef struct rex3regs {	/* THE CHIP */
    vol_ulong drawmode1;	/* extra mode bits for GL 	0x0000 */
    vol_ulong drawmode0;	/* command register 		0x0004 */

    vol_ulong lsmode;		/* line stipple mode		0x0008 */
    vol_ulong lspattern;	/* 32 bit pixel lspattern	0x000c */
    vol_ulong lspatsave;	/* save register for lspattern	0x0010 */
    vol_ulong zpattern;		/* 32 bit pixel zpattern	0x0014 */

    vol_ulong colorback;	/* background color		0x0018 */
    vol_ulong colorvram;	/* fast vram clear color	0x001c */
    vol_ulong alpharef;		/* afunction reference value	0x0020 */

    vol_ulong pad0;		/* padding 			0x0024 */	

    vol_ulong smask0x;		/* screen mask 0, window rel,	0x0028 */
    vol_ulong smask0y;		/* exclusively for the GL	0x002c */
    vol_ulong _setup;		/* do line/span setup, no iter  0x0030 */
    vol_ulong _stepz;		/* Enable ZPATTERN for this pix 0x0034 */
    vol_ulong _lsrestore;	/* Restore lspattern,count	0x0038 */
    vol_ulong _lssave;		/* Backup lspattern,count	0x003c */

    char _pad1[0x100-0x40];

    float_long _xstart;		/* 16.4(7) current x		0x0100 */
    float_long _ystart;		/* 16.4(7) current y		0x0104 */
    float_long _xend;		/* 16.4(7)			0x0108 */
    float_long _yend;		/* 16.4(7)			0x010c */
    vol_ulong xsave;		/* 16	x save for blocks	0x0110 */
    vol_ulong xymove;		/* x,y copy dest offset		0x0114 */
    float_long bresd;		/* s19.8 bres d error term	0x0118 */
    float_long bress1;		/* s2.15 bres s coverage term	0x011c */
    vol_ulong bresoctinc1;	/* 3(4)17.3 octant+inc1 value	0x0120 */
    volatile int bresrndinc2;	/* 8(3)18.3 bres inc2 value	0x0124 */
    vol_ulong brese1;		/* 1.15 bres e1 (minor slope)	0x0128 */
    vol_ulong bress2;           /* s18.8 bres s2 coverage term  0x012c */
    vol_ulong aweight0;		/* antialiasing weights		0x0130 */
    vol_ulong aweight1;		/* antialiasing weights		0x0134 */
    float_long xstartf;		/* 12.4(7) GL version of _xstart0x0138 */
    float_long ystartf;		/* 12.4(7)			0x013c */
    float_long xendf;		/* 12.4(7)			0x0140 */
    float_long yendf;		/* 12.4(7)			0x0144 */
    fixed16 xstarti;		/* 16 integer format for xstart 0x0148 */
    float_long xendf1;		/* 12.4(7) same as xend		0x014c */
    fixed16 xystarti;		/* 16,16			0x0150 */
    fixed16 xyendi;		/* 16,16			0x0154 */
    fixed16 xstartendi;		/* 16,16			0x0158 */
    char _pad2[0x200-0x15c];
    float_long colorred;	/* o12.11 red (also foreground)	0x0200 */
    float_long coloralpha;	/* o8.11 alpha			0x0204 */
    float_long colorgrn;	/* o8.11 green			0x0208 */
    float_long colorblue;	/* o8.11 blue			0x020c */
    float_long slopered;	/* s9.11			0x0210 */
    float_long slopealpha;	/* s9.11			0x0214 */
    float_long slopegrn;	/* s9.11			0x0218 */
    float_long slopeblue;	/* s9.11			0x021c */
    vol_ulong wrmask;		/* writemask			0x0220 */
    vol_ulong colori;		/* packed bgr/ci		0x0224 */
    float_long colorx;		/* 12.11 red (no overflow)	0x0228 */
    float_long slopered1;	/* same as slopered		0x022c */
    vol_ulong hostrw0;		/* host PIO/DMA port (msw)	0x0230 */
    vol_ulong hostrw1;		/* host PIO/DMA port (lsw)	0x0234 */
    vol_ulong dcbmode;		/* display ctrl bus mode reg 	0x0238 */
    volatile int pad3;						/* 0x023c */
    DCB_reg dcbdata0;		/* display ctrl bus port (msw)	0x0240 */
    vol_ulong dcbdata1;		/* display ctrl bus port (lsw)	0x0244 */
} Rex3regs;
	

typedef struct configregs {
    vol_ulong smask1x;		/* screenmask1 right,left edges 0x1300 */
    vol_ulong smask1y;		/* screenmask1 bottom,top edges 0x1304 */
    vol_ulong smask2x;		/* screenmask2 right,left edges 0x1308 */
    vol_ulong smask2y;		/* screenmask2 bottom,top edges 0x130c */
    vol_ulong smask3x;		/* screenmask3 right,left edges 0x1310 */
    vol_ulong smask3y;		/* screenmask3 bottom,top edges 0x1314 */
    vol_ulong smask4x;		/* screenmask4 right,left edges 0x1318 */
    vol_ulong smask4y;		/* screenmask4 bottom,top edges 0x131c */
    vol_ulong topscan;		/* y coord of top screen line	0x1320 */
    vol_ulong xywin;		/* window offset 		0x1324 */
    vol_ulong clipmode;		/* cid,smask settings 		0x1328 */
    vol_ulong pad0;		/*				0x132c */
    vol_ulong config;		/* miscellaneous  config bits 	0x1330 */
    vol_ulong pad1;		/*				0x1334 */
    vol_ulong status;		/* chip busy, FIFO, int status  0x1338 */
				/* read clears interrupt status bits   */
    vol_ulong ustatus;		/* padding on rex rev a, 'read-only' 0x133c */
				/* copy of status on rex rev b.	       */
    vol_ulong dcbreset;		/* resets DCB and flushes BFIFO 0x1340 */
} Configregs;

typedef struct rex3chip {
		/* page 0 */    
	struct rex3regs set;		/* 0x0000 */
	char _pad0[0x7fc-sizeof(struct rex3regs)];
	volatile unsigned int dummy; /* 0x7fc */
	struct rex3regs go;		/* 0x0800 */

	char _pad1[0x1300-0x800-sizeof(struct rex3regs)];

		/* page 1 */
	struct {
	    struct configregs set; 	/* 0x1300 */
	    char _pad0[0x800-sizeof(struct configregs)];
	    struct configregs go; 	/* 0x1b00 */
	} p1;
} rex3Chip, Rex3chip;


#endif /* REX_ASMCODE */
#endif /* REXSIM */

/* Since alot of flags went away, define here as null bits
   and leave the code as it is for now, 
   marking where we have to change stuff.

   NONE of these should be defined !   - billt
   */

#define LSCONTINUE 0
#define SHADECONTINUE 0
#define XYCONTINUE 0
#define XMAJOR 0
#define YMAJOR 0
#define QUADMODE 0
#define LRQPOLY 0
/* RGBMODE, DITHER now live in DM1 */
#define RGBMODECMD 0
#define DITHER 0
#define DITHERRANGE 0
/* BLOCK is a function of ADDRMODE */
#define BLOCK 0
#define STOPONX 0
#define STOPONY 0
/* COLORCOMPARE is a combo of 3 bits (<, = , >) */
#define COLORCOMP 0
/* FRACTIONS are gone... */
#define INITFRAC 0
#define FRACTION1 0

/* -- some old AUX1 junk -- */
#define DOUBLEBUF 0
#define DBLDST0 0
#define DBLDST1 0
#define DBLSRC 0
#define COLORAUX 0


/* --- a couple of old cmds also only for conversion --- */
#define REX_LDPIXEL 0x1
#define REX_ANTIAUX 0
#define REX_DRAW 0
#define LOGICSRC 0
/* --- Blech! locicops are in DM1 too! */
#define REX_LO_ZERO REX_LO_ZERO 
#define REX_LO_AND DM1_LO_AND
#define REX_LO_ANDR DM1_LO_ANDR
#define REX_LO_SRC DM1_LO_SRC
#define REX_LO_ANDI DM1_LO_ANDI
#define REX_LO_DST DM1_LO_DST
#define REX_LO_XOR DM1_LO_XOR
#define REX_LO_OR DM1_LO_OR
#define REX_LO_NOR DM1_LO_NOR
#define REX_LO_XNOR DM1_LO_XNOR
#define REX_LO_NDST DM1_LO_NDST
#define REX_LO_ORR DM1_LO_ORR
#define REX_LO_NSRC DM1_LO_NSRC
#define REX_LO_ORI DM1_LO_ORI
#define REX_LO_NAND DM1_LO_NAND
#define REX_LO_ONE DM1_LO_ONE


/*
 *  drawmode flags
 */
#define DM0_OPCODE	0x3		/* opcode(1:0)	*/
#	define DM0_NOP		0x0
#	define DM0_READ		0x1
#	define DM0_DRAW		0x2
#	define DM0_SCR2SCR	0x3
#define DM0_ADRMODE_SHIFT	2	/* adrmode(2:0)	*/
#	define DM0_ADRMODE	(0x7<<DM0_ADRMODE_SHIFT)
#	define DM0_SPAN		(0x0<<DM0_ADRMODE_SHIFT)
#	define DM0_BLOCK	(0x1<<DM0_ADRMODE_SHIFT)
#	define DM0_ILINE	(0x2<<DM0_ADRMODE_SHIFT)
#	define DM0_FLINE	(0x3<<DM0_ADRMODE_SHIFT)
#	define DM0_ALINE	(0x4<<DM0_ADRMODE_SHIFT)
#ifdef OLDJUNK

/* XXX These definitions are obsolete */

#	define DM0_AELINE	(0x5<<DM0_ADRMODE_SHIFT)
#	define DM0_ACWEDGE	(0x6<<DM0_ADRMODE_SHIFT)
#	define DM0_ACCWEDGE	(0x7<<DM0_ADRMODE_SHIFT)

#else

/* XXX These are according to 11/2/92 spec */

#       define DM0_TLINE        (0x5<<DM0_ADRMODE_SHIFT)
#       define DM0_BLINE        (0x6<<DM0_ADRMODE_SHIFT)

#endif /* OLDJUNK */

#define DM0_DOSETUP	BIT(5)
#define DM0_COLORHOST	BIT(6)
#define DM0_ALPHAHOST	BIT(7)
#define DM0_STOPONX	BIT(8)
#define DM0_STOPONY	BIT(9)
#define DM0_STOPONXY	(DM0_STOPONX | DM0_STOPONY)
#define DM0_SKIPFIRST	BIT(10)
#define DM0_SKIPLAST	BIT(11)
#define DM0_ENZPATTERN	BIT(12)
#define DM0_ENLSPATTERN	BIT(13)
#define DM0_LSADVLAST	BIT(14)
#define DM0_LENGTH32	BIT(15)
#define DM0_ZOPAQUE	BIT(16)
#define DM0_LSOPAQUE	BIT(17)
#define DM0_SHADE	BIT(18)
#define DM0_LRONLY	BIT(19)

#ifdef OLDJUNK

/* XXX These definitions are obsolete */

#define DM0_CICLAMP     BIT(20)
#define DM0_ENDPTFILTER BIT(21)

#else

/* XXX These are according to 11/2/92 spec */

#define DM0_XYOFFSET    BIT(20)
#define DM0_CICLAMP     BIT(21)
#define DM0_ENDPTFILTER BIT(22)

#endif	/* OLDJUNK */
/* New Feature in REX REV B */
#define	DM0_YSTRIDE	BIT(23)

#define DM1_PLANES_SHIFT	0
#define DM1_PLANES	0x7		/* planes(2:0)	*/
#	define DM1_NOPLANES	0x0
#	define DM1_RGBPLANES	0x1
#	define DM1_RGBAPLANES	0x2
#	define DM1_OLAYPLANES	0x4
#	define DM1_PUPPLANES	0x5
#	define DM1_CIDPLANES	0x6
#define DM1_DRAWDEPTH_SHIFT	3	/* drawdepth(1:0)	*/
#define DM1_DRAWDEPTH_MASK      (3 << DM1_DRAWDEPTH_SHIFT)
#	define DM1_DRAWDEPTH	(0x3 << DM1_DRAWDEPTH_SHIFT)	
#	define DM1_DRAWDEPTH4	(0x0 << DM1_DRAWDEPTH_SHIFT)
#	define DM1_DRAWDEPTH8	(0x1 << DM1_DRAWDEPTH_SHIFT)
#	define DM1_DRAWDEPTH12	(0x2 << DM1_DRAWDEPTH_SHIFT)
#	define DM1_DRAWDEPTH24	(0x3 << DM1_DRAWDEPTH_SHIFT)
#define DM1_DBLSRC		BIT(5)
#define DM1_YFLIP		BIT(6)
#define DM1_RWPACKED		BIT(7)
#define DM1_HOSTDEPTH_SHIFT 	8	/* hostdepth(1:0)	*/
#define DM1_HOSTDEPTH_MASK      (3 << DM1_HOSTDEPTH_SHIFT)
#	define DM1_HOSTDEPTH	(0x3 << DM1_HOSTDEPTH_SHIFT)
#	define DM1_HOSTDEPTH4	(0x0 << DM1_HOSTDEPTH_SHIFT)
#	define DM1_HOSTDEPTH8	(0x1 << DM1_HOSTDEPTH_SHIFT)
#	define DM1_HOSTDEPTH12	(0x2 << DM1_HOSTDEPTH_SHIFT)
#	define DM1_HOSTDEPTH32	(0x3 << DM1_HOSTDEPTH_SHIFT)
#define DM1_RWDOUBLE		BIT(10)
#define DM1_SWAPENDIAN		BIT(11)
#define DM1_COLORCOMPARE_SHIFT  12	/* compare (2:0)	*/
#define DM1_COLORCOMPARE_MASK  (7 << DM1_COLORCOMPARE_SHIFT)
#	define DM1_COLORCOMPARE (0x7 << DM1_COLORCOMPARE_SHIFT)
#	define DM1_COLORCOMPLT	BIT(12)
#	define DM1_COLORCOMPEQ	BIT(13)
#	define DM1_COLORCOMPGT	BIT(14)
#define DM1_RGBMODE		BIT(15)
#define DM1_ENDITHER		BIT(16)
#define DM1_FASTCLEAR		BIT(17)
#define DM1_ENBLEND		BIT(18)
#define DM1_SF_SHIFT		19	/* sfactor(2:0)	*/
#define DM1_SF_MASK   		(7 << DM1_SF_SHIFT)
#	define DM1_SF		(0x7 << DM1_SF_SHIFT)
#	define DM1_SF_ZERO	(0x0 << DM1_SF_SHIFT)
#	define DM1_SF_ONE	(0x1 << DM1_SF_SHIFT)
#	define DM1_SF_DC	(0x2 << DM1_SF_SHIFT)
#	define DM1_SF_MDC	(0x3 << DM1_SF_SHIFT)
#	define DM1_SF_SA	(0x4 << DM1_SF_SHIFT)
#	define DM1_SF_MSA	(0x5 << DM1_SF_SHIFT)
#define DM1_DF_SHIFT		22	/* dfactor(2:0)	*/
#define DM1_DF_MASK     (7 << DM1_DF_SHIFT)
#	define DM1_DF		(0x7 << DM1_DF_SHIFT)
#	define DM1_DF_ZERO	(0x0 << DM1_DF_SHIFT)
#	define DM1_DF_ONE	(0x1 << DM1_DF_SHIFT)
#	define DM1_DF_SC	(0x2 << DM1_DF_SHIFT)
#	define DM1_DF_MSC	(0x3 << DM1_DF_SHIFT)
#	define DM1_DF_SA	(0x4 << DM1_DF_SHIFT)
#	define DM1_DF_MSA	(0x5 << DM1_DF_SHIFT)
#define DM1_ENBACKBLEND		BIT(25)
#define DM1_ENPREFETCH		BIT(26)
#define DM1_BLENDALPHA		BIT(27)
#define DM1_LO_SHIFT		28	/* logicop(3:0)	*/
#	define DM1_LO		(0xF << DM1_LO_SHIFT)
#       define DM1_LO_MASK      DM1_LO
#	define DM1_LO_ZERO	(0x0 << DM1_LO_SHIFT)
#	define DM1_LO_AND	(0x1 << DM1_LO_SHIFT)
#	define DM1_LO_ANDR	(0x2 << DM1_LO_SHIFT)
#	define DM1_LO_SRC	(0x3 << DM1_LO_SHIFT)
#	define DM1_LO_ANDI	(0x4 << DM1_LO_SHIFT)
#	define DM1_LO_DST	(0x5 << DM1_LO_SHIFT)
#	define DM1_LO_XOR	(0x6 << DM1_LO_SHIFT)
#	define DM1_LO_OR	(0x7 << DM1_LO_SHIFT)
#	define DM1_LO_NOR	(0x8 << DM1_LO_SHIFT)
#	define DM1_LO_XNOR	(0x9 << DM1_LO_SHIFT)
#	define DM1_LO_NDST	(0xa << DM1_LO_SHIFT)
#	define DM1_LO_ORR	(0xb << DM1_LO_SHIFT)
#	define DM1_LO_NSRC	(0xc << DM1_LO_SHIFT)
#	define DM1_LO_ORI	(0xd << DM1_LO_SHIFT)
#	define DM1_LO_NAND	(0xe << DM1_LO_SHIFT)
#	define DM1_LO_ONE	(0xf << DM1_LO_SHIFT)



/*
 * Clipmode register bits
 */

#define SMASK0	BIT(0)
#define SMASK1	BIT(1)
#define SMASK2	BIT(2)
#define SMASK3	BIT(3)
#define SMASK4	BIT(4)
#define ALL_SMASKS	31

#define CM_CIDMATCH_SHIFT       9
#define CM_CIDMATCH_MASK        (0xf << CM_CIDMATCH_SHIFT)


/*
 * Status register bits
 */

#define REX3VERSION_MASK 7
#define GFXBUSY         BIT(3)
#define BACKBUSY        BIT(4)
#define VRINT           BIT(5)
#define VIDEOINT        BIT(6)
#define GFIFO_LEVEL_SHIFT       7
#define GFIFO_LEVEL_MASK        (0x3f << GFIFO_LEVEL_SHIFT)
#define BFIFO_LEVEL_SHIFT       13
#define BFIFO_LEVEL_MASK        (0x1f << BFIFO_LEVEL_SHIFT)
#define BFIFO_INT        BIT(18)
#define GFIFO_INT        BIT(19)


/*
 * Config register bits
 */

#define GIO32MODE       BIT(0)
#define BUSWIDTH        BIT(1)
#define EXTREGXCVR      BIT(2)
#define BFIFODEPTH_SHIFT        3
#define BFIFODEPTH_MASK         (0xf << BFIFODEPTH_SHIFT)
#define BFIFOABOVEINT   BIT(7)
#define GFIFODEPTH_SHIFT        8
#define GFIFODEPTH_MASK         (0x1f << GFIFODEPTH_SHIFT)
#define GFIFOABOVEINT   BIT(13)
#define TIMEOUT_SHIFT   14
#define TIMEOUT_MASK    (7 << TIMEOUT_SHIFT)
#define VREFRESH_SHIFT  17
#define VREFRESH_MASK   (0x7 << VREFRESH_SHIFT)
#define FB_TYPE         BIT(20)

/*
 * Display Control Bus (DCB) macros
 */

#define DCB_DATAWIDTH_MASK  (0x3)
#define DCB_ENDATAPACK      BIT(2)
#define DCB_ENCRSINC        BIT(3)
#define DCB_CRS_SHIFT    4
#define DCB_CRS_MASK     (0x7 << DCB_CRS_SHIFT)
#define DCB_ADDR_SHIFT   7
#define DCB_ADDR_MASK    (0xf << DCB_ADDR_SHIFT)
#define DCB_ENSYNCACK       BIT(11)
#define DCB_ENASYNCACK      BIT(12)
#define DCB_CSWIDTH_SHIFT   13
#define DCB_CSWIDTH_MASK    (0x1f << CSWIDTH_SHIFT)
#define DCB_CSHOLD_SHIFT    18
#define DCB_CSHOLD_MASK     (0x1f << CSHOLD_SHIFT)
#define DCB_CSSETUP_SHIFT   23
#define DCB_CSSETUP_MASK    (0x1f << CSSETUP_SHIFT)
#define DCB_SWAPENDIAN      BIT(28)


/*
 * Some values for DCBMODE fields
 */
#define DCB_DATAWIDTH_4		0x0
#define DCB_DATAWIDTH_1		0x1
#define DCB_DATAWIDTH_2		0x2
#define DCB_DATAWIDTH_3		0x3


/*
 * DCB_ADDR values to select the various dcb slave devices
 */
#define DCB_VC2          (0 << DCB_ADDR_SHIFT)
#define DCB_CMAP_ALL     (1 << DCB_ADDR_SHIFT)
#define DCB_CMAP0        (2 << DCB_ADDR_SHIFT)
#define DCB_CMAP1        (3 << DCB_ADDR_SHIFT)
#define DCB_XMAP_ALL     (4 << DCB_ADDR_SHIFT)
#define DCB_XMAP0        (5 << DCB_ADDR_SHIFT)
#define DCB_XMAP1        (6 << DCB_ADDR_SHIFT)
#define DCB_BT445        (7 << DCB_ADDR_SHIFT)
#define DCB_VCC1         (8 << DCB_ADDR_SHIFT)
#define DCB_VAB1         (9 << DCB_ADDR_SHIFT)
#define DCB_LG3_BDVERS0      (10 << DCB_ADDR_SHIFT)
#define DCB_LG3_ICS1562      (11 << DCB_ADDR_SHIFT)
#define DCB_RESERVED     (15 << DCB_ADDR_SHIFT)

/*
 * New DCB Addresses which are used in (new) Indigo2 Video and Galileo 1.5
 * since these boards have to work with Mardi Gras also. Yet, these
 * are not necessarily the MGRAS address, these translate to the Mardi Gras
 * addresses when the lower 2 bits are swapped (which will happen on 
 * the Newport to new video board flex cable).
 */
#define DCB_VAB1_NEW     (9 << DCB_ADDR_SHIFT)
/*
 * While the Presenter is currently using address 12 and
 * conflicting with the CC1, it has been changed for Mardi Gras
 * To use the new video boards with Newport (an unreleased product)
 * the presenter probe must be disabled by changing the presenter
 * DCB address in gfx/kern/sys/pcd.h (and possibly
 * lotus/stand/arcs/lib/libsk/graphics/NEWPORT/pcd.h), so 
 * it is probed at address 11.  This will of course not work with
 * the presenter card but it will allow you to test new video
 * boards will Newport
 */

#define DCB_VCC1_NEW     (12 << DCB_ADDR_SHIFT)
/*#define DCB_VCC1_NEW     (8 << DCB_ADDR_SHIFT)*/

/*
 * Addresses being used for Galileo 1.5.
 */
#define DCB_VCC1_GAL     (8 << DCB_ADDR_SHIFT) /* was 12 and will return */
#define DCB_VAB1_GAL     (9 << DCB_ADDR_SHIFT)
#define DCB_TMI_CSC      (13 << DCB_ADDR_SHIFT)
#define DCB_GAL          (14 << DCB_ADDR_SHIFT)

/*
 * LG3 - (Newport for Fullhouse) board defines
 */
/* Version 0 register */
#define LG3_VC2_UNRESET BIT(0)
#define	LG3_GFX_UNRESET	BIT(1)
#define LG3_PLL_UNRESET BIT(2)
#define LG3_DLHD_MASTER BIT(3)

#define LG3_BDVERS_PROTOCOL ((2 << DCB_CSWIDTH_SHIFT) | (1 << DCB_CSHOLD_SHIFT) | (1 << DCB_CSSETUP_SHIFT))

#define lg3BdVersGet(rex3, data)			 		\
        rex3->set.dcbmode = DCB_LG3_BDVERS0 | \
		LG3_BDVERS_PROTOCOL | DCB_DATAWIDTH_1 ;			\
        data = rex3->set.dcbdata0.bybyte.b3

#define lg3BdVersSet(rex3, data)			 		\
        rex3->set.dcbmode = DCB_LG3_BDVERS0 | \
		LG3_BDVERS_PROTOCOL | DCB_DATAWIDTH_1 ;		\
	rex3->set.dcbdata0.bybyte.b3 = (data)

#define Ics1562Set(rex3, data)			 		\
        rex3->set.dcbmode = DCB_LG3_ICS1562 | LG3_BDVERS_PROTOCOL | DCB_DATAWIDTH_1 ; \
	rex3->set.dcbdata0.bybyte.b3 = (data)

#define	LG3_BD_001	0x7
#define	LG3_BD_002	0x0
/*
 * Lsmode register bits
 */
#define LSRCOUNT_SHIFT  0
#define LSRCOUNT_MASK   (0xff << LSRCOUNT_SHIFT)
#define LSREPEAT_SHIFT  8
#define LSREPEAT_MASK   (0xff << LSREPEAT_SHIFT)
#define LSRCNTSAVE_SHIFT        16
#define LSRCNTSAVE_MASK (0xff << LSRCNTSAVE_SHIFT)
#define LSLENGTH_SHIFT  24
#define LSLENGTH_MASK   (0xf << LSLENGTH_SHIFT)

#if	defined ( _KERNEL ) && defined ( REX3_RUNTIME_REV_CHECK )

extern void _newport_poll_status (register struct rex3chip *, register int);

#define REX3WAIT(rex3)  _newport_poll_status (rex3, GFXBUSY)
#define BFIFOWAIT(rex3)  _newport_poll_status (rex3, BACKBUSY)

#else

/* XXX  When we drop support for rex rev b,
 * change status to ustatus in the macros below.
 */
#define REX3WAIT(rex3)  while ((rex3)->p1.set.status & GFXBUSY)
#define BFIFOWAIT(rex3)  while ((rex3)->p1.set.status & BACKBUSY)

#endif

/*
 * Legal GIO bus addresses for Newport graphics boards.
 */
#define REX3_GIO_ADDR_0         0x1f0f0000
#define REX3_GIO_ADDR_1         0x1f4f0000
#define REX3_GIO_ADDR_2         0x1f8f0000
#define REX3_GIO_ADDR_3         0x1fcf0000

#define NG1_XSIZE       1280    /* screen size in x */
#define NG1_YSIZE       1024    /* screen size in y */

/*
 * XXX Correct values TBD.  Depends on video timing
 */
#define CURSOR_XOFF 29
#define CURSOR_YOFF 31

#ifdef _STANDALONE
struct rex3chip;
struct ng1_info;
void Ng1RegisterInit(struct rex3chip *, struct ng1_info *);
extern int ng1checkboard(void);
extern void vc2LoadSRAM(struct rex3chip *, unsigned short *,
		unsigned int , unsigned int);
#endif

#endif /* __SYS_NG1HW_H__ */
