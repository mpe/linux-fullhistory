/* $Id: fmovs.c,v 1.7 1999/05/28 13:42:05 jj Exp $
 * arch/sparc/math-emu/fmovs.c
 *
 * Copyright (C) 1998 Peter Maydell (pmaydell@chiark.greenend.org.uk)
 *
 */

int FMOVS(unsigned long *rd, unsigned long *rs2)
{
	rd[0] = rs2[0];
	return 0;
}
