/*
 *		NET_ALIAS network device aliasing module.
 *
 *
 * Version:	@(#)net_alias.c	0.43   12/20/95
 *
 * Authors:	Juan Jose Ciarlante, <jjciarla@raiz.uncu.edu.ar>
 *		Marcelo Fabian Roccasalva, <mfroccas@raiz.uncu.edu.ar>
 *
 * Features:
 *	-	AF_ independent: net_alias_type objects
 *	-	AF_INET optimized
 *	-	ACTUAL alias devices inserted in dev chain
 *	-	fast hashed alias address lookup
 *	-	net_alias_type objs registration/unreg., module-ables.
 *	-	/proc/net/aliases & /proc/net/alias_types entries
 * Fixes:
 *			JJC	:	several net_alias_type func. renamed.
 *			JJC	:	net_alias_type object methods now pass 
 *					*this.
 *			JJC	:	xxx_rcv device selection based on <src,dst> 
 *					addrs
 *		Andreas Schultz	:	Kerneld support.
 *
 * FIXME:
 *	- User calls sleep/wake_up locking.
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/if.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

#ifdef ALIAS_USER_LAND_DEBUG
#include "net_alias.h"
#include "user_stubs.h"
#endif

#include <linux/net_alias.h>

#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif

/*
 * Only allow the following flags to pass from main device to aliases
 */

#define  NET_ALIAS_IFF_MASK   (IFF_UP|IFF_BROADCAST|IFF_RUNNING|IFF_NOARP|IFF_LOOPBACK|IFF_POINTOPOINT)

static struct net_alias_type * nat_getbytype(int type);
static int nat_attach_chg(struct net_alias_type *nat, int delta);
static int nat_bind(struct net_alias_type *nat,struct net_alias *alias, struct sockaddr *sa);
static int nat_unbind(struct net_alias_type *nat, struct net_alias *alias);


static int net_alias_devinit(struct device *dev);
static int net_alias_hard_start_xmit(struct sk_buff *skb, struct device *dev);
static int net_alias_devsetup(struct net_alias *alias, struct net_alias_type *nat, struct sockaddr *sa);
static struct net_alias **net_alias_slow_findp(struct net_alias_info *alias_info, struct net_alias *alias);
static struct device *net_alias_dev_create(struct device *main_dev, int slot, int *err, struct sockaddr *sa, void *data);
static struct device *net_alias_dev_delete(struct device *main_dev, int slot, int *err);
static void net_alias_free(struct device *dev);
					   
/*
 * net_alias_type base array, will hold net_alias_type obj hashed list heads.
 */

struct net_alias_type *nat_base[16];


/*
 * get net_alias_type ptr by type
 */

static __inline__ struct net_alias_type *
nat_getbytype(int type)
{
  struct net_alias_type *nat;
  for(nat = nat_base[type & 0x0f]; nat ; nat = nat->next)
  {
    if (nat->type == type) return nat;
  }
  return NULL;
}


/*
 * get addr32 representation (pre-hashing) of address.
 * if NULL nat->get_addr32, assume sockaddr_in struct (IP-ish).
 */

static __inline__ __u32
nat_addr32(struct net_alias_type *nat, struct sockaddr *sa)
{
  if (nat->get_addr32)
    return nat->get_addr32(nat, sa);
  else
    return (*(struct sockaddr_in *)sa).sin_addr.s_addr;
}


/*
 * hashing code for alias_info->hash_tab entries
 * 4 bytes -> 1/2 byte using xor complemented by af
 */

static __inline__ unsigned
HASH(__u32 addr, int af)
{
  unsigned tmp = addr ^ (addr>>16); /* 4 -> 2 */
  tmp ^= (tmp>>8);                  /* 2 -> 1 */
  return (tmp^(tmp>>4)^af) & 0x0f;	    /* 1 -> 1/2 */
}


/*
 * get hash key for supplied net alias type and address
 * nat must be !NULL
 * the purpose here is to map a net_alias_type and a generic
 * address to a hash code.
 */

static __inline__ int
nat_hash_key(struct net_alias_type *nat, struct sockaddr *sa)
{
  return HASH(nat_addr32(nat,sa), sa->sa_family);
}


/*
 * change net_alias_type number of attachments (bindings)
 */

