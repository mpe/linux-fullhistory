/* $Id: bare.h,v 1.2 1995/11/25 00:57:41 davem Exp $
 * bare.h:  Defines for the low level entry code of the BOOT program.
 *          We include in the head.h stuff that the real kernel uses
 *          and this saves a lot of repetition here.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>
#include <asm/psr.h>
#include <asm/cprefix.h>

#define     SANE_PIL  (0xd00)    /* No interrupts except clock and unmaskable NMI's */
#define     SANE_PSR  (SANE_PIL|PSR_S|PSR_ET)

#define     BOOTBLOCK_NENTRIES   0x40      /* Number of entries in the boot block */
#define     BOOTBLOCK_ENTSIZE    0x04      /* Size in bytes of each boot block entry */

