/*
** linux/atarihw.h -- This header defines some macros and pointers for
**                    the various Atari custom hardware registers.
**
** Copyright 1994 by Bj”rn Brauel
**
** 5/1/94 Roman Hodek:
**   Added definitions for TT specific chips.
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
*/

#ifndef _LINUX_ATARIHW_H_
#define _LINUX_ATARIHW_H_

#include <linux/types.h>

/* Reading the MFP port register gives a machine independent delay, since the
 * MFP always has a 8 MHz clock. This avoids problems with the varying length
 * of nops on various machines. Somebody claimed that the tstb takes 600 ns.
 */
#define	MFPDELAY() \
	__asm__ __volatile__ ( "tstb %0" : : "m" (mfp.par_dt_reg) : "cc" );

/* Memory used for screen ram and stdma buffers */
void atari_stram_init (void);
void *atari_stram_alloc (long size, unsigned long *start_mem );
void atari_stram_free (void *);

extern int is_medusa;

/* Do cache push/invalidate for DMA read/write. This function obeys the
 * snooping on some machines (Medusa) and processors: The Medusa itself can
 * snoop, but only the '040 can source data from its cache to DMA writes i.e.,
 * reads from memory). Both '040 and '060 invalidate cache entries on snooped
 * DMA reads (i.e., writes to memory).
 */

#include <linux/mm.h>
#include <asm/pgtable.h>

static inline void dma_cache_maintenance( unsigned long paddr,
					  unsigned long len,
					  int writeflag )

{
	if (writeflag) {
		if (!is_medusa || m68k_is040or060 == 6)
			cache_push( paddr, len );
	}
	else {
		if (!is_medusa)
			cache_clear( paddr, len );
	}
}


/* 
** Shifter  
 */
#define ST_LOW  0
#define ST_MID  1
#define ST_HIGH 2
#define TT_LOW  7
#define TT_MID  4
#define TT_HIGH 6

#define SHF_BAS (0xffff8200)    
struct SHIFTER 
 {
 	u_char pad1;
	u_char bas_hi;
	u_char pad2;
	u_char bas_md;
	u_char pad3;
	u_char volatile vcounthi;
 	u_char pad4;
 	u_char volatile vcountmid;
 	u_char pad5;
 	u_char volatile vcountlow;
 	u_char volatile syncmode;
 	u_char pad6;
 	u_char pad7; 
	u_char bas_lo;
 };
# define shifter ((*(volatile struct SHIFTER *)SHF_BAS))

#define SHF_FBAS (0xffff820e)
struct SHIFTER_F030
 {
  u_short off_next;
  u_short scn_width;
 };                             
# define shifter_f030 ((*(volatile struct SHIFTER_F030 *)SHF_FBAS))


#define	SHF_TBAS (0xffff8200)
struct SHIFTER_TT {
	u_char	char_dummy0;
	u_char	bas_hi;			/* video mem base addr, high and mid byte */
	u_char	char_dummy1;
	u_char	bas_md;
	u_char	char_dummy2;
	u_char	vcount_hi;		/* pointer to currently displayed byte */
	u_char	char_dummy3;
	u_char	vcount_md;
	u_char	char_dummy4;
	u_char	vcount_lo;
	u_short	st_sync;		/* ST compatible sync mode register, unused */
	u_char	char_dummy5;
	u_char	bas_lo;			/* video mem addr, low byte */
	u_char	char_dummy6[2+3*16];
	/* $ffff8240: */
	u_short	color_reg[16];	/* 16 color registers */
	u_char	st_shiftmode;	/* ST compatible shift mode register, unused */
	u_char  char_dummy7;
	u_short tt_shiftmode;	/* TT shift mode register */


};
#define	shifter_tt	((*(volatile struct SHIFTER_TT *)SHF_TBAS))

/* values for shifter_tt->tt_shiftmode */
#define	TT_SHIFTER_STLOW		0x0000
#define	TT_SHIFTER_STMID		0x0100
#define	TT_SHIFTER_STHIGH		0x0200
#define	TT_SHIFTER_TTLOW		0x0700
#define	TT_SHIFTER_TTMID		0x0400
#define	TT_SHIFTER_TTHIGH		0x0600
#define	TT_SHIFTER_MODEMASK	0x0700
#define TT_SHIFTER_NUMMODE	0x0008
#define	TT_SHIFTER_PALETTE_MASK	0x000f
#define	TT_SHIFTER_GRAYMODE		0x1000

