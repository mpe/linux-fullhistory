/* $Id: fabss.c,v 1.8 1999/05/28 13:41:33 jj Exp $
 * arch/sparc/math-emu/fabss.c
 *
 * Copyright (C) 1998 Peter Maydell (pmaydell@chiark.greenend.org.uk)
 *
 */

int FABSS(unsigned long *rd, unsigned long *rs2)
{
	rd[0] = rs2[0] & 0x7fffffffUL;
	return 0;
}
