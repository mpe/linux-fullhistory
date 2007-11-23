/* $Id: console.c,v 1.8 1996/04/05 07:44:35 tridge Exp $
 * console.c: Routines that deal with sending and receiving IO
 *            to/from the current console device using the PROM.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <linux/string.h>

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
	case PROM_AP1000:
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
	case PROM_AP1000:
#if CONFIG_AP1000
		{
		  extern void ap_putchar(char );
		  ap_putchar(c);
		  return 0;
		}
#endif
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

/* Query for input device type */
enum prom_input_device
prom_query_input_device()
{
	int st_p;
	char propb[64];
	char *p;

	switch(prom_vers) {
	case PROM_V0:
	case PROM_V2:
	default:
		switch(*romvec->pv_stdin) {
		case PROMDEV_KBD:	return PROMDEV_IKBD;
		case PROMDEV_TTYA:	return PROMDEV_ITTYA;
		case PROMDEV_TTYB:	return PROMDEV_ITTYB;
		default:
			return PROMDEV_I_UNK;
		};
	case PROM_V3:
	case PROM_P1275:
		st_p = (*romvec->pv_v2devops.v2_inst2pkg)(*romvec->pv_v2bootargs.fd_stdin);
		if(prom_node_has_property(st_p, "keyboard"))
			return PROMDEV_IKBD;
		prom_getproperty(st_p, "device_type", propb, sizeof(propb));
		if(strncmp(propb, "serial", sizeof("serial")))
			return PROMDEV_I_UNK;
		prom_getproperty(prom_root_node, "stdin-path", propb, sizeof(propb));
		p = propb;
		while(*p) p++; p -= 2;
		if(p[0] == ':') {
			if(p[1] == 'a')
				return PROMDEV_ITTYA;
			else if(p[1] == 'b')
				return PROMDEV_ITTYB;
		}
		return PROMDEV_I_UNK;
	case PROM_AP1000:
		return PROMDEV_I_UNK;
	};
}

/* Query for output device type */

enum prom_output_device
prom_query_output_device()
{
	int st_p;
	char propb[64];
	char *p;
	int propl;

	switch(prom_vers) {
	case PROM_V0:
		switch(*romvec->pv_stdin) {
		case PROMDEV_SCREEN:	return PROMDEV_OSCREEN;
		case PROMDEV_TTYA:	return PROMDEV_OTTYA;
		case PROMDEV_TTYB:	return PROMDEV_OTTYB;
		};
		break;
	case PROM_V2:
	case PROM_V3:
	case PROM_P1275:
		st_p = (*romvec->pv_v2devops.v2_inst2pkg)(*romvec->pv_v2bootargs.fd_stdout);
		propl = prom_getproperty(st_p, "device_type", propb, sizeof(propb));
		if (propl >= 0 && propl == sizeof("display") &&
			strncmp("display", propb, sizeof("display")) == 0)
		{
			return PROMDEV_OSCREEN;
		}
		if(prom_vers == PROM_V3) {
			if(strncmp("serial", propb, sizeof("serial")))
				return PROMDEV_O_UNK;
			prom_getproperty(prom_root_node, "stdout-path", propb, sizeof(propb));
			p = propb;
			while(*p) p++; p -= 2;
			if(p[0]==':') {
				if(p[1] == 'a')
					return PROMDEV_OTTYA;
				else if(p[1] == 'b')
					return PROMDEV_OTTYB;
			}
			return PROMDEV_O_UNK;
		} else {
			/* This works on SS-2 (an early OpenFirmware) still. */
			switch(*romvec->pv_stdin) {
			case PROMDEV_TTYA:	return PROMDEV_OTTYA;
			case PROMDEV_TTYB:	return PROMDEV_OTTYB;
			};
		}
		break;
	case PROM_AP1000:
		return PROMDEV_I_UNK;
	};
	return PROMDEV_O_UNK;
}
