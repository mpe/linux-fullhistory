/* $Id: fnegs.c,v 1.9 1999/05/28 13:42:06 jj Exp $
 * arch/sparc/math-emu/fnegs.c
 *
 * Copyright (C) 1998 Peter Maydell (pmaydell@chiark.greenend.org.uk)
 *
 */

int FNEGS(unsigned long *rd, unsigned long *rs2)
{
 	/* just change the sign bit */
	rd[0] = rs2[0] ^ 0x80000000UL;
	return 0;
}