/* 256 TT palette registers */
#define	TT_PALETTE_BASE	(0xffff8400)
#define	tt_palette	((volatile u_short *)TT_PALETTE_BASE)

#define	TT_PALETTE_RED_MASK		0x0f00
#define	TT_PALETTE_GREEN_MASK	0x00f0
#define	TT_PALETTE_BLUE_MASK	0x000f

/*
** Falcon030 VIDEL Video Controller
** for description see File 'linux\tools\atari\hardware.txt
 */
#define f030_col ((u_long *)		0xffff9800)
#define f030_xreg ((u_short*)		0xffff8282)
#define f030_yreg ((u_short*)		0xffff82a2)
#define f030_creg ((u_short*)		0xffff82c0)
#define f030_sreg ((u_short*)		0xffff8260)
#define f030_mreg ((u_short*)		0xffff820a)
#define f030_linewidth ((u_short*)      0xffff820e)
#define f030_hscroll ((u_char*)		0xffff8265)

#define VIDEL_BAS (0xffff8260)
struct VIDEL {
	u_short st_shift;
	u_short pad1;
	u_char  xoffset_s;
	u_char  xoffset;
	u_short f_shift;
	u_char  pad2[0x1a];
	u_short hht;
	u_short hbb;
	u_short hbe;
	u_short hdb;
	u_short hde;
	u_short hss;
	u_char  pad3[0x14];
	u_short vft;
	u_short vbb;
	u_short vbe;
	u_short vdb;
	u_short vde;
	u_short vss;
	u_char  pad4[0x12];
	u_short control;
	u_short mode;
};
#define	videl	((*(volatile struct VIDEL *)VIDEL_BAS))

/*
** DMA/WD1772 Disk Controller
 */                          
 
#define FWD_BAS (0xffff8604)  
struct DMA_WD
 {
  u_short fdc_acces_seccount;
  u_short dma_mode_status;
  u_char dma_vhi;	/* Some extended ST-DMAs can handle 32 bit addresses */
  u_char dma_hi;
  u_char char_dummy2;
  u_char dma_md;
  u_char char_dummy3;
  u_char dma_lo;
  u_short fdc_speed;
 };
# define dma_wd ((*(volatile struct DMA_WD *)FWD_BAS))
/* alias */
#define	st_dma dma_wd
/* The two highest bytes of an extended DMA as a short; this is a must
 * for the Medusa.
 */
#define st_dma_ext_dmahi (*((volatile unsigned short *)0xffff8608))

/*
** YM2149 Sound Chip
** access in bytes
 */

#define YM_BAS (0xffff8800)
struct SOUND_YM
 {
  u_char rd_data_reg_sel;
  u_char char_dummy1;
  u_char wd_data;
 };
#define sound_ym ((*(volatile struct SOUND_YM *)YM_BAS))

/* TT SCSI DMA */

#define	TT_SCSI_DMA_BAS	(0xffff8700)
struct TT_DMA {
	u_char	char_dummy0;
	u_char	dma_addr_hi;
	u_char	char_dummy1;
	u_char	dma_addr_hmd;
	u_char	char_dummy2;
	u_char	dma_addr_lmd;
	u_char	char_dummy3;
	u_char	dma_addr_lo;
	u_char	char_dummy4;
	u_char	dma_cnt_hi;
	u_char	char_dummy5;
	u_char	dma_cnt_hmd;
	u_char	char_dummy6;
	u_char	dma_cnt_lmd;
	u_char	char_dummy7;
	u_char	dma_cnt_lo;
	u_long	dma_restdata;
	u_short	dma_ctrl;
};
#define	tt_scsi_dma	((*(volatile struct TT_DMA *)TT_SCSI_DMA_BAS))

/* TT SCSI Controller 5380 */

#define	TT_5380_BAS	(0xffff8781)
struct TT_5380 {
	u_char	scsi_data;
	u_char	char_dummy1;
	u_char	scsi_icr;
	u_char	char_dummy2;
	u_char	scsi_mode;
	u_char	char_dummy3;
	u_char	scsi_tcr;
	u_char	char_dummy4;
	u_char	scsi_idstat;
	u_char	char_dummy5;
	u_char	scsi_dmastat;
	u_char	char_dummy6;
	u_char	scsi_targrcv;
	u_char	char_dummy7;
	u_char	scsi_inircv;
};
#define	tt_scsi			((*(volatile struct TT_5380 *)TT_5380_BAS))
#define	tt_scsi_regp	((volatile char *)TT_5380_BAS)


