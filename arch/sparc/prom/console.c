/* console.c: Routines that deal with sending and receiving IO
 *            to/from the current console device using the PROM.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/openprom.h>
#include <asm/oplib.h>

/* Non blocking get character from console input device, returns -1
 * if no input was taken.  This can be used for polling.
 */
int
prom_nbgetchar(void)
{
	static char inc;

	switch(prom_vers) {
	case PROM_V0:
		return (*(romvec->pv_nbgetchar))();
		break;
	case PROM_V2:
	case PROM_V3:
	case PROM_P1275:
		if( (*(romvec->pv_v2devops).v2_dev_read)(*romvec->pv_v2bootargs.fd_stdin , &inc, 0x1) == 1)
			return inc;
		return -1;
		break;
	};
	return 0; /* Ugh, we could spin forever on unsupported proms ;( */
}

/* Non blocking put character to console device, returns -1 if
 * unsuccessful.
 */
int
prom_nbputchar(char c)
{
	static char outc;

	switch(prom_vers) {
	case PROM_V0:
		return (*(romvec->pv_nbputchar))(c);
		break;
	case PROM_V2:
	case PROM_V3:
	case PROM_P1275:
		outc = c;
		if( (*(romvec->pv_v2devops).v2_dev_write)(*romvec->pv_v2bootargs.fd_stdout, &outc, 0x1) == 1)
			return 0;
		return -1;
		break;
	};
	return 0; /* Ugh, we could spin forever on unsupported proms ;( */
}

/* Blocking version of get character routine above. */
char
prom_getchar(void)
{
	int character;
	while((character = prom_nbgetchar()) == -1) ;
	return (char) character;
}

/* Blocking version of put character routine above. */
void
prom_putchar(char c)
{
	while(prom_nbputchar(c) == -1) ;
	return;
}

