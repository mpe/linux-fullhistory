/* $Id: bootstr.c,v 1.4 1996/02/08 07:06:43 zaitcev Exp $
 * bootstr.c:  Boot string/argument acquisition from the PROM.
 *
 * Copyright(C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/oplib.h>

static char barg_buf[256];

char *
prom_getbootargs(void)
{
	int iter;
	char *cp;

	switch(prom_vers) {
	case PROM_V0:
		cp = barg_buf;
		for(iter = 0; iter < 8; iter++) {
			strcpy(cp, (*(romvec->pv_v0bootargs))->argv[iter]);
			cp += strlen(cp); *cp++=' ';
		}
		*cp = 0;
		break;
	case PROM_V2:
	case PROM_V3:
		cp = barg_buf;
		strcpy(cp, *romvec->pv_v2bootargs.bootpath);
		cp += strlen(cp);
		*cp++ = ' ';
		strcpy(cp, *romvec->pv_v2bootargs.bootargs);
		cp += strlen(cp);
		*cp = 0;
		break;
	default:
		barg_buf[0] = 0;
		break;
	}
	return barg_buf;
}
