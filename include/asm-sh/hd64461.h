#ifndef __ASM_SH_HD64461
#define __ASM_SH_HD64461
/*
 *	$Id: hd64461.h,v 1.1 2000/06/10 21:45:48 yaegashi Exp $
 *	Copyright (C) 2000 YAEGASHI Takeshi
 *	Hitachi HD64461 companion chip support
 */
#include <linux/config.h>

#define HD64461_CPTWAR	0x1030	
#define HD64461_CPTWDR	0x1032
#define HD64461_CPTRAR	0x1034	
#define HD64461_CPTRDR	0x1036

#define HD64461_PCC0ISR         0x2000
#define HD64461_PCC0GCR         0x2002
#define HD64461_PCC0CSCR        0x2004
#define HD64461_PCC0CSCIER      0x2006
#define HD64461_PCC0SCR         0x2008
#define HD64461_PCC1ISR         0x2010
#define HD64461_PCC1GCR         0x2012
#define HD64461_PCC1CSCR        0x2014
#define HD64461_PCC1CSCIER      0x2016
#define HD64461_PCC1SCR         0x2018
#define HD64461_P0OCR           0x202a
#define HD64461_P1OCR           0x202c
#define HD64461_PGCR            0x202e

#define HD64461_NIRR		0x5000
#define HD64461_NIMR		0x5002

#ifndef CONFIG_HD64461_IOBASE
#define CONFIG_HD64461_IOBASE	0xb0000000
#endif

#define HD64461_IRQBASE	64

#endif
