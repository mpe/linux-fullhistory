/*
** linux/machw.h -- This header defines some macros and pointers for
**                    the various Macintosh custom hardware registers.
**
** Copyright 1997 by Michael Schmitz
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
*/

#ifndef _ASM_MACHW_H_
#define _ASM_MACHW_H_

/*
 * head.S maps the videomem to VIDEOMEMBASE
 */

#define VIDEOMEMBASE	0xf0000000
#define VIDEOMEMSIZE	(4096*1024)
#define VIDEOMEMMASK	(-4096*1024)

#ifndef __ASSEMBLY__

#include <linux/types.h>

/* Mac SCSI Controller 5380 */

#define	MAC_5380_BAS	(0x50F10000) /* This is definitely wrong!! */
struct MAC_5380 {
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
#define	mac_scsi       ((*(volatile struct MAC_5380 *)MAC_5380_BAS))

/*
** SCC Z8530
*/
 
#define MAC_SCC_BAS (0x50F04000)
struct MAC_SCC
 {
  u_char cha_a_ctrl;
  u_char char_dummy1;
  u_char cha_a_data;
  u_char char_dummy2;
  u_char cha_b_ctrl;
  u_char char_dummy3;
  u_char cha_b_data;
 };
# define mac_scc ((*(volatile struct SCC*)MAC_SCC_BAS))

/*
** VIA 6522
*/

#define VIA1_BAS	(0x50F00000)
#define VIA2_BAS	(0x50F02000)
#define VIA2_BAS_IIci	(0x50F26000)
struct VIA
 {
  u_char buf_b;
  u_char dummy1[0x199];
  u_char buf_a;
  u_char dummy2[0x199];
  u_char dir_b;
  u_char dummy3[0x199];
  u_char dir_a;
  u_char dummy4[0x199];
  u_char timer1_cl;
  u_char dummy5[0x199];
  u_char timer1_ch;
  u_char dummy6[0x199];
  u_char timer1_ll;
  u_char dummy7[0x199];
  u_char timer1_lh;
  u_char dummy8[0x199];
  u_char timer2_cl;
  u_char dummy9[0x199];
  u_char timer2_ch;
  u_char dummy10[0x199];
  u_char sr;
  u_char dummy11[0x199];
  u_char acr;
  u_char dummy12[0x199];
  u_char pcr;
  u_char dummy13[0x199];
  u_char int_fl;
  u_char dummy14[0x199];
  u_char int_en;
  u_char dummy15[0x199];
  u_char anr;
  u_char dummy16[0x199];
 };

# define via_1         ((*(volatile struct VIA *)VIA1_BAS))
# define via_2         ((*(volatile struct VIA *)VIA2_BAS))
# define via1_regp     ((volatile unsigned char *)VIA1_BAS)
 
/*
 * OSS/RBV base address 
 */

#define OSS_BAS		0x50f1a000
#define PSC_BAS		0x50f31000

/* move to oss.h?? */
#define nIFR	0x203
#define oIFR	0x202


/* hardware stuff */

#define MACHW_DECLARE(name)    unsigned name : 1
#define MACHW_SET(name)                (mac_hw_present.name = 1)
#define MACHW_PRESENT(name)    (mac_hw_present.name)

struct {
  /* video hardware */
  /* sound hardware */
  /* disk storage interfaces */
  MACHW_DECLARE(MAC_SCSI_80);     /* Directly mapped NCR5380 */
  MACHW_DECLARE(MAC_SCSI_96);     /* 53c9[46] */
  MACHW_DECLARE(MAC_SCSI_96_2);   /* 2nd 53c9[46] Q900 and Q950 */
  MACHW_DECLARE(IDE);             /* IDE Interface */
  /* other I/O hardware */
  MACHW_DECLARE(SCC);             /* Serial Communications Contr. */
  /* DMA */
  MACHW_DECLARE(SCSI_DMA);        /* DMA for the NCR5380 */
  /* real time clocks */
  MACHW_DECLARE(RTC_CLK);         /* clock chip */
  /* supporting hardware */
  MACHW_DECLARE(VIA1);            /* Versatile Interface Ad. 1 */
  MACHW_DECLARE(VIA2);            /* Versatile Interface Ad. 2 */
  MACHW_DECLARE(RBV);             /* Versatile Interface Ad. 2+ */
  /* NUBUS */
  MACHW_DECLARE(NUBUS);           /* NUBUS */
} mac_hw_present;

/* extern struct mac_hw_present mac_hw_present; */

#endif /* __ASSEMBLY__ */

#endif /* linux/machw.h */
