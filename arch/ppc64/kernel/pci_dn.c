/*
 * pci_dn.c
 *
 * Copyright (C) 2001 Todd Inglett, IBM Corporation
 *
 * PCI manipulation via device_nodes.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *    
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/pSeries_reconfig.h>

#include "pci.h"

/*
 * Traverse_func that inits the PCI fields of the device node.
 * NOTE: this *must* be done before read/write config to the device.
 */
static void * __devinit update_dn_pci_info(struct device_node *dn, void *data)
{
	struct pci_controller *phb = data;
	int *type = (int *)get_property(dn, "ibm,pci-config-space-type", NULL);
	u32 *regs;

	dn->phb = phb;
	regs = (u32 *)get_property(dn, "reg", NULL);
	if (regs) {
		/* First register entry is addr (00BBSS00)  */
		dn->busno = (regs[0] >> 16) & 0xff;
		dn->devfn = (regs[0] >> 8) & 0xff;
	}

	dn->pci_ext_config_space = (type && *type == 1);
	return NULL;
}

/*
 * Traverse a device tree stopping each PCI device in the tree.
 * This is done depth first.  As each node is processed, a "pre"
 * function is called and the children are processed recursively.
 *
 * The "pre" func returns a value.  If non-zero is returned from
 * the "pre" func, the traversal stops and this value is returned.
 * This return value is useful when using traverse as a method of
 * finding a device.
 *
 * NOTE: we do not run the func for devices that do not appear to
 * be PCI except for the start node which we assume (this is good
 * because the start node is often a phb which may be missing PCI
 * properties).
 * We use the class-code as an indicator. If we run into
 * one of these nodes we also assume its siblings are non-pci for
 * performance.
 */
void *traverse_pci_devices(struct device_node *start, traverse_func pre,
		void *data)
{
	struct device_node *dn, *nextdn;
	void *ret;

	/* We started with a phb, iterate all childs */
	for (dn = start->child; dn; dn = nextdn) {
		u32 *classp, class;

		nextdn = NULL;
		classp = (u32 *)get_property(dn, "class-code", NULL);
		class = classp ? *classp : 0;

		if (pre && ((ret = pre(dn, data)) != NULL))
			return ret;

		/* If we are a PCI bridge, go down */
		if (dn->child && ((class >> 8) == PCI_CLASS_BRIDGE_PCI ||
				  (class >> 8) == PCI_CLASS_BRIDGE_CARDBUS))
			/* Depth first...do children */
			nextdn = dn->child;
		else if (dn->sibling)
			/* ok, try next sibling instead. */
			nextdn = dn->sibling;
		if (!nextdn) {
			/* Walk up to next valid sibling. */
			do {
				dn = dn->parent;
				if (dn == start)
					return NULL;
			} while (dn->sibling == NULL);
			nextdn = dn->sibling;
		}
	}
	return NULL;
}

void __devinit pci_devs_phb_init_dynamic(struct pci_controller *phb)
{
	struct device_node * dn = (struct device_node *) phb->arch_data;

	/* PHB nodes themselves must not match */
	dn->devfn = dn->busno = -1;
	dn->phb = phb;

	/* Update dn->phb ptrs for new phb and children devices */
	traverse_pci_devices(dn, update_dn_pci_info, phb);
}

/*
 * Traversal func that looks for a <busno,devfcn> value.
 * If found, the device_node is returned (thus terminating the traversal).
 */
static void *is_devfn_node(struct device_node *dn, void *data)
{
	int busno = ((unsigned long)data >> 8) & 0xff;
	int devfn = ((unsigned long)data) & 0xff;

	return ((devfn == dn->devfn) && (busno == dn->busno)) ? dn : NULL;
}

/*
 * This is the "slow" path for looking up a device_node from a
 * pci_dev.  It will hunt for the device under its parent's
 * phb and then update sysdata for a future fastpath.
 *
 * It may also do fixups on the actual device since this happens
 * on the first read/write.
 *
 * Note that it also must deal with devices that don't exist.
 * In this case it may probe for real hardware ("just in case")
 * and add a device_node to the device tree if necessary.
 *
 */
struct device_node *fetch_dev_dn(struct pci_dev *dev)
{
	struct device_node *orig_dn = dev->sysdata;
	struct pci_controller *phb = orig_dn->phb; /* assume same phb as orig_dn */
	struct device_node *phb_dn;
	struct device_node *dn;
	unsigned long searchval = (dev->bus->number << 8) | dev->devfn;

	phb_dn = phb->arch_data;
	dn = traverse_pci_devices(phb_dn, is_devfn_node, (void *)searchval);
	if (dn)
		dev->sysdata = dn;
	return dn;
}
EXPORT_SYMBOL(fetch_dev_dn);

static int pci_dn_reconfig_notifier(struct notifier_block *nb, unsigned long action, void *node)
{
	struct device_node *np = node;
	int err = NOTIFY_OK;

	switch (action) {
	case PSERIES_RECONFIG_ADD:
		update_dn_pci_info(np, np->parent->phb);
		break;
	default:
		err = NOTIFY_DONE;
		break;
	}
	return err;
}

static struct notifier_block pci_dn_reconfig_nb = {
	.notifier_call = pci_dn_reconfig_notifier,
};

/*
 * Actually initialize the phbs.
 * The buswalk on this phb has not happened yet.
 */
void __init pci_devs_phb_init(void)
{
	struct pci_controller *phb, *tmp;

	/* This must be done first so the device nodes have valid pci info! */
	list_for_each_entry_safe(phb, tmp, &hose_list, list_node)
		pci_devs_phb_init_dynamic(phb);

	pSeries_reconfig_notifier_register(&pci_dn_reconfig_nb);
}