static int
nat_attach_chg(struct net_alias_type *nat, int delta)
{
  unsigned long flags;
  int n_at;
  if (!nat) return -1;
  save_flags(flags);
  cli();
  n_at = nat->n_attach + delta;
  if (n_at < 0)
  {
    restore_flags(flags);
    printk(KERN_WARNING "net_alias: tried to set n_attach < 0 for (family==%d) nat object.\n",
	   nat->type);
    return -1;
  }
  nat->n_attach = n_at;
  restore_flags(flags);
  return 0;
}


/*
 * bind alias to its type (family) object and call initialization hook
 */

static __inline__ int
nat_bind(struct net_alias_type *nat,struct net_alias *alias, struct sockaddr *sa)
{
  if (nat->alias_init_1) nat->alias_init_1(nat, alias, sa);
  return nat_attach_chg(nat, +1);
}


/*
 * unbind alias from type object and call alias destructor
 */

static __inline__ int
nat_unbind(struct net_alias_type *nat, struct net_alias *alias)
{
  if (nat->alias_done_1) nat->alias_done_1(nat, alias);
  return nat_attach_chg(nat, -1);
}


/*
 * compare device address with given. if NULL nat->dev_addr_chk,
 * compare dev->pa_addr with (sockaddr_in) 32 bits address (IP-ish)
 */

static __inline__ int nat_dev_addr_chk_1(struct net_alias_type *nat,
				   struct device *dev, struct sockaddr *sa)
{
  if (nat->dev_addr_chk)
    return nat->dev_addr_chk(nat, dev, sa);
  else
    return (dev->pa_addr == (*(struct sockaddr_in *)sa).sin_addr.s_addr);
}


/*
 * alias device init()
 * do nothing.
 */

static int 
net_alias_devinit(struct device *dev)
{
#ifdef ALIAS_USER_LAND_DEBUG
  printk("net_alias_devinit(%s) called.\n", dev->name);
#endif
  return 0;
}


/*
 * hard_start_xmit() should not be called.
 * ignore ... but shout!.
 */

static int
net_alias_hard_start_xmit(struct sk_buff *skb, struct device *dev)
{
  printk(KERN_WARNING "net_alias: net_alias_hard_start_xmit() for %s called (ignored)!!\n", dev->name);
  dev_kfree_skb(skb, FREE_WRITE);
  return 0;
}


static int
net_alias_open(struct device * dev)
{
  return 0;
}

static int
net_alias_close(struct device * dev)
{
  return 0;
}

/*
 * setups a new (alias) device 
 */

static int
net_alias_devsetup(struct net_alias *alias, struct net_alias_type *nat,
		struct sockaddr *sa)
{
  struct device *main_dev;
  struct device *dev;
  int family;
  int i;

  /*
   *
   * generic device setup based on main_dev info
   *
   * FIXME: is NULL bitwise 0 for all Linux platforms?
   */
  
  main_dev = alias->main_dev;
  dev = &alias->dev;
  memset(dev, '\0', sizeof(struct device));
  family = (sa)? sa->sa_family : main_dev->family;

  dev->alias_info = NULL;	/* no aliasing recursion */
  dev->my_alias = alias;	/* point to alias */
  dev->name = alias->name;
  dev->type = main_dev->type;
  dev->open = net_alias_open;
  dev->stop = net_alias_close;
  dev->hard_header_len = main_dev->hard_header_len;
  memcpy(dev->broadcast, main_dev->broadcast, MAX_ADDR_LEN);
  memcpy(dev->dev_addr, main_dev->dev_addr, MAX_ADDR_LEN);
  dev->addr_len = main_dev->addr_len;
  dev->init = net_alias_devinit;
  dev->hard_start_xmit = net_alias_hard_start_xmit;
  dev->flags = main_dev->flags & NET_ALIAS_IFF_MASK & ~IFF_UP;

  /*
   * only makes sense if same family
   */
  
  if (family == main_dev->family)
  {
    dev->metric = main_dev->metric;
    dev->mtu = main_dev->mtu;
    dev->pa_alen = main_dev->pa_alen;
    dev->hard_header = main_dev->hard_header;
    dev->rebuild_header = main_dev->rebuild_header;
  }
  
  /*
   *	Fill in the generic fields of the device structure.
   *    not actually used, avoids some dev.c #ifdef's
   */
  
  for (i = 0; i < DEV_NUMBUFFS; i++)
    skb_queue_head_init(&dev->buffs[i]);
  
  dev->family = family;
  return 0;
}


/*
 * slow alias find (parse the whole hash_tab)
 * returns: alias' pointer address
 */

