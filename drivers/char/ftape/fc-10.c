/* Yo, Emacs! we're -*- Linux-C -*-
 *
 
   Copyright (C) 1993,1994 Jon Tombs.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   The entire guts of this program was written by dosemu, modified to
   record reads and writes to the ports in the 0x180-0x188 address space,
   while running the CMS program TAPE.EXE V2.0.5 supplied with the drive.

   Modified to use an array of addresses and generally cleaned up (made
   much shorter) 4 June 94, dosemu isn't that good at writing short code it
   would seem :-). Made independent of 0x180, but I doubt it will work
   at any other address.

   Modified for distribution with ftape source. 21 June 94, SJL.

   Modifications on 20 October 95, by Daniel Cohen (catman@wpi.edu):
   Modified to support different DMA, IRQ, and IO Ports.  Borland's
   Turbo Debugger in virtual 8086 mode (TD386.EXE with hardware breakpoints
   provided by the TDH386.SYS Device Driver) was used on the CMS program
   TAPE V4.0.5.  I set breakpoints on I/O to ports 0x180-0x187.  Note that
   CMS's program will not successfully configure the tape drive if you set
   breakpoints on IO Reads, but you can set them on IO Writes without problems.
   Known problems:
   - You can not use DMA Channels 5 or 7.

   Modification on 29 January 96, by Daniel Cohen (catman@wpi.edu):
   Modified to only accept IRQs 3 - 7, or 9.  Since we can only send a 3 bit
   number representing the IRQ to the card, special handling is required when
   IRQ 9 is selected.  IRQ 2 and 9 are the same, and we should request IRQ 9
   from the kernel while telling the card to use IRQ 2.  Thanks to Greg
   Crider (gcrider@iclnet.org) for finding and locating this bug, as well as
   testing the patch.

 *
 *      This file contains code for the CMS FC-10/FC-20 card.
 */

#include <linux/ftape.h>
#include <asm/io.h>

#include "tracing.h"
#include "fdc-io.h"
#include "fc-10.h"

#ifdef PROBE_FC10

/*  This code will only work if the FC-10 (or FC-20) is set to
 *  use DMA channels 1, 2, or 3.  DMA channels 5 and 7 seem to be 
 *  initialized by the same command as channels 1 and 3, respectively.
 */
#if (FDC_DMA > 3)
#error : The FC-10/20 must be set to use DMA channels 1, 2, or 3!
#endif

/*  Only allow the FC-10/20 to use IRQ 3-7, or 9.  Note that CMS's program
 *  only accepts IRQ's 2-7, but in linux, IRQ 2 is the same as IRQ 9.
 */
#if (FDC_IRQ < 3 || FDC_IRQ == 8 || FDC_IRQ > 9)
#error : The FC-10/20 must be set to use IRQ levels 3 - 7, or 9!
#error :              Note IRQ 9 is the same as IRQ 2
#endif

unsigned short inbs_magic[] = {
	0x3, 0x3, 0x0, 0x4, 0x7, 0x2, 0x5, 0x3, 0x1, 0x4,
	0x3, 0x5, 0x2, 0x0, 0x3, 0x7, 0x4, 0x2,
	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7
};

unsigned short fc10_ports[] = {
	0x180, 0x210, 0x2A0, 0x300, 0x330, 0x340, 0x370
};

int fc10_enable(void)
{
	int i;
	byte cardConfig = 0x00;
	byte x;

	/*  Clear state machine ???
	 */
	for (i = 0; i < NR_ITEMS(inbs_magic); i++) {
		inb(FDC_BASE + inbs_magic[i]);
	}
	outb(0x0, FDC_BASE);

	x = inb(FDC_BASE);
	if (x == 0x13 || x == 0x93) {
		for (i = 1; i < 8; i++) {
			if (inb(FDC_BASE + i) != x) {
				return 0;
			}
		}
	} else {
		return 0;
	}

	outb(0x8, FDC_BASE);

	for (i = 0; i < 8; i++) {
		if (inb(FDC_BASE + i) != 0x0) {
			return 0;
		}
	}
	outb(0x10, FDC_BASE);

	for (i = 0; i < 8; i++) {
		if (inb(FDC_BASE + i) != 0xff) {
			return 0;
		}
	}

	/*  Okay, we found a FC-10 card ! ???
	 */
	outb(0x0, fdc.ccr);

	/*  Clear state machine again ???
	 */
	for (i = 0; i < NR_ITEMS(inbs_magic); i++) {
		inb(FDC_BASE + inbs_magic[i]);
	}
	/* Send io port */
	for (i = 0; i < NR_ITEMS(fc10_ports); i++)
		if (FDC_BASE == fc10_ports[i])
			cardConfig = i + 1;
	if (cardConfig == 0)
		return 0;	/* Invalid I/O Port */
	/* and IRQ - If using IRQ 9, tell the FC card it is actually IRQ 2 */
	if (FDC_IRQ != 9)
		cardConfig |= FDC_IRQ << 3;
	else
		cardConfig |= 2 << 3;

	/* and finally DMA Channel */
	cardConfig |= FDC_DMA << 6;
	outb(cardConfig, FDC_BASE);	/* DMA [2 bits]/IRQ [3 bits]/BASE [3 bits] */

	/*  Enable FC-10 ???
	 */
	outb(0, fdc.ccr);
	outb(0, FDC_BASE + 0x6);
	outb(8, fdc.dor);
	outb(8, fdc.dor);
	outb(1, FDC_BASE + 0x6);

	/*  Initialize fdc, select drive B:
	 */
	outb(0x08, fdc.dor);	/* assert reset, dma & irq enabled */
	outb(0x0c, fdc.dor);	/* release reset */
	outb(0x2d, fdc.dor);	/* select drive 1 */

	return (x == 0x93) ? 2 : 1;
}

#endif /* CMS_FC10_CONTROLLER */
