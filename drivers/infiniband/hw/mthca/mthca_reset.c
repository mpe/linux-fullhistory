/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: mthca_reset.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "mthca_dev.h"
#include "mthca_cmd.h"

int mthca_reset(struct mthca_dev *mdev)
{
	int i;
	int err = 0;
	u32 *hca_header    = NULL;
	u32 *bridge_header = NULL;
	struct pci_dev *bridge = NULL;

#define MTHCA_RESET_OFFSET 0xf0010
#define MTHCA_RESET_VALUE  cpu_to_be32(1)

	/*
	 * Reset the chip.  This is somewhat ugly because we have to
	 * save off the PCI header before reset and then restore it
	 * after the chip reboots.  We skip config space offsets 22
	 * and 23 since those have a special meaning.
	 *
	 * To make matters worse, for Tavor (PCI-X HCA) we have to
	 * find the associated bridge device and save off its PCI
	 * header as well.
	 */

	if (mdev->hca_type == TAVOR) {
		/* Look for the bridge -- its device ID will be 2 more
		   than HCA's device ID. */
		while ((bridge = pci_get_device(mdev->pdev->vendor,
						mdev->pdev->device + 2,
						bridge)) != NULL) {
			if (bridge->hdr_type    == PCI_HEADER_TYPE_BRIDGE &&
			    bridge->subordinate == mdev->pdev->bus) {
				mthca_dbg(mdev, "Found bridge: %s (%s)\n",
					  pci_pretty_name(bridge), pci_name(bridge));
				break;
			}
		}

		if (!bridge) {
			/*
			 * Didn't find a bridge for a Tavor device --
			 * assume we're in no-bridge mode and hope for
			 * the best.
			 */
			mthca_warn(mdev, "No bridge found for %s (%s)\n",
				  pci_pretty_name(mdev->pdev), pci_name(mdev->pdev));
		}

	}

	/* For Arbel do we need to save off the full 4K PCI Express header?? */
	hca_header = kmalloc(256, GFP_KERNEL);
	if (!hca_header) {
		err = -ENOMEM;
		mthca_err(mdev, "Couldn't allocate memory to save HCA "
			  "PCI header, aborting.\n");
		goto out;
	}

	for (i = 0; i < 64; ++i) {
		if (i == 22 || i == 23)
			continue;
		if (pci_read_config_dword(mdev->pdev, i * 4, hca_header + i)) {
			err = -ENODEV;
			mthca_err(mdev, "Couldn't save HCA "
				  "PCI header, aborting.\n");
			goto out;
		}
	}

	if (bridge) {
		bridge_header = kmalloc(256, GFP_KERNEL);
		if (!bridge_header) {
			err = -ENOMEM;
			mthca_err(mdev, "Couldn't allocate memory to save HCA "
				  "bridge PCI header, aborting.\n");
			goto out;
		}

		for (i = 0; i < 64; ++i) {
			if (i == 22 || i == 23)
				continue;
			if (pci_read_config_dword(bridge, i * 4, bridge_header + i)) {
				err = -ENODEV;
				mthca_err(mdev, "Couldn't save HCA bridge "
					  "PCI header, aborting.\n");
				goto out;
			}
		}
	}

	/* actually hit reset */
	{
		void __iomem *reset = ioremap(pci_resource_start(mdev->pdev, 0) +
					      MTHCA_RESET_OFFSET, 4);

		if (!reset) {
			err = -ENOMEM;
			mthca_err(mdev, "Couldn't map HCA reset register, "
				  "aborting.\n");
			goto out;
		}

		writel(MTHCA_RESET_VALUE, reset);
		iounmap(reset);
	}

	/* Docs say to wait one second before accessing device */
	msleep(1000);

	/* Now wait for PCI device to start responding again */
	{
		u32 v;
		int c = 0;

		for (c = 0; c < 100; ++c) {
			if (pci_read_config_dword(bridge ? bridge : mdev->pdev, 0, &v)) {
				err = -ENODEV;
				mthca_err(mdev, "Couldn't access HCA after reset, "
					  "aborting.\n");
				goto out;
			}

			if (v != 0xffffffff)
				goto good;

			msleep(100);
		}

		err = -ENODEV;
		mthca_err(mdev, "PCI device did not come back after reset, "
			  "aborting.\n");
		goto out;
	}

good:
	/* Now restore the PCI headers */
	if (bridge) {
		/*
		 * Bridge control register is at 0x3e, so we'll
		 * naturally restore it last in this loop.
		 */
		for (i = 0; i < 16; ++i) {
			if (i * 4 == PCI_COMMAND)
				continue;

			if (pci_write_config_dword(bridge, i * 4, bridge_header[i])) {
				err = -ENODEV;
				mthca_err(mdev, "Couldn't restore HCA bridge reg %x, "
					  "aborting.\n", i);
				goto out;
			}
		}

		if (pci_write_config_dword(bridge, PCI_COMMAND,
					   bridge_header[PCI_COMMAND / 4])) {
			err = -ENODEV;
			mthca_err(mdev, "Couldn't restore HCA bridge COMMAND, "
				  "aborting.\n");
			goto out;
		}
	}

	for (i = 0; i < 16; ++i) {
		if (i * 4 == PCI_COMMAND)
			continue;

		if (pci_write_config_dword(mdev->pdev, i * 4, hca_header[i])) {
			err = -ENODEV;
			mthca_err(mdev, "Couldn't restore HCA reg %x, "
				  "aborting.\n", i);
			goto out;
		}
	}

	if (pci_write_config_dword(mdev->pdev, PCI_COMMAND,
				   hca_header[PCI_COMMAND / 4])) {
		err = -ENODEV;
		mthca_err(mdev, "Couldn't restore HCA COMMAND, "
			  "aborting.\n");
		goto out;
	}

out:
	if (bridge)
		pci_dev_put(bridge);
	kfree(bridge_header);
	kfree(hca_header);

	return err;
}