static struct net_alias **
net_alias_slow_findp(struct net_alias_info *alias_info, struct net_alias *alias)
{
  unsigned idx, n_aliases;
  struct net_alias **aliasp;

  /*
   * for each alias_info's hash_tab entry, for every alias ...
   */
  
  n_aliases = alias_info->n_aliases;
  for (idx=0; idx < 16 ; idx++)
    for (aliasp = &alias_info->hash_tab[idx];*aliasp;aliasp = &(*aliasp)->next)
      if (*aliasp == alias)
	return aliasp;
      else
	if (--n_aliases == 0) break; /* faster give up */
  return NULL;
}


/*
 * create alias device for main_dev with given slot num.
 * if sa==NULL will create a same_family alias device 
 */

static struct device *
net_alias_dev_create(struct device *main_dev, int slot, int *err, struct sockaddr *sa, void *data)
{
  struct net_alias_info *alias_info;
  struct net_alias *alias, **aliasp;
  struct net_alias_type *nat;
  struct device *dev;
  unsigned long flags;
  int family;
  __u32 addr32;
  
  /* FIXME: lock */
  alias_info = main_dev->alias_info;

  /*
   * if NULL address given, take family from main_dev
   */
  
  family = (sa)? sa->sa_family : main_dev->family;
  
  /*
   * check if wanted family has a net_alias_type object registered
   */
  
  nat = nat_getbytype(family);
  if (!nat) {
#ifdef CONFIG_KERNELD
    char modname[20];
    sprintf (modname,"netalias-%d", family);
    request_module(modname);

    nat = nat_getbytype(family);
    if (!nat) {
#endif
      printk(KERN_WARNING "net_alias_dev_create(%s:%d): unregistered family==%d\n",
             main_dev->name, slot, family);
      /* *err = -EAFNOSUPPORT; */
      *err = -EINVAL;
      return NULL;
#ifdef CONFIG_KERNELD
    }
#endif
  }
  
  /*
   * do not allow creation over downed devices
   */

  *err = -EIO;
  
  if (! (main_dev->flags & IFF_UP) )
    return NULL;
  
  /*
   * if first alias, must also create alias_info
   */
      
  *err = -ENOMEM;

  if (!alias_info)
  { 
    alias_info = kmalloc(sizeof(struct net_alias_info), GFP_KERNEL);
    if (!alias_info) return NULL; /* ENOMEM */
    memset(alias_info, 0, sizeof(struct net_alias_info));
  }
  
  if (!(alias = kmalloc(sizeof(struct net_alias), GFP_KERNEL)))
    return NULL;		/* ENOMEM */

  /*
   * FIXME: is NULL bitwise 0 for all Linux platforms?
   */
  
  memset(alias, 0, sizeof(struct net_alias));
  alias->slot = slot;
  alias->main_dev = main_dev;
  alias->nat = nat;
  alias->next = NULL;
  alias->data = data;
  sprintf(alias->name, "%s:%d", main_dev->name, slot);
  
  /*
   * initialise alias' device structure
   */
  
  net_alias_devsetup(alias, nat, sa);

  dev = &alias->dev;
  
  save_flags(flags);
  cli();

  /*
   * bind alias to its object type
   * nat_bind calls nat->alias_init_1
   */
  
  nat_bind(nat, alias, sa);

  /*
   * if no address passed, take from device (could have been
   * set by nat->alias_init_1)
   */
  
  addr32 = (sa)? nat_addr32(nat, sa) : alias->dev.pa_addr;
  
  /*
   * store hash key in alias: will speed-up rehashing and deletion
   */
  
  alias->hash = HASH(addr32, family);

  /*
   * insert alias in hashed linked list
   */
  
  aliasp = &alias_info->hash_tab[alias->hash];
  alias->next = *aliasp;	
  *aliasp = alias;

  /*
   * if first alias ...
   */
  
  if (!alias_info->n_aliases++)
  {
    alias_info->taildev = main_dev;
    main_dev->alias_info = alias_info;
  }
  
  /*
   * add device at tail (just after last main_dev alias)
   */
  
  dev->next = alias_info->taildev->next;
  alias_info->taildev->next = dev;
  alias_info->taildev = dev;
  restore_flags(flags);
  return dev;
}


/*
 * delete one main_dev alias (referred by its slot num)
 */

