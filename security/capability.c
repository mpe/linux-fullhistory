/*
 *  Capabilities Linux Security Module
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/ptrace.h>

#ifdef CONFIG_SECURITY


static struct security_operations capability_ops = {
	.ptrace =			cap_ptrace,
	.capget =			cap_capget,
	.capset_check =			cap_capset_check,
	.capset_set =			cap_capset_set,
	.capable =			cap_capable,
	.netlink_send =			cap_netlink_send,
	.netlink_recv =			cap_netlink_recv,

	.bprm_compute_creds =		cap_bprm_compute_creds,
	.bprm_set_security =		cap_bprm_set_security,
	.bprm_secureexec =		cap_bprm_secureexec,

	.inode_setxattr =		cap_inode_setxattr,
	.inode_removexattr =		cap_inode_removexattr,

	.task_post_setuid =		cap_task_post_setuid,
	.task_reparent_to_init =	cap_task_reparent_to_init,

	.syslog =                       cap_syslog,

	.vm_enough_memory =             cap_vm_enough_memory,
};

#if defined(CONFIG_SECURITY_CAPABILITIES_MODULE)
#define MY_NAME THIS_MODULE->name
#else
#define MY_NAME "capability"
#endif

/* flag to keep track of how we were registered */
static int secondary;


static int __init capability_init (void)
{
	/* register ourselves with the security framework */
	if (register_security (&capability_ops)) {
		printk (KERN_INFO
			"Failure registering capabilities with the kernel\n");
		/* try registering with primary module */
		if (mod_reg_security (MY_NAME, &capability_ops)) {
			printk (KERN_INFO "Failure registering capabilities "
				"with primary security module.\n");
			return -EINVAL;
		}
		secondary = 1;
	}
	printk (KERN_INFO "Capability LSM initialized\n");
	return 0;
}

static void __exit capability_exit (void)
{
	/* remove ourselves from the security framework */
	if (secondary) {
		if (mod_unreg_security (MY_NAME, &capability_ops))
			printk (KERN_INFO "Failure unregistering capabilities "
				"with primary module.\n");
		return;
	}

	if (unregister_security (&capability_ops)) {
		printk (KERN_INFO
			"Failure unregistering capabilities with the kernel\n");
	}
}

security_initcall (capability_init);
module_exit (capability_exit);

MODULE_DESCRIPTION("Standard Linux Capabilities Security Module");
MODULE_LICENSE("GPL");

#endif	/* CONFIG_SECURITY */
