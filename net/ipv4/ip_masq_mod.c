/*
 *		IP_MASQ_MOD masq modules support
 *
 *
 * Version:	@(#)ip_masq_mod.c  0.02      97/10/30
 *
 * Author:	Juan Jose Ciarlante, <jjciarla@raiz.uncu.edu.ar>
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Changes:
 *		Cyrus Durgin:		fixed kerneld stuff for kmod.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <net/ip_masq.h>
#include <net/ip_masq_mod.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

EXPORT_SYMBOL(register_ip_masq_mod);
EXPORT_SYMBOL(unregister_ip_masq_mod);
EXPORT_SYMBOL(ip_masq_mod_lkp_link);
EXPORT_SYMBOL(ip_masq_mod_lkp_unlink);

/*
 *	Base pointer for registered modules
 */
struct ip_masq_mod * ip_masq_mod_reg_base = NULL;

/*
 *	Base pointer for lookup (subset of above, a module could be
 *	registered, but it could have no active rule); will avoid
 *	unnecessary lookups.
 */
struct ip_masq_mod * ip_masq_mod_lkp_base = NULL;

int ip_masq_mod_register_proc(struct ip_masq_mod *mmod)
{
#ifdef CONFIG_PROC_FS        
	int ret;

	struct proc_dir_entry *ent = mmod->mmod_proc_ent;

	if (!ent) 
		return 0;
	if (!ent->name) {
		ent->name = mmod->mmod_name;
		ent->namelen = strlen (mmod->mmod_name);
	}
	ret = proc_net_register(ent);
	if (ret) mmod->mmod_proc_ent = NULL;

	return ret;
#else
	return 0;
#endif
}

void ip_masq_mod_unregister_proc(struct ip_masq_mod *mmod)
{
#ifdef CONFIG_PROC_FS        
	struct proc_dir_entry *ent = mmod->mmod_proc_ent;
	if (!ent)
		return;
	proc_unregister(proc_net, ent->low_ino);
#endif
}

/*
 *	Link/unlink object for lookups
 */

int ip_masq_mod_lkp_unlink(struct ip_masq_mod *mmod)
{
	struct ip_masq_mod **mmod_p;

	start_bh_atomic();

	for (mmod_p = &ip_masq_mod_lkp_base; *mmod_p ; mmod_p = &(*mmod_p)->next)
		if (mmod == (*mmod_p))  {
			*mmod_p = mmod->next;
			mmod->next = NULL;
			end_bh_atomic();
			return 0;
		}

	end_bh_atomic();
	return -EINVAL;
}

int ip_masq_mod_lkp_link(struct ip_masq_mod *mmod)
{
	start_bh_atomic();

	mmod->next = ip_masq_mod_lkp_base;
	ip_masq_mod_lkp_base=mmod;

	end_bh_atomic();
	return 0;
}

int register_ip_masq_mod(struct ip_masq_mod *mmod)
{
	if (!mmod) {
		IP_MASQ_ERR("register_ip_masq_mod(): NULL arg\n");
		return -EINVAL;
	}
	if (!mmod->mmod_name) {
		IP_MASQ_ERR("register_ip_masq_mod(): NULL mmod_name\n");
		return -EINVAL;
	}
	ip_masq_mod_register_proc(mmod);

	mmod->next_reg = ip_masq_mod_reg_base;
	ip_masq_mod_reg_base=mmod;

	return 0;
}

int unregister_ip_masq_mod(struct ip_masq_mod *mmod)
{
	struct ip_masq_mod **mmod_p;

	if (!mmod) {
		IP_MASQ_ERR( "unregister_ip_masq_mod(): NULL arg\n");
		return -EINVAL;
	}

	/*
	 * 	Only allow unregistration if it is not referenced
	 */
	if (atomic_read(&mmod->refcnt))  {
		IP_MASQ_ERR( "unregister_ip_masq_mod(): is in use by %d guys. failed\n",
				atomic_read(&mmod->refcnt));
		return -EINVAL;
	}

	/*	
	 *	Must be already unlinked from lookup list
	 */
	if (mmod->next) {
		IP_MASQ_WARNING("MASQ: unregistering \"%s\" while in lookup list.fixed.",
			mmod->mmod_name);
		ip_masq_mod_lkp_unlink(mmod);
	}

	for (mmod_p = &ip_masq_mod_reg_base; *mmod_p ; mmod_p = &(*mmod_p)->next_reg)
		if (mmod == (*mmod_p))  {
			ip_masq_mod_unregister_proc(mmod);
			*mmod_p = mmod->next_reg;
			return 0;
		}

	IP_MASQ_ERR("unregister_ip_masq_mod(%s): not linked \n", mmod->mmod_name);
	return -EINVAL;
}