static struct device *
net_alias_dev_delete(struct device *main_dev, int slot, int *err)
{
  struct net_alias_info *alias_info;
  struct net_alias *alias, **aliasp;
  struct device *dev;
  unsigned n_aliases;
  unsigned long flags;
  struct net_alias_type *nat;
  struct device *prevdev;
  
  /* FIXME: lock */
  *err = -ENODEV;
  
  if (main_dev == NULL) return NULL;

  /*
   * does main_dev have aliases?
   */
  
  alias_info = main_dev->alias_info;
  if (!alias_info) return NULL;	/* ENODEV */
  
  n_aliases = alias_info->n_aliases;
  
  /*
   * find device that holds the same slot number (could also
   * be strcmp() ala dev_get).
   */
  
  for (prevdev=main_dev, alias = NULL;prevdev->next && n_aliases; prevdev = prevdev->next)
  {
    if (!(alias = prevdev->next->my_alias))
    {
      printk(KERN_ERR "net_alias_dev_delete(): incorrect non-alias device after maindev\n");
      continue;			/* or should give up? */
    }
    if (alias->slot == slot) break;
    alias = NULL;
    n_aliases--;
  }
  
  if (!alias) return NULL;	/* ENODEV */
  
  dev = &alias->dev;
  
  /*
   * find alias hashed entry
   */
  
  for(aliasp = &alias_info->hash_tab[alias->hash]; *aliasp; aliasp = &(*aliasp)->next)
    if(*aliasp == alias) break;

  /*
   * if not found (???), try a full search
   */
  
  if (*aliasp != alias)
    if ((aliasp = net_alias_slow_findp(alias_info, alias)))
      printk(KERN_WARNING "net_alias_dev_delete(%s): bad hashing recovered\n", alias->name);
    else
    {
      printk(KERN_ERR "net_alias_dev_delete(%s): unhashed alias!\n",alias->name);
      return NULL;		/* ENODEV */
    }
  
  nat = alias->nat;

  save_flags(flags);
  cli();
  
  /*
   * unbind alias from alias_type obj.
   */
  
  nat_unbind(nat, alias);
  
  /*
   * is alias at tail?
   */
  
  if ( dev == alias_info->taildev )
    alias_info->taildev = prevdev;
  
  /*
   * unlink and close device
   */
  prevdev->next = dev->next;
  dev_close(dev);
  
  /*
   * unlink alias
   */
  
  *aliasp = (*aliasp)->next;

  if (--alias_info->n_aliases == 0) /* last alias */
    main_dev->alias_info = NULL;
  restore_flags(flags);

  /*
   * now free structures
   */
  
  kfree_s(alias, sizeof(struct net_alias));
  if (main_dev->alias_info == NULL)
    kfree_s(alias_info, sizeof(struct net_alias_info));

  /*
   * deletion ok (*err=0), NULL device returned.
   */
  
  *err = 0;
  return NULL;
}

/*
 * free all main device aliasing stuff
 * will be called on dev_close(main_dev)
 */

static void
net_alias_free(struct device *main_dev)
{
  struct net_alias_info *alias_info;
  struct net_alias *alias;
  struct net_alias_type *nat;
  struct device *dev;
  unsigned long flags;

  /*
   * do I really have aliases?
   */
  
  if (!(alias_info = main_dev->alias_info))    return;

  /*
   * fast device link "short-circuit": set main_dev->next to
   * device after last alias
   */
  
  save_flags(flags);
  cli();
  
  dev =  main_dev->next;
  main_dev->next = alias_info->taildev->next;
  main_dev->alias_info = NULL;
  alias_info->taildev->next = NULL;
  
  restore_flags(flags);

  /*
   * loop over alias devices, free and dev_close()
   */
  
  while (dev)
  {
    if (net_alias_is(dev))
    {
      alias = dev->my_alias;
      if (alias->main_dev == main_dev)
      {
	/*
	 * unbind alias from alias_type object
	 */
	
	nat = alias->nat;
	if (nat)
	{
	  nat_unbind(nat, alias);
	} /*  else error/printk ??? */
	
	dev_close(dev);
	dev = dev->next;
	
	kfree_s(alias, sizeof(struct net_alias));
	continue;
      }
      else
	printk(KERN_ERR "net_alias_free(%s): '%s' is not my alias\n",
	       main_dev->name, alias->name);
    }
    else
      printk(KERN_ERR "net_alias_free(%s): found a non-alias after device!\n",
	     main_dev->name);
    dev = dev->next;
  }
  
  kfree_s(alias_info, sizeof(alias_info));
  return;
}

