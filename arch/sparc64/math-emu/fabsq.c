/* $Id: fabsq.c,v 1.5 1999/05/28 13:42:27 jj Exp $
 * arch/sparc64/math-emu/fabsq.c
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@ultra.linux.cz)
 *
 */

int FABSQ(unsigned long *rd, unsigned long *rs2)
{
	rd[0] = rs2[0] & 0x7fffffffffffffffUL;
	rd[1] = rs2[1];
	return 0;
}
