/* $Id: tree.c,v 1.1 1996/12/27 08:49:13 jj Exp $
 * tree.c: Basic device tree traversal/scanning for the Linux
 *         prom library.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

/* Return the child of node 'node' or zero if no this node has no
 * direct descendent.
 */
__inline__ int
prom_getchild(int node)
{
	long cnode;

	if(node == -1) return 0;
	cnode = (*prom_command)("child", P1275_INOUT(1, 1), node);
	if(cnode == -1) return 0;
	return (int)cnode;
}

/* Return the next sibling of node 'node' or zero if no more siblings
 * at this level of depth in the tree.
 */
__inline__ int
prom_getsibling(int node)
{
	long sibnode;

	if(node == -1) return 0;
	sibnode = (*prom_command)("peer", P1275_INOUT(1, 1), node);
	if(cnode == -1) return 0;
	return (int)sibnode;
}

/* Return the length in bytes of property 'prop' at node 'node'.
 * Return -1 on error.
 */
__inline__ int
prom_getproplen(int node, char *prop)
{
	if((!node) || (!prop)) return -1;
	return (*prom_command)("getproplen", 
			       P1275_ARG(1,P1275_ARG_IN_STRING)|
			       P1275_INOUT(2, 1), 
			       node, prop);
}

/* Acquire a property 'prop' at node 'node' and place it in
 * 'buffer' which has a size of 'bufsize'.  If the acquisition
 * was successful the length will be returned, else -1 is returned.
 */
__inline__ int
prom_getproperty(int node, char *prop, char *buffer, int bufsize)
{
	int plen;

	plen = prom_getproplen(node, prop);
	if((plen > bufsize) || (plen == 0) || (plen == -1))
		return -1;
	else {
		/* Ok, things seem all right. */
		return (*prom_command)("getprop", 
				       P1275_ARG(1,P1275_ARG_IN_STRING)|
				       P1275_ARG(2,P1275_ARG_OUT_BUF)|
				       P1275_INOUT(4, 1), 
				       node, prop, buffer, P1275_SIZE(plen));
	}
}

/* Acquire an integer property and return its value.  Returns -1
 * on failure.
 */
__inline__ int
prom_getint(int node, char *prop)
{
	int intprop;

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

	retval = prom_getint(node, property);
	if(retval == -1) return deflt;

	return retval;
}

/* Acquire a boolean property, 1=TRUE 0=FALSE. */
int
prom_getbool(int node, char *prop)
{
	int retval;

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
	char namebuf[128];
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
	char promlib_buf[128];

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

/* Gets name in the {name@x,yyyyy|name (if no reg)} form */
int 
prom_getname (int node, char *buffer, int len)
{
	int i;
	struct linux_prom_registers reg[PROMREG_MAX];
	
	i = prom_getproperty (node, "name", buffer, len);
	if (i <= 0) return -1;
	buffer [i] = 0;
	len -= i;
	i = prom_getproperty (node, "reg", (char *)reg, sizeof (reg));
	if (i <= 0) return 0;
	if (len < 11) return -1;
	buffer = strchr (buffer, 0);
	sprintf (buffer, "@%x,%x", reg[0].which_io, (uint)reg[0].phys_addr);
	return 0;
}

/* Return the first property type for node 'node'.
 * buffer should be at least 32B in length
 */
__inline__ char *
prom_firstprop(int node, char *buffer)
{
	if(node == -1) return "";
	*buffer = 0;
	(*prom_command)("nextprop", P1275_ARG(2,P1275_ARG_OUT_32B)|
				    P1275_INOUT(3, 0), 
				    node, (char *) 0x0, buffer);
	return buffer;
}

/* Return the property type string after property type 'oprop'
 * at node 'node' .  Returns NULL string if no more
 * property types for this node.
 */
__inline__ char *
prom_nextprop(int node, char *oprop, char *buffer)
{
	char buf[32];

	if(node == -1) return "";
	*buffer = 0;
	(*prom_command)("nextprop", P1275_ARG(1,P1275_ARG_IN_STRING)|
				    P1275_ARG(2,P1275_ARG_OUT_32B)|
				    P1275_INOUT(3, 0), 
				    node, oprop, (oprop == buffer) ? 
				    buf : buffer);
	if (oprop == buffer) strcpy (buffer, buf);
	return buffer;
}

int
prom_finddevice(char *name)
{
	if(!name) return 0;
	*buffer = 0;
	(*prom_command)("finddevice", P1275_ARG(0,P1275_ARG_IN_STRING)|
				      P1275_INOUT(1, 1), 
				      name);
	return buffer;
}

int
prom_node_has_property(int node, char *prop)
{
	char buf[32];
	char *current_property = buf;
	
	*buf = 0;
	do {
		current_property = prom_nextprop(node, current_property, buf);
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
	unsigned long flags;
	int ret;

	if(size == 0) return 0;
	if((pname == 0) || (value == 0)) return 0;
	
	return (*prom_command)("setprop", P1275_ARG(1,P1275_ARG_IN_STRING)|
					  P1275_ARG(2,P1275_ARG_OUT_BUF)|
					  P1275_INOUT(4, 1), 
					  node, pname, value, P1275_SIZE(size));
}

__inline__ int
prom_inst2pkg(int inst)
{
	int node;
	
	node = (*prom_command)("instance-to-package", P1275_INOUT(1, 1), inst);
	if (node == -1) return 0;
	return node;
}

/* Return 'node' assigned to a particular prom 'path'
 * FIXME: Should work for v0 as well
 */
int
prom_pathtoinode(char *path)
{
	int node, inst;
	
	inst = prom_devopen (path);
	if (inst == -1) return 0;
	node = prom_inst2pkg (inst);
	prom_devclose (inst);
	if (node == -1) return 0;
	return node;
}