/*
 * dev_get() with added alias naming magic.
 */

struct device *
net_alias_dev_get(char *dev_name, int aliasing_ok, int *err,
		  struct sockaddr *sa, void *data)
{
  struct device *dev;
  char *sptr,*eptr;
  int slot = 0;
  int delete = 0;
  
  *err = -ENODEV;
  if ((dev=dev_get(dev_name)))
    return dev;

  /*
   * want alias naming magic?
   */
  
  if (!aliasing_ok) return NULL;

  if (!dev_name || !*dev_name)
    return NULL;
  
  /*
   * find the first ':' , must be followed by, at least, 1 char
   */
  
  for (sptr=dev_name ; *sptr ; sptr++) if(*sptr==':') break;
  if (!*sptr || !*(sptr+1))
    return NULL;
  
  /*
   * seems to be an alias name, fetch main device
   */
  
  *sptr='\0';
  if (!(dev=dev_get(dev_name)))
    return NULL;
  *sptr++=':';
  
  /*
   * fetch slot number
   */
  
  slot = simple_strtoul(sptr,&eptr,10);
  if (slot >= NET_ALIAS_MAX_SLOT)
    return NULL;

  /*
   * if last char is '-', it is a deletion request
   */
  
  if (eptr[0] == '-' && !eptr[1] ) delete++;
  else if (eptr[0])
    return NULL;
  
  /*
   * well... let's work.
   */
  
  if (delete)
    return net_alias_dev_delete(dev, slot, err);
  else
    return net_alias_dev_create(dev, slot, err, sa, data);
}


/*
 * rehash alias device with address supplied. 
 */

int
net_alias_dev_rehash(struct device *dev, struct sockaddr *sa)
{
  struct net_alias_info *alias_info;
  struct net_alias *alias, **aliasp;
  struct device *main_dev;
  unsigned long flags;
  struct net_alias_type *o_nat, *n_nat;
  unsigned n_hash;

  /*
   * defensive ...
   */
  
  if (dev == NULL) return -1;
  if ( (alias = dev->my_alias) == NULL ) return -1;
  
  if (!sa)
  {
    printk(KERN_ERR "net_alias_rehash(): NULL sockaddr passed\n");
    return -1;
  }

  /*
   * defensive. should not happen.
   */

  if ( (main_dev = alias->main_dev) == NULL )
  {
    printk(KERN_ERR "net_alias_rehash for %s: NULL maindev\n", alias->name);
    return -1;
  }

  /*
   * defensive. should not happen.
   */

  if (!(alias_info=main_dev->alias_info))
  {
    printk(KERN_ERR "net_alias_rehash for %s: NULL alias_info\n", alias->name);
    return -1;
  }
  
  /*
   * will the request also change device family?
   */
  
  o_nat = alias->nat;
  if (!o_nat)
  {
    printk(KERN_ERR "net_alias_rehash(%s): unbound alias.\n", alias->name);
    return -1;
  }

  /*
   * point to new alias_type obj.
   */
  
  if (o_nat->type == sa->sa_family)
    n_nat = o_nat;
  else
  {
    n_nat = nat_getbytype(sa->sa_family);
    if (!n_nat)
    {
      printk(KERN_ERR "net_alias_rehash(%s): unreg family==%d.\n", alias->name, sa->sa_family);
      return -1;
    }
  }
  
  /*
   * new hash key. if same as old AND same type (family) return;
   */
  
  n_hash = nat_hash_key(n_nat, sa);
  if (n_hash == alias->hash && o_nat == n_nat )
    return 0;

  /*
   * find alias in hashed list
   */
  
  for (aliasp = &alias_info->hash_tab[alias->hash]; *aliasp; aliasp = &(*aliasp)->next)
    if (*aliasp == alias) break;
  
  /*
   * not found (???). try a full search
   */
  
  if(!*aliasp)
    if ((aliasp = net_alias_slow_findp(alias_info, alias)))
      printk(KERN_WARNING "net_alias_rehash(%s): bad hashing recovered\n", alias->name);
    else
    {
      printk(KERN_ERR "net_alias_rehash(%s): unhashed alias!\n", alias->name);
      return -1;
    }
  
  save_flags(flags);
  cli();
  
  /*
   * if type (family) changed, unlink from old type object (o_nat)
   * will call o_nat->alias_done_1()
   */
  
  if (o_nat != n_nat)
    nat_unbind(o_nat, alias);

  /*
   * if diff hash key, change alias position in hashed list
   */
  
  if (n_hash != alias->hash)
  {
    *aliasp = (*aliasp)->next;
    alias->hash = n_hash;
    aliasp = &alias_info->hash_tab[n_hash];
    alias->next = *aliasp;
    *aliasp = alias;
  }
  
  /*
   * if type (family) changed link to new type object (n_nat)
   * will call n_nat->alias_init_1()
   */
  
  if (o_nat != n_nat)
    nat_bind(n_nat, alias, sa);
  
  restore_flags(flags);
  return 0;
}




