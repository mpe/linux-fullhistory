/* init.c:  Initialize internal variables used by the PROM
 *          library functions.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/openprom.h>
#include <asm/oplib.h>

struct linux_romvec *romvec;
enum prom_major_version prom_vers;
unsigned int prom_rev, prom_prev;

/* The root node of the prom device tree. */
int prom_root_node;

/* Pointer to the device tree operations structure. */
struct linux_nodeops *prom_nodeops;

/* You must call prom_init() before you attempt to use any of the
 * routines in the prom library.  It returns 0 on success, 1 on
 * failure.  It gets passed the pointer to the PROM vector.
 */

extern void prom_meminit(void);
extern void prom_ranges_init(void);

int
prom_init(struct linux_romvec *rp)
{
	if(!rp) return 1;
	romvec = rp;
	if(romvec->pv_magic_cookie != LINUX_OPPROM_MAGIC)
		return 1;

	/* Ok, we seem to have a sane romvec here. */
	switch(romvec->pv_romvers) {
	case 0:
		prom_vers = PROM_V0;
		break;
	case 2:
		prom_vers = PROM_V2;
		break;
	case 3:
		prom_vers = PROM_V3;
		break;
	case 4:
		prom_vers = PROM_P1275;
		prom_printf("PROMLIB: Sun IEEE Prom not supported yet\n");
		return 1;
		break;
	default:
		prom_printf("PROMLIB: Bad PROM version %d\n",
			    romvec->pv_romvers);
		return 1;
		break;
	};

	prom_rev = romvec->pv_plugin_revision;
	prom_prev = romvec->pv_printrev;
	prom_nodeops = romvec->pv_nodeops;

	prom_root_node = prom_getsibling(0);
	if((prom_root_node == 0) || (prom_root_node == -1))
		return 1;

	if((((unsigned long) prom_nodeops) == 0) || 
	   (((unsigned long) prom_nodeops) == -1))
		return 1;

	prom_meminit();
	prom_ranges_init();

	prom_printf("PROMLIB: Sun Boot Prom Version %d Revision %d\n",
		    romvec->pv_romvers, prom_rev);

	/* Initialization successful. */
	return 0;
}
