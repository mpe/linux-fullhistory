/* idprom.c: Routines to load the idprom into kernel addresses and
 *           interpret the data contained within.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>

#include <asm/types.h>
#include <asm/openprom.h>
#include <asm/idprom.h>

struct idp_struct idprom;
extern int num_segmaps, num_contexts;

void get_idprom(void)
{
  char* idp_addr;
  char* knl_idp_addr;
  int i;

  idp_addr = (char *)IDPROM_ADDR;
  knl_idp_addr = (char *) &idprom;

  for(i = 0; i<IDPROM_SIZE; i++)
      *knl_idp_addr++ = *idp_addr++;

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
  int node_root;

  node_root = (*(romvec->pv_nodeops->no_nextnode))(0);

  vac_prop_len = (*(romvec->pv_nodeops->no_proplen))(node_root, "vac-size");

  if(vac_prop_len != -1)
    {
      (*(romvec->pv_nodeops->no_getprop))(node_root, "vac-size", (char *) &vacsize);
      return vacsize;
    }
  else
    {

  /* The prom node functions can't help, do it via idprom struct */
      switch(idprom.id_machtype)
	{
	case 0x51:
	case 0x52:
	case 0x53:
	case 0x54:
	case 0x55:
	case 0x56:
	case 0x57:
	  return 65536;
	default:
	  return -1;
	}
    };
}

/* find_vac_linesize() returns the size in bytes of the VAC linesize */ 

int
find_vac_linesize(void)
{
  int vac_prop_len;
  int vaclinesize = 0;
  int node_root;

  node_root = (*(romvec->pv_nodeops->no_nextnode))(0);

  vac_prop_len = (*(romvec->pv_nodeops->no_proplen))(node_root, "vac-linesize");

  if(vac_prop_len != -1)
    {
      (*(romvec->pv_nodeops->no_getprop))(node_root, "vac-linesize",
				      (char *) &vaclinesize);
      return vaclinesize;
    }
  else
    {

  /* The prom node functions can't help, do it via idprom struct */
      switch(idprom.id_machtype)
	{
	case 0x51:
	case 0x52:
	case 0x53:
	case 0x54:
	  return 16;
	case 0x55:
	case 0x56:
	case 0x57:
	  return 32;
	default:
	  return -1;
	}
    };
}

int
find_vac_hwflushes(void)
{
  register int len, node_root;
  int tmp1, tmp2;

  node_root = (*(romvec->pv_nodeops->no_nextnode))(0);

  len = (*(romvec->pv_nodeops->no_proplen))(node_root, "vac_hwflush");

#ifdef DEBUG_IDPROM
  printf("DEBUG: find_vac_hwflushes: proplen vac_hwflush=0x%x\n", len);
#endif

  /* Sun 4/75 has typo in prom_node, it's a dash instead of an underscore
   * in the property name. :-(
   */
  len |= (*(romvec->pv_nodeops->no_proplen))(node_root, "vac-hwflush");

#ifdef DEBUG_IDPROM
  printf("DEBUG: find_vac_hwflushes: proplen vac-hwflush=0x%x\n", len);
#endif

  len = (*(romvec->pv_nodeops->no_getprop))(node_root,"vac_hwflush", 
					    (char *) &tmp1);
  if(len != 4) tmp1=0;

  len = (*(romvec->pv_nodeops->no_getprop))(node_root, "vac-hwflush",
					    (char *) &tmp2);
  if(len != 4) tmp2=0;


  return (tmp1|tmp2);
}

void
find_mmu_num_segmaps(void)
{
  register int root_node, len;

  root_node = (*(romvec->pv_nodeops->no_nextnode))(0);

  len = (*(romvec->pv_nodeops->no_getprop))(root_node, "mmu-npmg", 
					    (char *) &num_segmaps);

#ifdef DEBUG_MMU
  printf("find_mmu_num_segmaps: property length = %d\n", len);
#endif

  if(len != 4) num_segmaps = 128;

  return;
}

void
find_mmu_num_contexts(void)
{
  register int root_node, len;

  root_node = (*(romvec->pv_nodeops->no_nextnode))(0);

  len = (*(romvec->pv_nodeops->no_getprop))(root_node, "mmu-nctx", 
					    (char *) &num_contexts);

#ifdef DEBUG_MMU
  printf("find_mmu_num_contexts: property length = %d\n", len);
#endif

  if(len != 4) num_contexts = 8;

  return;
}