/*
 *  implements /proc/net/alias_types entry
 *  shows net_alias_type objects registered.
 */

int net_alias_types_getinfo(char *buffer, char **start, off_t offset, int length, int dummy)
{
  off_t pos=0, begin=0;
  int len=0;
  struct net_alias_type *nat;
  unsigned idx;
  len=sprintf(buffer,"type    name            n_attach\n");
  for (idx=0 ; idx < 16 ; idx++)
    for (nat = nat_base[idx]; nat ; nat = nat->next)
    {
      len += sprintf(buffer+len, "%-7d %-15s %-7d\n",
		     nat->type, nat->name,nat->n_attach);
      pos=begin+len;
      if(pos<offset)
      {
	len=0;
	begin=pos;
      }
      if(pos>offset+length)
	break;
    }
  *start=buffer+(offset-begin);
  len-=(offset-begin);
  if(len>length)
    len=length;	
  return len;
}


/*
 *  implements /proc/net/aliases entry, shows alias devices.
 *   calls alias nat->alias_print_1 if not NULL and formats everything
 *   to a fixed rec. size without using local (stack) buffers
 *
 */

#define NET_ALIASES_RECSIZ 64
int net_alias_getinfo(char *buffer, char **start, off_t offset, int length, int dummy)
{
  off_t pos=0, begin=0;
  int len=0;
  int dlen;
  struct net_alias_type *nat;
  struct net_alias *alias;
  struct device *dev;

  len=sprintf(buffer,"%-*s\n",NET_ALIASES_RECSIZ-1,"device           family address");
  for (dev = dev_base; dev ; dev = dev->next)
    if (net_alias_is(dev))
    {
      alias = dev->my_alias;
      nat = alias->nat;
      dlen=sprintf(buffer+len, "%-16s %-6d ", alias->name, alias->dev.family);
      
      /*
       * call alias_type specific print function.
       */
      
      if (nat->alias_print_1)
	dlen += nat->alias_print_1(nat, alias, buffer+len+dlen, NET_ALIASES_RECSIZ - dlen);
      else
	dlen += sprintf(buffer+len+dlen, "-");

      /*
       * fill with spaces if needed 
       */
      
      if (dlen < NET_ALIASES_RECSIZ) memset(buffer+len+dlen, ' ', NET_ALIASES_RECSIZ - dlen);
      /*
       * truncate to NET_ALIASES_RECSIZ
       */
      
      len += NET_ALIASES_RECSIZ;
      buffer[len-1] = '\n';
      
      pos=begin+len;
      if(pos<offset)
      {
	len=0;
	begin=pos;
      }
      if(pos>offset+length)
	break;
    }
  *start=buffer+(offset-begin);
  len-=(offset-begin);
  if(len>length)
    len=length;	
  return len;
}


/*
 * notifier for devices events
 */

int net_alias_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
  struct device *dev = ptr;

  if (event == NETDEV_DOWN)
  {
#ifdef ALIAS_USER_LAND_DEBUG
    printk("net_alias: NETDEV_DOWN for %s received\n", dev->name);
#endif
    if (net_alias_has(dev))
      net_alias_free(dev);
  }

  if (event == NETDEV_UP)
  {
#ifdef ALIAS_USER_LAND_DEBUG
    printk("net_alias: NETDEV_UP for %s received\n", dev->name);
#endif
    dev->alias_info = 0;
  }

  return NOTIFY_DONE;
}


/*
 * device aliases address comparison workhorse
 * no checks for nat and alias_info, must be !NULL
 */

