#include <linux/config.h>
#include <linux/module.h>

#include "mpc.h"
#include "lec.h"

/*
 * lane_mpoa_init.c: A couple of helper functions
 * to make modular LANE and MPOA client easier to implement
 */

/*
 * This is how it goes:
 *
 * if xxxx is not compiled as module, call atm_xxxx_init_ops()
 *    from here
 * else call atm_mpoa_init_ops() from init_module() within
 *    the kernel when xxxx module is loaded
 *
 * In either case function pointers in struct atm_xxxx_ops
 * are initialized to their correct values. Either they
 * point to functions in the module or in the kernel
 */
 
extern struct atm_mpoa_ops atm_mpoa_ops; /* in common.c */
extern struct atm_lane_ops atm_lane_ops; /* in common.c */

#if defined(CONFIG_ATM_MPOA) || defined(CONFIG_ATM_MPOA_MODULE)
void atm_mpoa_init(void)
{
#ifndef CONFIG_ATM_MPOA_MODULE /* not module */
        atm_mpoa_init_ops(&atm_mpoa_ops);
#endif

        return;
}
#endif

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
struct net_bridge_fdb_entry *(*br_fdb_get_hook)(struct net_bridge *br,
						unsigned char *addr) = NULL;
void (*br_fdb_put_hook)(struct net_bridge_fdb_entry *ent) = NULL;
#if defined(CONFIG_ATM_LANE_MODULE) || defined(CONFIG_BRIDGE_MODULE)
EXPORT_SYMBOL(br_fdb_get_hook);
EXPORT_SYMBOL(br_fdb_put_hook);
#endif /* defined(CONFIG_ATM_LANE_MODULE) || defined(CONFIG_BRIDGE_MODULE) */
#endif /* defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE) */

void atm_lane_init(void)
{
#ifndef CONFIG_ATM_LANE_MODULE /* not module */
        atm_lane_init_ops(&atm_lane_ops);
#endif

        return;
}        
#endif
