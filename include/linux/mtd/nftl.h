
/* Defines for NAND Flash Translation Layer  */
/* (c) 1999 Machine Vision Holdings, Inc.    */
/* Author: David Woodhouse <dwmw2@mvhi.com>  */
/* $Id: nftl.h,v 1.6 2000/03/31 15:12:20 dwmw2 Exp $ */

#ifndef __MTD_NFTL_H__
#define __MTD_NFTL_H__

#include <linux/mtd/mtd.h>

/* Block Control Information */

struct nftl_bci {
	unsigned char ECCSig[6];
	__u16 Status;
}__attribute__((packed));

/* Unit Control Information */

struct nftl_uci0 {
	__u16 VirtUnitNum;
	__u16 ReplUnitNum;
	__u16 SpareVirtUnitNum;
	__u16 SpareReplUnitNum;
} __attribute__((packed));

struct nftl_uci1 {
	__u32 WearInfo;
	__u16 EraseMark;
	__u16 EraseMark1;
} __attribute__((packed));

struct nftl_uci2 {
	__u32 WriteInh;
	__u32 unused;
} __attribute__((packed));

union nftl_uci {
	struct nftl_uci0 a;
	struct nftl_uci1 b;
	struct nftl_uci2 c;
};

struct nftl_oob {
	struct nftl_bci b;
	union nftl_uci u;
};

/* NFTL Media Header */

struct NFTLMediaHeader {
	char DataOrgID[6];
	__u16 NumEraseUnits;
	__u16 FirstPhysicalEUN;
	__u32 FormattedSize;
	unsigned char UnitSizeFactor;
} __attribute__((packed));

#define MAX_ERASE_ZONES (8192 - 512)

#define ERASE_MARK 0x3c69
#define BLOCK_FREE 0xffff
#define BLOCK_USED 0x5555
#define BLOCK_IGNORE 0x1111
#define BLOCK_DELETED 0x0000

#define ZONE_GOOD 0xff
#define ZONE_BAD_ORIGINAL 0
#define ZONE_BAD_MARKED 7

#ifdef __KERNEL__


struct NFTLrecord {
	struct mtd_info *mtd;
	struct semaphore mutex;
	__u16 MediaUnit, SpareMediaUnit;
	__u32 EraseSize;
	struct NFTLMediaHeader MediaHdr;
	int usecount;
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	__u16 numvunits;
	__u16 lastEUN;
	__u16 numfreeEUNs;
	__u16 LastFreeEUN; /* To speed up finding a free EUN */
	__u32 long nr_sects;
	int head,sect,cyl;
	__u16 *EUNtable; /* [numvunits]: First EUN for each virtual unit  */
	__u16 *VirtualUnitTable; /* [numEUNs]: VirtualUnitNumber for each */
	__u16 *ReplUnitTable; /* [numEUNs]: ReplUnitNumber for each */
};

#define NFTL_MAJOR 93
#define MAX_NFTLS 16

#endif /* __KERNEL__ */

#endif /* __MTD_NFTL_H__ */