static __inline__ struct device *
nat_addr_chk(struct net_alias_type *nat, struct net_alias_info *alias_info, struct sockaddr *sa, int flags_on, int flags_off)
{
  struct net_alias *alias;
  for(alias = alias_info->hash_tab[nat_hash_key(nat,sa)];
      alias; alias = alias->next)
  {
    if (alias->dev.family != sa->sa_family) continue;

    /*
     * nat_dev_addr_chk_1 will call type specific address cmp function.
     */
    
    if (alias->dev.flags & flags_on && !(alias->dev.flags & flags_off) &&
	nat_dev_addr_chk_1(nat,&alias->dev,sa))
      return &alias->dev;
  }
  return NULL;
}

/*
 * nat_addr_chk enough for protocols whose addr is (fully) stored at pa_addr.
 * note that nat pointer is ignored because of static comparison.
 */

static __inline__ struct device *
nat_addr_chk32(struct net_alias_type *nat, struct net_alias_info *alias_info, int family, __u32 addr32, int flags_on, int flags_off)
{
  struct net_alias *alias;
  for (alias=alias_info->hash_tab[HASH(addr32,family)];
       alias; alias=alias->next)
  {
    if (alias->dev.family != family) continue;
    
    /*
     * "hard" (static) comparison between addr32 and pa_addr.
     */
    
    if (alias->dev.flags & flags_on && !(alias->dev.flags & flags_off) &&
	addr32 == alias->dev.pa_addr)
      return &alias->dev;
  }
  return NULL;
}

/*
 * returns alias device with specified address AND flags_on AND flags_off,
 * else NULL.
 * intended for main devices.
 */

struct device *
net_alias_dev_chk(struct device *main_dev, struct sockaddr *sa,int flags_on, int flags_off)
{
  struct net_alias_info *alias_info = main_dev->alias_info;
  struct net_alias_type *nat;
  
  /*
   * only if main_dev has aliases
   */

  if (!alias_info) return NULL;
  
  /*
   * get alias_type object for sa->sa_family.
   */
  
  nat = nat_getbytype(sa->sa_family);
  if (!nat)
    return NULL;

  return nat_addr_chk(nat, alias_info, sa, flags_on, flags_off);
}

/*
 * net_alias_dev_chk enough for protocols whose addr is (fully) stored
 * at pa_addr.
 */

struct device *
net_alias_dev_chk32(struct device *main_dev, int family, __u32 addr32,
		int flags_on, int flags_off)
{
  struct net_alias_info *alias_info = main_dev->alias_info;
  
  /*
   * only if main_dev has aliases
   */

  if (!alias_info) return NULL;
  
  return nat_addr_chk32(NULL, alias_info, family, addr32, flags_on, flags_off);
}


/*
 * select closest (main or alias) device to <src,dst> addresses given. if no
 * further info is available, return main_dev (for easier calling arrangement).
 *
 * Should be called early at xxx_rcv() time for device selection
 */

struct device *
net_alias_dev_rcv_sel(struct device *main_dev, struct sockaddr *sa_src, struct sockaddr *sa_dst)
{
  int family;
  struct net_alias_type *nat;
  struct net_alias_info *alias_info;
  struct device *dev;
  
  if (main_dev == NULL) return NULL;

  /*
   * if not aliased, don't bother any more
   */

  if ((alias_info = main_dev->alias_info) == NULL)
    return main_dev;

  /*
   * find out family
   */

  family = (sa_src)? sa_src->sa_family : ((sa_dst)? sa_dst->sa_family : AF_UNSPEC);
  if (family == AF_UNSPEC) return main_dev;

  /*
   * get net_alias_type object for this family
   */

  if ( (nat = nat_getbytype(family)) == NULL ) return main_dev;
  
  /*
   * first step: find out if dst addr is main_dev's or one of its aliases'
   */

  if (sa_dst)
  {
    if (nat_dev_addr_chk_1(nat, main_dev,sa_dst))
      return main_dev;

    dev = nat_addr_chk(nat, alias_info, sa_dst, IFF_UP, 0);

    if (dev != NULL) return dev;
  }

  /*
   * second step: find the rcv addr 'closest' alias through nat method call
   */

  if ( sa_src == NULL || nat->dev_select == NULL) return main_dev;
  dev = nat->dev_select(nat, main_dev, sa_src);

  if (dev == NULL || dev->family != family) return main_dev;
  
  /*
   * dev ok only if it is alias of main_dev
   */
  
  dev = net_alias_is(dev)?
    ( (dev->my_alias->main_dev == main_dev)? dev : NULL) : NULL;

  /*
   * do not return NULL.
   */
  
  return (dev)? dev : main_dev;

}

