
/* Defines for NAND flash devices           */
/* (c) 1999 Machine Vision Holdings, Inc.   */
/* Author: David Woodhouse <dwmw2@mvhi.com> */
/* $Id: nand.h,v 1.2 1999/08/17 22:57:08 dwmw2 Exp $ */

#ifndef __MTD_NAND_H__
#define __MTD_NAND_H__

#define NAND_CMD_READ0 0
#define NAND_CMD_READ1 1
#define NAND_CMD_PAGEPROG 0x10
#define NAND_CMD_READOOB 0x50
#define NAND_CMD_ERASE1 0x60
#define NAND_CMD_STATUS 0x70
#define NAND_CMD_SEQIN 0x80
#define NAND_CMD_READID 0x90
#define NAND_CMD_ERASE2 0xd0
#define NAND_CMD_RESET 0xff

#define NAND_MFR_TOSHIBA 0x98
#define NAND_MFR_SAMSUNG 0xec


#endif /* __MTD_NAND_H__ */