/* 
** Falcon DMA Sound Subsystem
** not implemented yet
 */     

/*
** Falcon Blitter
*/

#define BLT_BAS (0xffff8a00)

struct BLITTER
 {
  u_short halftone[16];
  u_short src_x_inc;
  u_short src_y_inc;
  u_long src_address;
  u_short endmask1;  
  u_short endmask2;  
  u_short endmask3;
  u_short dst_x_inc;
  u_short dst_y_inc;
  u_long dst_address;
  u_short wd_per_line;
  u_short ln_per_bb;
  u_short hlf_op_reg;
  u_short log_op_reg;
  u_short lin_nm_reg;
  u_short skew_reg;  
 };
# define blitter ((*(volatile struct BLITTER *)BLT_BAS))


/*
** SCC Z8530
 */
 
#define SCC_BAS (0xffff8c81)
struct SCC
 {
  u_char cha_a_ctrl;
  u_char char_dummy1;
  u_char cha_a_data;
  u_char char_dummy2;
  u_char cha_b_ctrl;
  u_char char_dummy3;
  u_char cha_b_data;
 };
# define scc ((*(volatile struct SCC*)SCC_BAS))

/* The ESCC (Z85230) in an Atari ST. The channels are revered! */
# define st_escc ((*(volatile struct SCC*)0xfffffa31))
# define st_escc_dsr ((*(volatile char *)0xfffffa39))

/* TT SCC DMA Controller (same chip as SCSI DMA) */

#define	TT_SCC_DMA_BAS	(0xffff8c01)
#define	tt_scc_dma	((*(volatile struct TT_DMA *)TT_SCC_DMA_BAS))

/*
** VIDEL Palette Register 
 */

#define FPL_BAS (0xffff9800)
struct VIDEL_PALETTE
 {
  u_long reg[256];
 };
# define videl_palette ((*(volatile struct VIDEL_PALETTE*)FPL_BAS))


/*
** Falcon DSP Host Interface
** not implemented yet
 */
 
/*
** MFP 68901
 */
 
#define MFP_BAS (0xfffffa01)
struct MFP
 {
  u_char par_dt_reg;
  u_char char_dummy1;
  u_char active_edge;
  u_char char_dummy2;
  u_char data_dir;
  u_char char_dummy3;
  u_char int_en_a;
  u_char char_dummy4;
  u_char int_en_b;
  u_char char_dummy5;
  u_char int_pn_a;
  u_char char_dummy6;
  u_char int_pn_b;
  u_char char_dummy7;
  u_char int_sv_a;
  u_char char_dummy8;
  u_char int_sv_b;
  u_char char_dummy9;
  u_char int_mk_a;
  u_char char_dummy10;
  u_char int_mk_b;
  u_char char_dummy11;
  u_char vec_adr;
  u_char char_dummy12;
  u_char tim_ct_a;
  u_char char_dummy13;
  u_char tim_ct_b;
  u_char char_dummy14;
  u_char tim_ct_cd;
  u_char char_dummy15;
  u_char tim_dt_a;
  u_char char_dummy16;
  u_char tim_dt_b;
  u_char char_dummy17;
  u_char tim_dt_c;
  u_char char_dummy18;
  u_char tim_dt_d;
  u_char char_dummy19;
  u_char sync_char;
  u_char char_dummy20;
  u_char usart_ctr;
  u_char char_dummy21;
  u_char rcv_stat;
  u_char char_dummy22;
  u_char trn_stat;
  u_char char_dummy23;
  u_char usart_dta;
 };
# define mfp ((*(volatile struct MFP*)MFP_BAS))

/* TT's second MFP */

#define	TT_MFP_BAS	(0xfffffa81)
# define tt_mfp ((*(volatile struct MFP*)TT_MFP_BAS))


/* TT System Control Unit */

