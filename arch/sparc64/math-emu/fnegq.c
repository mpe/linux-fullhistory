/* $Id: fnegq.c,v 1.6 1999/05/28 13:44:21 jj Exp $
 * arch/sparc64/math-emu/fnegq.c
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@ultra.linux.cz)
 *
 */

int FNEGQ(unsigned long *rd, unsigned long *rs2)
{
	rd[0] = rs2[0] ^ 0x8000000000000000UL;
	rd[1] = rs2[1];
	return 0;
}
