  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * fake a really simple Sun prom for the AP+
 *
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/oplib.h>
#include <asm/idprom.h> 
#include <asm/machines.h> 
#include <asm/ap1000/apservice.h> 

static struct linux_romvec ap_romvec;
static struct idprom ap_idprom;

struct property {
	char *name;
	char *value;
	int length;
};

struct node {
	int level;
	struct property *properties;
};

struct property null_properties = { NULL, NULL, -1 };

struct property root_properties[] = {
	{"device_type", "cpu", 4},
	{"idprom",      (char *)&ap_idprom, sizeof(ap_idprom)},
	{"banner-name", "Fujitsu AP1000+", 16},
	{NULL, NULL, -1}
};

struct node nodes[] = {
	{ 0, &null_properties }, 
	{ 0, root_properties },
	{ -1,&null_properties }
};


static int no_nextnode(int node)
{
	if (nodes[node].level == nodes[node+1].level)
		return node+1;
	return -1;
}

static int no_child(int node)
{
	if (nodes[node].level == nodes[node+1].level-1)
		return node+1;
	return -1;
}

static struct property *find_property(int node,char *name)
{
	struct property *prop = &nodes[node].properties[0];
	while (prop && prop->name) {
		if (strcmp(prop->name,name) == 0) return prop;
		prop++;
	}
	return NULL;
}

static int no_proplen(int node,char *name)
{
	struct property *prop = find_property(node,name);
	if (prop) return prop->length;
	return -1;
}

static int no_getprop(int node,char *name,char *value)
{
	struct property *prop = find_property(node,name);
	if (prop) {
		memcpy(value,prop->value,prop->length);
		return 1;
	}
	return -1;
}

static int no_setprop(int node,char *name,char *value,int len)
{
	return -1;
}

static char *no_nextprop(int node,char *name)
{
	struct property *prop = find_property(node,name);
	if (prop) return prop[1].name;
	return NULL;
}


static struct linux_nodeops ap_nodeops = {
	no_nextnode,
	no_child,
	no_proplen,
	no_getprop,
	no_setprop,
	no_nextprop
};


	
static unsigned char calc_idprom_cksum(struct idprom *idprom)
{
        unsigned char cksum, i, *ptr = (unsigned char *)idprom;

        for (i = cksum = 0; i <= 0x0E; i++)
                cksum ^= *ptr++;

        return cksum;
}

static int synch_hook;


struct linux_romvec *ap_prom_init(void)
{
	memset(&ap_romvec,0,sizeof(ap_romvec));

	ap_romvec.pv_romvers = 42;
	ap_romvec.pv_nodeops = &ap_nodeops;
	ap_romvec.pv_reboot = ap_reboot;
	ap_romvec.pv_synchook = &synch_hook;

	ap_idprom.id_format = 1;
	ap_idprom.id_sernum = mpp_cid();
	ap_idprom.id_machtype = SM_SUN4M_OBP;
	ap_idprom.id_cksum = calc_idprom_cksum(&ap_idprom);

	return &ap_romvec;
}





