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
 * $Id: mthca_mcg.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/init.h>

#include "mthca_dev.h"
#include "mthca_cmd.h"

enum {
	MTHCA_QP_PER_MGM = 4 * (MTHCA_MGM_ENTRY_SIZE / 16 - 2)
};

struct mthca_mgm {
	u32 next_gid_index;
	u32 reserved[3];
	u8  gid[16];
	u32 qp[MTHCA_QP_PER_MGM];
};

static const u8 zero_gid[16];	/* automatically initialized to 0 */

/*
 * Caller must hold MCG table semaphore.  gid and mgm parameters must
 * be properly aligned for command interface.
 *
 *  Returns 0 unless a firmware command error occurs.
 *
 * If GID is found in MGM or MGM is empty, *index = *hash, *prev = -1
 * and *mgm holds MGM entry.
 *
 * if GID is found in AMGM, *index = index in AMGM, *prev = index of
 * previous entry in hash chain and *mgm holds AMGM entry.
 *
 * If no AMGM exists for given gid, *index = -1, *prev = index of last
 * entry in hash chain and *mgm holds end of hash chain.
 */
static int find_mgm(struct mthca_dev *dev,
		    u8 *gid, struct mthca_mgm *mgm,
		    u16 *hash, int *prev, int *index)
{
	void *mailbox;
	u8 *mgid;
	int err;
	u8 status;

	mailbox = kmalloc(16 + MTHCA_CMD_MAILBOX_EXTRA, GFP_KERNEL);
	if (!mailbox)
		return -ENOMEM;
	mgid = MAILBOX_ALIGN(mailbox);

	memcpy(mgid, gid, 16);

	err = mthca_MGID_HASH(dev, mgid, hash, &status);
	if (err)
		goto out;
	if (status) {
		mthca_err(dev, "MGID_HASH returned status %02x\n", status);
		err = -EINVAL;
		goto out;
	}

	if (0)
		mthca_dbg(dev, "Hash for %04x:%04x:%04x:%04x:"
			  "%04x:%04x:%04x:%04x is %04x\n",
			  be16_to_cpu(((u16 *) gid)[0]), be16_to_cpu(((u16 *) gid)[1]),
			  be16_to_cpu(((u16 *) gid)[2]), be16_to_cpu(((u16 *) gid)[3]),
			  be16_to_cpu(((u16 *) gid)[4]), be16_to_cpu(((u16 *) gid)[5]),
			  be16_to_cpu(((u16 *) gid)[6]), be16_to_cpu(((u16 *) gid)[7]),
			  *hash);

	*index = *hash;
	*prev  = -1;

	do {
		err = mthca_READ_MGM(dev, *index, mgm, &status);
		if (err)
			goto out;
		if (status) {
			mthca_err(dev, "READ_MGM returned status %02x\n", status);
			return -EINVAL;
		}

		if (!memcmp(mgm->gid, zero_gid, 16)) {
			if (*index != *hash) {
				mthca_err(dev, "Found zero MGID in AMGM.\n");
				err = -EINVAL;
			}
			goto out;
		}

		if (!memcmp(mgm->gid, gid, 16))
			goto out;

		*prev = *index;
		*index = be32_to_cpu(mgm->next_gid_index) >> 5;
	} while (*index);

	*index = -1;

 out:
	kfree(mailbox);
	return err;
}

int mthca_multicast_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct mthca_dev *dev = to_mdev(ibqp->device);
	void *mailbox;
	struct mthca_mgm *mgm;
	u16 hash;
	int index, prev;
	int link = 0;
	int i;
	int err;
	u8 status;

	mailbox = kmalloc(sizeof *mgm + MTHCA_CMD_MAILBOX_EXTRA, GFP_KERNEL);
	if (!mailbox)
		return -ENOMEM;
	mgm = MAILBOX_ALIGN(mailbox);

	if (down_interruptible(&dev->mcg_table.sem))
		return -EINTR;

	err = find_mgm(dev, gid->raw, mgm, &hash, &prev, &index);
	if (err)
		goto out;

	if (index != -1) {
		if (!memcmp(mgm->gid, zero_gid, 16))
			memcpy(mgm->gid, gid->raw, 16);
	} else {
		link = 1;

		index = mthca_alloc(&dev->mcg_table.alloc);
		if (index == -1) {
			mthca_err(dev, "No AMGM entries left\n");
			err = -ENOMEM;
			goto out;
		}

		err = mthca_READ_MGM(dev, index, mgm, &status);
		if (err)
			goto out;
		if (status) {
			mthca_err(dev, "READ_MGM returned status %02x\n", status);
			err = -EINVAL;
			goto out;
		}

		memcpy(mgm->gid, gid->raw, 16);
		mgm->next_gid_index = 0;
	}

	for (i = 0; i < MTHCA_QP_PER_MGM; ++i)
		if (!(mgm->qp[i] & cpu_to_be32(1 << 31))) {
			mgm->qp[i] = cpu_to_be32(ibqp->qp_num | (1 << 31));
			break;
		}

	if (i == MTHCA_QP_PER_MGM) {
		mthca_err(dev, "MGM at index %x is full.\n", index);
		err = -ENOMEM;
		goto out;
	}

	err = mthca_WRITE_MGM(dev, index, mgm, &status);
	if (err)
		goto out;
	if (status) {
		mthca_err(dev, "WRITE_MGM returned status %02x\n", status);
		err = -EINVAL;
	}

	if (!link)
		goto out;

	err = mthca_READ_MGM(dev, prev, mgm, &status);
	if (err)
		goto out;
	if (status) {
		mthca_err(dev, "READ_MGM returned status %02x\n", status);
		err = -EINVAL;
		goto out;
	}

	mgm->next_gid_index = cpu_to_be32(index << 5);

	err = mthca_WRITE_MGM(dev, prev, mgm, &status);
	if (err)
		goto out;
	if (status) {
		mthca_err(dev, "WRITE_MGM returned status %02x\n", status);
		err = -EINVAL;
	}

 out:
	up(&dev->mcg_table.sem);
	kfree(mailbox);
	return err;
}

