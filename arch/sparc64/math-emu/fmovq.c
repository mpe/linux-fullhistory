/* $Id: fmovq.c,v 1.2 1999/05/28 13:44:09 jj Exp $
 * arch/sparc64/math-emu/fmovq.c
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@ultra.linux.cz)
 *
 */

int FMOVQ(unsigned long *rd, unsigned long *rs2)
{
	rd[0] = rs2[0];
	rd[1] = rs2[1];
	return 0;
}
