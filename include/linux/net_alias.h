#ifndef _NET_ALIAS_H
#define _NET_ALIAS_H

#include <linux/types.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/inet.h>
#include <linux/in.h>		/* for default IP behavior */


/*
 * max. alias slot number allowed 
 */

#define NET_ALIAS_MAX_SLOT  256

struct net_alias;
struct net_alias_info;
struct net_alias_type;


/*
 * main alias structure
 * note that *defines* dev & devname
 */

struct net_alias
{
  struct device dev;		/* alias device defn*/
  char name[IFNAMSIZ];		/* device name defn */
  unsigned hash;		/* my hash value: for quick rehash */
  unsigned slot;		/* slot number */
  void *data;			/* private data */
  struct device *main_dev;	/* pointer to main device */
  struct net_alias_type *nat;	/* alias type bound */
  struct net_alias *next;	/* next alias (hashed linked list) */
};


/*
 *  alias structure pointed by main device
 *  it holds main device's alias hash table
 */

struct net_alias_info
{
  int n_aliases;		/* num aliases */
  struct device *taildev;	/* my last (alias) device */
  struct net_alias *hash_tab[16]; /* hashed alias table */
};

/*
 * net_alias_type class
 * declares a generic (AF_ independent) structure that will
 * manage generic to family-specific behavior.
 */

struct net_alias_type
{
  int type;			/* aliasing type: address family */
  int n_attach;	 		/* number of aliases attached */
  char name[16];		/* af_name */
  __u32 (*get_addr32)		/* get __u32 addr 'representation'*/
    (struct sockaddr*);	
  int (*addr_chk)		/* address checking func: */
    (struct device *, struct sockaddr *);
  int (*alias_init_1)		/* called after alias creation: */
    (struct net_alias *alias, struct sockaddr *sa);
  int (*alias_done_1)		/* called before alias deletion */
    (struct net_alias *alias);
  int (*alias_print_1)	
    (char *buf, int len, struct net_alias *alias);
  struct net_alias_type *next;	/* link */
};


/*
 * is dev an alias?
 */

static __inline__ int
net_alias_is(struct device *dev)
{
  return (dev->my_alias != 0);
}


/*
 * does dev have aliases?
 */

static __inline__ int
net_alias_has(struct device *dev)
{
  return (dev->alias_info != 0);
}


extern void net_alias_init(void);

extern struct device * net_alias_dev_get(char *dev_name, int aliasing_ok, int *err, struct sockaddr *sa, void *data);
extern int net_alias_rehash(struct net_alias *alias, struct sockaddr *sa);

extern int net_alias_getinfo(char *buf, char **, off_t , int , int );
extern int net_alias_types_getinfo(char *buf, char **, off_t , int , int );

extern int register_net_alias_type(struct net_alias_type *nat, int type);
extern int unregister_net_alias_type(struct net_alias_type *nat);

extern struct device * net_alias_chk(struct device *dev, struct sockaddr *sa, int flags_1, int flags_0);
extern struct device * net_alias_chk32(struct device *dev, int family, __u32 addr32, int flags_1, int flags_0);


/*
 * returns MY 'true' main device
 * intended for alias devices
 */

static __inline__ struct device *net_alias_main_dev(struct device *dev)
{
  return (net_alias_is(dev))? dev->my_alias->main_dev : dev;
}


/*
 * returns NEXT 'true' device
 * intended for true devices
 */

static __inline__ struct device *
net_alias_nextdev(struct device *dev)
{
  return (dev->alias_info)? dev->alias_info->taildev->next : dev->next;
}


/*
 * sets NEXT 'true' device
 * intended for main devices (treat main device as block: dev+aliases).
 */

static __inline__ struct device *
net_alias_nextdev_set(struct device *dev, struct device *nextdev)
{
  struct device *pdev = dev;
  if (net_alias_has(dev))
  {
    pdev = dev->alias_info->taildev; /* point to last dev alias */
  }
  pdev->next = nextdev;
  return nextdev;
}


/*
 * addr_chk wrapper: check given generic address with (UP) aliases
 */

static __inline__ struct device *
net_alias_addr_chk(struct device *dev, struct sockaddr *sa)
{
  return net_alias_chk(dev, sa, IFF_UP, 0);
}


/*
 * addr_chk32 wrapper: check given u32 address with (UP) aliases
 */

static __inline__ struct device *
net_alias_addr_chk32(struct device *dev, int family, __u32 addr32)
{
  return net_alias_chk32(dev, family, addr32, IFF_UP, 0);
}

#endif  /* _NET_ALIAS_H */