#define	TT_SCU_BAS	(0xffff8e01)
struct TT_SCU {
	u_char	sys_mask;
	u_char	char_dummy1;
	u_char	sys_stat;
	u_char	char_dummy2;
	u_char	softint;
	u_char	char_dummy3;
	u_char	vmeint;
	u_char	char_dummy4;
	u_char	gp_reg1;
	u_char	char_dummy5;
	u_char	gp_reg2;
	u_char	char_dummy6;
	u_char	vme_mask;
	u_char	char_dummy7;
	u_char	vme_stat;
};
#define	tt_scu	((*(volatile struct TT_SCU *)TT_SCU_BAS))

/* TT real time clock */

#define	TT_RTC_BAS	(0xffff8961)
struct TT_RTC {
	u_char	regsel;
	u_char	dummy;
	u_char	data;
};
#define	tt_rtc	((*(volatile struct TT_RTC *)TT_RTC_BAS))


/*
** ACIA 6850
 */
/* constants for the ACIA registers */

/* baudrate selection and reset (Baudrate = clock/factor) */
#define ACIA_DIV1  0
#define ACIA_DIV16 1
#define ACIA_DIV64 2
#define ACIA_RESET 3

/* character format */
#define ACIA_D7E2S (0<<2)	/* 7 data, even parity, 2 stop */
#define ACIA_D7O2S (1<<2)	/* 7 data, odd parity, 2 stop */
#define ACIA_D7E1S (2<<2)	/* 7 data, even parity, 1 stop */
#define ACIA_D7O1S (3<<2)	/* 7 data, odd parity, 1 stop */
#define ACIA_D8N2S (4<<2)	/* 8 data, no parity, 2 stop */
#define ACIA_D8N1S (5<<2)	/* 8 data, no parity, 1 stop */
#define ACIA_D8E1S (6<<2)	/* 8 data, even parity, 1 stop */
#define ACIA_D8O1S (7<<2)	/* 8 data, odd parity, 1 stop */

/* transmit control */
#define ACIA_RLTID (0<<5)	/* RTS low, TxINT disabled */
#define ACIA_RLTIE (1<<5)	/* RTS low, TxINT enabled */
#define ACIA_RHTID (2<<5)	/* RTS high, TxINT disabled */
#define ACIA_RLTIDSB (3<<5)	/* RTS low, TxINT disabled, send break */

/* receive control */
#define ACIA_RID (0<<7)		/* RxINT disabled */
#define ACIA_RIE (1<<7)		/* RxINT enabled */

/* status fields of the ACIA */
#define ACIA_RDRF 1		/* Receive Data Register Full */
#define ACIA_TDRE (1<<1)	/* Transmit Data Register Empty */
#define ACIA_DCD  (1<<2)	/* Data Carrier Detect */
#define ACIA_CTS  (1<<3)	/* Clear To Send */
#define ACIA_FE   (1<<4)	/* Framing Error */
#define ACIA_OVRN (1<<5)	/* Receiver Overrun */
#define ACIA_PE   (1<<6)	/* Parity Error */
#define ACIA_IRQ  (1<<7)	/* Interrupt Request */

#define ACIA_BAS (0xfffffc00)
struct ACIA 
 {
  u_char key_ctrl;
  u_char char_dummy1;
  u_char key_data;
  u_char char_dummy2;
  u_char mid_ctrl;
  u_char char_dummy3;
  u_char mid_data;
 };
# define acia ((*(volatile struct ACIA*)ACIA_BAS))

#define	TT_DMASND_BAS (0xffff8900)
struct TT_DMASND {
	u_char	int_ctrl;	/* Falcon: Interrupt control */
	u_char	ctrl;
	u_char	pad2;
	u_char	bas_hi;
	u_char	pad3;
	u_char	bas_mid;
	u_char	pad4;
	u_char	bas_low;
	u_char	pad5;
	u_char	addr_hi;
	u_char	pad6;
	u_char	addr_mid;
	u_char	pad7;
	u_char	addr_low;
	u_char	pad8;
	u_char	end_hi;
	u_char	pad9;
	u_char	end_mid;
	u_char	pad10;
	u_char	end_low;
	u_char	pad11[12];
	u_char	track_select;	/* Falcon */
	u_char	mode;
 	u_char	pad12[14];
 	/* Falcon only: */
 	u_short	cbar_src;
 	u_short cbar_dst;
 	u_char	ext_div;
 	u_char	int_div;
 	u_char	rec_track_select;
 	u_char	dac_src;
 	u_char	adc_src;
 	u_char	input_gain;
 	u_short	output_atten;
};
# define tt_dmasnd ((*(volatile struct TT_DMASND *)TT_DMASND_BAS))

