/*
 * $Id: chipreg.c,v 1.12 2001/10/02 15:29:53 dwmw2 Exp $
 *
 * Registration for chip drivers
 *
 */

#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/kmod.h>
#include <linux/spinlock.h>
#include <linux/mtd/compatmac.h>
#include <linux/mtd/map.h>

spinlock_t chip_drvs_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(chip_drvs_list);

void register_mtd_chip_driver(struct mtd_chip_driver *drv)
{
	spin_lock(&chip_drvs_lock);
	list_add(&drv->list, &chip_drvs_list);
	spin_unlock(&chip_drvs_lock);
}

void unregister_mtd_chip_driver(struct mtd_chip_driver *drv)
{
	spin_lock(&chip_drvs_lock);
	list_del(&drv->list);
	spin_unlock(&chip_drvs_lock);
}

static struct mtd_chip_driver *get_mtd_chip_driver (char *name)
{
	struct list_head *pos;
	struct mtd_chip_driver *ret = NULL, *this;

	spin_lock(&chip_drvs_lock);

	list_for_each(pos, &chip_drvs_list) {
		this = list_entry(pos, typeof(*this), list);
		
		if (!strcmp(this->name, name)) {
			ret = this;
			break;
		}
	}
	if (ret && !try_inc_mod_count(ret->module)) {
		/* Eep. Failed. */
		ret = NULL;
	}

	spin_unlock(&chip_drvs_lock);

	return ret;
}

	/* Hide all the horrid details, like some silly person taking
	   get_module_symbol() away from us, from the caller. */

struct mtd_info *do_map_probe(char *name, struct map_info *map)
{
	struct mtd_chip_driver *drv;
	struct mtd_info *ret;

	drv = get_mtd_chip_driver(name);

	if (!drv && !request_module(name))
		drv = get_mtd_chip_driver(name);

	if (!drv)
		return NULL;

	ret = drv->probe(map);
#ifdef CONFIG_MODULES
	/* We decrease the use count here. It may have been a 
	   probe-only module, which is no longer required from this
	   point, having given us a handle on (and increased the use
	   count of) the actual driver code.
	*/
	if(drv->module)
		__MOD_DEC_USE_COUNT(drv->module);
#endif

	if (ret)
		return ret;
	
	return NULL;
}

EXPORT_SYMBOL(register_mtd_chip_driver);
EXPORT_SYMBOL(unregister_mtd_chip_driver);
EXPORT_SYMBOL(do_map_probe);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Core routines for registering and invoking MTD chip drivers");
