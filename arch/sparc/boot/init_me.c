/* $Id: init_me.c,v 1.3 1996/04/21 10:30:09 davem Exp $
 * init_me.c:  Initialize empirical constants and gather some info from
 *             the boot prom.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/openprom.h>  /* For property declarations and the prom structs */
#include <asm/oplib.h>
#include <asm/vac-ops.h>

#include "empirical.h"   /* Don't ask... */

#define DEBUG_INIT_ME    /* Tell me what's going on */

unsigned int nwindows;   /* Set in bare.S */
unsigned int nwindowsm1;
unsigned int pac_or_vac; /* 0 means "dunno" 1 means "VAC" 2 means "PAC" */
unsigned int pvac_size;  /* Use the same two variables for a PAC and VAC */
unsigned int pvac_linesize;
unsigned int pac_size;
int num_segmaps;
int num_contexts;
unsigned int BOGOMIPS;        /* bogosity without the VAC cache on */
unsigned int BOGOMIPS_WCACHE; /* bogosity with the VAC cache */
unsigned int delay_factor;

extern int prom_node_root;
void (*printk)(const char *str, ...);

void init_me(void)
{
	unsigned int grrr;

	printk = romvec->pv_printf;
	prom_node_root = prom_nextnode(0);
	prom_getprop(prom_node_root, "mmu-npmg", &num_segmaps,
		     sizeof(unsigned int));

	pvac_size = prom_getint_default(prom_node_root, "vac-size", 65536);

	pvac_linesize = prom_getint_default(prom_node_root, "vac-linesize", 16);
	
	grrr = prom_getint_default(prom_node_root, "mips-on", 0);
	if(!grrr) {
		grrr = prom_getint_default(prom_node_root, "clock-frequency", 0);
		if(grrr > 15000000 && grrr < 100000000) {
			BOGOMIPS = 3;
			BOGOMIPS_WCACHE = grrr / 1000000;
		} else {
			BOGOMIPS = DEF_BOGO;
			BOGOMIPS_WCACHE = DEF_BOGO;
		}
	} else (BOGOMIPS_WCACHE = grrr, 
		BOGOMIPS = prom_getint(prom_node_root, "mips-off"));

#ifdef DEBUG_INIT_ME
	(*(romvec->pv_printf))("\nBOGOMIPS        %d\n", (int) BOGOMIPS);
	(*(romvec->pv_printf))("BOGOMIPS_WCACHE %d\n", (int) BOGOMIPS_WCACHE);
	(*(romvec->pv_printf))("pvac_size        %d\n", (int) pvac_size);
	(*(romvec->pv_printf))("pvac_linesize    %d\n", (int) pvac_linesize);
	(*(romvec->pv_printf))("num_segmaps     %d\n", (int) num_segmaps);
#endif

	delay_factor = (BOGOMIPS > 3) ? ((BOGOMIPS - 2) >> 1) : 11;

	(*(romvec->pv_printf))("\nLILO: \n");
	return;
}
