/* $Id: idprom.c,v 1.19 1996/04/25 06:08:41 davem Exp $
 * idprom.c: Routines to load the idprom into kernel addresses and
 *           interpret the data contained within.
 *
 * Because they use the IDPROM's machine type field, some of the
 * virtual address cache probings on the sun4c are done here.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/oplib.h>
#include <asm/idprom.h>
#include <asm/machines.h>  /* Fun with Sun released architectures. */
#include <asm/system.h>    /* For halt() macro */

struct idp_struct *idprom;
static struct idp_struct idprom_buff;

/* Here is the master table of Sun machines which use some implementation
 * of the Sparc CPU and have a meaningful IDPROM machtype value that we
 * know about.  See asm-sparc/machines.h for empirical constants.
 */
struct Sun_Machine_Models Sun_Machines[NUM_SUN_MACHINES] = {
/* First, Sun4's */
{ "Sun 4/100 Series", (SM_SUN4 | SM_4_110) },
{ "Sun 4/200 Series", (SM_SUN4 | SM_4_260) },
{ "Sun 4/300 Series", (SM_SUN4 | SM_4_330) },
{ "Sun 4/400 Series", (SM_SUN4 | SM_4_470) },
/* Now, Sun4c's */
{ "Sun4c SparcStation 1", (SM_SUN4C | SM_4C_SS1) },
{ "Sun4c SparcStation IPC", (SM_SUN4C | SM_4C_IPC) },
{ "Sun4c SparcStation 1+", (SM_SUN4C | SM_4C_SS1PLUS) },
{ "Sun4c SparcStation SLC", (SM_SUN4C | SM_4C_SLC) },
{ "Sun4c SparcStation 2", (SM_SUN4C | SM_4C_SS2) },
{ "Sun4c SparcStation ELC", (SM_SUN4C | SM_4C_ELC) },
{ "Sun4c SparcStation IPX", (SM_SUN4C | SM_4C_IPX) },
/* Finally, early Sun4m's */
{ "Sun4m SparcSystem600", (SM_SUN4M | SM_4M_SS60) },
{ "Sun4m SparcStation10", (SM_SUN4M | SM_4M_SS50) },
{ "Sun4m SparcStation5", (SM_SUN4M | SM_4M_SS40) },
/* One entry for the OBP arch's which are sun4d, sun4e, and newer sun4m's */
{ "Sun4M OBP based system", (SM_SUN4M_OBP | 0x0) } };

void
sparc_display_systype(unsigned char machtyp)
{
	char system_name[128];
	int i;

	for(i = 0; i<NUM_SUN_MACHINES; i++) {
		if(Sun_Machines[i].id_machtype == machtyp) {
			if(machtyp!=(SM_SUN4M_OBP | 0x0)) {
				printk("TYPE: %s\n", Sun_Machines[i].name);
				break;
			} else {
				prom_getproperty(prom_root_node, "banner-name",
						 system_name, sizeof(system_name));
				printk("TYPE: %s\n", system_name);
				break;
			}
		}
	}
	if(i == NUM_SUN_MACHINES)
		printk("Uh oh, IDPROM had bogus id_machtype value <%x>\n", machtyp);
	return;
}

void
get_idprom(void)
{
	prom_getidp((char *) &idprom_buff, sizeof(idprom_buff));

	idprom = &idprom_buff;

	sparc_display_systype(idprom->id_machtype);

	printk("Ethernet address: %x:%x:%x:%x:%x:%x\n",
		    idprom->id_eaddr[0], idprom->id_eaddr[1], idprom->id_eaddr[2],
		    idprom->id_eaddr[3], idprom->id_eaddr[4], idprom->id_eaddr[5]);

	return;
}

/* find_vac_size() returns the number of bytes in the VAC (virtual
 * address cache) on this machine.
 */

int
find_vac_size(void)
{
	int vac_prop_len;
	int vacsize = 0;

	vac_prop_len = prom_getproplen(prom_root_node, "vac-size");
	if(vac_prop_len != -1) {
		vacsize = prom_getint(prom_root_node, "vac-size");
		return vacsize;
	} else {
		switch(idprom->id_machtype) {
		case (SM_SUN4C | SM_4C_SS1):     /* SparcStation1 */
		case (SM_SUN4C | SM_4C_IPC):     /* SparcStation IPX */
		case (SM_SUN4C | SM_4C_SS1PLUS): /* SparcStation1+ */
		case (SM_SUN4C | SM_4C_SLC):     /* SparcStation SLC */
		case (SM_SUN4C | SM_4C_SS2):     /* SparcStation2 Cache-Chip BUG! */
		case (SM_SUN4C | SM_4C_ELC):     /* SparcStation ELC */
		case (SM_SUN4C | SM_4C_IPX):     /* SparcStation IPX */
			return 65536;
		default:
			printk("find_vac_size: Can't determine size of VAC, bailing out...\n");
			halt();
			break;
		};
	};
	return -1;
}

/* find_vac_linesize() returns the size in bytes of the VAC linesize */ 

int
find_vac_linesize(void)
{
	int vac_prop_len;

	vac_prop_len = prom_getproplen(prom_root_node, "vac-linesize");

	if(vac_prop_len != -1)
		return prom_getint(prom_root_node, "vac-linesize");
	else {
		switch(idprom->id_machtype) {
		case (SM_SUN4C | SM_4C_SS1):     /* SparcStation1 */
		case (SM_SUN4C | SM_4C_IPC):     /* SparcStation IPC */
		case (SM_SUN4C | SM_4C_SS1PLUS): /* SparcStation1+ */
		case (SM_SUN4C | SM_4C_SLC):     /* SparcStation SLC */
			return 16;
		case (SM_SUN4C | SM_4C_SS2):     /* SparcStation2 Cache-Chip BUG! */
		case (SM_SUN4C | SM_4C_ELC):     /* SparcStation ELC */
		case (SM_SUN4C | SM_4C_IPX):     /* SparcStation IPX */
			return 32;
		default:
			printk("find_vac_linesize: Can't determine VAC linesize, bailing out...\n");
			halt();
			break;
		};
	};
	return -1;
}

int
find_vac_hwflushes(void)
{
	register int len;
	int tmp1, tmp2;

	/* Sun 4/75 has typo in prom_node, it's a dash instead of an underscore
	 * in the property name. :-(
	 */
	len = prom_getproperty(prom_root_node, "vac_hwflush",
			       (char *) &tmp1, sizeof(int));
	if(len != 4) tmp1=0;

	len = prom_getproperty(prom_root_node, "vac-hwflush",
			       (char *) &tmp2, sizeof(int));
	if(len != 4) tmp2=0;

	return (tmp1|tmp2);
}