/*
 * dev_rcv_sel32: dev_rcv_sel for 'pa_addr' protocols.
 */

struct device *
net_alias_dev_rcv_sel32(struct device *main_dev, int family, __u32 src, __u32 dst)
{
  struct net_alias_type *nat;
  struct net_alias_info *alias_info;
  struct sockaddr_in sin_src;
  struct device *dev;
  
  if (main_dev == NULL) return NULL;

  /*
   * if not aliased, don't bother any more
   */

  if ((alias_info = main_dev->alias_info) == NULL)
    return main_dev;

  /*
   * early return if dst is main_dev's address
   */
  
  if (dst == main_dev->pa_addr)
    return main_dev;
  
  if (family == AF_UNSPEC) return main_dev;

  /*
   * get net_alias_type object for this family
   */

  if ( (nat = nat_getbytype(family)) == NULL ) return main_dev;
  
  /*
   * first step: find out if dst address one of main_dev aliases'
   */
  
  if (dst)
  {
    dev = nat_addr_chk32(nat, alias_info, family, dst, IFF_UP, 0);
    if (dev) return dev;
  }
  
  /*
   * second step: find the rcv addr 'closest' alias through nat method call
   */

  if ( src == 0 || nat->dev_select == NULL) return main_dev;

  sin_src.sin_family = family;
  sin_src.sin_addr.s_addr = src;
    
  dev = nat->dev_select(nat, main_dev, (struct sockaddr *)&sin_src);

  if (dev == NULL || dev->family != family) return main_dev;
  
  /*
   * dev ok only if it is alias of main_dev
   */
  
  dev = net_alias_is(dev)?
    ( (dev->my_alias->main_dev == main_dev)? dev : NULL) : NULL;

  /*
   * do not return NULL.
   */
  
  return (dev)? dev : main_dev;
  
}


/*
 * device event hook
 */

static struct notifier_block net_alias_dev_notifier = {
  net_alias_device_event,
  NULL,
  0
};


/*
 * net_alias initialisation
 * called from net_dev_init().
 */

void net_alias_init(void)
{

  /*
   * register dev events notifier
   */
  
  register_netdevice_notifier(&net_alias_dev_notifier);

  /*
   * register /proc/net entries
   */
  
#ifndef ALIAS_USER_LAND_DEBUG
  proc_net_register(&(struct proc_dir_entry) {
    PROC_NET_ALIAS_TYPES, 11, "alias_types",
    S_IFREG | S_IRUGO, 1, 0, 0,
    0, &proc_net_inode_operations,
    net_alias_types_getinfo
  });
  proc_net_register(&(struct proc_dir_entry) {
    PROC_NET_ALIASES, 7, "aliases",
    S_IFREG | S_IRUGO, 1, 0, 0,
    0, &proc_net_inode_operations,
    net_alias_getinfo
  });
#endif
  
}

/*
 * net_alias type object registering func.
 */
int register_net_alias_type(struct net_alias_type *nat, int type)
{
  unsigned hash;
  unsigned long flags;
  if (!nat)
  {
    printk(KERN_ERR "register_net_alias_type(): NULL arg\n");
    return -EINVAL;
  }
  nat->type = type;
  nat->n_attach = 0;
  hash = nat->type & 0x0f;
  save_flags(flags);
  cli();
  nat->next = nat_base[hash];
  nat_base[hash] = nat;
  restore_flags(flags);
  return 0;
}

/*
 * net_alias type object unreg.
 */
int unregister_net_alias_type(struct net_alias_type *nat)
{
  struct net_alias_type **natp;
  unsigned hash;
  unsigned long flags;
  
  if (!nat)
  {
    printk(KERN_ERR "unregister_net_alias_type(): NULL arg\n");
    return -EINVAL;
  }

  /*
   * only allow unregistration if it has no attachments
   */
  if (nat->n_attach)
  {
    printk(KERN_ERR "unregister_net_alias_type(): has %d attachments. failed\n",
	   nat->n_attach);
    return -EINVAL;
  }
  hash = nat->type & 0x0f;
  save_flags(flags);
  cli();
  for (natp = &nat_base[hash]; *natp ; natp = &(*natp)->next)
  {
    if (nat==(*natp))
    {
      *natp = nat->next;
      restore_flags(flags);
      return 0;
    }
  }
  restore_flags(flags);
  printk(KERN_ERR "unregister_net_alias_type(type=%d): not found!\n", nat->type);
  return -EINVAL;
}

