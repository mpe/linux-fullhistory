/* $Id: tree.c,v 1.8 1996/04/04 16:31:09 tridge Exp $
 * tree.c: Basic device tree traversal/scanning for the Linux
 *         prom library.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/string.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

static char promlib_buf[128];

/* Return the child of node 'node' or zero if no this node has no
 * direct descendent.
 */
int
prom_getchild(int node)
{
	int cnode;

#if CONFIG_AP1000
        printk("prom_getchild -> 0\n");
        return 0;
#endif
	if(node == -1) return 0;
	cnode = prom_nodeops->no_child(node);
	if((cnode == 0) || (cnode == -1)) return 0;
	return cnode;
}

/* Return the next sibling of node 'node' or zero if no more siblings
 * at this level of depth in the tree.
 */
int
prom_getsibling(int node)
{
	int sibnode;

#if CONFIG_AP1000
        printk("prom_getsibling -> 0\n");
        return 0;
#endif
	if(node == -1) return 0;
	sibnode = prom_nodeops->no_nextnode(node);
	if((sibnode == 0) || (sibnode == -1)) return 0;
	return sibnode;
}

/* Return the length in bytes of property 'prop' at node 'node'.
 * Return -1 on error.
 */
int
prom_getproplen(int node, char *prop)
{
#if CONFIG_AP1000
        printk("prom_getproplen(%s) -> -1\n",prop);
        return -1;
#endif
	if((!node) || (!prop)) return -1;
	return prom_nodeops->no_proplen(node, prop);
}

/* Acquire a property 'prop' at node 'node' and place it in
 * 'buffer' which has a size of 'bufsize'.  If the acquisition
 * was successful the length will be returned, else -1 is returned.
 */
int
prom_getproperty(int node, char *prop, char *buffer, int bufsize)
{
	int plen;

#if CONFIG_AP1000
        printk("prom_getproperty(%s) -> -1\n",prop);
        return -1;
#endif
	plen = prom_getproplen(node, prop);
	if((plen > bufsize) || (plen == 0) || (plen == -1)) return -1;

	/* Ok, things seem all right. */
	return prom_nodeops->no_getprop(node, prop, buffer);
}

/* Acquire an integer property and return it's value.  Returns -1
 * on failure.
 */
int
prom_getint(int node, char *prop)
{
	static int intprop;

#if CONFIG_AP1000
        printk("prom_getint(%s) -> -1\n",prop);
        return -1;
#endif
	if(prom_getproperty(node, prop, (char *) &intprop, sizeof(int)) != -1)
		return intprop;

	return -1;
}

/* Acquire an integer property, upon error return the passed default
 * integer.
 */

int
prom_getintdefault(int node, char *property, int deflt)
{
	int retval;

#if CONFIG_AP1000
        printk("prom_getintdefault(%s) -> 0\n",property);
        return 0;
#endif
	retval = prom_getint(node, property);
	if(retval == -1) return deflt;

	return retval;
}

/* Acquire a boolean property, 1=TRUE 0=FALSE. */
int
prom_getbool(int node, char *prop)
{
	int retval;

#if CONFIG_AP1000
        printk("prom_getbool(%s) -> 0\n",prop);
        return 0;
#endif
	retval = prom_getproplen(node, prop);
	if(retval == -1) return 0;
	return 1;
}

/* Acquire a property whose value is a string, returns a null
 * string on error.  The char pointer is the user supplied string
 * buffer.
 */
void
prom_getstring(int node, char *prop, char *user_buf, int ubuf_size)
{
	int len;

#if CONFIG_AP1000
        printk("prom_getstring(%s) -> .\n",prop);
        return;
#endif
	len = prom_getproperty(node, prop, user_buf, ubuf_size);
	if(len != -1) return;
	user_buf[0] = 0;
	return;
}


/* Does the device at node 'node' have name 'name'?
 * YES = 1   NO = 0
 */
int
prom_nodematch(int node, char *name)
{
	static char namebuf[128];
	prom_getproperty(node, "name", namebuf, sizeof(namebuf));
	if(strcmp(namebuf, name) == 0) return 1;
	return 0;
}

/* Search siblings at 'node_start' for a node with name
 * 'nodename'.  Return node if successful, zero if not.
 */
int
prom_searchsiblings(int node_start, char *nodename)
{
	int thisnode, error;

	for(thisnode = node_start; thisnode;
	    thisnode=prom_getsibling(thisnode)) {
		error = prom_getproperty(thisnode, "name", promlib_buf,
					 sizeof(promlib_buf));
		/* Should this ever happen? */
		if(error == -1) continue;
		if(strcmp(nodename, promlib_buf)==0) return thisnode;
	}

	return 0;
}

/* Return the first property type for node 'node'.
 */
char *
prom_firstprop(int node)
{
	if(node == -1) return "";
	return prom_nodeops->no_nextprop(node, (char *) 0x0);
}

/* Return the property type string after property type 'oprop'
 * at node 'node' .  Returns NULL string if no more
 * property types for this node.
 */
char *
prom_nextprop(int node, char *oprop)
{
	if(node == -1) return "";
	return prom_nodeops->no_nextprop(node, oprop);
}

int
prom_node_has_property(int node, char *prop)
{
	char *current_property = "";

	do {
		current_property = prom_nextprop(node, current_property);
		if(!strcmp(current_property, prop))
		   return 1;
	} while (*current_property);
	return 0;
}

/* Set property 'pname' at node 'node' to value 'value' which has a length
 * of 'size' bytes.  Return the number of bytes the prom accepted.
 */
int
prom_setprop(int node, char *pname, char *value, int size)
{
	if(size == 0) return 0;
	if((pname == 0) || (value == 0)) return 0;
	return prom_nodeops->no_setprop(node, pname, value, size);
}
