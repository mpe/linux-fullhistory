/* $Id: isar.h,v 1.2 1998/11/15 23:54:54 keil Exp $
 * isar.h   ISAR (Siemens PSB 7110) specific defines
 *
 * Author Karsten Keil (keil@isdn4linux.de)
 *
 *
 * $Log: isar.h,v $
 * Revision 1.2  1998/11/15 23:54:54  keil
 * changes from 2.0
 *
 * Revision 1.1  1998/08/13 23:33:48  keil
 * First version, only init
 *
 *
 */
 
#define ISAR_IRQMSK	0x04
#define ISAR_IRQSTA	0x04
#define ISAR_IRQBIT	0x75
#define ISAR_CTRL_H	0x61
#define ISAR_CTRL_L	0x60
#define ISAR_IIS	0x58
#define ISAR_IIA	0x58
#define ISAR_HIS	0x50
#define ISAR_HIA	0x50
#define ISAR_MBOX	0x4c
#define ISAR_WADR	0x4a
#define ISAR_RADR	0x48 

#define ISAR_HIS_VNR	0x14
#define ISAR_HIS_DKEY	0x02
#define ISAR_HIS_FIRM	0x1e
#define ISAR_HIS_STDSP  0x08
#define ISAR_HIS_DIAG	0x05
#define ISAR_HIS_P0CFG	0x3c
#define ISAR_HIS_P12CFG	0x24
#define ISAR_HIS_SARTCFG	0x25	
#define ISAR_HIS_PUMPCFG	0x26	
#define ISAR_HIS_IOM2CFG	0x27
#define ISAR_HIS_IOM2REQ	0x07
#define ISAR_HIS_BSTREQ	0x0c
#define ISAR_HIS_PSTREQ	0x0e
#define ISAR_HIS_SDATA	0x20
#define ISAR_HIS_DPS1	0x40
#define ISAR_HIS_DPS2	0x80
#define SET_DPS(x)	((x<<6) & 0xc0)

#define ISAR_IIS_MSCMSD 0x3f
#define ISAR_IIS_VNR	0x15
#define ISAR_IIS_DKEY	0x03
#define ISAR_IIS_FIRM	0x1f
#define ISAR_IIS_STDSP  0x09
#define ISAR_IIS_DIAG	0x25
#define ISAR_IIS_GSTEV	0x0
#define ISAR_IIS_BSTEV	0x28
#define ISAR_IIS_BSTRSP	0x2c
#define ISAR_IIS_PSTRSP	0x2e
#define ISAR_IIS_PSTEV	0x2a
#define ISAR_IIS_IOM2RSP	0x27

#define ISAR_IIS_RDATA	0x20
#define ISAR_CTRL_SWVER	0x10
#define ISAR_CTRL_STST	0x40

#define ISAR_MSG_HWVER	{0x20, 0, 1}

#define ISAR_DP1_USE	1
#define ISAR_DP2_USE	2

#define PMOD_BYPASS	7

#define SMODE_DISABLE	0
#define SMODE_HDLC	3
#define SMODE_BINARY	4

#define HDLC_FED	0x40
#define HDLC_FSD	0x20
#define HDLC_FST	0x20
#define HDLC_ERROR	0x1c

#define BSTAT_RDM0	0x1
#define BSTAT_RDM1	0x2
#define BSTAT_RDM2	0x4
#define BSTAT_RDM3	0x8


extern int ISARVersion(struct IsdnCardState *cs, char *s);
extern int isar_load_firmware(struct IsdnCardState *cs, u_char *buf);
extern void isar_int_main(struct IsdnCardState *cs);
extern void initisar(struct IsdnCardState *cs);
extern void isar_fill_fifo(struct BCState *bcs);