int mthca_multicast_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct mthca_dev *dev = to_mdev(ibqp->device);
	void *mailbox;
	struct mthca_mgm *mgm;
	u16 hash;
	int prev, index;
	int i, loc;
	int err;
	u8 status;

	mailbox = kmalloc(sizeof *mgm + MTHCA_CMD_MAILBOX_EXTRA, GFP_KERNEL);
	if (!mailbox)
		return -ENOMEM;
	mgm = MAILBOX_ALIGN(mailbox);

	if (down_interruptible(&dev->mcg_table.sem))
		return -EINTR;

	err = find_mgm(dev, gid->raw, mgm, &hash, &prev, &index);
	if (err)
		goto out;

	if (index == -1) {
		mthca_err(dev, "MGID %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x "
			  "not found\n",
			  be16_to_cpu(((u16 *) gid->raw)[0]),
			  be16_to_cpu(((u16 *) gid->raw)[1]),
			  be16_to_cpu(((u16 *) gid->raw)[2]),
			  be16_to_cpu(((u16 *) gid->raw)[3]),
			  be16_to_cpu(((u16 *) gid->raw)[4]),
			  be16_to_cpu(((u16 *) gid->raw)[5]),
			  be16_to_cpu(((u16 *) gid->raw)[6]),
			  be16_to_cpu(((u16 *) gid->raw)[7]));
		err = -EINVAL;
		goto out;
	}

	for (loc = -1, i = 0; i < MTHCA_QP_PER_MGM; ++i) {
		if (mgm->qp[i] == cpu_to_be32(ibqp->qp_num | (1 << 31)))
			loc = i;
		if (!(mgm->qp[i] & cpu_to_be32(1 << 31)))
			break;
	}

	if (loc == -1) {
		mthca_err(dev, "QP %06x not found in MGM\n", ibqp->qp_num);
		err = -EINVAL;
		goto out;
	}

	mgm->qp[loc]   = mgm->qp[i - 1];
	mgm->qp[i - 1] = 0;

	err = mthca_WRITE_MGM(dev, index, mgm, &status);
	if (err)
		goto out;
	if (status) {
		mthca_err(dev, "WRITE_MGM returned status %02x\n", status);
		err = -EINVAL;
		goto out;
	}

	if (i != 1)
		goto out;

	goto out;

	if (prev == -1) {
		/* Remove entry from MGM */
		if (be32_to_cpu(mgm->next_gid_index) >> 5) {
			err = mthca_READ_MGM(dev,
					     be32_to_cpu(mgm->next_gid_index) >> 5,
					     mgm, &status);
			if (err)
				goto out;
			if (status) {
				mthca_err(dev, "READ_MGM returned status %02x\n",
					  status);
				err = -EINVAL;
				goto out;
			}
		} else
			memset(mgm->gid, 0, 16);

		err = mthca_WRITE_MGM(dev, index, mgm, &status);
		if (err)
			goto out;
		if (status) {
			mthca_err(dev, "WRITE_MGM returned status %02x\n", status);
			err = -EINVAL;
			goto out;
		}
	} else {
		/* Remove entry from AMGM */
		index = be32_to_cpu(mgm->next_gid_index) >> 5;
		err = mthca_READ_MGM(dev, prev, mgm, &status);
		if (err)
			goto out;
		if (status) {
			mthca_err(dev, "READ_MGM returned status %02x\n", status);
			err = -EINVAL;
			goto out;
		}

		mgm->next_gid_index = cpu_to_be32(index << 5);

		err = mthca_WRITE_MGM(dev, prev, mgm, &status);
		if (err)
			goto out;
		if (status) {
			mthca_err(dev, "WRITE_MGM returned status %02x\n", status);
			err = -EINVAL;
			goto out;
		}
	}

 out:
	up(&dev->mcg_table.sem);
	kfree(mailbox);
	return err;
}

int __devinit mthca_init_mcg_table(struct mthca_dev *dev)
{
	int err;

	err = mthca_alloc_init(&dev->mcg_table.alloc,
			       dev->limits.num_amgms,
			       dev->limits.num_amgms - 1,
			       0);
	if (err)
		return err;

	init_MUTEX(&dev->mcg_table.sem);

	return 0;
}

void __devexit mthca_cleanup_mcg_table(struct mthca_dev *dev)
{
	mthca_alloc_cleanup(&dev->mcg_table.alloc);
}