int ip_masq_mod_in_rule(struct iphdr *iph, __u16 *portp)
{
	struct ip_masq_mod *mmod;
	int ret;

	for (mmod=ip_masq_mod_lkp_base;mmod;mmod=mmod->next) {
		if (!mmod->mmod_in_rule) continue;
		switch (ret=mmod->mmod_in_rule(iph, portp)) {
			case IP_MASQ_MOD_NOP:
				continue;
			case IP_MASQ_MOD_ACCEPT:
				return 1;
			case IP_MASQ_MOD_REJECT:
				return -1;
		}
	}
	return 0;
}

int ip_masq_mod_out_rule(struct iphdr *iph, __u16 *portp)
{
	struct ip_masq_mod *mmod;
	int ret;

	for (mmod=ip_masq_mod_lkp_base;mmod;mmod=mmod->next) {
		if (!mmod->mmod_out_rule) continue;
		switch (ret=mmod->mmod_out_rule(iph, portp)) {
			case IP_MASQ_MOD_NOP:
				continue;
			case IP_MASQ_MOD_ACCEPT:
				return 1;
			case IP_MASQ_MOD_REJECT:
				return -1;
		}
	}
	return 0;
}

struct ip_masq * ip_masq_mod_in_create(struct iphdr *iph, __u16 *portp, __u32 maddr)
{
	struct ip_masq_mod *mmod;
	struct ip_masq *ms;

	for (mmod=ip_masq_mod_lkp_base;mmod;mmod=mmod->next) {
		if (!mmod->mmod_in_create) continue;
		if ((ms=mmod->mmod_in_create(iph, portp, maddr))) {
			return ms;
		}
	}
	return NULL;
}

struct ip_masq * ip_masq_mod_out_create(struct iphdr *iph, __u16 *portp, __u32 maddr)
{
	struct ip_masq_mod *mmod;
	struct ip_masq *ms;

	for (mmod=ip_masq_mod_lkp_base;mmod;mmod=mmod->next) {
		if (!mmod->mmod_out_create) continue;
		if ((ms=mmod->mmod_out_create(iph, portp, maddr))) {
			return ms;
		}
	}
	return NULL;
}

int ip_masq_mod_in_update(struct iphdr *iph, __u16 *portp, struct ip_masq *ms)
{
	struct ip_masq_mod *mmod;
	int ret;

	for (mmod=ip_masq_mod_lkp_base;mmod;mmod=mmod->next) {
		if (!mmod->mmod_in_update) continue;
		switch (ret=mmod->mmod_in_update(iph, ms)) {
			case IP_MASQ_MOD_NOP:
				continue;
			case IP_MASQ_MOD_ACCEPT:
				return 1;
			case IP_MASQ_MOD_REJECT:
				return -1;
		}
	}
	return 0;
}

int ip_masq_mod_out_update(struct iphdr *iph, __u16 *portp,  struct ip_masq *ms)
{
	struct ip_masq_mod *mmod;
	int ret;

	for (mmod=ip_masq_mod_lkp_base;mmod;mmod=mmod->next) {
		if (!mmod->mmod_out_update) continue;
		switch (ret=mmod->mmod_out_update(iph, portp, ms)) {
			case IP_MASQ_MOD_NOP:
				continue;
			case IP_MASQ_MOD_ACCEPT:
				return 1;
			case IP_MASQ_MOD_REJECT:
				return -1;
		}
	}
	return 0;
}

struct ip_masq_mod * ip_masq_mod_getbyname(const char *mmod_name)
{
	struct ip_masq_mod * mmod;

	IP_MASQ_DEBUG(1, "searching mmod_name \"%s\"\n", mmod_name);
	
	for (mmod=ip_masq_mod_reg_base; mmod ; mmod=mmod->next) {
		if (mmod->mmod_ctl && *(mmod_name)
				&& (strcmp(mmod_name, mmod->mmod_name)==0)) {
			/* HIT */
			return mmod;
		}
	}
	return NULL;
}

/*
 *	Module control entry
 *	no need to lock (already locked in ip_masq.c)
 */
int ip_masq_mod_ctl(int optname, struct ip_fw_masqctl *mctl, int optlen)
{
	struct ip_masq_mod * mmod;
#ifdef CONFIG_KMOD
	char kmod_name[IP_MASQ_MOD_NMAX+8];
#endif
	/* tappo */
	mctl->u.mod.name[IP_MASQ_MOD_NMAX-1] = 0;

	mmod = ip_masq_mod_getbyname(mctl->u.mod.name);
	if (mmod)
		return mmod->mmod_ctl(optname, mctl, optlen);
#ifdef CONFIG_KMOD
	sprintf(kmod_name,"ip_masq_%s", mctl->u.mod.name);

	IP_MASQ_DEBUG(1, "About to request \"%s\" module\n", kmod_name);

	/* 
	 *	Let sleep for a while ...
	 */
	request_module(kmod_name);
	mmod = ip_masq_mod_getbyname(mctl->u.mod.name);
	if (mmod)
		return mmod->mmod_ctl(optname, mctl, optlen);
#endif
	return ESRCH;
}