#define	DMASND_CTRL_OFF		0x00
#define	DMASND_CTRL_ON		0x01
#define	DMASND_CTRL_REPEAT	0x02
#define	DMASND_MODE_MONO	0x80
#define	DMASND_MODE_STEREO	0x00
#define DMASND_MODE_8BIT	0x00
#define DMASND_MODE_16BIT	0x40	/* Falcon only */
#define	DMASND_MODE_6KHZ	0x00	/* Falcon: mute */
#define	DMASND_MODE_12KHZ	0x01
#define	DMASND_MODE_25KHZ	0x02
#define	DMASND_MODE_50KHZ	0x03

#define DMASNDSetBase(bufstart)						\
    do {								\
	tt_dmasnd.bas_hi  = (unsigned char)(((bufstart) & 0xff0000) >> 16); \
	tt_dmasnd.bas_mid = (unsigned char)(((bufstart) & 0x00ff00) >> 8); \
	tt_dmasnd.bas_low = (unsigned char) ((bufstart) & 0x0000ff); \
    } while( 0 )

#define DMASNDGetAdr() ((tt_dmasnd.addr_hi << 16) +	\
			(tt_dmasnd.addr_mid << 8) +	\
			(tt_dmasnd.addr_low))

#define DMASNDSetEnd(bufend)				\
    do {						\
	tt_dmasnd.end_hi  = (unsigned char)(((bufend) & 0xff0000) >> 16); \
	tt_dmasnd.end_mid = (unsigned char)(((bufend) & 0x00ff00) >> 8); \
	tt_dmasnd.end_low = (unsigned char) ((bufend) & 0x0000ff); \
    } while( 0 )


#define	TT_MICROWIRE_BAS	(0xffff8922)
struct TT_MICROWIRE {
	u_short	data;
	u_short	mask;
};
# define tt_microwire ((*(volatile struct TT_MICROWIRE *)TT_MICROWIRE_BAS))

#define	MW_LM1992_ADDR		0x0400

#define	MW_LM1992_VOLUME(dB)	\
    (0x0c0 | ((dB) < -80 ? 0 : (dB) > 0 ? 40 : (((dB) + 80) / 2)))
#define	MW_LM1992_BALLEFT(dB)	\
    (0x140 | ((dB) < -40 ? 0 : (dB) > 0 ? 20 : (((dB) + 40) / 2)))
#define	MW_LM1992_BALRIGHT(dB)	\
    (0x100 | ((dB) < -40 ? 0 : (dB) > 0 ? 20 : (((dB) + 40) / 2)))
#define	MW_LM1992_TREBLE(dB)	\
    (0x080 | ((dB) < -12 ? 0 : (dB) > 12 ? 12 : (((dB) / 2) + 6)))
#define	MW_LM1992_BASS(dB)	\
    (0x040 | ((dB) < -12 ? 0 : (dB) > 12 ? 12 : (((dB) / 2) + 6)))

#define	MW_LM1992_PSG_LOW	0x000
#define	MW_LM1992_PSG_HIGH	0x001
#define	MW_LM1992_PSG_OFF	0x002

#define MSTE_RTC_BAS	(0xfffffc21)

struct MSTE_RTC {
	u_char sec_ones;
	u_char dummy1;
	u_char sec_tens;
	u_char dummy2;
	u_char min_ones;
	u_char dummy3;
	u_char min_tens;
	u_char dummy4;
	u_char hr_ones;
	u_char dummy5;
	u_char hr_tens;
	u_char dummy6;
	u_char weekday;
	u_char dummy7;
	u_char day_ones;
	u_char dummy8;
	u_char day_tens;
	u_char dummy9;
	u_char mon_ones;
	u_char dummy10;
	u_char mon_tens;
	u_char dummy11;
	u_char year_ones;
	u_char dummy12;
	u_char year_tens;
	u_char dummy13;
	u_char mode;
	u_char dummy14;
	u_char test;
	u_char dummy15;
	u_char reset;
};

#define mste_rtc ((*(volatile struct MSTE_RTC *)MSTE_RTC_BAS))

#endif /* linux/atarihw.h */

