/*
 * init.c: PROM library initialisation code.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: init.c,v 1.2 1999/02/25 21:22:49 tsbogend Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/config.h>

#include <asm/sgialib.h>

/* #define DEBUG_PROM_INIT */

/* Master romvec interface. */
struct linux_romvec *romvec;
struct linux_promblock *sgi_pblock;
int prom_argc;
char **prom_argv, **prom_envp;
unsigned short prom_vers, prom_rev;

extern void prom_testtree(void);

__initfunc(int prom_init(int argc, char **argv, char **envp))
{
	struct linux_promblock *pb;

	romvec = ROMVECTOR;
	pb = sgi_pblock = PROMBLOCK;
	prom_argc = argc;
	prom_argv = argv;
	prom_envp = envp;

	if(pb->magic != 0x53435241) {
		prom_printf("Aieee, bad prom vector magic %08lx\n", pb->magic);
		while(1)
			;
	}

	prom_init_cmdline();

	prom_vers = pb->ver;
	prom_rev = pb->rev;
	prom_identify_arch();
#ifdef CONFIG_SGI
	printk("PROMLIB: SGI ARCS firmware Version %d Revision %d\n",
		    prom_vers, prom_rev);
#else
	printk("PROMLIB: ARC firmware Version %d Revision %d\n",
		    prom_vers, prom_rev);
#endif
	prom_meminit();

#if 0
	prom_testtree();
#endif

#ifdef DEBUG_PROM_INIT
	{
		prom_printf("Press a key to reboot\n");
		(void) prom_getchar();
		romvec->imode();
	}
#endif
	return 0;
}
