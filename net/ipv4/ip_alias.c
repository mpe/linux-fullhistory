#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/if.h>
#include <linux/inet.h>

#ifdef ALIAS_USER_LAND_DEBUG
#include "net_alias.h"
#include "ip_alias.h"
#include "user_stubs.h"
#endif

#include <linux/net_alias.h>
#include <net/ip_alias.h>

/*
 * AF_INET alias init
 */
static int 
ip_alias_init_1(struct net_alias *alias, struct sockaddr *sa)
{
#ifdef ALIAS_USER_LAND_DEBUG
  printk("alias_init(%s) called.\n", alias->name);
#endif
  MOD_INC_USE_COUNT;
  return 0;
}

/*
 * AF_INET alias done
 */
static int
ip_alias_done_1(struct net_alias *alias)
{
#ifdef ALIAS_USER_LAND_DEBUG
  printk("alias_done(%s) called.\n", alias->name);
#endif
  MOD_DEC_USE_COUNT;
  return 0;
}

/*
 * print address info
 */

int
ip_alias_print_1(char *buf, int len, struct net_alias *alias)
{
  char *p;

  p = (char *) &alias->dev.pa_addr;
  return sprintf(buf, "%d.%d.%d.%d",
		 (p[0] & 255), (p[1] & 255), (p[2] & 255), (p[3] & 255));
}

/*
 * net_alias AF_INET type defn.
 */

struct net_alias_type ip_alias_type =
{
  AF_INET,			/* type */
  0,				/* n_attach */
  "ip",				/* name */
  NULL,				/* get_addr32() */
  NULL,				/* addr_chk() */
  ip_alias_init_1,		/* alias_init_1() */
  ip_alias_done_1,		/* alias_done_1() */
  ip_alias_print_1,		/* alias_print_1() */
  NULL				/* next */
};

/*
 * ip_alias module initialization
 */

int ip_alias_init(void)
{
  return register_net_alias_type(&ip_alias_type, AF_INET);
}

/*
 * ip_alias module done
 */

int ip_alias_done(void)
{
  return unregister_net_alias_type(&ip_alias_type);
}

#ifdef MODULE

int init_module(void)
{
  if (ip_alias_init() != 0)
    return -EIO;
  return 0;
}

void cleanup_module(void)
{
  if (ip_alias_done() != 0)
    printk("ip_alias: can't remove module");
}

#endif /* MODULE */
