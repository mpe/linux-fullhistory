/* promops.c:  Prom node tree operations and Prom Vector initialization
 *             initialization routines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>

#include <asm/openprom.h>

/* #define DEBUG_PROMOPS */
#define MAX_PR_LEN   64           /* exotic hardware probably overshoots this */

int prom_node_root;               /* initialized in init_prom */

extern struct linux_romvec *romvec;

/* These two functions return and siblings and direct child descendents
 * in the prom device tree respectively.
 */

int
node_get_sibling(int node)
{
  return (*(romvec->pv_nodeops->no_nextnode))(node);
}

int
node_get_child(int node)
{
  return (*(romvec->pv_nodeops->no_child))(node);
}

/* The following routine is used during device probing to determine
 * an integer value property about a (perhaps virtual) device. This
 * could be anything, like the size of the mmu cache lines, etc.
 * the default return value is -1 is the prom has nothing interesting.
 */

unsigned int prom_int_null;

unsigned int *
get_int_from_prom(int node, char *nd_prop, unsigned int *value)
{
  unsigned int pr_len;

  *value = &prom_int_null;    /* duh, I was returning -1 as an unsigned int, prom_panic() */

  pr_len = romvec->pv_nodeops->no_proplen(node, nd_prop);
  if(pr_len > MAX_PR_LEN)
    {
#ifdef DEBUG_PROMOPS
      printk("Bad pr_len in promops -- node: %d nd_prop: %s pr_len: %d",
	     node, nd_prop, (int) pr_len);
#endif
      return value;      /* XXX */
    }

  romvec->pv_nodeops->no_getprop(node, nd_prop, (char *) value);

  return value;
}


/* This routine returns what is termed a property string as opposed
 * to a property integer as above. This can be used to extract the
 * 'type' of device from the prom. An example could be the clock timer
 * chip type. By default you get returned a null string if garbage
 * is returned from the prom.
 */

char *
get_str_from_prom(int node, char *nd_prop, char *value)
{
  unsigned int pr_len;

  *value='\n';

  pr_len = romvec->pv_nodeops->no_proplen(node, nd_prop);
  if(pr_len > MAX_PR_LEN)
    {
#ifdef DEBUG_PROMOPS
      printk("Bad pr_len in promops -- node: %d nd_prop: %s pr_len: %d",
	     node, nd_prop, pr_len);
#endif
      return value;      /* XXX */
    }

  romvec->pv_nodeops->no_getprop(node, nd_prop, value);
  value[pr_len] = 0;

  return value;
}

/* This gets called from head.S upon bootup to initialize the
 * prom vector pointer for the rest of the kernel.
 */

void
init_prom(struct linux_romvec *r_ptr)
{
  romvec = r_ptr;
  prom_node_root = romvec->pv_nodeops->no_nextnode(0);
  prom_int_null = 0;
  
  return;
}
